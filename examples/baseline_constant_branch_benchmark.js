function constantBranchLoop() {
  let i = 0;
  let total = 0;
  while (i < 5000) {
    if (i < 2500) { total = total + 1; }
    if (i > 2500) { total = total + 1; }
    if (i == 2500) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatConstantBranchLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + constantBranchLoop();
    round = round + 1;
  }
  return checksum;
}

repeatConstantBranchLoop(20);
