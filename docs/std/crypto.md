# Cryptography

The cryptography modules are in `std/crypto`. Every algorithm names its specification in the file header and is checked against published test vectors. Functions take raw pointers and lengths and write into caller buffers; the sizes follow from the digest, tag or key length.

## Hash functions (`std/crypto/hash`)

| Function | Output | Specification |
|---|---|---|
| `md5(data, n, out)` | 16 bytes | RFC 1321. Broken; for legacy formats only |
| `md4(data, n, out)` | 16 bytes | RFC 1320. Broken; test vectors only |
| `sha1(data, n, out)` | 20 bytes | RFC 3174. Broken for signatures |
| `sha224`, `sha256` | 28, 32 bytes | FIPS 180-4 |
| `sha384`, `sha512` | 48, 64 bytes | FIPS 180-4 |
| `sha512t224`, `sha512t256` | 28, 32 bytes | FIPS 180-4, section 5.3.6 |
| `sha3224`, `sha3256`, `sha3384`, `sha3512` | 28, 32, 48, 64 bytes | FIPS 202 |
| `shake128(data, n, out, outLen)`, `shake256` | any length | FIPS 202, section 6.2 |
| `blake2b(key, klen, data, n, out, outLen)` | 1 to 64 bytes | RFC 7693 |
| `blake2s(key, klen, data, n, out, outLen)` | 1 to 32 bytes | RFC 7693 |
| `cshake128`, `cshake256` | any length | NIST SP 800-185, section 3.3 |

A `digest(alg, data, n, out)` call selects one of these by a `DIGEST_*` id; `digestLen(alg)` and `digestBlockLen(alg)` give the output and block sizes.

## Message authentication codes

| Function | Specification |
|---|---|
| `hmac(alg, key, klen, msg, mlen, out)` | RFC 2104, over any `DIGEST_*` id with a block length (MD5, SHA-1, SHA-2, SHA-3). Allocates a temporary buffer for the message |
| `hmacSha256(key, klen, msg, mlen, out)` | RFC 2104, SHA-256, message limited to 1024 bytes |
| `cmacAes(key, keyLen, msg, mlen, out)` | NIST SP 800-38B; RFC 4493 vectors for AES-128, SP 800-38B example for AES-256 |
| `gmacAes(key, keyLen, nonce, aad, alen, tag)` | NIST SP 800-38D with an empty plaintext |
| `kmac128(key, klen, data, n, custom, clen, out, outLen)`, `kmac256` | NIST SP 800-185, section 4 |
| `poly1305(key, msg, n, tag)` | RFC 8439, section 2.5. The 32-byte one-time key must not be reused |

HMAC with BLAKE2 is not defined: BLAKE2 has its own keyed mode (`blake2b` and `blake2s` take a key).

## Block ciphers and modes (`std/crypto/cipher`)

The AES core (`aes.xl`) supports 16-, 24- and 32-byte keys (FIPS 197). Every mode takes the key length as an argument.

| Function | Specification |
|---|---|
| `aesEcbEncrypt`, `aesEcbDecrypt` | NIST SP 800-38A, section 6.1; length a multiple of 16 |
| `aesCbcEncrypt`, `aesCbcDecrypt` | NIST SP 800-38A, section 6.2; length a multiple of 16, no padding added |
| `aesCtrXor` | NIST SP 800-38A, section 6.5; any length |
| `aesCcmSeal`, `aesCcmOpen` | NIST SP 800-38C and RFC 3610; nonce 7 to 13 bytes, tag 4 to 16 bytes (even) |
| `aesXtsEncrypt`, `aesXtsDecrypt` | IEEE 1619-2007 and NIST SP 800-38E; data unit a multiple of 16 bytes (no ciphertext stealing) |
| `aesGcmSeal`, `aesGcmOpen` | NIST SP 800-38D, 12-byte nonce, 16-byte tag; key 16, 24 or 32 bytes |
| `aes128EncryptBlock` | FIPS 197, one block with a 16-byte key |

## Stream ciphers and AEAD

