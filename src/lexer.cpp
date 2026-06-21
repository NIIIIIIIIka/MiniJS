#include "minijs/lexer.h"

namespace minijs
{

Lexer::Lexer(std::string_view source)
    : source_(source)
{
}

bool Lexer::isAtEnd() const
{
    return current_ >= source_.size();
}

char Lexer::advance()
{
    const char ch = source_[current_++];
    location_.offset = current_;

    if (ch == '\n') {
        ++location_.line;
        location_.column = 1;
    }
    else {
        ++location_.column;
    }
    return ch;
}

char Lexer::peek() const
{
    if (isAtEnd()) {
        return '\0';
    }
    return source_[current_];
}

void Lexer::skipWhitespace()
{
    while (!isAtEnd())
    {
        switch (peek())
        {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            advance();
            break;

        default:
            return;
        }
    }
}

Token Lexer::makeToken(TokenType type, std::size_t start, SourceLocation startLocation) const {
    return Token{
        type,
        source_.substr(start, current_ - start),
        startLocation
    };
}
Token Lexer::next()
{
    skipWhitespace();

    const std::size_t start = current_;
    const SourceLocation startLocation = location_;

    if (isAtEnd())
    {
        return makeToken(TokenType::Eof, start, startLocation);
    }

    const char ch = advance();

    if (isIdentifierStart(ch))
    {
        return scanIdentifier(start, startLocation);
    }

    if (isDigit(ch))
    {
        return scanNumber(start, startLocation);
    }

    switch (ch)
    {
    case '(':
        return makeToken(TokenType::LeftParen, start, startLocation);
    case ')':
        return makeToken(TokenType::RightParen, start, startLocation);
    case '{':
        return makeToken(TokenType::LeftBrace, start, startLocation);
    case '}':
        return makeToken(TokenType::RightBrace, start, startLocation);
    case ',':
        return makeToken(TokenType::Comma, start, startLocation);
    case '.':
        return makeToken(TokenType::Dot, start, startLocation);
    case '+':
        return makeToken(TokenType::Plus, start, startLocation);
    case '-':
        return makeToken(TokenType::Minus, start, startLocation);
    case '*':
        return makeToken(TokenType::Star, start, startLocation);
    case '/':
        return makeToken(TokenType::Slash, start, startLocation);
    case '=':
        return makeToken(TokenType::Equal, start, startLocation);
    case ';':
        return makeToken(TokenType::Semicolon, start, startLocation);
    default:
        return makeToken(TokenType::Invalid, start, startLocation);
    }
}
Token Lexer::scanIdentifier(std::size_t start, SourceLocation startLocation)
{
    while (isIdentifierPart(peek()))
    {
        advance();
    }
    const std::string_view text =
        source_.substr(start, current_ - start);

    const TokenType type =
        text == "let" ? TokenType::Let : TokenType::Identifier;
    return makeToken(type,start,startLocation);
}

Token Lexer::scanNumber(std::size_t start, SourceLocation startLocation)
{
    while (isDigit(peek()))
    {
        advance();
    }
    return makeToken(TokenType::Number, start, startLocation);
}

bool Lexer::isIdentifierStart(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        ch == '_';
}

bool Lexer::isIdentifierPart(char ch)
{
    return isIdentifierStart(ch) ||
        (ch >= '0' && ch <= '9');
}

bool Lexer::isDigit(char ch)
{
    return ch>='0' && ch<='9';
}

}
