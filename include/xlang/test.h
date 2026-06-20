#pragma once

#include "xlang/ast.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xlang {

struct TestOptions {
    std::filesystem::path root{"test/xlang"};
    std::optional<std::filesystem::path> runtime_override;
    std::string clang{"clang"};
    bool keep_artifacts{false};
    bool parallel{false};
};

struct TestSuiteResult {
    int files_total{0};
    int files_passed{0};
    int files_failed{0};
    int tests_passed{0};
    int tests_failed{0};
    int exit_code{0};
};

[[nodiscard]] TestSuiteResult runTestSuite(const TestOptions& options);
[[nodiscard]] int runSingleTestFile(const TestOptions& options, const std::filesystem::path& file);
[[nodiscard]] bool isTestFileName(const std::filesystem::path& path);
[[nodiscard]] bool isTestFunctionName(const std::string& name);
[[nodiscard]] std::vector<std::string> collectTestFunctions(const Program& program);
[[nodiscard]] Program withTestHarness(const Program& program, const std::vector<std::string>& tests,
                                      bool parallel, const std::string& file_label);
[[nodiscard]] std::vector<std::filesystem::path> materializeModuleSearchPaths(
    const std::filesystem::path& work_dir, bool skip_runtime);

void rejectTestFileForBuildRun(const std::filesystem::path& path);

}  // namespace xlang
