function localPairLoop(count) {
  let i = 0;
  let total = 0;
  while (i < count) {
    total = total + i;
    i = i + 1;
  }
  return total;
}

function repeatLocalPairLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + localPairLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatLocalPairLoop(20, 5000);
