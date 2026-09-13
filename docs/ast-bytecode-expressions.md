# MiniJS 表达式 AST 与 Opcode 对照

本文以当前实现为准，说明 MiniJS 中每种表达式的 AST 结构、编译顺序、字节码格式和栈行为。

涉及的主要实现文件：

- `include/minijs/ast.h`：表达式 AST 节点定义。
- `include/minijs/chunk.h`：`Opcode` 枚举、常量池和 feedback slot 定义。
- `src/compiler.cpp`：`Compiler::emitExpression()` 的 AST 到字节码编译逻辑。
- `src/disassembler.cpp`：opcode 操作数的反汇编格式。

## 代码位置

- AST 表达式和语句节点：[include/minijs/ast.h](../include/minijs/ast.h)、[src/ast.cpp](../src/ast.cpp)
- opcode、常量池、feedback slot 和 Chunk 存储：[include/minijs/chunk.h](../include/minijs/chunk.h)、[src/chunk.cpp](../src/chunk.cpp)
- AST 到 bytecode 编译：[include/minijs/compiler.h](../include/minijs/compiler.h)、[src/compiler.cpp](../src/compiler.cpp) 中的 `Compiler::emitExpression()`、`Compiler::emitStatement()`、`Compiler::compileProgram()`
- VM 指令执行：[include/minijs/vm.h](../include/minijs/vm.h)、[src/vm.cpp](../src/vm.cpp) 中的 `VM::run()`
- 反汇编和字节码解码：[src/disassembler.cpp](../src/disassembler.cpp)、[include/minijs/bytecode_decoder.h](../include/minijs/bytecode_decoder.h)、[src/bytecode_decoder.cpp](../src/bytecode_decoder.cpp)
- 字节码格式测试：[tests/test_bytecode.cpp](../tests/test_bytecode.cpp)

## 阅读约定

### AST 表示法

下面的 `ExprPtr` 表示 `std::unique_ptr<Expr>`。尖括号表示拥有一个子节点，字符串和 token 表示节点保存的普通字段。

例如：

```text
BinaryExpr(
    left = NumberExpr("1"),
    op = Plus,
    right = NumberExpr("2"))
```

对应源码 `1 + 2`。

### 字节码表示法

每个 opcode 占一个字节，后面可能跟一个或多个一字节操作数。操作数的常见含义如下：

| 写法 | 含义 |
| --- | --- |
| `constant[n]` | 当前 `Chunk` 常量池下标 `n` |
| `name[n]` | 常量池下标 `n` 中的字符串名称 |
| `slot[n]` | 当前函数栈帧或闭包中的槽位 `n` |
| `argc=n` | 调用参数数量 |
| `feedback=n` | 当前 `Chunk` 的 feedback slot 下标 |
| `jump -> target` | 编译器回填后的跳转目标，底层实际保存的是偏移量 |

除非特别说明，表达式编译完成后会在栈顶留下表达式结果。二元运算和比较会消费两个操作数并留下一个结果。

## 操数约定

本节定义字节码中操作数的统一编码和解释方式。示例中的 `opcode` 代表一个字节，后续字段按照从左到右的顺序紧跟在它后面。

### 宽度与范围

- opcode 的底层类型是 `std::uint8_t`，每条指令的操作码占 1 字节。
- 常量池下标、名称常量下标、局部槽位、upvalue 槽位、元素数量和参数数量当前都编码为 1 字节，因此范围是 `0..255`。
- `OP_JUMP_IF_FALSE`、`OP_JUMP` 和 `OP_LOOP` 的跳转距离编码为两个字节，按高字节在前、低字节在后的顺序写入：

  ```text
  opcode highByte lowByte
  ```

- 两字节跳转距离使用无符号 `uint16`，最大距离为 65535。前向跳转的目标是 `instructionEnd + distance`；循环回跳的目标是 `instructionEnd - distance`。
- 当前没有宽操作数或多字节常量池索引格式。超过一字节范围的常量、局部变量、参数、数组元素、对象属性或调用参数会在编译阶段报错。

### 常量池索引

