#pragma once

#include "xlang/error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xlang {

enum class TokenKind {
    End,
    Fn,
    Local,
    Return,
    Import,
    From,
    As,
    Export,
    External,
    Declare,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Semicolon,
    Plus,
    Minus,
    Star,
    Slash,
    Eq,
    Ident,
    Number,
};

struct Token {
    TokenKind kind{TokenKind::End};
    std::string text;
    std::int64_t number{0};
    std::size_t line{1};
    std::size_t column{1};
};

class Lexer {
public:
    explicit Lexer(std::string source);

    [[nodiscard]] const std::vector<Token>& tokens() const { return tokens_; }

private:
    void tokenize();

    [[nodiscard]] char peek() const;
    [[nodiscard]] char peekNext() const;
    char advance();
    void skipWhitespaceAndComments();
    Token makeToken(TokenKind kind, std::size_t line, std::size_t column, std::string text = {},
                    std::int64_t number = 0);

    std::string source_;
    std::size_t pos_{0};
    std::vector<Token> tokens_;
};

std::vector<Token> tokenize(const std::string& source);

}  // namespace xlang
