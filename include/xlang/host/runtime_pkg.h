#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xlang {

struct RuntimeManifest {
    std::string runtime_version;
    std::vector<std::string> supported_compilers;
};

[[nodiscard]] std::optional<RuntimeManifest> readRuntimeManifest(const std::filesystem::path& root);
[[nodiscard]] bool compilerSupportedByRuntime(const RuntimeManifest& manifest,
                                              std::string_view compiler_version);
[[nodiscard]] std::filesystem::path defaultRuntimeInstallDir();

}  // namespace xlang
