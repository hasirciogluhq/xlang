# Network protocol messages

The wire formats of some application protocols are in `std/net/proto`. Each module builds or reads the bytes; none opens a connection.

| Module | Functions | Specification |
|---|---|---|
| `std/net/proto/ntp` | `ntpClientRequest`, `ntpParse`, `ntpToUnixSeconds`, `ntpFromUnixNanos`, `ntpToUnixNanos`, `ntpOffsetDelay` | RFC 5905, sections 6, 7.3 and 8 |
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

## Further protocol messages

| Module | Functions | Specification |
|---|---|---|
| `std/net/proto/ntpclient` | `ntpQuery(ip, port, timeoutMs)` returns the offset and delay | RFC 5905 sections 7.3 and 8 (UDP; t1 and t4 come from `sysClockNanos`: nanoseconds on Linux, microseconds on macOS, 100 ns on Windows; a reply whose origin timestamp is not t1 is dropped) |
| `std/net/proto/dnstransport` | `dnsBuildQueryEdns`, `dnsFrameTcp`, `dnsUnframeTcp`, `dohRequestTarget` | RFC 6891 section 6.1.2 (OPT record in the additional section), RFC 1035 section 4.2.2 (DNS over TCP framing, used by DNS over TLS, RFC 7858), RFC 8484 section 4.1 (GET with `dns=` base64url) |
| `std/net/proto/stun` | `stunBindingRequest`, `stunMappedAddress` | RFC 8489 sections 6 and 14.2 (IPv4 XOR-MAPPED-ADDRESS, MAPPED-ADDRESS fallback) |
| `std/net/proto/dhcp` | `dhcpBuild`, `dhcpParseReply` | RFC 2131 sections 2 and 3, RFC 2132 options 1, 3, 6, 15, 50, 51, 53, 54, 55, 58, 59 |
| `std/net/proto/dhcpclient` | `DhcpClient`: `start`, `recv`, `tick`, `take`, `release`, `decline` | RFC 2131 sections 3.1, 4.1 and 4.4 (DISCOVER, OFFER, REQUEST, ACK, renewing at T1, rebinding at T2, NAK and lease expiry) |
| `std/net/proto/dhcp6` | `Dhcp6Client`: `start`, `recv`, `tick`, `take`, `release`; `dhcp6Parse` | RFC 8415 sections 7, 15, 18 and 21 (SOLICIT, ADVERTISE, REQUEST, REPLY, RENEW, REBIND, RELEASE with one IA_NA; DUID-LL) |
| `std/net/proto/slaac` | `SlaacClient`; `eui64`, `linkLocal`, `slaacAddress`, `icmp6Checksum`, `rsBuild`, `nsBuild`, `raParse` | RFC 4862 sections 5.3 to 5.5, RFC 4861 sections 4.1 to 4.6, RFC 4291 appendix A, RFC 8106 section 5.1 (RDNSS), RFC 4443 section 2.3 |

The DNS over TLS and DNS over HTTPS transports are the caller's TLS and HTTP clients; this module frames and encodes only. The STUN messages are built and parsed; nothing here sends them on a socket.

The DHCPv4, DHCPv6 and SLAAC clients are state machines without I/O or a clock. The caller passes `now` in milliseconds, hands each received datagram to `recv`, calls `tick` once `due` has passed, and sends what `take` returns: DHCPv4 to `dest` (broadcast or the server, UDP 67), DHCPv6 to `ff02::1:2` port 547, SLAAC from `src` to `dst` with hop limit 255. Retransmission starts at the RFC's initial timeout, doubles up to its maximum and has jitter (`std/net/proto/wire`). Timers use fixed numbers where the RFC lets the client choose. The DHCPv4 client does not ARP-probe the address (it exposes `decline` for the caller who does), and DHCPv6 handles one IA_NA with no IA_TA or prefix delegation.

## Base91

`std/encoding/base91` implements Joachim Henke's base91 (base91.sourceforge.net): 91 printable characters, 13 or 14 bits per pair, no padding.
