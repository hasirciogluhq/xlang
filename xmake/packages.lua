-- Bounded context: third-party package requires.

local cross = xlang_ctx.is_cross()

if not cross then
    add_requires("cli11 2.4.2")
    add_requires("llvm", {
        kind = "library",
        system = true,
        configs = {
            clang = false,
            ["clang-tools-extra"] = false,
            lld = false,
            lldb = false,
            mlir = false,
            polly = false,
            ["compiler-rt"] = false,
            libunwind = false,
            libcxx = false,
            libcxxabi = false,
        },
    })
end

-- Host targets that consume LLVM. xmake's system llvm component list lags
-- newer LLVM splits: TargetParser (15+), CGData / CodeGenTypes /
-- DebugInfoDWARFLowLevel (19–22), and Homebrew LLVM needs zstd.
function xlang_ctx.add_llvm()
    add_packages("cli11", "llvm")
    add_links(
        "LLVMTargetParser",
        "LLVMCGData",
        "LLVMCodeGenTypes",
        "LLVMDebugInfoDWARFLowLevel"
    )
    if is_plat("macosx", "linux") then
        local brew = os.getenv("HOMEBREW_PREFIX")
        if brew then
            add_linkdirs(path.join(brew, "lib"))
        elseif is_plat("macosx") then
            add_linkdirs("/opt/homebrew/lib", "/usr/local/lib")
        end
        add_syslinks("zstd")
    end
end
