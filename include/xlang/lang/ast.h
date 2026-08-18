#pragma once

#include "xlang/types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang {

// AST nodes are tagged unions (Kind) with static factory helpers.
// Keep factories thin; prefer adding Kind + factory over ad-hoc helpers.

enum class BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
};

struct Span {
    std::size_t line{1};
    std::size_t column{1};
};

struct Expr;
struct Stmt;
struct Block;
struct Function;
struct Program;

struct TypedName {
    std::string name;
    Type type{TypeKind::Int32};
};

struct FunctionSignature {
    std::string name;
    std::vector<TypedName> params;
    Type return_type{TypeKind::Int32};
    bool variadic{false};
};

struct ImportSpec {
    bool wildcard{false};
    bool use_prefix{false};
    std::string name;
    std::string alias;
    std::string bound_alias;
};

struct ItemModifiers {
    bool exported{false};
    bool external{false};
    bool declare{false};
};

struct ImportDecl {
    bool is_from{false};
    bool is_clause_import{false};
    std::string module;
    std::string alias;
    std::vector<ImportSpec> names;
    Span span{};
};

struct StructField {
    std::string name;
    Type type{TypeKind::Int32};
};

struct InterfaceMethod {
    std::string name;
    std::vector<TypedName> params;
    Type return_type{TypeKind::Int32};
};

struct InterfaceDecl {
    std::string name;
    std::vector<InterfaceMethod> methods;
    bool exported{false};
    Span span{};
};

struct StructDecl {
    std::string name;
    std::vector<StructField> fields;
    bool exported{false};
    Span span{};
};

struct FieldInit {
    std::string name;
    std::unique_ptr<Expr> value;
};

struct GlobalVar {
    std::string name;
    Type type{TypeKind::Int32};
    std::unique_ptr<Expr> init;
    bool exported{false};
    bool external{false};
    Span span{};
};

struct Expr {
    enum class Kind {
        IntLiteral,
        FloatLiteral,
        BoolLiteral,
        StringLiteral,
        Variable,
        Binary,
        Call,
        FunctionRef,
        FieldAccess,
        MethodCall,
        New,
        NewArray,
        Index,
        Null,
        Cast,
        AddrOf,
        Deref,
        NativeSyscall,
    } kind;

    Span span{};
    std::int64_t int_value{};
    double float_value{};
    bool bool_value{};
    /// Kind::Cast: false = `as` (static-like), true = `reinterpret` (bit/pointer).
    bool reinterpret_cast_{false};
    std::string name;
    std::string string_value;
    BinOp bin_op{};
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::unique_ptr<Expr> index;
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<FieldInit> field_inits;
    Type type{TypeKind::Struct};

    static std::unique_ptr<Expr> makeInt(std::int64_t value, Span span);
    static std::unique_ptr<Expr> makeFloat(double value, Span span);
    static std::unique_ptr<Expr> makeBool(bool value, Span span);
    static std::unique_ptr<Expr> makeString(std::string value, Span span);
    static std::unique_ptr<Expr> makeNull(Span span);
    static std::unique_ptr<Expr> makeVar(std::string name, Span span);
    static std::unique_ptr<Expr> makeBinary(BinOp op, std::unique_ptr<Expr> left,
                                            std::unique_ptr<Expr> right, Span span);
    static std::unique_ptr<Expr> makeCall(std::string name, std::vector<std::unique_ptr<Expr>> args,
                                          Span span);
    static std::unique_ptr<Expr> makeFunctionRef(std::string name, Span span);
    static std::unique_ptr<Expr> makeFieldAccess(std::unique_ptr<Expr> object, std::string field,
                                                 Span span);
    static std::unique_ptr<Expr> makeMethodCall(std::unique_ptr<Expr> object, std::string method,
                                               std::vector<std::unique_ptr<Expr>> args, Span span);
    static std::unique_ptr<Expr> makeNew(std::string struct_name,
                                         std::vector<FieldInit> field_inits, Span span);
    static std::unique_ptr<Expr> makeNewArray(Type element_type, Span span);
    static std::unique_ptr<Expr> makeIndex(std::unique_ptr<Expr> object, std::unique_ptr<Expr> index,
                                           Span span);
    static std::unique_ptr<Expr> makeCast(std::unique_ptr<Expr> value, Type target_type, Span span,
                                          bool reinterpret = false);
    static std::unique_ptr<Expr> makeAddrOf(std::unique_ptr<Expr> value, Span span);
    static std::unique_ptr<Expr> makeDeref(std::unique_ptr<Expr> value, Span span);
    /// `@syscall(number, args...)` — CPU-native trap; number in `left`, args in `args`.
    static std::unique_ptr<Expr> makeNativeSyscall(std::unique_ptr<Expr> number,
                                                   std::vector<std::unique_ptr<Expr>> args,
                                                   Span span);
};

struct Stmt {
    enum class Kind {
        Local,
        Assign,
        MemberAssign,
        IndexAssign,
        DerefAssign,
        Return,
        Expr,
        Delete,
        If,
        While,
    } kind;

    Span span{};
    std::string name;
    Type type{TypeKind::Int32};
    bool explicit_type{false};
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> index_target;
    std::string field;
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> return_value;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Block> then_block;
    std::unique_ptr<Block> else_block;
    std::unique_ptr<Block> loop_body;
};

struct Block {
    Span span{};
    std::vector<Stmt> statements;
};

struct Function {
    std::string name;
    std::vector<TypedName> params;
    Type return_type{TypeKind::Int32};
    Block body;
    bool exported{false};
    bool external{false};
    /// `declare syscall <n> name(...)` — CPU-native trap; not an external C symbol.
    bool syscall{false};
    /// Kernel syscall number when `syscall` is true.
    std::int64_t syscall_number{0};
    bool variadic{false};
    Span span{};
};

struct Program {
    std::vector<ImportDecl> imports;
    std::unordered_map<std::string, std::string> import_aliases;
    std::vector<StructDecl> structs;
    std::vector<InterfaceDecl> interfaces;
    std::vector<GlobalVar> globals;
    std::vector<Function> functions;
};

}  // namespace xlang
