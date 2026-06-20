#include "xlang/build.h"
#include "xlang/compiler.h"
#include "xlang/error.h"
#include "xlang/module.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void printUsage() {
    std::cerr
        << "xlank — xlang compiler (source -> LLVM IR -> executable)\n\n"
        << "Usage:\n"
        << "  xlank build <input.xlang> [-o output] [--build=exe|lib] [--emit-ir] [--keep-ir] [--clang path]\n"
        << "  xlank run   <input.xlang> [--keep-artifacts] [--clang path]\n"
        << "  xlank parse <input.xlang>\n";
}

int runBuild(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    xlang::CompileOptions options;
    options.input = std::filesystem::path(argv[2]);
    options.clang = xlang::defaultClang();

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            options.output = std::filesystem::path(argv[++i]);
        } else if (arg.rfind("--build=", 0) == 0) {
            const std::string kind = arg.substr(8);
            if (kind == "exe") {
                options.build_kind = xlang::BuildKind::Exe;
            } else if (kind == "lib") {
                options.build_kind = xlang::BuildKind::Lib;
            } else {
                std::cerr << "unknown build kind: " << kind << " (use exe or lib)\n";
                return 1;
            }
        } else if (arg == "--emit-ir") {
            options.emit_ir = true;
        } else if (arg == "--keep-ir") {
            options.keep_ir = true;
        } else if (arg == "--clang" && i + 1 < argc) {
            options.clang = argv[++i];
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            return 1;
        }
    }

    const xlang::CompileResult result = xlang::compileFile(options);

    if (options.emit_ir) {
        std::cerr << "IR written to " << result.executable << '\n';
    } else {
        std::cerr << "Built " << result.executable << '\n';
        if (result.has_ir) {
            std::cerr << "IR kept at " << result.ir_path << '\n';
        }
    }

    return 0;
}

int runRun(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    xlang::RunOptions options;
    options.input = std::filesystem::path(argv[2]);
    options.clang = xlang::defaultClang();

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--keep-artifacts") {
            options.keep_artifacts = true;
        } else if (arg == "--clang" && i + 1 < argc) {
            options.clang = argv[++i];
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            return 1;
        }
    }

    const xlang::RunResult result = xlang::runFile(options);

    if (result.kept_artifacts) {
        std::cerr << "Artifacts kept at " << result.work_dir << '\n';
    }

    return result.exit_code;
}

int runParse(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    const xlang::Program program = xlang::loadProgram(argv[2]);

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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];

    try {
        if (command == "build") {
            return runBuild(argc, argv);
        }
        if (command == "run") {
            return runRun(argc, argv);
        }
        if (command == "parse") {
            return runParse(argc, argv);
        }

        printUsage();
        return 1;
    } catch (const xlang::XlangError& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
