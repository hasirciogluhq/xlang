#include "xlang/codegen/builder.h"

#include "xlang/error.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <format>
#include <utility>

namespace xlang {

struct CommonIrBuilder::Impl {
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
};

CommonIrBuilder::CommonIrBuilder(std::string module_name, std::string target_triple)
    : impl_(std::make_unique<Impl>()), triple_(std::move(target_triple)) {
    if (triple_.empty()) {
        triple_ = llvm::sys::getDefaultTargetTriple();
    }
    impl_->context = std::make_unique<llvm::LLVMContext>();
    impl_->module = std::make_unique<llvm::Module>(std::move(module_name), *impl_->context);
    impl_->module->setTargetTriple(llvm::Triple(triple_));
    impl_->builder = std::make_unique<llvm::IRBuilder<>>(*impl_->context);

    try {
        TargetEmit emit(triple_);
        emit.applyDataLayout(*impl_->module);
    } catch (const XlangError&) {
        // Target lookup may fail in incomplete toolchains; Module still usable.
    }
}

CommonIrBuilder::~CommonIrBuilder() = default;

CommonIrBuilder::CommonIrBuilder(CommonIrBuilder&&) noexcept = default;
CommonIrBuilder& CommonIrBuilder::operator=(CommonIrBuilder&&) noexcept = default;

llvm::LLVMContext& CommonIrBuilder::context() { return *impl_->context; }

llvm::Module& CommonIrBuilder::module() { return *impl_->module; }

const llvm::Module& CommonIrBuilder::module() const { return *impl_->module; }

llvm::IRBuilder<>& CommonIrBuilder::ir() { return *impl_->builder; }

ReleasedModule CommonIrBuilder::release() {
    ReleasedModule out;
    impl_->builder.reset();
    out.module = std::move(impl_->module);
    out.context = std::move(impl_->context);
    return out;
}

void CommonIrBuilder::setInsertPoint(llvm::BasicBlock* block) {
    impl_->builder->SetInsertPoint(block);
}

llvm::Type* CommonIrBuilder::voidTy() { return llvm::Type::getVoidTy(*impl_->context); }

llvm::Type* CommonIrBuilder::i1Ty() { return llvm::Type::getInt1Ty(*impl_->context); }

llvm::Type* CommonIrBuilder::i8Ty() { return llvm::Type::getInt8Ty(*impl_->context); }

llvm::Type* CommonIrBuilder::i32Ty() { return llvm::Type::getInt32Ty(*impl_->context); }

llvm::Type* CommonIrBuilder::i64Ty() { return llvm::Type::getInt64Ty(*impl_->context); }

llvm::Type* CommonIrBuilder::i128Ty() { return llvm::Type::getInt128Ty(*impl_->context); }

llvm::Type* CommonIrBuilder::floatTy() { return llvm::Type::getFloatTy(*impl_->context); }

llvm::Type* CommonIrBuilder::doubleTy() { return llvm::Type::getDoubleTy(*impl_->context); }

llvm::Type* CommonIrBuilder::ptrTy() { return llvm::PointerType::getUnqual(*impl_->context); }

llvm::Constant* CommonIrBuilder::constI1(bool value) {
    return llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i1Ty()), value ? 1 : 0);
}

llvm::Constant* CommonIrBuilder::constI8(std::int64_t value) {
    return llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i8Ty()), value, true);
}

llvm::Constant* CommonIrBuilder::constI32(std::int64_t value) {
    return llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i32Ty()), value, true);
}

llvm::Constant* CommonIrBuilder::constI64(std::int64_t value) {
    return llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i64Ty()), value, true);
}

llvm::Constant* CommonIrBuilder::constF64(double value) {
    return llvm::ConstantFP::get(doubleTy(), value);
}

llvm::Constant* CommonIrBuilder::constNullPtr() {
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy()));
}

llvm::Constant* CommonIrBuilder::constZero(llvm::Type* type) {
    return llvm::Constant::getNullValue(type);
}

