#pragma once

#include <string>
#include <unordered_set>

namespace xlang {

[[nodiscard]] bool isKnownSyscall(const std::string& name);
void emitSyscallDefinitions(std::string& output, const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedThreadLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedSslLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedServerLink(const std::unordered_set<std::string>& syscalls);
[[nodiscard]] bool syscallsNeedPanicLink(const std::unordered_set<std::string>& syscalls);

}  // namespace xlang
