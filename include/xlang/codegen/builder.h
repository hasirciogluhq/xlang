#pragma once

#include "xlang/codegen/target.h"

#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace llvm {
class AllocaInst;
class BasicBlock;
class Constant;
class Function;
class FunctionType;
class GlobalVariable;
class StructType;
class Type;
class Value;
}  // namespace llvm

namespace xlang {

/// Ownership transferred out of CommonIrBuilder via release().
struct ReleasedModule {
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
};

/// OS-agnostic IR construction: Module via LLVM C++ API.
/// Blind declare/call for external/syscall symbols; native syscall = CPU trap.
class CommonIrBuilder {
public:
    CommonIrBuilder(std::string module_name, std::string target_triple);
    ~CommonIrBuilder();

    CommonIrBuilder(const CommonIrBuilder&) = delete;
    CommonIrBuilder& operator=(const CommonIrBuilder&) = delete;
    CommonIrBuilder(CommonIrBuilder&&) noexcept;
    CommonIrBuilder& operator=(CommonIrBuilder&&) noexcept;

    [[nodiscard]] llvm::LLVMContext& context();
    [[nodiscard]] llvm::Module& module();
    [[nodiscard]] const llvm::Module& module() const;
    [[nodiscard]] llvm::IRBuilder<>& ir();
    [[nodiscard]] const std::string& triple() const { return triple_; }

    /// Move Module+Context out; builder becomes empty and must not be used.
    [[nodiscard]] ReleasedModule release();

    void setInsertPoint(llvm::BasicBlock* block);

    [[nodiscard]] llvm::Type* voidTy();
    [[nodiscard]] llvm::Type* i1Ty();
    [[nodiscard]] llvm::Type* i8Ty();
    [[nodiscard]] llvm::Type* i32Ty();
    [[nodiscard]] llvm::Type* i64Ty();
    [[nodiscard]] llvm::Type* i128Ty();
    [[nodiscard]] llvm::Type* floatTy();
    [[nodiscard]] llvm::Type* doubleTy();
    [[nodiscard]] llvm::Type* ptrTy();

    [[nodiscard]] llvm::Constant* constI1(bool value);
    [[nodiscard]] llvm::Constant* constI8(std::int64_t value);
    [[nodiscard]] llvm::Constant* constI32(std::int64_t value);
    [[nodiscard]] llvm::Constant* constI64(std::int64_t value);
    [[nodiscard]] llvm::Constant* constF64(double value);
    [[nodiscard]] llvm::Constant* constNullPtr();
    [[nodiscard]] llvm::Constant* constZero(llvm::Type* type);

    [[nodiscard]] llvm::FunctionType* functionType(llvm::Type* ret,
                                                   const std::vector<llvm::Type*>& params,
                                                   bool variadic = false);

    /// Blind declare — no whitelist, no body.
    llvm::Function* declareFunction(std::string_view name, llvm::FunctionType* type);

    llvm::Function* createFunction(std::string_view name, llvm::FunctionType* type,
                                   llvm::GlobalValue::LinkageTypes linkage =
                                       llvm::Function::ExternalLinkage);

    [[nodiscard]] llvm::Function* getFunction(std::string_view name);

    llvm::BasicBlock* createBlock(llvm::Function* function, std::string_view name);

    llvm::GlobalVariable* createGlobal(std::string_view name, llvm::Type* type,
                                       llvm::Constant* init,
                                       llvm::GlobalValue::LinkageTypes linkage =
                                           llvm::GlobalValue::ExternalLinkage,
                                       bool is_constant = false);

    llvm::GlobalVariable* createExternalGlobal(std::string_view name, llvm::Type* type);

    /// Private unnamed_addr string constant; returns i8* to first byte.
    llvm::Value* createStringGlobal(std::string_view text, std::string_view name);

    llvm::AllocaInst* emitAlloca(llvm::Type* type, std::string_view name = "");
    llvm::Value* emitLoad(llvm::Type* type, llvm::Value* ptr, std::string_view name = "");
    void emitStore(llvm::Value* value, llvm::Value* ptr);

    llvm::Value* emitAdd(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitSub(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitMul(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitSDiv(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitFAdd(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitFSub(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitFMul(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitFDiv(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitAnd(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitOr(llvm::Value* lhs, llvm::Value* rhs);

    llvm::Value* emitICmp(llvm::CmpInst::Predicate pred, llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitFCmp(llvm::CmpInst::Predicate pred, llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* emitSelect(llvm::Value* cond, llvm::Value* true_v, llvm::Value* false_v);

    llvm::Value* emitSExt(llvm::Value* value, llvm::Type* dest);
    llvm::Value* emitZExt(llvm::Value* value, llvm::Type* dest);
    llvm::Value* emitTrunc(llvm::Value* value, llvm::Type* dest);
    llvm::Value* emitBitCast(llvm::Value* value, llvm::Type* dest);
    llvm::Value* emitPtrToInt(llvm::Value* value, llvm::Type* dest);
    llvm::Value* emitIntToPtr(llvm::Value* value, llvm::Type* dest);

    llvm::Value* emitGEP(llvm::Type* pointee, llvm::Value* ptr,
                         llvm::ArrayRef<llvm::Value*> indices);
    llvm::Value* emitStructGEP(llvm::StructType* type, llvm::Value* ptr, unsigned index);

    llvm::Value* emitCall(llvm::Function* callee, const std::vector<llvm::Value*>& args);
    llvm::Value* emitCall(llvm::FunctionCallee callee, llvm::ArrayRef<llvm::Value*> args);

    void emitBr(llvm::BasicBlock* dest);
    void emitCondBr(llvm::Value* cond, llvm::BasicBlock* true_bb, llvm::BasicBlock* false_bb);
    void emitRet(llvm::Value* value);
    void emitRetVoid();

    /// Emit CPU-native syscall trap (x86_64: syscall, aarch64: svc #0).
    /// `number` and `args` are i64 values; returns i64.
    llvm::Value* emitNativeSyscall(llvm::Value* number, const std::vector<llvm::Value*>& args);

    [[nodiscard]] std::string toIrString() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string triple_;
};

}  // namespace xlang
