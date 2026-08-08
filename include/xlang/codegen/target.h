#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace llvm {
class LLVMContext;
class Module;
class TargetMachine;
}  // namespace llvm

namespace xlang {

enum class TargetArch {
    X86_64,
    AArch64,
    Unknown,
};

/// CPU-native syscall inline-asm template for a given arch and arg count.
struct NativeSyscallAsm {
    std::string instruction;
    std::string constraints;
    std::size_t max_args{6};
};

[[nodiscard]] TargetArch archFromTriple(std::string_view triple);
[[nodiscard]] NativeSyscallAsm nativeSyscallAsm(TargetArch arch, std::size_t arg_count);
[[nodiscard]] NativeSyscallAsm nativeSyscallAsmForTriple(std::string_view triple,
                                                         std::size_t arg_count);

/// Parse LLVM IR text into a Module owned by the given context.
[[nodiscard]] std::unique_ptr<llvm::Module> parseIrString(llvm::LLVMContext& context,
                                                          std::string_view ir);

/// Emit a native object file from Module via TargetMachine.
void emitObject(llvm::Module& module, const std::filesystem::path& path,
                std::string_view triple = {});

/// TargetMachine + DataLayout + object emit. OS APIs are not handled here.
class TargetEmit {
public:
    explicit TargetEmit(std::string triple);
    ~TargetEmit();

    TargetEmit(const TargetEmit&) = delete;
    TargetEmit& operator=(const TargetEmit&) = delete;
    TargetEmit(TargetEmit&&) noexcept;
    TargetEmit& operator=(TargetEmit&&) noexcept;

    [[nodiscard]] const std::string& triple() const { return triple_; }
    [[nodiscard]] llvm::TargetMachine* machine() const { return tm_.get(); }

    void applyDataLayout(llvm::Module& module) const;
    void emitObject(llvm::Module& module, const std::filesystem::path& path) const;

private:
    std::string triple_;
    std::unique_ptr<llvm::TargetMachine> tm_;
};

/// Parse IR text and emit a native object (convenience wrapper).
void emitIrStringToObject(std::string_view ir, std::string_view triple,
                          const std::filesystem::path& object_path);

void ensureLlvmTargetsInitialized();

}  // namespace xlang
