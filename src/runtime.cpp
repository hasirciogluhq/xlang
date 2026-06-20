#include "xlang/runtime.h"

#include "xlang/codegen.h"
#include "xlang/compiler.h"
#include "xlang/embedded_runtime.h"
#include "xlang/error.h"
#include "xlang/module.h"
#include "xlang/parser.h"
#include "xlang/syscalls.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#ifndef XLANG_RUNTIME_DIR
#define XLANG_RUNTIME_DIR ""
#endif

namespace xlang {

namespace {

int runCommand(const std::string& command) {
    return std::system(command.c_str());
}

std::vector<FunctionSignature> collectExports(const Program& program) {
    std::vector<FunctionSignature> exports;
    for (const Function& function : program.functions) {
        if (!function.exported || function.body.statements.empty()) {
            continue;
        }
        FunctionSignature signature;
        signature.name = function.name;
        signature.params = function.params;
        signature.return_type = function.return_type;
        signature.variadic = function.variadic;
        exports.push_back(std::move(signature));
    }
    return exports;
}

std::vector<FunctionSignature> collectSyscalls(const Program& program) {
    std::vector<FunctionSignature> syscalls;
    for (const Function& function : program.functions) {
        if (!function.syscall) {
            continue;
        }
        FunctionSignature signature;
        signature.name = function.name;
        signature.params = function.params;
        signature.return_type = function.return_type;
        signature.variadic = function.variadic;
        syscalls.push_back(std::move(signature));
    }
    return syscalls;
}

std::vector<StructDecl> collectStructs(const Program& program) {
    return program.structs;
}

bool programNeedsThreadLink(const Program& program) {
    for (const Function& function : program.functions) {
        if (!function.syscall) {
            continue;
        }
        if (function.name == "start_thread" || function.name == "mutex_init" ||
            function.name == "cond_init") {
            return true;
        }
    }
    return false;
}

bool programNeedsSslLink(const Program& program) {
    for (const Function& function : program.functions) {
        if (!function.syscall) {
            continue;
        }
        if (function.name == "net_tls_connect" || function.name == "net_tls_send" ||
            function.name == "net_tls_recv" || function.name == "net_tls_close") {
            return true;
        }
    }
    return false;
}

bool programNeedsServerLink(const Program& program) {
    for (const Function& function : program.functions) {
        if (!function.syscall) {
            continue;
        }
        if (function.name == "net_tcp_connect" || function.name == "net_tcp_listen" ||
            function.name == "net_tcp_accept") {
            return true;
        }
    }
    return false;
}

bool programNeedsPanicLink(const Program& program) {
    for (const Function& function : program.functions) {
        if (!function.syscall) {
            continue;
        }
        if (function.name == "panic" || function.name == "recover" ||
            function.name == "try_invoke0") {
            return true;
        }
    }
    return false;
}

bool programNeedsProcessLink(const Program& program) {
    static const char* kProcessSyscalls[] = {
        "env_get",       "env_set",         "cwd",           "chdir",
        "run_capture",   "capture_stdout",  "proc_fork",     "proc_exec",
        "proc_wait",     "proc_exit",       "proc_kill",     "pipe_create",
        "pipe_read_fd",  "pipe_write_fd",   "fd_close",      "fd_read",
        "fd_write",      "fd_dup2",
    };
    for (const Function& function : program.functions) {
        if (!function.syscall) {
            continue;
        }
        for (const char* name : kProcessSyscalls) {
            if (function.name == name) {
                return true;
            }
        }
    }
    return false;
}

bool programNeedsFileLink(const Program& program) {
    static const char* kFileSyscalls[] = {
        "file_open",        "file_close",       "file_read_path",  "file_write_path",
        "file_exists",      "file_size",        "file_read_handle", "file_write_handle",
    };
    for (const Function& function : program.functions) {
        if (!function.syscall) {
            continue;
        }
        for (const char* name : kFileSyscalls) {
            if (function.name == name) {
                return true;
            }
        }
    }
    return false;
}

bool programNeedsTimeLink(const Program& program) {
    for (const Function& function : program.functions) {
        if (!function.syscall) {
            continue;
        }
        if (function.name == "time_format" || function.name == "now_ms") {
            return true;
        }
    }
    return false;
}

std::filesystem::path runtimeEntryFromSourceTree() {
    if (std::string(XLANG_RUNTIME_DIR).empty()) {
        return {};
    }
    const std::filesystem::path runtime_dir = std::filesystem::path(XLANG_RUNTIME_DIR);
    std::error_code ec;
    if (std::filesystem::is_directory(runtime_dir, ec)) {
        return runtime_dir;
    }
    return runtime_dir / "runtime.xlang";
}

Program loadEmbeddedRuntimeProgram(const RuntimeOptions& options) {
    if (options.override_path) {
        ModuleLoader loader(*options.override_path);
        return loader.load();
    }

    const std::filesystem::path source_entry = runtimeEntryFromSourceTree();
    if (!source_entry.empty() && std::filesystem::exists(source_entry)) {
        ModuleLoader loader(source_entry);
        return loader.load();
    }

    const std::filesystem::path work_dir =
        options.work_dir.empty() ? std::filesystem::temp_directory_path() / "xlang-runtime"
                                 : options.work_dir / "embedded-runtime";
    const std::filesystem::path entry = materializeEmbeddedRuntime(work_dir);
    ModuleLoader loader(entry);
    return loader.load();
}

void compileProgramToObject(const Program& program, const std::string& clang,
                            const std::filesystem::path& work_dir,
                            const std::filesystem::path& object_path, bool skip_runtime) {
    CodegenOptions cg_options;
    cg_options.build_kind = BuildKind::Lib;
    cg_options.link_runtime = false;
    cg_options.target_triple = getClangTargetTriple(clang);

    const CodegenResult generated = Codegen::generate(program, cg_options);
    const std::filesystem::path ir_path = work_dir / (object_path.stem().string() + ".ll");

    {
        std::ofstream out(ir_path);
        if (!out) {
            throw XlangError("failed to write IR: " + ir_path.string());
        }
        out << generated.ir;
    }

    std::ostringstream cmd;
    cmd << clang << " -c \"" << ir_path.string() << "\" -o \"" << object_path.string() << "\"";
    cmd << " -Wno-override-module";
    if (generated.needs_thread_link) {
        cmd << " -pthread";
    }

    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("clang failed to compile object file");
    }

