# 手写递归下降 Parser：如何把源码变成 AST

如果说 Lexer 的工作是把源码切成一个个 Token，那么 Parser 的工作就是把这些 Token 重新组织成一棵有结构的树：AST，抽象语法树。

这一步很适合展示编译原理基础。因为它不靠黑盒库，也不是简单地把字符串切一切，而是要回答几个核心问题：

- Token 流如何被逐个消费？
- 表达式优先级怎么保证？
- 为什么 `1 + 2 * 3` 不会被错误地解析成 `(1 + 2) * 3`？
- `let`、`if`、`while`、`function`、`return` 这些语句如何分发？
- 语法错误如何记录成 diagnostic，而不是让程序直接崩掉？

下面以一个 MiniJS 风格的递归下降 Parser 为例，讲清楚源码变 AST 的全过程。

## 1. Parser 的输入不是源码，而是 Token 流

Parser 通常不会直接处理原始字符串。源码会先经过 Lexer：

```js
let x = 1 + 2 * 3;
```

被切成类似这样的 Token：

```text
Let Identifier(x) Equal Number(1) Plus Number(2) Star Number(3) Semicolon Eof
```

Parser 内部维护一个 `current` 下标，指向当前准备读取的 Token。几个基础操作构成了整个 Parser 的“消费模型”：

```cpp
bool match(TokenType type);
bool check(TokenType type) const;
const Token& advance();
const Token& peek() const;
const Token& previous() const;
bool isAtEnd() const;
```

它们的分工很简单：

- `peek()`：看当前 Token，但不消费。
- `check(type)`：判断当前 Token 是不是某种类型。
- `match(type)`：如果类型匹配，就消费它，并返回 `true`。
- `advance()`：无条件向前走一个 Token。
- `previous()`：拿到刚刚消费过的 Token。
- `isAtEnd()`：判断是否到达 `Eof`。

递归下降 Parser 的核心手感就是：每个函数负责识别一种语法结构；识别成功就消费对应 Token；识别失败就记录错误。

例如变量声明：

```cpp
StmtPtr Parser::letDeclaration() {
  if (!check(TokenType::Identifier)) {
    report(peek(), "expected variable name");
    return nullptr;
  }

  const Token name = advance();

  if (!match(TokenType::Equal)) {
    report(peek(), "expected '=' after variable name");
  }

  ExprPtr initializer = expression();

  if (!match(TokenType::Semicolon)) {
    report(peek(), "expected ';' after variable declaration");
  }

  return std::make_unique<LetStmt>(std::string(name.lexeme), std::move(initializer));
}
```

这段代码对应的语法大概是：

```text
letDeclaration -> "let" Identifier "=" expression ";"
```

它不是在“猜字符串”，而是在按照语法规则消费 Token。

## 2. AST 是源码的结构化表示

源码是线性的：

```js
let x = 1 + 2 * 3;
```

AST 是树状的：

```text
LetStmt
  name: x
  initializer:
    BinaryExpr(+)
      left: NumberExpr(1)
      right:
        BinaryExpr(*)
          left: NumberExpr(2)
          right: NumberExpr(3)
```

这棵树把“谁和谁结合”表达清楚了。后续解释器、编译器、优化器都不需要再从字符串里推断结构，只需要访问 AST 节点。

常见的表达式节点包括：

- `NumberExpr`：数字字面量，比如 `123`
- `VariableExpr`：变量读取，比如 `x`
- `BinaryExpr`：二元运算，比如 `a + b`
- `GroupingExpr`：括号表达式，比如 `(a + b)`
- `AssignExpr`：变量赋值，比如 `x = 10`
- `CallExpr`：函数调用，比如 `add(1, 2)`
- `ArrayExpr` / `IndexExpr`：数组字面量和下标访问

常见的语句节点包括：

- `LetStmt`：变量声明
- `ExprStmt`：表达式语句
- `IfStmt`：条件语句
- `WhileStmt`：循环语句
- `FunctionStmt`：函数声明
- `ReturnStmt`：返回语句
- `BlockStmt`：代码块

Parser 的目标，就是把 Token 流组装成这些节点。

## 3. 表达式优先级：用函数层级表达绑定强弱

表达式解析是 Parser 里最容易被问到的部分。

MiniJS 里可以把表达式入口写成：

```cpp
ExprPtr Parser::expression() {
  return assignment();
}
```

然后逐层下降：

```text
expression  -> assignment
assignment  -> equality "=" assignment | equality
equality    -> comparison (("==" | "!=") comparison)*
comparison  -> term ((">" | ">=" | "<" | "<=") term)*
term        -> factor (("+" | "-") factor)*
factor      -> call (("*" | "/" | "%") call)*
call        -> primary ("(" arguments? ")" | "[" expression "]")*
primary     -> Number | Identifier | "true" | "false" | "null" | "(" expression ")" | array
```

