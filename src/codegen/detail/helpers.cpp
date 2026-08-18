#include "xlang/codegen/detail/helpers.h"

#include "xlang/error.h"

#include <format>

namespace xlang::codegen_detail {


std::vector<Type> paramTypes(const std::vector<TypedName>& params) {
    std::vector<Type> types;
    types.reserve(params.size());
    for (const TypedName& param : params) {
        types.push_back(param.type);
    }
    return types;
}

bool paramTypesMatch(const std::vector<Type>& expected, const std::vector<Type>& actual,
                     bool variadic) {
    if (variadic) {
        if (actual.size() < expected.size()) {
            return false;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (!typesEqual(expected[i], actual[i])) {
                return false;
            }
        }
        return true;
    }
    if (expected.size() != actual.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!typesEqual(expected[i], actual[i])) {
            return false;
        }
    }
    return true;
}

bool paramTypesMatchWithWidening(const std::vector<Type>& expected,
                                 const std::vector<Type>& actual, bool variadic) {
    if (variadic) {
        if (actual.size() < expected.size()) {
            return false;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (typesEqual(expected[i], actual[i])) {
                continue;
            }
            if (expected[i].kind == TypeKind::Int64 && actual[i].kind == TypeKind::Int32) {
                continue;
            }
            return false;
        }
        return true;
    }
    if (expected.size() != actual.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (typesEqual(expected[i], actual[i])) {
            continue;
        }
        if (expected[i].kind == TypeKind::Int64 && actual[i].kind == TypeKind::Int32) {
            continue;
        }
        return false;
    }
    return true;
}

std::optional<FunctionSignature> findMatchingFunctionImpl(
    const std::string& name, const std::vector<Type>& arg_types,
    const std::vector<FunctionSignature>& candidates, bool allow_widening) {
    const FunctionSignature* match = nullptr;
    for (const FunctionSignature& candidate : candidates) {
        if (candidate.name != name) {
            continue;
        }
        const std::vector<Type> expected = paramTypes(candidate.params);
        const bool matches = allow_widening
                                 ? paramTypesMatchWithWidening(expected, arg_types,
                                                               candidate.variadic)
                                 : paramTypesMatch(expected, arg_types, candidate.variadic);
        if (!matches) {
            continue;
        }
        if (match != nullptr) {
            throw XlangError(std::format("ambiguous call to `{}`", name));
        }
        match = &candidate;
    }
    if (match == nullptr) {
        return std::nullopt;
    }
    return *match;
}

std::optional<FunctionSignature> findMatchingFunction(
    const std::string& name, const std::vector<Type>& arg_types,
    const std::vector<FunctionSignature>& candidates) {
    if (const std::optional<FunctionSignature> exact =
            findMatchingFunctionImpl(name, arg_types, candidates, false)) {
        return exact;
    }
    return findMatchingFunctionImpl(name, arg_types, candidates, true);
}

std::vector<FunctionSignature> collectDefinedFunctions(const Program& program) {
    std::vector<FunctionSignature> functions;
    for (const Function& function : program.functions) {
        FunctionSignature signature;
        signature.name = function.name;
        signature.params = function.params;
        signature.return_type = function.return_type;
        signature.variadic = function.variadic;
        functions.push_back(std::move(signature));
    }
    return functions;
}

const Function* findFunctionDefinition(const Program& program, const std::string& name,
                                       const std::vector<Type>& param_types) {
    for (const Function& function : program.functions) {
        if (function.name != name) {
            continue;
        }
        if (!paramTypesMatch(paramTypes(function.params), param_types, function.variadic)) {
            continue;
        }
        return &function;
    }
    return nullptr;
}

const Function* findUniqueFunctionByName(const Program& program, const std::string& name) {
    const Function* match = nullptr;
    for (const Function& function : program.functions) {
        if (function.name != name || function.syscall) {
            continue;
        }
        if (function.body.statements.empty() && !function.external) {
            continue;
        }
        if (match != nullptr) {
            throw XlangError(std::format("ambiguous function reference `{}`", name));
        }
        match = &function;
    }
    return match;
}

