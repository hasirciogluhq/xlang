#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xlang {

enum class BinOp { Add, Sub, Mul, Div };

struct Span {
    std::size_t line{1};
    std::size_t column{1};
};

struct Expr;
struct Stmt;
struct Block;
struct Function;
struct Program;

struct FunctionSignature {
    std::string name;
    std::vector<std::string> params;
};

struct ImportSpec {
    std::string name;
    std::string alias;
};

struct ItemModifiers {
    bool exported{false};
    bool external{false};
    bool declare{false};
};

struct ImportDecl {
    bool is_from{false};
    std::string module;
    std::string alias;
    std::vector<ImportSpec> names;
    Span span{};
};

struct GlobalVar {
    std::string name;
    std::unique_ptr<Expr> init;
    bool exported{false};
    bool external{false};
    Span span{};
};

struct Expr {
    enum class Kind { IntLiteral, Variable, Binary, Call } kind;

    Span span{};
    std::int64_t int_value{};
    std::string name;
    BinOp bin_op{};
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::vector<std::unique_ptr<Expr>> args;

    static std::unique_ptr<Expr> makeInt(std::int64_t value, Span span);
    static std::unique_ptr<Expr> makeVar(std::string name, Span span);
    static std::unique_ptr<Expr> makeBinary(BinOp op, std::unique_ptr<Expr> left,
                                            std::unique_ptr<Expr> right, Span span);
    static std::unique_ptr<Expr> makeCall(std::string name, std::vector<std::unique_ptr<Expr>> args,
                                          Span span);
};

struct Stmt {
    enum class Kind { Local, Assign, Return, Expr } kind;

    Span span{};
    std::string name;
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> return_value;
};

struct Block {
    Span span{};
    std::vector<Stmt> statements;
};

struct Function {
    std::string name;
    std::vector<std::string> params;
    Block body;
    bool exported{false};
    bool external{false};
    Span span{};
};

struct Program {
    std::vector<ImportDecl> imports;
    std::vector<GlobalVar> globals;
    std::vector<Function> functions;
};

}  // namespace xlang
