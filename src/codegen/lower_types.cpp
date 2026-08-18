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

CommonIrBuilder& Codegen::b() { return *irb_; }

llvm::Function* Codegen::ensureFn(std::string_view name, llvm::FunctionType* type) {
    if (llvm::Function* existing = b().getFunction(name)) {
        return existing;
    }
    return b().declareFunction(name, type);
}

std::string Codegen::globalName(const std::string& name) { return "g_" + name; }

llvm::Type* Codegen::llvmType(const Type& type) {
    switch (type.kind) {
    case TypeKind::Void:
        return b().voidTy();
    case TypeKind::Int32:
        return b().i32Ty();
    case TypeKind::Int64:
        return b().i64Ty();
    case TypeKind::BigInt:
        return b().i128Ty();
    case TypeKind::Float:
        return b().floatTy();
    case TypeKind::Double:
        return b().doubleTy();
    case TypeKind::Bool:
    case TypeKind::Char:
        return b().i8Ty();
    case TypeKind::String:
    case TypeKind::Interface:
    case TypeKind::Pointer:
    case TypeKind::Struct:
        return b().ptrTy();
    }
    throw XlangError("invalid type for LLVM lowering");
}

llvm::StructType* Codegen::structBodyType(const std::string& name) {
    const auto it = struct_types_.find(name);
    if (it == struct_types_.end()) {
        throw XlangError(std::format("unknown struct type `{}`", name));
    }
    return it->second;
}

llvm::Constant* Codegen::zeroOf(const Type& type) { return b().constZero(llvmType(type)); }

llvm::Value* Codegen::coerceInt(llvm::Value* value, const Type& from, const Type& to) {
    if (typesEqual(from, to)) {
        return value;
    }
    if (from.kind == TypeKind::Int32 && to.kind == TypeKind::Int64) {
        return b().emitSExt(value, b().i64Ty());
    }
    if (from.kind == TypeKind::Int64 && to.kind == TypeKind::Int32) {
        return b().emitTrunc(value, b().i32Ty());
    }
    return value;
}

llvm::Value* Codegen::asI64(const Type& ty, llvm::Value* value) {
    if (ty.kind == TypeKind::Int64) {
        return value;
    }
    if (ty.kind == TypeKind::Int32) {
        return b().emitSExt(value, b().i64Ty());
    }
    if (ty.kind == TypeKind::Bool || ty.kind == TypeKind::Char) {
        return b().emitZExt(value, b().i64Ty());
    }
    throw XlangError("@syscall arguments must be integer types");
}

llvm::Value* Codegen::boolToI1(llvm::Value* value) {
    return b().emitICmp(llvm::CmpInst::ICMP_NE, value, b().constI8(0));
}

std::string Codegen::freshLabel() { return "L" + std::to_string(label_counter_++); }

std::size_t Codegen::typeSizeBytes(const Type& type) const {
    if (type.kind == TypeKind::Struct) {
        const StructDecl* decl = findStruct(type.struct_name);
        if (decl == nullptr) {
            throw XlangError(std::format("unknown struct `{}`", type.struct_name));
        }
        return structSizeBytes(*decl);
    }
    switch (type.kind) {
    case TypeKind::Int32:
    case TypeKind::Float:
        return 4;
    case TypeKind::Int64:
    case TypeKind::Double:
    case TypeKind::BigInt:
    case TypeKind::String:
    case TypeKind::Pointer:
        return 8;
    case TypeKind::Bool:
    case TypeKind::Char:
        return 1;
    default:
        return llvmTypeAlign(type);
    }
}


}  // namespace xlang
