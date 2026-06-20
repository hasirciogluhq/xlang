#pragma once

#include "xlang/build.h"

#include <filesystem>
#include <optional>
#include <string>

namespace xlang {

struct CompileOptions {
    std::filesystem::path input;
    std::vector<std::filesystem::path> link_objects;
    std::filesystem::path output;
    std::filesystem::path ir_output;
    BuildKind build_kind{BuildKind::Exe};
    bool emit_ir{false};
    bool keep_ir{false};
    bool skip_runtime{false};
    std::optional<std::filesystem::path> runtime_override;
    std::filesystem::path work_dir;
    std::string clang{"clang"};
};

struct CompileResult {
    std::filesystem::path executable;
    std::filesystem::path ir_path;
    bool has_ir{false};
};

struct RunOptions {
    std::filesystem::path input;
    std::string clang{"clang"};
    bool keep_artifacts{false};
    std::optional<std::filesystem::path> runtime_override;
};

struct RunResult {
    int exit_code{0};
    std::filesystem::path executable;
    std::filesystem::path work_dir;
    bool kept_artifacts{false};
};

CompileResult compileSource(const std::string& source, const CompileOptions& options);
CompileResult compileFile(const CompileOptions& options);
RunResult runFile(const RunOptions& options);

[[nodiscard]] std::string defaultClang();
[[nodiscard]] std::filesystem::path makeBuildWorkDir();

}  // namespace xlang
