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

Codegen::Codegen(CodegenOptions options) : options_(std::move(options)) {}

CodegenResult::CodegenResult() = default;
CodegenResult::~CodegenResult() = default;
CodegenResult::CodegenResult(CodegenResult&&) noexcept = default;
CodegenResult& CodegenResult::operator=(CodegenResult&&) noexcept = default;

std::string CodegenResult::dumpIr() const {
    if (!module) {
        return {};
    }
    std::string out;
    llvm::raw_string_ostream stream(out);
    module->print(stream, nullptr);
    return out;
}

CodegenResult Codegen::generate(const Program& program, const CodegenOptions& options) {
    Codegen cg(options);
    cg.program_ = &program;

    const std::string triple = options.target_triple.empty()
                                   ? llvm::sys::getDefaultTargetTriple()
                                   : options.target_triple;
    cg.irb_ = std::make_unique<CommonIrBuilder>("xlang", triple);

    for (const ImportDecl& import : program.imports) {
        if (!import.alias.empty()) {
            cg.import_aliases_[import.alias] = import.alias;
        }
        if (import.is_clause_import) {
            for (const ImportSpec& spec : import.names) {
                if (spec.use_prefix && !spec.bound_alias.empty()) {
                    cg.import_aliases_[spec.bound_alias] = spec.bound_alias;
                }
            }
        }
    }
    for (const auto& [alias, target] : program.import_aliases) {
        cg.import_aliases_[alias] = target;
    }
    cg.needs_heap_ =
        programUsesHeap(program) || programUsesStrings(program) || programUsesArrays(program);
    cg.needs_strings_ = programUsesStrings(program);
    cg.needs_arrays_ = programUsesArrays(program);
    for (const StructDecl& decl : options.runtime_structs) {
        if (structUsesArrayField(decl)) {
            cg.needs_arrays_ = true;
            break;
        }
    }
    cg.collectSyscalls(program);
    cg.emitPrelude(program);
    if (cg.needs_arrays_) {
        cg.emitArrayHeaderType();
    }
    cg.emitStructTypes(program);
    if (cg.needs_arrays_) {
        cg.emitArrayRuntimeSupport();
    }
    if (cg.needs_strings_) {
        cg.emitStringRuntimeSupport();
        cg.preemitStringLiterals(program);
    }
    cg.emitGlobals(program);
    cg.emitGlobalInit(program);
    for (const Function& function : program.functions) {
        // declare syscall <n> → CPU-native trap body
        // declare / external / empty → blind LLVM declare (bridge / C ABI)
        if (function.syscall) {
            cg.emitNativeSyscallFunction(function);
            continue;
        }
        if (function.external || function.body.statements.empty()) {
            cg.emitDeclareFunction(function);
            continue;
        }
        const std::vector<Type> types = paramTypes(function.params);
        cg.defined_functions_.insert(
            mangleFunctionName(function.name, types, function.variadic));
        cg.emitFunction(function);
    }

    CodegenResult result;
    result.syscalls = std::move(cg.syscalls_);
    ReleasedModule released = cg.irb_->release();
    result.context = std::move(released.context);
    result.module = std::move(released.module);

    if (!options.object_output.empty()) {
        emitObject(*result.module, options.object_output, triple);
        result.object_path = options.object_output;
    }

    return result;
}


}  // namespace xlang
