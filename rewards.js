let performance_sum = 0;
// let performance_sample_size = 2016;
// let performance_sample_size = 1008;
let performance_sample_size = 30 * 12*21;
// let performance_sample_size = 4;
let block_height = 0;
let produced_count = 0;
let last_produced_at = 0;

function _produce_block(blocks_elapsed) {
  // blocks_elapsed = Math.min(blocks_elapsed, performance_sample_size);
  performance_sum = 1 + performance_sum * Math.pow(1 - 1 / performance_sample_size, blocks_elapsed);
  produced_count++;
  last_produced_at = block_height;
  getPerformance();
}

function produce_block(blocks_elapsed) {
  console.log('Jump blocks', blocks_elapsed)
  // blocks_elapsed = Math.min(blocks_elapsed, performance_sample_size);
  block_height += blocks_elapsed;
  if(shouldIProduce()) {
    _produce_block(blocks_elapsed);
  }
}

function shouldIProduce() {
  let slot = block_height % (21 * 12);
  return slot >= 0 && slot < 12;
}

function produce_blocks(num) {
  for(let i = 0; i < num; i++) {
    block_height++;
    // console.log(slot, block_height, last_produced_at, block_height - last_produced_at + 1)
    if(shouldIProduce()) {
    // if(true) {
      _produce_block(block_height - last_produced_at);
    }
  }
}

function _getPerformance() {
  // return performance_sum / ((1./21.) * performance_sample_size);
  return performance_sum * Math.pow(1 - 1 / performance_sample_size, block_height - last_produced_at) / (0.99 * (1./21.) * performance_sample_size);
  // return performance_sum / (0.99 * (1./21.) * performance_sample_size);
  // return performance_sum / performance_sample_size;
}

function getPerformance() {
  console.log("perf", _getPerformance(), produced_count, block_height);
  // console.log(performance_sum / performance_sample_size, performance_sum, produced_count, block_height);
}

function changeSampleSize(newSize) {
  let previousPerf = _getPerformance();
  performance_sample_size = newSize;
  // performance_sum *= previousPerf / _getPerformance();
  performance_sum = 0;
  console.log("New Sample Size", newSize, previousPerf, _getPerformance());
}

// produce_blocks(performance_sample_size / 4 - 1);
// produce_blocks(performance_sample_size / 2 - 1);
// produce_block(performance_sample_size / 4 + 1);
// changeSampleSize(performance_sample_size * 2)
// produce_blocks(performance_sample_size / 4 - 1);
// produce_blocks(performance_sample_size / 2 - 1);
// produce_block(performance_sample_size / 4 + 1);
// produce_blocks(performance_sample_size / 4 - 1);
// produce_blocks(performance_sample_size / 2 - 1);
// produce_block(performance_sample_size / 4 + 1);
// produce_blocks(performance_sample_size / 4 - 1);
// produce_blocks(performance_sample_size / 2 - 1);
// produce_block(performance_sample_size / 4 + 1);
// produce_blocks(performance_sample_size / 4 - 1);
// changeSampleSize(performance_sample_size / 2)
// produce_blocks(performance_sample_size / 2 - 1);
// changeSampleSize(performance_sample_size * 2)
// // produce_block(performance_sample_size / 4 + 1);
// produce_block(12*21*1);
// getPerformance();
// produce_block(12*21*1);
// getPerformance();
// produce_block(12*21*1);
// getPerformance();
// produce_block(12*21*1);
// getPerformance();
// produce_block(12*21*1);
// getPerformance();
produce_blocks(performance_sample_size * 1000);
produce_block(12*21*1);
getPerformance();
produce_block(12*21*1);
getPerformance();
produce_block(12*21*1);
getPerformance();
produce_block(12*21*1);
getPerformance();
produce_block(12*21*1);
getPerformance();

// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(1);
// produce_block(2);
// produce_block(15);
// console.log();
// console.log((1+3.831059455871582*Math.pow(1 - 1 / performance_sample_size, 5))/performance_sample_size)
// console.log(Math.pow(1 - 1 / performance_sample_size, 5))
// console.log();
// 
// // for(i = 0; i < 1000; i++) {
// //   produce_block(1);
// // }
// produce_block(1000);
// 1,0,0,1