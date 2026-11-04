---
title: Cloud storage (S3)
---

`std/cloud` is an S3 client for any S3-compatible service. It signs requests with AWS Signature Version 4 (<https://docs.aws.amazon.com/IAM/latest/UserGuide/reference_aws-signing.html>) and sends them through the standard HTTP client (`std/http`, see [HTTP](http.html)), so HTTPS works through the same TLS code.

## Profiles

A profile names the endpoint and the addressing style. The client needs no other provider-specific setting.

| Function | Endpoint |
|----------|----------|
| `s3ProfileAws(region)` | `s3.<region>.amazonaws.com`, virtual-hosted addressing |
| `s3ProfileGeneric(host, region, https)` | any host, path-style addressing |
| `s3ProfileR2(accountId)` | Cloudflare R2 |
| `s3ProfileGcs()` | Google Cloud Storage interoperability endpoint |

`s3ProfileGeneric` is the one to use for self-hosted servers such as SeaweedFS, MinIO or Ceph RGW. Set `https` to `false` for plain HTTP.

## Client

```
local s3: S3Client = S3Client(s3ProfileGeneric("127.0.0.1:18333", "us-east-1", false), "testkey", "testsecret")
```

Methods:

- Buckets: `CreateBucket(bucket)`.
- Objects: `PutObject(bucket, key, contentType, body)`, `GetObject`, `HeadObject`, `DeleteObject`.
- Listing: `ListObjectsV2(bucket, prefix, token, maxKeys)`. The answer's `nextToken` fetches the next page while `truncated` is true.
- Multipart upload: `CreateMultipartUpload`, `UploadPart(bucket, key, uploadId, partNumber, body)`, `CompleteMultipartUpload(bucket, key, uploadId, partsXml)`, `AbortMultipartUpload`. Every part except the last is at least 5 MiB; part numbers run from 1 to 10000.
- Presigned URLs: `PresignGet(bucket, key, expires)` returns a URL valid for `expires` seconds (query-string SigV4 with `UNSIGNED-PAYLOAD`). `PresignGetAt` takes the time explicitly, which makes the URL reproducible.
- Session credentials: `UseSessionToken(token)`.
- `Warnings()` counts the optional headers the provider ignored. Required headers that are missing are errors, not warnings.

Every call returns an `S3Result`:

- `ok`, `status`: success and the HTTP status.
- `code`, `message`: the S3 error code and text on failure.
- `body`, `etag`, `contentType`: the response payload and headers.
- `clientError`: a transport error from the HTTP client (`CLIENT_ERR_*`), `0` when the request reached the server.
- `UploadId()`: the upload identifier of a `CreateMultipartUpload` answer.

## Example

```
from "std" import print, S3Client, S3Result, s3ProfileGeneric

@entry fn main() -> i32 {
    local s3: S3Client = S3Client(s3ProfileGeneric("127.0.0.1:18333", "us-east-1", false), "testkey", "testsecret")
    local put: S3Result = s3.PutObject("photos", "cat.jpg", "image/jpeg", "...bytes...")
    if !put.ok {
        print("put failed\n")
        return 1
    }
    return 0
}
```

## Testing

The unit tests (`std/cloud/s3.test.xl`) check the signing vectors and the request shapes without a server. The end-to-end program `test/e2e/s3.xl` needs an S3-compatible server on `127.0.0.1:18333` with the access key `testkey` and secret `testsecret`. The repository provides the SeaweedFS identity file `test/e2e/seaweedfs.json`:

```
docker run -d -p 18333:8333 -v "$PWD/test/e2e/seaweedfs.json:/etc/seaweedfs/s3.json" chrislusf/seaweedfs server -s3 -s3.config=/etc/seaweedfs/s3.json
XLANG_E2E_S3=1 xmake run e2e
```

The program creates a bucket, puts, gets, heads and lists objects, fetches a presigned URL over HTTP, uploads a 5 MiB part and a 4-byte part, and checks the assembled size and the 404 after a delete. It can be rerun against the same server.

The S3 test runs natively only, because the server is on the host's loopback address, which a Docker container does not reach.

## Limits

- The client holds whole bodies in memory; streaming upload and download are not implemented yet (see [known limitations](../known-limitations.html)).
- Only the operations above are implemented. Bucket deletion, versioning, lifecycle, ACL and policy calls are not.
- The addressing style comes from the profile; the client does not probe the server.
