function comparisonBranchLoop(count) {
  let i = 0;
  let total = 0;
  let midpoint = count / 2;
  while (i < count) {
    if (i < midpoint) { total = total + 1; }
    if (i > midpoint) { total = total + 1; }
    if (i == midpoint) { total = total + 1; }
    i = i + 1;
  }
  return total;
}

function repeatComparisonBranchLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + comparisonBranchLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatComparisonBranchLoop(20, 5000);
