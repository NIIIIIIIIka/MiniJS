# MiniJS 语言说明

MiniJS 是一个用 C++ 实现的 JavaScript-like 小语言。当前版本运行在 AST Interpreter 上，本文档记录已经支持的语言子集。

## 值类型

MiniJS 当前支持以下运行时值：

- number
- boolean
- null
- undefined
- string
- array
- object
- function
- builtin

```javascript
let n = 10;
let ok = true;
let empty = null;
let missing = undefined;
let name = "Tom";
let list = [1, 2, 3];
let user = { name: "Tom", age: 18 };
```

## 变量

变量使用 `let` 声明，并且可以重新赋值。

```javascript
let x = 10;
x = x + 1;
```

块语句会创建新的嵌套作用域。

```javascript
let x = 1;

{
  let x = 2;
  print(x); // 2
}

print(x); // 1
```

## 运算符

算术运算：

```javascript
1 + 2 * 3;
10 / 2;
10 % 3;
```

比较与相等判断：

```javascript
1 < 2;
2 >= 2;
"a" == "a";
null != undefined;
```

逻辑运算：

```javascript
true && false;
false || true;
!false;
```

## 控制流

MiniJS 支持 `if`、`while`、`for`、`break` 和 `continue`。

```javascript
let x = 10;

if (x > 0) {
  print(x);
} else {
  print(0);
}
```

```javascript
let i = 0;

while (i < 3) {
  print(i);
  i = i + 1;
}
```

```javascript
let sum = 0;

for (let i = 1; i <= 5; i = i + 1) {
  if (i == 3) {
    continue;
  }

  sum = sum + i;
}
```

## 函数

函数使用 `function` 声明，通过 `return` 返回值。

```javascript
function add(a, b) {
  return a + b;
}

print(add(1, 2));
```

没有显式返回值的函数会返回 `undefined`。

```javascript
function noop() {
}

print(typeOf(noop())); // undefined
```

## 闭包

函数会捕获它定义时所在的环境，因此可以形成闭包。

```javascript
function makeCounter() {
  let count = 0;

  function next() {
    count = count + 1;
    return count;
  }

  return next;
}

let counter = makeCounter();
print(counter()); // 1
print(counter()); // 2
```

## 数组

数组支持字面量、索引访问、索引赋值、`length`、`push` 和 `pop`。

```javascript
let a = [1, 2, 3];

print(a[0]);
a[1] = 10;

a.push(4);
print(a.length);
print(a.pop());
```

## 对象

对象支持字面量创建、属性读取和属性赋值。

```javascript
let user = {
  name: "Tom",
  age: 18
};

print(user.name);
user.age = 20;
print(user.age);
```

## 内置函数

MiniJS 当前提供以下内置函数：

```javascript
print(value);
has(value, "key");
keys(value);
del(value, "key");
typeOf(value);
len(value);
```

示例：

```javascript
let user = { name: "Tom", age: 18 };

print(has(user, "name")); // true
print(len(keys(user)));   // 2

del(user, "age");
print(has(user, "age"));  // false

print(typeOf(user));      // object
```
