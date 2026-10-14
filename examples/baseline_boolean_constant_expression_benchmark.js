function booleanConstantExpressionLoop() {
  let i = 0;
  let total = 0;
  let yes = true;
  let no = false;
  while (i < 5000) {
    let first = no == false;
    let second = no == true;
    let third = yes == true;
    let fourth = yes == false;
    if (first) { total = total + 1; }
    if (second) { total = total + 1; }
    if (third) { total = total + 1; }
    if (fourth) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatBooleanConstantExpressionLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanConstantExpressionLoop();
    round = round + 1;
  }
  return checksum;
}

repeatBooleanConstantExpressionLoop(20);
