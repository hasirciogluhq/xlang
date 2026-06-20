#pragma once

#include "xlang/ast.h"
#include "xlang/lexer.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace xlang {

Program parseSource(const std::string& source);

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Program parseProgram();

private:
    [[nodiscard]] const Token& peek() const;
    [[nodiscard]] const Token& previous() const;
    [[nodiscard]] bool isAtEnd() const;
    [[nodiscard]] bool check(TokenKind kind) const;
    [[nodiscard]] bool match(TokenKind kind);

    Token advance();
    Token consume(TokenKind kind, const std::string& message);
    void consumeEndOfStatement();

    [[nodiscard]] ItemModifiers parseModifiers();
    void validateModifiers(const ItemModifiers& modifiers, const std::string& kind) const;

    ImportDecl parseImport();
    std::vector<ImportSpec> parseImportNames();
    GlobalVar parseGlobalVar(const ItemModifiers& modifiers);
    GlobalVar parseDeclareGlobal(const ItemModifiers& modifiers);
    Function parseFunction(const ItemModifiers& modifiers);
    Function parseDeclareFunction(const ItemModifiers& modifiers);
    Function parseDeclareSyscall();
    std::vector<std::string> parseParams();
    Block parseBlock();
    Stmt parseStatement();
    std::unique_ptr<Expr> parseExpr();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parsePrimary();
    std::vector<std::unique_ptr<Expr>> parseCallArgs();

    [[nodiscard]] Span currentSpan() const;
    [[nodiscard]] bool isFunctionName(const std::string& name) const;
    void registerFunction(const std::string& name);
    [[nodiscard]] ParseError error(const std::string& message) const;

    std::vector<Token> tokens_;
    std::size_t pos_{0};
    std::unordered_set<std::string> function_names_;
};

}  // namespace xlang
