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

void testIfTrueBranch() { EXPECT(run("if (1) 10; else 20;").asNumber() == 10); }

void testIfFalseBranch() { EXPECT(run("if (0) 10; else 20;").asNumber() == 20); }

void testIfWithVariableCondition() {
  EXPECT(run("let x = 10; if (x > 5) x + 1; else x - 1;").asNumber() == 11);
}

void testIfWithoutElse() { EXPECT(run("if (0) 10;").toString() == "null"); }

void testBlock() { EXPECT(run("{ let x = 10; x + 1; }").asNumber() == 11); }

void testIfWithBlockBranches() {
  EXPECT(run("let x = 10; if (x > 5) { x + 1; } else { x - 1; }").asNumber() == 11);
}

void testAssignment() { EXPECT(run("let x = 1; x = x + 2; x;").asNumber() == 3); }

void testWhileLoop() { EXPECT(run("let i = 0; while (i < 3) { i = i + 1; } i;").asNumber() == 3); }

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
  testIfTrueBranch();
  testIfFalseBranch();
  testIfWithVariableCondition();
  testIfWithoutElse();
  testBlock();
  testIfWithBlockBranches();
  testAssignment();
  testWhileLoop();
  testUndefinedVariable();
  testDivisionByZero();
  testModuloByZero();
  testAssignUndefinedVariable();
}
