# Standard library

The standard library is written in xlang and is embedded in the compiler. A program imports it with `from "std" import ...`, or a module by path, for example `from "std/str" import replace`.

## Modules

- `std/str`: byte views (`fromParts`, `view`), comparison and glob matching, and the owned `String` type with the text functions. See [Strings](strings.html).
- `std/collections`: `Vec<T>`. See [Collections](collections.html).
- `std/random`: OS entropy, a ChaCha20 generator, Xoshiro and SplitMix generators, UUIDs of every version and other identifiers. See [Random and identifiers](random.html).
- `std/sys`: the operating system layer (system calls on Linux and macOS, library calls on Windows). See [System calls](syscalls/).
- `std/encoding`: hexadecimal, Base64, Base32, Base58, Base85, PEM, DER, UTF-8, CSV, JSON, TOML, CBOR, MessagePack, Protocol Buffers wire primitives, and Punycode. See [Encodings](encoding.html).
- `std/compress`: DEFLATE, gzip and zlib, both decoding (all block types) and encoding (fixed Huffman and stored). See [Compression](compress.html).
- `std/auth`: HS256 JWS tokens and AWS SigV4 signing. See [Authentication tokens](auth.html).
- `std/time`: calendar arithmetic and RFC 3339 timestamps. See [Date and time](time.html).
- `std/semver`: Semantic Versioning 2.0.0 parsing and precedence. See [Versions](semver.html).
- `std/net/proto`: NTP, syslog and SOCKS5 messages. See [Network protocol messages](protocols.html).
- `std/crypto`: SHA-1, SHA-2, MD5, ChaCha20-Poly1305, AES-128-GCM, HMAC, HKDF, ECDSA, RSA signatures, X25519 and X.509 parsing.
- `std/checksum`: CRC-32, CRC-32C, Adler-32 and FNV-1 and FNV-1a.
- `std/net`, `std/tls`, `std/http`: sockets, TLS and HTTP. See [HTTP](http.html) and [Event polling](event-loop.html).
- `std/runtime`: panic, unwinding and stack switching.

Every function is documented in its file header, which names the specification it implements (RFC number or official page).
