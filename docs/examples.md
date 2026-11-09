---
title: Examples
---

The examples come in two sets with one source each:

- **Short programs for the documentation** live in `docs/examples/` as `.xl` files. The pages of this site include them,
  so the code shown is the code in the repository.
- **Complete programs** live in `examples/<name>/` (`main.xl` plus a README). They build as ordinary programs with
  `xlang build -o <name> examples/<name>/main.xl`.

Every `xmake build` copies both sets into `build/examples/` (`docs/` and `project/`) and type-checks each program with the
compiler that was just built (`xlang check`), so an example that stops compiling fails the build.

## Routing (Gin style)

Each example below is one of the routing examples of the Gin documentation, written with `std/http`. The server is the
`Engine` of `std/http/engine`: routes are registered with the method calls, each handler takes a `*Context`, and
`Engine.Run(port)` serves until the process ends.

### Hello world

```xlang
{% include_relative examples/hello.xl %}
```

### Path parameters

`:name` matches one segment; `*action` matches the rest of the path, including its leading slash.

```xlang
{% include_relative examples/params.xl %}
```

### Query string

`Query(name)` returns the value of a query parameter, and `DefaultQuery(name, fallback)` returns the fallback when the
parameter is absent.

```xlang
{% include_relative examples/query.xl %}
```

### Groups and middleware

`Group(prefix)` returns a `*RouterGroup` that copies the middleware in effect at that point; `Use` adds middleware to the
routes registered after it, and `Next` runs the rest of the chain.

```xlang
{% include_relative examples/groups.xl %}
```

### Redirects

```xlang
{% include_relative examples/redirects.xl %}
```

### JSON

```xlang
{% include_relative examples/json.xl %}
```

## Complete programs

| Program | What it shows |
|---|---|
| [`examples/blog-api`](https://github.com/hasirciogluhq/xlang/tree/development/examples/blog-api) | JSON REST API, route groups, bearer-token (HS256) authentication, an in-memory store |
| [`examples/https-server`](https://github.com/hasirciogluhq/xlang/tree/development/examples/https-server) | TLS 1.3 server with a certificate and key from files, JSON responses |
| [`examples/certificate-inspect`](https://github.com/hasirciogluhq/xlang/tree/development/examples/certificate-inspect) | PEM certificate fields, chain validation to a root, revocation in a CRL |
| [`examples/jwt-tool`](https://github.com/hasirciogluhq/xlang/tree/development/examples/jwt-tool) | HS256 sign and verify; RS256, ES256 and EdDSA verification with JWK keys; RFC 7638 thumbprints |
| [`examples/archive-tool`](https://github.com/hasirciogluhq/xlang/tree/development/examples/archive-tool) | gzip and zlib compression and decompression of files |
| [`examples/dns-lookup`](https://github.com/hasirciogluhq/xlang/tree/development/examples/dns-lookup) | name resolution with the system resolver and an RFC 1035 query built as bytes |
