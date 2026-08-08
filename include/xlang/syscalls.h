#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace xlang {

[[nodiscard]] bool isKnownSyscall(std::string_view name);
void emitSyscallDefinitions(std::string& output, const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedThreadLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedSslLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedServerLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedPanicLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedProcessLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedFileLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedTimeLink(const std::unordered_set<std::string>& syscalls);

} // namespace xlang
