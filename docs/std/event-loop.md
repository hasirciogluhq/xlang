# Readiness polling

The standard library exposes one readiness API in `std/sys`: `sysPollCreate`, `sysPollWatch`, `sysPollWait`.
A poll set is created once; a watch asks for one readiness event on a socket (read or write) and is one-shot,
so it stops firing after it reports. The caller re-arms it after handling the event. The same signatures are
used on every OS, and the bodies differ per OS.

| OS | Backend | Kernel interface | Watches |
|---|---|---|---|
| Linux | epoll | `epoll_create1`, `epoll_ctl`, `epoll_wait` (x86-64) or `epoll_pwait` (arm64) | any fd |
| macOS | kqueue | `kqueue` 362, `kevent` 363 (XNU table) | any fd |
| Windows | WSAPoll | `WSAPoll` from ws2_32 | sockets only, 256 across the process |

Sources: epoll uapi header `include/uapi/linux/eventpoll.h` (struct packed on x86-64 only); kqueue `bsd/sys/event.h`
(struct kevent, 32 bytes on 64-bit); WSAPoll on Microsoft Learn and the Winsock header constants.

io_uring and IOCP are not implemented. io_uring is a completion model and needs its own backend; IOCP is also a
completion model, so neither fits the readiness API.