`OP_CONSTANT` 和其他需要名称或对象数据的指令不会把完整 `Value` 直接写入指令流，而是写入当前 `Chunk::constants()` 的下标：

```text
OP_CONSTANT constantIndex
OP_GET_GLOBAL nameIndex
OP_OBJECT propertyNamesIndex
```

这些下标的共同约定是：

- 下标从 0 开始。
- 指令操作数只保存下标，不保存类型标签或值本体。
- `OP_GET_GLOBAL`、`OP_SET_GLOBAL`、`OP_GET_PROPERTY`、`OP_SET_PROPERTY`、`OP_METHOD_CALL` 和 `OP_SUPER_CALL` 的名称索引必须指向字符串常量。
- `OP_OBJECT` 的索引必须指向一个 `Value` 字符串数组；数组长度与对象属性值数量相同，并按相同顺序配对。
- `OP_CLOSURE` 的索引指向常量池中的 `BytecodeFunction`，而不是已经创建好的运行时 closure。

因此，反汇编输出中的：

```text
OP_GET_PROPERTY 5 age feedback=0
```

表示“读取常量池第 5 项指定的属性名”，并不表示属性名本身被编码在第 5 个字节位置。

### 名称操作数

名称操作数分为两类：

1. 全局变量指令使用 `nameIndex`，通过常量池字符串定位全局绑定。
2. 属性和方法指令使用 `nameIndex`，通过常量池字符串定位属性或方法名称。

它们的物理编码相同，但语义不同：全局指令访问环境中的变量绑定；属性/方法指令访问栈上 receiver 对象的成员。

### 栈操作数顺序

编译器按源码的求值顺序生成子表达式，通常是从左到右。栈顶记作右侧：

```text
[ ... older values, first value, second value ]
                                      ^ top
```

主要栈约定如下：

| 指令 | 执行前栈顶部分 | 执行后栈顶部分 |
| --- | --- | --- |
| `OP_ADD` 等二元运算 | `[..., left, right]` | `[..., result]` |
| `OP_NEGATE`、`OP_NOT` | `[..., value]` | `[..., result]` |
| `OP_GET_INDEX` | `[..., object, index]` | `[..., value]` |
| `OP_SET_INDEX` | `[..., object, index, value]` | `[..., assignedValue]` |
| `OP_GET_PROPERTY` | `[..., object]` | `[..., propertyValue]` |
| `OP_SET_PROPERTY` | `[..., object, value]` | `[..., assignedValue]` |
| `OP_ARRAY count` | `[..., element0, ..., element(count-1)]` | `[..., array]` |
| `OP_OBJECT namesIndex` | `[..., value0, ..., value(count-1)]` | `[..., object]` |

数组和对象的元素值按源码顺序入栈；VM 组装完成后留下一个聚合值。赋值指令保留赋值结果，所以表达式 `a = b = 1` 可以继续组合使用。

### 调用操作数

普通调用使用：

```text
OP_CALL argc
```

执行前的栈布局为：

```text
[ ... callee, arg0, arg1, ..., arg(argc - 1) ]
```

其中 `callee` 最先入栈，参数按源码顺序入栈，`argc` 不包含 callee。指令执行后，上述区域被一个返回值替换：

```text
[ ... returnValue ]
```

方法调用使用：

```text
OP_METHOD_CALL nameIndex argc feedbackSlot
```

它的栈布局为：

```text
[ ... receiver, arg0, arg1, ..., arg(argc - 1) ]
```

这里 `receiver` 是方法调用的对象，不计入 `argc`。方法名通过 `nameIndex` 获取，调用点通过 `feedbackSlot` 关联 inline cache。

`OP_SUPER_CALL` 的实际编译编码当前是：

```text
OP_SUPER_CALL nameIndex argc
```

执行前栈中还会有编译器隐式压入的父类和 receiver：

```text
[ ... superClass, receiver, arg0, ..., arg(argc - 1) ]
```

`superClass` 和 `receiver` 都不计入 `argc`。它们由 `emitExpression()` 在生成 `OP_SUPER_CALL` 之前分别读取 `super` 和 `this` 得到。

### 局部变量与 upvalue 槽位

