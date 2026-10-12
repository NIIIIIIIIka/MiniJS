function label() {
  return "ok";
}

function literalReturnLoop(count) {
  let i = 0;
  let total = 0;
  while (i < count) {
    total = total + len(label());
    i = i + 1;
  }
  return total;
}

function repeatLiteralReturnLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + literalReturnLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatLiteralReturnLoop(20, 5000);
