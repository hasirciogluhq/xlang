# jwt-tool

Signs and verifies compact JSON Web Tokens (RFC 7515), and prints RFC 7638 thumbprints of JSON Web Keys (RFC 7517).

```
xl build -o jwt-tool examples/jwt-tool/main.xl
./jwt-tool sign-hs256 secret payload.json
./jwt-tool verify-hs256 secret "$TOKEN"
./jwt-tool verify-jwk RS256 key.jwk "$TOKEN"         # RS256, ES256 or EdDSA
./jwt-tool thumbprint key.jwk
```

Only signatures are checked: `exp`, `aud` and the other claims are for the caller to read.
