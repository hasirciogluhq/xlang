# std/http

The HTTP library lives in std/http and is re-exported by std.
The server router is Gin-style with Router, Group, route parameters and handler chains.
Static routes win over parameter routes and HEAD falls back to GET.
A path matching with or without a trailing slash is redirected, a known path with another method gets 405, and otherwise 404.
Middleware is a function of Context that calls ctxNext to run the rest of the chain or ctxAbort to stop it.
Responses are whole through ctxRespond and its helpers or chunked through ctxBeginStream, ctxWriteChunk and ctxEndStream.
Request bodies need Content-Length because chunked request bodies are not supported.
The client offers fetch with redirects, a helper per method, fetchMethod for any token and httpOpen with httpRead for streamed bodies.
Client failures are negative FETCH_ERR codes and fetchErrorText describes them.
