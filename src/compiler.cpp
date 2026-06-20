#include "xlang/compiler.h"

#include "xlang/codegen.h"
#include "xlang/error.h"
#include "xlang/module.h"
#include "xlang/parser.h"
#include "xlang/runtime.h"

#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace xlang {

namespace {

int runCommand(const std::string& command) {
    return std::system(command.c_str());
}

int executeProgram(const std::filesystem::path& executable) {
    const pid_t pid = fork();
    if (pid < 0) {
        throw XlangError("failed to fork process");
    }

    if (pid == 0) {
        const std::string path = executable.string();
        execl(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        throw XlangError("failed to wait for process");
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

std::filesystem::path makeTempWorkDir() {
    const auto base = std::filesystem::temp_directory_path();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 0xFFFFFF);

    for (int attempt = 0; attempt < 32; ++attempt) {
        std::ostringstream name;
        name << "xlank-" << std::hex << dist(gen);
        const std::filesystem::path dir = base / name.str();
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
            return dir;
        }
    }

    throw XlangError("failed to create temporary directory");
}

void removeTree(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

std::filesystem::path defaultOutputPath(const CompileOptions& options) {
    const std::filesystem::path parent =
        options.input.has_parent_path() ? options.input.parent_path() : std::filesystem::path(".");
    const std::string stem = options.input.stem().string();
    if (options.build_kind == BuildKind::Lib) {
        return parent / (stem + ".o");
    }
    return parent / stem;
}

CompileResult compileProgram(const Program& program, const CompileOptions& options) {
    CodegenOptions cg_options;
    cg_options.build_kind = options.build_kind;
    cg_options.link_runtime = !options.skip_runtime;

    RuntimeBundle runtime;
    if (!options.skip_runtime) {
        runtime = ensureRuntime(options.clang, true);
        cg_options.runtime_exports = runtime.exports;
    }

    const std::string ir = Codegen::generate(program, cg_options);

    const std::filesystem::path parent =
        options.input.has_parent_path() ? options.input.parent_path() : std::filesystem::path(".");
    const std::string stem = options.input.stem().string();

    const std::filesystem::path ir_path =
        options.ir_output.empty() ? parent / (stem + ".ll") : options.ir_output;

    {
        std::ofstream out(ir_path);
        if (!out) {
            throw XlangError("failed to write IR: " + ir_path.string());
        }
        out << ir;
    }

    CompileResult result;
    result.ir_path = ir_path;
    result.has_ir = true;

    if (options.emit_ir) {
        result.executable = ir_path;
        return result;
    }

    const std::filesystem::path output =
        options.output.empty() ? defaultOutputPath(options) : options.output;

    if (output.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(output.parent_path(), ec);
    }

    const std::filesystem::path object_path =
        options.build_kind == BuildKind::Lib ? output : (output.parent_path() / (stem + ".o"));

    {
        std::ostringstream cmd;
        cmd << options.clang << " -c \"" << ir_path.string() << "\" -o \"" << object_path.string()
            << "\"";
        const int status = runCommand(cmd.str());
        if (status != 0) {
            throw XlangError("clang failed to compile object file");
        }
    }

    if (options.build_kind == BuildKind::Lib) {
        if (!options.keep_ir) {
            std::error_code ec;
            std::filesystem::remove(ir_path, ec);
            result.has_ir = false;
        }
        result.executable = object_path;
        return result;
    }

    {
        std::ostringstream cmd;
        cmd << options.clang << " \"" << object_path.string() << "\"";
        if (!options.skip_runtime) {
            for (const std::filesystem::path& runtime_object : runtime.objects) {
                cmd << " \"" << runtime_object.string() << "\"";
            }
        }
        cmd << " -o \"" << output.string() << "\"";
        const int status = runCommand(cmd.str());
        if (status != 0) {
            throw XlangError("clang failed to link executable");
        }
    }

    if (!options.keep_ir) {
        std::error_code ec;
        std::filesystem::remove(ir_path, ec);
        result.has_ir = false;
    }

    std::error_code ec;
    std::filesystem::remove(object_path, ec);

    result.executable = output;
    return result;
}

}  // namespace

std::string defaultClang() {
    if (const char* env = std::getenv("XLANG_CLANG")) {
        return env;
    }
    return "clang";
}

CompileResult compileSource(const std::string& source, const CompileOptions& options) {
    const Program program = parseSource(source);
    return compileProgram(program, options);
}

CompileResult compileFile(const CompileOptions& options) {
    const Program program = loadProgram(options.input);
    return compileProgram(program, options);
}

RunResult runFile(const RunOptions& options) {
    const std::filesystem::path work_dir = makeTempWorkDir();
    const std::filesystem::path executable = work_dir / "program";

    CompileOptions compile_options;
    compile_options.input = options.input;
    compile_options.output = executable;
    compile_options.ir_output = work_dir / "program.ll";
    compile_options.clang = options.clang;
    compile_options.keep_ir = options.keep_artifacts;
    compile_options.build_kind = BuildKind::Exe;

    RunResult result;
    result.work_dir = work_dir;
    result.kept_artifacts = options.keep_artifacts;

    try {
        const CompileResult compiled = compileFile(compile_options);
        result.executable = compiled.executable;
        result.exit_code = executeProgram(compiled.executable);
    } catch (...) {
        if (!options.keep_artifacts) {
            removeTree(work_dir);
            result.kept_artifacts = false;
        }
        throw;
    }

    if (!options.keep_artifacts) {
        removeTree(work_dir);
        result.kept_artifacts = false;
        result.executable.clear();
        result.work_dir.clear();
    }

    return result;
}

}  // namespace xlang
