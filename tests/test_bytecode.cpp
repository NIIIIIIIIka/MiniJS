#include <iostream>
#include <memory>
#include <sstream>
#include <string_view>

#include "minijs/compiler.h"
#include "minijs/disassembler.h"
#include "minijs/parser.h"
#include "minijs/runtime_error.h"
#include "minijs/vm.h"
#include "test_framework.h"

namespace {

minijs::Value runBytecode(std::string_view source) {
  minijs::Parser parser(source);
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compile(*expression);

  minijs::VM vm;
  return vm.run(chunk);
}

minijs::Value runBytecodeProgram(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  return vm.run(chunk);
}

void testCompileNumberExpression() { EXPECT(runBytecode("42;").asNumber() == 42); }

void testCompileArithmeticExpression() {
  EXPECT(runBytecode("1 + 2 * 3;").asNumber() == 7);
  EXPECT(runBytecode("(1 + 2) * 3;").asNumber() == 9);
}

void testCompileUnaryMinus() { EXPECT(runBytecode("-10;").asNumber() == -10); }

void testCompileModuloExpression() { EXPECT(runBytecode("10 % 3;").asNumber() == 1); }

void testCompileBooleanNullUndefinedLiterals() {
  EXPECT(runBytecode("true;").toString() == "true");
  EXPECT(runBytecode("false;").toString() == "false");
  EXPECT(runBytecode("null;").isNull());
  EXPECT(runBytecode("undefined;").isUndefined());
}

void testCompileStringLiteral() { EXPECT(runBytecode("\"Tom\";").toString() == "Tom"); }

void testCompileStringConcatenation() {
  EXPECT(runBytecode("\"hello \" + \"world\";").toString() == "hello world");
  EXPECT(runBytecode("\"age: \" + 18;").toString() == "age: 18");
  EXPECT(runBytecode("18 + \" years\";").toString() == "18 years");
}

void testCompileComparisonExpressions() {
  EXPECT(runBytecode("1 < 2;").toString() == "true");
  EXPECT(runBytecode("1 <= 2;").toString() == "true");
  EXPECT(runBytecode("2 > 1;").toString() == "true");
  EXPECT(runBytecode("2 >= 1;").toString() == "true");
  EXPECT(runBytecode("1 == 1;").toString() == "true");
  EXPECT(runBytecode("1 != 2;").toString() == "true");
  EXPECT(runBytecode("1 > 2;").toString() == "false");
  EXPECT(runBytecode("2 <= 1;").toString() == "false");
}

void testCompileLogicalNot() {
  EXPECT(runBytecode("!false;").toString() == "true");
  EXPECT(runBytecode("!true;").toString() == "false");
  EXPECT(runBytecode("!0;").toString() == "true");
  EXPECT(runBytecode("!1;").toString() == "false");
}

void testCompileLogicalAndShortCircuit() {
  EXPECT(runBytecodeProgram("false && unknown;").toString() == "false");
  EXPECT(runBytecodeProgram("true && 42;").asNumber() == 42);
}

void testCompileLogicalOrShortCircuit() {
  EXPECT(runBytecodeProgram("true || unknown;").toString() == "true");
  EXPECT(runBytecodeProgram("false || 42;").asNumber() == 42);
}

void testCompileLogicalReturnsOperandValue() {
  EXPECT(runBytecodeProgram("0 || 2;").asNumber() == 2);
  EXPECT(runBytecodeProgram("1 || 2;").asNumber() == 1);
  EXPECT(runBytecodeProgram("0 && 2;").asNumber() == 0);
  EXPECT(runBytecodeProgram("1 && 2;").asNumber() == 2);
}

void testCompileLogicalWithBuiltins() {
  EXPECT(runBytecodeProgram(
             "let p = { name: \"Tom\", age: 18 };"
             "let ks = keys(p);"
             "len(ks) == 2 && has(p, ks[0]) && has(p, ks[1]);")
             .toString() == "true");
}

minijs::Chunk compileExpression(std::string_view source) {
  minijs::Parser parser(source);
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  return compiler.compile(*expression);
}

minijs::Chunk compileProgram(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  return compiler.compileProgram(program);
}

void testDisassembleArithmeticExpression() {
  const minijs::Chunk chunk = compileExpression("1 + 2 * 3;");

  const std::string expected =
      "0000 OP_CONSTANT 0 1\n"
      "0002 OP_CONSTANT 1 2\n"
      "0004 OP_CONSTANT 2 3\n"
      "0006 OP_MUL\n"
      "0007 OP_ADD\n"
      "0008 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleUnaryMinus() {
  const minijs::Chunk chunk = compileExpression("-10;");

  const std::string expected =
      "0000 OP_CONSTANT 0 10\n"
      "0002 OP_NEGATE\n"
      "0003 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleStringLiteral() {
  const minijs::Chunk chunk = compileExpression("\"Tom\";");

  const std::string expected =
      "0000 OP_CONSTANT 0 Tom\n"
      "0002 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleBooleanNullUndefinedLiterals() {
  EXPECT(minijs::disassembleChunk(compileExpression("true;")) ==
         "0000 OP_CONSTANT 0 true\n"
         "0002 OP_RETURN\n");
  EXPECT(minijs::disassembleChunk(compileExpression("null;")) ==
         "0000 OP_CONSTANT 0 null\n"
         "0002 OP_RETURN\n");
  EXPECT(minijs::disassembleChunk(compileExpression("undefined;")) ==
         "0000 OP_CONSTANT 0 undefined\n"
         "0002 OP_RETURN\n");
}

void testDisassembleLogicalAndJump() {
  const minijs::Chunk chunk = compileExpression("true && 42;");

  const std::string expected =
      "0000 OP_CONSTANT 0 true\n"
      "0002 OP_JUMP_IF_FALSE 2 -> 8\n"
      "0005 OP_POP\n"
      "0006 OP_CONSTANT 1 42\n"
      "0008 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleLogicalOrJump() {
  const minijs::Chunk chunk = compileExpression("true || 42;");

  const std::string expected =
      "0000 OP_CONSTANT 0 true\n"
      "0002 OP_JUMP_IF_FALSE 2 -> 8\n"
      "0005 OP_JUMP 5 -> 11\n"
      "0008 OP_POP\n"
      "0009 OP_CONSTANT 1 42\n"
      "0011 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleClosureCapturesOuterLocal() {
  const minijs::Chunk script =
      compileProgram("function outer() {"
                     "  let x = 10;"
                     "  function inner() {"
                     "    return x;"
                     "  }"
                     "  return inner;"
                     "}"
                     "outer();");

  const auto outer = script.constant(0).asBytecodeFunction();
  const std::string outerBytecode = minijs::disassembleChunk(outer->chunk);

  EXPECT(outerBytecode.find("OP_CONSTANT 0 10") != std::string::npos);
  EXPECT(outerBytecode.find("OP_CLOSURE 1 <function inner>") != std::string::npos);
  EXPECT(outerBytecode.find("| local 0") != std::string::npos);
  EXPECT(outerBytecode.find("OP_GET_LOCAL 1") != std::string::npos);

  const auto inner = outer->chunk.constant(1).asBytecodeFunction();
  const std::string innerBytecode = minijs::disassembleChunk(inner->chunk);

  EXPECT(innerBytecode.find("OP_GET_UPVALUE 0") != std::string::npos);
  EXPECT(innerBytecode.find("OP_RETURN") != std::string::npos);
}

void testDisassembleClosureCapturesThroughUpvalue() {
  const minijs::Chunk script =
      compileProgram("function outer() {"
                     "  let x = 10;"
                     "  function middle() {"
                     "    function inner() {"
                     "      return x;"
                     "    }"
                     "    return inner;"
                     "  }"
                     "  return middle;"
                     "}"
                     "outer();");

  const auto outer = script.constant(0).asBytecodeFunction();
  const std::string outerBytecode = minijs::disassembleChunk(outer->chunk);
  EXPECT(outerBytecode.find("OP_CLOSURE 1 <function middle>") != std::string::npos);
  EXPECT(outerBytecode.find("| local 0") != std::string::npos);

  const auto middle = outer->chunk.constant(1).asBytecodeFunction();
  const std::string middleBytecode = minijs::disassembleChunk(middle->chunk);
  EXPECT(middleBytecode.find("OP_CLOSURE 0 <function inner>") != std::string::npos);
  EXPECT(middleBytecode.find("| upvalue 0") != std::string::npos);

  const auto inner = middle->chunk.constant(0).asBytecodeFunction();
  const std::string innerBytecode = minijs::disassembleChunk(inner->chunk);
  EXPECT(innerBytecode.find("OP_GET_UPVALUE 0") != std::string::npos);
}

void testDisassembleClosureSetsUpvalue() {
  const minijs::Chunk script =
      compileProgram("function outer() {"
                     "  let x = 0;"
                     "  function add() {"
                     "    x = x + 1;"
                     "    return x;"
                     "  }"
                     "  return add;"
                     "}"
                     "outer();");

  const auto outer = script.constant(0).asBytecodeFunction();
  const auto add = outer->chunk.constant(1).asBytecodeFunction();
  const std::string bytecode = minijs::disassembleChunk(add->chunk);

  EXPECT(bytecode.find("OP_GET_UPVALUE 0") != std::string::npos);
  EXPECT(bytecode.find("OP_SET_UPVALUE 0") != std::string::npos);
}

void testDisassembleClosureClosesBlockLocal() {
  const minijs::Chunk script =
      compileProgram("function make() {"
                     "  let get = undefined;"
                     "  {"
                     "    let x = 42;"
                     "    function inner() { return x; }"
                     "    get = inner;"
                     "  }"
                     "  return get;"
                     "}"
                     "make();");

  const auto make = script.constant(0).asBytecodeFunction();
  const std::string bytecode = minijs::disassembleChunk(make->chunk);

  EXPECT(bytecode.find("OP_CLOSURE 2 <function inner>") != std::string::npos);
  EXPECT(bytecode.find("| local 1") != std::string::npos);
  EXPECT(bytecode.find("OP_CLOSE_UPVALUE") != std::string::npos);
}

void testDisassembleClassMethod() {
  const minijs::Chunk chunk = compileProgram("class Box { get() { return 123; } } Box;");
  const std::string bytecode = minijs::disassembleChunk(chunk);

  EXPECT(bytecode.find("OP_CLASS") != std::string::npos);
  EXPECT(bytecode.find("Box") != std::string::npos);
  EXPECT(bytecode.find("OP_CLOSURE") != std::string::npos);
  EXPECT(bytecode.find("<function get>") != std::string::npos);
  EXPECT(bytecode.find("OP_METHOD") != std::string::npos);
  EXPECT(bytecode.find("get") != std::string::npos);
}

void testDisassembleClassInheritance() {
  const minijs::Chunk chunk =
      compileProgram("class Animal {} class Dog < Animal { speak() { return \"woof\"; } } Dog;");
  const std::string bytecode = minijs::disassembleChunk(chunk);

  EXPECT(bytecode.find("OP_CLASS") != std::string::npos);
  EXPECT(bytecode.find("Animal") != std::string::npos);
  EXPECT(bytecode.find("Dog") != std::string::npos);
  EXPECT(bytecode.find("OP_INHERIT") != std::string::npos);
  EXPECT(bytecode.find("OP_METHOD") != std::string::npos);
  EXPECT(bytecode.find("speak") != std::string::npos);
}

void testDisassembleSuperMethodCall() {
  const minijs::Chunk chunk =
      compileProgram("class Animal { speak() { return \"animal\"; } }"
                     "class Dog < Animal { speak() { return super.speak(); } }"
                     "Dog().speak();");
  const std::string bytecode = minijs::disassembleChunk(chunk);

  EXPECT(bytecode.find("OP_INHERIT") != std::string::npos);
  EXPECT(bytecode.find("speak") != std::string::npos);

  std::shared_ptr<minijs::BytecodeFunction> dogSpeak;
  for (const minijs::Value& constant : chunk.constants()) {
    if (constant.isBytecodeFunction() && constant.asBytecodeFunction()->name == "speak") {
      dogSpeak = constant.asBytecodeFunction();
    }
  }

  EXPECT(dogSpeak != nullptr);
  const std::string methodBytecode = minijs::disassembleChunk(dogSpeak->chunk);
  EXPECT(methodBytecode.find("OP_SUPER_CALL") != std::string::npos);
  EXPECT(methodBytecode.find("speak") != std::string::npos);
}

void testDisassembleBreakClosesUpvalueBeforeJump() {
  const minijs::Chunk chunk =
      compileProgram("let saved = undefined;"
                     "while (true) {"
                     "  {"
                     "    let x = 42;"
                     "    function get() { return x; }"
                     "    saved = get;"
                     "    break;"
                     "  }"
                     "}"
                     "saved();");

  const std::string bytecode = minijs::disassembleChunk(chunk);
  const std::size_t closePosition = bytecode.find("OP_CLOSE_UPVALUE");
  const std::size_t breakJumpPosition = bytecode.find("OP_JUMP ", closePosition);

  EXPECT(closePosition != std::string::npos);
  EXPECT(breakJumpPosition != std::string::npos);
  EXPECT(closePosition < breakJumpPosition);
}

void testDisassembleLocalRecursionGetsCurrentClosure() {
  const minijs::Chunk script =
      compileProgram("function makeFact() {"
                     "  function fact(n) {"
                     "    if (n <= 1) return 1;"
                     "    return n * fact(n - 1);"
                     "  }"
                     "  return fact;"
                     "}"
                     "makeFact();");

  const auto makeFact = script.constant(0).asBytecodeFunction();
  const auto fact = makeFact->chunk.constant(0).asBytecodeFunction();
  const std::string bytecode = minijs::disassembleChunk(fact->chunk);

  EXPECT(bytecode.find("OP_GET_CURRENT_CLOSURE") != std::string::npos);
  EXPECT(bytecode.find("OP_CALL 1") != std::string::npos);
  EXPECT(bytecode.find("OP_GET_GLOBAL") == std::string::npos);
}

void testDisassembleParameterShadowsCurrentClosure() {
  const minijs::Chunk script =
      compileProgram("function test(test) {"
                     "  return test;"
                     "}"
                     "test(42);");

  const auto test = script.constant(0).asBytecodeFunction();
  const std::string bytecode = minijs::disassembleChunk(test->chunk);

  EXPECT(bytecode.find("OP_GET_LOCAL 0") != std::string::npos);
  EXPECT(bytecode.find("OP_GET_CURRENT_CLOSURE") == std::string::npos);
}

void testDisassembleWhileBreakJump() {
  const minijs::Chunk chunk = compileProgram("let i = 0; while (true) { break; } i;");

  const std::string expected =
      "0000 OP_CONSTANT 0 0\n"
      "0002 OP_DEFINE_GLOBAL 1 i\n"
      "0004 OP_CONSTANT 2 true\n"
      "0006 OP_JUMP_IF_FALSE 6 -> 16\n"
      "0009 OP_POP\n"
      "0010 OP_JUMP 10 -> 17\n"
      "0013 OP_LOOP 13 -> 4\n"
      "0016 OP_POP\n"
      "0017 OP_GET_GLOBAL 3 i\n"
      "0019 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleWhileContinueLoop() {
  const minijs::Chunk chunk = compileProgram("let i = 0; while (true) { continue; } i;");

  const std::string expected =
      "0000 OP_CONSTANT 0 0\n"
      "0002 OP_DEFINE_GLOBAL 1 i\n"
      "0004 OP_CONSTANT 2 true\n"
      "0006 OP_JUMP_IF_FALSE 6 -> 16\n"
      "0009 OP_POP\n"
      "0010 OP_LOOP 10 -> 4\n"
      "0013 OP_LOOP 13 -> 4\n"
      "0016 OP_POP\n"
      "0017 OP_GET_GLOBAL 3 i\n"
      "0019 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleForContinueJumpsToIncrement() {
  const minijs::Chunk chunk =
      compileProgram("let sum = 0; for (let i = 0; i < 2; i = i + 1) { continue; } sum;");

  const std::string expected =
      "0000 OP_CONSTANT 0 0\n"
      "0002 OP_DEFINE_GLOBAL 1 sum\n"
      "0004 OP_CONSTANT 2 0\n"
      "0006 OP_DEFINE_GLOBAL 3 i\n"
      "0008 OP_GET_GLOBAL 4 i\n"
      "0010 OP_CONSTANT 5 2\n"
      "0012 OP_LESS\n"
      "0013 OP_JUMP_IF_FALSE 13 -> 31\n"
      "0016 OP_POP\n"
      "0017 OP_JUMP 17 -> 20\n"
      "0020 OP_GET_GLOBAL 6 i\n"
      "0022 OP_CONSTANT 7 1\n"
      "0024 OP_ADD\n"
      "0025 OP_SET_GLOBAL 8 i\n"
      "0027 OP_POP\n"
      "0028 OP_LOOP 28 -> 8\n"
      "0031 OP_POP\n"
      "0032 OP_GET_GLOBAL 9 sum\n"
      "0034 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testCompileGlobalLet() { EXPECT(runBytecodeProgram("let x = 10; x;").asNumber() == 10); }

void testCompileGlobalExpressionUsesVariable() {
  EXPECT(runBytecodeProgram("let x = 10; x + 20;").asNumber() == 30);
}

void testCompileGlobalAssignment() {
  EXPECT(runBytecodeProgram("let x = 1; x = x + 2; x;").asNumber() == 3);
}

void testCompileIfElseStatement() {
  EXPECT(runBytecodeProgram("let x = 0;"
                            "if (true) {"
                            "  x = 1;"
                            "} else {"
                            "  x = 2;"
                            "}"
                            "x;")
             .asNumber() == 1);

  EXPECT(runBytecodeProgram("let x = 0;"
                            "if (false) {"
                            "  x = 1;"
                            "} else {"
                            "  x = 2;"
                            "}"
                            "x;")
             .asNumber() == 2);
}

void testCompileIfWithoutElseStatement() {
  EXPECT(runBytecodeProgram("let x = 0;"
                            "if (true) {"
                            "  x = 1;"
                            "}"
                            "x;")
             .asNumber() == 1);

  EXPECT(runBytecodeProgram("let x = 0;"
                            "if (false) {"
                            "  x = 1;"
                            "}"
                            "x;")
             .asNumber() == 0);
}

void testCompileWhileStatement() {
  EXPECT(runBytecodeProgram("let x = 3;"
                            "while (x > 0) {"
                            "  x = x - 1;"
                            "}"
                            "x;")
             .asNumber() == 0);
}

void testCompileWhileSkippedStatement() {
  EXPECT(runBytecodeProgram("let x = 0;"
                            "while (x > 0) {"
                            "  x = x - 1;"
                            "}"
                            "x;")
             .asNumber() == 0);
}

void testCompileWhileWithNestedIfStatement() {
  EXPECT(runBytecodeProgram("let x = 4;"
                            "let y = 0;"
                            "while (x > 0) {"
                            "  if (x > 2) {"
                            "    y = y + 10;"
                            "  } else {"
                            "    y = y + 1;"
                            "  }"
                            "  x = x - 1;"
                            "}"
                            "y;")
             .asNumber() == 22);
}

void testCompileForStatement() {
  EXPECT(runBytecodeProgram("let sum = 0;"
                            "for (let i = 1; i <= 3; i = i + 1) {"
                            "  sum = sum + i;"
                            "}"
                            "sum;")
             .asNumber() == 6);
}

void testCompileForSkippedStatement() {
  EXPECT(runBytecodeProgram("let sum = 0;"
                            "for (let i = 5; i <= 3; i = i + 1) {"
                            "  sum = sum + i;"
                            "}"
                            "sum;")
             .asNumber() == 0);
}

void testCompileForWithoutInitializerStatement() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "for (; i < 3; i = i + 1) {"
                            "}"
                            "i;")
             .asNumber() == 3);
}

