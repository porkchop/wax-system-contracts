const global_placement_counts = {};
const selections = {};
const MAX_HASH = 1000000000; //Number.MAX_SAFE_INTEGER;
const MAX_TRIES = 1000000;

function cmp(a,b) {
  return a - b;
}

for(let i = 0; i < MAX_TRIES; i++) {
  const hash = Math.floor(Math.random() * MAX_HASH);
  const standby_selected = hash % 21;
  const placement = hash % 36;
  // const placement = Math.floor(Math.random() * MAX_HASH) % 37;
  selections[standby_selected] = selections[standby_selected] || [];
  selections[standby_selected].push(placement);
  global_placement_counts[placement] = (global_placement_counts[placement] || 0) + 1;
}
// console.log(Object.keys(selections).sort());
for(let standby of Object.keys(selections).sort(cmp)) {
  // console.log(selections[standby]);
  const placement_counts = {};
  for(let placement of selections[standby]) {
    placement_counts[placement] = (placement_counts[placement] || 0) + 1;
  };
  console.log(`standby ${standby}, #positions: ${Object.keys(placement_counts).length}:`);
  for(let placement of Object.keys(placement_counts).sort(cmp)) {
    console.log(`\t${placement}: ${placement_counts[placement]}`);
  }
  // console.log(placement_counts);
}

console.log(`global placement:`);
for(let placement of Object.keys(global_placement_counts).sort(cmp)) {
  console.log(`\t${placement}: ${global_placement_counts[placement]}`);
}
// 
// console.log(MAX_HASH, global_placement_counts);
// 
// console.log()
// for(let i = 0; i < 36*3; i++) {
//   console.log(`${i}: ${i % 36}`);
// }
// 
// console.log()
// for(let i = 0; i < 36*3; i++) {
//   let v = Math.floor(Math.random() * MAX_HASH);
//   console.log(`${v}: ${v % 36}`);
// }