# dns-lookup

Resolves a host name to IPv4 addresses with the system resolver (`/etc/hosts`, then DNS), and builds the RFC 1035 query
for it as bytes.

```
xlang build -o dns-lookup examples/dns-lookup/main.xl
./dns-lookup example.com
```

The query bytes are built and their length printed; the program does not send them itself. The resolver it uses is
`std/net/dns`, a blocking call.
