-- Bounded context: C/C++ OS bridge static libraries (ABI only).
-- Layout: src/runtime/bridge/<domain>/ — no *_bridge suffix, plain names.
-- Artifacts: build/lib<name>.a  (filesystem, net, tls, process, time, panic)

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

local root = "$(projectdir)/src/runtime/bridge"

bridge("filesystem", {root .. "/filesystem/filesystem.c"})
bridge("net", {root .. "/net/net.c"})
bridge("tls", {root .. "/net/tls.c"}, {"openssl"})
bridge("process", {root .. "/process/process.c"})
bridge("time", {root .. "/time/time.c"})
bridge("panic", {root .. "/panic/panic.c"})