越靠下的函数，优先级越高。

也就是说：

- `primary()` 先处理数字、变量、括号、数组这种最小单元。
- `call()` 处理函数调用和下标访问。
- `factor()` 处理 `* / %`。
- `term()` 处理 `+ -`。
- `comparison()` 处理大小比较。
- `equality()` 处理相等比较。
- `assignment()` 处理赋值。

这个设计的好处是：优先级不需要靠一堆复杂条件硬凑出来，而是自然体现在函数调用顺序里。

## 4. 为什么 `1 + 2 * 3` 能解析正确？

看这段源码：

```js
1 + 2 * 3;
```

Token 流是：

```text
Number(1) Plus Number(2) Star Number(3) Semicolon Eof
```

解析从最低优先级入口开始：

```text
expression()
  -> assignment()
  -> equality()
  -> comparison()
  -> term()
```

`term()` 负责处理 `+` 和 `-`。但它不会直接调用 `primary()` 作为右操作数，而是调用更高优先级的 `factor()`：

```cpp
ExprPtr Parser::term() {
  ExprPtr expr = factor();

  while (match(TokenType::Plus) || match(TokenType::Minus)) {
    const TokenType op = previous().type;
    ExprPtr right = factor();
    expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
  }

  return expr;
}
```

执行过程可以这样理解：

1. `term()` 先调用 `factor()` 解析左侧，得到 `NumberExpr(1)`。
2. 看到 `+`，说明这是一个加法表达式。
3. 为了拿到右侧，`term()` 再次调用 `factor()`。
4. 右侧不是简单的 `2`，因为 `factor()` 会继续吃掉 `2 * 3`。
5. 所以右侧返回的是 `BinaryExpr(*, 2, 3)`。
6. 最终 `term()` 组装出 `BinaryExpr(+, 1, BinaryExpr(*, 2, 3))`。

也就是：

```text
(+ 1 (* 2 3))
```

这就是 `1 + 2 * 3` 能正确解析的原因：加法层的右操作数来自乘法层，而乘法层会优先把 `2 * 3` 收拢成一棵子树。

如果源码是：

```js
(1 + 2) * 3;
```

那么 `primary()` 遇到 `(` 会递归调用 `expression()`，先把括号里的 `1 + 2` 解析完，再回到外层乘法，于是 AST 变成：

```text
(* (group (+ 1 2)) 3)
```

括号通过递归入口改变了默认优先级。

## 5. 左结合和右结合：循环与递归的区别

多数二元运算是左结合的，比如：

```js
1 - 2 - 3
```

应该解析成：

```text
(- (- 1 2) 3)
```

所以 `term()` 和 `factor()` 使用 `while` 循环：

```cpp
while (match(TokenType::Plus) || match(TokenType::Minus)) {
  const TokenType op = previous().type;
  ExprPtr right = factor();
  expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
}
```

每次循环都把旧的 `expr` 放到新节点左边，因此天然形成左结合。

赋值表达式则通常是右结合的：

```js
a = b = 1
```

应该解析成：

```text
(= a (= b 1))
```

所以 `assignment()` 在遇到 `=` 后，会递归调用自己解析右侧：

```cpp
ExprPtr Parser::assignment() {
  ExprPtr expr = equality();

  if (match(TokenType::Equal)) {
    ExprPtr value = assignment();

    if (const auto* variable = dynamic_cast<const VariableExpr*>(expr.get())) {
      return std::make_unique<AssignExpr>(variable->name(), std::move(value));
    }

    report(previous(), "invalid assignment target");
  }

  return expr;
}
```

这里还有一个关键检查：赋值号左边必须是合法目标。

合法：

```js
x = 1;
arr[0] = 2;
```

非法：

```js
1 + 2 = 3;
```

Parser 会在这里记录 `invalid assignment target`。

## 6. statement()：语句分发中心

表达式解决的是“值怎么计算”，语句解决的是“程序怎么执行”。

顶层解析通常是：

```cpp
Program Parser::parseProgram() {
  Program statements;

  while (!isAtEnd()) {
    statements.push_back(statement());
  }

  return statements;
}
```

`statement()` 就是语句分发中心：

```cpp
StmtPtr Parser::statement() {
  if (match(TokenType::If)) {
    return ifStatement();
  }
  if (match(TokenType::Let)) {
    return letDeclaration();
  }
  if (match(TokenType::LeftBrace)) {
    return blockStatement();
  }
  if (match(TokenType::While)) {
    return whileStatement();
  }
  if (match(TokenType::Function)) {
    return functionDeclaration();
  }
  if (match(TokenType::Return)) {
    return returnStatement();
  }
  return expressionStatement();
}
```

