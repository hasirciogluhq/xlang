-- Bounded context: host xlang compiler binary (C++ only).
--
-- Source layout (context folders):
--   src/cli/          CLI entry
--   src/lang/         lexer, parser, ast, types
--   src/codegen/      LLVM IR builder split by concern
--   src/compiler/     compile/link/test orchestration
--   src/host/         layout and target resolution
--   src/platform/     OS backends
--   src/util/         shared helpers

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
    set_default(not cross)
    set_enabled(not cross)
    add_files(
        "$(projectdir)/src/cli/main.cpp",
        "$(projectdir)/src/lang/ast/ast.cpp",
        "$(projectdir)/src/lang/lexer/lexer.cpp",
        "$(projectdir)/src/lang/parser/parser.cpp",
        "$(projectdir)/src/lang/types/types.cpp",
        "$(projectdir)/src/codegen/*.cpp",
        "$(projectdir)/src/codegen/detail/*.cpp",
        "$(projectdir)/src/compiler/*.cpp",
        "$(projectdir)/src/compiler/test/*.cpp",
        "$(projectdir)/src/util/*.cpp",
        "$(projectdir)/src/host/*.cpp",
        "$(projectdir)/src/platform/common.cpp"
    )
    if is_plat("linux") then
        add_files("$(projectdir)/src/platform/linux/sys.cpp")
    elseif is_plat("macosx") then
        add_files("$(projectdir)/src/platform/macosx/sys.cpp")
    elseif is_plat("windows") then
        add_files("$(projectdir)/src/platform/windows/sys.cpp")
    end
    add_includedirs("$(projectdir)/include")
    add_target_defines()
    xlang_ctx.add_llvm()
    if is_plat("linux") then
        add_syslinks("ncurses", "z", "pthread", "dl", "m")
    elseif is_plat("macosx") then
        add_syslinks("z", "curses", "xml2")
    end
