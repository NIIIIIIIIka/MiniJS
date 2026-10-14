function booleanConstantAssignLoop() {
  let i = 0;
  let total = 0;
  let yes = true;
  let no = false;
  let flag = false;
  while (i < 5000) {
    flag = no == false;
    if (flag) { total = total + 1; }
    flag = no == true;
    if (flag) { total = total + 1; }
    flag = yes == true;
    if (flag) { total = total + 1; }
    flag = yes == false;
    if (flag) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatBooleanConstantAssignLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanConstantAssignLoop();
    round = round + 1;
  }
  return checksum;
}

repeatBooleanConstantAssignLoop(20);
