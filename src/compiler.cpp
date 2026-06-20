#include "xlang/compiler.h"

#include "xlang/codegen.h"
#include "xlang/embedded_runtime.h"
#include "xlang/embedded_libs.h"
#include "xlang/error.h"
#include "xlang/input.h"
#include "xlang/module.h"
#include "xlang/parser.h"
#include "xlang/runtime.h"
#include "xlang/test.h"

#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <cstdio>
#include <unordered_map>
#include <sys/wait.h>
#include <unistd.h>

#ifndef XLANG_RUNTIME_DIR
#define XLANG_RUNTIME_DIR ""
#endif

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

#ifndef XLANG_TLS_BRIDGE
#define XLANG_TLS_BRIDGE ""
#endif
#ifndef XLANG_OPENSSL_SSL
#define XLANG_OPENSSL_SSL ""
#endif
#ifndef XLANG_OPENSSL_CRYPTO
#define XLANG_OPENSSL_CRYPTO ""
#endif
#ifndef XLANG_NET_SERVER
#define XLANG_NET_SERVER ""
#endif
#ifndef XLANG_PANIC_BRIDGE
#define XLANG_PANIC_BRIDGE ""
#endif
#ifndef XLANG_PROCESS_BRIDGE
#define XLANG_PROCESS_BRIDGE ""
#endif

void appendLinkFlags(std::ostringstream& cmd, bool needs_pthread, bool needs_ssl, bool needs_server,
                     bool needs_panic, bool needs_process) {
    if (needs_pthread) {
        cmd << " -pthread";
    }
    if (needs_ssl) {
        if (XLANG_TLS_BRIDGE[0] != '\0') {
            cmd << " \"" << XLANG_TLS_BRIDGE << "\"";
        }
        if (XLANG_OPENSSL_SSL[0] != '\0') {
            cmd << " \"" << XLANG_OPENSSL_SSL << "\"";
        }
        if (XLANG_OPENSSL_CRYPTO[0] != '\0') {
            cmd << " \"" << XLANG_OPENSSL_CRYPTO << "\"";
        }
    }
    if (needs_server) {
        if (XLANG_NET_SERVER[0] != '\0') {
            cmd << " \"" << XLANG_NET_SERVER << "\"";
        }
    }
    if (needs_panic) {
        if (XLANG_PANIC_BRIDGE[0] != '\0') {
            cmd << " \"" << XLANG_PANIC_BRIDGE << "\"";
        }
    }
    if (needs_process) {
        if (XLANG_PROCESS_BRIDGE[0] != '\0') {
            cmd << " \"" << XLANG_PROCESS_BRIDGE << "\"";
        }
    }
}

void appendIrCompileFlags(std::ostringstream& cmd) {
    cmd << " -Wno-override-module";
}

void compileLlvmIrToObject(const std::string& clang, const std::filesystem::path& ir_path,
                           const std::filesystem::path& object_path, bool needs_pthread) {
    std::ostringstream cmd;
    cmd << clang << " -c \"" << ir_path.string() << "\" -o \"" << object_path.string() << "\"";
    appendIrCompileFlags(cmd);
    appendLinkFlags(cmd, needs_pthread, false, false, false, false);
    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("clang failed to compile LLVM IR");
    }
}

void linkObjects(const std::string& clang, const std::vector<std::filesystem::path>& objects,
                 const std::filesystem::path& output, bool needs_pthread, bool needs_ssl,
                 bool needs_server, bool needs_panic, bool needs_process) {
    ensureParentDir(output);

    std::ostringstream cmd;
    cmd << clang;
    for (const std::filesystem::path& object : objects) {
        cmd << " \"" << object.string() << "\"";
    }
    cmd << " -o \"" << output.string() << "\"";
    appendLinkFlags(cmd, needs_pthread, needs_ssl, needs_server, needs_panic, needs_process);

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
    cg_options.target_triple = getClangTargetTriple(ctx.options.clang);

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
        cg_options.runtime_structs = runtime.structs;
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
                generated.needs_thread_link || runtime.needs_thread_link,
                generated.needs_ssl_link || runtime.needs_ssl_link,
                generated.needs_server_link || runtime.needs_server_link,
                generated.needs_panic_link || runtime.needs_panic_link,
                generated.needs_process_link || runtime.needs_process_link);

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
    linkObjects(ctx.options.clang, link_inputs, output, runtime.needs_thread_link,
                runtime.needs_ssl_link, runtime.needs_server_link, runtime.needs_panic_link,
                runtime.needs_process_link);
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
    linkObjects(ctx.options.clang, link_inputs, output, runtime.needs_thread_link,
                runtime.needs_ssl_link, runtime.needs_server_link, runtime.needs_panic_link,
                runtime.needs_process_link);
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

std::string getClangTargetTriple(const std::string& clang) {
    static std::unordered_map<std::string, std::string> cache;
    const auto found = cache.find(clang);
    if (found != cache.end()) {
        return found->second;
    }

    const std::string command = clang + " -print-target-triple 2>/dev/null";
    std::string triple;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe != nullptr) {
        char buffer[256];
        if (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            triple = buffer;
            while (!triple.empty() && (triple.back() == '\n' || triple.back() == '\r')) {
                triple.pop_back();
            }
        }
        (void)pclose(pipe);
    }

    if (triple.empty()) {
        triple = "unknown-unknown-unknown";
    }
    cache.emplace(clang, triple);
    return triple;
}

CompileResult compileProgram(const Program& program, const CompileOptions& options) {
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
    rejectTestFileForBuildRun(options.input);

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
                std::vector<std::filesystem::path> module_search_paths;
                if (!ctx.options.skip_runtime) {
                    const std::filesystem::path embedded_libs =
                        ctx.work_dir / "embedded-libs";
                    materializeEmbeddedLibs(embedded_libs);
                    module_search_paths.push_back(embedded_libs);
                    const std::filesystem::path embedded_dir =
                        ctx.work_dir / "embedded-runtime";
                    materializeEmbeddedRuntime(embedded_dir);
                }
#ifndef XLANG_LIBS_DIR
#define XLANG_LIBS_DIR ""
#endif
                if (XLANG_LIBS_DIR[0] != '\0') {
                    module_search_paths.emplace_back(XLANG_LIBS_DIR);
                }
                const Program program =
                    loadProgram(ctx.options.input, module_search_paths);
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
    rejectTestFileForBuildRun(options.input);

    const std::filesystem::path work_dir = makeBuildWorkDir();
    const std::filesystem::path executable = work_dir / "program";

    CompileOptions compile_options;
    compile_options.input = options.input;
    compile_options.link_objects = options.link_objects;
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
