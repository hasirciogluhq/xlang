#include "xlang/runtime.h"

#include "xlang/codegen.h"
#include "xlang/compiler.h"
#include "xlang/error.h"
#include "xlang/host/embed.h"
#include "xlang/host/fetch.h"
#include "xlang/host/layout.h"
#include "xlang/host/runtime_pkg.h"
#include "xlang/module.h"
#include "xlang/parser.h"
#include "xlang/util.h"

#include <format>
#include <fstream>
#include <string>

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
    // RuntimeBundle.syscalls: plain `declare name(...)` bridge/C ABI symbols.
    // `declare syscall <n> name` is CPU-native and is not listed here.
    std::vector<FunctionSignature> declares;
    for (const Function& function : program.functions) {
        if (!function.external || function.syscall) {
            continue;
        }
        FunctionSignature signature;
        signature.name = function.name;
        signature.params = function.params;
        signature.return_type = function.return_type;
        signature.variadic = function.variadic;
        declares.push_back(std::move(signature));
    }
    return declares;
}

std::vector<StructDecl> collectStructs(const Program& program) {
    return program.structs;
}

RuntimeBundle fillBundleFromProgram(const Program& program) {
    RuntimeBundle bundle;
    bundle.exports = collectExports(program);
    bundle.syscalls = collectSyscalls(program);
    bundle.structs = collectStructs(program);
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

void maybeFetchRuntime(const RuntimeOptions& options) {
    if (!options.runtime_version || options.runtime_version->empty()) {
        return;
    }
    if (!options.github_repo || options.github_repo->empty()) {
        return;
    }
    const auto slash = options.github_repo->find('/');
    if (slash == std::string::npos) {
        return;
    }
    const std::string owner = options.github_repo->substr(0, slash);
    const std::string repo = options.github_repo->substr(slash + 1);
    const auto dest = defaultRuntimeInstallDir() / *options.runtime_version / "runtime.a";
    if (std::filesystem::exists(dest)) {
        return;
    }
    const std::string asset = "xlang-runtime-" + *options.runtime_version + ".a";
    (void)fetchGithubReleaseAsset(owner, repo, *options.runtime_version, asset, dest);
}

Program loadRuntimeProgram(const RuntimeOptions& options) {
    if (options.override_path) {
        ModuleLoader loader(*options.override_path);
        return loader.load();
    }

    const std::filesystem::path entry = findRuntimeEntry();
    if (entry.empty()) {
        throw XlangError(
            "runtime not found (set --runtime, or place runtime under module search paths)");
    }

    // Optional VERSION gate when a package root is present.
    std::filesystem::path root = entry;
    if (std::filesystem::is_regular_file(entry)) {
        root = entry.parent_path();
    }
    if (const auto manifest = readRuntimeManifest(root)) {
        if (options.runtime_version &&
            !compilerSupportedByRuntime(*manifest, *options.runtime_version)) {
            // Soft check: still load; user --runtime always wins for path.
        }
    }

    ModuleLoader loader(entry);
    return loader.load();
}

void compileProgramToObject(const Program& program, const std::string& clang,
                            const std::filesystem::path& object_path) {
    CodegenOptions cg_options;
    cg_options.build_kind = BuildKind::Object;
    cg_options.link_runtime = false;
    cg_options.target_triple = getClangTargetTriple(clang);
    cg_options.object_output = object_path.string();

    CodegenResult generated = Codegen::generate(program, cg_options);
    if (generated.object_path.empty()) {
        throw XlangError("codegen did not emit an object file");
    }
}

}  // namespace

RuntimeBundle loadRuntimeExports(const RuntimeOptions& options) {
    maybeFetchRuntime(options);
    return fillBundleFromProgram(loadRuntimeProgram(options));
}

RuntimeBundle ensureRuntime(const RuntimeOptions& options) {
    if (options.work_dir.empty()) {
        throw XlangError("runtime build requires a work directory");
    }

    maybeFetchRuntime(options);

    // Highest priority: prebuilt / embedded artifact for linking.
    if (const auto art =
            resolveRuntimeArtifact(options.override_path, /*skip_runtime=*/false, options.work_dir)) {
        RuntimeBundle bundle;
        try {
            if (options.override_path || !findRuntimeEntry().empty()) {
                bundle = fillBundleFromProgram(loadRuntimeProgram(options));
            }
        } catch (const XlangError&) {
        }
        bundle.object = *art;
        return bundle;
    }

    if (const auto prebuilt = findLibrary("runtime")) {
        RuntimeBundle bundle;
        try {
            bundle = fillBundleFromProgram(loadRuntimeProgram(options));
        } catch (const XlangError&) {
        }
        bundle.object = *prebuilt;
        return bundle;
    }

    const Program program = loadRuntimeProgram(options);
    const std::filesystem::path object = options.work_dir / "runtime.o";
    compileProgramToObject(program, options.clang, object);

    RuntimeBundle bundle = fillBundleFromProgram(program);
    bundle.object = object;
    return bundle;
}

}  // namespace xlang
