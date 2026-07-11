# Bytecode Closure Design

本文记录 MiniJS bytecode VM 中闭包的最小实现方案。

## 目标

闭包要解决的问题是：函数返回以后，内部函数仍然可以访问创建它时捕获到的外层局部变量。

```javascript
function outer() {
  let x = 1;

  function inner() {
    x = x + 1;
    return x;
  }

  return inner;
}

let f = outer();
print(f()); // 2
print(f()); // 3
```

`inner` 被返回后，`outer` 的调用帧已经结束，但 `x` 不能丢失。

## 三个运行时对象

### BytecodeFunction

`BytecodeFunction` 是函数代码模板，保存：

- 函数名
- 参数列表
- bytecode chunk
- upvalue 描述表

它本身不保存本次调用捕获到的变量值。

### BytecodeClosure

`BytecodeClosure` 是运行时真正可调用的函数值，保存：

- 一个 `BytecodeFunction`
- 本次创建时捕获到的 `Upvalue` 列表

同一个 `BytecodeFunction` 可以创建多个 `BytecodeClosure`。这就是为什么两次调用外层函数时，得到的闭包状态可以互不影响。

### Upvalue

`Upvalue` 表示一个被捕获的外层局部变量。

变量还在外层函数的栈槽里时：

```text
Upvalue.stackIndex -> stack_[slot]
Upvalue.isClosed = false
```

外层函数返回或局部作用域结束后：

```text
Upvalue.closed = 原来的栈槽值
Upvalue.isClosed = true
```

之后内部函数访问这个变量时，就读写 `closed`。

## 变量查找顺序

编译变量表达式时，按下面顺序查找：

```text
local
  -> current closure
  -> upvalue
  -> global
```

含义分别是：

- `local`：当前函数自己的局部变量和参数。
- `current closure`：当前函数自己的名字，用于支持返回后的局部递归函数。
- `upvalue`：外层函数的局部变量或外层已经捕获的 upvalue。
- `global`：全局变量。

这个顺序保证局部变量可以遮蔽函数名和全局变量。

## 编译阶段

遇到函数声明时，编译器会为函数体创建一个子 `Compiler`。

如果内部函数引用了外层变量：

```javascript
function outer() {
  let x = 1;

  function inner() {
    return x;
  }

  return inner;
}
```

`inner` 编译 `x` 时：

1. 先查自己的 `locals_`，找不到。
2. 调用 `resolveUpvalue("x")` 查外层。
3. 在 `outer.locals_` 中找到 `x`。
4. 把 `outer` 的 `x` 标记为 `isCaptured = true`。
5. 在 `inner.upvalues_` 中记录：捕获外层 local slot。
6. `inner` 访问 `x` 时 emit `GetUpvalue`。

如果跨多层捕获：

```javascript
function a() {
  let x = 1;
  function b() {
    function c() {
      return x;
    }
    return c;
  }
  return b;
}
```

`c` 找不到 `x` 时，会递归询问 `b`；`b` 再询问 `a`。最终 `b` 捕获 `a` 的 local，`c` 捕获 `b` 的 upvalue，形成一条 upvalue 链。

## 关键 opcode

### OP_CLOSURE

创建运行时闭包。

```text
OP_CLOSURE functionConstant
  isLocal index
  isLocal index
  ...
```

`functionConstant` 指向常量池里的 `BytecodeFunction`。

后面的 `(isLocal, index)` 描述每个 upvalue 的来源：

- `isLocal = true`：捕获当前调用帧的 local slot。
- `isLocal = false`：复用当前闭包已经捕获的 upvalue。

### OP_GET_UPVALUE

读取闭包捕获的变量。

如果 upvalue 还开着，就从 `stack_[stackIndex]` 读。
如果 upvalue 已关闭，就从 `closed` 读。

### OP_SET_UPVALUE

写入闭包捕获的变量。

写入规则和 `OP_GET_UPVALUE` 对称：

- 未关闭：写回栈槽。
- 已关闭：写回 `closed`。

### OP_CLOSE_UPVALUE

关闭即将离开作用域的捕获变量。

普通局部变量离开作用域时只需要 `Pop`。
被闭包捕获的局部变量离开作用域时，需要先复制到 upvalue 的 `closed`，再弹出栈槽。

### OP_GET_CURRENT_CLOSURE

把当前调用帧的 closure 压栈。

它主要用于局部递归函数：

```javascript
function outer() {
  function fact(n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
  }

  return fact;
}
```

`fact` 被返回后，不能再从 `outer` 的局部槽里找自己；函数体内部引用 `fact` 时，需要直接取当前正在运行的 closure。

## VM 生命周期

闭包变量的生命周期可以分成三段：

```text
创建闭包
  -> captureUpvalue(stackIndex)

外层变量仍在栈上
  -> upvalue 指向 stack_[stackIndex]

外层作用域结束
  -> closeUpvalues(firstStackIndex)
  -> 把栈槽值复制到 upvalue.closed
```

`captureUpvalue()` 会复用相同 `stackIndex` 的 open upvalue。这样同一个外层变量被多个内部函数捕获时，它们共享同一个 upvalue。

`closeUpvalues()` 会关闭指定栈槽及之后的 open upvalue。关闭以后，即使原来的栈槽被弹出，闭包仍然能读写保存下来的值。

## 最小正确性检查

闭包实现至少要覆盖这些行为：

- 返回的内部函数能读取外层局部变量。
- 多个闭包实例的状态互不影响。
- 同一次外层调用创建的多个内部函数共享同一个捕获变量。
- 多层闭包能通过 upvalue 链捕获变量。
- 捕获的 block local 在 `break` 和 `continue` 时也会被关闭。
- 返回后的局部递归函数仍能调用自己。
- 参数和局部变量可以遮蔽当前函数名。
