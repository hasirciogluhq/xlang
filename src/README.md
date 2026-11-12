# src: the `xl` tool

`xl` is written in xlang and built by the compiler `xlc` with the stdlib of the `xl-std/` submodule:

```bash
cd src
xlc main.xl -M std=../xl-std --import std -DfeatureDefaultAllocator=true '-DxlVersion="1.0.0-rc.1"' -o ../xl
```

Without `-DxlVersion` the binary reports `0.0.0-dev`. Tests: `test/run.sh <xl> <xlc> xl-std` (every command, no network); CI and
releases: `.github/workflows/ci.yml`, `release.yml`. The commands are documented in `xlang-docs/compiler/cli.md`, `packages.md` and
`install.md`.

| Path | Content |
| --- | --- |
| `main.xl` | entry point: dispatches `run`, `build`, `check`, `env`, `pkg`, `lc`, `self-update`, `version`, `help` |
| `core/problem.xl` | `Problem` (code, message, fix) and the `Done`/`fail`/`finish` helpers every command returns through |
| `core/host.xl` | OS and CPU names, `$XLANG_HOME`, environment variables, path joining |
| `core/proc.xl` | `Line`: a child process command line; `call` (inherit stdio) and `capture` (collect stdout) |
| `core/git.xl` | `git ls-remote` and `git clone` for packages and release tags |
| `core/tag.xl` | release tags and version specs (`latest`, `v3`, `v3.4`, `v3.4.1`) and choosing among a repository's tags |
| `core/scan.xl` | the TOML subset xl reads: keys, strings, inline tables, one-line string arrays |
| `core/store.xl` | atomic file writes (temporary file, then rename) |
| `core/release.xl` | GitHub release assets (or `$XLANG_MIRROR`), `checksums.txt` verification, `.tar.gz`/`.zip` unpacking |
| `core/version.xl` | `VERSION` from `-DxlVersion` |
| `pkg/manifest.xl` | `xlang.toml`: the `[dependencies]` table, edited line by line so comments and other tables survive |
| `pkg/lock.xl` | `xlang.lock`: one `[[package]]` per resolved repository |
| `pkg/resolve.xl` | minimal version selection over the dependency graph, the `.xlang/cache` layout |
| `pkg/command.xl` | `xl pkg add|install|update|remove|list|clean` |
| `lc/toolchain.xl` | installed compilers and stdlibs under `$XLANG_HOME`, the active pair, `$XLC`/`$XLANG_STD` |
| `lc/command.xl` | `xl lc install|update|list|use` |
| `build/setup.xl` | the build environment of one file: toolchain, package roots, `xlang.local.toml` |
| `build/command.xl` | `xl check|build|run|env` |
| `self/command.xl` | `xl self-update` |
