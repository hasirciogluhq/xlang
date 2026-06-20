#pragma once

#include "xlang/build.h"

#include <filesystem>
#include <vector>

namespace xlang {

enum class InputKind { Xlang, Object, LlvmIr };

struct ResolvedBuildInputs {
    std::filesystem::path primary;
    std::vector<std::filesystem::path> link_objects;
    InputKind primary_kind{InputKind::Xlang};
};

[[nodiscard]] InputKind detectInputKind(const std::filesystem::path& path);
[[nodiscard]] BuildKind defaultBuildKind(InputKind input_kind);
[[nodiscard]] std::filesystem::path defaultOutputPath(const std::filesystem::path& input,
                                                      InputKind input_kind, BuildKind build_kind);
[[nodiscard]] ResolvedBuildInputs resolveBuildInputs(
    const std::vector<std::filesystem::path>& inputs);

}  // namespace xlang
