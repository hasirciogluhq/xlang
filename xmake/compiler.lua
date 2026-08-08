-- Bounded context: host xlang compiler binary (disabled when cross-compiling).

local cross = xlang_ctx.is_cross()

target("xlang")
    set_kind("binary")
    set_basename("xlang")
    set_default(not cross)
    set_enabled(not cross)
    add_files(
        "$(projectdir)/src/main.cpp",
        "$(projectdir)/src/ast.cpp",
        "$(projectdir)/src/lexer.cpp",
        "$(projectdir)/src/parser.cpp",
        "$(projectdir)/src/codegen.cpp",
        "$(projectdir)/src/compiler.cpp",
        "$(projectdir)/src/module.cpp",
        "$(projectdir)/src/runtime.cpp",
        "$(projectdir)/src/syscalls.cpp",
        "$(projectdir)/src/input.cpp",
        "$(projectdir)/src/types.cpp",
        "$(projectdir)/src/test_runner.cpp",
        "$(projectdir)/src/util.cpp",
        "$(projectdir)/src/paths.cpp"
    )
    add_includedirs("$(projectdir)/include")
    xlang_ctx.add_llvm()
    add_deps(
        "xlang_net_server",
        "xlang_panic_bridge",
        "xlang_process_bridge",
        "xlang_time_bridge",
        "xlang_file_bridge",
        "xlang_tls_bridge",
        {inherit = false}
    )
    if is_plat("linux") then
        add_syslinks("ncurses", "z", "pthread", "dl", "m")
    elseif is_plat("macosx") then
        add_syslinks("z", "curses", "xml2")
    end
