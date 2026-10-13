function booleanExpressionLoop() {
  let i = 0;
  let total = 0;
  let yes = true;
  let no = false;
  while (i < 5000) {
    let first = no == no;
    let second = no == yes;
    let third = yes == yes;
    let fourth = yes == no;
    if (first) { total = total + 1; }
    if (second) { total = total + 1; }
    if (third) { total = total + 1; }
    if (fourth) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatBooleanExpressionLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanExpressionLoop();
    round = round + 1;
  }
  return checksum;
}

repeatBooleanExpressionLoop(20);
