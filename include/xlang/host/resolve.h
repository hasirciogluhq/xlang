#pragma once

#include "xlang/host/platform.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xlang {

struct TargetSpec {
    platform::Os os{platform::Os::Unknown};
    platform::Arch arch{platform::Arch::Unknown};
    std::string triple;
};

[[nodiscard]] TargetSpec resolveTarget(std::optional<std::string> os_override,
                                       std::optional<std::string> arch_override,
                                       std::optional<std::string> triple_override);

[[nodiscard]] platform::Os parseOs(std::string_view name);
[[nodiscard]] platform::Arch parseArch(std::string_view name);
[[nodiscard]] std::string osName(platform::Os os);
[[nodiscard]] std::string archName(platform::Arch arch);

}  // namespace xlang
