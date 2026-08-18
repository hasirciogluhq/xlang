#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace xlang {

[[nodiscard]] std::vector<std::filesystem::path> defaultLibSearchPaths();
[[nodiscard]] std::vector<std::filesystem::path> defaultModuleSearchPaths();

}  // namespace xlang
