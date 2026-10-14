function localExpressionLoop(count) {
  let i = 0;
  let total = 0;
  let offset = 2;
  while (i < count) {
    total = total + (i + offset);
    i = i + 1;
  }
  return total;
}

function repeatLocalExpressionLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + localExpressionLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatLocalExpressionLoop(20, 5000);
