-- Bounded context: third-party package requires.

local cross = xlang_ctx.is_cross()

if not cross then
    add_requires("cli11 2.4.2")
end

-- LLVM comes from the system (`llvm-config`, or $LLVM_CONFIG). Linked statically; only the
-- components the compiler uses (passes, the aarch64/x86 backends, object/archive writers).
function xlang_ctx.add_llvm()
    add_packages("cli11")
    on_load(function (target)
        local candidates = {}
        if os.getenv("LLVM_CONFIG") then
            table.insert(candidates, os.getenv("LLVM_CONFIG"))
        end
        local brew = os.getenv("HOMEBREW_PREFIX")
        if brew then
            table.insert(candidates, path.join(brew, "opt/llvm/bin/llvm-config"))
        end
        for _, c in ipairs({"/opt/homebrew/opt/llvm/bin/llvm-config", "/usr/local/opt/llvm/bin/llvm-config",
                            "llvm-config"}) do
            table.insert(candidates, c)
        end
        local cfg
        for _, c in ipairs(candidates) do
            local ok = try { function () return os.iorun(c .. " --version") end }
            if ok then
                cfg = c
                break
            end
        end
        assert(cfg, "llvm-config not found (set LLVM_CONFIG)")
        local function run(args)
            return (os.iorun(cfg .. " " .. args)):trim()
        end
        target:add("includedirs", run("--includedir"))
        target:add("linkdirs", run("--libdir"))
        for _, l in ipairs(run("--link-static --libs aarch64 x86 passes irreader bitwriter object mc target codegen"):split("%s+")) do
            local name = l:gsub("^%-l", "")
            if #name > 0 then
                target:add("links", name)
            end
        end
        target:add("linkdirs", "/opt/homebrew/lib", "/usr/local/lib")
        target:add("syslinks", "z", "zstd")
        if is_plat("macosx") then
            target:add("syslinks", "curses", "xml2")
        elseif is_plat("linux") then
            target:add("syslinks", "ncurses", "pthread", "dl", "m")
        end
    end)
end
