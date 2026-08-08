# Runtime package & embed

## Layout

```
src/runtime/
  RUNTIME_VERSION          # plaintext version for the runtime artifact
  SUPPORTED_COMPILERS      # lines of supported compiler versions (`*` = any)
  frontend/                # OS-agnostic .xlang sources
  bridge/{linux,macosx,windows}/  # C ABI: xl_<object>_<action>
```

## Resolution order (user compile)

Highest wins:

1. `--runtime=` / `--bridge=` overrides
2. Else extract **embedded** defaults from the `xlang` binary (unless `--no-runtime` / `--no-bridge`)
3. Else in-tree / `XLANG_LIB_PATH` via `findLibrary`

`--no-runtime`: do not link the frontend runtime; **bridges still link** unless `--no-bridge`.
`--no-bridge`: raw compile — user supplies symbols/objects. OS baseline syslibs still apply for executables.

## Defaults for `xlang build foo.xlang`

| Setting | Default |
|---------|---------|
| `--build=` | `executable` |
| Runtime | on |
| Bridges | on |
| OS baseline syslibs | on (e.g. `-pthread`) |

Build kinds: `executable` | `static` | `shared` (experimental) | `object`.

## Release bootstrap (mandatory)

You cannot embed `.xlang` runtime until something can compile it:

1. **Stage-1:** build `xlang` without final runtime embed (bridges as normal static libs).
2. Stage-1 compiles `src/runtime/frontend` → `runtime.o` / `runtime.a`.
3. Pack OS bridges → `bridges.a`; generate `build/embed/embedded_{runtime,bridges}.c`.
4. **Stage-2:** rebuild `xlang` so those payloads compile in (`XLANG_HAS_EMBEDDED_*`).

xmake targets: `build_bridges_pack`, `bootstrap_embed`, then rebuild `xlang`.

## Distribution

Runtime artifacts ship on GitHub Releases (`RUNTIME_VERSION` + `SUPPORTED_COMPILERS` in the tarball root). Compiler may fetch via `--runtime-version` + `--runtime-repo=owner/repo`. User `--runtime=` always wins for that build.
