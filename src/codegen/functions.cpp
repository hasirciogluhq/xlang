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

void Codegen::emitDeclareFunction(const FunctionSignature& function) {
    const std::vector<Type> param_type_list = paramTypes(function.params);
    const std::string llvm_name =
        mangleFunctionName(function.name, param_type_list, function.variadic);
    std::vector<llvm::Type*> params;
    for (const TypedName& p : function.params) {
        params.push_back(llvmType(p.type));
    }
    b().declareFunction(llvm_name,
                        b().functionType(llvmType(function.return_type), params,
                                         function.variadic));
}

void Codegen::emitDeclareFunction(const Function& function) {
    if (function.syscall) {
        emitNativeSyscallFunction(function);
        return;
    }

    // Plain `declare name(...)` / external — blind LLVM declare, raw C ABI symbol.
    std::vector<llvm::Type*> params;
    for (const TypedName& p : function.params) {
        params.push_back(llvmType(p.type));
    }
    b().declareFunction(function.name,
                        b().functionType(llvmType(function.return_type), params,
                                         function.variadic));
}

void Codegen::emitNativeSyscallFunction(const Function& function) {
    // declare syscall <n> name(args): ret
    // Define a real function that issues the CPU trap (not a bridge/C call).
    syscalls_.insert(function.name);

    std::vector<llvm::Type*> param_tys;
    for (const TypedName& p : function.params) {
        param_tys.push_back(llvmType(p.type));
    }
    llvm::FunctionType* fty =
        b().functionType(llvmType(function.return_type), param_tys, function.variadic);
    llvm::Function* fn =
        b().createFunction(function.name, fty, llvm::Function::InternalLinkage);
    current_function_ = fn;

    for (std::size_t i = 0; i < function.params.size(); ++i) {
        fn->getArg(i)->setName(function.params[i].name);
    }

    llvm::BasicBlock* entry = b().createBlock(fn, "entry");
    b().setInsertPoint(entry);

    std::vector<llvm::Value*> args_i64;
    args_i64.reserve(function.params.size());
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        args_i64.push_back(asI64(function.params[i].type, fn->getArg(i)));
    }

    llvm::Value* result =
        b().emitNativeSyscall(b().constI64(function.syscall_number), args_i64);

    if (function.return_type.kind == TypeKind::Void) {
        b().emitRetVoid();
    } else if (function.return_type.kind == TypeKind::Int64) {
        b().emitRet(result);
    } else if (function.return_type.kind == TypeKind::Int32) {
        b().emitRet(b().emitTrunc(result, b().i32Ty()));
    } else if (function.return_type.kind == TypeKind::Bool ||
               function.return_type.kind == TypeKind::Char) {
        b().emitRet(b().emitTrunc(result, b().i8Ty()));
    } else if (function.return_type.kind == TypeKind::Pointer ||
               function.return_type.kind == TypeKind::String ||
               function.return_type.kind == TypeKind::Struct) {
        b().emitRet(b().emitIntToPtr(result, b().ptrTy()));
    } else {
        throw XlangError(std::format(
            "declare syscall `{}` has unsupported return type `{}`", function.name,
            typeToString(function.return_type)));
    }

    current_function_ = nullptr;
}

void Codegen::emitFunction(const Function& function) {
    const std::vector<Type> param_type_list = paramTypes(function.params);
    const std::string llvm_name =
        mangleFunctionName(function.name, param_type_list, function.variadic);

    std::vector<llvm::Type*> param_tys;
    for (const TypedName& p : function.params) {
        param_tys.push_back(llvmType(p.type));
    }
    llvm::FunctionType* fty =
        b().functionType(llvmType(function.return_type), param_tys, function.variadic);
    llvm::Function* fn = b().createFunction(llvm_name, fty, fnLinkage(function));
    current_function_ = fn;

    for (std::size_t i = 0; i < function.params.size(); ++i) {
        fn->getArg(i)->setName(function.params[i].name);
    }

    llvm::BasicBlock* entry = b().createBlock(fn, "entry");
    b().setInsertPoint(entry);

    if (function.variadic && function.name == "print") {
        b().emitRet(b().constI32(0));
        current_function_ = nullptr;
        return;
    }

    LocalMap locals;
    local_types_.clear();
    current_return_type_ = function.return_type;
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        const TypedName& param = function.params[i];
        allocLocal(param.name, param.type, locals);
        storeValue(param.type, fn->getArg(i), locals.at(param.name));
    }

    if (function.name == "main" && needs_global_init_) {
        llvm::Function* init = b().getFunction("__xlang_init_globals");
        if (init != nullptr) {
            b().emitCall(init, {});
        }
    }

    bool has_return = false;
    for (const Stmt& stmt : function.body.statements) {
        if (emitStatement(stmt, locals)) {
            has_return = true;
            break;
        }
    }

    if (!has_return) {
        if (function.return_type.kind == TypeKind::Void) {
            b().emitRetVoid();
        } else {
            b().emitRet(zeroOf(function.return_type));
        }
    }

    current_function_ = nullptr;
}

llvm::GlobalValue::LinkageTypes Codegen::fnLinkage(const Function& function) const {
    if (function.external) {
        return llvm::Function::ExternalLinkage;
    }
    if (!function.exported && (options_.build_kind == BuildKind::Object ||
                               options_.build_kind == BuildKind::Static)) {
        return llvm::Function::InternalLinkage;
    }
    return llvm::Function::ExternalLinkage;
}

void Codegen::allocLocal(const std::string& name, const Type& type, LocalMap& locals) {
    llvm::AllocaInst* ptr = b().emitAlloca(llvmType(type), name + ".addr");
    locals[name] = ptr;
    local_types_[name] = type;
}

void Codegen::storeValue(const Type& type, llvm::Value* value, llvm::Value* ptr) {
    (void)type;
    b().emitStore(value, ptr);
}

std::pair<Type, llvm::Value*> Codegen::loadValue(const Type& type, llvm::Value* ptr) {
    return {type, b().emitLoad(llvmType(type), ptr)};
}

}  // namespace xlang
