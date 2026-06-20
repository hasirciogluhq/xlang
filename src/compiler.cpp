#include "xlang/compiler.h"

#include "xlang/codegen.h"
#include "xlang/error.h"
#include "xlang/input.h"
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

void removeTree(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

void ensureParentDir(const std::filesystem::path& path) {
    if (!path.has_parent_path()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
}

void copyFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    ensureParentDir(to);
    std::error_code ec;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        throw XlangError("failed to copy `" + from.string() + "` to `" + to.string() + "`");
    }
}

void compileLlvmIrToObject(const std::string& clang, const std::filesystem::path& ir_path,
                           const std::filesystem::path& object_path, bool needs_pthread) {
    std::ostringstream cmd;
    cmd << clang << " -c \"" << ir_path.string() << "\" -o \"" << object_path.string() << "\"";
    if (needs_pthread) {
        cmd << " -pthread";
    }
    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("clang failed to compile LLVM IR");
    }
}

void linkObjects(const std::string& clang, const std::vector<std::filesystem::path>& objects,
                 const std::filesystem::path& output, bool needs_pthread) {
    ensureParentDir(output);

    std::ostringstream cmd;
    cmd << clang;
    for (const std::filesystem::path& object : objects) {
        cmd << " \"" << object.string() << "\"";
    }
    cmd << " -o \"" << output.string() << "\"";
    if (needs_pthread) {
        cmd << " -pthread";
    }

    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("clang failed to link executable");
    }
}

struct BuildContext {
    CompileOptions options;
    InputKind input_kind;
    std::filesystem::path work_dir;
    bool owns_work_dir{false};
    std::string stem;
};

BuildContext makeBuildContext(CompileOptions options) {
    BuildContext ctx;
    ctx.options = std::move(options);
    ctx.input_kind = detectInputKind(ctx.options.input);
    if (ctx.options.build_kind != BuildKind::Exe && ctx.options.build_kind != BuildKind::Lib) {
        ctx.options.build_kind = defaultBuildKind(ctx.input_kind);
    }

    if (ctx.options.work_dir.empty()) {
        ctx.work_dir = makeBuildWorkDir();
        ctx.owns_work_dir = true;
    } else {
        ctx.work_dir = std::filesystem::absolute(ctx.options.work_dir);
        std::error_code ec;
        std::filesystem::create_directories(ctx.work_dir, ec);
    }

    ctx.stem = ctx.options.input.stem().string();
    return ctx;
}

void cleanupBuildContext(const BuildContext& ctx) {
    if (ctx.owns_work_dir && !ctx.options.keep_ir) {
        removeTree(ctx.work_dir);
    }
}

CompileResult compileXlangProgram(const Program& program, BuildContext& ctx) {
    CodegenOptions cg_options;
    cg_options.build_kind = ctx.options.build_kind;
    cg_options.link_runtime = !ctx.options.skip_runtime && ctx.options.build_kind == BuildKind::Exe;

    RuntimeBundle runtime;
    if (!ctx.options.skip_runtime) {
        RuntimeOptions runtime_options;
        runtime_options.override_path = ctx.options.runtime_override;
        runtime_options.clang = ctx.options.clang;
        runtime_options.work_dir = ctx.work_dir;

        if (ctx.options.build_kind == BuildKind::Exe) {
            runtime = ensureRuntime(runtime_options);
        } else {
            runtime = loadRuntimeExports(runtime_options);
        }
        cg_options.runtime_exports = runtime.exports;
    }

    const CodegenResult generated = Codegen::generate(program, cg_options);

    const std::filesystem::path ir_path =
        ctx.options.ir_output.empty() ? ctx.work_dir / (ctx.stem + ".ll") : ctx.options.ir_output;

    {
        std::ofstream out(ir_path);
        if (!out) {
            throw XlangError("failed to write IR: " + ir_path.string());
        }
        out << generated.ir;
    }

    CompileResult result;
    result.ir_path = ir_path;
    result.has_ir = true;

    if (ctx.options.emit_ir) {
        result.executable = ir_path;
        return result;
    }

    const std::filesystem::path output =
        ctx.options.output.empty()
            ? defaultOutputPath(ctx.options.input, ctx.input_kind, ctx.options.build_kind)
            : ctx.options.output;

    const std::filesystem::path object_path = ctx.work_dir / (ctx.stem + ".o");
    compileLlvmIrToObject(ctx.options.clang, ir_path, object_path, generated.needs_thread_link);

    if (ctx.options.build_kind == BuildKind::Lib) {
        if (object_path != output) {
            copyFile(object_path, output);
        }
        if (!ctx.options.keep_ir) {
            std::error_code ec;
            std::filesystem::remove(ir_path, ec);
            result.has_ir = false;
        }
        result.executable = output;
        return result;
    }

    std::vector<std::filesystem::path> link_inputs = {object_path};
    for (const std::filesystem::path& extra : ctx.options.link_objects) {
        link_inputs.push_back(extra);
    }
    if (!ctx.options.skip_runtime) {
        link_inputs.push_back(runtime.object);
    }
    linkObjects(ctx.options.clang, link_inputs, output,
                generated.needs_thread_link || runtime.needs_thread_link);

    if (!ctx.options.keep_ir) {
        std::error_code ec;
        std::filesystem::remove(ir_path, ec);
        result.has_ir = false;
    }

    result.executable = output;
    return result;
}

