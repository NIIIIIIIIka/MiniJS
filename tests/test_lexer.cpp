#include "minijs/lexer.h"
#include "test_framework.h"

#include <string_view>
#include <vector>

namespace
{
using minijs::Lexer;
using minijs::Token;
using minijs::TokenType;

std::vector<TokenType> scanTypes(std::string_view source)
{
    Lexer lexer(source);
    std::vector<TokenType> types;

    while (true)
    {
        const Token token = lexer.next();
        types.push_back(token.type);

        if (token.type == TokenType::Eof)
        {
            return types;
        }
    }
}

void testBasicExpression()
{
    const std::vector<TokenType> expected{
        TokenType::Let,
        TokenType::Identifier,
        TokenType::Equal,
        TokenType::Number,
        TokenType::Plus,
        TokenType::Number,
        TokenType::Semicolon,
        TokenType::Eof,
    };

    EXPECT(scanTypes("let a = 10 + 20;") == expected);
}

void testEmptyInput()
{
    const std::vector<TokenType> expected{TokenType::Eof};
    EXPECT(scanTypes("") == expected);
}

void testKeywordAndIdentifier()
{
    Lexer lexer("let letter let2");

    const Token keyword = lexer.next();
    const Token identifier = lexer.next();
    const Token identifierWithDigit = lexer.next();

    EXPECT(keyword.type == TokenType::Let);
    EXPECT(keyword.lexeme == "let");
    EXPECT(identifier.type == TokenType::Identifier);
    EXPECT(identifier.lexeme == "letter");
    EXPECT(identifierWithDigit.type == TokenType::Identifier);
    EXPECT(identifierWithDigit.lexeme == "let2");
}

void testTokenLocations()
{
    Lexer lexer("let\n  value");

    const Token keyword = lexer.next();
    const Token identifier = lexer.next();

    EXPECT(keyword.location.offset == 0);
    EXPECT(keyword.location.line == 1);
    EXPECT(keyword.location.column == 1);
    EXPECT(identifier.location.offset == 6);
    EXPECT(identifier.location.line == 2);
    EXPECT(identifier.location.column == 3);
}

void testPunctuationWithoutWhitespace()
{
    const std::vector<TokenType> expected{
        TokenType::Number,
        TokenType::Plus,
        TokenType::Number,
        TokenType::Comma,
        TokenType::Identifier,
        TokenType::Dot,
        TokenType::Identifier,
        TokenType::Semicolon,
        TokenType::Eof,
    };

    EXPECT(scanTypes("1+2,a.b;") == expected);
}
}

void runLexerTests()
{
    testBasicExpression();
    testEmptyInput();
    testKeywordAndIdentifier();
    testTokenLocations();
    testPunctuationWithoutWhitespace();
}
