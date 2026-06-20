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
    std::vector<StructDecl> structs;
    bool needs_thread_link{false};
    bool needs_ssl_link{false};
    bool needs_server_link{false};
    bool needs_panic_link{false};
};

struct RuntimeOptions {
    std::optional<std::filesystem::path> override_path;
    std::string clang{"clang"};
    std::filesystem::path work_dir;
};

[[nodiscard]] RuntimeBundle loadRuntimeExports(const RuntimeOptions& options);
[[nodiscard]] RuntimeBundle ensureRuntime(const RuntimeOptions& options);

}  // namespace xlang
