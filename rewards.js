let blocks_performance_window = 30 * 12 * 21;
let block_height = 0;
let producers = [];
let standbys = [];
let current_producers = [];
const performances_sum_sample_size = 1000;
const num_performance_producers = 16;
let performances_sum = performances_sum_sample_size * 0.5;
let standy_production_ratio = 0.01;
let standby_speed = 5;
let producer_speed = 5;
const full_round = 12 * 21;

random_selection = true;
let next_standby_index = 0;
let next_standby_placement = 0;

let standby_count = 0;
let producer_count = 0;
let last_producer_schedule_update = 0;

const LIB_BLOCKS = 336;
let lib_count = 0;
let block_count = 0;
let last_standby_block_count = 0;

class Producer {
  constructor(name, is_standby = false) {
    this.name = name;
    this.rolling_blocks = 0;
    this.last_block = 0;
    this.blocks_performance_window = 1;
    this.unpaid_blocks = 0;
    this.is_standby = is_standby;
  }
  
  roll_blocks(block_time) {
   let blocks_elapsed = block_time - this.last_block;
   return this.rolling_blocks * Math.pow(1. - 1. / this.blocks_performance_window, blocks_elapsed);
  }

  get_performance(block_time) {
    let rolling_blocks = this.roll_blocks(block_time);
    if(this.is_standby) {
      // return sigmoid(rolling_blocks, standby_speed, 12 / standy_production_ratio * (1 / 36) / 2);
      return sigmoid(rolling_blocks, standby_speed, this.blocks_performance_window * standy_production_ratio * (1 / 36) / 2);
    } else {
      // return sigmoid(rolling_blocks, producer_speed, 12 / 0.99 * (1 / 21) / 2);
      return sigmoid(rolling_blocks, producer_speed, this.blocks_performance_window * 0.99 * (1 / 21) / 2);
    }
  }

  track_block(block_time, block_accuracy_sample_size) {
    if(block_accuracy_sample_size != this.blocks_performance_window) {
      this.blocks_performance_window = block_accuracy_sample_size;
      this.rolling_blocks = 0;
    }

    this.rolling_blocks = 1. + this.roll_blocks(block_time);
    this.last_block = block_time;
    this.unpaid_blocks++;
    
    if(this.is_standby) {
      last_standby_block_count = block_count;
      standby_count++;
      // console.log(this.name);
    } else {
      producer_count++;
    }
  }
  
  print_performance() {
    console.log(`${this.name} perf: ${this.get_performance(block_height).toFixed(8)}\trolling_blocks: ${this.rolling_blocks}\troll_blocks: ${this.roll_blocks(block_height).toFixed(8)}\tblock delta: ${block_height - this.last_block}\tunpaid: ${this.unpaid_blocks} ${this.unpaid_blocks%12}\tblock: ${block_height}`);
  }
}

function sigmoid(x, speed, x_translation) {
  // let speed = 0.1;
  let s = speed / x_translation;
  let X = s * (x - x_translation);
  return 0.5 * (1 + X / (1 + Math.abs(X)));
}

function calc_sigmoid_speed(r, v) {
  return (2 * r - 1) / ((2 - 2 * r) * (2 * v - 1))
}

function print_sigmoid_speed(r, v) {
  console.log(`sigmoid speed where r = ${r}, v = ${v} is s = ${calc_sigmoid_speed(r, v)}`);
}

for(let i = 1; i <= 21; i++) {
  producers.push(new Producer(`producer${i}`));
}

for(let i = 1; i <= 36; i++) {
  standbys.push(new Producer(`standby${i}`, true));
}

current_producers = producers.slice();

function check_schedule(block_height) {
  block_count++;
  if(++lib_count >= LIB_BLOCKS && block_height % 12 == 11) {
    lib_count = 0;
    
    current_producers = producers.slice();
    if(standby_count / block_count < standy_production_ratio) {
      let i;
      let j;
      if(random_selection) {
        i = Math.floor(Math.random() * standbys.length);
        j = Math.floor(Math.random() * producers.length);
      } else {
        i = next_standby_index;
        next_standby_index = (next_standby_index + 1) % standbys.length;
        j = next_standby_placement;
        next_standby_placement = (next_standby_placement + 1) % producers.length;
      }

      current_producers[j] = standbys[i];
    }
  }
}

function produce_block(slots_elapsed) {
  block_height += slots_elapsed;
  let current_producer = current_producers[Math.floor(block_height % (current_producers.length * 12) / 12)];
  current_producer.track_block(block_height, blocks_performance_window);
  check_schedule(block_height);
}

