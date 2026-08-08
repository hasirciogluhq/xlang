-- Bounded context: C/C++ OS bridge static libraries (ABI only).
-- Layout: src/runtime/bridge — sockets, tls, file, process, time, panic.
-- No xlang sources here. Higher-level APIs live in src/runtime/frontend.
-- Artifacts land in build/ and are discovered via compiler lib search paths.

local tc = xlang_ctx.toolchain()

local function bridge(name, files, packages)
    target(name)
        set_kind("static")
        set_basename(name)
        set_targetdir("$(projectdir)/build")
        set_prefixname("lib")
        add_files(table.unpack(files))
        if packages then
            add_packages(table.unpack(packages))
        end
        if tc then
            set_toolchains(tc)
        end
    target_end()
end

local bridge_dir = "$(projectdir)/src/runtime/bridge"

bridge("xlang_net_bridge", {bridge_dir .. "/net_bridge.c"})
bridge("xlang_panic_bridge", {bridge_dir .. "/panic_bridge.c"})
bridge("xlang_process_bridge", {bridge_dir .. "/process_bridge.c"})
bridge("xlang_time_bridge", {bridge_dir .. "/time_bridge.c"})
bridge("xlang_file_bridge", {bridge_dir .. "/file_bridge.cpp"})
bridge("xlang_tls_bridge", {bridge_dir .. "/tls_bridge.c"}, {"openssl"})