void testCompileWhileBreakStatement() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "while (true) {"
                            "  i = i + 1;"
                            "  if (i == 3) break;"
                            "}"
                            "i;")
             .asNumber() == 3);
}

void testCompileWhileContinueStatement() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "let sum = 0;"
                            "while (i < 5) {"
                            "  i = i + 1;"
                            "  if (i == 3) continue;"
                            "  sum = sum + i;"
                            "}"
                            "sum;")
             .asNumber() == 12);
}

void testCompileForBreakStatement() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "for (; true; i = i + 1) {"
                            "  if (i == 3) break;"
                            "}"
                            "i;")
             .asNumber() == 3);
}

void testCompileForContinueStatement() {
  EXPECT(runBytecodeProgram("let sum = 0;"
                            "for (let i = 0; i < 5; i = i + 1) {"
                            "  if (i == 3) continue;"
                            "  sum = sum + i;"
                            "}"
                            "sum;")
             .asNumber() == 7);
}

void testCompileBreakContinuePopBlockLocals() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "while (i < 3) {"
                            "  {"
                            "    let temp = i;"
                            "    i = i + 1;"
                            "    continue;"
                            "  }"
                            "}"
                            "i;")
             .asNumber() == 3);

  EXPECT(runBytecodeProgram("let i = 0;"
                            "while (true) {"
                            "  {"
                            "    let temp = 10;"
                            "    i = i + temp;"
                            "    break;"
                            "  }"
                            "}"
                            "i;")
             .asNumber() == 10);
}

