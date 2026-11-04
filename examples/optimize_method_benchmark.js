class OptimizeCounter {
  add(value) {
    return value + 1;
  }
}

function callAdd(receiver, value) {
  return receiver.add(value);
}

let methodCounter = OptimizeCounter();
let methodIndex = 0;
let methodChecksum = 0;
while (methodIndex < 50000) {
  methodChecksum = callAdd(methodCounter, methodChecksum);
  methodIndex = methodIndex + 1;
}
methodChecksum;
