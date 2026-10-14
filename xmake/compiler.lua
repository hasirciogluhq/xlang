-- Bounded context: host xlang compiler binary (C++ only).
--
-- Source layout (context folders):
--   src/cli/          CLI entry
--   src/lang/         lexer, parser, ast, types
--   src/util/         source manager, diagnostics, errors
--   (src/codegen, src/compiler, src/host, src/platform: old pipeline, not built until phase 4.7)

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

-- Frontend library: lexer, preprocessor, macros, parser, AST, types, diagnostics, module checks.
target("xlang-frontend")
    set_kind("static")
    set_default(false)
    set_enabled(not cross)
    add_files(
        "$(projectdir)/src/lang/ast/ast.cpp",
        "$(projectdir)/src/lang/lexer/lexer.cpp",
        "$(projectdir)/src/lang/preprocess/preprocess.cpp",
        "$(projectdir)/src/lang/macro/macro.cpp",
        "$(projectdir)/src/lang/parser/parser.cpp",
        "$(projectdir)/src/lang/types/types.cpp",
        "$(projectdir)/src/lang/config.cpp",
        "$(projectdir)/src/lang/frontend.cpp",
        "$(projectdir)/src/sema/sema.cpp",
        "$(projectdir)/src/sema/layout.cpp",
        "$(projectdir)/src/sema/conversion.cpp",
        "$(projectdir)/src/sema/scope.cpp",
        "$(projectdir)/src/sema/compilation.cpp",
        "$(projectdir)/src/sema/lower.cpp",
        "$(projectdir)/src/hir/hir.cpp",
        "$(projectdir)/src/util/source.cpp",
        "$(projectdir)/src/util/diagnostics.cpp"
    )
    add_includedirs("$(projectdir)/include", {public = true})

-- Code generation: HIR -> LLVM, pass pipeline, object/IR/bitcode writers, C header, link driver.
target("xlang-codegen")
    set_kind("static")
    set_default(false)
    set_enabled(not cross)
    add_files(
        "$(projectdir)/src/codegen/*.cpp",
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
    add_files("$(projectdir)/src/cli/main.cpp")
    add_deps("xlang-codegen", "xlang-frontend")
    add_target_defines()
    xlang_ctx.add_llvm()
    if is_plat("macosx") then
        add_syslinks("z", "curses", "xml2")
    elseif is_plat("linux") then
        add_syslinks("ncurses", "z", "pthread", "dl", "m")
    end

-- Frontend/diagnostics unit tests: `xmake build xlang-tests && xmake run xlang-tests`.
target("xlang-tests")
    set_kind("binary")
    set_default(false)
    set_enabled(not cross)
    add_files("$(projectdir)/test/compiler/main.cpp", "$(projectdir)/test/compiler/sema_test.cpp",
              "$(projectdir)/test/compiler/lower_test.cpp")
    add_deps("xlang-frontend")
    add_tests("frontend")

-- Plan 7.6: the standard library is plain xl source (`std/`). Building this target type-checks
-- it with the freshly built compiler; installing puts it under <prefix>/share/xlang/std, where
-- the compiler looks next to its own binary (`../share/xlang/std`) as well as in `std/`.
target("std")
    set_kind("phony")
    set_default(false)
    set_enabled(not cross)
    add_deps("xlang")
    after_build(function (target)
        local exe = path.join(os.projectdir(), "build", "xlang")
        os.execv(exe, {"check", path.join(os.projectdir(), "std", "index.xl"), "--no-std"})
    end)
    on_install(function (target)
        local dest = path.join(target:installdir(), "share", "xlang")
        os.mkdir(dest)
        os.cp(path.join(os.projectdir(), "std"), dest)
    end)

-- End-to-end tests: `xmake run e2e` builds and runs the programs of test/e2e (natively, and on
-- Linux aarch64/x86_64 through Docker when it is available).
target("e2e")
    set_kind("phony")
    set_default(false)
    set_enabled(not cross)
    add_deps("xlang")
    on_run(function (target)
        os.execv(path.join(os.projectdir(), "test", "e2e", "run.sh"))
    end)