llvm::FunctionType* CommonIrBuilder::functionType(llvm::Type* ret,
                                                 const std::vector<llvm::Type*>& params,
                                                 bool variadic) {
    return llvm::FunctionType::get(ret, params, variadic);
}

llvm::Function* CommonIrBuilder::declareFunction(std::string_view name, llvm::FunctionType* type) {
    if (llvm::Function* existing = impl_->module->getFunction(name)) {
        return existing;
    }
    return llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, impl_->module.get());
}

llvm::Function* CommonIrBuilder::createFunction(std::string_view name, llvm::FunctionType* type,
                                                llvm::GlobalValue::LinkageTypes linkage) {
    if (llvm::Function* existing = impl_->module->getFunction(name)) {
        if (existing->empty()) {
            existing->setLinkage(linkage);
            return existing;
        }
        throw XlangError(std::format("function `{}` already defined", name));
    }
    return llvm::Function::Create(type, linkage, name, impl_->module.get());
}

llvm::Function* CommonIrBuilder::getFunction(std::string_view name) {
    return impl_->module->getFunction(name);
}

llvm::BasicBlock* CommonIrBuilder::createBlock(llvm::Function* function, std::string_view name) {
    return llvm::BasicBlock::Create(*impl_->context, name, function);
}

llvm::GlobalVariable* CommonIrBuilder::createGlobal(std::string_view name, llvm::Type* type,
                                                    llvm::Constant* init,
                                                    llvm::GlobalValue::LinkageTypes linkage,
                                                    bool is_constant) {
    if (auto* existing = impl_->module->getGlobalVariable(name, true)) {
        return existing;
    }
    return new llvm::GlobalVariable(*impl_->module, type, is_constant, linkage, init, name);
}

llvm::GlobalVariable* CommonIrBuilder::createExternalGlobal(std::string_view name,
                                                            llvm::Type* type) {
    if (auto* existing = impl_->module->getGlobalVariable(name, true)) {
        return existing;
    }
    return new llvm::GlobalVariable(*impl_->module, type, false,
                                    llvm::GlobalValue::ExternalLinkage, nullptr, name);
}

llvm::Value* CommonIrBuilder::createStringGlobal(std::string_view text, std::string_view name) {
    return impl_->builder->CreateGlobalString(text, name);
}

llvm::AllocaInst* CommonIrBuilder::emitAlloca(llvm::Type* type, std::string_view name) {
    return impl_->builder->CreateAlloca(type, nullptr, name);
}

llvm::Value* CommonIrBuilder::emitLoad(llvm::Type* type, llvm::Value* ptr, std::string_view name) {
    return impl_->builder->CreateLoad(type, ptr, name);
}

void CommonIrBuilder::emitStore(llvm::Value* value, llvm::Value* ptr) {
    impl_->builder->CreateStore(value, ptr);
}

