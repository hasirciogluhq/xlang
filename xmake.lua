-- xlang: LLVM-based language compiler
-- Reference: https://xmake.io/llms.txt / https://xmake.io/llms-full.txt
--
-- Orchestrator only. Bounded contexts live under xmake/.
-- Build compiles the C/C++ compiler and host tooling only.

set_project("xlang")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

set_languages("c11", "c++23")
set_warnings("all")
set_targetdir("$(projectdir)/build")

includes("xmake/context.lua")
includes("xmake/toolchains.lua")
includes("xmake/packages.lua")
includes("xmake/compiler.lua")

option("xlang_target_os")
    set_default("")
    set_showmenu(true)
    set_description("Override XLANG_TARGET_OS (linux|macosx|windows)")
option_end()

option("xlang_target_arch")
    set_default("")
    set_showmenu(true)
    set_description("Override XLANG_TARGET_ARCH (x86|x64|arm64)")
option_end()
