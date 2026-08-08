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

-- TLS bridge. Cross builds must not use the host system openssl.
add_requires("openssl", {system = not cross})

-- Host targets that consume LLVM. xmake's system llvm component list still
-- omits LLVMTargetParser, where llvm::Triple lives (LLVM 15+).
function xlang_ctx.add_llvm()
    add_packages("cli11", "llvm")
    add_links("LLVMTargetParser")
end
