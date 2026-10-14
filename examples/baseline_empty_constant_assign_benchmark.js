function emptyConstantAssignLoop() {
  let i = 0;
  let total = 0;
  let empty = null;
  let absent = undefined;
  let flag = false;
  while (i < 5000) {
    flag = empty == null;
    if (flag) { total = total + 1; }
    flag = empty == undefined;
    if (flag) { total = total + 1; }
    flag = absent == undefined;
    if (flag) { total = total + 1; }
    flag = absent == null;
    if (flag) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatEmptyConstantAssignLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + emptyConstantAssignLoop();
    round = round + 1;
  }
  return checksum;
}

repeatEmptyConstantAssignLoop(20);
