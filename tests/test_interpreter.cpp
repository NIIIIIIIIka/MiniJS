#include <stdexcept>
#include <string_view>

#include "minijs/interpreter.h"
#include "minijs/parser.h"
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

void testUndefinedVariable() {
  try {
    run("x + 1;");
    EXPECT(false);
  } catch (const std::runtime_error& error) {
    EXPECT(std::string_view(error.what()) == "undefined variable: x");
  }
}

}  // namespace

void runInterpreterTests() {
  testExpressionEvaluation();
  testLetAndVariable();
  testMultipleLets();
  testPercent();
  testGrouping();
  testUndefinedVariable();
}
