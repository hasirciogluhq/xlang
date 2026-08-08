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

void Codegen::emitBlock(const Block& block, LocalMap& locals, bool& has_return) {
    for (const Stmt& stmt : block.statements) {
        if (emitStatement(stmt, locals)) {
            has_return = true;
            return;
        }
    }
}

bool Codegen::emitStatement(const Stmt& stmt, LocalMap& locals) {
    switch (stmt.kind) {
    case Stmt::Kind::Local: {
        const auto [ty, val] = emitExpr(*stmt.expr, locals);
        Type local_type = stmt.explicit_type ? stmt.type : ty;
        llvm::Value* stored_val = coerceInt(val, ty, local_type);
        allocLocal(stmt.name, local_type, locals);
        storeValue(local_type, stored_val, locals.at(stmt.name));
        return false;
    }
    case Stmt::Kind::Assign: {
        const Type var_type = resolveVarType(stmt.name, locals);
        const auto [ty, val] = emitExpr(*stmt.expr, locals);
        storeValue(var_type, coerceInt(val, ty, var_type), resolveVar(stmt.name, locals));
        return false;
    }
    case Stmt::Kind::MemberAssign: {
        const auto [obj_ty, obj_ptr] = emitExpr(*stmt.target, locals);
        if (obj_ty.kind != TypeKind::Struct) {
            throw XlangError("field assignment requires struct object");
        }
        const StructDecl* decl = findStruct(obj_ty.struct_name);
        if (decl == nullptr) {
            throw XlangError(std::format("unknown struct `{}`", obj_ty.struct_name));
        }
        const std::size_t index = structFieldIndex(*decl, stmt.field);
        const Type field_type = decl->fields[index].type;
        const auto [_, val] = emitExpr(*stmt.expr, locals);
        llvm::Value* gep =
            b().emitStructGEP(structBodyType(decl->name), obj_ptr, static_cast<unsigned>(index));
        storeValue(field_type, val, gep);
        return false;
    }
    case Stmt::Kind::IndexAssign: {
        const auto [arr_ty, arr] = emitExpr(*stmt.index_target->object, locals);
        if (!arr_ty.isArray()) {
            throw XlangError("index assignment requires array");
        }
        const auto [_, idx] = emitExpr(*stmt.index_target->index, locals);
        const auto [val_ty, val] = emitExpr(*stmt.expr, locals);
        (void)val_ty;
        const Type elem = arr_ty.arrayElementType();
        const std::size_t sz = typeSizeBytes(elem);
        llvm::Value* idx64 = b().emitSExt(idx, b().i64Ty());
        llvm::Value* head =
            b().emitLoad(b().i64Ty(), b().emitStructGEP(array_hdr_ty_, arr, 3));
        llvm::Value* pos = b().emitAdd(head, idx64);
        llvm::Value* data =
            b().emitLoad(b().ptrTy(), b().emitStructGEP(array_hdr_ty_, arr, 0));
        llvm::Value* off = b().emitMul(pos, b().constI64(static_cast<std::int64_t>(sz)));
        llvm::Value* slot = b().emitGEP(b().i8Ty(), data, {off});
        storeValue(elem, val, slot);
        return false;
    }
    case Stmt::Kind::If: {
        const auto [_, cond] = emitExpr(*stmt.condition, locals);
        llvm::Value* cond_i1 = boolToI1(cond);
        llvm::BasicBlock* then_bb = b().createBlock(current_function_, freshLabel());
        llvm::BasicBlock* else_bb =
            stmt.else_block ? b().createBlock(current_function_, freshLabel()) : nullptr;
        llvm::BasicBlock* merge_bb = b().createBlock(current_function_, freshLabel());
        b().emitCondBr(cond_i1, then_bb, else_bb ? else_bb : merge_bb);
        b().setInsertPoint(then_bb);
        bool branch_return = false;
        emitBlock(*stmt.then_block, locals, branch_return);
        if (!branch_return) {
            b().emitBr(merge_bb);
        }
        if (stmt.else_block) {
            b().setInsertPoint(else_bb);
            branch_return = false;
            emitBlock(*stmt.else_block, locals, branch_return);
            if (!branch_return) {
                b().emitBr(merge_bb);
            }
        }
        b().setInsertPoint(merge_bb);
        return false;
    }
    case Stmt::Kind::While: {
        llvm::BasicBlock* cond_bb = b().createBlock(current_function_, freshLabel());
        llvm::BasicBlock* body_bb = b().createBlock(current_function_, freshLabel());
        llvm::BasicBlock* exit_bb = b().createBlock(current_function_, freshLabel());
        b().emitBr(cond_bb);
        b().setInsertPoint(cond_bb);
        const auto [_, cond] = emitExpr(*stmt.condition, locals);
        b().emitCondBr(boolToI1(cond), body_bb, exit_bb);
        b().setInsertPoint(body_bb);
        bool branch_return = false;
        emitBlock(*stmt.loop_body, locals, branch_return);
        if (!branch_return) {
            b().emitBr(cond_bb);
        }
        b().setInsertPoint(exit_bb);
        return false;
    }
    case Stmt::Kind::Return: {
        if (stmt.return_value) {
            auto [ty, val] = emitExpr(*stmt.return_value, locals);
            val = coerceInt(val, ty, current_return_type_);
            b().emitRet(val);
        } else if (current_return_type_.kind == TypeKind::Void) {
            b().emitRetVoid();
        } else {
            b().emitRet(zeroOf(current_return_type_));
        }
        return true;
    }
    case Stmt::Kind::Delete: {
        const auto [ty, val] = emitExpr(*stmt.expr, locals);
        if (ty.kind != TypeKind::Struct && !ty.isPointer()) {
            throw XlangError("delete expects pointer or struct handle");
        }
        llvm::Function* free_fn =
            ensureFn("free", b().functionType(b().voidTy(), {b().ptrTy()}));
        b().emitCall(free_fn, {val});
        return false;
    }
    case Stmt::Kind::Expr: {
        (void)emitExpr(*stmt.expr, locals);
        return false;
    }
    }
    return false;
}


}  // namespace xlang
