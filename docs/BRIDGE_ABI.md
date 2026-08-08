# Bridge ABI (`xl_*`)

Bridge helpers (userspace) use a fixed naming rule. Raw kernel syscalls and foreign `declare` symbols are **not** under this rule.

| Rule | Correct | Incorrect |
|------|---------|-----------|
| Prefix `xl_` | `xl_thread_start` | `thread_start`, `xlang_thread_start` |
| `<object>_<action>` | `xl_socket_listen` | `xl_listen_socket`, `xl_start_thread` |
| Underscored domain | `xl_thread_start` | `xlthread_start` |
| Bridge ≠ raw syscall | `xl_thread_start` (helper) | compiler knowing kernel `clone` numbers |

## Two call paths

1. **Blind declare/call (bridges / C ABI)** — plain `declare name(...)` (or `declare fn name(...)`) → LLVM `declare` + `call`. No whitelist. Link/embed resolves symbols (`xl_thread_start`, `pthread_create`, user stubs…). **Not** `declare syscall`.
2. **CPU-native syscall** — `declare syscall <number> name(...)` defines a wrapper that emits the arch trap, or `@syscall(number, args...)` inline. `x86_64`: `syscall` · `aarch64`: `svc #0`. Number/ABI is the user’s; works with `--no-runtime` / `--no-bridge`.

## Layout

```
src/runtime/bridge/{linux,macosx,windows}/
  filesystem.c  net.c  tls.c  process.c  time.c  panic.c  thread.c
```

Same `xl_*` names on every OS; arch via `XLANG_ARCH_X64` / `X86` / `ARM64`.
