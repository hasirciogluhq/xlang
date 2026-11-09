# Authentication tokens

`std/auth` builds and checks signed JSON tokens.

| Function | Specification |
|---|---|
| `jwtSignHs256(header, payload, key, klen)` | RFC 7515 (JWS compact serialization) with HMAC-SHA-256; returns the token as a `String` |
| `jwtVerifyHs256(token, key, klen)` | checks that the header declares `"HS256"` and that the signature matches (constant-time compare); `none` is never accepted |

The base64url text is unpadded, as JOSE requires (RFC 7515 section 2). `std/encoding/base64` provides `base64UrlEncodeRaw` and `base64UrlDecodeRaw` for that form; the padded variants remain for other uses.

The signer reproduces the token of RFC 7515 appendix A.1 exactly, and the verifier accepts it. Claim validation beyond the signature is not implemented.

## Asymmetric tokens with JSON Web Keys (`std/auth/jws`)

| Function | Specification |
|---|---|
| `jwtVerifyWithJwk(token, jwk, alg)` | RFC 7515 verification with a key in JWK form (RFC 7517): `"RS256"` (RFC 7518 section 3.3), `"ES256"` (section 3.4, R || S), or `"EdDSA"` (Ed25519, RFC 8037). The header alg must equal `alg` |
| `jwkThumbprintSha256(jwk)` | RFC 7638: SHA-256 of the canonical JSON of the required members, base64url; returns a `String` |

The tokens were produced by openssl (RSA-2048 and P-256 keys, and an Ed25519 key) and verified here; tampered tokens, a wrong key and a mismatched alg are rejected. The RFC 7638 thumbprint was compared with one computed by openssl from the same canonical JSON. Key sets, `kid` selection, x5c, and the other algorithms (ES384, RS512, PS256) are not implemented.

## AWS Signature Version 4

`std/auth/sigv4` does the keyed steps of the AWS signing process: `sigv4SigningKey(secret, dateStamp, region, service, out)` (the four HMAC steps for date, region and service, written as 32 bytes to `out`), `sigv4Signature(secret, dateStamp, region, service, stringToSign)` (the hex signature of the string to sign), and `sigv4StringToSign(amzDate, scope, canonicalRequestHashHex)` (the string with the algorithm name, the request time, the scope and the hex digest of the canonical request). The caller builds the canonical request, hashes it with SHA-256 and passes the hex digest in.

The AWS documentation's IAM ListUsers example gives the signature `5d672d79c15b13162d9279b0855cfba6789a8edb4c82c400e06b5924a6f2b5d7`, and the code reproduces it.

## OAuth 2.0 helpers (`std/auth/oauth`)

| Function | Specification |
|---|---|
| `pctEncode(value)`, `pctEncodeInto(value, n, out)` | RFC 3986 section 2.1: unreserved characters kept, everything else as uppercase `%XX` |
| `pkceChallengeS256(verifier)` | RFC 7636 section 4.2: BASE64URL(SHA-256(verifier)); the verifier must be 43 to 128 unreserved characters (section 4.1) |
| `authorizationUrl(endpoint, clientId, redirectUri, scope, state, challenge)` | RFC 6749 section 4.1.1 with `response_type=code`; PKCE parameters are added when `challenge` is not empty |

The challenge for the RFC 7636 appendix B verifier is `E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM`, the value the RFC gives, and openssl computes the same. Sending the requests and parsing the token responses are left to the HTTP client.
