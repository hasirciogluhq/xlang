# https-server

A TLS 1.3 server on 127.0.0.1 with routes for a greeting, a path parameter, and the Unix time as JSON.

```
xl build -o https-server examples/https-server/main.xl
./https-server server.crt server.key 8443
curl --cacert ca.crt https://127.0.0.1:8443/time
```

The certificate and key are PEM files; RSA and ECDSA P-256/P-384 keys are accepted. A missing or unreadable file stops the
program with exit status 1. The port defaults to 8443.
