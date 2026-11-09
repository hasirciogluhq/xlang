# std/random

`std/random` gives random bytes, a cryptographically secure generator, non-cryptographic generators, UUIDs and
other identifiers. Random bytes always come from the operating system (`randomBytes`, `sysRandom`), so no
generator state needs to be seeded by the program.

## Entropy and generators

- `randomBytes(out, n)` fills `n` bytes from the OS (getrandom on Linux, getentropy on macOS, BCryptGenRandom on Windows).
- `csprng`: ChaCha20 with fast key erasure, reseeded from the OS after `CSPRNG_RESEED_BYTES`.
- `prng`: Xoshiro256** seeded through SplitMix64 (Blackman and Vigna), with unbiased bounded integers (Lemire's method).

## UUIDs (RFC 9562)

- `uuidV3(out, ns, name, n)` and `uuidV5(out, ns, name, n)` hash the 16-byte namespace and the name with MD5 and SHA-1.
- `uuidV4(out)` and `uuidV7(out, unixMillis)` take random bits; v7 stores the millisecond time the caller gives.
- `uuidV8(out, custom)` takes 16 caller bytes and sets version and variant.
- `UuidTimeGen` makes v1 (`v1`) and v6 (`v6`) UUIDs. Its node is random with the multicast bit set (RFC 9562 section 6.10),
  and its clock sequence increases when the clock goes back. Two calls never return the same value.
- `uuidNil`, `uuidString` (36 lowercase characters), `uuidParse` (canonical form, either case), `uuidVersion`,
  `uuidIsRfcVariant`, and the namespaces `UUID_NS_DNS`, `UUID_NS_URL`, `UUID_NS_OID`, `UUID_NS_X500` (RFC 9562 Appendix B).

Time is always the caller's: `std/sys` gives seconds only.

## Other identifiers (`ids`)

- `ulid(unixMillis) -> String`: 26 Crockford base32 characters (ulid/spec). `UlidGen.next` is monotonic within one millisecond.
- `cuidV1(unixMillis) -> String`: 25 characters starting with `c` (paralleldrive/cuid). The fingerprint is random per process,
  not a host hash, because no hostname is read.
- `cuidV2(unixMillis) -> String`: 24 characters (paralleldrive/cuid2). Deviation: the hash is SHA-256, so values differ from
  the reference, which uses SHA-3. The reference default length is kept.
- `nanoid(size, alphabet) -> String`: rejection sampling over a mask (ai/nanoid). The reference default alphabet is
  `_-0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ` with size 21.
- `Snowflake`: 41-bit milliseconds since a custom epoch, 10-bit machine id, 12-bit sequence. Panics on a backwards clock or more than
  4096 IDs in one millisecond.
- `ksuid(unixSeconds) -> String`: 27 base62 characters (segmentio/ksuid), epoch 1400000000.
- `ObjectIdGen.next(unixSeconds) -> String`: 24 hex characters, MongoDB ObjectId (BSON specification). Old design, still the database default.
- `XidGen.next(unixSeconds) -> String`: 20 base32hex characters (rs/xid). Machine and pid bytes are random per process, not host-derived.
- `Sonyflake`: 39 bits of 10 ms units, 8-bit sequence, 16-bit machine id (sony/sonyflake).
- `InstagramIdGen`: 41-bit milliseconds, 13-bit shard, 10-bit sequence (Instagram engineering blog, 2012). The epoch is the caller's.
- `tsid(unixMillis) -> ui64`: 42-bit milliseconds since 2020-01-01 and 22 random bits (tsid-creator).
- `PushIdGen.next(unixMillis) -> String`: 20 characters, Firebase push ID, monotonic within one millisecond.
- Typed random values: `randomU32`, `randomI64`, `randomF64` (53 bits, in [0, 1)), `randomBelowU64(bound)` (no modulo bias).

Section headings in the catalogue (std.plan.md section 8b) list every other generator, UUID version and ID family that is still planned.

Verified against known vectors: MD5 of the empty string, SHA-1 of "abc", UUID v3 and v5 of `www.example.com` in the DNS namespace.
