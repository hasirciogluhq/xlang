#include "xlang/ast.h"

namespace xlang {

std::unique_ptr<Expr> Expr::makeInt(std::int64_t value, Span span) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Kind::IntLiteral;
    expr->int_value = value;
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

}  // namespace xlang