function produce_blocks(num) {
  for(let i = 0; i < num; i++) {
    produce_block(1);
  }
}

function update_performance(new_performance) {
  performances_sum += new_performance - performances_sum / performances_sum_sample_size;
}

function average_producers_performance() {
  return performances_sum / performances_sum_sample_size;
}

let my_producers = producers.concat(standbys);
// let my_producers = standbys.slice();

function calculate_producers_performance() {
  let producer_performances = [];

  for( let producer of my_producers ) {
    // let perf = Math.max(Math.min(1.0, producer.get_performance(block_height)), 0.5); // clamp everyones' perf to >= 0.5 and <= 1
    let perf = producer.get_performance(block_height);
    if(perf == -1.) {
      perf = average_producers_performance();
    }
    producer_performances.push(perf);
  }

  while(producer_performances.length < num_performance_producers) {
    producer_performances.push(average_producers_performance());
  }

  producer_performances.sort((a, b) => b - a);
  
  if(producer_performances.length > num_performance_producers) {
    producer_performances.splice(num_performance_producers, producer_performances.length);
  }

  let perfs = '';
  for(perf of producer_performances) {
    perfs += perf + ' ';
  }
  console.log('Individual performances:', perfs);

  let performance = producer_performances.reduce((s, p) => s + p, 0) / num_performance_producers;
  update_performance(performance);
  return performance;
}

function print_performance() {
  my_producers.forEach(p => p.print_performance());
  let standbys_performance = standbys.reduce((s, p) => s + p.get_performance(block_height), 0) / standbys.length;
  let producers_performance = producers.reduce((s, p) => s + p.get_performance(block_height), 0) / producers.length;
  console.log('Producers performance', producers_performance);
  console.log('Standbys performance', standbys_performance);
  console.log('My producers performance', calculate_producers_performance());
  console.log('Standby ratio', standby_count / producer_count);
}

// function show_sigmoid(x, 0) {
//   console.log(`sigmoid(${x}) = ${sigmoid(x)}`)
// }
// let values = [];
// const max_values = 100;
// const min_value = -10;
// const max_value = 10;
// // const min_value = 0;
// // const max_value = 100;
// for(let i = 0; i <= max_values; i++) {
//   let x = min_value + i * (max_value - min_value) / max_values
//   values.push(x);
// }
// values.forEach(show_sigmoid);

// standy_production_ratio = 0.04;
// blocks_performance_window = 100 * full_round;
// produce_blocks(500 * full_round);
// print_performance();

// standy_production_ratio = 0.04;
// blocks_performance_window = 600 * full_round;
// produce_blocks(3600 * full_round);
// print_performance();

random_selection = false;
standy_production_ratio = 0.01;
// blocks_performance_window = 100 * full_round;
blocks_performance_window = 43200;
// standby_speed = 5;
produce_blocks(3600 * 2.33 * full_round);
print_performance();

console.log('standby expected rolling_blocks', blocks_performance_window * standy_production_ratio * (1 / 36) / 2);
console.log('producer expected rolling_blocks', blocks_performance_window * 0.99 * (1 / 21) / 2);
console.log(`random_selection ${random_selection}, standy_production_ratio ${standy_production_ratio}, blocks_performance_window ${blocks_performance_window}, standby_speed ${standby_speed}, producer_speed ${producer_speed}`)
// blocks_performance_window = 21 * 12 * 100;
// produce_blocks(blocks_performance_window);
// print_performance();
// produce_blocks(blocks_performance_window);
// print_performance();
// produce_blocks(3 * blocks_performance_window);
// print_performance();
// produce_blocks(10 * blocks_performance_window);
// print_performance();
// produce_block(blocks_performance_window);
// print_performance();
// // produce_block(blocks_performance_window);
// // print_performance();
// // produce_blocks(blocks_performance_window);
// // print_performance();
// produce_blocks(blocks_performance_window);
// print_performance();

console.log(`blocks_performance_window hours ${blocks_performance_window / 60 / 60 / 2}`);
let expected_blocks = blocks_performance_window * standy_production_ratio * (1 / 36);
let rolling_blocks = expected_blocks * Math.pow(1. - 1. / blocks_performance_window, 36 * 12 / .01);
let sig = sigmoid(rolling_blocks, standby_speed, expected_blocks / 2)
console.log(`expected_blocks ${expected_blocks}, rolling_blocks ${rolling_blocks}, sigmoid = ${sig}`);
// print_sigmoid_speed(0.9, 0.9);
// print_sigmoid_speed(0.8, 0.8);
// print_sigmoid_speed(0.9, 0.8);
// print_sigmoid_speed(0.9, 0.7);
