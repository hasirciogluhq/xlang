#pragma once

#include "xlang/ast.h"
#include "xlang/build.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xlang {

struct CodegenOptions {
    BuildKind build_kind{BuildKind::Exe};
    bool link_runtime{true};
    std::vector<FunctionSignature> runtime_exports;
};

class Codegen {
public:
    [[nodiscard]] static std::string generate(const Program& program,
                                              const CodegenOptions& options = {});

private:
    explicit Codegen(CodegenOptions options);

    void emitPrelude(const Program& program);
    void emitRuntimeDeclares(const Program& program);
    void emitGlobals(const Program& program);
    void emitGlobalInit(const Program& program);
    void emitFunction(const Function& function);
    void emitDeclareFunction(const FunctionSignature& function);
    void emitDeclareFunction(const Function& function);

    bool emitStatement(const Stmt& stmt, std::unordered_map<std::string, std::string>& locals);
    std::pair<std::string, std::string> emitExpr(const Expr& expr,
                                                 const std::unordered_map<std::string, std::string>& locals);

    [[nodiscard]] bool definesFunction(const Program& program, const std::string& name) const;
    [[nodiscard]] std::string resolveVar(const std::string& name,
                                         const std::unordered_map<std::string, std::string>& locals) const;
    [[nodiscard]] std::string fnLinkage(const Function& function) const;
    [[nodiscard]] std::string globalLinkage(const GlobalVar& global) const;

    std::string freshTmp();
    [[nodiscard]] static std::string localPtr(const std::string& name);
    [[nodiscard]] static std::string globalName(const std::string& name);

    void writeln(const std::string& line);

    CodegenOptions options_;
    std::string output_;
    std::uint32_t tmp_counter_{0};
    std::unordered_set<std::string> globals_;
    std::unordered_set<std::string> defined_functions_;
    bool needs_global_init_{false};
};

}  // namespace xlang
