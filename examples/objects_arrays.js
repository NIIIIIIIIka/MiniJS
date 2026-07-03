let user = {
  name: "Tom",
  age: 18
};

print(user.name);
user.age = 20;

print(has(user, "age"));
print(len(keys(user)));

let numbers = [1, 2, 3];
numbers.push(4);

print(numbers.length);
print(numbers.pop());