| Function | Specification |
|---|---|
| `chacha20Block(key, counter, nonce, out)` | RFC 8439, section 2.3 |
| `aeadSeal`, `aeadOpen` | ChaCha20-Poly1305, RFC 8439 section 2.8 |
| `hChaCha20(key, nonce16, out)` | draft-irtf-cfrg-xchacha, section 2.2 |
| `xchacha20Xor(key, nonce24, counter, src, n, dst)` | XChaCha20 over the draft construction |
| `xchacha20Poly1305Seal`, `xchacha20Poly1305Open` | XChaCha20-Poly1305 |
| `salsa20Xor(key, nonce8, counter, src, n, dst)` | Salsa20/20 (Bernstein, eSTREAM); keystream vector checked |
| `hSalsa20(key, nonce16, out)`, `xsalsa20Xor(key, nonce24, src, n, dst)` | HSalsa20 and XSalsa20 (NaCl) |

## Key derivation (`std/crypto/kdf`)

| Function | Specification |
|---|---|
| `pbkdf2(alg, pass, plen, salt, slen, iterations, out, outLen)` | RFC 8018, section 5.2, over any HMAC digest; RFC 6070 vectors (SHA-1) |
| `hkdfExtractAlg(alg, salt, slen, ikm, ilen, prk)`, `hkdfExpandAlg(alg, prk, info, ilen, okm, len)` | RFC 5869 over any digest id; RFC 5869 appendix A.1 (SHA-256) |
| `scrypt(pass, plen, salt, slen, n, r, p, out, outLen)` | RFC 7914; both section 12 vectors checked (N = 16 and N = 1024) |
| `concatKdf(alg, z, zlen, info, ilen, out, outLen)` | NIST SP 800-56C one-step; no vector checked yet |
| `x963Kdf(alg, z, zlen, info, ilen, out, outLen)` | ANSI X9.63 / SEC 1 section 3.6.1; no vector checked yet |

HKDF-Expand-Label of TLS 1.3 is in `std/crypto/hash/hmac` and stays SHA-256 only.

## Key agreement, signatures and X.509

| Function | Specification |
|---|---|
| `x25519(out, scalar, u)`, `x25519Base(out, scalar)` | RFC 7748 |
| `x448(out, scalar, u)`, `x448Base(out, scalar)` | RFC 7748 section 5; RFC 7748 section 6.2 public key checked |
| `ed25519PublicKey(seed, pk)`, `ed25519Sign(seed, msg, mlen, sig)`, `ed25519Verify(pk, msg, mlen, sig)` | RFC 8032, section 5.1. Verified with RFC 8032 test 1; the scalar multiplication is not constant-time |
| `ecdsaSign`, `ecdsaVerify` (P-256, P-384, P-521 and secp256k1, `ECDSA_SECP256K1`, `ECDSA_P521`) | FIPS 186-4; SEC 2 for secp256k1 |
| `ecdhPublicKey(curve, d, qx, qy)`, `ecdhShared(curve, d, px, py, out)` (P-256, P-384, P-521 and secp256k1) | NIST SP 800-56A; the peer point is checked to be on the curve |
| `rsaVerifyPkcs1`, `rsaVerifyPss`, `rsaSignPss` | RFC 8017, sections 8.1 and 8.2 |
| `rsaEncryptPkcs1`, `rsaDecryptPkcs1` | RFC 8017, section 7.2 (the random padding string is nonzero; prefer OAEP) |
| `rsaEncryptOaep`, `rsaDecryptOaep` | RFC 8017, section 7.1 with MGF1 (appendix B.2.1); one error for every failure |
| `mgf1(alg, seed, slen, out, len)` | RFC 8017, appendix B.2.1, any seed length |
| `ecdsaKeypair(curve, d, qx, qy)` | FIPS 186-4 key generation for P-256, P-384, P-521 and secp256k1 |

