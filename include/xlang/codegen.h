#pragma once

#include "xlang/ast.h"
#include "xlang/build.h"
#include "xlang/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xlang {

struct CodegenOptions {
    BuildKind build_kind{BuildKind::Exe};
    bool link_runtime{true};
    std::vector<FunctionSignature> runtime_exports;
    std::vector<StructDecl> runtime_structs;
};

struct CodegenResult {
    std::string ir;
    std::unordered_set<std::string> syscalls;
    bool needs_thread_link{false};
    bool needs_ssl_link{false};
    bool needs_server_link{false};
};

class Codegen {
public:
    [[nodiscard]] static CodegenResult generate(const Program& program,
                                                const CodegenOptions& options = {});

private:
    explicit Codegen(CodegenOptions options);

    void collectSyscalls(const Program& program);
    void emitPrelude(const Program& program);
    void emitStructTypes(const Program& program);
    void emitRuntimeDeclares(const Program& program);
    void emitGlobals(const Program& program);
    void emitGlobalInit(const Program& program);
    void emitFunction(const Function& function);
    void emitDeclareFunction(const FunctionSignature& function);
    void emitDeclareFunction(const Function& function);
    void emitSyscallLowering();
    void emitStringRuntimeSupport();
    void emitArrayRuntimeSupport();
    void preemitStringLiterals(const Program& program);
    void collectStringLiteralsFromExpr(const Expr& expr);
    void collectStringLiteralsFromStmt(const Stmt& stmt);

    [[nodiscard]] bool isStringType(const Type& type) const;
    [[nodiscard]] std::string emitStringLiteral(const std::string& text);
    void ensureStringLiteralGlobal(const std::string& text);
    [[nodiscard]] std::string stringLiteralGlobalName(const std::string& text) const;
    [[nodiscard]] std::string emitIntToString(const std::string& int_value);
    [[nodiscard]] std::string emitStringConcat(const std::string& left, const std::string& right);

    bool emitStatement(const Stmt& stmt, std::unordered_map<std::string, std::string>& locals);
    void emitBlock(const Block& block, std::unordered_map<std::string, std::string>& locals,
                   bool& has_return);
    [[nodiscard]] std::size_t elementSizeBytes(const Type& type) const;
    [[nodiscard]] std::string freshLabel();
    std::pair<Type, std::string> emitExpr(const Expr& expr,
                                          const std::unordered_map<std::string, std::string>& locals);

    std::pair<Type, std::string> emitPrintCall(const std::vector<std::unique_ptr<Expr>>& args,
                                               const std::unordered_map<std::string, std::string>& locals);
    std::string emitSpawnEntry(const Expr& arg,
                               const std::unordered_map<std::string, std::string>& locals);
    void emitDeferredThunks();

    [[nodiscard]] bool definesFunction(const Program& program, const std::string& name,
                                       const std::vector<Type>& param_types) const;
    [[nodiscard]] std::optional<FunctionSignature> resolveFunctionCall(
        const std::string& name, const std::vector<Type>& arg_types) const;
    [[nodiscard]] const StructDecl* findStruct(const std::string& name) const;
    [[nodiscard]] Type resolveVarType(const std::string& name,
                                      const std::unordered_map<std::string, std::string>& locals) const;
    [[nodiscard]] std::string resolveVar(const std::string& name,
                                         const std::unordered_map<std::string, std::string>& locals) const;
    [[nodiscard]] std::string fnLinkage(const Function& function) const;
    [[nodiscard]] std::string globalLinkage(const GlobalVar& global) const;
    [[nodiscard]] std::string structTypeName(const std::string& name) const;
    [[nodiscard]] std::string structValueTypeName(const std::string& name) const;
    [[nodiscard]] int structFieldIndex(const StructDecl& decl, const std::string& field) const;
    [[nodiscard]] std::size_t structSizeBytes(const StructDecl& decl) const;
    [[nodiscard]] std::size_t typeSizeBytes(const Type& type) const;

    void allocLocal(const std::string& name, const Type& type,
                    std::unordered_map<std::string, std::string>& locals);
    void storeValue(const Type& type, const std::string& value, const std::string& ptr,
                    const std::unordered_map<std::string, std::string>& locals);
    std::pair<Type, std::string> loadValue(const Type& type, const std::string& ptr,
                                           const std::unordered_map<std::string, std::string>& locals);

    std::string freshTmp();
    [[nodiscard]] static std::string localPtr(const std::string& name);
    [[nodiscard]] static std::string globalName(const std::string& name);

    void writeln(const std::string& line);

    [[nodiscard]] std::optional<FunctionSignature> resolveMethodCall(
        const std::string& name, const Type& receiver_type,
        const std::vector<Type>& arg_types) const;
    [[nodiscard]] std::string importPrefixedName(const std::string& alias,
                                                 const std::string& method) const;

    CodegenOptions options_;
    const Program* program_{nullptr};
    std::string output_;
    std::uint32_t tmp_counter_{0};
    std::unordered_set<std::string> globals_;
    std::unordered_map<std::string, Type> global_types_;
    std::unordered_set<std::string> defined_functions_;
    std::unordered_set<std::string> syscalls_;
    bool needs_global_init_{false};
    bool needs_heap_{false};
    bool needs_strings_{false};
    bool needs_arrays_{false};
    bool needs_printf_{false};
    std::uint32_t spawn_thunk_counter_{0};
    std::vector<std::string> spawn_thunks_;
    std::vector<std::string> spawn_cap_globals_;
    std::uint32_t string_literal_counter_{0};
    std::uint32_t label_counter_{0};
    std::unordered_map<std::string, std::string> string_literal_globals_;
    std::unordered_map<std::string, Type> local_types_;
    std::unordered_map<std::string, std::string> import_aliases_;
    Type current_return_type_{TypeKind::Int32};
};

}  // namespace xlang
