-- Bounded context: host xlang compiler binary (C++ only).
--
-- Source layout (context folders):
--   src/cli/          CLI entry
--   src/lang/         lexer, parser, ast, types
--   src/util/         source manager, diagnostics, errors
--   (src/codegen, src/compiler, src/platform: old pipeline, not built until phase 4.7)

local cross = xlang_ctx.is_cross()

local function add_target_defines()
    if has_config("xlang_target_os") and get_config("xlang_target_os") ~= "" then
        add_defines("XLANG_TARGET_OS=\"" .. get_config("xlang_target_os") .. "\"")
    end
    if has_config("xlang_target_arch") and get_config("xlang_target_arch") ~= "" then
        add_defines("XLANG_TARGET_ARCH=\"" .. get_config("xlang_target_arch") .. "\"")
    end
    if is_arch("x86_64", "x64") then
        add_defines("XLANG_ARCH_X64")
    elseif is_arch("i386", "x86") then
        add_defines("XLANG_ARCH_X86")
    elseif is_arch("arm64", "aarch64") then
        add_defines("XLANG_ARCH_ARM64")
    end
end

-- The standard library (`xl-std/**/*.xl`, the xl-std submodule) is embedded into the compiler: this generates a C++ file that
-- holds every source as a byte array. Rewritten only when the content changes.
-- Frontend library: lexer, preprocessor, macros, parser, AST, types, diagnostics, module checks.
target("xlang-frontend")
    set_kind("static")
    set_default(false)
    set_enabled(not cross)
    on_load(function (target)
        -- The library is the repository root (index.xl and the area folders); `test/` holds its own tests and is not embedded.
        local stdroot = path.join(os.projectdir(), "xl-std")
        local files = {}
        for _, file in ipairs(os.files(path.join(stdroot, "**.xl"))) do
            if not path.relative(file, stdroot):gsub("\\", "/"):startswith("test/") then
                files[#files + 1] = file
            end
        end
        table.sort(files)
        local out = {"// Generated from xl-std/**/*.xl by xmake/compiler.lua. Do not edit.\n",
                     "#include \"xlang/lang/embedded_std.h\"\n\n#include <array>\n\n",
                     "namespace xlang {\nnamespace {\n\n"}
        local entries = {}
        for i, file in ipairs(files) do
            local data = io.readfile(file)
            local bytes = {}
            for j = 1, #data do
                local b = data:byte(j)
                bytes[j] = b < 128 and b or b - 256 -- `char` is signed: keep the initializers narrowing-free
            end
            out[#out + 1] = string.format("constexpr char kData%d[] = {%s0}; // trailing NUL: never an empty array\n", i,
                                          table.concat(bytes, ",") .. (#bytes > 0 and "," or ""))
            local rel = path.relative(file, path.join(os.projectdir(), "xl-std")):gsub("\\", "/")
            entries[i] = string.format("    EmbeddedFile{\"%s\", {kData%d, %d}},\n", rel, i, #data)
        end
        out[#out + 1] = string.format("\nconstexpr std::array<EmbeddedFile, %d> kFiles{{\n%s}};\n\n} // namespace\n\n",
                                      #files, table.concat(entries))
        out[#out + 1] = "std::span<const EmbeddedFile> embeddedStd() noexcept { return kFiles; }\n\n} // namespace xlang\n"
        local content = table.concat(out)
        local target_file = path.join(os.projectdir(), "build", "gen", "std_blob.cpp")
        if not os.isfile(target_file) or io.readfile(target_file) ~= content then
            io.writefile(target_file, content)
        end
        target:add("files", target_file)
    end)
    add_files(
        "$(projectdir)/src/lang/ast/ast.cpp",
        "$(projectdir)/src/lang/ast/clone.cpp",
        "$(projectdir)/src/lang/lexer/lexer.cpp",
        "$(projectdir)/src/lang/preprocess/preprocess.cpp",
        "$(projectdir)/src/lang/macro/macro.cpp",
        "$(projectdir)/src/lang/parser/parser.cpp",
        "$(projectdir)/src/lang/types/types.cpp",
        "$(projectdir)/src/lang/config.cpp",
        "$(projectdir)/src/lang/frontend.cpp",
        "$(projectdir)/src/sema/sema/const_value.cpp",
        "$(projectdir)/src/sema/sema/symbols.cpp",
        "$(projectdir)/src/sema/sema/instances.cpp",
        "$(projectdir)/src/sema/sema/types.cpp",
        "$(projectdir)/src/sema/sema/module_scope.cpp",
        "$(projectdir)/src/sema/sema/const_eval.cpp",
        "$(projectdir)/src/sema/sema/enums.cpp",
        "$(projectdir)/src/sema/sema/layout.cpp",
        "$(projectdir)/src/sema/sema/checks.cpp",
        "$(projectdir)/src/sema/layout.cpp",
        "$(projectdir)/src/sema/conversion.cpp",
        "$(projectdir)/src/sema/scope.cpp",
        "$(projectdir)/src/sema/compilation/locate.cpp",
        "$(projectdir)/src/sema/compilation/exports.cpp",
        "$(projectdir)/src/sema/compilation/imports.cpp",
        "$(projectdir)/src/sema/compilation/registered.cpp",
        "$(projectdir)/src/sema/compilation/driver.cpp",
        "$(projectdir)/src/sema/lower/items.cpp",
        "$(projectdir)/src/sema/lower/stmt.cpp",
        "$(projectdir)/src/sema/lower/expr.cpp",
        "$(projectdir)/src/sema/variadic.cpp",
        "$(projectdir)/src/pkg/toml.cpp",
        "$(projectdir)/src/pkg/semver.cpp",
        "$(projectdir)/src/pkg/manifest.cpp",
        "$(projectdir)/src/pkg/git.cpp",
        "$(projectdir)/src/pkg/project.cpp",
        "$(projectdir)/src/hir/hir.cpp",
        "$(projectdir)/src/hir/prune.cpp",
        "$(projectdir)/src/util/source.cpp",
        "$(projectdir)/src/util/diagnostics.cpp",
        "$(projectdir)/src/util/program.cpp",
        -- `xlang::host::run`: spawned by `src/util/program.cpp` and `src/pkg/git.cpp` above, so it
        -- lives here rather than in the `xlang` binary, which reaches it through this library.
        "$(projectdir)/src/cli/process.cpp"
    )
    add_includedirs("$(projectdir)/include", {public = true})
    -- Internal headers live in the area directory (`src/sema/lower/lower.hpp`) and are included
    -- as `sema/...`: not part of the installed `include/` tree.
    add_includedirs("$(projectdir)/src", {public = true})

-- Code generation: HIR -> LLVM, pass pipeline, object/IR/bitcode writers, C header, link driver.
target("xlang-codegen")
    set_kind("static")
    set_default(false)
    set_enabled(not cross)
    add_files(
        "$(projectdir)/src/codegen/*.cpp",
        "$(projectdir)/src/codegen/llvm/*.cpp",
        "$(projectdir)/src/driver/*.cpp"
    )
    add_includedirs("$(projectdir)/include", {public = true})
    add_deps("xlang-frontend")
    xlang_ctx.add_llvm()

target("xlang")
    set_kind("binary")
    set_basename("xlang")
    set_default(not cross)
    set_enabled(not cross)
    add_files("$(projectdir)/src/cli/app/main.cpp", "$(projectdir)/src/cli/tests/runner.cpp",
              "$(projectdir)/src/cli/console.cpp",
              "$(projectdir)/src/cli/update/update.cpp", "$(projectdir)/src/cli/completions/completions.cpp",
              "$(projectdir)/src/cli/package/package.cpp")
    add_deps("xlang-codegen", "xlang-frontend")
    add_target_defines()
    xlang_ctx.add_llvm()
    if is_plat("macosx") then
        add_syslinks("z", "curses", "xml2")
    elseif is_plat("linux") then
        add_syslinks("ncurses", "z", "pthread", "dl", "m")
    end

-- Plan 7.6: the standard library is plain xl source (`xl-std/`). Building this target type-checks
-- it with the freshly built compiler. (The compiler itself embeds the sources: see std_blob.cpp.)
target("std")
    set_kind("phony")
    set_default(false)
    set_enabled(not cross)
    add_deps("xlang")
    after_build(function (target)
        local exe = path.join(os.projectdir(), "outputs", "xlang")
        os.execv(exe, {"check", path.join(os.projectdir(), "xl-std", "index.xl"), "--no-std"})
    end)

-- Examples: `xlang-docs/examples/*.xl` (short programs that the documentation pages include) and `examples/<name>/main.xl` (complete
-- programs). Every build copies both sets into `build/examples/` (xlang-docs/ and project/) and type-checks each program with the
-- freshly built compiler, so an example cannot silently rot.
target("examples")
    set_kind("phony")
    set_default(not cross)
    set_enabled(not cross)
    add_deps("xlang")
    after_build(function (target)
        local root = os.projectdir()
        local exe = path.join(root, "outputs", "xlang")
        local out = path.join(root, "build", "examples")
        os.tryrm(out)
        os.cp(path.join(root, "xlang-docs", "examples"), path.join(out, "docs"))
        os.cp(path.join(root, "examples"), path.join(out, "project"))
        for _, f in ipairs(os.files(path.join(out, "docs", "*.xl"))) do
            os.execv(exe, {"check", f})
        end
        for _, d in ipairs(os.dirs(path.join(out, "project", "*"))) do
            os.execv(exe, {"check", path.join(d, "main.xl")})
        end
    end)

-- The tests of the standard library live in the xl-std repository (`xl-std/test/`), and the compiler is `xlc`. `xmake run e2e` builds the
-- native runner (an xlang program, `xl-std/test/e2e/runner.xl`) with the compiler named by $XLC (default: `xlc` on PATH) and runs it on the
-- table. The Docker and CrossOver legs are `xl-std/test/e2e/run.sh`.
target("e2e")
    set_kind("phony")
    set_default(false)
    set_enabled(not cross)
    on_run(function (target)
        local stdroot = path.join(os.projectdir(), "xl-std")
        local xlc = os.getenv("XLC") or "xlc"
        local runner = path.join(os.projectdir(), "outputs", "e2e-runner")
        os.execv(xlc, {path.join(stdroot, "test", "e2e", "runner.xl"), "-M", "std=" .. stdroot, "--import", "std",
                       "-DfeatureDefaultAllocator=true", "-o", runner})
        os.execv(runner, {xlc, stdroot, path.join(stdroot, "test", "e2e")})
    end)
