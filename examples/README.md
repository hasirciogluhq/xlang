# Examples

Complete programs. Each directory holds a `main.xl` and this kind of README: what it does, how to build and run it.
The short programs that the documentation shows are in `xlang-docs/examples/` (see the Examples page of the documentation site).

Build one with the compiler, for example:

```
xl build -o blog-api examples/blog-api/main.xl
./blog-api s3cret 8080
```

Every `xmake build` type-checks all of them.

| Program | Needs |
|---|---|
| `blog-api/` | nothing; serves on 127.0.0.1 |
| `https-server/` | a PEM certificate and key |
| `certificate-inspect/` | a PEM certificate (optional root and CRL) |
| `jwt-tool/` | a secret, or a JSON Web Key |
| `archive-tool/` | an input file |
| `dns-lookup/` | a host name and a resolver (the system's) |
