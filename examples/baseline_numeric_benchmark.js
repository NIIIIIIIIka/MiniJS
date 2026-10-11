function numericLoop(count) {
  let i = 0;
  let total = 0;
  let midpoint = count / 2;
  while (i < count) {
    total = total + (i * 2 - i) / 2;
    if (i > midpoint) {
      total = total + 1;
    }
    if (i == midpoint) {
      total = total + 1;
    }
    i = i + 1;
  }
  return total;
}

function repeatNumericLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + numericLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatNumericLoop(20, 5000);
