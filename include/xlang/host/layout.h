#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace xlang {

[[nodiscard]] std::vector<std::filesystem::path> defaultLibSearchPaths();
[[nodiscard]] std::vector<std::filesystem::path> defaultModuleSearchPaths(bool include_runtime);
[[nodiscard]] std::optional<std::filesystem::path> findLibrary(std::string_view name);

}  // namespace xlang
