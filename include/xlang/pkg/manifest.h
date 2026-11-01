#pragma once

#include "xlang/pkg/toml.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xlang::pkg {

/// One dependency (or the `[std]` package). Either a git repository (`version`, optional
/// `source`) or a directory on disk (`local`).
struct Dependency {
    std::string path;     ///< import path of the repository root, e.g. `github.com/user/repo`
    std::string version;  ///< a release tag, or `latest`; empty for a local dependency
    std::string source;   ///< git URL or repository directory; empty = `https://<path>.git`
    std::filesystem::path local;  ///< absolute directory of a local dependency; empty for git
    [[nodiscard]] bool isLocal() const { return !local.empty(); }
};

/// Prebuilt code a package ships: linked into executables and shared libraries when `os`/`arch`
/// match the target (empty = any).
struct NativeEntry {
    std::string os;    ///< linux | macos | windows
    std::string arch;  ///< x86_64 | aarch64
    std::vector<std::string> link;  ///< object files and archives, relative to the package root
};

/// `xlang.toml`.
struct Manifest {
    std::filesystem::path file;  ///< empty when there is no manifest (defaults only)
    std::filesystem::path dir;   ///< project root (the manifest's directory)
    std::string name;
    std::vector<Dependency> dependencies;
    std::optional<Dependency> std_package;  ///< `[std]`
    std::vector<NativeEntry> natives;
};

inline constexpr const char* kManifestName = "xlang.toml";
inline constexpr const char* kLockName = "xlang.lock";

/// `host.tld/segment/...`: the first segment has a dot, no segment is empty, `.` or `..`.
[[nodiscard]] bool validImportPath(std::string_view path);
/// True for a module spec such as `github.com/u/r/sub` (a package import, not a file path).
[[nodiscard]] bool looksLikePackageImport(std::string_view spec);

/// Parses and validates a manifest. Throws `PackageError` (E0740 / E0742).
[[nodiscard]] Manifest parseManifest(std::string_view text, const std::filesystem::path& file);
[[nodiscard]] Manifest loadManifest(const std::filesystem::path& file);

/// `xlang.toml` in `start` or the nearest parent directory.
[[nodiscard]] std::optional<std::filesystem::path> findManifest(const std::filesystem::path& start);

/// Splits `host/u/r@v1.2.0` into path and version (`latest` when no `@`). Throws E0740 / E0742.
[[nodiscard]] Dependency parseDependencySpec(std::string_view spec);

/// Text edits that keep the user's comments and layout. `setDependencyText` adds or replaces the
/// `"path" = "version"` line of `[dependencies]`; `removeDependencyText` deletes it (returns
/// false when absent).
[[nodiscard]] std::string setDependencyText(const std::string& manifest, const std::string& path,
                                            const std::string& version);
[[nodiscard]] bool removeDependencyText(std::string& manifest, const std::string& path);
/// Replaces (or appends) the `[std]` table with `package` and `version`.
[[nodiscard]] std::string setStdText(const std::string& manifest, const std::string& package,
                                     const std::string& version);

}  // namespace xlang::pkg
