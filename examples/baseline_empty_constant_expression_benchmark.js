function emptyConstantExpressionLoop() {
  let i = 0;
  let total = 0;
  let empty = null;
  let absent = undefined;
  while (i < 5000) {
    let first = empty == null;
    let second = empty == undefined;
    let third = absent == undefined;
    let fourth = absent == null;
    if (first) { total = total + 1; }
    if (second) { total = total + 1; }
    if (third) { total = total + 1; }
    if (fourth) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatEmptyConstantExpressionLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + emptyConstantExpressionLoop();
    round = round + 1;
  }
  return checksum;
}

repeatEmptyConstantExpressionLoop(20);
