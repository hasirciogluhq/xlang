-- Shared build context for description-scope includes.
-- Other xmake/*.lua files read this table; do not put targets here.

xlang_ctx = xlang_ctx or {}

function xlang_ctx.is_cross()
    return (is_plat("mingw") and not is_host("windows"))
        or (is_plat("linux") and not is_host("linux"))
        or (is_plat("macosx") and not is_host("macosx"))
end

-- Returns toolchain name for set_toolchains, or nil for host-native.
-- mingw: llvm-mingw has no macOS host builds → system mingw (Homebrew).
function xlang_ctx.toolchain()
    if is_plat("mingw") then
        if is_host("macosx") then
            return "mingw"
        end
        return "mingw[clang]@llvm-mingw"
    elseif is_plat("linux") and not is_host("linux") then
        return "@muslcc"
    elseif is_plat("macosx") and not is_host("macosx") then
        return "@zig"
    end
end

function xlang_ctx.toolchain_packages()
    if is_plat("mingw") and not is_host("macosx") then
        return {"llvm-mingw"}
    elseif is_plat("linux") and not is_host("linux") then
        return {"muslcc"}
    elseif is_plat("macosx") and not is_host("macosx") then
        return {"zig"}
    end
    return {}
end
