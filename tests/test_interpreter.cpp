#include <iostream>
#include <sstream>
#include <string_view>

#include "minijs/interpreter.h"
#include "minijs/parser.h"
#include "minijs/runtime_error.h"
#include "test_framework.h"

namespace {

minijs::Value run(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Interpreter interpreter;
  return interpreter.interpret(program);
}

void testExpressionEvaluation() { EXPECT(run("1 + 2 * 3;").asNumber() == 7); }

void testLetAndVariable() { EXPECT(run("let x = 10; x + 5;").asNumber() == 15); }

void testMultipleLets() { EXPECT(run("let x = 10; let y = x + 20; x + y;").asNumber() == 40); }

void testPercent() { EXPECT(run("10 % 3;").asNumber() == 1); }

void testGrouping() { EXPECT(run("(1 + 2) * 3;").asNumber() == 9); }

void testComparison() {
  EXPECT(run("1 > 2;").toString() == "false");
  EXPECT(run("2 >= 2;").toString() == "true");
}

void testBooleanAndNullLiterals() {
  EXPECT(run("true;").toString() == "true");
  EXPECT(run("false;").toString() == "false");
  EXPECT(run("null;").isNull());
}

void testIfTrueBranch() { EXPECT(run("if (1) 10; else 20;").asNumber() == 10); }

void testIfFalseBranch() { EXPECT(run("if (0) 10; else 20;").asNumber() == 20); }

void testIfWithBooleanAndNullConditions() {
  EXPECT(run("if (true) 10; else 20;").asNumber() == 10);
  EXPECT(run("if (false) 10; else 20;").asNumber() == 20);
  EXPECT(run("if (null) 10; else 20;").asNumber() == 20);
}

void testIfWithVariableCondition() {
  EXPECT(run("let x = 10; if (x > 5) x + 1; else x - 1;").asNumber() == 11);
}

void testIfWithoutElse() { EXPECT(run("if (0) 10;").toString() == "null"); }

void testBlock() { EXPECT(run("{ let x = 10; x + 1; }").asNumber() == 11); }

void testIfWithBlockBranches() {
  EXPECT(run("let x = 10; if (x > 5) { x + 1; } else { x - 1; }").asNumber() == 11);
}

void testBlockScopeShadowsOuterVariable() {
  EXPECT(run("let x = 1; { let x = 2; x; } x;").asNumber() == 1);
}

void testBlockScopeDoesNotLeakVariable() {
  try {
    run("{ let y = 10; } y;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: y");
  }
}

void testBlockAssignsOuterVariable() { EXPECT(run("let x = 1; { x = 3; } x;").asNumber() == 3); }

void testAssignment() { EXPECT(run("let x = 1; x = x + 2; x;").asNumber() == 3); }

void testArrayLiteralAndIndex() {
  EXPECT(run("let a = [1, 2, 3]; a[0];").asNumber() == 1);
  EXPECT(run("let a = [1 + 2, 4 * 5]; a[0] + a[1];").asNumber() == 23);
  EXPECT(run("[10, 20, 30][1];").asNumber() == 20);
}

void testArrayElementAssignment() {
  EXPECT(run("let a = [1, 2, 3]; a[1] = 10; a[1];").asNumber() == 10);
}

void testArrayReferenceSemantics() {
  EXPECT(run("let a = [1, 2, 3]; let b = a; b[0] = 99; a[0];").asNumber() == 99);
}

void testArrayIndexOutOfBounds() {
  try {
    run("let a = [1]; a[1];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: array index out of bounds");
  }
}

void testArrayIndexMustBeInteger() {
  try {
    run("let a = [1]; a[0.5];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: array index must be a non-negative integer");
  }
}

void testIndexNonArrayValue() {
  try {
    run("1[0];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not an array");
  }
}

void testWhileLoop() { EXPECT(run("let i = 0; while (i < 3) { i = i + 1; } i;").asNumber() == 3); }

void testPrint() {
  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

  const minijs::Value result = run("print(1 + 2);");

  std::cout.rdbuf(previous);
  EXPECT(output.str() == "3\n");
  EXPECT(result.isNull());
}

void testUnknownFunction() {
  try {
    run("unknown(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined function: unknown");
  }
}

void testPrintArity() {
  try {
    run("print();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: print expects 1 argument");
  }
}

void testFunctionCall() { EXPECT(run("function add(a, b) { a + b; } add(1, 2);").asNumber() == 3); }

void testFunctionCallCanUseOuterVariable() {
  EXPECT(run("let base = 10; function add(x) { base + x; } add(5);").asNumber() == 15);
}

void testFunctionArity() {
  try {
    run("function add(a, b) { a + b; } add(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: function add expects 2 arguments");
  }
}

void testNonCallableValue() {
  try {
    run("let x = 1; x();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: x is not callable");
  }
}

void testReturnFromFunction() {
  EXPECT(run("function add(a, b) { return a + b; } add(1, 2);").asNumber() == 3);
}

void testEarlyReturnFromFunction() {
  EXPECT(run("function test(x) { if (x > 0) { return 1; } return 2; } test(10);").asNumber() == 1);
}

void testRecursiveFunction() {
  EXPECT(run("function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } fact(5);")
             .asNumber() == 120);
}

void testReturnOutsideFunction() {
  try {
    run("return 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: return outside function");
  }
}

void testUndefinedVariable() {
  try {
    run("x + 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: x");
  }
}

void testDivisionByZero() {
  try {
    run("10 / 0;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
  }
}

void testModuloByZero() {
  try {
    run("10 % 0;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: modulo by zero");
  }
}

void testAssignUndefinedVariable() {
  try {
    run("x = 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: x");
  }
}

}  // namespace

void runInterpreterTests() {
  testExpressionEvaluation();
  testLetAndVariable();
  testMultipleLets();
  testPercent();
  testGrouping();
  testComparison();
  testBooleanAndNullLiterals();
  testIfTrueBranch();
  testIfFalseBranch();
  testIfWithBooleanAndNullConditions();
  testIfWithVariableCondition();
  testIfWithoutElse();
  testBlock();
  testIfWithBlockBranches();
  testBlockScopeShadowsOuterVariable();
  testBlockScopeDoesNotLeakVariable();
  testBlockAssignsOuterVariable();
  testAssignment();
  testArrayLiteralAndIndex();
  testArrayElementAssignment();
  testArrayReferenceSemantics();
  testArrayIndexOutOfBounds();
  testArrayIndexMustBeInteger();
  testIndexNonArrayValue();
  testWhileLoop();
  testPrint();
  testUnknownFunction();
  testPrintArity();
  testFunctionCall();
  testFunctionCallCanUseOuterVariable();
  testFunctionArity();
  testNonCallableValue();
  testReturnFromFunction();
  testEarlyReturnFromFunction();
  testRecursiveFunction();
  testReturnOutsideFunction();
  testUndefinedVariable();
  testDivisionByZero();
  testModuloByZero();
  testAssignUndefinedVariable();
}