`slot` 不是源码变量编号，而是编译器为当前函数分配的运行时槽位：

```text
OP_GET_LOCAL slot
OP_SET_LOCAL slot
OP_GET_UPVALUE slot
OP_SET_UPVALUE slot
```

- `OP_GET_LOCAL` 和 `OP_SET_LOCAL` 的 slot 索引当前函数调用帧的栈布局。
- `OP_GET_UPVALUE` 和 `OP_SET_UPVALUE` 的 slot 索引当前 closure 的 upvalue 数组。
- 同一个数字在不同函数中没有全局意义；每个函数都有自己的 local 和 upvalue 编号空间。
- 函数参数也使用 local slot。

### 跳转操作数

跳转指令的两个操作数不是绝对地址，而是相对当前指令末尾的距离。编译器先写入两个占位字节，生成后续代码后再回填：

```text
OP_JUMP_IF_FALSE highByte lowByte
OP_JUMP          highByte lowByte
OP_LOOP          highByte lowByte
```

反汇编器将相对距离转换成便于阅读的目标 offset，例如：

```text
0002 OP_JUMP_IF_FALSE 2 -> 8
```

这里 `2` 是该指令的起始 offset，`8` 是实际目标 offset，不是写入指令流的原始第三个操作数。

### Feedback slot

属性和方法调用指令的最后一个操作数是 feedback slot：

```text
OP_GET_PROPERTY  nameIndex feedbackSlot
OP_SET_PROPERTY  nameIndex feedbackSlot
OP_METHOD_CALL   nameIndex argc feedbackSlot
```

feedback slot 是当前 `Chunk` 的 side table 下标，不是栈槽，也不是常量池下标。创建 slot 时还会记录 `FeedbackKind`：

```text
GetProperty
SetProperty
MethodCall
```

VM 用该 slot 保存 inline cache 的状态和缓存项。多个指令可以使用不同 slot；同一指令在重复执行时会访问同一个 slot，从而积累该访问点的运行时反馈。

### `OP_CLOSURE` 的变长操作数

`OP_CLOSURE` 的基本格式是：

```text
OP_CLOSURE functionConstantIndex
  isLocal index
  isLocal index
  ...
```

`functionConstantIndex` 指向 `BytecodeFunction`。后面的每个二元组对应函数的一个 upvalue，数量由该函数的 `upvalues` 数组决定，因此这条指令的长度不是固定的：

- `isLocal = 1`：从当前调用帧的 local slot `index` 捕获。
- `isLocal = 0`：从当前 closure 的 upvalue slot `index` 复用。

这组描述信息不属于普通表达式结果栈；它们是 VM 创建 closure 时读取的内联元数据。详见 [Bytecode Closure 设计](bytecode-closure.md)。

## 总览

| 源码类别 | AST 节点 | 主要 opcode |
| --- | --- | --- |
| 数字、字符串、布尔、null、undefined | `NumberExpr`、`StringExpr`、`BoolExpr`、`NullExpr`、`UndefinedExpr` | `OP_CONSTANT` |
| 变量读取 | `VariableExpr` | `OP_GET_LOCAL`、`OP_GET_UPVALUE`、`OP_GET_GLOBAL`、`OP_GET_CURRENT_CLOSURE` |
| 数组字面量 | `ArrayExpr` | 子表达式 opcode、`OP_ARRAY` |
| 对象字面量 | `ObjectExpr` | 子表达式 opcode、`OP_OBJECT` |
| 一元运算 | `UnaryExpr` | `OP_NEGATE`、`OP_NOT` |
| 算术、比较、相等 | `BinaryExpr` | `OP_ADD` 等 |
| 逻辑运算 | `LogicalExpr` | `OP_JUMP_IF_FALSE`、`OP_JUMP`、`OP_POP` |
| 括号 | `GroupingExpr` | 子表达式自身的 opcode |
| 变量赋值 | `AssignExpr` | `OP_SET_LOCAL`、`OP_SET_UPVALUE`、`OP_SET_GLOBAL` |
| 下标读取/赋值 | `IndexExpr`、`IndexAssignExpr` | `OP_GET_INDEX`、`OP_SET_INDEX` |
| 属性读取/赋值 | `GetExpr`、`SetExpr` | `OP_GET_PROPERTY`、`OP_SET_PROPERTY` |
| 普通调用 | `CallExpr` | `OP_CALL` |
| 方法调用 | `MethodCallExpr` | `OP_METHOD_CALL` |
| `super` 方法调用 | `SuperCallExpr` | `OP_SUPER_CALL` |

