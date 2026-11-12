#include "xlang/pkg/semver.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <ranges>
#include <tuple>

namespace xlang::pkg {

bool ReleaseTag::isTest() const {
    return suffix == "test" || suffix.ends_with("-test");
}

namespace {

/// Reads a decimal number from the front of `text` and removes it; nothing is consumed on failure.
[[nodiscard]] std::optional<std::uint64_t> takeNumber(std::string_view& text) {
    std::uint64_t value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end == text.data()) {
        return std::nullopt;
    }
    text.remove_prefix(static_cast<std::size_t>(end - text.data()));
    return value;
}

[[nodiscard]] bool takeDot(std::string_view& text) {
    if (!text.starts_with('.')) {
        return false;
    }
    text.remove_prefix(1);
    return true;
}

[[nodiscard]] constexpr bool suffixChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
}

}  // namespace

std::optional<ReleaseTag> parseReleaseTag(std::string_view tag) {
    ReleaseTag release{.tag = std::string(tag)};
    std::string_view rest = tag;
    if (rest.starts_with('v')) {
        rest.remove_prefix(1);
    }
    const auto major = takeNumber(rest);
    if (!major || !takeDot(rest)) {
        return std::nullopt;
    }
    const auto minor = takeNumber(rest);
    if (!minor || !takeDot(rest)) {
        return std::nullopt;
    }
    const auto patch = takeNumber(rest);
    if (!patch) {
        return std::nullopt;
    }
    release.major = *major;
    release.minor = *minor;
    release.patch = *patch;
    if (rest.empty()) {
        return release;
    }
    if (!rest.starts_with('-') || rest.size() == 1) {
        return std::nullopt;
    }
    rest.remove_prefix(1);
    if (!std::ranges::all_of(rest, suffixChar)) {
        return std::nullopt;
    }
    release.suffix = std::string(rest);
    return release;
}

std::string tagProblem(std::string_view tag) {
    if (parseReleaseTag(tag)) {
        return {};  // pre-release and `-test` tags are valid when named explicitly
    }
    return std::format("`{}` is not a version tag: it must be `vMAJOR.MINOR.PATCH` or `MAJOR.MINOR.PATCH`, "
                       "for example `v1.2.0`",
                       tag);
}

bool releaseLess(const ReleaseTag& a, const ReleaseTag& b) {
    // A stable release sorts after its own pre-releases (1.0.0-rc < 1.0.0); `v1.0.0` and `1.0.0` are
    // the same version, and the `v` spelling is preferred deterministically.
    const auto key = [](const ReleaseTag& r) { return std::tuple{r.major, r.minor, r.patch, r.stable(), r.suffix, r.tag}; };
    return key(a) < key(b);
}

std::optional<ReleaseTag> latestRelease(const std::vector<std::string>& tags) {
    auto stable = tags | std::views::transform([](const std::string& t) { return parseReleaseTag(t); }) |
                  std::views::filter([](const std::optional<ReleaseTag>& r) { return r && r->stable(); });
    std::optional<ReleaseTag> best;
    for (const std::optional<ReleaseTag>& r : stable) {
        if (!best || releaseLess(*best, *r)) {
            best = r;
        }
    }
    return best;
}

}  // namespace xlang::pkg
