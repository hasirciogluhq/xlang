#include "xlang/runtime.h"

#include "xlang/codegen.h"
#include "xlang/embedded_runtime.h"
#include "xlang/error.h"
#include "xlang/module.h"
#include "xlang/parser.h"
#include "xlang/syscalls.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

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
        exports.push_back(std::move(signature));
    }
    return exports;
}

bool programNeedsThreadLink(const Program& program) {
    for (const Function& function : program.functions) {
        if (function.syscall && function.name == "start_thread") {
            return true;
        }
    }
    return false;
}

Program loadEmbeddedRuntimeProgram() {
    return parseSource(kEmbeddedRuntimeSource);
}

Program loadRuntimeProgram(const RuntimeOptions& options) {
    if (options.override_path) {
        ModuleLoader loader(*options.override_path);
        return loader.load();
    }
    return loadEmbeddedRuntimeProgram();
}

void compileProgramToObject(const Program& program, const std::string& clang,
                            const std::filesystem::path& work_dir,
                            const std::filesystem::path& object_path, bool skip_runtime) {
    CodegenOptions cg_options;
    cg_options.build_kind = BuildKind::Lib;
    cg_options.link_runtime = false;

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
    const Program program = loadRuntimeProgram(options);

    RuntimeBundle bundle;
    bundle.exports = collectExports(program);
    bundle.needs_thread_link = programNeedsThreadLink(program);
    return bundle;
}

RuntimeBundle ensureRuntime(const RuntimeOptions& options) {
    if (options.work_dir.empty()) {
        throw XlangError("runtime build requires a work directory");
    }

    const Program program = loadRuntimeProgram(options);
    const std::filesystem::path object = options.work_dir / "runtime.o";

    compileProgramToObject(program, options.clang, options.work_dir, object, true);

    RuntimeBundle bundle;
    bundle.object = object;
    bundle.exports = collectExports(program);
    bundle.needs_thread_link = programNeedsThreadLink(program);
    return bundle;
}

}  // namespace xlang
