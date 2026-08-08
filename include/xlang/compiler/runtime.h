#pragma once

#include "xlang/ast.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xlang {

struct RuntimeBundle {
    std::filesystem::path object;
    std::vector<FunctionSignature> exports;
    std::vector<FunctionSignature> syscalls;
    std::vector<StructDecl> structs;
};

struct RuntimeOptions {
    std::optional<std::filesystem::path> override_path;
    std::string clang{"clang"};
    std::filesystem::path work_dir;
    std::optional<std::string> runtime_version;
    std::optional<std::string> github_repo;  // owner/repo
};

[[nodiscard]] RuntimeBundle loadRuntimeExports(const RuntimeOptions& options);
[[nodiscard]] RuntimeBundle ensureRuntime(const RuntimeOptions& options);

}  // namespace xlang
