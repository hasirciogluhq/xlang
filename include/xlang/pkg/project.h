#pragma once

#include "xlang/lang/config.h"
#include "xlang/pkg/manifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xlang::pkg {

/// A package-system problem found while resolving; the compiler reports it as an error.
struct Problem {
    std::string code;
    std::string message;
    std::string fix;
};

struct LockEntry {
    std::string path;
    std::string version;
    std::string commit;
    std::string source;
    bool is_std{false};
};

/// `xlang.lock`: the exact version and commit of every git package that was installed.
struct Lock {
    std::vector<LockEntry> entries;

    [[nodiscard]] const LockEntry* find(const std::string& path, bool is_std) const;
    /// A missing file is an empty lock. Throws `PackageError` E0740 when the file is damaged.
    [[nodiscard]] static Lock load(const std::filesystem::path& file);
    [[nodiscard]] std::string text() const;
};

struct ResolvedPackage {
    std::string path;     ///< import path (`github.com/u/r`); `std` for a local std directory
    std::string version;  ///< tag; empty for local packages
    std::string commit;
    std::string source;
    std::filesystem::path dir;
    bool local{false};
    bool is_std{false};
    bool from_root{false};  ///< named by the project manifest (not only by another package)
    std::vector<NativeEntry> natives;
};

struct ResolveOptions {
    bool online{false};  ///< may run `git` (install/update); builds are always offline
    bool update{false};  ///< ignore the lock for `latest` entries
    std::string std_spec;  ///< `--std=host/u/r[@ver]`: replaces the manifest's `[std]`
};

struct Project {
    Manifest manifest;
    std::filesystem::path cache;                 ///< `<root>/.xlang/cache`
    std::vector<ResolvedPackage> packages;       ///< sorted by path, std excluded
    std::optional<ResolvedPackage> std_package;
    std::vector<Problem> problems;
    Lock lock;                                   ///< what `writeLock` would store

    /// Object files and archives the packages ship for `target`. Missing files become problems.
    [[nodiscard]] std::vector<std::filesystem::path> nativeInputs(const TargetInfo& target,
                                                                 std::vector<Problem>& problems) const;
};

/// `<root>/.xlang/cache`
[[nodiscard]] std::filesystem::path cacheDir(const std::filesystem::path& root);

/// Resolves the manifest's packages: offline from lock + cache, or online fetching what is
/// missing. Never throws for per-package failures: they are collected in `problems`.
[[nodiscard]] Project resolveProject(Manifest manifest, const ResolveOptions& options);

/// Writes `xlang.lock` next to the manifest when the content changed.
void writeLock(const Project& project);

}  // namespace xlang::pkg
