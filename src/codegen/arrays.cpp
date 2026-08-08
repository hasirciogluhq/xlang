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

void Codegen::emitArrayHeaderType() {
    if (array_hdr_type_emitted_) {
        return;
    }
    array_hdr_type_emitted_ = true;
    array_hdr_ty_ = llvm::StructType::create(b().context(),
                                             {b().ptrTy(), b().i64Ty(), b().i64Ty(), b().i64Ty()},
                                             "array.hdr");
}

void Codegen::emitArrayRuntimeSupport() {
    emitArrayHeaderType();
    auto* realloc_ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().i64Ty()});
    auto* memcpy_ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().ptrTy(), b().i64Ty()});
    auto* malloc_ty = b().functionType(b().ptrTy(), {b().i64Ty()});
    llvm::Function* realloc_fn = ensureFn("realloc", realloc_ty);
    llvm::Function* memcpy_fn = ensureFn("memcpy", memcpy_ty);
    llvm::Function* malloc_fn = ensureFn("malloc", malloc_ty);

    // __xlang_array_new
    {
        auto* ty = b().functionType(b().ptrTy(), {b().i64Ty()});
        llvm::Function* fn =
            b().createFunction("__xlang_array_new", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* elem_size = fn->getArg(0);
        llvm::Value* raw = b().emitCall(malloc_fn, {b().constI64(32)});
        llvm::Value* arr = raw;
        llvm::Value* cap_bytes = b().emitMul(elem_size, b().constI64(4));
        llvm::Value* data = b().emitCall(malloc_fn, {cap_bytes});
        b().emitStore(data, b().emitStructGEP(array_hdr_ty_, arr, 0));
        b().emitStore(b().constI64(0), b().emitStructGEP(array_hdr_ty_, arr, 1));
        b().emitStore(b().constI64(4), b().emitStructGEP(array_hdr_ty_, arr, 2));
        b().emitStore(b().constI64(0), b().emitStructGEP(array_hdr_ty_, arr, 3));
        b().emitRet(arr);
    }

    // __xlang_array_len
    {
        auto* ty = b().functionType(b().i64Ty(), {b().ptrTy()});
        llvm::Function* fn =
            b().createFunction("__xlang_array_len", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* arr = fn->getArg(0);
        b().emitRet(b().emitLoad(b().i64Ty(), b().emitStructGEP(array_hdr_ty_, arr, 1)));
    }

    // __xlang_array_push
    {
        auto* ty = b().functionType(b().voidTy(), {b().ptrTy(), b().ptrTy(), b().i64Ty()});
        llvm::Function* fn =
            b().createFunction("__xlang_array_push", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        llvm::BasicBlock* grow = b().createBlock(fn, "grow");
        llvm::BasicBlock* write = b().createBlock(fn, "write");
        b().setInsertPoint(entry);
        llvm::Value* arr = fn->getArg(0);
        llvm::Value* elem = fn->getArg(1);
        llvm::Value* elem_size = fn->getArg(2);
        llvm::Value* len_ptr = b().emitStructGEP(array_hdr_ty_, arr, 1);
        llvm::Value* cap_ptr = b().emitStructGEP(array_hdr_ty_, arr, 2);
        llvm::Value* head_ptr = b().emitStructGEP(array_hdr_ty_, arr, 3);
        llvm::Value* data_ptr = b().emitStructGEP(array_hdr_ty_, arr, 0);
        llvm::Value* len = b().emitLoad(b().i64Ty(), len_ptr);
        llvm::Value* cap = b().emitLoad(b().i64Ty(), cap_ptr);
        llvm::Value* head = b().emitLoad(b().i64Ty(), head_ptr);
        llvm::Value* tail = b().emitAdd(head, len);
        llvm::Value* full = b().emitICmp(llvm::CmpInst::ICMP_UGE, len, cap);
        b().emitCondBr(full, grow, write);
        b().setInsertPoint(grow);
        llvm::Value* new_cap = b().emitMul(cap, b().constI64(2));
        llvm::Value* new_bytes = b().emitMul(new_cap, elem_size);
        llvm::Value* old_data = b().emitLoad(b().ptrTy(), data_ptr);
        llvm::Value* new_data = b().emitCall(realloc_fn, {old_data, new_bytes});
        b().emitStore(new_data, data_ptr);
        b().emitStore(new_cap, cap_ptr);
        b().emitBr(write);
        b().setInsertPoint(write);
        // Re-load values that may differ after grow (use PHI-safe approach: reload).
        llvm::Value* data = b().emitLoad(b().ptrTy(), data_ptr);
        llvm::Value* len2 = b().emitLoad(b().i64Ty(), len_ptr);
        llvm::Value* head2 = b().emitLoad(b().i64Ty(), head_ptr);
        llvm::Value* tail2 = b().emitAdd(head2, len2);
        (void)tail;
        llvm::Value* offset = b().emitMul(tail2, elem_size);
        llvm::Value* slot = b().emitGEP(b().i8Ty(), data, {offset});
        b().emitCall(memcpy_fn, {slot, elem, elem_size});
        b().emitStore(b().emitAdd(len2, b().constI64(1)), len_ptr);
        b().emitRetVoid();
    }

    // __xlang_array_pop_front
    {
        auto* ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().i64Ty()});
        llvm::Function* fn = b().createFunction("__xlang_array_pop_front", ty,
                                                llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* arr = fn->getArg(0);
        llvm::Value* elem_size = fn->getArg(1);
        llvm::Value* len_ptr = b().emitStructGEP(array_hdr_ty_, arr, 1);
        llvm::Value* head_ptr = b().emitStructGEP(array_hdr_ty_, arr, 3);
        llvm::Value* data_ptr = b().emitStructGEP(array_hdr_ty_, arr, 0);
        llvm::Value* len = b().emitLoad(b().i64Ty(), len_ptr);
        llvm::Value* head = b().emitLoad(b().i64Ty(), head_ptr);
        llvm::Value* data = b().emitLoad(b().ptrTy(), data_ptr);
        llvm::Value* offset = b().emitMul(head, elem_size);
        llvm::Value* slot = b().emitGEP(b().i8Ty(), data, {offset});
        llvm::Value* buf = b().emitCall(malloc_fn, {elem_size});
        b().emitCall(memcpy_fn, {buf, slot, elem_size});
        b().emitStore(b().emitAdd(head, b().constI64(1)), head_ptr);
        b().emitStore(b().emitSub(len, b().constI64(1)), len_ptr);
        b().emitRet(buf);
    }

    // __xlang_array_get_raw
    {
        auto* ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().i64Ty(), b().i64Ty()});
        llvm::Function* fn =
            b().createFunction("__xlang_array_get_raw", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* arr = fn->getArg(0);
        llvm::Value* index = fn->getArg(1);
        llvm::Value* elem_size = fn->getArg(2);
        llvm::Value* head =
            b().emitLoad(b().i64Ty(), b().emitStructGEP(array_hdr_ty_, arr, 3));
        llvm::Value* data =
            b().emitLoad(b().ptrTy(), b().emitStructGEP(array_hdr_ty_, arr, 0));
        llvm::Value* pos = b().emitAdd(head, index);
        llvm::Value* offset = b().emitMul(pos, elem_size);
        llvm::Value* slot = b().emitGEP(b().i8Ty(), data, {offset});
        llvm::Value* buf = b().emitCall(malloc_fn, {elem_size});
        b().emitCall(memcpy_fn, {buf, slot, elem_size});
        b().emitRet(buf);
    }

    // __xlang_array_pop_raw
    {
        auto* ty = b().functionType(b().ptrTy(), {b().ptrTy(), b().i64Ty()});
        llvm::Function* fn =
            b().createFunction("__xlang_array_pop_raw", ty, llvm::Function::InternalLinkage);
        llvm::BasicBlock* entry = b().createBlock(fn, "entry");
        b().setInsertPoint(entry);
        llvm::Value* arr = fn->getArg(0);
        llvm::Value* elem_size = fn->getArg(1);
        llvm::Value* len_ptr = b().emitStructGEP(array_hdr_ty_, arr, 1);
        llvm::Value* len = b().emitLoad(b().i64Ty(), len_ptr);
        llvm::Value* last = b().emitSub(len, b().constI64(1));
        llvm::Value* head =
            b().emitLoad(b().i64Ty(), b().emitStructGEP(array_hdr_ty_, arr, 3));
        llvm::Value* data =
            b().emitLoad(b().ptrTy(), b().emitStructGEP(array_hdr_ty_, arr, 0));
        llvm::Value* pos = b().emitAdd(head, last);
        llvm::Value* offset = b().emitMul(pos, elem_size);
        llvm::Value* slot = b().emitGEP(b().i8Ty(), data, {offset});
        llvm::Value* buf = b().emitCall(malloc_fn, {elem_size});
        b().emitCall(memcpy_fn, {buf, slot, elem_size});
        b().emitStore(last, len_ptr);
        b().emitRet(buf);
    }
}


}  // namespace xlang
