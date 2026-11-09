# HTTP audit against RFC 9110 and RFC 9112

This page records how `std/http` was checked against the HTTP/1.1 rules that a server must follow. Each row names the rule,
the code that implements it, and whether it passes now. The check was a reading of the code and raw requests sent with `nc`
to a server built from `std/http`.

| Rule | Source | Status |
|---|---|---|
| A request with `Transfer-Encoding` other than `chunked` is answered `501` | RFC 9112 section 6.1 | Fixed: `501 Not Implemented`, connection closed |
| `Content-Length` is read only from a header line that starts with the name, not from a value that contains it | RFC 9112 section 6.3 (framing), section 5 (field lines) | Fixed: `headerAt` anchors the name to the start of a line |
| An HTTP/1.1 request without a `Host` header is answered `400` | RFC 9112 section 3.2 | Fixed: `400 Bad Request` |
| Every response carries a `Date` header in IMF-fixdate form | RFC 9110 section 6.6.1 and section 5.6.7 | Fixed: `httpDate` on every response |
| HEAD gets the headers and the `Content-Length` of the GET response, without a body | RFC 9110 section 9.3.2 | Passes (`isHead`) |
| Header names compare case-insensitively | RFC 9110 section 5.1 | Passes (`lower` in `headerAt`) |
| Request line: method, target, and `HTTP/1.1` or `HTTP/1.0` | RFC 9112 section 3 | Passes for the two versions; other versions are not refused |
| Several `Content-Length` headers with different values are refused | RFC 9112 section 6.3 | Fixed: `400 Bad Request` (`contentLengthConflict`) |
| `Expect: 100-continue` gets an interim `100` before the body is read | RFC 9110 section 10.1.1 | Fixed: `HTTP/1.1 100 Continue` is sent when the body is still missing |
| Persistent connections (keep-alive) | RFC 9112 section 9.3 | Passes (`keepAliveWanted`, `Context.keepAlive`): the response says `Connection: keep-alive` or `Connection: close` to match |
| Chunked request bodies are decoded (chunk extensions are skipped, trailers are read and dropped) | RFC 9112 section 7.1 | Fixed: decoded in place before routing; a malformed body gets `400` |
| The request target is percent-decoded before routing | RFC 3986 section 2.4 | Fixed: `%XX` becomes the byte XX, before routing |
| Target-form validation (origin form, `*`, absolute URI) | RFC 9112 section 3.2 | Fixed: any other form gets `400` (`targetValid`) |

## How the fixes were checked

- `printf 'POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n' | nc 127.0.0.1 <port>` answers `HTTP/1.1 501`.
- `printf 'GET / HTTP/1.1\r\n\r\n' | nc ...` answers `HTTP/1.1 400`; the same request with `HTTP/1.0` answers `200`.
- A `GET` with `X-A: content-length: 99` and no `Content-Length` header answers `200` and reads no body.
- A normal `GET` response contains a `Date:` line in the form `Wed, 04 Nov 2026 03:34:53 GMT`.

## Re-checks after the fixes

- `POST /echo` with `Transfer-Encoding: chunked`, chunks `5` and `7;ext=1`, returns `hello, world`; `curl -H "Transfer-Encoding: chunked" --data-binary` returns the body unchanged.
- `Transfer-Encoding: gzip, chunked` is answered `501`.
- `GET /hello%20world` reaches the route registered as `/hello world`.
- `POST` with `Expect: 100-continue` receives `HTTP/1.1 100 Continue` before the final `200`.