## 1. 字面量表达式

### 数字

AST：

```text
NumberExpr {
  value_: string  // 源码文本，例如 "3.14"
}
```

源码：

```javascript
42;
```

编译结果：

```text
OP_CONSTANT constant[n=42]
```

编译器会通过 `std::stod()` 把 AST 中保存的文本转换为 `Value`，再放入常量池。数字本身不直接编码在 opcode 后面。

### 字符串

AST：

```text
StringExpr {
  value_: string  // 已去掉引号的内容
}
```

源码 `"hello"` 的结构和字节码为：

```text
StringExpr("hello")

OP_CONSTANT constant[n="hello"]
```

### 布尔值、null 和 undefined

AST 结构分别为：

```text
BoolExpr      { value_: "true" | "false" }
NullExpr      { value_: string }
UndefinedExpr { /* no fields */ }
```

它们都使用 `OP_CONSTANT`，区别在于写入常量池的 `Value`：

```text
true       -> OP_CONSTANT constant[n=true]
false      -> OP_CONSTANT constant[n=false]
null       -> OP_CONSTANT constant[n=null]
undefined  -> OP_CONSTANT constant[n=undefined]
```

## 2. 变量读取

AST：

```text
VariableExpr {
  name_: string
}
```

例如 `x` 是 `VariableExpr("x")`。编译器按照 local、当前闭包、upvalue、global 的顺序解析名称：

| 变量位置 | opcode 格式 | 说明 |
| --- | --- | --- |
| 当前函数局部变量/参数 | `OP_GET_LOCAL slot` | 从当前 VM 栈帧读取 |
| 当前函数自身名称 | `OP_GET_CURRENT_CLOSURE` | 支持返回后的局部递归函数 |
| 外层捕获变量 | `OP_GET_UPVALUE slot` | 从闭包的 upvalue 读取 |
| 全局变量 | `OP_GET_GLOBAL nameIndex` | `nameIndex` 指向名称字符串 |

`this` 没有单独的 AST 节点，仍然是 `VariableExpr("this")`；编译器只在方法上下文中允许读取它。

示例：

```javascript
let x = 10;
x;
```

顶层程序的典型字节码为：

```text
OP_CONSTANT 0 10
OP_DEFINE_GLOBAL 1 x
OP_GET_GLOBAL 2 x
OP_RETURN
```

常量池下标会随着其他表达式出现而变化，因此文档中的具体下标只用于说明格式。

## 3. 数组字面量

AST：

```text
ArrayExpr {
  elements_: vector<ExprPtr>
}
```

源码：

```javascript
[1, x + 2, "ok"]
```

编译器按从左到右的顺序编译每个元素，然后生成元素数量：

```text
OP_CONSTANT 0 1
OP_GET_GLOBAL 1 x
OP_CONSTANT 2 2
OP_ADD
OP_CONSTANT 3 ok
OP_ARRAY 3
```

执行 `OP_ARRAY count` 时，栈顶的 `count` 个值被组合成数组，并留下一个数组值。元素数量必须能放入一个字节，最大为 255。

空数组为：

```text
OP_ARRAY 0
```

## 4. 对象字面量

AST：

```text
ObjectExpr {
  properties_: vector<ObjectProperty>
}

ObjectProperty {
  name: string
  value: ExprPtr
}
```

源码：

```javascript
{ name: "Tom", age: 18 }
```

对象属性值先按源码顺序入栈；所有属性名则被收集为一个字符串数组，作为一个常量写入常量池：

```text
OP_CONSTANT 0 Tom
OP_CONSTANT 1 18
OP_OBJECT 2 [name, age]
```