void testCompileBreakOutsideLoop() {
  try {
    runBytecodeProgram("break;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: break outside loop");
  }
}

void testCompileContinueOutsideLoop() {
  try {
    runBytecodeProgram("continue;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: continue outside loop");
  }
}

void testBytecodeFunctionDoesNotInheritOuterBreakContext() {
  try {
    runBytecodeProgram("while (true) {"
                       "  function stop() {"
                       "    break;"
                       "  }"
                       "  break;"
                       "}");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: break outside loop");
  }
}

void testBytecodeFunctionDoesNotInheritOuterContinueContext() {
  try {
    runBytecodeProgram("while (true) {"
                       "  function next() {"
                       "    continue;"
                       "  }"
                       "  break;"
                       "}");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: continue outside loop");
  }
}

void testBytecodeFunctionUsesOwnLoopContext() {
  EXPECT(runBytecodeProgram("function count() {"
                            "  let i = 0;"
                            "  while (true) {"
                            "    i = i + 1;"
                            "    if (i == 3) break;"
                            "  }"
                            "  return i;"
                            "}"
                            "count();")
             .asNumber() == 3);
}

void testCompileBlockLocalVariable() {
  EXPECT(runBytecodeProgram("let result = 0;"
                            "{"
                            "  let x = 1;"
                            "  result = x;"
                            "}"
                            "result;")
             .asNumber() == 1);
}

