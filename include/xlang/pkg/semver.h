#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xlang::pkg {

/// A release tag: `v1.2.3` or `1.2.3`, optionally with a `-suffix` (`v1.2.3-rc.1`).
struct ReleaseTag {
    std::string tag;  ///< as written in the repository
    std::uint64_t major{0};
    std::uint64_t minor{0};
    std::uint64_t patch{0};
    std::string suffix;  ///< text after the first `-`, without it; empty for a stable release

    [[nodiscard]] bool stable() const { return suffix.empty(); }
    /// A tag whose suffix ends in `test` (`v1.2.3-test`, `v1.2.3-rc-test`): never chosen as latest.
    [[nodiscard]] bool isTest() const;
};

/// Parses `v?MAJOR.MINOR.PATCH(-suffix)?`. Anything else is not a version.
[[nodiscard]] std::optional<ReleaseTag> parseReleaseTag(std::string_view tag);

/// Why `tag` cannot be installed when named explicitly; empty when it can. Only the format is
/// checked: pre-release and `-test` tags are installable when named.
[[nodiscard]] std::string tagProblem(std::string_view tag);

/// Ordering by (major, minor, patch); a stable release sorts after its own pre-releases.
[[nodiscard]] bool releaseLess(const ReleaseTag& a, const ReleaseTag& b);

/// The highest stable release among `tags`: tags that are not versions, pre-releases and
/// `-test` tags are ignored. Empty when there is none.
[[nodiscard]] std::optional<ReleaseTag> latestRelease(const std::vector<std::string>& tags);

}  // namespace xlang::pkg
