function booleanAssignLoop() {
  let i = 0;
  let total = 0;
  let flag = false;
  while (i < 5000) {
    flag = true;
    if (flag) { total = total + 1; }
    flag = false;
    if (flag) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatBooleanAssignLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanAssignLoop();
    round = round + 1;
  }
  return checksum;
}

repeatBooleanAssignLoop(20);
