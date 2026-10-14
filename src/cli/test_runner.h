#pragma once

#include <string>
#include <vector>

namespace xlang::cli {

struct TestRunOptions {
    std::vector<std::string> patterns;  ///< file, directory, glob (`*`, `**`, `?`, `[..]`), `re:<regex>` or a substring
    std::vector<std::string> excludes;  ///< same forms; matching files are dropped
    bool list_only{false};
    bool color{true};
    /// Extra arguments forwarded to `xlang build` for every test file (-D, -O, --std, ...).
    std::vector<std::string> build_args;
};

/// `xlang test`: finds `*.test.xl` files, builds each with `self build`, runs it and prints a colored
/// report. Returns the process exit code (0 when every file built and passed).
int runTests(const TestRunOptions& options, const std::string& self);

/// `xlang run`: builds `file` with `self build` into a temporary executable, runs it and returns its
/// exit status (the build's failure status when it did not build).
int runProgram(const std::string& self, const std::string& file, const std::vector<std::string>& build_args);

/// Path of the running executable (used to re-invoke the compiler for each test file).
std::string selfExecutable(const char* argv0);

}  // namespace xlang::cli
