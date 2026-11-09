# Standard library

The standard library is written in xlang and is embedded in the compiler. A program imports it with `from "std" import ...`, or a module by path, for example `from "std/str" import replace`.

## Modules

- `std/str`: byte views (`fromParts`, `view`), comparison and glob matching, and the owned `String` type with the text functions. See [Strings](strings.html).
- `std/collections`: `Vec<T>`. See [Collections](collections.html).
- `std/random`: OS entropy, a ChaCha20 generator, Xoshiro and SplitMix generators, UUIDs of every version and other identifiers. See [Random and identifiers](random.html).
- `std/sys`: the operating system layer. See [System calls](syscalls/).
- `std/encoding`: hexadecimal, Base64, Base32, Base58, Base85, PEM, DER, UTF-8, CSV, JSON, TOML, CBOR, MessagePack, Protocol Buffers wire primitives, and Punycode. See [Encodings](encoding.html).
- `std/compress`: DEFLATE, gzip and zlib, both decoding (all block types) and encoding (fixed Huffman and stored). See [Compression](compress.html).
- `std/auth`: HS256 JWS tokens and AWS SigV4 signing. See [Authentication tokens](auth.html).
- `std/time`: calendar arithmetic and RFC 3339 timestamps. See [Date and time](time.html).
- `std/semver`: Semantic Versioning 2.0.0 parsing and precedence. See [Versions](semver.html).
- `std/net/proto`: NTP, syslog and SOCKS5 messages. See [Network protocol messages](protocols.html).
- `std/crypto`: SHA-1, SHA-2, SHA-3, MD4, MD5, BLAKE2, HMAC and the MAC interface; AES modes (CBC, CTR, ECB, CCM, XTS), AES-GCM, ChaCha20-Poly1305 and Salsa20; HKDF, PBKDF2 and scrypt; X25519 and X448; RSA, ECDSA, Ed25519 and Ed448; X.509, CRL, CSR, SPKI and key parsing. See [Cryptography](crypto.html).
- `std/checksum`: CRC-32, CRC-32C, Adler-32 and FNV-1 and FNV-1a. See [Encodings](encoding.html).
- `std/net`, `std/tls`, `std/http`: sockets, TLS 1.3, and the HTTP server and client. See [HTTP](http.html) and [Event polling](event-loop.html).
- `std/math`: integer and float helpers, fixed-width big integers, and the geometry types `Vec2`, `Vec3`, `Vec4`, `Rect`, `Mat2`, `Mat3`, `Mat4`, `Quaternion` and `Transform`. See [Mathematics](math.html).
- `std/io`: `print`, `println`, `fprint` and the other output functions, whole-file helpers, directory listing and file status.
- `std/mem`: `memcpy`, `memmove`, `memset` and `memcmp`, the `Allocator` interface, the default heap and `currentAllocator`.
- `std/thread`: threads, `Mutex`, `Cond`, `RwLock`, `Once`, and the cooperative scheduler.
- `std/os`: process control (exit, the process id, environment access through `std/sys`).
- `std/cpu`, `std/gpu`, `std/hw`: CPU feature flags, GPU device discovery, and hardware facts the OS publishes. See [CPU features](cpu.html) and [System information](system.html).
- `std/cloud`: an S3 client. See [Cloud storage](cloud.html).
- `std/runtime`: panic, unwinding and stack switching. See [Runtime]({{ site.baseurl }}/compiler/runtime.html).
- `std/testing`: the test framework. See [Testing]({{ site.baseurl }}/compiler/testing.html).
- `std/sys`: the native OS layer (system calls on Linux and macOS, library calls on Windows). User code reaches it only through the areas above. See [System calls](syscalls/).

Every function is documented in its file header, which names the specification it implements (RFC number or official page).
