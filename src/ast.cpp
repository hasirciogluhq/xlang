#include "xlang/ast.h"

namespace xlang {

std::unique_ptr<Expr> Expr::makeInt(std::int64_t value, Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::IntLiteral;
    expr->int_value = value;
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeFloat(double value, Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::FloatLiteral;
    expr->float_value = value;
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeBool(bool value, Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::BoolLiteral;
    expr->bool_value = value;
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeString(std::string value, Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::StringLiteral;
    expr->name = std::move(value);
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeNull(Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::Null;
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeVar(std::string name, Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::Variable;
    expr->name = std::move(name);
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeBinary(BinOp op, std::unique_ptr<Expr> left,
                                       std::unique_ptr<Expr> right, Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::Binary;
    expr->bin_op = op;
    expr->left = std::move(left);
    expr->right = std::move(right);
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeCall(std::string name, std::vector<std::unique_ptr<Expr>> args,
                                     Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::Call;
    expr->name = std::move(name);
    expr->args = std::move(args);
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeFunctionRef(std::string name, Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::FunctionRef;
    expr->name = std::move(name);
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeFieldAccess(std::unique_ptr<Expr> object, std::string field,
                                            Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::FieldAccess;
    expr->object = std::move(object);
    expr->name = std::move(field);
    expr->span = span;
    return expr;
}

std::unique_ptr<Expr> Expr::makeNew(std::string struct_name, std::vector<FieldInit> field_inits,
                                    Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::New;
    expr->name = std::move(struct_name);
    expr->field_inits = std::move(field_inits);
    expr->new_type = Type::makeStruct(expr->name);
    expr->span = span;
    return expr;
}

}  // namespace xlang
