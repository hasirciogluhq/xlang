#include "xlang/compiler.h"

#include "xlang/codegen.h"
#include "xlang/error.h"
#include "xlang/host/embed.h"
#include "xlang/host/layout.h"
#include "xlang/host/platform.h"
#include "xlang/host/resolve.h"
#include "xlang/input.h"
#include "xlang/module.h"
#include "xlang/parser.h"
#include "xlang/runtime.h"
#include "xlang/test.h"
#include "xlang/util.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

namespace xlang {

namespace {

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
        throw XlangError(std::format("failed to copy `{}` to `{}`", from.string(), to.string()));
    }
}

void appendBaselineSyslibs(std::ostringstream& cmd, platform::Os os) {
    for (const std::string& flag : platform::baselineSyslibFlags(os)) {
        cmd << ' ' << flag;
    }
}

void appendOpenSslLinkFlags(std::ostringstream& cmd) {
    // Homebrew / common prefixes (clang on macOS does not search these by default).
    static constexpr const char* kLibDirs[] = {
        "/opt/homebrew/opt/openssl@3/lib",
        "/opt/homebrew/opt/openssl/lib",
        "/usr/local/opt/openssl@3/lib",
        "/usr/local/opt/openssl/lib",
    };
    for (const char* dir : kLibDirs) {
        if (std::filesystem::exists(dir)) {
            cmd << " -L\"" << dir << '"';
            break;
        }
    }
    if (const char* root = std::getenv("OPENSSL_ROOT_DIR"); root != nullptr && root[0] != '\0') {
        cmd << " -L\"" << root << "/lib\"";
    }
    cmd << " -lssl -lcrypto";
}

void appendBridgeLibs(std::ostringstream& cmd, const std::vector<std::filesystem::path>& bridges) {
    for (const auto& path : bridges) {
        cmd << " \"" << path.string() << '"';
    }
    // OpenSSL for tls bridge when present (static embed prefers .a; syslibs for openssl).
    bool has_tls = false;
    for (const auto& path : bridges) {
        const std::string name = path.filename().string();
        if (name.find("tls") != std::string::npos) {
            has_tls = true;
            break;
        }
    }
    if (has_tls) {
        appendOpenSslLinkFlags(cmd);
    }
}

void linkExecutable(const std::string& clang, const std::vector<std::filesystem::path>& objects,
                    const std::filesystem::path& output, platform::Os os,
                    const std::vector<std::filesystem::path>& bridges) {
    ensureParentDir(output);

    std::ostringstream cmd;
    cmd << clang;
    for (const std::filesystem::path& object : objects) {
        cmd << " \"" << object.string() << '"';
    }
    appendBridgeLibs(cmd, bridges);
    cmd << " -o \"" << output.string() << '"';
    appendBaselineSyslibs(cmd, os);

    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("clang failed to link executable");
    }
}

void linkShared(const std::string& clang, const std::vector<std::filesystem::path>& objects,
                const std::filesystem::path& output, platform::Os os,
                const std::vector<std::filesystem::path>& bridges) {
    ensureParentDir(output);
    std::ostringstream cmd;
    cmd << clang << " -shared";
    for (const std::filesystem::path& object : objects) {
        cmd << " \"" << object.string() << '"';
    }
    appendBridgeLibs(cmd, bridges);
    cmd << " -o \"" << output.string() << '"';
    appendBaselineSyslibs(cmd, os);
    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("clang failed to link shared library (experimental)");
    }
}

void archiveStatic(const std::vector<std::filesystem::path>& objects,
                   const std::filesystem::path& output) {
    ensureParentDir(output);
    std::ostringstream cmd;
    cmd << "ar rcs \"" << output.string() << '"';
    for (const auto& object : objects) {
        cmd << " \"" << object.string() << '"';
    }
    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("ar failed to create static library");
    }
}

void compileLlvmIrFileToObject(const std::string& clang, const std::filesystem::path& ir_path,
                               const std::filesystem::path& object_path) {
    std::ostringstream cmd;
    cmd << clang << " -c \"" << ir_path.string() << "\" -o \"" << object_path.string()
        << "\" -Wno-override-module";
    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("clang failed to compile LLVM IR");
    }
}

