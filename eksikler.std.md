# Stdlib gaps (self-hosting)

Based on the file list of `std/` and `docs/LANGUAGE.md`. File contents were not searched, so "missing" means no module of that name exists.

Existing: `Vec<T>`, `String`, `fmt`, `fs` (read-all), `json`, `mem` (`Allocator`), `thread`, `sync`, `sched`, `io`, `testing`.

| # | Module | Status | Why a compiler needs it |
|---|--------|--------|-------------------------|
| 1 | `Option<T>`, `Result<T, E>` | Missing | Error and absence values; generics already allow plain structs. |
| 2 | `Span<T>` | Missing | Stand-in until the compiler has a slice type. |
| 3 | `HashMap<K, V>`, `HashSet<K>` | Missing | Symbol tables, interning, import cache. Needs hash and equality hooks (depends on operator overload). |
| 4 | Ordered map / `sort` | Missing | Deterministic diagnostics and output order. |
| 5 | Arena allocator | Missing | AST/HIR lifetime; the `Allocator` interface already exists. |
| 6 | `StringBuilder` | Partial | `String` + `fmtAppend` cover most of it. |
| 7 | Path helpers | Missing | Import resolution, output paths. |
| 8 | `fs` extensions | Partial | Needs directory listing (`getdents`/`readdir`), `stat`, `mkdir`, write file, rename, remove. |
| 9 | `process` | Missing | Spawn, wait, pipes (package manager runs `git`). Linux via raw `fork`/`execve`; macOS may need libSystem via `@library` in `std/sys/macos.xl`; Windows `CreateProcess`. |
| 10 | Args and environment | Unverified | `main` arguments and env lookup. |
| 11 | TOML parser | Missing | Currently in C++ (`src/pkg/toml.cpp`); must be ported. |
| 12 | Semver and git client | Missing | Currently in C++ (`src/pkg/`); must be ported. |
| 13 | Time | Unverified | Cache expiry, timestamps in lock files. |
| 14 | Bitset / small vector | Missing | Sema flags, dense sets. |

## Order

1. `Option`/`Result`, `Span`, `HashMap`, arena.
2. `fs` extensions, `process`, args/env.
3. Ports of TOML, semver, git (only when the package manager moves to xlang).
