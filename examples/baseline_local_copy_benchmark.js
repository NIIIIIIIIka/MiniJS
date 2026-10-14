function localCopyLoop(count) {
  let i = 0;
  let total = 0;
  let copied = 0;
  while (i < count) {
    copied = i;
    total = total + copied;
    i = i + 1;
  }
  return total;
}

function repeatLocalCopyLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + localCopyLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatLocalCopyLoop(20, 5000);
