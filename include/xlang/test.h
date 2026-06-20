#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace xlang {

struct TestOptions {
    std::filesystem::path root{"test/xlang"};
    std::optional<std::filesystem::path> runtime_override;
    std::string clang{"clang"};
    bool keep_artifacts{false};
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
[[nodiscard]] bool isTestFileName(const std::filesystem::path& path);

}  // namespace xlang
