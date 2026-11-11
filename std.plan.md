# std implementation plan

Status keys:
- `done`: code exists in the tree under the name shown.
- `wip`: code is written this session but not yet type-checked by the compiler (`xmake build std`) or verified.
- `todo`: not started.
- `defer`: not started on purpose; the decision and a Turkish feature assessment go to xlang-docs/known-limitations.md.

Names are plain camelCase (`x509Parse`, not `xl_x509_parse`). No e2e or test file is added until every item is `done` or `defer`.
The three test files already written (`test/e2e/encodings.xl`, `random.xl`, `checksums.xl`) are not run again until then.

## Rules
- API style (CLAUDE.md "Std API types"): typed returns (`String`, struct, class, `Vec<T>`); no module prefix on names (`slice`, not `strSlice`); raw pointers and `str` only at byte boundaries.
- Migration of the existing `str` API to typed returns (for example `slice`, `trim`, `split` returning views today) is a queued item: decide per function, record in KNOWN-LIMITATIONS if a view is kept on purpose.
- Every function follows its RFC or official spec, and the file header cites it.
- `xlang-docs/`, `xlang-docs/`, `src/README.md`, `xlang-docs/known-limitations.md` change in the same step as the code.
- Compiler check: `xmake build std`. Full suite runs once at the end.

## Documentation site (done)
- GitHub Pages: `xlang-docs/` is the site. `xlang-docs/_config.yml` (Jekyll, theme minima, plugins jekyll-optional-front-matter and jekyll-relative-links), `xlang-docs/index.md` (landing page), `xlang-docs/.github/workflows/pages.yml` (actions/jekyll-build-pages, then deploy-pages; deployed from the xlang-docs repository). Built locally with Jekyll once: all pages and relative links render (`.html`).
- One-time repository setting: Settings > Pages > Source: "GitHub Actions".
- `xlang-docs/` stays out of the site on purpose (AI-facing). `xlang-docs/known-limitations.md`, `CLAUDE.md` and this plan stay at the root because the project rules name them; the site links to KNOWN-LIMITATIONS on GitHub.
- New docs pages must be added to `xlang-docs/index.md` in the same change.

## Current queue (read this first; the numbered sections below are the detailed catalogue)
Work runs top to bottom. An item is finished when its code type-checks (`xmake build std`, and `xlang check --target` for std/sys files), its xlang-docs/ and docs-ai/ pages and header comments match, and its KNOWN-LIMITATIONS entries exist. Tests and e2e files are added only after the whole queue is finished.

Q1. DONE. Verified by scratch programs (not added to the repo): ULID, cuid v1 and v2 shapes, nanoid, Snowflake, KSUID, ObjectId, XID, Sonyflake, Instagram, TSID, PushID; MD5("") and SHA-1("abc") and UUID v3 and v5 (DNS, www.example.com) match known vectors; string helpers `compare`, `equalIgnoreCase`, `containsAny`, `trimStart`, `trimEnd`, `globMatch`, `replace`, `replaceAll`, `repeat`, `slice`, `trim`, `split` work. Item list kept below for reference:
  - IDs: `std/random/ids.xl` with cuidV1, cuidV2 (SHA-256 until sha3 exists; deviation recorded), ulid with a monotonic generator, nanoid (default and custom alphabet, mask rejection sampling), snowflake (epoch, machine id, sequence), ksuid. Export from `std/random/index.xl`.
  - UUID: check uuidV1, uuidV3, uuidV5, uuidV6, uuidV8, namespaces and uuidParse in `std/random/uuid.xl` type-check on all targets.
  - Hash vectors: md5 (RFC 1321 appendix A.5) and sha1 (RFC 3174 section 7.3) compile; their test vectors are checked only in the final test pass.
  - Strings: strCompare, strEqualIgnoreCase, strContainsAny, strTrimStart, strTrimEnd, strReplace, strRepeat, strGlobMatch and the matching String methods in `std/str/str.xl` and `std/str/string.xl`.