void testCompileNestedBlockLocalVariables() {
  EXPECT(runBytecodeProgram("let result = 0;"
                            "{"
                            "  let x = 1;"
                            "  {"
                            "    let y = 2;"
                            "    result = x + y;"
                            "  }"
                            "}"
                            "result;")
             .asNumber() == 3);
}

void testCompileLocalAssignment() {
  EXPECT(runBytecodeProgram("let result = 0;"
                            "{"
                            "  let x = 1;"
                            "  x = x + 2;"
                            "  result = x;"
                            "}"
                            "result;")
             .asNumber() == 3);
}

void testCompileLocalShadowsGlobal() {
  EXPECT(runBytecodeProgram("let x = 10;"
                            "let result = 0;"
                            "{"
                            "  let x = 1;"
                            "  result = x;"
                            "}"
                            "x + result;")
             .asNumber() == 11);
}

void testCompileDuplicateLocalDeclaration() {
  try {
    runBytecodeProgram("{ let x = 1; let x = 2; }");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: variable already declared in this scope: x");
  }
}

void testCompileBytecodeFunctionCall() {
  EXPECT(runBytecodeProgram("function add(a, b) {"
                            "  return a + b;"
                            "}"
                            "add(1, 2);")
             .asNumber() == 3);
}

void testCompileBytecodeFunctionReturnsArgument() {
  EXPECT(runBytecodeProgram("function id(x) {"
                            "  return x;"
                            "}"
                            "id(42);")
             .asNumber() == 42);
}

void testCompileBytecodeFunctionParametersUseLocalSlots() {
  EXPECT(runBytecodeProgram("function pick(a, b, c) {"
                            "  return b;"
                            "}"
                            "pick(10, 20, 30);")
             .asNumber() == 20);
}

void testCompileBytecodeFunctionParametersAndLocalsUseSlots() {
  EXPECT(runBytecodeProgram("function addWithLocal(a, b) {"
                            "  let c = a + b;"
                            "  return c;"
                            "}"
                            "addWithLocal(10, 20);")
             .asNumber() == 30);
}

void testCompileBytecodeFunctionParameterRedeclaration() {
  try {
    runBytecodeProgram(
        "function f(a) {"
        "  let a = 2;"
        "  return a;"
        "}"
        "f(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: variable already declared in this scope: a");
  }
}

void testBytecodeParameterShadowsCurrentFunctionName() {
  EXPECT(runBytecodeProgram("function test(test) {"
                            "  return test;"
                            "}"
                            "test(42);")
             .asNumber() == 42);
}

void testBytecodeLocalShadowsCurrentFunctionName() {
  EXPECT(runBytecodeProgram("function get() {"
                            "  let get = 10;"
                            "  return get;"
                            "}"
                            "get();")
             .asNumber() == 10);
}

void testCompileBytecodeFunctionReadsGlobalVariable() {
  EXPECT(runBytecodeProgram("let base = 10;"
                            "function addBase(x) {"
                            "  return x + base;"
                            "}"
                            "addBase(5);")
             .asNumber() == 15);
}

void testCompileBytecodeFunctionAssignsGlobalVariable() {
  EXPECT(runBytecodeProgram("let total = 1;"
                            "function add(x) {"
                            "  total = total + x;"
                            "}"
                            "add(4);"
                            "total;")
             .asNumber() == 5);
}

void testBytecodeCallReturnedFunctionDirectly() {
  EXPECT(runBytecodeProgram("function makeOne() {"
                            "  function one() { return 1; }"
                            "  return one;"
                            "}"
                            "makeOne()();")
             .asNumber() == 1);
}

void testBytecodeClassMethodCall() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  get() { return 123; }"
                            "}"
                            "let b = Box();"
                            "b.get();")
             .asNumber() == 123);
}

void testBytecodeClassBoundMethodCall() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  get() { return 123; }"
                            "}"
                            "let b = Box();"
                            "let get = b.get;"
                            "get();")
             .asNumber() == 123);
}

void testBytecodeClassMethodThisField() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  set(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "let b = Box();"
                            "b.set(42);"
                            "b.get();")
             .asNumber() == 42);
}

void testBytecodeClassInstanceFieldsAreIndependent() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  set(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "let a = Box();"
                            "let b = Box();"
                            "a.set(1);"
                            "b.set(2);"
                            "a.get() + b.get();")
             .asNumber() == 3);
}

void testBytecodeClassInitInitializesInstance() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  init(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "let b = Box(42);"
                            "b.get();")
             .asNumber() == 42);
}

void testBytecodeClassInitReturnValueIsIgnored() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  init(value) { this.value = value; return 999; }"
                            "  get() { return this.value; }"
                            "}"
                            "let b = Box(42);"
                            "b.get();")
             .asNumber() == 42);
}

void testBytecodeClassInitReturnsInstance() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  init() { return 999; }"
                            "  get() { return 123; }"
                            "}"
                            "Box().get();")
             .asNumber() == 123);
}

void testBytecodeClassInitInstancesAreIndependent() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  init(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "let a = Box(1);"
                            "let b = Box(2);"
                            "a.get() + b.get();")
             .asNumber() == 3);
}

void testBytecodeClassMethodCallsAnotherMethod() {
  EXPECT(runBytecodeProgram("class Counter {"
                            "  init(value) { this.value = value; }"
                            "  inc() { this.value = this.value + 1; }"
                            "  next() { this.inc(); return this.value; }"
                            "}"
                            "let c = Counter(10);"
                            "c.next();")
             .asNumber() == 11);
}

void testBytecodeClassInheritsMethod() {
  EXPECT(runBytecodeProgram("class Animal { speak() { return \"animal\"; } }"
                            "class Dog < Animal {}"
                            "Dog().speak();")
             .toString() == "animal");
}

void testBytecodeClassOverridesInheritedMethod() {
  EXPECT(runBytecodeProgram("class Animal { speak() { return \"animal\"; } }"
                            "class Dog < Animal { speak() { return \"dog\"; } }"
                            "Dog().speak();")
             .toString() == "dog");
}

void testBytecodeClassInheritsInit() {
  EXPECT(runBytecodeProgram("class Parent {"
                            "  init(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "class Child < Parent {}"
                            "Child(123).get();")
             .asNumber() == 123);
}

void testBytecodeClassSuperclassMustBeClass() {
  try {
    runBytecodeProgram("let Parent = 123; class Child < Parent {}");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: superclass must be a class");
  }
}

void testBytecodeClassSuperMethodCall() {
  EXPECT(runBytecodeProgram("class Animal { speak() { return \"animal\"; } }"
                            "class Dog < Animal { speak() { return super.speak() + \" dog\"; } }"
                            "Dog().speak();")
             .toString() == "animal dog");
}

void testBytecodeClassSuperMethodUsesCurrentReceiver() {
  EXPECT(runBytecodeProgram("class Parent { value() { return this.name; } }"
                            "class Child < Parent { value() { return super.value() + \" child\"; } }"
                            "let c = Child();"
                            "c.name = \"receiver\";"
                            "c.value();")
             .toString() == "receiver child");
}

void testBytecodeClassSuperAcrossMultipleLevels() {
  EXPECT(runBytecodeProgram("class A { name() { return \"A\"; } }"
                            "class B < A { name() { return super.name() + \"B\"; } }"
                            "class C < B { name() { return super.name() + \"C\"; } }"
                            "C().name();")
             .toString() == "ABC");
}

