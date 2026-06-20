#include "xlang/lexer.h"

#include <cctype>
#include <unordered_map>

namespace xlang {

namespace {

const std::unordered_map<std::string, TokenKind> kKeywords = {
    {"fn", TokenKind::Fn},         {"local", TokenKind::Local},   {"return", TokenKind::Return},
    {"import", TokenKind::Import}, {"from", TokenKind::From},   {"as", TokenKind::As},
    {"export", TokenKind::Export}, {"external", TokenKind::External},
    {"declare", TokenKind::Declare},
};

}  // namespace

Lexer::Lexer(std::string source) : source_(std::move(source)) { tokenize(); }

char Lexer::peek() const {
    if (pos_ >= source_.size()) {
        return '\0';
    }
    return source_[pos_];
}

char Lexer::peekNext() const {
    if (pos_ + 1 >= source_.size()) {
        return '\0';
    }
    return source_[pos_ + 1];
}

char Lexer::advance() {
    if (pos_ >= source_.size()) {
        return '\0';
    }
    return source_[pos_++];
}

void Lexer::skipWhitespaceAndComments() {
    while (true) {
        if (peek() == ' ' || peek() == '\t' || peek() == '\r') {
            advance();
            continue;
        }
        if (peek() == '/' && peekNext() == '/') {
            while (peek() != '\0' && peek() != '\n') {
                advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::makeToken(TokenKind kind, std::size_t line, std::size_t column, std::string text,
                       std::int64_t number) {
    Token token;
    token.kind = kind;
    token.text = std::move(text);
    token.number = number;
    token.line = line;
    token.column = column;
    return token;
}

void Lexer::tokenize() {
    std::size_t line = 1;
    std::size_t column = 1;

    while (true) {
        skipWhitespaceAndComments();

        const std::size_t start_line = line;
        const std::size_t start_column = column;
        const char c = peek();

        if (c == '\0') {
            tokens_.push_back(makeToken(TokenKind::End, line, column));
            break;
        }

        if (c == '\n') {
            advance();
            ++line;
            column = 1;
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string ident;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
                ident += advance();
                ++column;
            }

            const auto it = kKeywords.find(ident);
            const TokenKind kind = it != kKeywords.end() ? it->second : TokenKind::Ident;
            tokens_.push_back(makeToken(kind, start_line, start_column, ident));
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string digits;
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                digits += advance();
                ++column;
            }
            tokens_.push_back(
                makeToken(TokenKind::Number, start_line, start_column, digits, std::stoll(digits)));
            continue;
        }

        advance();
        ++column;

        switch (c) {
            case '(': tokens_.push_back(makeToken(TokenKind::LParen, start_line, start_column)); break;
            case ')': tokens_.push_back(makeToken(TokenKind::RParen, start_line, start_column)); break;
            case '{': tokens_.push_back(makeToken(TokenKind::LBrace, start_line, start_column)); break;
            case '}': tokens_.push_back(makeToken(TokenKind::RBrace, start_line, start_column)); break;
            case ',': tokens_.push_back(makeToken(TokenKind::Comma, start_line, start_column)); break;
            case ';': tokens_.push_back(makeToken(TokenKind::Semicolon, start_line, start_column)); break;
            case '+': tokens_.push_back(makeToken(TokenKind::Plus, start_line, start_column)); break;
            case '-': tokens_.push_back(makeToken(TokenKind::Minus, start_line, start_column)); break;
            case '*': tokens_.push_back(makeToken(TokenKind::Star, start_line, start_column)); break;
            case '/': tokens_.push_back(makeToken(TokenKind::Slash, start_line, start_column)); break;
            case '=': tokens_.push_back(makeToken(TokenKind::Eq, start_line, start_column)); break;
            default:
                throw LexError(start_line, start_column,
                               std::string("unexpected character '") + c + "'");
        }
    }
}

std::vector<Token> tokenize(const std::string& source) {
    return Lexer(source).tokens();
}

}  // namespace xlang
