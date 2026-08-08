-- Bounded context: C/C++ OS bridge static libraries (ABI only).
-- Layout: src/runtime/bridge/<os>/{filesystem,net,tls,process,time,panic,thread}.c
-- Builds only the host/target OS folder (linux / macosx / windows).
-- Artifacts: build/lib<name>.a

local tc = xlang_ctx.toolchain()

local function bridge_os()
    if is_plat("linux") then
        return "linux"
    elseif is_plat("macosx") then
        return "macosx"
    elseif is_plat("windows", "mingw") then
        return "windows"
    end
    return nil
end

local function bridge(name, files, packages)
    target(name)
        set_kind("static")
        set_basename(name)
        set_targetdir("$(projectdir)/build")
        set_prefixname("lib")
        add_files(table.unpack(files))
        if is_arch("x86_64", "x64") then
            add_defines("XLANG_ARCH_X64")
        elseif is_arch("i386", "x86") then
            add_defines("XLANG_ARCH_X86")
        elseif is_arch("arm64", "aarch64") then
            add_defines("XLANG_ARCH_ARM64")
        end
        if packages then
            add_packages(table.unpack(packages))
        end
        if tc then
            set_toolchains(tc)
        end
    target_end()
end

local os_name = bridge_os()
if os_name then
    local root = "$(projectdir)/src/runtime/bridge/" .. os_name
    bridge("filesystem", {root .. "/filesystem.c"})
    bridge("net", {root .. "/net.c"})
    if os_name ~= "windows" then
        bridge("tls", {root .. "/tls.c"}, {"openssl"})
    else
        bridge("tls", {root .. "/tls.c"})
    end
    bridge("process", {root .. "/process.c"})
    bridge("time", {root .. "/time.c"})
    bridge("panic", {root .. "/panic.c"})
    bridge("thread", {root .. "/thread.c"})
    bridge("sync", {root .. "/sync.c"})
end
