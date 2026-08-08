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

void Codegen::emitStringRuntimeSupport() {
    auto* sprintf_ty =
        b().functionType(b().i32Ty(), {b().ptrTy(), b().ptrTy()}, true);
    auto* strlen_ty = b().functionType(b().i64Ty(), {b().ptrTy()});
    auto* strcpy_ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().ptrTy()});
    auto* strcat_ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().ptrTy()});
    auto* strcmp_ty = b().functionType(b().i32Ty(), {b().ptrTy(), b().ptrTy()});
    auto* strstr_ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().ptrTy()});
    auto* strncpy_ty =
        b().functionType(b().ptrTy(), {b().ptrTy(), b().ptrTy(), b().i64Ty()});
    auto* malloc_ty = b().functionType(b().ptrTy(), {b().i64Ty()});

    llvm::Function* sprintf_fn = ensureFn("sprintf", sprintf_ty);
    llvm::Function* strlen_fn = ensureFn("strlen", strlen_ty);
    llvm::Function* strcpy_fn = ensureFn("strcpy", strcpy_ty);
    llvm::Function* strcat_fn = ensureFn("strcat", strcat_ty);
    llvm::Function* strcmp_fn = ensureFn("strcmp", strcmp_ty);
    llvm::Function* strstr_fn = ensureFn("strstr", strstr_ty);
    llvm::Function* strncpy_fn = ensureFn("strncpy", strncpy_ty);
    llvm::Function* malloc_fn = ensureFn("malloc", malloc_ty);

    int_fmt_ = b().createStringGlobal("%d", "__xlang_int_fmt");

    // __xlang_int_to_str
    {
        auto* ty = b().functionType(b().ptrTy(), {b().i32Ty()});
        llvm::Function* fn =
            b().createFunction("__xlang_int_to_str", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* n = fn->getArg(0);
        n->setName("n");
        llvm::Value* buf = b().emitCall(malloc_fn, {b().constI64(32)});
        b().emitCall(llvm::FunctionCallee(sprintf_ty, sprintf_fn), {buf, int_fmt_, n});
        b().emitRet(buf);
    }

    // __xlang_str_concat
    {
        auto* ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().ptrTy()});
        llvm::Function* fn =
            b().createFunction("__xlang_str_concat", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* a = fn->getArg(0);
        llvm::Value* bb = fn->getArg(1);
        a->setName("a");
        bb->setName("b");
        llvm::Value* la = b().emitCall(strlen_fn, {a});
        llvm::Value* lb = b().emitCall(strlen_fn, {bb});
        llvm::Value* sum = b().emitAdd(la, lb);
        llvm::Value* size = b().emitAdd(sum, b().constI64(1));
        llvm::Value* buf = b().emitCall(malloc_fn, {size});
        b().emitCall(strcpy_fn, {buf, a});
        b().emitCall(strcat_fn, {buf, bb});
        b().emitRet(buf);
    }

    // __xlang_str_len
    {
        auto* ty = b().functionType(b().i32Ty(), {b().ptrTy()});
        llvm::Function* fn =
            b().createFunction("__xlang_str_len", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* s = fn->getArg(0);
        llvm::Value* n = b().emitCall(strlen_fn, {s});
        b().emitRet(b().emitTrunc(n, b().i32Ty()));
    }

    // __xlang_str_eq
    {
        auto* ty = b().functionType(b().i32Ty(), {b().ptrTy(), b().ptrTy()});
        llvm::Function* fn =
            b().createFunction("__xlang_str_eq", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* rc = b().emitCall(strcmp_fn, {fn->getArg(0), fn->getArg(1)});
        llvm::Value* eq = b().emitICmp(llvm::CmpInst::ICMP_EQ, rc, b().constI32(0));
        b().emitRet(b().emitSelect(eq, b().constI32(1), b().constI32(0)));
    }

    // __xlang_str_byte
    {
        auto* ty = b().functionType(b().i32Ty(), {b().ptrTy(), b().i32Ty()});
        llvm::Function* fn =
            b().createFunction("__xlang_str_byte", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        llvm::BasicBlock* load_bb = b().createBlock(fn, "load");
        llvm::BasicBlock* out_bb = b().createBlock(fn, "out");
        b().setInsertPoint(entry);
        llvm::Value* s = fn->getArg(0);
        llvm::Value* i = fn->getArg(1);
        llvm::Value* len = b().emitCall(strlen_fn, {s});
        llvm::Value* i64 = b().emitSExt(i, b().i64Ty());
        llvm::Value* bad = b().emitICmp(llvm::CmpInst::ICMP_UGE, i64, len);
        b().emitCondBr(bad, out_bb, load_bb);
        b().setInsertPoint(load_bb);
        llvm::Value* p = b().emitGEP(b().i8Ty(), s, {i64});
        llvm::Value* c = b().emitLoad(b().i8Ty(), p);
        b().emitRet(b().emitZExt(c, b().i32Ty()));
        b().setInsertPoint(out_bb);
        b().emitRet(b().constI32(-1));
    }

    // __xlang_str_find
    {
        auto* ty = b().functionType(b().i32Ty(), {b().ptrTy(), b().ptrTy()});
        llvm::Function* fn =
            b().createFunction("__xlang_str_find", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        llvm::BasicBlock* found = b().createBlock(fn, "found");
        llvm::BasicBlock* none = b().createBlock(fn, "none");
        b().setInsertPoint(entry);
        llvm::Value* hay = fn->getArg(0);
        llvm::Value* needle = fn->getArg(1);
        llvm::Value* hit = b().emitCall(strstr_fn, {hay, needle});
        llvm::Value* miss =
            b().emitICmp(llvm::CmpInst::ICMP_EQ, hit, b().constNullPtr());
        b().emitCondBr(miss, none, found);
        b().setInsertPoint(found);
        llvm::Value* off = b().emitPtrToInt(hit, b().i64Ty());
        llvm::Value* base = b().emitPtrToInt(hay, b().i64Ty());
        llvm::Value* idx64 = b().emitSub(off, base);
        b().emitRet(b().emitTrunc(idx64, b().i32Ty()));
        b().setInsertPoint(none);
        b().emitRet(b().constI32(-1));
    }

    // __xlang_str_sub
    {
        auto* ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().i32Ty(), b().i32Ty()});
        llvm::Function* fn =
            b().createFunction("__xlang_str_sub", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        llvm::BasicBlock* empty = b().createBlock(fn, "empty");
        llvm::BasicBlock* copy = b().createBlock(fn, "copy");
        b().setInsertPoint(entry);
        llvm::Value* s = fn->getArg(0);
        llvm::Value* start = fn->getArg(1);
        llvm::Value* len = fn->getArg(2);
        llvm::Value* total = b().emitCall(strlen_fn, {s});
        llvm::Value* start64 = b().emitSExt(start, b().i64Ty());
        llvm::Value* len64 = b().emitSExt(len, b().i64Ty());
        llvm::Value* bad_start =
            b().emitICmp(llvm::CmpInst::ICMP_UGE, start64, total);
        llvm::Value* end64 = b().emitAdd(start64, len64);
        llvm::Value* bad_end = b().emitICmp(llvm::CmpInst::ICMP_UGT, end64, total);
        llvm::Value* bad = b().emitOr(bad_start, bad_end);
        b().emitCondBr(bad, empty, copy);
        b().setInsertPoint(empty);
        llvm::Value* z = b().emitCall(malloc_fn, {b().constI64(1)});
        b().emitStore(b().constI8(0), z);
        b().emitRet(z);
        b().setInsertPoint(copy);
        llvm::Value* size = b().emitAdd(len64, b().constI64(1));
        llvm::Value* buf = b().emitCall(malloc_fn, {size});
        llvm::Value* src = b().emitGEP(b().i8Ty(), s, {start64});
        b().emitCall(strncpy_fn, {buf, src, len64});
        llvm::Value* end = b().emitGEP(b().i8Ty(), buf, {len64});
        b().emitStore(b().constI8(0), end);
        b().emitRet(buf);
    }
}

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
    if (expr.index) {
        collectStringLiteralsFromExpr(*expr.index);
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
        throw XlangError("string runtime not initialized");
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
        throw XlangError("string runtime not initialized");
    }
    return b().emitCall(fn, {left, right});
}


}  // namespace xlang
