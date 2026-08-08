-- xlang: LLVM-based language compiler
-- Reference: https://xmake.io/llms.txt / https://xmake.io/llms-full.txt
--
-- Orchestrator only. Bounded contexts live under xmake/.

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
includes("xmake/bridges.lua")
includes("xmake/compiler.lua")