void testBytecodeClassBoundMethodKeepsSuperBinding() {
  EXPECT(runBytecodeProgram("class A { name() { return \"A\"; } }"
                            "class B < A { name() { return super.name() + \"B\"; } }"
                            "let b = B();"
                            "let f = b.name;"
                            "f();")
             .toString() == "AB");
}

void testBytecodeClassSuperInitInitializesReceiver() {
  EXPECT(runBytecodeProgram("class A { init(value) { this.value = value; } }"
                            "class B < A {"
                            "  init(value) { super.init(value + 1); }"
                            "  get() { return this.value; }"
                            "}"
                            "B(41).get();")
             .asNumber() == 42);
}

void testBytecodeClassSuperMissingMethod() {
  try {
    runBytecodeProgram("class Animal {}"
                       "class Dog < Animal { speak() { return super.missing(); } }"
                       "Dog().speak();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: superclass has no method: missing");
  }
}

void testBytecodeClassSuperOutsideSubclassMethod() {
  try {
    runBytecodeProgram("class Box { get() { return super.get(); } } Box().get();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: super outside subclass method");
  }
}

void testBytecodeSuperOutsideMethod() {
  try {
    runBytecodeProgram("super.get();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: super outside subclass method");
  }
}

void testBytecodeThisOutsideMethod() {
  try {
    runBytecodeProgram("this.name;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: this outside method");
  }
}

void testBytecodeClassMethodUsesDefinitionClosure() {
  EXPECT(runBytecodeProgram("let x = \"global\";"
                            "class Box { get() { return x; } }"
                            "function test() {"
                            "  let x = \"local\";"
                            "  let b = Box();"
                            "  return b.get();"
                            "}"
                            "test();")
             .toString() == "global");
}

void testBytecodeClassBoundMethodUsesDefinitionClosure() {
  EXPECT(runBytecodeProgram("let x = \"global\";"
                            "class Box { get() { return x; } }"
                            "function test(fn) {"
                            "  let x = \"local\";"
                            "  return fn();"
                            "}"
                            "let b = Box();"
                            "test(b.get);")
             .toString() == "global");
}

void testBytecodeClassCallArity() {
  try {
    runBytecodeProgram("class Box {} Box(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: class Box expects 0 arguments");
  }
}

void testBytecodeClassInitArity() {
  try {
    runBytecodeProgram("class Box { init(value) { this.value = value; } } Box();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: method init expects 1 arguments");
  }
}

void testBytecodeClassUnknownMethod() {
  try {
    runBytecodeProgram("class Box {} let b = Box(); b.missing();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value has no method: missing");
  }
}

void testBytecodeClassMissingFieldReturnsUndefined() {
  EXPECT(runBytecodeProgram("class Box {} let b = Box(); b.name;").isUndefined());
}

void testBytecodeClassMethodArity() {
  try {
    runBytecodeProgram("class Box { set(value) { this.value = value; } }"
                       "let b = Box();"
                       "b.set();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: method set expects 1 arguments");
  }
}

void testBytecodeClassBoundMethodArity() {
  try {
    runBytecodeProgram("class Box { set(value) { this.value = value; } }"
                       "let b = Box();"
                       "let set = b.set;"
                       "set();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: method set expects 1 arguments");
  }
}

void testBytecodeClosureKeepsOuterVariable() {
  EXPECT(runBytecodeProgram("function makeCounter() {"
                            "  let count = 0;"
                            "  function next() {"
                            "    count = count + 1;"
                            "    return count;"
                            "  }"
                            "  return next;"
                            "}"
                            "let c = makeCounter();"
                            "c();"
                            "c();")
             .asNumber() == 2);
}

void testBytecodeClosureInstancesAreIndependent() {
  EXPECT(runBytecodeProgram("function makeCounter() {"
                            "  let count = 0;"
                            "  function next() {"
                            "    count = count + 1;"
                            "    return count;"
                            "  }"
                            "  return next;"
                            "}"
                            "let a = makeCounter();"
                            "let b = makeCounter();"
                            "a();"
                            "a();"
                            "b();")
             .asNumber() == 1);
}

void testBytecodeClosuresShareCapturedVariable() {
  EXPECT(runBytecodeProgram("function makePair() {"
                            "  let x = 0;"
                            "  function add() {"
                            "    x = x + 1;"
                            "  }"
                            "  function get() {"
                            "    return x;"
                            "  }"
                            "  return [add, get];"
                            "}"
                            "let pair = makePair();"
                            "pair[0]();"
                            "pair[1]();")
             .asNumber() == 1);
}

void testBytecodeSharedUpvalueObservesMultipleWrites() {
  EXPECT(runBytecodeProgram("function makePair() {"
                            "  let x = 0;"
                            "  function add() {"
                            "    x = x + 1;"
                            "  }"
                            "  function get() {"
                            "    return x;"
                            "  }"
                            "  return [add, get];"
                            "}"
                            "let pair = makePair();"
                            "pair[0]();"
                            "pair[0]();"
                            "pair[1]();")
             .asNumber() == 2);
}

void testBytecodeClosureCapturesThroughMultipleLevels() {
  EXPECT(runBytecodeProgram("function outer() {"
                            "  let x = 10;"
                            "  function middle() {"
                            "    function inner() {"
                            "      return x;"
                            "    }"
                            "    return inner;"
                            "  }"
                            "  return middle;"
                            "}"
                            "let m = outer();"
                            "let i = m();"
                            "i();")
             .asNumber() == 10);
}

void testBytecodeClosureCapturesBlockLocal() {
  EXPECT(runBytecodeProgram("function make() {"
                            "  let get = undefined;"
                            "  {"
                            "    let x = 42;"
                            "    function inner() { return x; }"
                            "    get = inner;"
                            "  }"
                            "  return get;"
                            "}"
                            "let get = make();"
                            "get();")
             .asNumber() == 42);
}

void testBytecodeBreakClosesCapturedLocal() {
  EXPECT(runBytecodeProgram("let saved = undefined;"
                            "while (true) {"
                            "  {"
                            "    let x = 42;"
                            "    function get() { return x; }"
                            "    saved = get;"
                            "    break;"
                            "  }"
                            "}"
                            "saved();")
             .asNumber() == 42);
}

void testBytecodeContinueClosesCapturedLocal() {
  EXPECT(runBytecodeProgram("let saved = undefined;"
                            "let i = 0;"
                            "while (i < 1) {"
                            "  {"
                            "    let x = 99;"
                            "    function get() { return x; }"
                            "    saved = get;"
                            "    i = i + 1;"
                            "    continue;"
                            "  }"
                            "}"
                            "saved();")
             .asNumber() == 99);
}

void testCompileBytecodeFunctionWithoutReturn() {
  EXPECT(runBytecodeProgram("function noReturn() {"
                            "  let x = 1;"
                            "}"
                            "noReturn();")
             .isUndefined());
}

void testBytecodeFunctionArityMismatch() {
  try {
    runBytecodeProgram(
        "function add(a, b) {"
        "  return a + b;"
        "}"
        "add(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: function add expects 2 arguments");
  }
}

void testCompileBytecodeFunctionLocalVariable() {
  EXPECT(runBytecodeProgram("function addOne(x) {"
                            "  let y = x + 1;"
                            "  return y;"
                            "}"
                            "addOne(41);")
             .asNumber() == 42);
}

void testCompileBytecodeRecursiveFunction() {
  EXPECT(runBytecodeProgram("function fact(n) {"
                            "  if (n <= 1) {"
                            "    return 1;"
                            "  }"
                            "  return n * fact(n - 1);"
                            "}"
                            "fact(5);")
             .asNumber() == 120);
}

