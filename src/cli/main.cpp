#include "xlang/build.h"
#include "xlang/compiler.h"
#include "xlang/error.h"
#include "xlang/input.h"
#include "xlang/module.h"
#include "xlang/test.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

xlang::BuildKind parseBuildKind(std::string_view kind) {
    if (kind == "executable" || kind == "binary" || kind == "exe") {
        return xlang::BuildKind::Executable;
    }
    if (kind == "static") {
        return xlang::BuildKind::Static;
    }
    if (kind == "shared") {
        return xlang::BuildKind::Shared;
    }
    if (kind == "object" || kind == "lib") {
        // `lib` kept as alias for object for old scripts; prefer --build=object|static.
        return kind == "lib" ? xlang::BuildKind::Object : xlang::BuildKind::Object;
    }
    throw xlang::XlangError(std::format(
        "unknown build kind `{}` (use executable|static|shared|object)", kind));
}

xlang::CompileOptions makeCompileOptions(
    const std::vector<std::filesystem::path>& inputs, const std::filesystem::path& output,
    const std::string& build_kind, bool emit_ir, bool keep_ir,
    const std::optional<std::filesystem::path>& runtime_override,
    const std::optional<std::filesystem::path>& bridge_override, bool no_runtime, bool no_bridge,
    const std::optional<std::string>& target_os, const std::optional<std::string>& target_arch,
    const std::optional<std::string>& target_triple,
    const std::optional<std::string>& runtime_version,
    const std::optional<std::string>& github_repo, const std::string& clang) {
    const xlang::ResolvedBuildInputs resolved = xlang::resolveBuildInputs(inputs);

    xlang::CompileOptions options;
    options.input = resolved.primary;
    options.link_objects = resolved.link_objects;
    options.output = output;
    options.emit_ir = emit_ir;
    options.keep_ir = keep_ir;
    options.build_kind = parseBuildKind(build_kind);
    options.runtime_override = runtime_override;
    options.bridge_override = bridge_override;
    options.skip_runtime = no_runtime;
    options.skip_bridge = no_bridge;
    options.target_os = target_os;
    options.target_arch = target_arch;
    options.target_triple = target_triple;
    options.runtime_version = runtime_version;
    options.github_runtime_repo = github_repo;
    options.clang = clang.empty() ? xlang::defaultClang() : clang;
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"xlang — compiler (.xlang / .o / .ll -> executable|static|shared|object)"};
    app.require_subcommand(1);

    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    std::string build_kind = "executable";
    std::optional<std::filesystem::path> runtime_override;
    std::optional<std::filesystem::path> bridge_override;
    bool emit_ir = false;
    bool keep_ir = false;
    bool no_runtime = false;
    bool no_bridge = false;
    std::optional<std::string> target_os;
    std::optional<std::string> target_arch;
    std::optional<std::string> target_triple;
    std::optional<std::string> runtime_version;
    std::optional<std::string> github_repo;
    std::string clang;

    auto* build = app.add_subcommand("build", "Compile or link input files");
    build->add_option("inputs", inputs,
                      "Primary .xlang/.ll/.o plus optional .o files to link")
        ->required()
        ->expected(-1)
        ->check(CLI::ExistingFile);
    build->add_option("-o,--output", output, "Output path");
    build
        ->add_option("--build", build_kind,
                     "Output kind: executable|static|shared|object (default executable; "
                     "shared=experimental)")
        ->check(CLI::IsMember(
            {"executable", "binary", "exe", "static", "shared", "object", "lib"}));
    build->add_option("--runtime", runtime_override,
                      "Override runtime (.xlang entry or .o/.a artifact)");
    build->add_option("--bridge", bridge_override,
                      "Override bridges (directory of .a/.o or single archive)");
    build->add_flag("--no-runtime", no_runtime,
                    "Do not link/embed xlang runtime (bridges still link unless --no-bridge)");
    build->add_flag("--no-bridge", no_bridge, "Do not link default OS bridges (raw compile)");
    build->add_option("--target", target_os, "Target OS override (linux|macosx|windows)");
    build->add_option("--arch", target_arch, "Target arch override (x86|x64|arm64)");
    build->add_option("--triple", target_triple, "Full target triple override");
    build->add_option("--runtime-version", runtime_version, "Preferred runtime package version");
    build->add_option("--runtime-repo", github_repo, "GitHub owner/repo for runtime fetch");
    build->add_flag("--emit-ir", emit_ir, "Emit LLVM IR only");
    build->add_flag("--keep-ir", keep_ir, "Keep intermediate LLVM IR dump");
    build->add_option("--clang", clang, "Clang binary path");

    bool keep_artifacts = false;
    std::vector<std::filesystem::path> run_inputs;
    auto* run = app.add_subcommand("run", "Compile and run a .xlang program");
    run->add_option("inputs", run_inputs,
                    "Primary .xlang source plus optional .o files to link")
        ->required()
        ->expected(-1)
        ->check(CLI::ExistingFile);
    run->add_option("--runtime", runtime_override, "Override runtime");
    run->add_option("--bridge", bridge_override, "Override bridges");
    run->add_flag("--keep-artifacts", keep_artifacts, "Keep temp build directory");
    run->add_option("--clang", clang, "Clang binary path");

    std::filesystem::path parse_input;
    auto* parse = app.add_subcommand("parse", "Parse and summarize a .xlang file");
    parse->add_option("input", parse_input, "xlang source file")->required()->check(CLI::ExistingFile);

    std::filesystem::path test_root = "test/xlang";
    bool test_parallel = false;
    auto* test_cmd = app.add_subcommand("test", "Run *.test.xlang files (Vitest-style)");
    test_cmd->add_option("path", test_root,
                         "Test file, directory, or path/name pattern (regex)")
        ->default_str("test/xlang");
    test_cmd->add_option("--runtime", runtime_override, "Override runtime");
    test_cmd->add_flag("--parallel", test_parallel, "Run Test* functions in parallel via spawn");
    test_cmd->add_flag("--keep-artifacts", keep_artifacts, "Keep temp build directories");
    test_cmd->add_option("--clang", clang, "Clang binary path");

    CLI11_PARSE(app, argc, argv);

    try {
        if (build->parsed()) {
            for (const std::filesystem::path& input : inputs) {
                if (xlang::isTestFileName(input)) {
                    throw xlang::XlangError(
                        "test files cannot be built; use `xlang test`");
                }
            }

            const xlang::CompileOptions options = makeCompileOptions(
                inputs, output, build_kind, emit_ir, keep_ir, runtime_override, bridge_override,
                no_runtime, no_bridge, target_os, target_arch, target_triple, runtime_version,
                github_repo, clang);

            const xlang::CompileResult result = xlang::compileFile(options);

            if (emit_ir) {
                std::cerr << "IR written to " << result.executable << '\n';
            } else {
                std::cerr << "Built " << result.executable << '\n';
                if (result.has_ir) {
                    std::cerr << "IR kept at " << result.ir_path << '\n';
                }
            }
            return 0;
        }

        if (run->parsed()) {
            const xlang::ResolvedBuildInputs resolved = xlang::resolveBuildInputs(run_inputs);
            if (xlang::isTestFileName(resolved.primary)) {
                throw xlang::XlangError("test files cannot be run; use `xlang test`");
            }
            if (resolved.primary_kind != xlang::InputKind::Xlang) {
                throw xlang::XlangError("run requires a .xlang source as the first input");
            }

            xlang::RunOptions options;
            options.input = resolved.primary;
            options.link_objects = resolved.link_objects;
            options.keep_artifacts = keep_artifacts;
            options.runtime_override = runtime_override;
            options.bridge_override = bridge_override;
            if (!clang.empty()) {
                options.clang = clang;
            }

            const xlang::RunResult result = xlang::runFile(options);
            if (result.kept_artifacts) {
                std::cerr << "Artifacts kept at " << result.work_dir << '\n';
            }
            return result.exit_code;
        }

        if (parse->parsed()) {
            const std::vector<std::filesystem::path> module_search_paths =
                xlang::defaultTestModuleSearchPaths(false);
            const xlang::Program program =
                xlang::loadProgram(parse_input, module_search_paths);

            std::cout << "globals: " << program.globals.size() << '\n';
            for (const xlang::GlobalVar& global : program.globals) {
                std::cout << "  " << global.name
                          << (global.exported ? " export" : "")
                          << (global.external ? " external" : "") << '\n';
            }
            std::cout << "functions: " << program.functions.size() << '\n';
            for (const xlang::Function& fn : program.functions) {
                std::cout << "  fn " << fn.name << " params=" << fn.params.size()
                          << " stmts=" << fn.body.statements.size()
                          << (fn.exported ? " export" : "")
                          << (fn.external ? " external" : "")
                          << (fn.body.statements.empty() ? " declare" : "") << '\n';
            }
            return 0;
        }

        if (test_cmd->parsed()) {
            xlang::TestOptions options;
            options.root = test_root;
            options.runtime_override = runtime_override;
            options.keep_artifacts = keep_artifacts;
            options.parallel = test_parallel;
            if (!clang.empty()) {
                options.clang = clang;
            }
            const xlang::TestSuiteResult result = xlang::runTestSuite(options);
            return result.exit_code;
        }
    } catch (const xlang::XlangError& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }

    return 1;
}
