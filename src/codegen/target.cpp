#include "xlang/codegen/target.h"

#include "xlang/error.h"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <format>
#include <mutex>
#include <string>

namespace xlang {
namespace {

std::string_view archToken(std::string_view triple) {
    const auto dash = triple.find('-');
    if (dash == std::string_view::npos) {
        return triple;
    }
    return triple.substr(0, dash);
}

}  // namespace

void ensureLlvmTargetsInitialized() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Host-native targets only — InitializeAll* requires every LLVM backend lib.
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
    });
}

TargetArch archFromTriple(std::string_view triple) {
    const std::string_view arch = archToken(triple);
    if (arch == "x86_64" || arch == "amd64") {
        return TargetArch::X86_64;
    }
    if (arch == "aarch64" || arch == "arm64") {
        return TargetArch::AArch64;
    }
    return TargetArch::Unknown;
}

NativeSyscallAsm nativeSyscallAsm(TargetArch arch, std::size_t arg_count) {
    NativeSyscallAsm info;
    info.max_args = 6;
    if (arg_count > info.max_args) {
        throw XlangError(
            std::format("native syscall supports at most {} arguments", info.max_args));
    }

    switch (arch) {
    case TargetArch::X86_64: {
        // Linux/BSD x86_64: rax=nr, rdi/rsi/rdx/r10/r8/r9=args, ret=rax
        static constexpr const char* kArgRegs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
        info.instruction = "syscall";
        info.constraints = "={rax},{rax}";
        for (std::size_t i = 0; i < arg_count; ++i) {
            info.constraints += ",{";
            info.constraints += kArgRegs[i];
            info.constraints += '}';
        }
        info.constraints += ",~{rcx},~{r11},~{memory}";
        return info;
    }
    case TargetArch::AArch64: {
        // Linux aarch64: x8=nr, x0-x5=args, ret=x0; trap = svc #0
        static constexpr const char* kArgRegs[] = {"x0", "x1", "x2", "x3", "x4", "x5"};
        info.instruction = "svc #0";
        info.constraints = "={x0},{x8}";
        for (std::size_t i = 0; i < arg_count; ++i) {
            info.constraints += ",{";
            info.constraints += kArgRegs[i];
            info.constraints += '}';
        }
        info.constraints += ",~{memory}";
        return info;
    }
    default:
        throw XlangError("native syscall unsupported for target architecture");
    }
}

NativeSyscallAsm nativeSyscallAsmForTriple(std::string_view triple, std::size_t arg_count) {
    return nativeSyscallAsm(archFromTriple(triple), arg_count);
}

TargetEmit::TargetEmit(std::string triple) : triple_(std::move(triple)) {
    ensureLlvmTargetsInitialized();
    if (triple_.empty()) {
        triple_ = llvm::sys::getDefaultTargetTriple();
    }

    const llvm::Triple tt(triple_);
    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(tt, error);
    if (target == nullptr) {
        throw XlangError(std::format("failed to lookup target '{}': {}", triple_, error));
    }

    llvm::TargetOptions opts;
    tm_.reset(target->createTargetMachine(tt, "generic", "", opts, llvm::Reloc::PIC_));
    if (!tm_) {
        throw XlangError(std::format("failed to create TargetMachine for '{}'", triple_));
    }
}

TargetEmit::~TargetEmit() = default;

TargetEmit::TargetEmit(TargetEmit&&) noexcept = default;
TargetEmit& TargetEmit::operator=(TargetEmit&&) noexcept = default;

void TargetEmit::applyDataLayout(llvm::Module& module) const {
    module.setTargetTriple(llvm::Triple(triple_));
    module.setDataLayout(tm_->createDataLayout());
}

void TargetEmit::emitObject(llvm::Module& module, const std::filesystem::path& path) const {
    applyDataLayout(module);

    std::error_code ec;
    llvm::raw_fd_ostream out(path.string(), ec, llvm::sys::fs::OF_None);
    if (ec) {
        throw XlangError(std::format("failed to open object output {}: {}", path.string(),
                                     ec.message()));
    }

    llvm::legacy::PassManager pass;
    if (tm_->addPassesToEmitFile(pass, out, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        throw XlangError("TargetMachine cannot emit object file for this target");
    }
    pass.run(module);
    out.flush();
}

std::unique_ptr<llvm::Module> parseIrString(llvm::LLVMContext& context, std::string_view ir) {
    llvm::SMDiagnostic diagnostic;
    const std::string ir_copy(ir);
    auto module = llvm::parseIR(llvm::MemoryBufferRef(ir_copy, "xlang"), diagnostic, context);
    if (!module) {
        std::string message;
        llvm::raw_string_ostream stream(message);
        diagnostic.print("xlang", stream);
        throw XlangError(std::format("failed to parse LLVM IR:\n{}", message));
    }
    return module;
}

void emitObject(llvm::Module& module, const std::filesystem::path& path, std::string_view triple) {
    std::string triple_str(triple);
    if (triple_str.empty()) {
        if (!module.getTargetTriple().str().empty()) {
            triple_str = module.getTargetTriple().str();
        } else {
            triple_str = llvm::sys::getDefaultTargetTriple();
        }
    }
    TargetEmit emit(triple_str);
    emit.emitObject(module, path);
}

void emitIrStringToObject(std::string_view ir, std::string_view triple,
                          const std::filesystem::path& object_path) {
    llvm::LLVMContext context;
    auto module = parseIrString(context, ir);
    emitObject(*module, object_path, triple);
}

}  // namespace xlang
