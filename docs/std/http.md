---
title: HTTP
---

`std/http` has an HTTP/1.1 server and client over plain TCP or TLS 1.3. The parts live next to each other:

| File | Contents |
|---|---|
| `engine.xl` | `Engine`, `RouterGroup` and `Route`: routing, groups and dispatch |
| `context.xl` | `Context`: the request and the response of one exchange |
| `server.xl` | `routerServe` and `routerServeTls`: the connection loop |
| `middleware.xl` | `Logger`, `Recovery`, `Cors` |
| `client.xl` | `Client`, `Request`, `Response`, `Headers` and `Header` |
| `multipart.xl` | multipart form parsing |
| `url.xl`, `util.xl` | URL parsing (`Url`: `https`, `port`, `host`, `path`; `urlParse`, `urlParsePath`, `fetchErrorText`) and header helpers |

## Server

```xlang
from "std" import Engine, Context, Logger, println

fn hello(c: *Context) {
    c.String(200, "hello\n")
}

@entry fn main() -> i32 {
    local e: Engine = Engine()
    e.Use(Logger)
    e.GET("/hello", hello)
    return e.Run(8080)
}
```

### Engine

| Member | Meaning |
|---|---|
| `Engine()` | an engine with no routes |
| `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`, `OPTIONS` | register a route: `(path, fn(*Context))` |
| `Any(path, handler)` | register a route for every method |
| `Handle(method, path, handler)` | register a route for one method token |
| `Use(mw)` | add middleware to every route registered after it |
| `Group(prefix)` | a `*RouterGroup` with that prefix, copying the middleware in effect |
| `NoRoute(handler)`, `NoMethod(handler)` | replace the 404 and 405 handlers |
| `Serve(c *Context)` | route one request (the handler the connection loop calls) |
| `Run(port)`, `RunTLS(port, &credentials)` | serve until the process ends |
| `redirectTrailingSlash`, `redirectFixedPath`, `handleMethodNotAllowed` | fields that turn the matching rules below on and off |

A `RouterGroup` has the same route methods and `Use`/`Group`, all relative to its prefix.

### Matching

- Static routes win over `:name` and `*name`; `:name` matches one segment and `*name` the rest of the path (its value includes the leading `/`).
- `HEAD` is a route method of its own; a `HEAD` request is answered by the matching `HEAD` route (if any) and the body is not written.
- A path that differs from a route only by a trailing slash is redirected (301 for `GET`, 307 for the other methods).
- A known path with another method is only told apart when `handleMethodNotAllowed` is `true` (it is `false` in a fresh engine, so such a request gets 404); otherwise the answer is 404.
- Registering two routes of one method that differ only in parameter names (`/user/:id` and `/user/:name`) stops the program at start-up, as Gin does.

### Middleware

Middleware is `fn(*Context)`.

```xlang
fn auth(c: *Context) {
    if c.GetHeader("Authorization").len == 0 {
        c.AbortWithStatus(401)
        return
    }
    c.Next()
}
```

`Next()` runs the rest of the chain and then continues after it; `Abort()` and `AbortWithStatus(code)` stop it; `IsAborted()` reports whether it was stopped.

### `Context`

| Group | Members |
|---|---|
| Request | `Method()`, `Path()`, `Body()`, `Param(name)`, `Query(name)`, `DefaultQuery(name, fallback)`, `PostForm(name)`, `GetHeader(name)`, `FormValue`, `FormFile`, `FormFileName`, `FormFileType` |
| Response | `Status(code)`, `String(code, text)`, `JSON(code, text)`, `HTML(code, text)`, `Data(code, contentType, bytes)`, `Redirect(code, location)`, `SetHeader(name, value)` |
| Chunked | `StreamStart()`, `WriteChunk(bytes)`, `EndStream()` |
| Chain | `Next()`, `Abort()`, `AbortWithStatus(code)`, `IsAborted()`, `Responded()` |

The examples on the [Examples]({{ site.baseurl }}/examples.html) page are the routing examples of the Gin documentation, written with `std/http`, and are included straight from `docs/examples/`.

## Client

```xlang
from "std" import Client, Request, Response, println

@entry fn main() -> i32 {
    local c: Client = Client()
    local req: Request = Request("POST", "https://example.com/")
    req.SetBody("{}")
    req.headers.Set("Content-Type", "application/json")
    local resp: Response = c.Do(&req)
    println("{} {}", resp.status, resp.Text())
    return 0
}
```

| Type | Members |
|---|---|
| `Client` | `Do(&req) -> Response`, `Get(url)`, `Head(url)`, `Delete(url)`, `Post(url, contentType, body)`, `Put(url, contentType, body)`, `followRedirects(status)`; fields `timeoutMs`, `maxRedirects`, `userAgent` |
| `Request` | `Request(method, url)`; `SetBody(text)`; field `headers` |
| `Headers` | `Add`, `Set`, `Remove`, `Get`, `Has`, `Length`, `Write` |
| `Response` | `Header(name)`, `Text()`, `Ok()`; fields `status`, `reason`, `headers`, `body`, `error`, `finalUrl` |

`clientErrorText(code)` turns a failure code into a message; `fetchErrorText(code)` does the same for the `FETCH_ERR_*` codes.

Limits are listed in [Known limitations]({{ site.baseurl }}/known-limitations.html).
