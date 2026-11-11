# blog-api

A JSON REST service: public reads, authenticated writes, route groups, and an in-memory store.

```
xl build -o blog-api examples/blog-api/main.xl
./blog-api s3cret 8080                       # serve on 127.0.0.1:8080
TOKEN=$(./blog-api s3cret token alice)       # a bearer token signed with s3cret (HS256)
curl -H "Authorization: Bearer $TOKEN" -d '{"title":"Hello","body":"First"}' http://127.0.0.1:8080/api/posts
curl http://127.0.0.1:8080/posts
```

Routes: `GET /posts`, `GET /posts/:id` (public); `POST /api/posts`, `PUT /api/posts/:id`, `DELETE /api/posts/:id` (token
required, from the `Authorization` header). Titles and bodies that contain quotes, backslashes or control characters are
refused with 400, so the JSON answers stay valid without an escaper. The store holds 64 posts.
