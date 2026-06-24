#include "minijs/ast.h"
#include "minijs/parser.h"
#include "test_framework.h"

#include <memory>
#include <string>
#include <string_view>

namespace
{
std::string parseToString(std::string_view source)
{
    minijs::Parser parser(source);
    minijs::ExprPtr expression = parser.parse();

    EXPECT(parser.diagnostics().empty());
    return minijs::formatExpr(*expression);
}

void testExpressionPrecedence()
{
    EXPECT(parseToString("1 + 2 * 3;") == "(+ 1 (* 2 3))");
}

void testGrouping()
{
    EXPECT(parseToString("(1 + 2) * 3;") == "(* (group (+ 1 2)) 3)");
}

void testLeftAssociativity()
{
    EXPECT(parseToString("10 / 2 - 3;") == "(- (/ 10 2) 3)");
}

void testPercentOperator()
{
    EXPECT(parseToString("10 % 3 + 1;") == "(+ (% 10 3) 1)");
}

void testExpressionWithoutSemicolon()
{
    EXPECT(parseToString("1 + 2") == "(+ 1 2)");
}

void testUnexpectedTokenDiagnostic()
{
    minijs::Parser parser("1 + ;");
    minijs::ExprPtr expression = parser.parse();

    EXPECT(expression != nullptr);
    EXPECT(!parser.diagnostics().empty());
    EXPECT(parser.diagnostics()[0].message == "expected expression");
}

void testMissingRightParenDiagnostic()
{
    minijs::Parser parser("(1 + 2;");
    minijs::ExprPtr expression = parser.parse();

    EXPECT(expression != nullptr);
    EXPECT(!parser.diagnostics().empty());
    EXPECT(parser.diagnostics()[0].message == "expected ')' after expression");
}
}

void runParserTests()
{
    testExpressionPrecedence();
    testGrouping();
    testLeftAssociativity();
    testPercentOperator();
    testExpressionWithoutSemicolon();
    testUnexpectedTokenDiagnostic();
    testMissingRightParenDiagnostic();
}
