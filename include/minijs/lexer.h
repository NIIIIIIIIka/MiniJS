#pragma once

#include "minijs/diagnostic.h"
#include "minijs/source_location.h"
#include "minijs/token.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace minijs
{

// Converts a source buffer into a stream of JavaScript-like tokens.
class Lexer
{
public:
    explicit Lexer(std::string_view source);

    // Kept for existing callers; prefer nextToken() in new code.
    Token next();

    // Returns the next token and advances the lexer.
    Token nextToken();

    const std::vector<Diagnostic>& diagnostics() const;

private:
    bool isAtEnd() const;
    char advance();
    char peek() const;
    char peekNext() const;

    // Skips trivia that should not produce tokens.
    void skipWhitespace();

    Token makeToken(TokenType type, std::size_t start, SourceLocation startLocation) const;
    Token scanIdentifier(std::size_t start, SourceLocation startLocation);
    Token scanNumber(std::size_t start, SourceLocation startLocation);
    Token scanString(std::size_t start, SourceLocation startLocation);

    // Consumes the current character only when it equals expected.
    bool match(char expected);

    static bool isIdentifierStart(char ch);
    static bool isIdentifierPart(char ch);
    static bool isDigit(char ch);

    void report(SourceLocation location, std::string message);

    std::string_view source_;
    std::size_t current_ = 0; // Index of the next unread character.
    SourceLocation location_;
    std::vector<Diagnostic> diagnostics_;
};

}