Q2. DONE. Hashes and MACs, all checked against published vectors: sha224 (FIPS 180-4, `abc` vector), sha512t224 and sha512t256 (SHA-512/t, `abc` vectors), md4 (RFC 1320 vectors), sha3224/256/384/512 and shake128/256 (FIPS 202 vectors), blake2b and blake2s (RFC 7693 `abc` vectors), generic `hmac(alg, ...)` over every digest id (RFC 2104; HMAC-SHA-256, MD5 and SHA-1 RFC/Wikipedia vectors), cmacAes128 (RFC 4493 vectors), gmacAes128 (AES-128 only), cshake128/256 and kmac128/256 (NIST SP 800-185 sample vectors). Open inside Q2: AES-256 variants of CMAC and GMAC (after Q4), streaming HMAC over long messages, BLAKE2 keyed MAC vectors beyond the unkeyed one.
Q3. DONE. base32 and base32hex (RFC 4648 sections 6 and 7; RFC 4648 vectors), base85 (RFC 1924, computed vectors) and ascii85 (Adobe; "Man " -> "9jqo^"), constantTimeEq and secureZero (`std/crypto/ct`), poly1305 (RFC 8439 section 2.5.2 vector; the AEAD's internal Poly1305 got the partial-block fix that the standalone form needs). Docs: `xlang-docs/std/encoding.md`, `xlang-docs/std/crypto.md`.
Q4. DONE. Ciphers, every vector checked: a shared AES core for 16-, 24- and 32-byte keys (`cipher/aes.xl`, FIPS 197 encryption and inverse cipher; the AES-256 block is checked through SP 800-38A F.1.5); ECB, CBC, CTR (NIST SP 800-38A F.1.1, F.2.1, F.5.1 vectors), CCM (RFC 3610 packet 1), XTS (IEEE 1619 vector 1, AES-128), GCM-192 and GCM-256 (GCM spec test cases 13 and 14 tags), CMAC-256 (SP 800-38B example); XChaCha20 with HChaCha20 (draft vector), XChaCha20-Poly1305 (round trip; no published vector checked), Salsa20/20 (eSTREAM vector) and XSalsa20 (NaCl vector). Open inside Q4: XTS ciphertext stealing, XChaCha20-Poly1305 vector, AES-CFB/OFB/OCB, AES-192 CCM/XTS vectors.
Q5. DONE for the curves named here (Ed448, P-521 and secp256k1 added with openssl checks: public keys, deterministic Ed448 signatures, ECDSA verification, ECDH). Earlier notes follow. Done and checked: Ed25519 (`sig/ed25519.xl`; RFC 8032 test 1 public key, signature and verification, a bit-flipped signature is rejected); ECDH on P-256 and P-384 (`ecdhPublicKey`, `ecdhShared`; 2G x-coordinate `7cf27b18...` and d = 1 check); ECDSA key generation (`ecdsaKeypair`, P-256 and P-384); RSA PKCS#1 v1.5 and OAEP encryption and decryption (`sig/rsaenc.xl`; checked against OpenSSL in both directions with a 2048-bit key); `mgf1` now handles any seed length (the old copy in rsa.xl had an 80-byte buffer and was removed). Found and fixed while doing it: `bnFromBytes` takes big-endian bytes, so little-endian encodings are converted with `leToLimbs`. RSA key generation (`sig/rsakeygen.xl`: random primes with trial division and 8 Miller-Rabin rounds; d = (k phi + 1) / e with k = -phi^-1 mod e, so no big-number inverse is needed; a 2048-bit key takes about 1 s; the output is checked by round trip only). X448 done (`kex/x448.xl`, RFC 7748 section 6.2 public key). Open: Ed448 (14-limb field, Shake256, a = 1 formulas), P-521 (needs the `Pt` arrays widened from 12 to 17 limbs and `size / 4` changed to rounded-up limbs), secp256k1 (a = 0 needs its own doubling formula).
Q6. DONE with two open checks. pbkdf2 over any digest (RFC 6070 SHA-1 vectors, c = 1 and c = 2), hkdf generic over any digest (RFC 5869 A.1, SHA-256; SHA-512 and SHA-3 use the same code, no vector), scrypt (RFC 7914: both section 12 vectors), concat_kdf and x963_kdf (written, no vector: KNOWN-LIMITATIONS). HKDF over BLAKE2 is not defined. The KDFs live in `std/crypto/kdf`.
Q7. IN PROGRESS. Done: X.509 accessors and checks in `x509/names.xl` (subject, issuer, signature, RSA modulus and exponent, EC point as byte views; isCa, pathLen, selfSigned, checkEmail, checkIp over the SAN list; SHA-256 and SHA-1 fingerprints); TLS version and cipher suite names with the IANA values (`std/tls/names.xl`); PEM encoder `pemEncode` (`encoding/pemwrite.xl`, RFC 7468 layout checked on a small block). Open: CSR create, parse and sign (RFC 2986), CRL parse and verify (RFC 5280 section 5), DER encoder (X.690 definite lengths), key serialization (SubjectPublicKeyInfo and PKCS#8 to and from PEM), public names `x509Verify` and `x509ChainVerify` over the existing path code.
Q8. DONE with accuracy notes. Integers (`math/integer.xl`: gcd, lcm, factorial, binomial, modPow, modInverse, isPrime (deterministic for 64-bit), nextPrime, legendre, jacobi, crt, abs/sign/clamp); big integers (`math/bigint.xl` and `math/bigdiv.xl`: add, sub, mul, Knuth Algorithm D divide, pow, gcd, modPow, modInverse, probable prime; the division identity q v + r = u was tested on 200 random inputs); floating point (`math/float.xl`, `math/trig.xl`: floor, ceil, trunc, round, fmod, remainder, modf, copysign, nextafter, frexp, ldexp, exp, exp2, log, ln, log2, log10, pow, cbrt, hypot, sin, cos, tan, atan, atan2, asin, acos, sinh, cosh, tanh, asinh, acosh, atanh, sqrt in math.xl); vectors and matrices (`math/vector.xl`: dot, cross, norm, normalize, lerp, smoothstep, mapRange, degree and radian conversion, matAdd, matSub, matMul, matTranspose, matDet, matInverse). Floating-point values agree with bc -l to 1e-9 on sample points; full double accuracy is not verified (see KNOWN-LIMITATIONS). Open: bigint lcm, matrix sizes beyond the explicit dimensions (no fixed-size types yet). Docs: `xlang-docs/std/math.md`.
Q9 progress: JWS HS256 and JWT (`auth/jwt.xl`, RFC 7515 appendix A.1 token reproduced exactly); AWS SigV4 keyed steps (`auth/sigv4.xl`, AWS ListUsers example signature reproduced); base64url unpadded codecs (`base64UrlEncodeRaw` and `base64UrlDecodeRaw`, RFC 7515 section 2); JSON full validator, RFC 6901 pointer lookup, RFC 8259 string decoding and a JsonWriter (`encoding/jsonfull.xl`, checked on the RFC 6901 example document); UTF-8 decode, validate and encode (`encoding/utf8.xl`, RFC 3629); CSV record reader (`encoding/csv.xl`, RFC 4180); DEFLATE, gzip and zlib decoders (`compress/inflate.xl`, RFC 1951/1952/1950) checked on fixed, dynamic and stored blocks; DER TLV writer and minimal INTEGER (`encoding/der.xl`, X.690). Compression (writing) is not implemented. Later progress: Base58 (`encoding/base58.xl`, Bitcoin alphabet; "Hello World!" gives 2NEpo7TZRRrLZSi2U and leading zeros map to "1"); CBOR encoders (`encoding/cbor.xl`, RFC 8949 shortest-form heads, checked on a string header length; decoder not yet); Protocol Buffers varint, zigzag, tag and fixed values (`encoding/protobuf.xl`, 300 gives AC 02, zigzag -1 to 1 and -2 to 3); SemVer 2.0.0 parse and precedence (`semver/semver.xl`, pre-release ordering and build metadata rules checked); date and time (`time/date.xl`, RFC 3339, checked against system date). Remaining Q9 list below.
Q9. Platform and runtime (the rest of sections 13 to 24, each in its own area):
  - Syscall docs: macOS Mach trap table and the arm64 register definition; Windows Win32 function list per area, each checked on Microsoft Learn.
  - Event backends: io_uring completion backend with epoll fallback (section 14); Windows IOCP only if a completion API is chosen (see open decisions).
  - Threads on macOS through libSystem (see open decisions), then fiber scheduler and guard pages (section 15).
  - CPU feature detection (section 16), GPU and hardware (sections 17, 18) for Linux first.
  - Networking protocols (section 19), HTTP/TLS audit (section 20), cloud and auth (section 21), system services (section 22), data formats and compression (section 23).
  - Linux static archive search path for `[[native]]` (section 24); dynamic Linux linking stays deferred.
Q10. Final: write the Turkish feature assessments for every `defer` item in xlang-docs/known-limitations.md; run `xmake build std`; run the full e2e suite once and fix what it finds.

## Open decisions (each has a recommendation; the user decides)
- D1 macOS threads: the raw `bsdthread_*` path fails outside libSystem (KNOWN-LIMITATIONS "macOS raw threads"). Recommendation: create threads through `@library("System")` pthread functions now that libSystem linking exists. Needs your approval because it changes the thread model.
- D2 Windows event model: WSAPoll (readiness, done) versus IOCP (completion, not started). Recommendation: keep WSAPoll; IOCP only when a completion API is needed.
- D3 Linux dynamic linking (PT_INTERP, PLT/GOT, DT_NEEDED): recommendation defer; static `[[native]]` archives cover the current use.
- D4 GPU on macOS (Metal needs the Objective-C runtime): recommendation defer; Linux DRM first.
- D5 cuid2 hash: SHA-256 until SHA-3 lands in Q2; afterwards switch to SHA-3 and drop the deviation note.
- D6 `print` on Windows keeps ws2_32 linked (its socket branch is reachable). Recommendation: accept; a file-only print would need a separate stdout API.

## Deferred (no code now; reasons in xlang-docs/known-limitations.md)
xxhash, highwayhash and other non-standard hashes; blake3; ripemd, whirlpool, tiger, sm3, gost, streebog; legacy ciphers (DES, RC4, Blowfish, Twofish, Camellia, SM4, ARIA, SEED, CAST5, IDEA, RC2, RC5, RC6, Serpent, Kuznyechik); OCB and CFB/OFB; DSA and SM2; Argon2 and bcrypt; OCSP request and response; PKCS#8 and PKCS#12 encryption; QUIC and DTLS; Vulkan, CUDA, OpenCL, ROCm; SPIR-V; IOKit and Metal; gRPC; CoAP; LDAP, Kerberos, SNMP; zstd, brotli, LZ4, Snappy; YAML; zlib-style Huffman tables beyond inflate and fixed-table deflate.

## 1. Checksums (`std/checksum`)
- crc32, crc32Update (IEEE, reflected): wip
- crc32c, crc32cUpdate (Castagnoli): wip
- adler32, adler32Update (RFC 1950 section 8.2): wip
- fnv1a32, fnv1_32, fnv1a64, fnv1_64 (draft-eastlake-fnv): wip
- fnv1_32 and fnv1a_32 and fnv1_64 and fnv1a_64: names in the list map to the camelCase functions above (`fnv1a32`, `fnv1_32`, `fnv1a64`, `fnv1_64`): wip
- crc8, crc16, crc64 (CRC catalogue, reflected and non-reflected variants): todo
- murmurhash3_32, murmurhash3_128 (Appleby, public reference): todo
- siphash_2_4, siphash_4_8 (Aumasson and Bernstein, siphash.org): todo
- xxhash32, xxhash64, xxhash3_64, xxhash3_128: defer (no RFC; large spec)
- highwayhash, cityhash, farmhash, spookyhash, metrohash, t1ha, wyhash: defer (no standard, many variants)

## 2. Hashes (`std/crypto/hash`)
- sha256 (FIPS 180-4): done
- sha384, sha512 (FIPS 180-4): done
- sha224, sha512_224, sha512_256 (FIPS 180-4): todo
- sha1 (RFC 3174): wip
- md5 (RFC 1321): wip
- md4 (RFC 1320): todo
- sha3_224, sha3_256, sha3_384, sha3_512, shake128, shake256 (FIPS 202): todo
- blake2s, blake2b (RFC 7693): todo
- blake3: defer (no RFC; reference spec only)
- ripemd128, ripemd160, ripemd256, ripemd320: defer (legacy)
- whirlpool, tiger, tiger2, sm3, gost, streebog256, streebog512: defer (legacy or national; sm3 and streebog reconsidered with sm2)
- digest dispatch (`digest`, `digestLen`) covers sha256, sha384, sha512: done; extend to sha1, sha224, sha3 with their ids: todo
- hmacSha256 (RFC 2104): done (SHA-256 only)
- hmac_md5, hmac_sha1, hmac_sha224, hmac_sha384, hmac_sha512, hmac_sha3_256, hmac_sha3_512, hmac_blake2s, hmac_blake2b, hmac_blake3, hmac_sm3: todo (generic HMAC over the digest dispatch, RFC 2104)
- poly1305 (RFC 8439 section 2.5): todo (the AEAD in chacha20poly1305.xl has the MAC inline; extract it as an exported function)
- cmac_aes128, cmac_aes256 (NIST SP 800-38B): todo
- gmac_aes128, gmac_aes256 (NIST SP 800-38D with empty plaintext): todo
- kmac128, kmac256 (NIST SP 800-185): todo

## 3. Ciphers and AEAD (`std/crypto/cipher`)
- aes128EncryptBlock (FIPS 197): done
- aesGcmSeal, aesGcmOpen (AES-128-GCM, NIST SP 800-38D): done (names map to aes_128_gcm)
- aes_256_gcm, aes_192_gcm: todo (AES-256 key schedule and 192 key schedule)
- chacha20Block (RFC 8439 section 2.3): done (chacha20 and chacha20_ietf)
- aeadSeal, aeadOpen (RFC 8439 section 2.8, ChaCha20-Poly1305): done (chacha20_poly1305)
- xchacha20, xchacha20_poly1305 (draft-irtf-cfrg-xchacha): todo
- aes_ccm, aes_128_ccm, aes_192_ccm, aes_256_ccm (RFC 3610, NIST SP 800-38C): todo
- aes_cbc, aes_128_cbc, aes_192_cbc, aes_256_cbc (NIST SP 800-38A): todo
- aes_ctr, aes_128_ctr, aes_192_ctr, aes_256_ctr (NIST SP 800-38A): todo
- aes_xts, aes_256_xts (NIST SP 800-38E): todo
- aes_128_ecb, aes_192_ecb, aes_256_ecb (NIST SP 800-38A): todo (teaching and block-only use; warn in docs)
- aes_128_ocb, aes_256_ocb (RFC 7253): defer
- aes_128_cfb, aes_192_cfb, aes_256_cfb, aes_128_ofb, aes_192_ofb, aes_256_ofb (NIST SP 800-38A): defer (CTR covers the use cases; revisit on request)
- salsa20, xsalsa20 (Bernstein, eSTREAM): todo
- rc4, rc4_drop: defer (RFC 7465 forbids it in TLS)
- des, des_cbc, des3, des3_cbc (FIPS 46-3, withdrawn): defer
- blowfish, blowfish_cbc, twofish, twofish_cbc, twofish_ctr: defer
- camellia_128, camellia_192, camellia_256 (RFC 3713): defer
- sm4, sm4_cbc, sm4_ctr, sm4_gcm (GB/T 32907): defer
- aria (RFC 5794): defer
- seed (RFC 4269), cast5 (RFC 2144), idea, rc2 (RFC 2268), rc5, rc6, serpent, kuznyechik (GOST R 34.12): defer

## 4. Key agreement and signatures (`std/crypto/kex`, `std/crypto/sig`)
- x25519, x25519Base (RFC 7748): done (x25519_keypair = x25519Base with a random scalar: todo wrapper)
- curve25519_keypair: todo (wrapper over x25519Base)
- x448, x448_keypair, curve448_keypair (RFC 7748): todo
- ed25519_keypair, ed25519_sign, ed25519_verify, ed25519_derive (RFC 8032): todo
- ed448_keypair, ed448_sign, ed448_verify (RFC 8032): todo
- ecdsaSign, ecdsaVerify with ECDSA_P256 and ECDSA_P384 (FIPS 186-4): done
- ecdsa_keypair (random scalar until 0 < d < n): todo
- secp256r1_sign, secp256r1_verify, secp384r1_sign, secp384r1_verify: done (through ecdsaSign and ecdsaVerify with the curve id)
- secp256r1_keypair, secp384r1_keypair: todo (wrapper over ecdsa_keypair)
- secp256r1_ecdh, ecdh (P-256 and P-384, SP 800-56A): todo
- secp521r1_keypair, secp521r1_sign, secp521r1_verify: todo (needs P-521 field)
- secp256k1_keypair, secp256k1_sign, secp256k1_verify, secp256k1_ecdh (SEC 2): todo
- rsaVerifyPkcs1 (RFC 8017 section 8.2): done
- rsaVerifyPss, rsaSignPss (RFC 8017 section 8.1): done
- rsa_keypair, rsa_encrypt, rsa_decrypt, rsa_sign (PKCS1 v1.5): todo (needs private key ops and key generation)
- rsa_oaep_encrypt, rsa_oaep_decrypt (RFC 8017 section 7.1): todo
- rsa_pss_verify: done as rsaVerifyPss
- dsa_keypair, dsa_sign, dsa_verify (FIPS 186-5): defer
- sm2_keypair, sm2_sign, sm2_verify, sm2_encrypt, sm2_decrypt (GB/T 32918): defer

## 5. Key derivation (`std/crypto/kdf`)
- hkdfExtract, hkdfExpand, hkdfExpandLabel (RFC 5869, RFC 8446 section 7.1, SHA-256): done
- hkdf_sha512 (RFC 5869 over SHA-512): todo
- hkdf_sha3_256, hkdf_blake2b: todo (after sha3 and blake2)
- pbkdf2_sha256 (RFC 8018 section 5.2): todo (HMAC-SHA-256 exists; pbkdf2 is a loop over it)
- pbkdf2_sha1, pbkdf2_sha512: todo
- pbkdf2_md5: defer (no security value; RFC 8018 does not define it)
- scrypt (RFC 7914): todo (needs salsa20/8 and pbkdf2_sha256)
- argon2i, argon2d, argon2id (RFC 9106): defer (memory-hard; large, needs a design pass)
- bcrypt: defer (no RFC; OpenBSD format)
- concat_kdf (NIST SP 800-56C): todo
- x963_kdf (ANSI X9.63, SEC 1 section 3.6.1): todo

## 6. X.509, PKI, PEM, DER and TLS (`std/crypto/x509`, `std/encoding/pem`, `std/tls`)
- derRead (ITU-T X.690 TLV reader): done
- x509Parse (RFC 5280 section 4): done; the fields of `Cert` give subject, issuer, serial, validity, public key, signature and extensions
- x509_get_subject, x509_get_issuer, x509_get_serial, x509_get_not_before, x509_get_not_after, x509_get_public_key, x509_get_signature, x509_get_extensions: wrappers over the `Cert` fields; todo as named functions
- x509_is_ca (basicConstraints, RFC 5280 section 4.2.1.9): todo as named function (check `Cert` for the flag)
- x509VerifySignature, x509CheckSignedBy, x509IssuedBy (RFC 5280 section 6.1): done
- x509_verify: todo as the public name (wraps x509VerifySignature)
- x509_chain_verify: todo as the public name (wraps trustVerifyChain in tls/truststore)
- x509_self_signed: todo (issuer equals subject and the signature verifies with the own key)
- x509MatchesHost (RFC 6125 section 6, SAN dNSName): done as x509_check_host
- x509_check_email (RFC 5280 section 4.2.1.6, rfc822Name): todo
- x509_check_ip (RFC 5280 section 4.2.1.6, iPAddress): todo
- x509ValidAt (RFC 5280 section 4.1.2.5): done (validity window check)
- expired, valid (wrappers over x509ValidAt): todo
- revoked (RFC 5280 section 5): depends on crl_verify: todo
- pemNext (RFC 7468 section 3): done as pem_decode (block extraction)
- pem_decode (RFC 7468): todo as public name; pemNext covers the parse step
- pem_encode (RFC 7468 section 2): todo
- der_decode: todo as public name (derRead covers the TLV step)
- der_encode (X.690 definite length): todo
- privKeyParsePem (RFC 5915, RFC 5208, RFC 8410 for private keys): done for PEM
- private_key_parse (PEM and DER): todo as public name (DER branch and dispatch)
- private_key_to_pem, public_key_parse, public_key_to_pem (RFC 5280 SubjectPublicKeyInfo): todo
- csr_create, csr_parse, csr_sign (RFC 2986 PKCS #10): todo
- crl_parse, crl_verify (RFC 5280 section 5): todo
- ocsp_request, ocsp_response, ocsp_verify (RFC 6960): defer (needs network policy; parse-only is the first step)
- cert_fingerprint_sha256: todo (SHA-256 over the DER certificate, done digest)
- cert_fingerprint_sha1: todo (depends on sha1: wip)
- pkcs8_parse, pkcs8_encrypt, pkcs8_decrypt (RFC 5958, RFC 8018 PBES2): defer (needs PBES2 ciphers)
- pkcs12_parse, pkcs12_create (RFC 7292): defer (needs PBES1 and legacy ciphers)
- tls_version (RFC 8446 section 4.2.1, RFC 5246 appendix A.5 for legacy values): todo
- tls_cipher_suites (IANA TLS Cipher Suite registry): todo
- certificate, certificate_chain, trust_store, root_ca, intermediate_ca, leaf_cert, self_signed, expired, revoked, valid: vocabulary. `trustSetBundle`, `trustVerifyChain`, `trustCount` in tls/truststore cover trust_store and root_ca; the others map to the functions above.
- p12, pfx, csr, crl, ocsp, pem, der: format aliases of the functions above; todo as aliases where the public names exist

## 7. Generic dispatch (`std/crypto`)
- digest(alg, data, n, out), digestLen(alg): done for sha256, sha384, sha512
- encrypt, decrypt, sign, verify, hash, hmac: todo (generic dispatch by algorithm id, same as digest)
- one_way, two_way, symmetric, asymmetric, authenticated_encryption, key_exchange: vocabulary only (module names, no code)

## 8. Random and IDs (`std/random`)
- randomBytes (OS entropy, sysRandom): done as random_bytes
- randomU64: done as random_u64
- Xoshiro256.next, below, between, float64, fill: done (random_u64, random_f64 as methods)
- random_u8, random_u16, random_u32, random_i32, random_i64, random_f32: todo (typed wrappers)
- uuidV4 (RFC 9562 section 5.4): done (uuid_v4)
- uuidV7 (RFC 9562 section 5.7): done (uuid_v7)
- uuidString (RFC 9562 section 4): done
- uuid_v1 (RFC 9562 section 5.1, node random with multicast bit section 6.10): wip
- uuid_v3 (RFC 9562 section 5.3, MD5 over namespace and name): wip
- uuid_v5 (RFC 9562 section 5.5, SHA-1 over namespace and name): wip
- uuid_v6 (RFC 9562 section 5.6): wip
- uuid_v8 (RFC 9562 section 5.8): wip
- uuid namespaces DNS, URL, OID, X500 (RFC 9562 Appendix B): wip
- uuid parse and version helpers: wip
- cuid v1 (paralleldrive/cuid, 25 characters): wip
- cuid v2 (paralleldrive/cuid2, letter plus base36 digest): wip; hash is SHA-256 until sha3 exists, so values differ from the reference
- ulid (ulid/spec, Crockford base32, 26 characters): wip
- nanoid (ai/nanoid, rejection sampling): wip
- snowflake (Twitter layout, 41-bit time, 10-bit machine, 12-bit sequence): wip
- ksuid (segmentio/ksuid, 20 bytes, base62, 27 characters): wip

## 8b. Random and identifier catalogue (complete list; supersedes the short list in section 8)
Status keys as above. Each line: name, spec or RFC, age (old = superseded, new = current recommendation), status.

Entropy and time sources (per OS, through std/sys; see sections 13 and 22):
- OS entropy: getrandom (Linux 318 x86-64, 278 arm64; man getrandom(2)), getentropy (macOS 500; man getentropy(2)), BCryptGenRandom (Windows; bcrypt.dll, Microsoft Learn): done (randomBytes).
- CPU entropy: RDRAND and RDSEED (Intel SDM Vol. 2 instruction reference; NIST SP 800-90B for conditioning), ARM RNDR/RNDRRS (Arm ARM FEAT_RNG): todo; through `__builtin_asm`, used only as an extra input to the CSPRNG, never alone.
- Clock: seconds done (sysClockSeconds, clock_gettime 228 on x86-64, 113 arm64); milliseconds and nanoseconds todo (clock_gettime per OS; macOS gettimeofday 116 or clock_gettime_nsec_np via library; Windows GetSystemTimePreciseAsFileTime). Monotonic clock todo (CLOCK_MONOTONIC on Linux, mach_absolute_time on macOS, QueryPerformanceCounter on Windows).
- Process and machine identity for IDs: pid (getpid 39 x86-64; Windows GetCurrentProcessId), hostname (uname/gethostname: todo, needed for exact cuid v1 and XID). Until then per-process random fallback, recorded as a deviation.

Random generators (non-cryptographic unless marked):
- SplitMix64 (Steele, Lea, Flood 2014; Vigna seeding): done (splitMix64).
- Xoshiro256** (Blackman and Vigna, prng.xl): done (Xoshiro256).
- Xoshiro256++ (Blackman and Vigna 2019): todo, same state, different output scramble.
- Xoshiro128** and Xoshiro128++ (32-bit state, Blackman and Vigna): todo.
- PCG32 XSH-RR and PCG64 XSL-RR (O'Neill 2014, pcg-random.org): todo (new).
- Wyrand (public spec in the wyhash repository): todo (fast, 64-bit).
- MT19937 (Matsumoto and Nishimura 1998): todo, old, kept for reproducing old sequences.
- Lehmer / Park-Miller MINSTD (Park and Miller 1988): todo, old, for classroom compatibility only.
- Xorshift64* (Marsaglia 2003; Vigna): todo.
- WELL512 (Panneton, L'Ecuyer, Matsumoto 2006): defer.
- Philox and Threefry (Salmon et al. 2011, Random123): defer (counter-based, needed only for parallel GPU streams).
- ChaCha8, ChaCha12 (Bernstein 2008, reduced rounds): defer.
- ARC4 / RC4 streams: defer (RFC 7465 forbids them in TLS; never a default).
- Dual_EC_DRBG: never (NIST SP 800-90A Rev.1 withdrew it after the Shumow and Ferguson 2007 backdoor report).

Cryptographically secure (CSPRNG):
- ChaCha20 with fast key erasure (Bernstein, RFC 8439 for the block function): done (csprng).
- HMAC_DRBG (NIST SP 800-90A Rev.1, section 10.1.2) over HMAC-SHA-256: todo (uses hmac and sha256).
- Hash_DRBG (NIST SP 800-90A Rev.1, section 10.1.1): todo, old (kept for compatibility, not a default).
- CTR_DRBG (NIST SP 800-90A Rev.1, section 10.2.1) over AES-256: todo (needs AES-256, section 3).
- Fortuna (Ferguson and Schneier 2003): defer (large; the OS entropy already covers the use).
- Yarrow: defer, old.

Distributions and sampling:
- Unbiased bounded integers: randomBelowU64 done (rejection, no modulo bias); Lemire multiply-shift todo only if a 128-bit product is added.
- Uniform float in [0, 1): randomF64 done (53 bits); float32 todo.
- Normal (Box and Muller 1958, Marsaglia polar method, Ziggurat by Marsaglia and Tsang 2000): todo (Ziggurat is the fast default).
- Exponential (inverse CDF), gamma (Marsaglia and Tsang 2000), beta, binomial (BTPE, Kachitvichyanukul and Schmeiser 1988), Poisson (PTRS, Hormann 1993): todo, after the normal.
- Shuffle (Fisher-Yates): done (shuffle). Sampling without replacement (reservoir, Algorithm R, Vitter 1985): todo.

UUID (RFC 9562, https://www.rfc-editor.org/rfc/rfc9562; supersedes RFC 4122):
- v1 time and node (section 5.1): done (UuidTimeGen.v1). Old design; new code prefers v7 when no MAC address is needed.
- v2 DCE security (section 5.2 says the format is not defined): defer.
- v3 MD5 namespace name (section 5.3, RFC 1321 for MD5): done (uuidV3). Old; use v5 for new names.
- v4 random (section 5.4): done (uuidV4).
- v5 SHA-1 namespace name (section 5.5, RFC 3174): done (uuidV5). Current for name-based IDs.
- v6 reordered time (section 5.6): done (UuidTimeGen.v6).
- v7 Unix milliseconds and random (section 5.7): done (uuidV7). Current for time-ordered IDs.
- v8 custom (section 5.8): done (uuidV8).
- Nil (section 5.9) and Max (section 5.10): nil done; max todo (all 0xff bytes).
- Namespaces (Appendix B): done (UUID_NS_DNS, URL, OID, X500).
- Parse and text (section 4): done (uuidParse, uuidString). Uppercase output option todo.
- Validation helpers (version and variant): done (uuidVersion, uuidIsRfcVariant).

Textual and sortable IDs (not standards unless noted):
- ULID (ulid/spec, Crockford base32 from https://www.crockford.com/base32.html): done (ulid, UlidGen monotonic). New for sortable text keys.
- cuid v1 (paralleldrive/cuid): done (cuidV1, fingerprint random per process). Old; superseded by v2.
- cuid v2 (paralleldrive/cuid2): done (cuidV2) with SHA-256 deviation until SHA-3 (section 2). New in its family.
- nanoid (ai/nanoid): done (nanoid, rejection sampling over the alphabet mask). New; short URL-safe keys.
- PushID (Firebase): done (PushIdGen, monotonic). Old (2012).
- XID (rs/xid): done (XidGen, host and pid random per process). New; compact sortable.
- KSUID (segmentio/ksuid): done (ksuid). New; 27 base62 characters.
- MongoDB ObjectId (bsonspec.org): done (ObjectIdGen). Old, still common in databases.
- TSID (f4b6a3/tsid-creator): done (tsid, random 22 bits, no monotonic class yet). New; 64-bit sortable.
- Sqids and Hashids (reversible encoding of integers; no spec): defer.
- Base58 short IDs (Bitcoin alphabet; no RFC): todo, state in docs that it is not standard.

Integer (64-bit) ID generators:
- Snowflake (Twitter, 2010 announcement): done (Snowflake class, epoch 1288834974657). Old; not a standard.
- Discord snowflake (same bit layout as Snowflake, epoch 1420070400000 from Discord developer docs): todo as a named constant and wrapper (Snowflake class already covers the layout).
- Instagram ID (2012 blog): done (InstagramIdGen).
- Sonyflake (sony/sonyflake): done (Sonyflake).
- Flickr ticket server, Baidu uid-generator, Meituan Leaf: defer (they depend on a shared database or ZooKeeper, which the stdlib does not provide).
- Random 64-bit IDs: randomU64 done; typed wrappers randomU32, randomI64, randomF64, randomBelowU64 done.

Work order for this catalogue: entropy and time (section 13 and 22 rows), PCG and Xoshiro variants, HMAC_DRBG, distributions, then the CPU-entropy path. Each item needs its spec cited in its header comment, and xlang-docs/random.md and docs-ai/random.md updated in the same change.

## 9. Encodings (`std/encoding`)
- hexEncode, hexDecode, base16Encode, base16Decode (RFC 4648 section 8): done
- base64Encode, base64Decode (RFC 4648 section 4): done
- base64UrlEncode, base64UrlDecode (RFC 4648 section 5): done
- base32_encode, base32_decode (RFC 4648 section 6): todo
- base32hex_encode, base32hex_decode (RFC 4648 section 7): todo
- base85_encode, base85_decode (RFC 1924): todo
- ascii85_encode, ascii85_decode (Adobe PostScript Language Reference, 3rd ed., section 3.3.2): todo
- json: jsonHas, jsonInt, jsonBool, jsonString (read-only top-level members): done

## 10. Constant time and memory (`std/mem`, `std/crypto`)
- constant_time_eq, constant_time_compare: todo (fixed-length compare with no early exit)
- secure_zero, secure_wipe: todo (writes through `__builtin_write`; check the emitted IR keeps the stores)

## 11. Strings (`std/str`)
- strEq, strFind, strContains, strStartsWith, strEndsWith, strTrim, strSplit, strSlice, strToU64, strToI64, strFromC, strFromParts: done
- String: length, asStr, cstr, at, reserve, push, append, equals, clone, clear, slice, find, startsWith, endsWith, toLowerAscii, toUpperAscii: done
- strCompare (byte-wise lexicographic, returns -1, 0, 1): wip
- strEqualIgnoreCase (ASCII): wip
- strContainsAny: wip
- strTrimStart, strTrimEnd: wip
- strReplace (all occurrences, empty needle rejected): wip
- strRepeat: wip
- strGlobMatch (`*` and `?`, iterative backtracking): wip
- String methods: compare, equalsIgnoreCase, contains, trimStart, trimEnd, replace, repeat, globMatch: wip

## 12. Math (`std/math`)
- abs, min, max, sqrt (f64, written in xl): done
- minInt, maxInt (i64): done
- abs, min, max for integers: todo
- clamp, sign: todo
- floor, ceil, round, trunc, cbrt, pow, exp, exp2, log, log2, log10, ln: todo (software, no libm)
- sin, cos, tan, asin, acos, atan, atan2, sinh, cosh, tanh, asinh, acosh, atanh: todo
- hypot, fmod, remainder, modf, frexp, ldexp, copysign, nextafter: todo
- gcd, lcm, factorial, binomial, is_prime, next_prime, mod_inverse, mod_pow, crt, legendre, jacobi, miller_rabin: todo (integer functions over u64 and bignum)
- bigint_add, bigint_sub: done at the limb level (bnAdd, bnSub, bnCmp, bnFromBytes, bnToBytes); the `bigint_*` public API names: todo
- bigint_mul, bigint_div, bigint_mod, bigint_pow, bigint_gcd, bigint_lcm, bigint_mod_inverse, bigint_mod_pow, bigint_is_prime: todo
- Montgomery modular arithmetic (montInit, montMul, montTo, montFrom, modAdd, modSub, montPow): done (mod_pow covered by montPow at limb level)
- matrix_add, matrix_sub, matrix_mul, matrix_transpose, matrix_inverse, matrix_det: todo
- dot, cross, norm, normalize, lerp, smoothstep, map_range, deg_to_rad, rad_to_deg: todo

## 13. Kernel interface: syscalls per OS (`std/sys`, docs in `xlang-docs/syscalls/`)
Rule: every number goes into `xlang-docs/syscalls/<os>-<arch>.md` before code uses it (CLAUDE.md, syscalls docs-first). Numbers below are from the primary source named in each line; anything else is marked `unverified`.
- Linux x86-64 table source: https://raw.githubusercontent.com/torvalds/linux/master/arch/x86/entry/syscalls/syscall_64.tbl (fetched). Verified: read 0, write 1, open 2, close 3, mmap 9, pread64 17, socket 41, connect 42, accept 43, bind 49, listen 50, clock_gettime 228, epoll_wait 232, openat 257, sched_getaffinity 204, futex 202, accept4 288, epoll_create1 291, getrandom 318, statx 332, io_uring_setup 425, io_uring_enter 426, io_uring_register 427, pidfd_open 434, clone3 435, membarrier 324.
- Linux arm64 (generic `include/uapi/asm-generic/unistd.h`, fetched): done, recorded in `xlang-docs/syscalls/linux-arm64.md`. Verified: socket 198, bind 200, listen 201, connect 203, accept4 242, epoll_pwait 22 (no epoll_wait on arm64), pipe2 59, clone 220, nanosleep 101, gettid 178. Numbers differ from x86-64, so no value is copied across architectures.
- Linux x86-64 remaining numbers (poll 7, epoll_ctl 233, kill 62, wait4 61, getdents64 217, ioctl 16, sendmsg 46, recvmsg 47): done, in `xlang-docs/syscalls/linux-x86_64.md`. x32 ABI entries (512+) in the same table are excluded.
- Linux: raw `syscall` where the number is documented and stable; `@library` where the OS promises the library interface (CLAUDE.md, section 24). Every area that needs OS setup gets a `sysXxxUp()` routed with `#if os`.
- macOS (XNU, `bsd/kern/syscalls.master`, source https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/kern/syscalls.master, fetched). Verified: read 3, write 4, open 5, close 6, access 33, accept 30, socket 97, connect 98, bind 104, listen 106, ioctl 54, select 93, poll 230, shm_open 266, workq_kernreturn 368, psynch_mutexwait 301, thread_selfid 372, kqueue 362, kevent 363, kevent64 369, getentropy 500, mmap 197.
- macOS caveats: the table is per XNU release, so the release tag used must be written in the doc. Mach traps (`osfmk/mach/syscall_sw.h`) and arm64 `svc #0x80` with x16 holding the number are a separate table (`bsd_arm64.c`); not yet read: todo. `__sysctl` is absent at the number checked, so `sysctl` is todo.
- Windows: Nt* numbers are not published by Microsoft and change per build (community sources only). Decision: do not hard-code SSNs. Use documented Win32 functions through `@library` imports from ntdll/kernel32/ws2_32 in `std/sys/windows.xl`, with the imports kept behind `#if os`. Verify each function on Microsoft Learn before use: todo.
- Per-area syscall list (each area: Linux, macOS, Windows column, all `todo` unless marked):
  - time: clock_gettime / mach_absolute_time / QueryPerformanceCounter, GetSystemTimePreciseAsFileTime.
  - files: openat / open, read, write, pread64, statx / stat, fstat, getdents64, unlink, rename, mkdir, fsync, ftruncate; Windows CreateFileW, ReadFile, WriteFile, GetFileInformationByHandleEx.
  - memory: mmap, munmap, mprotect, madvise, brk; macOS mmap 197; Windows VirtualAlloc, VirtualFree, VirtualProtect.
  - process and threads: clone3 (435), exit, wait4, getpid, pidfd_open (434), sched_yield (24), sched_getaffinity (204), futex (202) for locks; macOS psynch_* (defined only as a fallback, see section 17); Windows CreateThread, WaitOnAddress, WakeByAddressSingle.
  - entropy: getrandom (318), macOS getentropy (500), Windows BCryptGenRandom (bcrypt.dll; check docs).
  - signals: rt_sigaction (13), rt_sigprocmask (14); Windows SetConsoleCtrlHandler.
  - networking: socket (41), bind (49), listen (50), accept4 (288), connect (42), sendto/recvfrom (44/45), setsockopt (54), getsockopt (55), shutdown (48); macOS socket 97, connect 98, bind 104, listen 106, accept 30; Windows socket/bind/connect/WSASend/WSARecv in ws2_32, with WSAStartup hidden in `sockUp`.
- Rule: each row above becomes a `std/sys/<area>.xl` item only after its numbers are in `xlang-docs/syscalls/`.

## 14. Event notification and I/O backends (`std/sys/event`, `std/net`)
- Status (this turn): readiness API `sysPollCreate/Watch/Wait` exists on all three OSes. Linux epoll: done (existing). macOS kqueue: done (existing; `sched_yield` removed because unverified). Windows WSAPoll: wip, type-checked for x86_64-pc-windows-msvc, not run on Windows. Docs: `xlang-docs/event-loop.md`, `docs-ai/event-loop.md`. KNOWN-LIMITATIONS: "Event polling and OS library calls" section.
- Decided (see CLAUDE.md): OS runtime libraries may be linked; user code never calls them directly, but any stdlib area may declare and call them (e.g. `std/thread` creates threads through a library call), not only `std/sys`; prefer `syscall` where the number is documented and stable, `@library` where the OS promises the library interface.
- epoll (Linux): epoll_create1 (291), epoll_ctl (233 on x86-64, todo verify in the table above), epoll_wait (232), flags EPOLL_CLOEXEC; man page epoll(7) and epoll_wait(2) cited as URLs: todo.
- kqueue (macOS, FreeBSD): kqueue (362), kevent (363), kevent64 (369) in XNU. Semantics from the FreeBSD kevent(2) man page (https://man.freebsd.org/kevent/2): a kevent is keyed by (ident, filter); a zero-timeout call is a poll; nevents zero returns immediately: todo write doc.
- IOCP (Windows): CreateIoCompletionPort, GetQueuedCompletionStatus, GetQueuedCompletionStatusEx, PostQueuedCompletionStatus, OVERLAPPED. Source https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports (fetched). Rules from it: concurrency value = NumberOfConcurrentThreads; threads wake LIFO, packets dequeue FIFO; a thread belongs to one port at a time; the port is process-local. Thread pool API (CreateThreadpoolIo) is the simpler alternative and is recorded as the default choice: todo decide.
- io_uring (Linux 5.1+): io_uring_setup (425), io_uring_enter (426), io_uring_register (427). Setup flags (from include/uapi/linux/io_uring.h, fetched): IOPOLL 1<<0, SQPOLL 1<<1, SQ_AFF 1<<2, CQSIZE 1<<3, CLAMP 1<<4, ATTACH_WQ 1<<5, R_DISABLED 1<<6, SUBMIT_ALL 1<<7, COOP_TASKRUN 1<<8, TASKRUN_FLAG 1<<9, SQE128 1<<10, CQE32 1<<11, SINGLE_ISSUER 1<<12, DEFER_TASKRUN 1<<13, NO_MMAP 1<<14, REGISTERED_FD_ONLY 1<<15, NO_SQARRAY 1<<16, HYBRID_IOPOLL 1<<17, CQE_MIXED 1<<18, SQE_MIXED 1<<19, SQ_REWIND 1<<20. Opcodes verified: NOP 0, READV 1, WRITEV 2, FSYNC 3, READ_FIXED 4, WRITE_FIXED 5, POLL_ADD 6, POLL_REMOVE 7, SENDMSG 9, RECVMSG 10, TIMEOUT 11, ACCEPT 13, ASYNC_CANCEL 14, CONNECT 16, FALLOCATE 17, OPENAT 18, CLOSE 19, READ 22, WRITE 23, SEND 26, RECV 27, SOCKET 45. sqe and cqe struct sizes: unverified (read the header's struct definitions before coding); ring mmap offsets: todo.
- io_uring: the plan picks the setup flags deliberately per feature (SINGLE_ISSUER and DEFER_TASKRUN for the server loop, SQPOLL off by default because it burns a CPU). Fallback: epoll when io_uring_setup returns ENOSYS or EPERM (seccomp and container policy). Record the fallback in xlang-docs/known-limitations.md.
- poll (230 on macOS, 7 on Linux x86-64 `poll`: todo verify), select (macOS 93). Used only as the portable fallback.
- Event loop design (`std/net/loop`): one reactor per thread, one backend selected at `#if os` with a runtime probe for io_uring; no backend name leaks into the user API.

## 15. Context switch and stacks (`std/runtime/context`, done)
- Stack switching (`std/runtime/context`) exists: done. Remaining: per-OS stack allocation through `std/mem` with guard pages (mmap PROT_NONE on Linux and macOS, VirtualAlloc with PAGE_GUARD on Windows): todo.
- Fiber and green-thread scheduler on top of it (`std/thread/fiber`): todo; scheduler policy stays in stdlib, the compiler knows nothing about it.
- Context switch cost and ABI: the callee-saved register set per arch (x86-64 SysV and Windows x64 differ; arm64 AAPCS64): todo; each arch file cites the ABI document.
- Signal-safe stack for SIGSEGV on guard hit (sigaltstack on Linux and macOS): todo.

## 16. CPU (`std/cpu`, new area; partly exists as `std/cpu/*`)
- Feature detection x86-64: CPUID leaf 0 (vendor), leaf 1 (ECX bit 27 OSXSAVE, leaf 1 EDX), leaf 7 subleaf 0 (EBX bit 5 AVX2, EBX bit 16 AVX512F, EBX bit 28 AVX512CD, EBX bit 30 AVX512BW, EBX bit 31 AVX512VL), XGETBV with XCR0 to confirm the OS saves ZMM state. Source: Intel SDM Vol. 2 CPUID and Vol. 1 ch. 15: the search gave only secondary copies, so verify on the official PDF before coding: todo.
- AMD: CPUID 0x80000001 for LZCNT, ABM, SSE4A and similar; cite AMD APM Vol. 3: todo.
- arm64: read ID_AA64ISAR0_EL1 and friends from the Arm ARM (DDI0487), or on Linux the AT_HWCAP and AT_HWCAP2 bits from getauxval-like auxv (read through /proc/self/auxv, no libc): todo. macOS: sysctlbyname hw.optional.* (todo verify names in the XNU sysctl list). Windows: IsProcessorFeaturePresent and, for AVX, GetEnabledXStateFeatures: todo verify on Microsoft Learn.
- Topology: logical CPU count (Linux sched_getaffinity 204, macOS sysctl hw.logicalcpu, Windows GetLogicalProcessorInformationEx): todo.
- Cache line size: CPUID leaf 4 (x86) and CTR_EL0 (arm64): todo.
- Time stamp counter: rdtsc and cntvct_el0 are CPU primitives and go through `__builtin_asm`; the stdlib wraps them, the compiler does not know the name: todo.
- Memory barriers: `__builtin_fence` already exists; membarrier (Linux 324) for process-wide barriers: todo.
- Atomics for users: `__builtin_atomic*` exists; a `std/sync` wrapper (spinlock, mutex on futex and os primitives): todo.

## 17. GPU (`std/gpu`, `std/gpu/drm`, new)
- Linux DRM (kernel UAPI, `include/uapi/drm/drm.h`, source https://git.zx2c4.com/linux-dev/tree/Documentation/gpu/drm-uapi.rst): render nodes `/dev/dri/renderD<N>`, no DRM-master needed; only PRIME and driver-independent ioctls allowed on render nodes (DRM_RENDER_ALLOW in drm_ioctl.c). Plan: device enumeration (DRM_IOCTL_VERSION), GEM buffers, command submission per driver (amdgpu, i915/xe, msm, panfrost, nouveau each differ): driver ioctl numbers are todo; verify from each driver's uapi header.
- macOS: Metal is an Objective-C API. Calling it needs the Objective-C runtime (objc_msgSend) through a `@library` import: decision needed, recorded as defer until a design pass. IOKit (IOServiceGetMatchingService) for device enumeration: todo verify.
- Windows: DXGI (EnumAdapters1, GetDesc1) and D3D12 device creation are COM vtable calls through dxgi.dll and d3d12.dll: todo verify with Microsoft Learn; COM vtable layout must cite the MSDN page.
- Vulkan, CUDA, OpenCL, ROCm: each needs a vendor loader (libvulkan, libcuda). Libraries allowed only through `@library` when necessary (CLAUDE.md). Status: defer, with a Turkish assessment in xlang-docs/known-limitations.md.
- Compute and shaders: a SPIR-V emitter is out of scope; record as defer.

## 18. Hardware and devices (`std/hw`, new)
- Linux: sysfs (`/sys/class`, `/sys/bus/pci/devices/*/vendor` and `device`), hwmon temperature files, `/proc/cpuinfo` and `/proc/meminfo` (read-only text), ioctl on block devices (BLKGETSIZE64 needs the number from `linux/fs.h`: todo verify), evdev input (EVIOCG* ioctls: todo verify), usbdevfs ioctls: todo verify each.
- macOS: IOKit (IORegistryEntryCreateCFProperty needs CoreFoundation): defer; sysctl hw.* for memory, cpu and model: todo.
- Windows: SetupAPI (SetupDiGetClassDevs), WMI is avoided (COM heavy), GetSystemInfo, GlobalMemoryStatusEx, GetVolumeInformationW: todo verify.
- SMBIOS and ACPI tables: read from `/sys/firmware/dmi/tables` (Linux), firmware tables via GetSystemFirmwareTable (Windows): todo; spec is the DMTF SMBIOS spec DSP0134 and ACPI spec (uefi.org): cite before coding.
- Power and thermal: RAPL energy counters (Intel SDM and Linux powercap sysfs): todo.
- Serial and GPIO: `/dev/tty*` termios ioctls (TCGETS, TCSETS on Linux): todo verify; Windows CreateFile on COMn with DCB: todo.

## 19. Networking protocols (`std/net`)
- IP, IPv6, ICMP, ICMPv6: RFC 791, RFC 8200, RFC 792, RFC 4443: todo (types, checksum, header build).
- TCP: RFC 9293; UDP: RFC 768; sockets and options: POSIX and Winsock docs: todo.
- Multicast: IGMPv3 (RFC 3376), MLDv2 (RFC 3810): todo (socket options only, no protocol engine).
- ARP (RFC 826), DHCPv4 (RFC 2131), DHCPv6 (RFC 8415), SLAAC (RFC 4862): todo (client side only, raw sockets need elevated rights: document).
- DNS: RFC 1035 wire format, RFC 6891 EDNS0, DoH RFC 8484, DoT RFC 7858, DoQ RFC 9250: todo; `std/dns` moves to `std/net` per CLAUDE.md.
- NTP: RFC 5905 client packet: todo.
- SOCKS5: RFC 1928: todo. STUN: RFC 8489; TURN: RFC 8656: todo (NAT traversal; defer the ICE layer, RFC 8445).
- Sockets per OS: section 13 rows.

## 20. Web servers and HTTP (`std/http`, `std/net`, `std/tls`)
- HTTP semantics: RFC 9110; HTTP/1.1 message syntax: RFC 9112; HTTP/2: RFC 9113; HTTP/3: RFC 9114; QPACK: RFC 9204; HPACK: RFC 7541; cookies: RFC 6265; multipart: RFC 7578; URI: RFC 3986; Date: RFC 9110 section 5.6.7: todo.
- Existing: `std/http` has router, middleware, context, server, stream and fetch in code; their state against these RFCs is unverified. Action: audit each file against RFC 9110 and 9112 and record the gaps: todo.
- WebSocket: RFC 6455 (handshake with SHA-1 of key plus GUID, frame layout): todo, uses sha1 (wip).
- QUIC: RFC 9000 (transport), RFC 9001 (TLS over QUIC), RFC 9002 (loss recovery and congestion control): defer (large; needs TLS 1.3 on QUIC keys). Record in xlang-docs/known-limitations.md.
- TLS: RFC 8446 (1.3), RFC 5246 (1.2, legacy), RFC 6101 (SSL 3, historical), TLS 1.0 and 1.1 (RFC 2246, RFC 4346) are declared in CLAUDE.md as part of `std/tls`; existing: std/tls: todo audit; DTLS: RFC 9147: defer.
- HTTP server features: keep-alive, chunked transfer (RFC 9112 section 7.1), gzip and zstd (RFC 8878), brotli (RFC 7932): todo.
- Static files, range requests (RFC 9110 section 14): todo.
- Server cloud API surface: health, metrics (Prometheus text exposition format, https://prometheus.io/docs/instrumenting/exposition_formats/): todo.

## 21. Cloud, APIs and auth (`std/cloud`, `std/auth`, new)
- JWT: RFC 7519; JWS: RFC 7515; JWK: RFC 7517; JWA algorithms: RFC 7518 (HS256 uses hmac; RS256 uses rsaVerifyPkcs1; ES256 uses ecdsaVerify; EdDSA uses RFC 8037): todo.
- OAuth 2.0: RFC 6749; bearer tokens RFC 6750; PKCE RFC 7636; token introspection RFC 7662: todo (client side, plain HTTP requests).
- AWS Signature Version 4 (https://docs.aws.amazon.com/IAM/latest/UserGuide/reference_aws-signing.html): canonical request, HMAC-SHA-256 signing key chain; needs hmac and sha256 (done): todo. This is a documented public algorithm; use only the spec.
- Google Cloud and Azure request signing: defer (vendor SDK territory); record the reason.
- Instance metadata: link-local endpoints (169.254.169.254, the metadata address documented by each vendor): todo, behind a configurable base.
- gRPC over HTTP/2: spec https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md: defer until HTTP/2 framing exists.
- Protocol Buffers wire format (https://protobuf.dev/programming-guides/encoding/): varint, zigzag, length-delimited, fixed32/64: todo.
- Docker Engine API, Kubernetes API: defer; both are plain HTTP+JSON, so they follow from http and json.
- MQTT 5.0 (OASIS MQTT-5.0): todo after TCP and TLS. CoAP RFC 7252: defer.
- LDAP RFC 4511, Kerberos RFC 4120, SNMP RFC 3416, syslog RFC 5424: defer.
- Logging: structured output with RFC 5424 field rules: todo.

## 22. System services, files and processes (`std/os`, `std/fs`, `std/process`, new)
- Files: Linux openat, read, write, close, statx, getdents64; macOS open 5, read 3, write 4, close 6, getdirentries (todo verify number); Windows CreateFileW and FindFirstFileExW: todo.
- Processes: fork and exec are not offered on Windows, so the API is CreateProcessW on Windows and posix_spawn-like behavior elsewhere: todo verify; wait4 on Linux: todo number.
- Environment and args: auxv on Linux, `_NSGetArgv` avoided (libc-adjacent): decision: read args from the entry stack; record in xlang-docs/known-limitations.md: todo.
- Signals: rt_sigaction (13) on Linux, kill (62, x86-64, verify), sigaction on macOS (todo number), Windows SetConsoleCtrlHandler: todo.
- Time: clock_gettime (228), gettimeofday on macOS (todo number), QueryPerformanceCounter on Windows: todo. Time zones: TZif RFC 8536: todo; date and time formatting RFC 3339 and ISO 8601: todo.
- Random: entropy through getrandom (318) and macOS getentropy (500): wip (randomBytes exists).

## 23. Data formats and compression (`std/encoding`, `std/compress`, new)
- UTF-8: RFC 3629; punycode RFC 3492; IDNA RFC 5891: todo.
- JSON: RFC 8259 full parser (existing jsonHas, jsonInt, jsonBool, jsonString are read-only top-level; full tree and writer: todo); JSON Pointer RFC 6901, JSON Patch RFC 6902, JSON Merge Patch RFC 7396: todo.
- CBOR: RFC 8949; MessagePack spec (https://github.com/msgpack/msgpack/blob/master/spec.md): todo.
- CSV: RFC 4180: todo. PEM: RFC 7468 (pemNext done; encoder todo, see section 6). Base58 (Bitcoin alphabet, no RFC): todo, state it is not standard in the docs; base91 and Z85 (ZeroMQ RFC 32): defer.
- ASN.1 BER and DER: X.690: derRead done; full encoder todo.
- deflate RFC 1951, zlib RFC 1950, gzip RFC 1952: todo (inflate first, deflate with stored and fixed Huffman); zstd RFC 8878: defer; brotli RFC 7932: defer; LZ4 and Snappy: defer (no RFC; format docs cited if built).
- YAML and TOML: TOML spec https://toml.io/en/v1.0.0: todo; YAML: defer (large spec).
- Semver: https://semver.org/ : todo.

## 24. Native library linking, all three OSes (`src/driver/link.cpp`, `src/hir/prune.cpp`)
Status (done this turn): reachability pruning implemented (`src/hir/prune.cpp`, `pruneUnreachable`, called in the `build` path before codegen); `@library` linking on macOS (`-syslibroot` from `xcrun`, `-l<name>`) and Linux (`-l<name>` after the objects) for used imports only; `panic` and `unwind` write through `sysWriteFile` so the Windows socket path is not linked by default. Checked: a macOS program using `@library("System")` links and runs; an unused declaration adds no dependency; a Windows exe with no socket use imports only ntdll and kernel32; a scratch program with `print` runs. Remaining: Linux static archive search path (`[[native]]` only), dynamic Linux linking (defer), runtime checks on Linux and Windows.
Goal: `@library("name") declare fn` works on Linux and macOS the same way it works on Windows, and only the declared symbols that are actually used are linked.
- Rule per API: a stable, documented OS API is called through `@library` (dynamic or static); a number that the OS does not promise (raw syscall) is used only where no library entry exists. Decision per call is recorded in `xlang-docs/syscalls/` or `xlang-docs/os-libraries/`.
- Reachability roots (the set that survives): `@entry`, every `@init` and `@fini` function, every `register(role)` function, every `export` symbol when building a shared or static library, and everything they transitively call or reference (function pointers and vtables included). Everything else in the stdlib is dropped before linking.
- Pruned linking (required): the compiler emits only the imports reached from those roots, so a declared function that no reachable code calls produces no import, no stub and no load-time symbol. Implementation points: (1) collect `@library` symbols after monomorphization and dead-function removal, (2) emit one import per (library, symbol) actually used, (3) keep `--gc-sections` (Linux) and `-dead_strip` (macOS) as the second layer, (4) add a test that a program calling nothing from a library produces no reference to it (`nm`/`otool -L` check), written only after the plan is complete.
- Windows: already dynamic (import library generated by `llvm-dlltool` per DLL, symbols from `r.imports`). Pruning follows the same collected set; current code already builds only declared symbols, so it is the reference behavior.
- macOS: `ld64.lld` must receive `-lSystem` (or the framework or dylib that exports the symbol) and the dyld load commands are then produced by the linker. Each library is a dylib dependency only when a symbol from it is used. Current state: no `-lSystem`, raw syscalls only. The libc ban is lifted (CLAUDE.md), so `-lSystem` is allowed; no open decision remains.
- Linux: the current link is `-static -nostdlib` with no interpreter, so a shared `.so` cannot load. Two routes: (a) static `.a` archives given as `[[native]]` link inputs in `xlang.toml`, pruned by `--gc-sections`; (b) dynamic linking with `PT_INTERP` (`ld-linux`), `PT_DYNAMIC`, PLT/GOT and `DT_NEEDED`, which the compiler does not emit today. Route (a) first; route (b) is a separate design pass, recorded as defer until then.
- Stdlib is not a runtime blob: the standard library is compiled into each program and only reachable functions survive, so linking a library does not bring the whole stdlib with it.
- Verification: `xmake build` for the compiler; `xlang check --target <triple>` for the three OS targets on each new file (done for the poll module on aarch64-apple-darwin, x86_64-pc-windows-msvc, x86_64-unknown-linux-gnu, aarch64-unknown-linux-gnu); runtime checks only after the plan is complete.

## Order of work (superseded by "Current queue" above; kept for history)
1. Finish the wip items (md5, sha1, uuid v1 v3 v5 v6 v8, id generators, str helpers) and run `xmake build std`.
2. Generic HMAC, then sha224, sha512/224, sha512/256, sha3, shake, blake2, then their HMACs.
3. Base32, base32hex, base85, ascii85, constant-time helpers, poly1305.
4. Cipher modes: AES-CBC, CTR, GCM-256, CCM, XTS, XChaCha20 and salsa20 family.
5. Signatures and keys: Ed25519, ECDH and secp256r1 keygen, RSA keygen and OAEP, P-384 and P-521.
6. KDFs: pbkdf2, hkdf-sha512, scrypt, concat_kdf, x963_kdf.
7. X.509 public names, CSR, CRL, DER and PEM encoders, key serialization, TLS version and cipher names.
8. Math: integer functions, software floating point functions, bigint public API, matrix and vector helpers.
9. Record deferred items in xlang-docs/known-limitations.md with the Turkish feature assessment.
10. Run `xmake build std`, then the full test suite once.
11. Syscall docs first: DONE for `xlang-docs/syscalls/linux-x86_64.md`, `xlang-docs/syscalls/linux-arm64.md`, `xlang-docs/syscalls/macos-xnu.md` (BSD table and arm64 BSD convention) and `docs-ai/syscalls.md`. Still todo: macOS Mach trap table (`osfmk/mach/syscall_sw.h`), macOS arm64 `ARM64_SYSCALL_CODE_REG_NUM` definition, Windows documented Win32 functions per area.
12. Event backends: epoll, kqueue, IOCP and io_uring behind one reactor (section 14), with the io_uring to epoll fallback recorded.
13. CPU feature detection (section 16) after the Intel SDM and Arm ARM pages are read directly.
14. Networking protocols and HTTP audit (sections 19, 20), then JWT, JWS, JWK and AWS SigV4 (section 21).
15. GPU (section 17) and hardware (section 18): Linux DRM and sysfs first; macOS and Windows after their design passes.
16. System services, data formats and compression (sections 22, 23).

## 25. Language and API ergonomics (proposals, not built yet)
Status: recorded only, with one exception: 25.9 (slice syntax) is built — see that entry. Nothing else in this section is implemented; the user asked for these to be planned, not built. Each item needs its design (syntax, sema, codegen) before work starts, and each is checked against the CLAUDE.md rule that stdlib is preferred over compiler builtins.
Goal: remove the pain points of current code (see the table at the end of this section).

- 25.1 Error flow with `else`: `local slot = slotOf(id) else { c.status(404); return }` takes the fast path when the value is nil or failed. `else |e| { ... }` binds the error when the expression fails. Both work on `?T` and on fallible calls. Interacts with `try`/`catch`, which stays for panics.
- 25.2 `match` as the only multi-way branch (no `switch`; `match` covers its job). Expression form, patterns in stages: literal (`42`, `"hello"`, `true`), wildcard `_`, binding `x`, range `1..10` and `100..=200`, or `1 | 2 | 3`, guard `x if x > 10`, optional `Some(v)` / `None` for `?T`, enum `Color.RGB(r, g, b)` once enums carry payloads, struct `Post { used: true, id }` last. `match` and `else |e|` coexist: `else |e|` for the quick exit, `match` for full branching.
- 25.3 `?` operator (propagate): `local id = c.param("id").parse<i64>()?` returns the error to the caller. Needs a fallible return type in the function signature; defined together with 25.1.
- 25.4 Generic parsing: `parse<T>()` on `str`/`String`, returning the value plus an error with the position and reason. Stdlib function, not a compiler builtin, once generics on methods with a type argument are supported.
- 25.5 Fixed buffer with length: `buf[N]string` (a fixed array of bytes plus its length, on the stack, no allocation). Replaces hand-written `title: [u8; 120], titleLen: usize`.
- 25.6 Compile-time JSON literal: `json"{ id: p.id, title: p.title }"`. Expanded at compile time to an encoder call that escapes strings, so no hand-written `\"` escapes. Macro-like; the compiler only expands the literal, and the encoder itself is stdlib.
- 25.7 Route block: `r.group("/api", requireToken) { r.post(...); r.put(...); r.delete(...) }`. Needs a trailing block argument (a function literal passed as the last argument) and captures or an explicit receiver. Depends on closure capture (currently `unknown name k` for captured names) before the block can be sugar over a literal.
- 25.8 `for` over ranges and collections: `for i, p := range posts { }` and `for c := range s.bytes() { }`, plus the existing `for i in lo..hi`. Stdlib iterators; the loop form is the compiler part.
- 25.9 Slice syntax: done. `auth[7..]` and `s[a..b]` yield a `str` view without copying, over a `str` or a `[u8; N]`; both bounds optional. Bounds checked under `--safety=checked` (constant end past an array length is `E0701`), a non-byte base is `E0709`, and a slice is a value (`E0716` when assigned to). Lowered to `hir::ExprKind::Slice`; codegen clamps negative bounds and emits the two `checkedBranch` guards. E2E: `test/e2e/slices.xl`.
- 25.10 Operators on std types, both as builtins and as overloads, with the overload used for user types: `x += 1`, `a == b` (for `String` and `str` via `eq`), and `copy(dst, src)` as a builtin for bulk copy. Open decision: `copy` as a compiler builtin conflicts with the closed builtin list in CLAUDE.md; the stdlib form `std/mem` copy is the default unless the compiler change is approved.
- 25.11 Local type inference: `local slot := slotOf(id)`; the declared type is optional when the initializer fixes it.
- 25.12 String-first stdlib: user code and new stdlib functions use `String` for owned text and `str` only for views at boundaries. Conversions: `String.asStr()` (view), `String.from(str)` (copy), `str.toString()`; each stdlib module migrates its public API to these in one pass. Rule already in CLAUDE.md (Std API types); this item is the migration of the existing modules.

Pain points this section addresses (current code):

| Pain | Now | Planned |
|------|-----|---------|
| Three text types | `str`, `String`, `*u8` + length | `String` in APIs, `str` view at boundaries (25.12) |
| Manual length | `titleLen`, `textLen` fields | `buf[N]string` (25.5) |
| Manual pointer and length | `fromParts(&t[0], tl)` everywhere | slice syntax (25.9) |
| Manual JSON | `sprintf("{\"id\":{}}", ...)` plus escaping helpers | `json"..."` (25.6) |
| Manual parse | a hand-written `toId()` | `parse<i64>()?` (25.3, 25.4) |
| Error as bool | `fn readPost(...) -> bool`, no message | `else |e|` and `?` (25.1, 25.3) |
| Explicit type on every local | `local i: i32 = 0` | `:=` inference (25.11) |
| Index loops | `local i = 0; while i < n { i = i + 1 }` | `for ... range` (25.8) |
| Verbose calls for operators | `eq(fromC(x), "token")`, `nextId = nextId + 1` | operators (25.10) |

Order (when started): 25.12 and 25.11 (no grammar change, widest benefit), then 25.9, 25.8, 25.5, 25.4, 25.1 with 25.3, 25.2, 25.6, 25.10, 25.7 last (25.7 needs closure capture).