bool exprUsesString(const Expr& expr);
bool stmtUsesString(const Stmt& stmt);
bool exprUsesHeap(const Expr& expr);
bool stmtUsesHeap(const Stmt& stmt);

bool exprUsesString(const Expr& expr) {
    if (expr.kind == Expr::Kind::StringLiteral) {
        return true;
    }
    if (expr.kind == Expr::Kind::Binary && expr.bin_op == BinOp::Add) {
        if (expr.left && exprUsesString(*expr.left)) {
            return true;
        }
        if (expr.right && exprUsesString(*expr.right)) {
            return true;
        }
    }
    if (expr.object && exprUsesString(*expr.object)) {
        return true;
    }
    if (expr.left && exprUsesString(*expr.left)) {
        return true;
    }
    if (expr.right && exprUsesString(*expr.right)) {
        return true;
    }
    for (const auto& arg : expr.args) {
        if (exprUsesString(*arg)) {
            return true;
        }
    }
    for (const FieldInit& init : expr.field_inits) {
        if (init.value && exprUsesString(*init.value)) {
            return true;
        }
    }
    return false;
}

bool stmtUsesString(const Stmt& stmt) {
    if (stmt.condition && exprUsesString(*stmt.condition)) {
        return true;
    }
    if (stmt.expr && exprUsesString(*stmt.expr)) {
        return true;
    }
    if (stmt.return_value && exprUsesString(*stmt.return_value)) {
        return true;
    }
    if (stmt.target && exprUsesString(*stmt.target)) {
        return true;
    }
    if (stmt.then_block) {
        for (const Stmt& inner : stmt.then_block->statements) {
            if (stmtUsesString(inner)) {
                return true;
            }
        }
    }
    if (stmt.else_block) {
        for (const Stmt& inner : stmt.else_block->statements) {
            if (stmtUsesString(inner)) {
                return true;
            }
        }
    }
    if (stmt.loop_body) {
        for (const Stmt& inner : stmt.loop_body->statements) {
            if (stmtUsesString(inner)) {
                return true;
            }
        }
    }
    return false;
}

bool programUsesStrings(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.init && exprUsesString(*global.init)) {
            return true;
        }
    }
    for (const Function& function : program.functions) {
        for (const Stmt& stmt : function.body.statements) {
            if (stmtUsesString(stmt)) {
                return true;
            }
        }
    }
    return false;
}

bool exprUsesHeap(const Expr& expr) {
    if (expr.kind == Expr::Kind::New) {
        return true;
    }
    if (expr.object && exprUsesHeap(*expr.object)) {
        return true;
    }
    if (expr.left && exprUsesHeap(*expr.left)) {
        return true;
    }
    if (expr.right && exprUsesHeap(*expr.right)) {
        return true;
    }
    for (const auto& arg : expr.args) {
        if (exprUsesHeap(*arg)) {
            return true;
        }
    }
    for (const FieldInit& init : expr.field_inits) {
        if (init.value && exprUsesHeap(*init.value)) {
            return true;
        }
    }
    return false;
}

bool stmtUsesHeap(const Stmt& stmt) {
    if (stmt.kind == Stmt::Kind::Delete) {
        return true;
    }
    if (stmt.expr && exprUsesHeap(*stmt.expr)) {
        return true;
    }
    if (stmt.return_value && exprUsesHeap(*stmt.return_value)) {
        return true;
    }
    if (stmt.target && exprUsesHeap(*stmt.target)) {
        return true;
    }
    return false;
}

bool programUsesHeap(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.init && exprUsesHeap(*global.init)) {
            return true;
        }
    }
    for (const Function& function : program.functions) {
        for (const Stmt& stmt : function.body.statements) {
            if (stmtUsesHeap(stmt)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace xlang::codegen_detail
