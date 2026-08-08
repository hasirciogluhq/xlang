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

bool Codegen::definesFunction(const Program& program, const std::string& name,
                              const std::vector<Type>& param_types) const {
    const std::string mangled = mangleFunctionName(name, param_types);
    if (defined_functions_.contains(mangled)) {
        return true;
    }
    for (const Function& function : program.functions) {
        if (function.name != name) {
            continue;
        }
        if (paramTypesMatch(paramTypes(function.params), param_types, function.variadic)) {
            return true;
        }
    }
    return false;
}

std::optional<FunctionSignature> Codegen::resolveFunctionCall(
    const std::string& name, const std::vector<Type>& arg_types) const {
    const std::vector<FunctionSignature> defined = collectDefinedFunctions(*program_);
    if (const std::optional<FunctionSignature> match =
            findMatchingFunction(name, arg_types, defined)) {
        return match;
    }
    for (const auto& [alias, _] : import_aliases_) {
        const std::string prefixed = importPrefixedName(alias, name);
        if (const std::optional<FunctionSignature> match =
                findMatchingFunction(prefixed, arg_types, defined)) {
            return match;
        }
    }
    if (const std::optional<FunctionSignature> match =
            findMatchingFunction(name, arg_types, options_.runtime_exports)) {
        return match;
    }
    for (const auto& [alias, _] : import_aliases_) {
        const std::string prefixed = importPrefixedName(alias, name);
        if (const std::optional<FunctionSignature> match =
                findMatchingFunction(prefixed, arg_types, options_.runtime_exports)) {
            return match;
        }
    }
    if (const std::optional<FunctionSignature> match =
            findMatchingFunction(name, arg_types, options_.runtime_syscalls)) {
        return match;
    }
    return std::nullopt;
}

std::optional<FunctionSignature> Codegen::resolveMethodCall(
    const std::string& name, const Type& receiver_type,
    const std::vector<Type>& arg_types) const {
    std::vector<Type> full_args;
    full_args.push_back(receiver_type);
    full_args.insert(full_args.end(), arg_types.begin(), arg_types.end());
    if (const std::optional<FunctionSignature> exact = resolveFunctionCall(name, full_args)) {
        return exact;
    }

    const std::string suffix = "_" + name;
    const FunctionSignature* match = nullptr;
    const std::vector<FunctionSignature> defined = collectDefinedFunctions(*program_);
    for (const FunctionSignature& candidate : defined) {
        if (candidate.params.empty()) {
            continue;
        }
        if (!typesEqual(candidate.params[0].type, receiver_type)) {
            continue;
        }
        if (candidate.name.size() < suffix.size()) {
            continue;
        }
        if (candidate.name.substr(candidate.name.size() - suffix.size()) != suffix) {
            continue;
        }
        std::vector<Type> expected = paramTypes(candidate.params);
        if (!paramTypesMatchWithWidening(expected, full_args, candidate.variadic)) {
            continue;
        }
        if (match != nullptr) {
            throw XlangError(std::format("ambiguous method call `{}`", name));
        }
        match = &candidate;
    }
    if (match == nullptr) {
        return std::nullopt;
    }
    return *match;
}

std::string Codegen::importPrefixedName(const std::string& alias,
                                        const std::string& method) const {
    return alias + "_" + method;
}

Type Codegen::resolveVarType(const std::string& name, const LocalMap& locals) const {
    (void)locals;
    const auto local_it = local_types_.find(name);
    if (local_it != local_types_.end()) {
        return local_it->second;
    }
    const auto global_it = global_types_.find(name);
    if (global_it != global_types_.end()) {
        return global_it->second;
    }
    throw XlangError(std::format("unknown variable `{}`", name));
}

llvm::Value* Codegen::resolveVar(const std::string& name, const LocalMap& locals) const {
    const auto local_it = locals.find(name);
    if (local_it != locals.end()) {
        return local_it->second;
    }
    const auto global_it = global_vars_.find(name);
    if (global_it != global_vars_.end()) {
        return global_it->second;
    }
    throw XlangError(std::format("unknown variable `{}`", name));
}

}  // namespace xlang
