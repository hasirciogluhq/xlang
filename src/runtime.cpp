#include "xlang/runtime.h"

#include "xlang/codegen.h"
#include "xlang/compiler.h"
#include "xlang/error.h"
#include "xlang/module.h"
#include "xlang/syscalls.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace xlang {

namespace {

int runCommand(const std::string& command) {
    return std::system(command.c_str());
}

std::filesystem::path runtimeDirectory(const std::filesystem::path& near) {
    std::filesystem::path current = std::filesystem::absolute(near);
    if (std::filesystem::is_regular_file(current)) {
        current = current.parent_path();
    }

    for (int depth = 0; depth < 8; ++depth) {
        const std::filesystem::path candidate = current / "runtime" / "runtime.xlang";
        if (std::filesystem::exists(candidate)) {
            return candidate.parent_path();
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }

    if (const char* root = std::getenv("XLANG_ROOT")) {
        const std::filesystem::path candidate =
            std::filesystem::path(root) / "runtime" / "runtime.xlang";
        if (std::filesystem::exists(candidate)) {
            return candidate.parent_path();
        }
    }

    throw XlangError("runtime not found: expected runtime/runtime.xlang near project or XLANG_ROOT");
}

bool needsRebuild(const std::filesystem::path& source, const std::filesystem::path& object) {
    if (!std::filesystem::exists(object)) {
        return true;
    }
    return std::filesystem::last_write_time(source) > std::filesystem::last_write_time(object);
}

void compileObjectFile(const Program& program, const CompileOptions& options,
                       const std::filesystem::path& object_path) {
    CodegenOptions cg_options;
    cg_options.build_kind = options.build_kind;
    cg_options.link_runtime = false;

    const CodegenResult generated = Codegen::generate(program, cg_options);
    const std::filesystem::path ir_path =
        object_path.parent_path() / (object_path.stem().string() + ".ll");

    {
        std::ofstream out(ir_path);
        if (!out) {
            throw XlangError("failed to write IR: " + ir_path.string());
        }
        out << generated.ir;
    }

    std::ostringstream cmd;
    cmd << options.clang << " -c \"" << ir_path.string() << "\" -o \"" << object_path.string() << "\"";
    if (generated.needs_thread_link) {
        cmd << " -pthread";
    }

    const int status = runCommand(cmd.str());
    if (status != 0) {
        throw XlangError("failed to compile runtime object: " + object_path.string());
    }

    if (!options.keep_ir) {
        std::error_code ec;
        std::filesystem::remove(ir_path, ec);
    }
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

}  // namespace

std::filesystem::path findRuntimeSource(const std::filesystem::path& near) {
    return runtimeDirectory(near) / "runtime.xlang";
}

std::vector<FunctionSignature> loadRuntimeExports(const std::filesystem::path& near) {
    const std::filesystem::path source = findRuntimeSource(near);
    const Program program = loadProgram(source);
    return collectExports(program);
}

RuntimeBundle ensureRuntime(const std::string& clang, bool use_cache) {
    const std::filesystem::path runtime_dir = runtimeDirectory(std::filesystem::current_path());
    const std::filesystem::path source = runtime_dir / "runtime.xlang";
    const std::filesystem::path object = runtime_dir / "runtime.o";

    if (!std::filesystem::exists(source)) {
        throw XlangError("missing runtime source: " + source.string());
    }

    const Program program = loadProgram(source);

    CompileOptions options;
    options.input = source;
    options.clang = clang;
    options.build_kind = BuildKind::Lib;
    options.skip_runtime = true;
    options.keep_ir = false;

    if (!use_cache || needsRebuild(source, object)) {
        compileObjectFile(program, options, object);
    }

    RuntimeBundle bundle;
    bundle.source = source;
    bundle.objects = {object};
    bundle.exports = collectExports(program);
    for (const Function& function : program.functions) {
        if (function.syscall && function.name == "start_thread") {
            bundle.needs_thread_link = true;
            break;
        }
    }
    return bundle;
}

}  // namespace xlang