它的逻辑很直接：

- 当前 Token 是 `if`，交给 `ifStatement()`。
- 当前 Token 是 `let`，交给 `letDeclaration()`。
- 当前 Token 是 `{`，交给 `blockStatement()`。
- 当前 Token 是 `while`，交给 `whileStatement()`。
- 当前 Token 是 `function`，交给 `functionDeclaration()`。
- 当前 Token 是 `return`，交给 `returnStatement()`。
- 都不是，就当作普通表达式语句。

例如：

```js
if (x > 0) {
  return x;
} else {
  return 0;
}
```

会被解析成：

```text
IfStmt
  condition: BinaryExpr(>, x, 0)
  thenBranch: BlockStmt
    ReturnStmt(x)
  elseBranch: BlockStmt
    ReturnStmt(0)
```

`while` 类似：

```js
while (i < 10) {
  i = i + 1;
}
```

会变成：

```text
WhileStmt
  condition: BinaryExpr(<, i, 10)
  body: BlockStmt
    ExprStmt(AssignExpr(i, BinaryExpr(+, i, 1)))
```

函数声明也一样：

```js
function add(a, b) {
  return a + b;
}
```

Parser 会消费函数名、参数列表、函数体，并构造：

```text
FunctionStmt
  name: add
  params: a, b
  body:
    ReturnStmt(BinaryExpr(+, a, b))
```

## 7. block()：递归解析一段语句列表

代码块本质上也是一段小程序：

```js
{
  let x = 1;
  let y = x + 2;
}
```

解析代码块时，调用方通常已经消费了 `{`，然后 `block()` 一直解析语句，直到遇到 `}` 或 `Eof`：

```cpp
Program Parser::block() {
  Program statements;

  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    statements.push_back(statement());
  }

  if (!match(TokenType::RightBrace)) {
    report(peek(), "expected '}' after block");
  }

  return statements;
}
```

这也是递归下降 Parser 很自然的地方：语句里可以有块，块里又可以有语句，语句里还可以继续嵌套 `if`、`while`、`function`。

## 8. diagnostic：语法错误不要直接炸掉

一个好 Parser 不应该只会处理正确代码。错误代码也要尽量给出明确位置和信息。

例如：

```js
let x = 1 + ;
```

当 `primary()` 期待一个表达式，却看到 `;` 时，可以记录：

```text
expected expression
```

实现上，Parser 可以维护一个 `diagnostics_` 数组：

```cpp
void Parser::report(const Token& token, std::string message) {
  diagnostics_.emplace_back(token.location, std::move(message));
}
```

每次发现语法错误，不一定要马上抛异常，而是把错误记录下来：

```cpp
if (!match(TokenType::Semicolon)) {
  report(peek(), "expected ';' after expression");
}
```

这样做有几个好处：

- 调用方可以统一展示所有词法和语法错误。
- 测试可以断言具体 diagnostic。
- REPL 或 IDE 集成时，不会因为一个语法错误直接终止整个进程。
- Parser 可以在部分错误场景下继续前进，尽量构造出可调试的 AST。

当然，错误恢复是一个可以继续深入的话题。更完整的 Parser 会实现同步机制，比如遇到错误后跳到下一个 `;`、`}` 或语句起始关键字，再继续解析后续代码。

## 9. 手写递归下降 Parser 的价值

手写 Parser 不只是为了“造轮子”。它能训练几个非常基础但很重要的能力：

- 能把语法规则翻译成函数结构。
- 能理解 Token 流是怎么被消费的。
- 能解释优先级和结合性，而不是只会说“库会处理”。
- 能设计 AST，让后续解释器或编译器更容易工作。
- 能处理语法错误，并产出对用户友好的 diagnostic。

如果你在简历或项目里写了一个 MiniJS、解释器、脚本语言、规则引擎、表达式引擎，那么 Parser 基本是面试官很喜欢追问的地方。

因为它能快速区分：你是真的懂编译前端，还是只是把几个库串起来。

## 10. 面试问答模拟

**Q1：递归下降 Parser 是什么？**

A：递归下降 Parser 是一种自顶向下的语法分析方法。它通常把每条语法规则写成一个函数，比如 `expression()`、`statement()`、`primary()`。这些函数通过查看和消费 Token，递归调用其他解析函数，最终构造 AST。

**Q2：Parser 为什么不直接处理字符串？**

A：因为字符串里混杂了空白、注释、关键字、标识符、数字和符号。Lexer 会先把源码转换成 Token 流，Parser 再基于 Token 类型做结构分析。这样职责更清晰，也更容易记录源码位置和错误信息。

**Q3：`match()` 和 `check()` 有什么区别？**

