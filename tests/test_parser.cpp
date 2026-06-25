#include <memory>
#include <string>
#include <string_view>

#include "minijs/ast.h"
#include "minijs/parser.h"
#include "test_framework.h"

namespace {
std::string parseToString(std::string_view source) {
  minijs::Parser parser(source);
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());
  return minijs::formatExpr(*expression);
}

std::string parseProgramToString(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());
  return minijs::formatProgram(program);
}

void testExpressionPrecedence() { EXPECT(parseToString("1 + 2 * 3;") == "(+ 1 (* 2 3))"); }

void testGrouping() { EXPECT(parseToString("(1 + 2) * 3;") == "(* (group (+ 1 2)) 3)"); }

void testLeftAssociativity() { EXPECT(parseToString("10 / 2 - 3;") == "(- (/ 10 2) 3)"); }

void testPercentOperator() { EXPECT(parseToString("10 % 3 + 1;") == "(+ (% 10 3) 1)"); }

void testVariableExpression() { EXPECT(parseToString("x + 20;") == "(+ x 20)"); }

void testExpressionWithoutSemicolon() { EXPECT(parseToString("1 + 2") == "(+ 1 2)"); }

void testLetStatement() { EXPECT(parseProgramToString("let x = 10;") == "(let x 10)"); }

void testLetStatementWithVariableInitializer() {
  EXPECT(parseProgramToString("let y = x + 20;") == "(let y (+ x 20))");
}

void testExpressionStatement() { EXPECT(parseProgramToString("x + y;") == "(expr (+ x y))"); }

void testProgram() {
  const std::string expected =
      "(let x 10)\n"
      "(let y (+ x 20))\n"
      "(expr (+ x y))";

  EXPECT(parseProgramToString("let x = 10; let y = x + 20; x + y;") == expected);
}

void testUnexpectedTokenDiagnostic() {
  minijs::Parser parser("1 + ;");
  minijs::ExprPtr expression = parser.parse();

  EXPECT(expression != nullptr);
  EXPECT(!parser.diagnostics().empty());
  EXPECT(parser.diagnostics()[0].message == "expected expression");
}

void testMissingRightParenDiagnostic() {
  minijs::Parser parser("(1 + 2;");
  minijs::ExprPtr expression = parser.parse();

  EXPECT(expression != nullptr);
  EXPECT(!parser.diagnostics().empty());
  EXPECT(parser.diagnostics()[0].message == "expected ')' after expression");
}

void testMissingVariableNameDiagnostic() {
  minijs::Parser parser("let = 10;");
  minijs::Program program = parser.parseProgram();

  EXPECT(program.empty() || program[0] == nullptr);
  EXPECT(!parser.diagnostics().empty());
  EXPECT(parser.diagnostics()[0].message == "expected variable name");
}
}  // namespace

void runParserTests() {
  testExpressionPrecedence();
  testGrouping();
  testLeftAssociativity();
  testPercentOperator();
  testVariableExpression();
  testExpressionWithoutSemicolon();
  testLetStatement();
  testLetStatementWithVariableInitializer();
  testExpressionStatement();
  testProgram();
  testUnexpectedTokenDiagnostic();
  testMissingRightParenDiagnostic();
  testMissingVariableNameDiagnostic();
}
