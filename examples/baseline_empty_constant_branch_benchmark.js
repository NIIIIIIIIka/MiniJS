function emptyConstantBranchLoop() {
  let i = 0;
  let total = 0;
  let empty = null;
  let absent = undefined;
  while (i < 5000) {
    if (empty == null) { total = total + 1; }
    if (empty == undefined) { total = total + 1; }
    if (absent == undefined) { total = total + 1; }
    if (absent == null) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatEmptyConstantBranchLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + emptyConstantBranchLoop();
    round = round + 1;
  }
  return checksum;
}

repeatEmptyConstantBranchLoop(20);