void testCompileBytecodeRecursiveFibonacci() {
  EXPECT(runBytecodeProgram("function fib(n) {"
                            "  if (n <= 1) {"
                            "    return n;"
                            "  }"
                            "  return fib(n - 1) + fib(n - 2);"
                            "}"
                            "fib(6);")
             .asNumber() == 8);
}

void testBytecodeReturnedLocalFunctionCanRecurse() {
  EXPECT(runBytecodeProgram("function makeFact() {"
                            "  function fact(n) {"
                            "    if (n <= 1) {"
                            "      return 1;"
                            "    }"
                            "    return n * fact(n - 1);"
                            "  }"
                            "  return fact;"
                            "}"
                            "let fact = makeFact();"
                            "fact(5);")
             .asNumber() == 120);
}

void testBytecodeReturnOutsideFunction() {
  try {
    runBytecodeProgram("return 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: return outside function");
  }
}

void testBytecodeReturnInsideTopLevelBlock() {
  try {
    runBytecodeProgram("{ return 1; }");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: return outside function");
  }
}

void testBytecodeCallNonFunction() {
  try {
    runBytecodeProgram(
        "let x = 1;"
        "x();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not callable");
  }
}

void testBytecodePrintBuiltin() {
  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

  const minijs::Value result = runBytecodeProgram("print(1 + 2);");

  std::cout.rdbuf(previous);
  EXPECT(output.str() == "3\n");
  EXPECT(result.isNull());
}

void testBytecodeBuiltinFunctionCanBeAssigned() {
  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

  const minijs::Value result = runBytecodeProgram(
      "let p = print;"
      "p(\"hello\");");

  std::cout.rdbuf(previous);
  EXPECT(output.str() == "hello\n");
  EXPECT(result.isNull());
}

void testBytecodePrintArity() {
  try {
    runBytecodeProgram("print();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: print expects 1 argument");
  }
}

void testBytecodeClockBuiltin() { EXPECT(runBytecodeProgram("clock();").asNumber() == 0); }

void testBytecodeClockArity() {
  try {
    runBytecodeProgram("clock(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: clock expects 0 arguments");
  }
}

void testBytecodeLenArrayBuiltin() {
  EXPECT(runBytecodeProgram("len([1, 2, 3]);").asNumber() == 3);
}

void testBytecodeLenStringBuiltin() { EXPECT(runBytecodeProgram("len(\"Tom\");").asNumber() == 3); }

void testBytecodeLenObjectBuiltin() {
  EXPECT(runBytecodeProgram("len({ name: \"Tom\", age: 18 });").asNumber() == 2);
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");"
                            "len(p);")
             .asNumber() == 1);
}

void testBytecodeLenArity() {
  try {
    runBytecodeProgram("len();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: len expects 1 argument");
  }
}

void testBytecodeLenTypeError() {
  try {
    runBytecodeProgram("len(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: len expects string, array, or object");
  }
}

void testBytecodeTypeOfPrimitiveBuiltins() {
  EXPECT(runBytecodeProgram("typeOf(1);").toString() == "number");
  EXPECT(runBytecodeProgram("typeOf(true);").toString() == "boolean");
  EXPECT(runBytecodeProgram("typeOf(null);").toString() == "null");
  EXPECT(runBytecodeProgram("typeOf(undefined);").toString() == "undefined");
  EXPECT(runBytecodeProgram("typeOf(\"Tom\");").toString() == "string");
}

void testBytecodeTypeOfContainers() {
  EXPECT(runBytecodeProgram("typeOf([1, 2]);").toString() == "array");
  EXPECT(runBytecodeProgram("typeOf({ age: 18 });").toString() == "object");
}

void testBytecodeTypeOfFunctions() {
  EXPECT(runBytecodeProgram("typeOf(print);").toString() == "builtin");
  EXPECT(runBytecodeProgram("function add(a, b) {"
                            "  return a + b;"
                            "}"
                            "typeOf(add);")
             .toString() == "function");
}

void testBytecodeTypeOfArity() {
  try {
    runBytecodeProgram("typeOf();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: typeOf expects 1 argument");
  }
}

void testBytecodeHasBuiltinObjectProperty() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "has(p, \"name\");")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\" };"
                            "has(p, \"score\");")
             .toString() == "false");
}

void testBytecodeHasBuiltinArrayAndStringProperty() {
  EXPECT(runBytecodeProgram("has([1, 2, 3], \"length\");").toString() == "true");
  EXPECT(runBytecodeProgram("has([1, 2, 3], \"push\");").toString() == "true");
  EXPECT(runBytecodeProgram("has([1, 2, 3], \"pop\");").toString() == "true");
  EXPECT(runBytecodeProgram("has([1, 2, 3], \"0\");").toString() == "false");
  EXPECT(runBytecodeProgram("has(\"Tom\", \"length\");").toString() == "true");
  EXPECT(runBytecodeProgram("has(1, \"length\");").toString() == "false");
}

void testBytecodeHasArity() {
  try {
    runBytecodeProgram("has({});");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: has expects 2 arguments");
  }
}

void testBytecodeHasKeyMustBeString() {
  try {
    runBytecodeProgram("has({}, 1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: has key must be a string");
  }
}

void testBytecodeDelBuiltinObjectProperty() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");"
                            "has(p, \"age\");")
             .toString() == "false");

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");")
             .toString() == "true");
}

void testBytecodeDelBuiltinMissingOrNonObject() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\" };"
                            "del(p, \"age\");")
             .toString() == "false");
  EXPECT(runBytecodeProgram("del(123, \"age\");").toString() == "false");
}

void testBytecodeDelArity() {
  try {
    runBytecodeProgram("del({});");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: del expects 2 arguments");
  }
}

void testBytecodeDelKeyMustBeString() {
  try {
    runBytecodeProgram("del({ name: \"Tom\" }, 123);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: del key must be a string");
  }
}

void testBytecodeKeysBuiltinObject() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "len(keys(p));")
             .asNumber() == 2);

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "let ks = keys(p);"
                            "has(p, ks[0]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "let ks = keys(p);"
                            "has(p, ks[1]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "let ks = keys(p);"
                            "ks[0] != ks[1];")
             .toString() == "true");
}