    std::error_code ec;
    std::filesystem::remove(ir_path, ec);
    (void)skip_runtime;
}

}  // namespace

RuntimeBundle loadRuntimeExports(const RuntimeOptions& options) {
    const Program program = loadEmbeddedRuntimeProgram(options);

    RuntimeBundle bundle;
    bundle.exports = collectExports(program);
    bundle.syscalls = collectSyscalls(program);
    bundle.structs = collectStructs(program);
    bundle.needs_thread_link = programNeedsThreadLink(program);
    bundle.needs_ssl_link = programNeedsSslLink(program);
    bundle.needs_server_link = programNeedsServerLink(program);
    bundle.needs_panic_link = programNeedsPanicLink(program);
    bundle.needs_process_link = programNeedsProcessLink(program);
    bundle.needs_file_link = programNeedsFileLink(program);
    bundle.needs_time_link = programNeedsTimeLink(program);
    return bundle;
}

RuntimeBundle ensureRuntime(const RuntimeOptions& options) {
    if (options.work_dir.empty()) {
        throw XlangError("runtime build requires a work directory");
    }

    const Program program = loadEmbeddedRuntimeProgram(options);
    const std::filesystem::path object = options.work_dir / "runtime.o";

    compileProgramToObject(program, options.clang, options.work_dir, object, true);

    RuntimeBundle bundle;
    bundle.object = object;
    bundle.exports = collectExports(program);
    bundle.syscalls = collectSyscalls(program);
    bundle.structs = collectStructs(program);
    bundle.needs_thread_link = programNeedsThreadLink(program);
    bundle.needs_ssl_link = programNeedsSslLink(program);
    bundle.needs_server_link = programNeedsServerLink(program);
    bundle.needs_panic_link = programNeedsPanicLink(program);
    bundle.needs_process_link = programNeedsProcessLink(program);
    bundle.needs_file_link = programNeedsFileLink(program);
    bundle.needs_time_link = programNeedsTimeLink(program);
    return bundle;
}

}  // namespace xlang
