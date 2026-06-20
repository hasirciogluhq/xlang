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
    Syscall,
    Declare,
    Struct,
    New,
    Delete,
    True,
    False,
    Null,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Semicolon,
    Colon,
    Dot,
    Plus,
    Minus,
    Star,
    Slash,
    Eq,
    Ident,
    Number,
    FloatNumber,
    String,
    EqEq,
    NotEq,
    Lt,
    Gt,
    Le,
    Ge,
    AndAnd,
    OrOr,
    LBracket,
    RBracket,
    If,
    Else,
    While,
    Array,
    Ellipsis,
};

struct Token {
    TokenKind kind{TokenKind::End};
    std::string text;
    std::int64_t number{0};
    double float_number{0.0};
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
                    std::int64_t number = 0, double float_number = 0.0);

    std::string source_;
    std::size_t pos_{0};
    std::vector<Token> tokens_;
};

std::vector<Token> tokenize(const std::string& source);

}  // namespace xlang
