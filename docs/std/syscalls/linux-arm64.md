# Linux arm64 system call numbers

Source: the generic table `include/uapi/asm-generic/unistd.h` from the Linux master branch, which arm64
uses unchanged, read at https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/asm-generic/unistd.h.
arm64 passes the number in `x8`, arguments in `x0` to `x5`, and returns in `x0`; errors use the same
`-errno` convention as x86-64 (see syscall(2), https://man7.org/linux/man-pages/man2/syscall.2.html).

The numbers differ from x86-64 for the same call. Never copy a number between the two files.

## Verified numbers

| Name | Number |
|---|---|
| io_setup | 0 |
| epoll_create1 | 20 |
| epoll_ctl | 21 |
| epoll_pwait | 22 |
| ppoll | 73 |
| getdents64 | 61 |
| openat | 56 |
| mmap | 222 |
| mprotect | 226 |
| clock_gettime | 113 |
| clock_nanosleep | 115 |
| kill | 129 |
| exit_group | 94 |
| futex | 98 |
| socket | 198 |
| bind | 200 |
| listen | 201 |
| connect | 203 |
| sendto | 206 |
| recvfrom | 207 |
| accept4 | 242 |
| sched_getaffinity | 123 |
| getrandom | 278 |
| membarrier | 283 |
| statx | 291 |
| wait4 | 260 |
| rseq | 293 |
| io_uring_setup | 425 |
| io_uring_enter | 426 |
| io_uring_register | 427 |
| pidfd_open | 434 |
| clone3 | 435 |

`__NR_syscalls` is 473 in this table.

The `SYS_*` constants in `std/sys/linux.xl` for aarch64 are all confirmed by this table: EPOLL_CREATE1 20, EPOLL_CTL 21, EPOLL_WAIT 22, GETRANDOM 278, CLOCK_GETTIME 113, PIPE2 59, GETTID 178, CLONE 220, NANOSLEEP 101, MMAP 222, MUNMAP 215, SCHED_YIELD 124, EXIT 93, EXIT_GROUP 94, FUTEX 98, READ 63, WRITE 64, IOCTL 29, GETDENTS64 61, MPROTECT 226; the open family uses OPENAT 56, CLOSE 57, LSEEK 62, UNLINKAT 35.

arm64 has no `epoll_wait` entry; `epoll_pwait` (22) is the call to use, with a null sigmask. Any code path that names `SYS_EPOLL_WAIT` on arm64 must map to 22 and document the sigmask argument. The `SYS_EPOLL_WAIT = 22` constant in `linux.xl` is therefore correct only with that sigmask rule.

Also confirmed: ioctl 29, sendmsg 211, recvmsg 212, madvise 233, sched_yield 124, rt_sigaction 134, rt_sigprocmask 135, getpid 172, fcntl 25, dup3 24, eventfd2 19, signalfd4 74, timerfd_create 85, prlimit64 261.

## Not yet verified

The ioctl request numbers for DRM, sysfs and block devices (they come from each driver or subsystem header, not from this table).
