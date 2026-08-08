#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace xlang {

[[nodiscard]] int runCommand(std::string_view command);

template <typename... Names>
[[nodiscard]] bool setContainsAny(const std::unordered_set<std::string>& set, Names... names) {
    // const char* / string convert to temporary std::string for Key lookup
    return (set.contains(names) || ...);
}

[[nodiscard]] inline bool setContains(const std::unordered_set<std::string>& set,
                                      std::string_view name) {
    return set.contains(std::string(name));
}

} // namespace xlang
