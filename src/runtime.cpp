#include "xlang/runtime.h"

#include "xlang/codegen.h"
#include "xlang/compiler.h"
#include "xlang/error.h"
#include "xlang/module.h"
#include "xlang/parser.h"
#include "xlang/paths.h"
#include "xlang/syscalls.h"
#include "xlang/util.h"

#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace xlang {

namespace {

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

std::unordered_set<std::string> collectSyscallNames(const Program& program) {
    std::unordered_set<std::string> names;
    for (const Function& function : program.functions) {
        if (function.syscall) {
            names.insert(function.name);
        }
    }
    return names;
}

RuntimeBundle fillBundleFromProgram(const Program& program) {
    const std::unordered_set<std::string> syscall_names = collectSyscallNames(program);

    RuntimeBundle bundle;
    bundle.exports = collectExports(program);
    bundle.syscalls = collectSyscalls(program);
    bundle.structs = collectStructs(program);
    bundle.needs_thread_link = syscallsNeedThreadLink(syscall_names);
    bundle.needs_ssl_link = syscallsNeedSslLink(syscall_names);
    bundle.needs_server_link = syscallsNeedServerLink(syscall_names);
    bundle.needs_panic_link = syscallsNeedPanicLink(syscall_names);
    bundle.needs_process_link = syscallsNeedProcessLink(syscall_names);
    bundle.needs_file_link = syscallsNeedFileLink(syscall_names);
    bundle.needs_time_link = syscallsNeedTimeLink(syscall_names);
    return bundle;
}

std::filesystem::path findRuntimeEntry() {
    for (const std::filesystem::path& root : defaultModuleSearchPaths(true)) {
        std::error_code ec;
        const std::filesystem::path as_dir = root;
        if (std::filesystem::is_directory(as_dir, ec)) {
            const std::filesystem::path package = as_dir / "runtime.xlang";
            if (std::filesystem::is_regular_file(package, ec)) {
                return as_dir;
            }
            if (as_dir.filename() == "runtime") {
                return as_dir;
            }
        }
        const std::filesystem::path as_file = root / "runtime.xlang";
        if (std::filesystem::is_regular_file(as_file, ec)) {
            return as_file;
        }
    }
    return {};
}

Program loadRuntimeProgram(const RuntimeOptions& options) {
    if (options.override_path) {
        ModuleLoader loader(*options.override_path);
        return loader.load();
    }

    const std::filesystem::path entry = findRuntimeEntry();
    if (entry.empty()) {
        throw XlangError(
            "runtime not found (set --runtime, or place runtime under lib search paths)");
    }
    ModuleLoader loader(entry);
    return loader.load();
}

void compileProgramToObject(const Program& program, const std::string& clang,
                            const std::filesystem::path& work_dir,
                            const std::filesystem::path& object_path) {
    CodegenOptions cg_options;
    cg_options.build_kind = BuildKind::Lib;
    cg_options.link_runtime = false;
    cg_options.target_triple = getClangTargetTriple(clang);

    const CodegenResult generated = Codegen::generate(program, cg_options);
    const std::filesystem::path ir_path = work_dir / (object_path.stem().string() + ".ll");

    {
        std::ofstream out(ir_path);
        if (!out) {
            throw XlangError(std::format("failed to write IR: {}", ir_path.string()));
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
}

}  // namespace

RuntimeBundle loadRuntimeExports(const RuntimeOptions& options) {
    return fillBundleFromProgram(loadRuntimeProgram(options));
}

RuntimeBundle ensureRuntime(const RuntimeOptions& options) {
    if (options.work_dir.empty()) {
        throw XlangError("runtime build requires a work directory");
    }

    if (const auto prebuilt = findLibrary("runtime")) {
        RuntimeBundle bundle;
        if (options.override_path || !findRuntimeEntry().empty()) {
            try {
                bundle = fillBundleFromProgram(loadRuntimeProgram(options));
            } catch (const XlangError&) {
                // Prebuilt object is enough to link; exports may be incomplete.
            }
        }
        bundle.object = *prebuilt;
        return bundle;
    }

    const Program program = loadRuntimeProgram(options);
    const std::filesystem::path object = options.work_dir / "runtime.o";
    compileProgramToObject(program, options.clang, options.work_dir, object);

    RuntimeBundle bundle = fillBundleFromProgram(program);
    bundle.object = object;
    return bundle;
}

}  // namespace xlang
