-- xlang: LLVM-based language compiler
-- Reference: https://xmake.io/llms.txt / https://xmake.io/llms-full.txt
--
-- Orchestrator only. Bounded contexts live under xmake/.
-- Build compiles the C/C++ compiler and host tooling only.

set_project("xlang")
-- `xlang --version` and `xlang self-update` compare against this version (XLANG_VERSION).
local xlang_version = "0.1.0"
set_version(xlang_version)
add_defines("XLANG_VERSION=\"" .. xlang_version .. "\"")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

includes("xmake/context.lua")

-- C++26 wherever the compiler knows it, C++23 as the mandatory floor (never older).
rule("modern-cpp")
    on_config(function (target)
        target:set("languages", "c23", target:has_cxxflags("-std=c++26") and "c++26" or "c++23")
    end)
rule_end()

add_rules("modern-cpp")
set_warnings("all")
set_targetdir("$(projectdir)/outputs")

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
