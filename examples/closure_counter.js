function makeCounter() {
  let count = 0;

  function next() {
    count = count + 1;
    return count;
  }

  return next;
}

let counter = makeCounter();

print(counter());
print(counter());
print(counter());
