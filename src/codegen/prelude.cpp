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

void Codegen::collectSyscalls(const Program& program) {
    for (const Function& function : program.functions) {
        if (function.syscall) {
            syscalls_.insert(function.name);
        }
    }
}

void Codegen::emitPrelude(const Program& program) {
    if (needs_heap_) {
        ensureFn("malloc", b().functionType(b().ptrTy(), {b().i64Ty()}));
        ensureFn("free", b().functionType(b().voidTy(), {b().ptrTy()}));
    }

    ensureFn("printf", b().functionType(b().i32Ty(), {b().ptrTy()}, true));
    print_nl_ = b().createStringGlobal("\n", "__xlang_print_nl");
    print_fmt_s_ = b().createStringGlobal("%s", "__xlang_print_fmt_s");
    print_fmt_d_ = b().createStringGlobal("%d", "__xlang_print_fmt_d");
    print_fmt_f_ = b().createStringGlobal("%f", "__xlang_print_fmt_f");

    for (const Function& function : program.functions) {
        if (function.external) {
            // External C ABI: preserve the raw symbol name.
            defined_functions_.insert(function.name);
        }
    }

}



}  // namespace xlang
