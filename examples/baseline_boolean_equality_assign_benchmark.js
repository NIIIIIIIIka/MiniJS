function booleanEqualityAssignLoop() {
  let i = 0;
  let total = 0;
  let yes = true;
  let no = false;
  let flag = false;
  while (i < 5000) {
    flag = no == no;
    if (flag) { total = total + 1; }
    flag = no == yes;
    if (flag) { total = total + 1; }
    flag = yes == yes;
    if (flag) { total = total + 1; }
    flag = yes == no;
    if (flag) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatBooleanEqualityAssignLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanEqualityAssignLoop();
    round = round + 1;
  }
  return checksum;
}

repeatBooleanEqualityAssignLoop(20);