struct BuildContext {
    CompileOptions options;
    InputKind input_kind;
    std::filesystem::path work_dir;
    bool owns_work_dir{false};
    std::string stem;
    TargetSpec target;
};

BuildContext makeBuildContext(CompileOptions options) {
    BuildContext ctx;
    ctx.options = std::move(options);
    ctx.input_kind = detectInputKind(ctx.options.input);
    ctx.target = resolveTarget(ctx.options.target_os, ctx.options.target_arch,
                               ctx.options.target_triple);

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

std::string resolveTriple(const BuildContext& ctx) {
    // Explicit --triple / XLANG_TARGET_TRIPLE.
    if (ctx.options.target_triple && !ctx.options.target_triple->empty()) {
        return *ctx.options.target_triple;
    }
    // Host build (no OS/arch override): prefer clang's versioned Darwin triple so
    // Mach-O objects get LC_BUILD_VERSION (avoids ld "no platform load command").
    if (!ctx.options.target_os && !ctx.options.target_arch) {
        if (const char* env = std::getenv("XLANG_TARGET_TRIPLE");
            env != nullptr && env[0] != '\0') {
            return env;
        }
        const std::string clang_triple = getClangTargetTriple(ctx.options.clang);
        if (!clang_triple.empty() && clang_triple.find("unknown") == std::string::npos) {
            return clang_triple;
        }
    }
    if (!ctx.target.triple.empty() &&
        ctx.target.triple.find("unknown-unknown") == std::string::npos) {
        return ctx.target.triple;
    }
    return getClangTargetTriple(ctx.options.clang);
}

std::vector<std::filesystem::path> collectBridges(const BuildContext& ctx) {
    return resolveBridgeArtifacts(ctx.options.bridge_override, ctx.options.skip_bridge,
                                  ctx.work_dir);
}

CompileResult compileXlangProgram(const Program& program, BuildContext& ctx) {
    CodegenOptions cg_options;
    cg_options.build_kind = ctx.options.build_kind;
    cg_options.link_runtime =
        !ctx.options.skip_runtime && ctx.options.build_kind == BuildKind::Executable;
    cg_options.target_triple = resolveTriple(ctx);

    RuntimeBundle runtime;
    if (!ctx.options.skip_runtime) {
        RuntimeOptions runtime_options;
        runtime_options.override_path = ctx.options.runtime_override;
        runtime_options.clang = ctx.options.clang;
        runtime_options.work_dir = ctx.work_dir;
        runtime_options.runtime_version = ctx.options.runtime_version;
        runtime_options.github_repo = ctx.options.github_runtime_repo;

        if (linksFinalImage(ctx.options.build_kind)) {
            runtime = ensureRuntime(runtime_options);
        } else {
            runtime = loadRuntimeExports(runtime_options);
        }
        cg_options.runtime_exports = runtime.exports;
        cg_options.runtime_syscalls = runtime.syscalls;
        cg_options.runtime_structs = runtime.structs;
    }

    const std::filesystem::path object_path = ctx.work_dir / (ctx.stem + ".o");
    if (!ctx.options.emit_ir) {
        cg_options.object_output = object_path.string();
    }

    CodegenResult generated = Codegen::generate(program, cg_options);

    CompileResult result;

    if (ctx.options.emit_ir || ctx.options.keep_ir || !ctx.options.ir_output.empty()) {
        const std::filesystem::path ir_path =
            ctx.options.ir_output.empty() ? ctx.work_dir / (ctx.stem + ".ll")
                                          : ctx.options.ir_output;
        {
            std::ofstream out(ir_path);
            if (!out) {
                throw XlangError(std::format("failed to write IR: {}", ir_path.string()));
            }
            out << generated.dumpIr();
        }
        result.ir_path = ir_path;
        result.has_ir = true;
        if (ctx.options.emit_ir) {
            result.executable = ir_path;
            return result;
        }
    }

    const std::filesystem::path output =
        ctx.options.output.empty()
            ? defaultOutputPath(ctx.options.input, ctx.input_kind, ctx.options.build_kind)
            : ctx.options.output;

    if (generated.object_path.empty()) {
        throw XlangError("codegen did not emit an object file");
    }

    if (ctx.options.build_kind == BuildKind::Object) {
        if (object_path != output) {
            copyFile(object_path, output);
        }
        result.executable = output;
        return result;
    }

    std::vector<std::filesystem::path> link_inputs = {object_path};
    for (const std::filesystem::path& extra : ctx.options.link_objects) {
        link_inputs.push_back(extra);
    }

    if (!ctx.options.skip_runtime && !runtime.object.empty()) {
        link_inputs.push_back(runtime.object);
    } else if (!ctx.options.skip_runtime) {
        if (const auto art =
                resolveRuntimeArtifact(ctx.options.runtime_override, false, ctx.work_dir)) {
            link_inputs.push_back(*art);
        }
    }

    const auto bridges = collectBridges(ctx);

    if (ctx.options.build_kind == BuildKind::Static) {
        for (const auto& b : bridges) {
            link_inputs.push_back(b);
        }
        archiveStatic(link_inputs, output);
        result.executable = output;
        return result;
    }

    if (ctx.options.build_kind == BuildKind::Shared) {
        linkShared(ctx.options.clang, link_inputs, output, ctx.target.os, bridges);
        result.executable = output;
        return result;
    }

    // Executable (default)
    linkExecutable(ctx.options.clang, link_inputs, output, ctx.target.os, bridges);

    if (!ctx.options.keep_ir && result.has_ir) {
        std::error_code ec;
        std::filesystem::remove(result.ir_path, ec);
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

    if (ctx.options.build_kind == BuildKind::Object) {
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
    if (!ctx.options.skip_runtime && linksFinalImage(ctx.options.build_kind)) {
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
    if (!runtime.object.empty()) {
        link_inputs.push_back(runtime.object);
    }

    const auto bridges = collectBridges(ctx);

    if (ctx.options.build_kind == BuildKind::Static) {
        for (const auto& b : bridges) {
            link_inputs.push_back(b);
        }
        archiveStatic(link_inputs, output);
    } else if (ctx.options.build_kind == BuildKind::Shared) {
        linkShared(ctx.options.clang, link_inputs, output, ctx.target.os, bridges);
    } else {
        linkExecutable(ctx.options.clang, link_inputs, output, ctx.target.os, bridges);
    }
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
    compileLlvmIrFileToObject(ctx.options.clang, ctx.options.input, object_path);

    if (ctx.options.build_kind == BuildKind::Object) {
        if (object_path != output) {
            copyFile(object_path, output);
        }
        result.executable = output;
        return result;
    }

    RuntimeBundle runtime;
    if (!ctx.options.skip_runtime && linksFinalImage(ctx.options.build_kind)) {
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
    if (!runtime.object.empty()) {
        link_inputs.push_back(runtime.object);
    }

    const auto bridges = collectBridges(ctx);
    if (ctx.options.build_kind == BuildKind::Static) {
        for (const auto& b : bridges) {
            link_inputs.push_back(b);
        }
        archiveStatic(link_inputs, output);
    } else if (ctx.options.build_kind == BuildKind::Shared) {
        linkShared(ctx.options.clang, link_inputs, output, ctx.target.os, bridges);
    } else {
        linkExecutable(ctx.options.clang, link_inputs, output, ctx.target.os, bridges);
    }
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
        name << "xlang-" << std::hex << dist(gen);
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
                const std::vector<std::filesystem::path> module_search_paths =
                    defaultModuleSearchPaths(!ctx.options.skip_runtime);
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
    compile_options.build_kind = BuildKind::Executable;
    compile_options.work_dir = work_dir;
    compile_options.runtime_override = options.runtime_override;
    compile_options.bridge_override = options.bridge_override;

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
    }
    return result;
}

}  // namespace xlang
