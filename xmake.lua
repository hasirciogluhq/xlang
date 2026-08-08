-- xlang: LLVM-based language compiler
-- Reference: https://xmake.io/llms.txt / https://xmake.io/llms-full.txt

set_project("xlang")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})
add_moduledirs("xmake")

set_languages("c11", "c++17")
set_warnings("all")
-- Flat output: build/xlang (+ bridge .a files) for tooling / Docker / VS Code
set_targetdir("$(projectdir)/build")

add_requires("cli11 2.4.2")
add_requires("openssl")

local BRIDGE_DEFINE = {
    xlang_net_server     = "XLANG_NET_SERVER",
    xlang_panic_bridge   = "XLANG_PANIC_BRIDGE",
    xlang_process_bridge = "XLANG_PROCESS_BRIDGE",
    xlang_time_bridge    = "XLANG_TIME_BRIDGE",
    xlang_file_bridge    = "XLANG_FILE_BRIDGE",
    xlang_tls_bridge     = "XLANG_TLS_BRIDGE",
}

local function find_pkg_library(pkg, names)
    local linkdirs = table.wrap(pkg:get("linkdirs"))
    local installdir = pkg:installdir()
    if installdir and installdir ~= "" then
        table.insert(linkdirs, path.join(installdir, "lib"))
        table.insert(linkdirs, path.join(installdir, "lib64"))
    end
    for _, dir in ipairs(linkdirs) do
        for _, name in ipairs(names) do
            for _, ext in ipairs({".dylib", ".so", ".a", ".lib"}) do
                local candidates = {
                    path.join(dir, "lib" .. name .. ext),
                    path.join(dir, name .. ext),
                }
                for _, f in ipairs(candidates) do
                    if os.isfile(f) then
                        return path.absolute(f)
                    end
                end
            end
        end
    end
end

local function quote_define_path(p)
    return p:gsub("\\", "/")
end

target("xlang_net_server")
    set_kind("static")
    set_default(false)
    add_files("src/bridge/net_server.c")

target("xlang_panic_bridge")
    set_kind("static")
    set_default(false)
    add_files("src/bridge/panic_bridge.c")

target("xlang_process_bridge")
    set_kind("static")
    set_default(false)
    add_files("src/bridge/process_bridge.c")

target("xlang_time_bridge")
    set_kind("static")
    set_default(false)
    add_files("src/bridge/time_bridge.c")

target("xlang_file_bridge")
    set_kind("static")
    set_default(false)
    add_files("src/bridge/file_bridge.cpp")

target("xlang_tls_bridge")
    set_kind("static")
    set_default(false)
    add_files("src/bridge/tls_bridge.c")
    add_packages("openssl")

target("xlang")
    set_kind("binary")
    set_basename("xlang")
    add_files(
        "src/main.cpp",
        "src/ast.cpp",
        "src/lexer.cpp",
        "src/parser.cpp",
        "src/codegen.cpp",
        "src/compiler.cpp",
        "src/module.cpp",
        "src/runtime.cpp",
        "src/syscalls.cpp",
        "src/input.cpp",
        "src/types.cpp",
        "src/test_runner.cpp"
    )
    add_includedirs("include")
    add_packages("cli11")
    -- Build bridges for path defines only; do not link them into the compiler.
    add_deps(
        "xlang_net_server",
        "xlang_panic_bridge",
        "xlang_process_bridge",
        "xlang_time_bridge",
        "xlang_file_bridge",
        "xlang_tls_bridge",
        {inherit = false}
    )

    on_load(function (target)
        local gendir = target:autogendir()
        target:add("files", path.join(gendir, "embedded_runtime.cpp"), {always_added = true})
        target:add("files", path.join(gendir, "embedded_libs.cpp"), {always_added = true})
    end)

    on_config(function (target)
        import("core.project.project")

        target:add("defines",
            'XLANG_RUNTIME_DIR="' .. quote_define_path(path.absolute("runtime")) .. '"',
            'XLANG_LIBS_DIR="' .. quote_define_path(path.absolute("libs")) .. '"')

        for name, define in pairs(BRIDGE_DEFINE) do
            local dep = target:dep(name)
            if dep then
                target:add("defines",
                    define .. '="' .. quote_define_path(path.absolute(dep:targetfile())) .. '"')
            end
        end

        local tls = project.target("xlang_tls_bridge")
        local openssl = tls and tls:pkg("openssl")
        if openssl then
            local ssl = find_pkg_library(openssl, {"ssl", "libssl"})
            local crypto = find_pkg_library(openssl, {"crypto", "libcrypto"})
            if ssl then
                target:add("defines", 'XLANG_OPENSSL_SSL="' .. quote_define_path(ssl) .. '"')
            end
            if crypto then
                target:add("defines", 'XLANG_OPENSSL_CRYPTO="' .. quote_define_path(crypto) .. '"')
            end
        end
    end)

    before_build(function (target)
        import("core.project.depend")
        import("embed")

        local gendir = target:autogendir()
        local runtime_cpp = path.join(gendir, "embedded_runtime.cpp")
        local libs_cpp = path.join(gendir, "embedded_libs.cpp")
        local runtime_dir = path.join(os.projectdir(), "runtime")
        local libs_dir = path.join(os.projectdir(), "libs")

        depend.on_changed(function ()
            embed.generate_runtime(runtime_dir, runtime_cpp)
        end, {
            files = os.files(path.join(runtime_dir, "**.xlang")),
            dependfile = target:dependfile(runtime_cpp),
            changed = target:is_rebuilt()
        })

        depend.on_changed(function ()
            embed.generate_libs(libs_dir, libs_cpp)
        end, {
            files = os.files(path.join(libs_dir, "**.xlang")),
            dependfile = target:dependfile(libs_cpp),
            changed = target:is_rebuilt()
        })
    end)
