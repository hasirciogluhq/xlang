#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xlang {

// Default directories where xlang looks for static libs (.a / .o bridges).
[[nodiscard]] std::vector<std::filesystem::path> defaultLibSearchPaths();

// Module import roots: frontend xlang packages (src/runtime/frontend) + install trees.
// include_runtime is retained for call-site compatibility; frontend holds both
// auto-linked runtime modules and importable packages (net, http, …).
[[nodiscard]] std::vector<std::filesystem::path> defaultModuleSearchPaths(bool include_runtime);

// Resolve a static/object library by bare name (e.g. "xlang_panic_bridge", "runtime").
[[nodiscard]] std::optional<std::filesystem::path> findLibrary(std::string_view name);

} // namespace xlang
