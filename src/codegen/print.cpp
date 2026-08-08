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

std::pair<Type, llvm::Value*> Codegen::emitPrintCall(
    const std::vector<std::unique_ptr<Expr>>& args, const LocalMap& locals) {
    if (args.empty()) {
        throw XlangError("print requires at least one argument");
    }

    llvm::Function* printf_fn =
        ensureFn("printf", b().functionType(b().i32Ty(), {b().ptrTy()}, true));
    auto* printf_ty = b().functionType(b().i32Ty(), {b().ptrTy()}, true);

    if (args.size() == 1) {
        const auto [ty, val] = emitExpr(*args[0], locals);
        if (isStringType(ty)) {
            b().emitCall(llvm::FunctionCallee(printf_ty, printf_fn), {print_fmt_s_, val});
        } else if (ty.kind == TypeKind::Int32) {
            b().emitCall(llvm::FunctionCallee(printf_ty, printf_fn), {print_fmt_d_, val});
        } else if (ty.isFloating()) {
            b().emitCall(llvm::FunctionCallee(printf_ty, printf_fn), {print_fmt_f_, val});
        } else {
            throw XlangError("print supports string and numeric types");
        }
    } else {
        const auto [fmt_ty, fmt_val] = emitExpr(*args[0], locals);
        if (!isStringType(fmt_ty)) {
            throw XlangError("print format string must be a string");
        }
        std::vector<llvm::Value*> call_args;
        call_args.push_back(fmt_val);
        for (std::size_t i = 1; i < args.size(); ++i) {
            call_args.push_back(emitExpr(*args[i], locals).second);
        }
        b().emitCall(llvm::FunctionCallee(printf_ty, printf_fn), call_args);
    }

    b().emitCall(llvm::FunctionCallee(printf_ty, printf_fn), {print_nl_});
    return {Type{TypeKind::Int32}, b().constI32(0)};
}

llvm::Value* Codegen::emitSpawnEntry(const Expr& arg, const LocalMap& locals) {
    if (arg.kind == Expr::Kind::FunctionRef) {
        const Function* function = findUniqueFunctionByName(*program_, arg.name);
        if (function == nullptr) {
            throw XlangError(std::format("unknown function `{}` for spawn", arg.name));
        }
        if (!function->params.empty()) {
            throw XlangError("spawn requires a bound call such as spawn(worker(1, 2))");
        }
        const std::string llvm_name =
            mangleFunctionName(function->name, paramTypes(function->params), function->variadic);
        llvm::Function* fn =
            ensureFn(llvm_name, b().functionType(b().i32Ty(), {}));
        return b().emitPtrToInt(fn, b().i64Ty());
    }

    if (arg.kind != Expr::Kind::Call) {
        throw XlangError("spawn requires a function call such as spawn(worker(1, 2))");
    }

    std::vector<Type> arg_types;
    std::vector<std::pair<Type, llvm::Value*>> arg_values;
    for (const auto& inner_arg : arg.args) {
        auto emitted = emitExpr(*inner_arg, locals);
        arg_types.push_back(emitted.first);
        arg_values.push_back(emitted);
    }

    const std::optional<FunctionSignature> resolved = resolveFunctionCall(arg.name, arg_types);
    if (!resolved) {
        throw XlangError(std::format("no matching function for spawn target `{}`", arg.name));
    }

    const std::string inner_llvm =
        mangleFunctionName(resolved->name, paramTypes(resolved->params), resolved->variadic);

    const std::size_t id = spawn_thunk_counter_++;
    std::vector<llvm::GlobalVariable*> caps;
    for (std::size_t i = 0; i < arg_values.size(); ++i) {
        const Type& ty = arg_values[i].first;
        if (ty.kind == TypeKind::Struct) {
            throw XlangError("spawn does not support struct arguments yet");
        }
        const std::string cap_name =
            "__spawn_cap_" + std::to_string(id) + "_" + std::to_string(i);
        llvm::GlobalVariable* cap =
            b().createGlobal(cap_name, llvmType(ty), zeroOf(ty),
                             llvm::GlobalValue::InternalLinkage);
        b().emitStore(arg_values[i].second, cap);
        caps.push_back(cap);
    }

    const std::string thunk_name = "__spawn_thunk_" + std::to_string(id);
    llvm::Function* thunk = b().createFunction(
        thunk_name, b().functionType(b().i32Ty(), {}), llvm::Function::InternalLinkage);
    llvm::BasicBlock* entry = b().createBlock(thunk, "entry");
    llvm::BasicBlock* saved = b().ir().GetInsertBlock();
    auto saved_ip = b().ir().saveIP();
    b().setInsertPoint(entry);

    std::vector<llvm::Type*> param_llvm_tys;
    std::vector<llvm::Value*> call_args;
    for (std::size_t i = 0; i < arg_values.size(); ++i) {
        const Type& ty = arg_values[i].first;
        param_llvm_tys.push_back(llvmType(ty));
        call_args.push_back(b().emitLoad(llvmType(ty), caps[i]));
    }
    llvm::Function* inner = ensureFn(
        inner_llvm, b().functionType(llvmType(resolved->return_type), param_llvm_tys,
                                     resolved->variadic));
    b().emitCall(inner, call_args);
    b().emitRet(b().constI32(0));

    if (saved != nullptr) {
        b().ir().restoreIP(saved_ip);
    }

    return b().emitPtrToInt(thunk, b().i64Ty());
}


}  // namespace xlang
