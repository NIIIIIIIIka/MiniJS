function booleanConstantBranchLoop() {
  let i = 0;
  let total = 0;
  let yes = true;
  let no = false;
  while (i < 5000) {
    if (no == false) { total = total + 1; }
    if (no == true) { total = total + 1; }
    if (yes == true) { total = total + 1; }
    if (yes == false) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatBooleanConstantBranchLoop(rounds) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanConstantBranchLoop();
    round = round + 1;
  }
  return checksum;
}

repeatBooleanConstantBranchLoop(20);
