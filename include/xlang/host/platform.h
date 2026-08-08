#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xlang::platform {

enum class Os { Linux, Macosx, Windows, Unknown };
enum class Arch { X86, X64, Arm64, Unknown };

[[nodiscard]] Os hostOs();
[[nodiscard]] Arch hostArch();
[[nodiscard]] std::filesystem::path homeDir();
[[nodiscard]] char pathListSeparator();
[[nodiscard]] std::filesystem::path pathJoin(const std::filesystem::path& a,
                                             const std::filesystem::path& b);
[[nodiscard]] std::vector<std::filesystem::path> libDirs();
[[nodiscard]] std::vector<std::string> baselineSyslibFlags(Os os);
[[nodiscard]] std::string exeSuffix(Os os);
[[nodiscard]] std::string staticLibPrefix(Os os);
[[nodiscard]] std::string staticLibSuffix(Os os);
[[nodiscard]] std::string sharedLibSuffix(Os os);
[[nodiscard]] std::string objectSuffix(Os os);
[[nodiscard]] std::string defaultTriple(Os os, Arch arch);

/// Download URL to dest (HTTP(S)). Returns false on failure.
[[nodiscard]] bool download(std::string_view url, const std::filesystem::path& dest);

/// Extract .tar.gz / .tgz / .tar into dest_dir. Returns false on failure.
[[nodiscard]] bool extractTarball(const std::filesystem::path& archive,
                                  const std::filesystem::path& dest_dir);

struct ProcessResult {
    int exit_code{1};
    bool timed_out{false};
};

/// Run argv[0] with argv; wait for completion. Empty env uses current environment.
[[nodiscard]] ProcessResult runProcess(const std::vector<std::string>& argv,
                                       const std::optional<std::filesystem::path>& cwd = std::nullopt);

/// Execute a single program path (no args) and return its exit code.
[[nodiscard]] int executeProgram(const std::filesystem::path& executable);

}  // namespace xlang::platform