llvm::Value* CommonIrBuilder::emitAdd(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateAdd(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitSub(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateSub(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitMul(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateMul(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitSDiv(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateSDiv(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitFAdd(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateFAdd(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitFSub(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateFSub(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitFMul(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateFMul(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitFDiv(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateFDiv(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitAnd(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateAnd(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitOr(llvm::Value* lhs, llvm::Value* rhs) {
    return impl_->builder->CreateOr(lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitICmp(llvm::CmpInst::Predicate pred, llvm::Value* lhs,
                                      llvm::Value* rhs) {
    return impl_->builder->CreateICmp(pred, lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitFCmp(llvm::CmpInst::Predicate pred, llvm::Value* lhs,
                                      llvm::Value* rhs) {
    return impl_->builder->CreateFCmp(pred, lhs, rhs);
}

llvm::Value* CommonIrBuilder::emitSelect(llvm::Value* cond, llvm::Value* true_v,
                                        llvm::Value* false_v) {
    return impl_->builder->CreateSelect(cond, true_v, false_v);
}

llvm::Value* CommonIrBuilder::emitSExt(llvm::Value* value, llvm::Type* dest) {
    return impl_->builder->CreateSExt(value, dest);
}

llvm::Value* CommonIrBuilder::emitZExt(llvm::Value* value, llvm::Type* dest) {
    return impl_->builder->CreateZExt(value, dest);
}

llvm::Value* CommonIrBuilder::emitTrunc(llvm::Value* value, llvm::Type* dest) {
    return impl_->builder->CreateTrunc(value, dest);
}

llvm::Value* CommonIrBuilder::emitBitCast(llvm::Value* value, llvm::Type* dest) {
    if (value->getType() == dest) {
        return value;
    }
    return impl_->builder->CreateBitCast(value, dest);
}

llvm::Value* CommonIrBuilder::emitPtrToInt(llvm::Value* value, llvm::Type* dest) {
    return impl_->builder->CreatePtrToInt(value, dest);
}

llvm::Value* CommonIrBuilder::emitIntToPtr(llvm::Value* value, llvm::Type* dest) {
    return impl_->builder->CreateIntToPtr(value, dest);
}

llvm::Value* CommonIrBuilder::emitGEP(llvm::Type* pointee, llvm::Value* ptr,
                                     llvm::ArrayRef<llvm::Value*> indices) {
    return impl_->builder->CreateGEP(pointee, ptr, indices);
}

llvm::Value* CommonIrBuilder::emitStructGEP(llvm::StructType* type, llvm::Value* ptr,
                                           unsigned index) {
    return impl_->builder->CreateStructGEP(type, ptr, index);
}

llvm::Value* CommonIrBuilder::emitCall(llvm::Function* callee,
                                      const std::vector<llvm::Value*>& args) {
    return impl_->builder->CreateCall(callee, args);
}

llvm::Value* CommonIrBuilder::emitCall(llvm::FunctionCallee callee,
                                      llvm::ArrayRef<llvm::Value*> args) {
    return impl_->builder->CreateCall(callee, args);
}

void CommonIrBuilder::emitBr(llvm::BasicBlock* dest) { impl_->builder->CreateBr(dest); }

void CommonIrBuilder::emitCondBr(llvm::Value* cond, llvm::BasicBlock* true_bb,
                                llvm::BasicBlock* false_bb) {
    impl_->builder->CreateCondBr(cond, true_bb, false_bb);
}

void CommonIrBuilder::emitRet(llvm::Value* value) { impl_->builder->CreateRet(value); }

void CommonIrBuilder::emitRetVoid() { impl_->builder->CreateRetVoid(); }

llvm::Value* CommonIrBuilder::emitNativeSyscall(llvm::Value* number,
                                               const std::vector<llvm::Value*>& args) {
    const NativeSyscallAsm asm_info = nativeSyscallAsmForTriple(triple_, args.size());

    std::vector<llvm::Type*> param_tys;
    param_tys.reserve(1 + args.size());
    param_tys.push_back(i64Ty());
    for (std::size_t i = 0; i < args.size(); ++i) {
        param_tys.push_back(i64Ty());
    }

    llvm::FunctionType* asm_ty = llvm::FunctionType::get(i64Ty(), param_tys, false);
    llvm::InlineAsm* inline_asm = llvm::InlineAsm::get(
        asm_ty, asm_info.instruction, asm_info.constraints, /*hasSideEffects=*/true);

    std::vector<llvm::Value*> call_args;
    call_args.reserve(1 + args.size());
    call_args.push_back(number);
    for (llvm::Value* arg : args) {
        call_args.push_back(arg);
    }
    return impl_->builder->CreateCall(asm_ty, inline_asm, call_args);
}

std::string CommonIrBuilder::toIrString() const {
    std::string out;
    llvm::raw_string_ostream stream(out);
    impl_->module->print(stream, nullptr);
    return out;
}

}  // namespace xlang