CompileResult compileObjectInput(BuildContext& ctx) {
    const std::filesystem::path output =
        ctx.options.output.empty()
            ? defaultOutputPath(ctx.options.input, ctx.input_kind, ctx.options.build_kind)
            : ctx.options.output;

    CompileResult result;

    if (ctx.options.build_kind == BuildKind::Lib) {
        if (ctx.options.input != output) {
            copyFile(ctx.options.input, output);
        }
        result.executable = output;
        return result;
    }

    if (ctx.options.emit_ir) {
        throw XlangError("cannot emit IR from object file input");
    }

    RuntimeBundle runtime;
    if (!ctx.options.skip_runtime) {
        RuntimeOptions runtime_options;
        runtime_options.override_path = ctx.options.runtime_override;
        runtime_options.clang = ctx.options.clang;
        runtime_options.work_dir = ctx.work_dir;
        runtime = ensureRuntime(runtime_options);
    }

    std::vector<std::filesystem::path> link_inputs = {ctx.options.input};
    for (const std::filesystem::path& extra : ctx.options.link_objects) {
        link_inputs.push_back(extra);
    }
    if (!ctx.options.skip_runtime) {
        link_inputs.push_back(runtime.object);
    }
    linkObjects(ctx.options.clang, link_inputs, output, runtime.needs_thread_link);
    result.executable = output;
    return result;
}

CompileResult compileLlvmIrInput(BuildContext& ctx) {
    const std::filesystem::path output =
        ctx.options.output.empty()
            ? defaultOutputPath(ctx.options.input, ctx.input_kind, ctx.options.build_kind)
            : ctx.options.output;

    CompileResult result;
    result.ir_path = ctx.options.input;
    result.has_ir = true;

    if (ctx.options.emit_ir) {
        if (ctx.options.input != output) {
            copyFile(ctx.options.input, output);
            result.ir_path = output;
        }
        result.executable = result.ir_path;
        return result;
    }

    const std::filesystem::path object_path = ctx.work_dir / (ctx.stem + ".o");
    compileLlvmIrToObject(ctx.options.clang, ctx.options.input, object_path, false);

    if (ctx.options.build_kind == BuildKind::Lib) {
        if (object_path != output) {
            copyFile(object_path, output);
        }
        result.executable = output;
        return result;
    }

    RuntimeBundle runtime;
    if (!ctx.options.skip_runtime) {
        RuntimeOptions runtime_options;
        runtime_options.override_path = ctx.options.runtime_override;
        runtime_options.clang = ctx.options.clang;
        runtime_options.work_dir = ctx.work_dir;
        runtime = ensureRuntime(runtime_options);
    }

    std::vector<std::filesystem::path> link_inputs = {object_path};
    for (const std::filesystem::path& extra : ctx.options.link_objects) {
        link_inputs.push_back(extra);
    }
    if (!ctx.options.skip_runtime) {
        link_inputs.push_back(runtime.object);
    }
    linkObjects(ctx.options.clang, link_inputs, output, runtime.needs_thread_link);
    result.executable = output;
    return result;
}

}  // namespace

std::filesystem::path makeBuildWorkDir() {
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

std::string defaultClang() {
    if (const char* env = std::getenv("XLANG_CLANG")) {
        return env;
    }
    return "clang";
}

CompileResult compileSource(const std::string& source, const CompileOptions& options) {
    const Program program = parseSource(source);
    BuildContext ctx = makeBuildContext(options);
    try {
        CompileResult result = compileXlangProgram(program, ctx);
        cleanupBuildContext(ctx);
        return result;
    } catch (...) {
        cleanupBuildContext(ctx);
        throw;
    }
}

CompileResult compileFile(const CompileOptions& options) {
    const ResolvedBuildInputs resolved = resolveBuildInputs(
        [&]() {
            std::vector<std::filesystem::path> all = {options.input};
            all.insert(all.end(), options.link_objects.begin(), options.link_objects.end());
            return all;
        }());

    CompileOptions normalized = options;
    normalized.input = resolved.primary;
    normalized.link_objects = resolved.link_objects;

    BuildContext ctx = makeBuildContext(std::move(normalized));
    try {
        CompileResult result;
        switch (ctx.input_kind) {
            case InputKind::Xlang: {
                const Program program = loadProgram(ctx.options.input);
                result = compileXlangProgram(program, ctx);
                break;
            }
            case InputKind::Object:
                result = compileObjectInput(ctx);
                break;
            case InputKind::LlvmIr:
                result = compileLlvmIrInput(ctx);
                break;
        }
        cleanupBuildContext(ctx);
        return result;
    } catch (...) {
        cleanupBuildContext(ctx);
        throw;
    }
}

RunResult runFile(const RunOptions& options) {
    const std::filesystem::path work_dir = makeBuildWorkDir();
    const std::filesystem::path executable = work_dir / "program";

    CompileOptions compile_options;
    compile_options.input = options.input;
    compile_options.output = executable;
    compile_options.ir_output = work_dir / "program.ll";
    compile_options.clang = options.clang;
    compile_options.keep_ir = options.keep_artifacts;
    compile_options.build_kind = BuildKind::Exe;
    compile_options.work_dir = work_dir;
    compile_options.runtime_override = options.runtime_override;

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