`OP_OBJECT namesIndex` 的操作数指向常量池中的属性名数组。VM 根据该数组把栈上的值和属性名一一配对，因此值表达式的求值顺序是从左到右。最多支持 255 个属性。

## 5. 一元表达式

AST：

```text
UnaryExpr {
  op_: TokenType
  right_: ExprPtr
}
```

编译器先编译右侧表达式，再根据运算符生成 opcode：

| 源码 | AST 运算符 | 字节码 |
| --- | --- | --- |
| `-x` | `Minus` | `x; OP_NEGATE` |
| `!x` | `Bang` | `x; OP_NOT` |

示例：

```text
UnaryExpr(Minus, NumberExpr("10"))

OP_CONSTANT 0 10
OP_NEGATE
```

## 6. 二元表达式

AST：

```text
BinaryExpr {
  left_: ExprPtr
  op_: TokenType
  right_: ExprPtr
}
```

普通二元表达式总是先编译左操作数，再编译右操作数，最后生成一个运算 opcode：

| 源码运算符 | TokenType | opcode |
| --- | --- | --- |
| `+` | `Plus` | `OP_ADD` |
| `-` | `Minus` | `OP_SUB` |
| `*` | `Star` | `OP_MUL` |
| `/` | `Slash` | `OP_DIV` |
| `%` | `Percent` | `OP_MOD` |
| `==` | `EqualEqual` | `OP_EQUAL` |
| `>` | `Greater` | `OP_GREATER` |
| `<` | `Less` | `OP_LESS` |

示例 `1 + 2 * 3` 的 AST 体现了优先级：

```text
BinaryExpr(Plus,
  NumberExpr("1"),
  BinaryExpr(Mul, NumberExpr("2"), NumberExpr("3")))
```

字节码为：

```text
OP_CONSTANT 0 1
OP_CONSTANT 1 2
OP_CONSTANT 2 3
OP_MUL
OP_ADD
```

### 由基础 opcode 合成的比较

以下运算没有独立 opcode，而是用已有比较和 `OP_NOT` 组合：

| 源码 | 字节码尾部 |
| --- | --- |
| `a != b` | `OP_EQUAL; OP_NOT` |
| `a >= b` | `OP_LESS; OP_NOT` |
| `a <= b` | `OP_GREATER; OP_NOT` |

完整示例 `a <= b`：

```text
<compile a>
<compile b>
OP_GREATER
OP_NOT
```

## 7. 逻辑表达式与短路

AST：

```text
LogicalExpr {
  left_: ExprPtr
  op_: TokenType  // AndAnd 或 OrOr
  right_: ExprPtr
}
```

逻辑表达式与 `BinaryExpr` 不同：右侧不一定执行，并且结果保留的是操作数值而不是强制转换后的布尔值。

### `&&`

源码：

```javascript
a && b
```

结构：

```text
<compile a>
OP_JUMP_IF_FALSE -> end
OP_POP
<compile b>
end:
```

左值为 falsy 时直接跳过 `OP_POP` 和右值，左值保留在栈顶；左值为 truthy 时弹出左值并计算右值。

### `||`

源码：

```javascript
a || b
```

结构：

```text
<compile a>
OP_JUMP_IF_FALSE -> evaluate_right
OP_JUMP -> end
evaluate_right:
OP_POP
<compile b>
end:
```

左值为 truthy 时跳到结尾并保留左值；左值为 falsy 时弹出左值后计算右值。

## 8. 括号表达式

AST：

```text
GroupingExpr {
  expression_: ExprPtr
}
```

括号只影响 Parser 构造 AST 时的结合顺序，编译器不会生成专门的 opcode：

```text
(a + b)

GroupingExpr(
  BinaryExpr(VariableExpr("a"), Plus, VariableExpr("b")))

<compile a>
<compile b>
OP_ADD
```

## 9. 变量赋值

AST：

```text
AssignExpr {
  name_: string
  value_: ExprPtr
}
```

赋值表达式先计算右值，再根据名称解析结果写入目标。`OP_SET_*` 执行后仍保留被赋的值，因此赋值本身可以继续作为表达式使用。

