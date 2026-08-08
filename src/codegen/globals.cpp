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

void Codegen::emitGlobals(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        globals_.insert(global.name);
        global_types_[global.name] = global.type;
        const std::string gname = globalName(global.name);

        if (global.external) {
            global_vars_[global.name] = b().createExternalGlobal(gname, llvmType(global.type));
            continue;
        }

        const auto linkage = globalLinkage(global);
        llvm::Constant* init = zeroOf(global.type);
        if (global.init && global.init->kind == Expr::Kind::IntLiteral &&
            (global.type.kind == TypeKind::Int32 || global.type.kind == TypeKind::Int64)) {
            init = global.type.kind == TypeKind::Int64
                       ? b().constI64(global.init->int_value)
                       : b().constI32(global.init->int_value);
        } else if (global.init && global.init->kind == Expr::Kind::FloatLiteral &&
                   global.type.isFloating()) {
            init = b().constF64(global.init->float_value);
        } else if (global.init && global.init->kind == Expr::Kind::BoolLiteral &&
                   global.type.kind == TypeKind::Bool) {
            init = b().constI8(global.init->bool_value ? 1 : 0);
        } else if (global.init && global.init->kind == Expr::Kind::Null) {
            init = b().constNullPtr();
        }
        global_vars_[global.name] =
            b().createGlobal(gname, llvmType(global.type), init, linkage);
    }
}

void Codegen::emitGlobalInit(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.external || !global.init) {
            continue;
        }
        if (global.init->kind == Expr::Kind::IntLiteral &&
            (global.type.kind == TypeKind::Int32 || global.type.kind == TypeKind::Int64)) {
            continue;
        }
        if (global.init->kind == Expr::Kind::FloatLiteral && global.type.isFloating()) {
            continue;
        }
        if (global.init->kind == Expr::Kind::BoolLiteral && global.type.kind == TypeKind::Bool) {
            continue;
        }
        if (global.init->kind == Expr::Kind::Null) {
            continue;
        }
        needs_global_init_ = true;
        break;
    }
    if (!needs_global_init_) {
        return;
    }

    const auto linkage =
        (options_.build_kind == BuildKind::Object || options_.build_kind == BuildKind::Static)
            ? llvm::Function::InternalLinkage
            : llvm::Function::ExternalLinkage;
    llvm::Function* fn =
        b().createFunction("__xlang_init_globals", b().functionType(b().voidTy(), {}), linkage);
    llvm::BasicBlock* entry = b().createBlock(fn, "entry");
    b().setInsertPoint(entry);
    current_function_ = fn;
    LocalMap locals;
    for (const GlobalVar& global : program.globals) {
        if (global.external || !global.init) {
            continue;
        }
        if (global.init->kind == Expr::Kind::IntLiteral &&
            (global.type.kind == TypeKind::Int32 || global.type.kind == TypeKind::Int64)) {
            continue;
        }
        if (global.init->kind == Expr::Kind::FloatLiteral && global.type.isFloating()) {
            continue;
        }
        if (global.init->kind == Expr::Kind::BoolLiteral && global.type.kind == TypeKind::Bool) {
            continue;
        }
        if (global.init->kind == Expr::Kind::Null) {
            continue;
        }
        const auto [ty, val] = emitExpr(*global.init, locals);
        storeValue(ty, val, global_vars_.at(global.name));
    }
    b().emitRetVoid();
    current_function_ = nullptr;
}

llvm::GlobalValue::LinkageTypes Codegen::globalLinkage(const GlobalVar& global) const {
    if (global.external) {
        return llvm::GlobalValue::ExternalLinkage;
    }
    if (!global.exported && (options_.build_kind == BuildKind::Object ||
                             options_.build_kind == BuildKind::Static)) {
        return llvm::GlobalValue::InternalLinkage;
    }
    return llvm::GlobalValue::ExternalLinkage;
}


}  // namespace xlang
