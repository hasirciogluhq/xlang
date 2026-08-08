-- Bounded context: cross / host toolchain selection.
-- xmake downloads toolchain packages into its own cache from -p / -a.
--   xmake f -p mingw -a x86_64
--   xmake f -p linux -a x86_64
--   xmake f -p macosx -a arm64

for _, pkg in ipairs(xlang_ctx.toolchain_packages()) do
    add_requires(pkg)
end

local tc = xlang_ctx.toolchain()
if tc then
    set_toolchains(tc)
end