| 目标位置 | opcode 格式 |
| --- | --- |
| 当前局部变量 | `OP_SET_LOCAL slot` |
| 捕获变量 | `OP_SET_UPVALUE slot` |
| 全局变量 | `OP_SET_GLOBAL nameIndex` |

示例：

```javascript
x = x + 1
```

```text
<read x>
OP_CONSTANT 0 1
OP_ADD
OP_SET_<LOCAL|UPVALUE|GLOBAL> x
```

## 10. 下标访问与下标赋值

### 下标读取

AST：

```text
IndexExpr {
  object_: ExprPtr
  index_: ExprPtr
}
```

源码 `array[index]` 的编译顺序是对象、索引、读取：

```text
<compile array>
<compile index>
OP_GET_INDEX
```

`OP_GET_INDEX` 消费栈顶的索引和其下方的对象，留下读取结果。

### 下标赋值

AST：

```text
IndexAssignExpr {
  object_: ExprPtr
  index_: ExprPtr
  value_: ExprPtr
}
```

源码 `array[index] = value` 的编译顺序为：

```text
<compile array>
<compile index>
<compile value>
OP_SET_INDEX
```

`OP_SET_INDEX` 消费对象、索引和值，并留下赋值结果。

## 11. 对象属性访问

### 属性读取

AST：

```text
GetExpr {
  object_: ExprPtr
  name_: string
}
```

源码 `object.name`：

```text
<compile object>
OP_GET_PROPERTY nameIndex feedbackSlot
```

反汇编形式为：

```text
OP_GET_PROPERTY 5 age feedback=0
```

其中 `5` 是属性名 `age` 在常量池中的下标，`0` 是当前 `Chunk` 的 feedback slot。该 slot 用于记录对象 shape，支持属性 inline cache。

### 属性赋值

AST：

```text
SetExpr {
  object_: ExprPtr
  name_: string
  value_: ExprPtr
}
```

源码 `object.name = value`：

```text
<compile object>
<compile value>
OP_SET_PROPERTY nameIndex feedbackSlot
```

`OP_SET_PROPERTY` 同样会为每个编译位置分配 `FeedbackKind::SetProperty` slot，并保留赋值结果。

## 12. 普通函数调用

AST：

```text
CallExpr {
  callee_: ExprPtr
  arguments_: vector<ExprPtr>
}
```

源码：

```javascript
add(1, 2)
```

编译器先压入 callee，再按从左到右的顺序压入参数，最后生成参数数量：

```text
<compile add>
OP_CONSTANT 0 1
OP_CONSTANT 1 2
OP_CALL 2
```

调用前的栈布局为：

```text
[ ... callee, arg0, arg1, ... arg(n-1) ]
```

`OP_CALL argc` 消费 callee 和参数，并将返回值放回调用位置。参数数量最大为 255。

## 13. 方法调用

AST：

```text
MethodCallExpr {
  object_: ExprPtr
  name_: string
  arguments_: vector<ExprPtr>
}
```

源码：

```javascript
array.push(1)
```

编译结果：

```text
<compile array>
OP_CONSTANT 0 1
OP_METHOD_CALL nameIndex argc feedbackSlot
```

反汇编形式：

```text
OP_METHOD_CALL 3 push 1 feedback=0
```

`OP_METHOD_CALL` 的操作数依次为：

1. 方法名常量池下标。
2. 参数数量。
3. `FeedbackKind::MethodCall` slot 下标。

与普通调用相比，方法调用把 receiver 作为隐含的调用对象交给 VM；VM 可以对实例方法、静态方法以及数组的 `push`/`pop` 做专门分派和 inline cache。

## 14. `super` 方法调用

AST：

```text
SuperCallExpr {
  method_: string
  arguments_: vector<ExprPtr>
}
```

源码：

```javascript
super.speak(message)
```

当前 Parser 将它表示为 `SuperCallExpr`，而不是普通 `GetExpr` 加 `CallExpr`。编译器要求当前处于子类方法中，然后隐式读取 `super` 和 `this`：

