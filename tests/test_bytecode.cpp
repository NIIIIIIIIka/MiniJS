#include <iostream>
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
    EXPECT(std::string_view(error.what()) == "RuntimeError: len expects string or array");
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
  testDisassembleArithmeticExpression();
  testDisassembleUnaryMinus();
  testDisassembleStringLiteral();
  testDisassembleBooleanNullUndefinedLiterals();
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
  testCompileBlockLocalVariable();
  testCompileNestedBlockLocalVariables();
  testCompileLocalAssignment();
  testCompileLocalShadowsGlobal();
  testCompileDuplicateLocalDeclaration();
  testCompileBytecodeFunctionCall();
  testCompileBytecodeFunctionReturnsArgument();
  testCompileBytecodeFunctionWithoutReturn();
  testBytecodeFunctionArityMismatch();
  testCompileBytecodeFunctionLocalVariable();
  testCompileBytecodeRecursiveFunction();
  testCompileBytecodeRecursiveFibonacci();
  testBytecodeCallNonFunction();
  testBytecodePrintBuiltin();
  testBytecodeBuiltinFunctionCanBeAssigned();
  testBytecodePrintArity();
  testBytecodeClockBuiltin();
  testBytecodeClockArity();
  testBytecodeLenArrayBuiltin();
  testBytecodeLenStringBuiltin();
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
  testBytecodeKeysBuiltinObject();
  testBytecodeKeysBuiltinArrayAndString();
  testBytecodeKeysArity();
  testBytecodeUndefinedGlobal();
  testBytecodeProgramWithoutFinalExpressionReturnsNull();
  testDisassembleGlobalLetExpression();
  testDisassembleGlobalAssignment();
  testBytecodeDivisionByZero();
  testCompileArrayLiteralIndex();
  testCompileArrayVariableIndex();
  testCompileArrayExpressionElements();
  testCompileArrayIndexAssignment();
  testCompileArrayReferenceSemantics();
  testBytecodeArrayIndexOutOfBounds();
  testBytecodeArrayIndexMustBeInteger();
  testCompileObjectLiteralProperty();
  testCompileObjectExpressionProperty();
  testCompileMissingObjectProperty();
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
