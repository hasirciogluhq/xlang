#pragma once

#include <filesystem>

namespace xlang {

extern const char kEmbeddedRuntimeSource[];
[[nodiscard]] std::filesystem::path materializeEmbeddedRuntime(
    const std::filesystem::path& work_dir);

}  // namespace xlang
