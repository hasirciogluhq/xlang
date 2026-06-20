#include "xlang/build.h"
#include "xlang/compiler.h"
#include "xlang/error.h"
#include "xlang/input.h"
#include "xlang/module.h"
#include "xlang/test.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

xlang::BuildKind parseBuildKind(const std::string& kind) {
    if (kind == "exe") {
        return xlang::BuildKind::Exe;
    }
    if (kind == "lib") {
        return xlang::BuildKind::Lib;
    }
    throw xlang::XlangError("unknown build kind `" + kind + "` (use exe or lib)");
}

void fillCompileOptions(xlang::CompileOptions& options, const std::string& build_kind,
                        const std::optional<std::filesystem::path>& runtime_override,
                        bool skip_runtime) {
    options.build_kind = parseBuildKind(build_kind);
    options.runtime_override = runtime_override;
    options.skip_runtime = skip_runtime;
    options.clang = xlang::defaultClang();
}

xlang::CompileOptions makeCompileOptions(const std::vector<std::filesystem::path>& inputs,
                                         const std::filesystem::path& output,
                                         const std::string& build_kind, bool emit_ir,
                                         bool keep_ir,
                                         const std::optional<std::filesystem::path>& runtime_override,
                                         bool skip_runtime, const std::string& clang) {
    const xlang::ResolvedBuildInputs resolved = xlang::resolveBuildInputs(inputs);

    xlang::CompileOptions options;
    options.input = resolved.primary;
    options.link_objects = resolved.link_objects;
    options.output = output;
    options.emit_ir = emit_ir;
    options.keep_ir = keep_ir;
    fillCompileOptions(options, build_kind, runtime_override, skip_runtime);
    if (!clang.empty()) {
        options.clang = clang;
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"xlank — xlang compiler (.xlang / .o / .ll -> executable or object)"};
    app.require_subcommand(1);

    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    std::string build_kind = "exe";
    std::optional<std::filesystem::path> runtime_override;
    bool emit_ir = false;
    bool keep_ir = false;
    bool skip_runtime = false;
    std::string clang;

    auto* build = app.add_subcommand("build", "Compile or link input files");
    build->add_option("inputs", inputs,
                      "Primary .xlang/.ll/.o plus optional .o files to link")
        ->required()
        ->expected(-1)
        ->check(CLI::ExistingFile);
    build->add_option("-o,--output", output, "Output path");
    build->add_option("--build", build_kind, "Output kind: exe or lib")
        ->check(CLI::IsMember({"exe", "lib"}));
    build->add_option("--runtime", runtime_override, "Custom runtime .xlang (+ imports)")
        ->check(CLI::ExistingFile);
    build->add_flag("--skip-runtime", skip_runtime, "Do not link runtime");
    build->add_flag("--emit-ir", emit_ir, "Emit LLVM IR only");
    build->add_flag("--keep-ir", keep_ir, "Keep intermediate LLVM IR");
    build->add_option("--clang", clang, "Clang binary path");

    bool keep_artifacts = false;
    std::vector<std::filesystem::path> run_inputs;
    auto* run = app.add_subcommand("run", "Compile and run a .xlang program");
    run->add_option("inputs", run_inputs,
                    "Primary .xlang source plus optional .o files to link")
        ->required()
        ->expected(-1)
        ->check(CLI::ExistingFile);
    run->add_option("--runtime", runtime_override, "Custom runtime .xlang (+ imports)")
        ->check(CLI::ExistingFile);
    run->add_flag("--skip-runtime", skip_runtime, "Do not link runtime");
    run->add_flag("--keep-artifacts", keep_artifacts, "Keep temp build directory");
    run->add_option("--clang", clang, "Clang binary path");

    std::filesystem::path parse_input;
    auto* parse = app.add_subcommand("parse", "Parse and summarize a .xlang file");
    parse->add_option("input", parse_input, "xlang source file")->required()->check(CLI::ExistingFile);

    std::filesystem::path test_root = "test/xlang";
    auto* test_cmd = app.add_subcommand("test", "Run *.test.xlang files (Vitest-style)");
    test_cmd->add_option("path", test_root, "Test root directory");
    test_cmd->add_option("--runtime", runtime_override, "Custom runtime .xlang (+ imports)")
        ->check(CLI::ExistingFile);
    test_cmd->add_flag("--keep-artifacts", keep_artifacts, "Keep temp build directories");
    test_cmd->add_option("--clang", clang, "Clang binary path");

    CLI11_PARSE(app, argc, argv);

    try {
        if (build->parsed()) {
            const xlang::CompileOptions options =
                makeCompileOptions(inputs, output, build_kind, emit_ir, keep_ir, runtime_override,
                                   skip_runtime, clang);

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
            if (resolved.primary_kind != xlang::InputKind::Xlang) {
                throw xlang::XlangError("run requires a .xlang source as the first input");
            }

            xlang::RunOptions options;
            options.input = resolved.primary;
            options.link_objects = resolved.link_objects;
            options.keep_artifacts = keep_artifacts;
            options.runtime_override = runtime_override;
            if (!clang.empty()) {
                options.clang = clang;
            }
            if (skip_runtime) {
                throw xlang::XlangError("run requires runtime (remove --skip-runtime)");
            }

            const xlang::RunResult result = xlang::runFile(options);
            if (result.kept_artifacts) {
                std::cerr << "Artifacts kept at " << result.work_dir << '\n';
            }
            return result.exit_code;
        }

        if (parse->parsed()) {
            const xlang::Program program = xlang::loadProgram(parse_input);

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
