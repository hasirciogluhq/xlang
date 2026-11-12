#pragma once

#include "xlang/lang/config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xlang {
class Compilation;
class DiagnosticEngine;
}  // namespace xlang

namespace xlang::cli {

struct PackageOptions {
    std::string action;  ///< install | update | remove | list | clean
    std::vector<std::string> specs;
    bool update{false};      ///< install: re-resolve `latest` entries
    std::string std_spec;    ///< install: `--std=host/u/r[@ver]`, recorded in `[std]`
};

/// `xl install|update|remove|list|clean`: works on the xlang.toml found from the current directory.
int runPackage(const PackageOptions& options);

/// Called by build/check before the root file is loaded: finds xlang.toml from the root file's
/// directory, resolves it offline (lock + `.xlang/cache`), registers the packages and the stdlib
/// source on `compilation`, and reports package problems as diagnostics. `config.std_path` is the
/// `--std` value: a directory, or `host/user/repo[@version]`. Returns the native files to link.
std::vector<std::filesystem::path> attachProject(const std::filesystem::path& root_file, const BuildConfig& config,
                                                 Compilation& compilation, DiagnosticEngine& diagnostics);

}  // namespace xlang::cli
