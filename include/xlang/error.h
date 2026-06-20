#pragma once

#include <stdexcept>
#include <string>

namespace xlang {

class XlangError : public std::runtime_error {
public:
    explicit XlangError(const std::string& message)
        : std::runtime_error(message) {}
};

class LexError : public XlangError {
public:
    LexError(std::size_t line, std::size_t column, const std::string& message)
        : XlangError("lex error at line " + std::to_string(line) + ", column " +
                     std::to_string(column) + ": " + message),
          line_(line),
          column_(column) {}

    [[nodiscard]] std::size_t line() const { return line_; }
    [[nodiscard]] std::size_t column() const { return column_; }

private:
    std::size_t line_;
    std::size_t column_;
};

class ParseError : public XlangError {
public:
    ParseError(std::size_t line, std::size_t column, const std::string& message)
        : XlangError("parse error at line " + std::to_string(line) + ", column " +
                     std::to_string(column) + ": " + message),
          line_(line),
          column_(column) {}

    [[nodiscard]] std::size_t line() const { return line_; }
    [[nodiscard]] std::size_t column() const { return column_; }

private:
    std::size_t line_;
    std::size_t column_;
};

}  // namespace xlang
