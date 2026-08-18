#pragma once

#include "xlang/ast.h"
#include "xlang/build.h"
#include "xlang/codegen/builder.h"
#include "xlang/types.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm {
class BasicBlock;
class Function;
class GlobalVariable;
class StructType;
class Type;
class Value;
}  // namespace llvm

namespace xlang {

struct CodegenOptions {
    BuildKind build_kind{BuildKind::Executable};
    std::string target_triple;
    /// If non-empty, emit a native object via TargetMachine (primary artifact).
    std::string object_output;
};

/// Primary product is llvm::Module (+ optional object_path). String .ll is not the artifact.
struct CodegenResult {
    CodegenResult();
    ~CodegenResult();
    CodegenResult(CodegenResult&&) noexcept;
    CodegenResult& operator=(CodegenResult&&) noexcept;
    CodegenResult(const CodegenResult&) = delete;
    CodegenResult& operator=(const CodegenResult&) = delete;

    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    /// Set when TargetMachine object emit ran (`object_output` was set).
    std::string object_path;
    std::unordered_set<std::string> syscalls;

    /// Debug dump of the Module (for --emit-ir). Not the compile product path.
    [[nodiscard]] std::string dumpIr() const;
};

class Codegen {
public:
    [[nodiscard]] static CodegenResult generate(const Program& program,
                                                const CodegenOptions& options = {});

private:
    using LocalMap = std::unordered_map<std::string, llvm::Value*>;

    explicit Codegen(CodegenOptions options);

    void collectSyscalls(const Program& program);
    void emitPrelude(const Program& program);
    void emitStructTypes(const Program& program);
    void emitGlobals(const Program& program);
    void emitGlobalInit(const Program& program);
    void emitFunction(const Function& function);
    void emitDeclareFunction(const FunctionSignature& function);
    void emitDeclareFunction(const Function& function);
    /// `declare syscall <n> name` → real function body with CPU-native trap.
    void emitNativeSyscallFunction(const Function& function);
    void emitArrayHeaderType();
    void preemitStringLiterals(const Program& program);
    void collectStringLiteralsFromExpr(const Expr& expr);
    void collectStringLiteralsFromStmt(const Stmt& stmt);

    [[nodiscard]] bool isStringType(const Type& type) const;
    [[nodiscard]] llvm::Value* emitStringLiteral(const std::string& text);
    void ensureStringLiteralGlobal(const std::string& text);
    [[nodiscard]] llvm::Value* emitIntToString(llvm::Value* int_value);
    [[nodiscard]] llvm::Value* emitStringConcat(llvm::Value* left, llvm::Value* right);

    bool emitStatement(const Stmt& stmt, LocalMap& locals);
    void emitBlock(const Block& block, LocalMap& locals, bool& has_return);
    [[nodiscard]] std::string freshLabel();
    std::pair<Type, llvm::Value*> emitExpr(const Expr& expr, const LocalMap& locals);

    std::pair<Type, llvm::Value*> emitPrintCall(const std::vector<std::unique_ptr<Expr>>& args,
                                                const LocalMap& locals);
    llvm::Value* emitSpawnEntry(const Expr& arg, const LocalMap& locals);

    [[nodiscard]] bool definesFunction(const Program& program, const std::string& name,
                                       const std::vector<Type>& param_types) const;
    [[nodiscard]] std::optional<FunctionSignature> resolveFunctionCall(
        const std::string& name, const std::vector<Type>& arg_types) const;
    [[nodiscard]] const StructDecl* findStruct(const std::string& name) const;
    [[nodiscard]] Type resolveVarType(const std::string& name, const LocalMap& locals) const;
    [[nodiscard]] llvm::Value* resolveVar(const std::string& name, const LocalMap& locals) const;
    [[nodiscard]] llvm::GlobalValue::LinkageTypes fnLinkage(const Function& function) const;
    [[nodiscard]] llvm::GlobalValue::LinkageTypes globalLinkage(const GlobalVar& global) const;
    [[nodiscard]] std::size_t structFieldIndex(const StructDecl& decl,
                                               const std::string& field) const;
    [[nodiscard]] std::size_t structSizeBytes(const StructDecl& decl) const;
    [[nodiscard]] std::size_t typeSizeBytes(const Type& type) const;

    void allocLocal(const std::string& name, const Type& type, LocalMap& locals);
    void storeValue(const Type& type, llvm::Value* value, llvm::Value* ptr);
    std::pair<Type, llvm::Value*> loadValue(const Type& type, llvm::Value* ptr);

    [[nodiscard]] llvm::Type* llvmType(const Type& type);
    [[nodiscard]] llvm::StructType* structBodyType(const std::string& name);
    [[nodiscard]] llvm::Value* coerceInt(llvm::Value* value, const Type& from, const Type& to);
    [[nodiscard]] llvm::Value* asI64(const Type& ty, llvm::Value* value);
    [[nodiscard]] llvm::Value* boolToI1(llvm::Value* value);
    [[nodiscard]] llvm::Constant* zeroOf(const Type& type);
    [[nodiscard]] std::optional<FunctionSignature> resolveMethodCall(
        const std::string& name, const Type& receiver_type,
        const std::vector<Type>& arg_types) const;
    [[nodiscard]] std::string importPrefixedName(const std::string& alias,
                                                 const std::string& method) const;
    [[nodiscard]] static std::string globalName(const std::string& name);

    [[nodiscard]] CommonIrBuilder& b();
    [[nodiscard]] llvm::Function* ensureFn(std::string_view name, llvm::FunctionType* type);

    CodegenOptions options_;
    const Program* program_{nullptr};
    std::unique_ptr<CommonIrBuilder> irb_;

    std::unordered_set<std::string> globals_;
    std::unordered_map<std::string, Type> global_types_;
    std::unordered_map<std::string, llvm::GlobalVariable*> global_vars_;
    std::unordered_set<std::string> defined_functions_;
    std::unordered_set<std::string> syscalls_;
    bool needs_global_init_{false};
    bool needs_heap_{false};
    bool needs_strings_{false};
    bool needs_arrays_{false};
    bool array_hdr_type_emitted_{false};
    std::size_t spawn_thunk_counter_{0};
    std::size_t string_literal_counter_{0};
    std::size_t label_counter_{0};
    std::unordered_map<std::string, llvm::Value*> string_literal_globals_;
    std::unordered_map<std::string, Type> local_types_;
    std::unordered_map<std::string, std::string> import_aliases_;
    std::unordered_map<std::string, llvm::StructType*> struct_types_;
    llvm::StructType* array_hdr_ty_{nullptr};
    llvm::Value* print_nl_{nullptr};
    llvm::Value* print_fmt_s_{nullptr};
    llvm::Value* print_fmt_d_{nullptr};
    llvm::Value* print_fmt_f_{nullptr};
    llvm::Value* int_fmt_{nullptr};
    Type current_return_type_{TypeKind::Int32};
    llvm::Function* current_function_{nullptr};
};

}  // namespace xlang
