#pragma once

#include <filesystem>

namespace xlang {

[[nodiscard]] std::filesystem::path materializeEmbeddedLibs(const std::filesystem::path& work_dir);

}  // namespace xlang
