-- Bounded context: host tooling around the standalone compiler.
--
-- The compiler (lexer, preprocessor, parser, sema, HIR, codegen, linker driver, diagnostics) is `xlc`, the `xlc/` submodule, built with
-- `xmake f -P xlc && xmake -P xlc`. It has no code in this tree.
--
-- Source layout (context folders):
--   src/cli/          `xlang` tool entry: run, test, package, self-update, completions
--   src/pkg/          xlang.toml / xlang.lock / .xlang/cache: TOML subset, release tags, git, resolver
--
-- The `xlang` tool still includes compiler headers that no longer live here, so it does not build until it is ported to call `xlc`
-- as a child process (`-M NAME=DIR` per package, `-M std=<xl-std> --import std`). It is kept as a non-default target for that port.

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

target("xlang")
    set_kind("binary")
    set_basename("xlang")
    set_default(false)
    set_enabled(not cross)
    add_files("$(projectdir)/src/cli/app/main.cpp", "$(projectdir)/src/cli/tests/runner.cpp",
              "$(projectdir)/src/cli/console.cpp", "$(projectdir)/src/cli/process.cpp",
              "$(projectdir)/src/cli/update/update.cpp", "$(projectdir)/src/cli/completions/completions.cpp",
              "$(projectdir)/src/cli/package/package.cpp",
              "$(projectdir)/src/pkg/toml.cpp", "$(projectdir)/src/pkg/semver.cpp", "$(projectdir)/src/pkg/manifest.cpp",
              "$(projectdir)/src/pkg/git.cpp", "$(projectdir)/src/pkg/project.cpp")
    add_includedirs("$(projectdir)/include", "$(projectdir)/src")
    add_target_defines()
    if is_plat("macosx") then
        add_syslinks("z", "curses", "xml2")
    elseif is_plat("linux") then
        add_syslinks("ncurses", "z", "pthread", "dl", "m")
    end

-- Type-checks the standard library (`xl-std/index.xl`) with the compiler named by $XLC (default: `xlc` on PATH).
target("std")
    set_kind("phony")
    set_default(false)
    set_enabled(not cross)
    on_build(function (target)
        local stdroot = path.join(os.projectdir(), "xl-std")
        os.execv(os.getenv("XLC") or "xlc", {path.join(stdroot, "index.xl"), "--check",
                 "-DfeatureDefaultAllocator=true"})
    end)

-- Examples: `xlang-docs/examples/*.xl` (short programs that the documentation pages include) and `examples/<name>/main.xl` (complete
-- programs). Every build type-checks each program with the compiler named by $XLC (default: `xlc` on PATH), so an example cannot
-- silently rot.
target("examples")
    set_kind("phony")
    set_default(false)
    set_enabled(not cross)
    on_build(function (target)
        local root = os.projectdir()
        local xlc = os.getenv("XLC") or "xlc"
        local flags = {"--check", "-M", "std=" .. path.join(root, "xl-std"), "--import", "std", "-DfeatureDefaultAllocator=true"}
        local function check(file)
            os.execv(xlc, table.join({file}, flags))
        end
        for _, f in ipairs(os.files(path.join(root, "xlang-docs", "examples", "*.xl"))) do
            check(f)
        end
        for _, d in ipairs(os.dirs(path.join(root, "examples", "*"))) do
            check(path.join(d, "main.xl"))
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
