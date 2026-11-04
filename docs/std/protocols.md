# Network protocol messages

The wire formats of some application protocols are in `std/net/proto`. Each module builds or reads the bytes; none opens a connection.

| Module | Functions | Specification |
|---|---|---|
| `std/net/proto/ntp` | `ntpClientRequest`, `ntpParse`, `ntpToUnixSeconds`, `ntpOffsetDelay` | RFC 5905, sections 6, 7.3 and 8 |
| `std/net/proto/syslog` | `syslogPri`, `syslogMessage` | RFC 5424, section 6 (header with version 1; structured data written as `-`) |
| `std/net/proto/socks5` | `socks5Greeting`, `socks5ConnectDomain`, `socks5ConnectIPv4`, `socks5ParseReply` | RFC 1928 (no authentication method; the RFC 1929 username subnegotiation is not done) |
| `std/net/proto/ip` | `inetChecksum`, `ipv4Header`, `icmpEcho` | RFC 1071 (checksum), RFC 791 section 3.1 (IPv4 header, no options), RFC 792 (ICMP echo) |
| `std/net/proto/dnswire` | `dnsWriteName`, `dnsBuildQuery`, `dnsParseHeader`, `dnsReadName` | RFC 1035, sections 4.1 and 4.1.4 (names, compression pointers; loops stop after 16 pointers) |
| `std/net/proto/transport` | `ipv6Header`, `udpBuild4`, `tcpBuild4`, `icmpv6Echo`, `arpRequest` | RFC 8200 section 3 (IPv6), RFC 768 (UDP), RFC 9293 section 3.1 (TCP, no options), RFC 4443 sections 2.3 and 4.1 (ICMPv6 echo), RFC 826 (ARP) |

The NTP request sets LI 0, VN 4 and Mode 3, so byte 0 is 0x23. `ntpOffsetDelay` takes the four timestamps of section 8 (t1 client send, t2 server receive, t3 server send, t4 client receive) and returns the offset and delay in 32.32 units. The results were checked against values computed by hand from the formulas.

The syslog header writes the timestamp it is given; formatting it as RFC 3339 is the caller's job (see [Date and time](time.html)).

The IPv4 and ICMP helpers build the bytes and the checksum; they do not open raw sockets. The DNS module builds a standard query with RD set and reads names and the header. The resolver that sends queries is `std/net/dns`.
Checked against the RFC 791 example header from the Wikipedia IPv4 article (checksum b861), an exact `example.com` A query, a compressed name, a pointer loop (rejected), and ICMP messages that sum to zero.

The transport builders compute the checksums over the pseudo-header of RFC 768 and RFC 9293 (IPv4) or RFC 8200 section 8.1 (IPv6). A UDP checksum that comes out zero is sent as 0xffff. The checksums were checked by an independent sum (perl) over the bytes: the segment and its pseudo-header add to 0xffff. TCP options, window scaling, and the retransmission state machine are not implemented, and nothing here sends a packet.

## MQTT 5.0 (`std/net/proto/mqtt`)

| Function | Packet (OASIS MQTT 5.0 section) |
|---|---|
| `mqttConnect(clientId, n, keepAlive, out, cap)` | CONNECT, protocol 5, clean start (3.1) |
| `mqttPublish(topic, n, payload, pn, qos, retain, packetId, out, cap)` | PUBLISH, QoS 0, 1 or 2 with the retain flag (3.3) |
| `mqttSubscribe(packetId, filter, n, qos, out, cap)` | SUBSCRIBE with one filter (3.8) |
| `mqttPingReq(out)`, `mqttDisconnect(out)` | PINGREQ (3.12), DISCONNECT with reason 0 (3.14) |
| `mqttReadHeader(buf, n)`, `mqttParseConnack(buf, n)` | fixed header and CONNACK (3.2) |

Every packet has an empty property list. Checked against the official `eclipse-mosquitto:2` broker in a container, with an xlang client
on the host: CONNECT gets CONNACK with reason 0, SUBSCRIBE gets SUBACK, a PUBLISH to a subscribed topic comes back with its payload,
PINGREQ gets PINGRESP, and DISCONNECT is accepted.

Not implemented: will messages, username and password, AUTH, user properties, topic aliases, and the QoS 1 and QoS 2 acknowledgements (PUBACK, PUBREC, PUBREL, PUBCOMP).
