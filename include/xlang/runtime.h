#pragma once

#include "xlang/ast.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xlang {

struct RuntimeBundle {
    std::filesystem::path source;
    std::vector<std::filesystem::path> objects;
    std::vector<FunctionSignature> exports;
    bool needs_thread_link{false};
};

[[nodiscard]] std::filesystem::path findRuntimeSource(const std::filesystem::path& near);
[[nodiscard]] std::vector<FunctionSignature> loadRuntimeExports(const std::filesystem::path& near);
[[nodiscard]] RuntimeBundle ensureRuntime(const std::string& clang, bool use_cache = true);

}  // namespace xlang
