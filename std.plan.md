# std plan

Only open work is listed here. Finished work lives in the code, `xlang-docs/std/` and git history.

## Rules
- API style follows CLAUDE.md ("Stdlib style", "Std API types"): typed returns (`String`, struct, `Vec<T>`), no module prefix on names, raw pointers and `str` only at byte boundaries.
- Every function follows its RFC or official spec; the file header cites it.
- Code, `xlang-docs/` and `src/README.md` change in the same step.
- Docs first for any syscall or OS call (`xlang-docs/syscalls/`).
- Tests are added at the end: unit tests for platform-independent logic, e2e for OS-dependent code. Run each full suite once.
- Deferred items need an entry and a Turkish feature assessment in `xlang-docs/known-limitations.md`.

## Queue (top to bottom)

### 1. Small independent items
- Checksums: `crc8`, `crc16`, `crc64` (CRC catalogue), `murmurhash3` (32, 128), `siphash` (2-4, 4-8).
- IDs: `uuidMax` (RFC 9562 section 5.10), Discord snowflake (epoch constant and wrapper over `Snowflake`).
- PRNGs: PCG32 and PCG64, Xoshiro256++ and Xoshiro128, Xorshift64*, MT19937, MINSTD.
- CSPRNGs: HMAC_DRBG (SP 800-90A 10.1.2), Hash_DRBG, CTR_DRBG over AES-256.
- Distributions: normal (Ziggurat), exponential, gamma, beta, binomial, Poisson, reservoir sampling.
- Ciphers: AES-256 GMAC (check `gmacAes` key lengths).

### 2. CPU and runtime
- CPU: RDRAND/RDSEED and ARM RNDR as extra entropy input only, `rdtsc`/`cntvct` wrapper (via `__builtin_asm`), `membarrier` (Linux 324).
- Fibers: `std/thread/fiber` scheduler on `std/runtime/context`, stacks with guard pages (mmap PROT_NONE; VirtualAlloc PAGE_GUARD), `sigaltstack` for SIGSEGV on a guard hit.
- Windows IOCP: only if a completion API is chosen (see D2).

### 3. HTTP and cloud
- WebSocket (RFC 6455): handshake and frames.
- Static files and range requests (RFC 9110 section 14).
- Prometheus text exposition endpoint (health and metrics).
- Instance metadata client (link-local endpoints, configurable base).
- HTTP/2 with HPACK (RFC 9113, 7541), then HTTP/3 with QPACK (RFC 9114, 9204): each needs its own design pass.

### 4. Platform docs
- macOS Mach trap table (`osfmk/mach/syscall_sw.h`) and the arm64 syscall register convention.
- Windows: documented Win32 functions per area, each checked on Microsoft Learn.

### 5. To verify (not clear from a code search)
- `std/gpu` and `std/hw`: Linux DRM ioctls, sysfs, SMBIOS, `getauxval` coverage against the old sections 17 and 18.
- CFB/OFB (planned as `defer`): confirm absent.

### 6. Final
- Write the Turkish assessments for all `defer` items.
- Type-check: `xlc xl-std/index.xl --check -DfeatureDefaultAllocator=true`.
- Run the unit suite and the e2e suite once; fix what they find.

## Open decisions (recommendation first; the user decides)
- D1 macOS threads: raw `bsdthread_*` fails outside libSystem. Recommendation: create threads through `@library("System")` pthread functions.
- D2 Windows events: keep WSAPoll; IOCP only if a completion API is needed.
- D3 Linux dynamic linking (PT_INTERP, PLT/GOT, DT_NEEDED): defer; static `[[native]]` archives cover current use.
- D4 GPU on macOS (Metal needs the Objective-C runtime): defer; Linux DRM first.
- D5 cuid2 hash: switch from SHA-256 to SHA-3 now that SHA-3 exists, and drop the deviation note.
- D6 `print` on Windows keeps ws2_32 linked. Recommendation: accept.

## Deferred (no code now)
- Hashes: blake3, xxhash and other non-standard hashes, ripemd, whirlpool, tiger, sm3, gost, streebog.
- Ciphers: legacy block and stream ciphers (DES, RC4, Blowfish, Twofish, Camellia, SM4, ARIA, SEED, CAST5, IDEA, RC2, RC5, RC6, Serpent, Kuznyechik), OCB, CFB/OFB.
- Signatures and KDFs: DSA, SM2, Argon2, bcrypt.
- PKI: OCSP request/response, PKCS#8 and PKCS#12 encryption.
- Protocols: QUIC, DTLS, gRPC, CoAP, LDAP, Kerberos, SNMP.
- GPU: Vulkan, CUDA, OpenCL, ROCm, SPIR-V, IOKit, Metal.
- Formats: zstd, brotli, LZ4, Snappy, YAML, Sqids/Hashids, WELL512, Philox/Threefry, ChaCha8/12, Fortuna.
- Never: Dual_EC_DRBG, RC4 as a default.

## Language ergonomics (proposals, not built)
Each item needs a design (syntax, sema, codegen) first; stdlib is preferred over compiler builtins.

Built: slice syntax (`s[a..b]`, `auth[7..]`).

Order: 25.12 and 25.11, then 25.8, 25.5, 25.4, 25.1 with 25.3, 25.2, 25.6, 25.10, 25.7.

| # | Proposal | Replaces |
|---|----------|----------|
| 12 | String-first stdlib: `String` in APIs, `str` only as a boundary view (`asStr`, `String.from`, `toString`) | three text types |
| 11 | Local inference `local x := f()` | explicit type on every local |
| 8 | `for x, i in s` over collections and iterators (stdlib iterators) | index loops |
| 5 | `buf[N]string` fixed buffer with length | `title: [u8; 120], titleLen` |
| 4 | `parse<T>()` on `str`/`String` with position and reason | hand-written parsers |
| 1 | `else` / `else \|e\|` on `?T` and fallible calls | error as bool |
| 3 | `?` propagation (with 1) | manual error returns |
| 2 | `match` as the only multi-way branch (literal, `_`, binding, range, `or`, guard, `Some`/`None`, enum payload, struct) | `switch` |
| 6 | `json"..."` compile-time literal expanded to an encoder call | hand-escaped JSON |
| 10 | Operators on std types; `copy` stays stdlib (`std/mem`) unless the compiler change is approved | `eq(fromC(x), "t")` |
| 7 | Route block `r.group("/api", mw) { ... }` (needs trailing block and closure capture) | repeated route calls |
