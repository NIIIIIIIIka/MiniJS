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
        const Token token = lexer.nextToken();
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

    const Token keyword = lexer.nextToken();
    const Token identifier = lexer.nextToken();
    const Token identifierWithDigit = lexer.nextToken();

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

    const Token keyword = lexer.nextToken();
    const Token identifier = lexer.nextToken();

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

void testBracketsAndControlKeywords()
{
    const std::vector<TokenType> expected{
        TokenType::If,
        TokenType::LeftParen,
        TokenType::True,
        TokenType::RightParen,
        TokenType::LeftBrace,
        TokenType::While,
        TokenType::LeftParen,
        TokenType::False,
        TokenType::RightParen,
        TokenType::LeftBracket,
        TokenType::RightBracket,
        TokenType::RightBrace,
        TokenType::Else,
        TokenType::Function,
        TokenType::Return,
        TokenType::Eof,
    };

    EXPECT(scanTypes("if (true) { while(false) [] } else function return") == expected);
}

void testOperators()
{
    const std::vector<TokenType> expected{
        TokenType::Bang,
        TokenType::BangEqual,
        TokenType::Equal,
        TokenType::EqualEqual,
        TokenType::Less,
        TokenType::LessEqual,
        TokenType::Greater,
        TokenType::GreaterEqual,
        TokenType::Slash,
        TokenType::Percent,
        TokenType::Eof,
    };

    EXPECT(scanTypes("! != = == < <= > >= / %") == expected);
}

void testLiteralsAndComments()
{
    Lexer lexer("true false null undefined \"hello\\\"world\" // ignored\nname");

    const Token trueToken = lexer.nextToken();
    const Token falseToken = lexer.nextToken();
    const Token nullToken = lexer.nextToken();
    const Token undefinedToken = lexer.nextToken();
    const Token stringToken = lexer.nextToken();
    const Token identifierToken = lexer.nextToken();

    EXPECT(trueToken.type == TokenType::True);
    EXPECT(falseToken.type == TokenType::False);
    EXPECT(nullToken.type == TokenType::Null);
    EXPECT(undefinedToken.type == TokenType::Undefined);
    EXPECT(stringToken.type == TokenType::String);
    EXPECT(stringToken.lexeme == "\"hello\\\"world\"");
    EXPECT(identifierToken.type == TokenType::Identifier);
    EXPECT(identifierToken.lexeme == "name");
}

void testNumbers()
{
    Lexer lexer("12 3.14 5.");

    const Token integer = lexer.nextToken();
    const Token decimal = lexer.nextToken();
    const Token trailingDotNumber = lexer.nextToken();
    const Token dot = lexer.nextToken();

    EXPECT(integer.type == TokenType::Number);
    EXPECT(integer.lexeme == "12");
    EXPECT(decimal.type == TokenType::Number);
    EXPECT(decimal.lexeme == "3.14");
    EXPECT(trailingDotNumber.type == TokenType::Number);
    EXPECT(trailingDotNumber.lexeme == "5");
    EXPECT(dot.type == TokenType::Dot);
}

void testInvalidTokens()
{
    Lexer lexer("@ \"unterminated");

    const Token invalidCharacter = lexer.nextToken();
    const Token invalidString = lexer.nextToken();

    EXPECT(invalidCharacter.type == TokenType::Invalid);
    EXPECT(invalidCharacter.lexeme == "@");
    EXPECT(invalidString.type == TokenType::Invalid);
    EXPECT(lexer.diagnostics().size() == 2);
    EXPECT(lexer.diagnostics()[0].message == "unexpected character");
    EXPECT(lexer.diagnostics()[1].message == "unterminated string");
}

void testUnterminatedStringAtNewline()
{
    Lexer lexer("\"unterminated\nlet x = 1;");

    const Token invalidString = lexer.nextToken();
    const Token identifier = lexer.nextToken();

    EXPECT(invalidString.type == TokenType::Invalid);
    EXPECT(identifier.type == TokenType::Let);
    EXPECT(lexer.diagnostics().size() == 1);
    EXPECT(lexer.diagnostics()[0].location.line == 1);
    EXPECT(lexer.diagnostics()[0].location.column == 1);
}

void testTokenTypeName()
{
    EXPECT(minijs::tokenTypeName(TokenType::Let) == "Let");
    EXPECT(minijs::tokenTypeName(TokenType::String) == "String");
    EXPECT(minijs::tokenTypeName(TokenType::Eof) == "Eof");
}
}

void runLexerTests()
{
    testBasicExpression();
    testEmptyInput();
    testKeywordAndIdentifier();
    testTokenLocations();
    testPunctuationWithoutWhitespace();
    testBracketsAndControlKeywords();
    testOperators();
    testLiteralsAndComments();
    testNumbers();
    testInvalidTokens();
    testUnterminatedStringAtNewline();
    testTokenTypeName();
}
