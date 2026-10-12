function localBranchLoop(count) {
  let i = 0;
  let total = 0;
  let flag = true;
  while (i < count) {
    if (flag) { total = total + 1; }
    flag = !flag;
    i = i + 1;
  }
  return total;
}

function repeatLocalBranchLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + localBranchLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatLocalBranchLoop(20, 5000);