A：`check()` 只判断当前 Token 类型，不移动位置；`match()` 在类型匹配时会消费当前 Token。简单说，`check()` 是看一眼，`match()` 是看到了就吃掉。

**Q4：表达式优先级怎么实现？**

A：用不同层级的解析函数表示不同优先级。低优先级函数调用高优先级函数作为操作数，例如 `term()` 处理加减，但它的左右操作数来自 `factor()`；`factor()` 处理乘除，操作数来自 `call()` 或 `primary()`。越底层优先级越高。

**Q5：为什么 `1 + 2 * 3` 会被解析成 `1 + (2 * 3)`？**

A：因为 `term()` 解析 `+` 时，右操作数调用的是 `factor()`。而 `factor()` 会把 `2 * 3` 作为乘法表达式完整解析出来，再交给加法层。所以最终 AST 是 `BinaryExpr(+, 1, BinaryExpr(*, 2, 3))`。

**Q6：左结合怎么实现？**

A：用 `while` 循环不断把旧表达式作为左子树。例如 `1 - 2 - 3`，第一次得到 `(- 1 2)`，第二次把它作为左边，再构造 `(- (- 1 2) 3)`。

**Q7：赋值为什么通常要右结合？**

A：因为 `a = b = 1` 的语义通常是 `a = (b = 1)`。实现上，`assignment()` 在遇到 `=` 后递归调用自己解析右侧，所以自然形成右结合。

**Q8：怎么判断赋值左边是否合法？**

A：先解析出左侧表达式，再判断它是不是合法的 lvalue。例如变量读取 `VariableExpr` 可以赋值，下标访问 `IndexExpr` 也可以赋值；但 `1 + 2` 这种 `BinaryExpr` 不能作为赋值目标，应该报 `invalid assignment target`。

**Q9：`statement()` 一般做什么？**

A：`statement()` 是语句分发入口。它根据当前 Token 判断应该解析哪类语句，比如 `if` 走 `ifStatement()`，`let` 走 `letDeclaration()`，`while` 走 `whileStatement()`，`function` 走 `functionDeclaration()`，`return` 走 `returnStatement()`。如果都不是，就按表达式语句处理。

**Q10：`if` 和 `while` 为什么可以嵌套 block？**

A：因为它们的 body 解析入口通常还是 `statement()`。而 `statement()` 遇到 `{` 会进入 `blockStatement()`。所以 `if`、`while` 的 body 既可以是一条简单语句，也可以是一个 `{ ... }` 块。

**Q11：diagnostic 和 exception 的区别是什么？**

A：exception 更适合不可恢复的错误；diagnostic 更适合编译前端收集错误信息。Parser 记录 diagnostic 后，可以把错误位置和消息交给调用方展示，也可以在某些场景下继续解析后续代码。

**Q12：如果漏了分号，Parser 怎么处理？**

A：当某个语句期待 `;`，但 `match(TokenType::Semicolon)` 失败时，Parser 会用当前 Token 的位置记录一条错误，比如 `expected ';' after expression`。更完善的实现还会做错误恢复，跳到下一个可能的语句边界再继续。

**Q13：递归下降 Parser 有什么局限？**

A：它适合语法比较清晰、容易拆成递归函数的语言子集。直接处理左递归文法会有问题，比如 `expr -> expr "+" term` 会无限递归，需要改写成循环形式或使用其他算法。复杂语言也可能需要 Pratt Parser、LR Parser 或 Parser Generator。

**Q14：递归下降和 Pratt Parser 有什么区别？**

A：递归下降通常把每个优先级层级写成一个函数，结构直观；Pratt Parser 用 binding power 或 precedence table 驱动表达式解析，扩展操作符更灵活。小语言或教学项目用递归下降很清楚，操作符很多时 Pratt Parser 更紧凑。

**Q15：面试时怎么讲这个项目最有说服力？**

A：不要只说“我写了 Parser”。可以按这条线讲：Lexer 产出 Token 流，Parser 用 `current` 下标消费 Token；`statement()` 分发语句；表达式用多层函数处理优先级；`1 + 2 * 3` 会形成乘法在加法右侧的 AST；语法错误进入 diagnostic；最后解释器或编译器消费 AST。这样面试官能看到你真的理解编译前端的基本链路。

## 总结

手写递归下降 Parser 的关键，不在于代码有多长，而在于结构是否清楚：

- Token 是输入。
- AST 是输出。
- `match/check/advance` 负责消费 Token。
- `statement()` 负责分发语句。
- 表达式优先级通过函数层级表达。
- 左结合用循环，右结合用递归。
- 语法错误通过 diagnostic 记录。

能把这些讲清楚，就已经不是“调库写解析”了，而是在真正理解源码如何一步步变成可执行结构。
