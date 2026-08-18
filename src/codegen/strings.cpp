#include "xlang/codegen/codegen.h"

#include "xlang/codegen/detail/helpers.h"
#include "xlang/codegen/target.h"
#include "xlang/error.h"
#include "xlang/types.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <format>
#include <utility>
#include <vector>

using namespace xlang::codegen_detail;

namespace xlang {

bool Codegen::isStringType(const Type& type) const { return type.kind == TypeKind::String; }

void Codegen::ensureStringLiteralGlobal(const std::string& text) {
    if (string_literal_globals_.contains(text)) {
        return;
    }
    const std::string name = "__xlang_str." + std::to_string(string_literal_counter_++);
    string_literal_globals_.emplace(text, b().createStringGlobal(text, name));
}

llvm::Value* Codegen::emitStringLiteral(const std::string& text) {
    ensureStringLiteralGlobal(text);
    return string_literal_globals_.at(text);
}

void Codegen::collectStringLiteralsFromExpr(const Expr& expr) {
    if (expr.kind == Expr::Kind::StringLiteral) {
        ensureStringLiteralGlobal(expr.string_value);
        return;
    }
    if (expr.object) {
        collectStringLiteralsFromExpr(*expr.object);
    }
    if (expr.left) {
        collectStringLiteralsFromExpr(*expr.left);
    }
    if (expr.right) {
        collectStringLiteralsFromExpr(*expr.right);
    }
    for (const auto& arg : expr.args) {
        collectStringLiteralsFromExpr(*arg);
    }
    for (const FieldInit& init : expr.field_inits) {
        if (init.value) {
            collectStringLiteralsFromExpr(*init.value);
        }
    }
}

void Codegen::collectStringLiteralsFromStmt(const Stmt& stmt) {
    if (stmt.expr) {
        collectStringLiteralsFromExpr(*stmt.expr);
    }
    if (stmt.return_value) {
        collectStringLiteralsFromExpr(*stmt.return_value);
    }
    if (stmt.target) {
        collectStringLiteralsFromExpr(*stmt.target);
    }
    if (stmt.condition) {
        collectStringLiteralsFromExpr(*stmt.condition);
    }
    if (stmt.then_block) {
        for (const Stmt& inner : stmt.then_block->statements) {
            collectStringLiteralsFromStmt(inner);
        }
    }
    if (stmt.else_block) {
        for (const Stmt& inner : stmt.else_block->statements) {
            collectStringLiteralsFromStmt(inner);
        }
    }
    if (stmt.loop_body) {
        for (const Stmt& inner : stmt.loop_body->statements) {
            collectStringLiteralsFromStmt(inner);
        }
    }
}

void Codegen::preemitStringLiterals(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.init) {
            collectStringLiteralsFromExpr(*global.init);
        }
    }
    for (const Function& function : program.functions) {
        for (const Stmt& stmt : function.body.statements) {
            collectStringLiteralsFromStmt(stmt);
        }
    }
}

llvm::Value* Codegen::emitIntToString(llvm::Value* int_value) {
    llvm::Function* fn = b().getFunction("__xlang_int_to_str");
    if (fn == nullptr) {
        throw XlangError("string support not initialized");
    }
    llvm::Value* as_i32 = int_value;
    if (int_value->getType()->isIntegerTy(64)) {
        as_i32 = b().emitTrunc(int_value, b().i32Ty());
    } else if (int_value->getType()->isIntegerTy(8)) {
        as_i32 = b().emitZExt(int_value, b().i32Ty());
    }
    return b().emitCall(fn, {as_i32});
}

llvm::Value* Codegen::emitStringConcat(llvm::Value* left, llvm::Value* right) {
    llvm::Function* fn = b().getFunction("__xlang_str_concat");
    if (fn == nullptr) {
        throw XlangError("string support not initialized");
    }
    return b().emitCall(fn, {left, right});
}


}  // namespace xlang
