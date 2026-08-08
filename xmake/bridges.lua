-- Bounded context: OS bridge static libraries.
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

bridge("xlang_net_server", {"$(projectdir)/src/bridge/net_server.c"})
bridge("xlang_panic_bridge", {"$(projectdir)/src/bridge/panic_bridge.c"})
bridge("xlang_process_bridge", {"$(projectdir)/src/bridge/process_bridge.c"})
bridge("xlang_time_bridge", {"$(projectdir)/src/bridge/time_bridge.c"})
bridge("xlang_file_bridge", {"$(projectdir)/src/bridge/file_bridge.cpp"})
bridge("xlang_tls_bridge", {"$(projectdir)/src/bridge/tls_bridge.c"}, {"openssl"})
