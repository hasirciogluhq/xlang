# std/http

`import ... from "std"` gives the HTTP server and client. The source is split by purpose in `std/http/`:
`context.xl` (request/response `Context`), `router.xl` (routes, groups, dispatch), `server.xl` (HTTP and
HTTPS connection loop), `middleware.xl` (`mwLogger`, `mwCors`), `url.xl`, `stream.xl` and `fetch.xl`
(client), `util.xl` (string and header helpers).

## Server

```
local r: Router
routerInit(&r)
routerUse(&r, mwLogger)
routerGet(&r, "/users/:id", user)       // ctxParam(c, "id")
routerGet(&r, "/files/*path", files)    // value starts with "/"
local api: Group
routerGroup(&r, "/api", &api)
groupUse(&api, authMiddleware)
groupPost(&api, "/items", create)
routerServe(&r, 2130706433, 8080, listening)      // or routerServeTls(&r, ip, port, &cred, listening)
```

- Methods: `routerGet/Post/Put/Patch/Delete/Head/Options`, `routerAny`, `routerHandle(r, method, path, h)`;
  the same with the `group` prefix on a `Group`.
- Matching: static routes win over `:name` and `*name`; `HEAD` falls back to `GET`; a path that exists
  with or without a trailing slash is redirected (301 for GET/HEAD, 307 otherwise); a known path with
  another method answers 405; otherwise 404 (`routerNoRoute` replaces the handler).
- Middleware is `fn(*Context)`; call `ctxNext(c)` to run the rest of the chain and continue afterwards,
  `ctxAbort`/`ctxAbortWithStatus` to stop it.
- Request: `ctxHeader`, `ctxQuery`, `ctxDefaultQuery`, `ctxParam`, `c.body`/`c.bodyLen`, `c.method`, `c.path`.
- Response: `ctxString`, `ctxHTML`, `ctxJSON`, `ctxStatus`, `ctxRespond`, `ctxRedirect`, `ctxSetHeader`.
  Chunked: `ctxBeginStream`, `ctxWriteChunk`, `ctxEndStream`.
- Per-request data: `ctxSetUser` / `ctxUser`.

## Client

- `fetch(url, &opts, buf, cap, &resp)` reads the whole response into `buf`, follows redirects and sets
  `resp.status`, `resp.headers` (`responseHeader`), `resp.body`.
- Helpers: `fetchGet`, `fetchPost`, `fetchPut`, `fetchPatch`, `fetchDelete`, `fetchHead`, `fetchOptions`,
  `fetchMethod` (any method token).
- Streaming: `httpOpen`, `httpRead`, `httpClose`, `httpHeader`; request bodies can come from a buffer or a
  chunked `source` callback (`FetchOptions`).
- Errors: `FETCH_OK` or a negative `FETCH_ERR_*` code; `fetchErrorText` gives a message.

Limits are listed in `KNOWN-LIMITATIONS.md`.
