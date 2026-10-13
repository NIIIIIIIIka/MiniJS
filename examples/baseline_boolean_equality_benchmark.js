function booleanEqualityLoop() {
  let i = 0;
  let total = 0;
  let yes = true;
  let no = false;
  let flag = false;
  while (i < 5000) {
    if (flag == no) { total = total + 1; }
    if (flag == yes) { total = total + 1; }
    if (yes == yes) { total = total + 1; }
    if (yes == no) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatBooleanEqualityLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanEqualityLoop();
    round = round + 1;
  }
  return checksum;
}

repeatBooleanEqualityLoop(20);
