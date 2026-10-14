function add(a, b) {
  return a + b;
}

function callAdd(count) {
  let i = 0;
  let total = 0;
  while (i < count) {
    total = add(total, 1);
    i = i + 1;
  }
  return total;
}

function makeAdder(x) {
  function addInner(y) {
    return x + y;
  }
  return addInner;
}

function closureSum(count) {
  let addTwo = makeAdder(2);
  let i = 0;
  let total = 0;
  while (i < count) {
    total = addTwo(total);
    i = i + 1;
  }
  return total;
}

class Base {
  value(x) {
    return x + 1;
  }

  static bonus() {
    return 3;
  }
}

class Child < Base {
  value(x) {
    return super.value(x) + 1;
  }
}

function methodSum(count) {
  let child = Child();
  let i = 0;
  let total = 0;
  while (i < count) {
    total = child.value(total);
    i = i + 1;
  }
  return total + Child.bonus();
}

function objectProps(count) {
  let point = {
    x: 1,
    y: 2
  };
  let i = 0;
  let total = 0;
  while (i < count) {
    total = total + point.x;
    point.y = point.y + 1;
    i = i + 1;
  }
  return total + point.y;
}

function arrayOps(count) {
  let values = [];
  let i = 0;
  while (i < count) {
    values.push(i);
    i = i + 1;
  }

  let j = 0;
  let total = 0;
  while (j < count) {
    total = total + values[j];
    values[j] = values[j] + 1;
    j = j + 1;
  }
  return total + values.pop();
}

callAdd(1000) +
closureSum(300) +
methodSum(300) +
objectProps(500) +
arrayOps(200);
