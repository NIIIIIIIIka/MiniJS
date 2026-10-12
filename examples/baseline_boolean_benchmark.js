function booleanLoop(count) {
  let i = 0;
  let total = 0;
  while (i < count) {
    if (!(i < count / 2)) {
      total = total + 1;
    }
    if (!!(i == count / 2)) {
      total = total + 1;
    }
    i = i + 1;
  }
  return total;
}

function repeatBooleanLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + booleanLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatBooleanLoop(20, 5000);