void testBytecodeKeysBuiltinArrayAndString() {
  EXPECT(runBytecodeProgram("len(keys([1, 2]));").asNumber() == 3);
  EXPECT(runBytecodeProgram("let ks = keys([1, 2]);"
                            "has([1, 2], ks[0]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let ks = keys([1, 2]);"
                            "has([1, 2], ks[1]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let ks = keys([1, 2]);"
                            "has([1, 2], ks[2]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("len(keys(\"Tom\"));").asNumber() == 1);
  EXPECT(runBytecodeProgram("len(keys(1));").asNumber() == 0);
}

void testBytecodeKeysArity() {
  try {
    runBytecodeProgram("keys();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: keys expects 1 argument");
  }
}

void testBytecodeUndefinedGlobal() {
  try {
    runBytecodeProgram("x;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: x");
  }
}

void testBytecodeAssignUndefinedGlobal() {
  try {
    runBytecodeProgram("x = 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: x");
  }
}

void testBytecodeProgramWithoutFinalExpressionReturnsNull() {
  EXPECT(runBytecodeProgram("let x = 10;").isNull());
}

void testDisassembleGlobalLetExpression() {
  const minijs::Chunk chunk = compileProgram("let x = 10; x + 20;");

  const std::string expected =
      "0000 OP_CONSTANT 0 10\n"
      "0002 OP_DEFINE_GLOBAL 1 x\n"
      "0004 OP_GET_GLOBAL 2 x\n"
      "0006 OP_CONSTANT 3 20\n"
      "0008 OP_ADD\n"
      "0009 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleGlobalAssignment() {
  const minijs::Chunk chunk = compileProgram("let x = 1; x = x + 2; x;");

  const std::string expected =
      "0000 OP_CONSTANT 0 1\n"
      "0002 OP_DEFINE_GLOBAL 1 x\n"
      "0004 OP_GET_GLOBAL 2 x\n"
      "0006 OP_CONSTANT 3 2\n"
      "0008 OP_ADD\n"
      "0009 OP_SET_GLOBAL 4 x\n"
      "0011 OP_POP\n"
      "0012 OP_GET_GLOBAL 5 x\n"
      "0014 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testBytecodeDivisionByZero() {
  try {
    runBytecode("10 / 0;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
  }
}

void testBytecodeModuloByZero() {
  try {
    runBytecode("10 % 0;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: modulo by zero");
  }
}

void testCompileArrayLiteralIndex() { EXPECT(runBytecode("[1, 2, 3][1];").asNumber() == 2); }

void testCompileArrayVariableIndex() {
  EXPECT(runBytecodeProgram("let a = [10, 20, 30];"
                            "a[2];")
             .asNumber() == 30);
}

void testCompileArrayExpressionElements() {
  EXPECT(runBytecodeProgram("let x = 10;"
                            "[x, x + 1][1];")
             .asNumber() == 11);
}

void testCompileArrayIndexAssignment() {
  EXPECT(runBytecodeProgram("let a = [10, 20];"
                            "a[1] = 99;"
                            "a[1];")
             .asNumber() == 99);
}

void testCompileArrayReferenceSemantics() {
  EXPECT(runBytecodeProgram("let a = [1, 2];"
                            "let b = a;"
                            "b[0] = 99;"
                            "a[0];")
             .asNumber() == 99);
}

void testBytecodeArrayIndexOutOfBounds() {
  try {
    runBytecode("[1][1];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: array index out of bounds");
  }
}

void testBytecodeArrayIndexMustBeInteger() {
  try {
    runBytecode("[1][0.5];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: array index must be a non-negative integer");
  }
}

void testBytecodeIndexNonArrayValue() {
  try {
    runBytecode("1[0];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not an array");
  }
}

void testCompileObjectLiteralProperty() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "p.age;")
             .asNumber() == 18);
}

void testCompileObjectExpressionProperty() {
  EXPECT(runBytecodeProgram("let age = 18;"
                            "let p = { name: \"Tom\", age: age + 1 };"
                            "p.age;")
             .asNumber() == 19);
}

void testCompileMissingObjectProperty() {
  EXPECT(runBytecodeProgram("let p = { age: 18 };"
                            "p.name;")
             .isUndefined());
}

void testBytecodeGetPropertyFromNonObject() {
  try {
    runBytecode("1.age;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not an object");
  }
}

void testCompileObjectPropertyAssignment() {
  EXPECT(runBytecodeProgram("let p = { age: 18 };"
                            "p.age = 20;"
                            "p.age;")
             .asNumber() == 20);
}

void testCompileObjectReferenceSemantics() {
  EXPECT(runBytecodeProgram("let p = { age: 18 };"
                            "let q = p;"
                            "q.age = 20;"
                            "p.age;")
             .asNumber() == 20);
}

void testBytecodeArrayLengthProperty() {
  EXPECT(runBytecodeProgram("[1, 2, 3].length;").asNumber() == 3);
  EXPECT(runBytecodeProgram("let a = [1, 2, 3];"
                            "a.length;")
             .asNumber() == 3);
}

void testBytecodeStringLengthProperty() {
  EXPECT(runBytecodeProgram("\"Tom\".length;").asNumber() == 3);
  EXPECT(runBytecodeProgram("let s = \"hello\";"
                            "s.length;")
             .asNumber() == 5);
}

void testBytecodeObjectOwnLengthProperty() {
  EXPECT(runBytecodeProgram("let p = { length: 99 };"
                            "p.length;")
             .asNumber() == 99);
}

void testBytecodeArrayUnknownProperty() {
  EXPECT(runBytecodeProgram("[1, 2].unknown;").isUndefined());
}

void testBytecodeArrayPushMethod() {
  EXPECT(runBytecodeProgram("let a = [1, 2];"
                            "a.push(3);"
                            "a.length;")
             .asNumber() == 3);
  EXPECT(runBytecodeProgram("let a = [];"
                            "a.push(1);"
                            "a.push(2);"
                            "a[0] + a[1];")
             .asNumber() == 3);
  EXPECT(runBytecodeProgram("let a = [];"
                            "a.push(10);")
             .asNumber() == 1);
  EXPECT(runBytecodeProgram("let a = [];"
                            "a.push(1 + 2);"
                            "a[0];")
             .asNumber() == 3);
}

void testBytecodeArrayPopMethod() {
  EXPECT(runBytecodeProgram("let a = [1, 2];"
                            "a.pop();")
             .asNumber() == 2);
  EXPECT(runBytecodeProgram("let a = [1, 2];"
                            "a.pop();"
                            "a.length;")
             .asNumber() == 1);
  EXPECT(runBytecodeProgram("let a = [];"
                            "a.pop();")
             .isUndefined());
}

void testBytecodeArrayPushArity() {
  try {
    runBytecodeProgram("let a = [];"
                       "a.push();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: push expects 1 argument");
  }
}

void testBytecodeArrayPopArity() {
  try {
    runBytecodeProgram("let a = [];"
                       "a.pop(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: pop expects 0 arguments");
  }
}

void testBytecodeMethodCallOnNonArray() {
  try {
    runBytecodeProgram("\"Tom\".push(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: method call receiver is not an array");
  }
}

void testBytecodeUnknownArrayMethod() {
  try {
    runBytecodeProgram("let a = [];"
                       "a.shift();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: unknown array method: shift");
  }
}

void testDisassembleArrayMethodCall() {
  const minijs::Chunk chunk = compileProgram("let a = [];"
                                             "a.push(1);");

  const std::string expected =
      "0000 OP_ARRAY 0\n"
      "0002 OP_DEFINE_GLOBAL 0 a\n"
      "0004 OP_GET_GLOBAL 1 a\n"
      "0006 OP_CONSTANT 2 1\n"
      "0008 OP_METHOD_CALL 3 push 1\n"
      "0011 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleObjectLiteralProperty() {
  const minijs::Chunk chunk = compileProgram(
      "let p = { name: \"Tom\", age: 18 };"
      "p.age;");

  const std::string expected =
      "0000 OP_CONSTANT 0 Tom\n"
      "0002 OP_CONSTANT 1 18\n"
      "0004 OP_OBJECT 2 [name, age]\n"
      "0006 OP_DEFINE_GLOBAL 3 p\n"
      "0008 OP_GET_GLOBAL 4 p\n"
      "0010 OP_GET_PROPERTY 5 age\n"
      "0012 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

}  // namespace