```text
OP_GET_<LOCAL|UPVALUE|GLOBAL> super
OP_GET_<LOCAL|UPVALUE|GLOBAL> this
<compile arg0>
...
OP_SUPER_CALL nameIndex argc
```

`OP_SUPER_CALL` 的操作数是方法名常量池下标和参数数量，没有普通方法调用使用的 feedback slot。`super` 作为父类查找起点，`this` 作为实际 receiver。

## 15. 函数值与闭包相关表达式

当前语言把函数声明表示为 `FunctionStmt`，没有独立的 `FunctionExpr`。函数声明不是表达式，但它创建的函数值会通过闭包 opcode 出现在表达式可使用的值流中：

```text
OP_CLOSURE functionConstant
  | local slot
  | upvalue slot
```

函数体内部读取捕获变量时使用 `OP_GET_UPVALUE`，赋值时使用 `OP_SET_UPVALUE`。函数体引用自己的名字时使用 `OP_GET_CURRENT_CLOSURE`。完整的闭包布局和 upvalue 描述见 [Bytecode Closure 设计](bytecode-closure.md)。

## 16. 表达式语句的额外 `OP_POP`

表达式本身通常留下结果，但表达式作为非最后一个表达式语句时，`Compiler::emitStatement()` 会追加 `OP_POP` 丢弃结果：

```javascript
1 + 2;
print(3);
```

大致结构为：

```text
OP_CONSTANT 0 1
OP_CONSTANT 1 2
OP_ADD
OP_POP

OP_GET_GLOBAL 2 print
OP_CONSTANT 3 3
OP_CALL 1
OP_RETURN
```

完整程序会保留最后一个表达式语句的值作为程序结果；如果程序没有表达式语句，编译器会压入 `undefined` 后再执行 `OP_RETURN`。

## Opcode 操作数速查

| opcode | 编码结构 | 主要来源 |
| --- | --- | --- |
| `OP_CONSTANT` | `opcode, constantIndex` | 所有基础字面量 |
| `OP_GET_GLOBAL` / `OP_SET_GLOBAL` | `opcode, nameIndex` | 全局变量读写 |
| `OP_GET_LOCAL` / `OP_SET_LOCAL` | `opcode, slot` | 局部变量读写 |
| `OP_GET_UPVALUE` / `OP_SET_UPVALUE` | `opcode, slot` | 闭包变量读写 |
| `OP_GET_CURRENT_CLOSURE` | `opcode` | 函数自引用 |
| `OP_NEGATE`, `OP_NOT` | `opcode` | 一元运算 |
| `OP_ADD`、`OP_SUB`、`OP_MUL`、`OP_DIV`、`OP_MOD` | `opcode` | 算术运算 |
| `OP_EQUAL`、`OP_GREATER`、`OP_LESS` | `opcode` | 比较运算 |
| `OP_JUMP_IF_FALSE`、`OP_JUMP`、`OP_LOOP` | `opcode, highByte, lowByte` | 逻辑和控制流跳转 |
| `OP_ARRAY` | `opcode, elementCount` | 数组字面量 |
| `OP_GET_INDEX` / `OP_SET_INDEX` | `opcode` | 下标读写 |
| `OP_OBJECT` | `opcode, propertyNamesConstantIndex` | 对象字面量 |
| `OP_GET_PROPERTY` / `OP_SET_PROPERTY` | `opcode, nameIndex, feedbackSlot` | 属性读写 |
| `OP_CALL` | `opcode, argc` | 普通函数调用 |
| `OP_METHOD_CALL` | `opcode, nameIndex, argc, feedbackSlot` | 方法调用 |
| `OP_SUPER_CALL` | `opcode, nameIndex, argc` | 当前编译器实际写入两个操作数；反汇编器显示函数仍沿用方法调用格式 |

最后一行需要特别注意：`Opcode::SuperCall` 在 `disassembler.cpp` 中复用了 `methodCallInstruction()` 的显示函数，因此反汇编器会尝试显示 feedback 字段；但 `emitExpression()` 当前只为 `OP_SUPER_CALL` 写入方法名和参数数量。这是当前实现的格式差异，后续如果为 super 调用增加 inline cache，需要同步调整编码和反汇编逻辑。
