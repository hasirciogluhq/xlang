#include <iostream>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>

int main() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("sasa", *context);
    llvm::IRBuilder<> builder(*context);

    // int add(int, int)
    llvm::FunctionType* funcType =
        llvm::FunctionType::get(int32Ty, {int32Ty, int32Ty}, false);

    llvm::Function* addFunc = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, "add", module.get());

    unsigned idx = 0;
    for (auto& arg : addFunc->args()) {
        arg.setName(idx == 0 ? "a" : "b");
        ++idx;
    }

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", addFunc);
    builder.SetInsertPoint(entry);

    auto argsIt = addFunc->arg_begin();
    llvm::Value* a = argsIt++;
    llvm::Value* b = argsIt;

    llvm::Value* sum = builder.CreateAdd(a, b, "sum");
    builder.CreateRet(sum);

    // module->print(llvm::outs(), nullptr);

    auto jitExpected = llvm::orc::LLJITBuilder().create();
    if (!jitExpected) {
        llvm::logAllUnhandledErrors(jitExpected.takeError(), llvm::errs(), "JIT error: ");
        return 1;
    }

    auto jit = std::move(*jitExpected);

    // Module ve onu oluşturan context aynı ThreadSafeModule içinde taşınmalı.
    if (auto err = jit->addIRModule(
            llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::errs() << "add module failed\n";
        llvm::logAllUnhandledErrors(std::move(err), llvm::errs(), "");
        return 1;
    }

    auto addrExpected = jit->lookup("add");
    if (!addrExpected) {
        llvm::logAllUnhandledErrors(addrExpected.takeError(), llvm::errs(), "lookup error: ");
        return 1;
    }

    auto* add = addrExpected->toPtr<int (*)(int, int)>();
    std::cout << "add(2, 3) = " << add(2, 333) << '\n';

    return 0;
}