void runBytecodeTests() {
  testCompileNumberExpression();
  testCompileArithmeticExpression();
  testCompileUnaryMinus();
  testCompileModuloExpression();
  testCompileBooleanNullUndefinedLiterals();
  testCompileStringLiteral();
  testCompileStringConcatenation();
  testCompileComparisonExpressions();
  testCompileLogicalNot();
  testCompileLogicalAndShortCircuit();
  testCompileLogicalOrShortCircuit();
  testCompileLogicalReturnsOperandValue();
  testCompileLogicalWithBuiltins();
  testDisassembleArithmeticExpression();
  testDisassembleUnaryMinus();
  testDisassembleStringLiteral();
  testDisassembleBooleanNullUndefinedLiterals();
  testDisassembleLogicalAndJump();
  testDisassembleLogicalOrJump();
  testDisassembleClosureCapturesOuterLocal();
  testDisassembleClosureCapturesThroughUpvalue();
  testDisassembleClosureSetsUpvalue();
  testDisassembleClosureClosesBlockLocal();
  testDisassembleClassMethod();
  testDisassembleClassInheritance();
  testDisassembleSuperMethodCall();
  testDisassembleBreakClosesUpvalueBeforeJump();
  testDisassembleLocalRecursionGetsCurrentClosure();
  testDisassembleParameterShadowsCurrentClosure();
  testDisassembleWhileBreakJump();
  testDisassembleWhileContinueLoop();
  testDisassembleForContinueJumpsToIncrement();
  testCompileGlobalLet();
  testCompileGlobalExpressionUsesVariable();
  testCompileGlobalAssignment();
  testCompileIfElseStatement();
  testCompileIfWithoutElseStatement();
  testCompileWhileStatement();
  testCompileWhileSkippedStatement();
  testCompileWhileWithNestedIfStatement();
  testCompileForStatement();
  testCompileForSkippedStatement();
  testCompileForWithoutInitializerStatement();
  testCompileWhileBreakStatement();
  testCompileWhileContinueStatement();
  testCompileForBreakStatement();
  testCompileForContinueStatement();
  testCompileBreakContinuePopBlockLocals();
  testCompileBreakOutsideLoop();
  testCompileContinueOutsideLoop();
  testBytecodeFunctionDoesNotInheritOuterBreakContext();
  testBytecodeFunctionDoesNotInheritOuterContinueContext();
  testBytecodeFunctionUsesOwnLoopContext();
  testCompileBlockLocalVariable();
  testCompileNestedBlockLocalVariables();
  testCompileLocalAssignment();
  testCompileLocalShadowsGlobal();
  testCompileDuplicateLocalDeclaration();
  testCompileBytecodeFunctionCall();
  testCompileBytecodeFunctionReturnsArgument();
  testCompileBytecodeFunctionParametersUseLocalSlots();
  testCompileBytecodeFunctionParametersAndLocalsUseSlots();
  testCompileBytecodeFunctionParameterRedeclaration();
  testBytecodeParameterShadowsCurrentFunctionName();
  testBytecodeLocalShadowsCurrentFunctionName();
  testCompileBytecodeFunctionReadsGlobalVariable();
  testCompileBytecodeFunctionAssignsGlobalVariable();
  testBytecodeCallReturnedFunctionDirectly();
  testBytecodeClassMethodCall();
  testBytecodeClassBoundMethodCall();
  testBytecodeClassMethodThisField();
  testBytecodeClassInstanceFieldsAreIndependent();
  testBytecodeClassInitInitializesInstance();
  testBytecodeClassInitReturnValueIsIgnored();
  testBytecodeClassInitReturnsInstance();
  testBytecodeClassInitInstancesAreIndependent();
  testBytecodeClassMethodCallsAnotherMethod();
  testBytecodeClassInheritsMethod();
  testBytecodeClassOverridesInheritedMethod();
  testBytecodeClassInheritsInit();
  testBytecodeClassSuperclassMustBeClass();
  testBytecodeClassSuperMethodCall();
  testBytecodeClassSuperMethodUsesCurrentReceiver();
  testBytecodeClassSuperAcrossMultipleLevels();
  testBytecodeClassBoundMethodKeepsSuperBinding();
  testBytecodeClassSuperInitInitializesReceiver();
  testBytecodeClassSuperMissingMethod();
  testBytecodeClassSuperOutsideSubclassMethod();
  testBytecodeSuperOutsideMethod();
  testBytecodeThisOutsideMethod();
  testBytecodeClassMethodUsesDefinitionClosure();
  testBytecodeClassBoundMethodUsesDefinitionClosure();
  testBytecodeClassCallArity();
  testBytecodeClassInitArity();
  testBytecodeClassUnknownMethod();
  testBytecodeClassMissingFieldReturnsUndefined();
  testBytecodeClassMethodArity();
  testBytecodeClassBoundMethodArity();
  testBytecodeClosureKeepsOuterVariable();
  testBytecodeClosureInstancesAreIndependent();
  testBytecodeClosuresShareCapturedVariable();
  testBytecodeSharedUpvalueObservesMultipleWrites();
  testBytecodeClosureCapturesThroughMultipleLevels();
  testBytecodeClosureCapturesBlockLocal();
  testBytecodeBreakClosesCapturedLocal();
  testBytecodeContinueClosesCapturedLocal();
  testCompileBytecodeFunctionWithoutReturn();
  testBytecodeFunctionArityMismatch();
  testCompileBytecodeFunctionLocalVariable();
  testCompileBytecodeRecursiveFunction();
  testCompileBytecodeRecursiveFibonacci();
  testBytecodeReturnedLocalFunctionCanRecurse();
  testBytecodeReturnOutsideFunction();
  testBytecodeReturnInsideTopLevelBlock();
  testBytecodeCallNonFunction();
  testBytecodePrintBuiltin();
  testBytecodeBuiltinFunctionCanBeAssigned();
  testBytecodePrintArity();
  testBytecodeClockBuiltin();
  testBytecodeClockArity();
  testBytecodeLenArrayBuiltin();
  testBytecodeLenStringBuiltin();
  testBytecodeLenObjectBuiltin();
  testBytecodeLenArity();
  testBytecodeLenTypeError();
  testBytecodeTypeOfPrimitiveBuiltins();
  testBytecodeTypeOfContainers();
  testBytecodeTypeOfFunctions();
  testBytecodeTypeOfArity();
  testBytecodeHasBuiltinObjectProperty();
  testBytecodeHasBuiltinArrayAndStringProperty();
  testBytecodeHasArity();
  testBytecodeHasKeyMustBeString();
  testBytecodeDelBuiltinObjectProperty();
  testBytecodeDelBuiltinMissingOrNonObject();
  testBytecodeDelArity();
  testBytecodeDelKeyMustBeString();
  testBytecodeKeysBuiltinObject();
  testBytecodeKeysBuiltinArrayAndString();
  testBytecodeKeysArity();
  testBytecodeUndefinedGlobal();
  testBytecodeAssignUndefinedGlobal();
  testBytecodeProgramWithoutFinalExpressionReturnsNull();
  testDisassembleGlobalLetExpression();
  testDisassembleGlobalAssignment();
  testBytecodeDivisionByZero();
  testBytecodeModuloByZero();
  testCompileArrayLiteralIndex();
  testCompileArrayVariableIndex();
  testCompileArrayExpressionElements();
  testCompileArrayIndexAssignment();
  testCompileArrayReferenceSemantics();
  testBytecodeArrayIndexOutOfBounds();
  testBytecodeArrayIndexMustBeInteger();
  testBytecodeIndexNonArrayValue();
  testCompileObjectLiteralProperty();
  testCompileObjectExpressionProperty();
  testCompileMissingObjectProperty();
  testBytecodeGetPropertyFromNonObject();
  testCompileObjectPropertyAssignment();
  testCompileObjectReferenceSemantics();
  testBytecodeArrayLengthProperty();
  testBytecodeStringLengthProperty();
  testBytecodeObjectOwnLengthProperty();
  testBytecodeArrayUnknownProperty();
  testBytecodeArrayPushMethod();
  testBytecodeArrayPopMethod();
  testBytecodeArrayPushArity();
  testBytecodeArrayPopArity();
  testBytecodeMethodCallOnNonArray();
  testBytecodeUnknownArrayMethod();
  testDisassembleArrayMethodCall();
  testDisassembleObjectLiteralProperty();
}
