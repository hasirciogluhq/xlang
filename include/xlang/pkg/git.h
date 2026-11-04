#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace xlang::pkg {

struct ProcessResult {
    int status{-1};  ///< exit status; -1 when the process could not start
    std::string out;
    std::string err;
};

/// Runs `argv` (searched in PATH) without a shell; `GIT_TERMINAL_PROMPT=0` is always set so a
/// private repository fails instead of waiting for a password.
[[nodiscard]] ProcessResult runProcess(const std::vector<std::string>& argv, const std::filesystem::path& cwd = {});

struct TagRef {
    std::string name;
    std::string commit;
};

/// `https://<path>.git`, or `source` when given; a directory is turned into a `file://` URL.
[[nodiscard]] std::string gitUrl(const std::string& import_path, const std::string& source);

/// All tags of a repository (`git ls-remote --tags`). Throws `PackageError` E0744.
[[nodiscard]] std::vector<TagRef> listTags(const std::string& url);

/// Downloads the tree at `tag` into `dest` (no `.git`) and returns the commit. `dest` must not
/// exist; it appears atomically. Throws `PackageError` E0744.
[[nodiscard]] std::string fetchTag(const std::string& url, const std::string& tag, const std::filesystem::path& dest);

/// File inside every fetched package that records its commit.
inline constexpr const char* kCommitMarker = ".xlang-commit";

}  // namespace xlang::pkg