The RSA encryption functions are checked against OpenSSL in both directions (OAEP with SHA-256 and PKCS#1 v1.5 decryption of OpenSSL ciphertexts, and OpenSSL decryption of our OAEP ciphertext). `rsaGenerateKey(bits, n, d)` makes a key of 512 to 4096 bits with e = 65537 (random primes with Miller-Rabin, d = (k phi + 1) / e). A 2048-bit key takes about a second; the output is checked by the round trip, not by an external tool yet.

X.509 parsing and path checks are RFC 5280 and live in `x509`. The accessors and checks are in `x509/names.xl`: `x509SubjectDer`, `x509IssuerDer`, `x509SignatureBytes`, `x509RsaModulus`, `x509RsaExponent`, `x509EcPoint` (byte views of the DER), `x509IsCa`, `x509PathLen`, `x509SelfSigned`, `x509CheckEmail` and `x509CheckIp` (RFC 5280 section 4.2.1.6), and `x509FingerprintSha256` and `x509FingerprintSha1`. The TLS version and cipher suite names (`tlsVersionName`, `tlsCipherSuiteName`) are in `std/tls/names.xl` and follow the IANA registry.

Not yet implemented in this area: certificate signing requests (RFC 2986), CRL parsing and verification (RFC 5280 section 5), key serialization to and from PKCS#8 and SubjectPublicKeyInfo, and the DER encoder.

The shared secret from `ecdhShared` is the raw x coordinate: a key derivation function must hash it before use.

## Authenticators and helpers

| Function | Specification |
|---|---|
| `constantTimeEq(a, b, n)` | Compares every byte without an early exit (`std/crypto/ct`) |
| `secureZero(p, n)` | Overwrites with zeros using sequentially consistent atomic stores (NIST SP 800-88 Rev. 1) |

## Verification

Each algorithm is checked against published vectors: the specification's examples or the reference values in its appendix. The vectors are recorded in the source comments and in the test programs.

## Not implemented

The AES-CFB and AES-OFB modes, OCB, the DES, RC4 and other legacy ciphers, XTS ciphertext stealing, Argon2 and bcrypt, and the other items in the deferred list are in [Known limitations](../known-limitations.html).

## Certificate requests and revocation lists (`std/crypto/x509`)

| Function | Specification |
|---|---|
| `csrParse(der, len)` returns `Csr` (`subject`, `key`, `sigAlg`, `ok`) | RFC 2986, section 4 (PKCS #10 request) |
| `csrVerifySelf(csr)` | RFC 2986 section 3, step 4: the request's signature under its own key (proof of possession) |
| `crlParse(der, len)` returns `Crl` (`issuer`, `thisUpdate`, `nextUpdate`, `revoked`, `ok`) | RFC 5280, section 5.1 |
| `crlIsRevoked(crl, serial, n)` | RFC 5280 section 5.1.2.6; serials compare as integers, leading zero bytes ignored |
| `crlVerifySignature(crl, issuerCert)` | RFC 5280 section 5.1.1.3 |
| `x509ChainVerify(chain, n, roots, nRoots, now)` | RFC 5280 section 6.1 (validity, CA and pathLen checks, issuer names, signatures, trust anchor); no name constraints or revocation |

A request or CRL is parsed without copying; its views point into the input DER. Checked with openssl: an EC P-256 and an RSA-2048 request, a tampered request (rejected), and a CRL listing one revoked serial.
Reading attributes, CRL extensions and revocation reasons is not implemented; neither is writing a request or a CRL.

## Key serialization (`std/crypto/x509/spki`)

| Function | Specification |
|---|---|
| `spkiFromCert(cert)` returns the SubjectPublicKeyInfo DER as a `String` | RFC 5280 section 4.1.2.7; RSA (rsaEncryption) and EC P-256 / P-384 (id-ecPublicKey, RFC 5480) |
| `spkiPemFromCert(cert)` | RFC 7468 section 13, label `PUBLIC KEY` |
| `ecPkcs8FromKey(key)` returns the PKCS #8 DER of an EC `PrivKey` | RFC 5208 section 5, ECPrivateKey of RFC 5915 |
| `ecPkcs8PemFromKey(key)` | label `PRIVATE KEY` |

The output of `spkiFromCert` for an openssl-issued EC certificate and for an RSA-2048 request is byte-identical to `openssl pkey -pubout -outform DER`. The EC PKCS #8 PEM was parsed by openssl, and the public key it derives equals the original key.
RSA private keys are not written: `PrivKey` keeps only the modulus and the private exponent, and PKCS #1 needs the CRT values too.

## Edwards448 (`std/crypto/sig/ed448`)

| Function | Specification |
|---|---|
| `ed448PublicKey(seed, pk)` | RFC 8032 section 5.2.5 (57-byte seed and public key) |
| `ed448Sign(seed, msg, mlen, sig)` | RFC 8032 section 5.2.6, pure Ed448 (phflag 0, empty context); deterministic, 114-byte signature |
| `ed448Verify(pk, msg, mlen, sig)` | RFC 8032 section 5.2.7 |

Checked against openssl: the public key of a random seed, the signature (byte-identical, since Ed448 is deterministic) and verification of an openssl signature, with a flipped byte rejected. The base point is the RFC 8032 section 5.2.1 value; it satisfies the curve equation.
