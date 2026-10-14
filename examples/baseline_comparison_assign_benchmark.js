function comparisonAssignLoop(count) {
  let i = 0;
  let total = 0;
  let limit = 2500;
  let flag = false;
  while (i < count) {
    flag = i < limit;
    if (flag) { total = total + 1; }
    flag = i > 2500;
    if (flag) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatComparisonAssignLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + comparisonAssignLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatComparisonAssignLoop(20, 5000);
