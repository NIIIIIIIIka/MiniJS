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

void testBooleanAndNullLiterals() {
  EXPECT(parseToString("true;") == "true");
  EXPECT(parseToString("false;") == "false");
  EXPECT(parseToString("null;") == "null");
}

void testArrayLiteral() {
  EXPECT(parseToString("[1, 2, 3];") == "(array 1 2 3)");
  EXPECT(parseToString("[];") == "(array)");
}

void testObjectLiteral() {
  EXPECT(parseToString("{age: 18, score: 100};") == "(object (age 18) (score 100))");
  EXPECT(parseToString("{};") == "(object)");
}

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

void testComparisonExpression() { EXPECT(parseToString("x > 5;") == "(> x 5)"); }

void testIfStatementWithElse() {
  EXPECT(parseProgramToString("if (x > 5) x + 1; else x - 1;") ==
         "(if (> x 5) (expr (+ x 1)) (expr (- x 1)))");
}

void testIfStatementWithoutElse() {
  EXPECT(parseProgramToString("if (1) 10;") == "(if 1 (expr 10))");
}

void testBlockStatement() {
  EXPECT(parseProgramToString("{ let x = 10; x + 1; }") == "(block (let x 10) (expr (+ x 1)))");
}

void testIfStatementWithBlockBranches() {
  EXPECT(parseProgramToString("if (x > 5) { x + 1; } else { x - 1; }") ==
         "(if (> x 5) (block (expr (+ x 1))) (block (expr (- x 1))))");
}

void testAssignmentExpression() {
  EXPECT(parseProgramToString("x = x + 1;") == "(expr (assign x (+ x 1)))");
}

void testIndexExpression() {
  EXPECT(parseProgramToString("a[0];") == "(expr (index a 0))");
  EXPECT(parseToString("[1, 2, 3][1];") == "(index (array 1 2 3) 1)");
}

void testIndexAssignmentExpression() {
  EXPECT(parseProgramToString("a[1] = 10;") == "(expr (index-assign a 1 10))");
}

void testGetExpression() { EXPECT(parseProgramToString("p.age;") == "(expr (get p age))"); }

void testSetExpression() { EXPECT(parseProgramToString("p.age = 20;") == "(expr (set p age 20))"); }

void testWhileStatement() {
  EXPECT(parseProgramToString("while (i < 3) { i = i + 1; }") ==
         "(while (< i 3) (block (expr (assign i (+ i 1)))))");
}

void testCallExpression() {
  EXPECT(parseProgramToString("print(1 + 2);") == "(expr (call print (+ 1 2)))");
}

void testCallExpressionAsFactorOperand() {
  EXPECT(parseToString("2 * print(1);") == "(* 2 (call print 1))");
}

void testFunctionDeclaration() {
  EXPECT(parseProgramToString("function add(a, b) { a + b; }") ==
         "(function add (a b) (block (expr (+ a b))))");
}

void testReturnStatement() {
  EXPECT(parseProgramToString("function add(a, b) { return a + b; }") ==
         "(function add (a b) (block (return (+ a b))))");
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

void testMissingRightBraceDiagnostic() {
  minijs::Parser parser("{ let x = 10;");
  minijs::Program program = parser.parseProgram();

  EXPECT(!program.empty());
  EXPECT(!parser.diagnostics().empty());
  EXPECT(parser.diagnostics()[0].message == "expected '}' after block");
}

void testInvalidAssignmentTargetDiagnostic() {
  minijs::Parser parser("1 = 2;");
  minijs::Program program = parser.parseProgram();

  EXPECT(!program.empty());
  EXPECT(!parser.diagnostics().empty());
  EXPECT(parser.diagnostics()[0].message == "invalid assignment target");
}

void testMissingRightParenAfterArgumentsDiagnostic() {
  minijs::Parser parser("print(1;");
  minijs::Program program = parser.parseProgram();

  EXPECT(!program.empty());
  EXPECT(!parser.diagnostics().empty());
  EXPECT(parser.diagnostics()[0].message == "expected ')' after arguments");
}

void testMissingFunctionNameDiagnostic() {
  minijs::Parser parser("function (a) { a; }");
  minijs::Program program = parser.parseProgram();

  EXPECT(!program.empty());
  EXPECT(!parser.diagnostics().empty());
  EXPECT(parser.diagnostics()[0].message == "expected function name");
}
}  // namespace

void runParserTests() {
  testExpressionPrecedence();
  testGrouping();
  testLeftAssociativity();
  testPercentOperator();
  testVariableExpression();
  testBooleanAndNullLiterals();
  testArrayLiteral();
  testObjectLiteral();
  testExpressionWithoutSemicolon();
  testLetStatement();
  testLetStatementWithVariableInitializer();
  testExpressionStatement();
  testProgram();
  testComparisonExpression();
  testIfStatementWithElse();
  testIfStatementWithoutElse();
  testBlockStatement();
  testIfStatementWithBlockBranches();
  testAssignmentExpression();
  testIndexExpression();
  testIndexAssignmentExpression();
  testGetExpression();
  testSetExpression();
  testWhileStatement();
  testCallExpression();
  testCallExpressionAsFactorOperand();
  testFunctionDeclaration();
  testReturnStatement();
  testUnexpectedTokenDiagnostic();
  testMissingRightParenDiagnostic();
  testMissingVariableNameDiagnostic();
  testMissingRightBraceDiagnostic();
  testInvalidAssignmentTargetDiagnostic();
  testMissingRightParenAfterArgumentsDiagnostic();
  testMissingFunctionNameDiagnostic();
}
