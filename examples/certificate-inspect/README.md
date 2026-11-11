# certificate-inspect

Reads a PEM certificate and prints what a TLS client checks: validity, whether it is a CA, the SHA-256 fingerprint, and,
when a root is given, whether the chain validates (RFC 5280 section 6.1). A CRL argument checks the serial number against
the revocation list (RFC 5280 section 5).

```
xl build -o certificate-inspect examples/certificate-inspect/main.xl
./certificate-inspect leaf.crt ca.crt crl.pem
```

Exit status 0 when every requested check passes, 1 otherwise (for example when the certificate is revoked).
