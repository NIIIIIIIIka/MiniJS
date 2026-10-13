function booleanCopyLoop() {
  let i = 0;
  let total = 0;
  let yes = true;
  let no = false;
  let flag = false;
  while (i < 5000) {
    flag = yes;
    if (flag) { total = total + 1; }
    flag = no;
    if (flag) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatBooleanCopyLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanCopyLoop();
    round = round + 1;
  }
  return checksum;
}

repeatBooleanCopyLoop(20);
