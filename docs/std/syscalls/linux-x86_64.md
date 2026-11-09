# Linux x86-64 system call numbers

Source: the kernel table `arch/x86/entry/syscalls/syscall_64.tbl` from the Linux master branch,
read at https://raw.githubusercontent.com/torvalds/linux/master/arch/x86/entry/syscalls/syscall_64.tbl.
The number is passed in `rax`; arguments go in `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`; the result
returns in `rax`, and a value in -4095..-1 is `-errno` (see syscall(2), https://man7.org/linux/man-pages/man2/syscall.2.html).

Numbers are stable for an architecture but not shared with other architectures: arm64 uses the
generic table, recorded in `linux-arm64.md`.

## Verified numbers

| Name | Number | Used by |
|---|---|---|
| read | 0 | files |
| write | 1 | files |
| open | 2 | files |
| close | 3 | files |
| poll | 7 | event loop fallback |
| mmap | 9 | memory |
| mprotect | 10 | memory |
| munmap | 11 | memory |
| brk | 12 | memory |
| rt_sigaction | 13 | signals |
| rt_sigprocmask | 14 | signals |
| sched_yield | 24 | threads |
| select | 23 | event loop fallback |
| nanosleep | 35 | time |
| getpid | 39 | processes |
| socket | 41 | networking |
| connect | 42 | networking |
| accept | 43 | networking |
| sendto | 44 | networking |
| recvfrom | 45 | networking |
| shutdown | 48 | networking |
| bind | 49 | networking |
| listen | 50 | networking |
| getsockname | 51 | networking |
| getpeername | 52 | networking |
| setsockopt | 54 | networking |
| getsockopt | 55 | networking |
| clone | 56 | threads |
| exit | 60 | processes |
| wait4 | 61 | processes |
| kill | 62 | signals |
| fcntl | 72 | files |
| fsync | 74 | files |
| ftruncate | 77 | files |
| rename | 82 | files |
| mkdir | 83 | files |
| unlink | 87 | files |
| gettid | 186 | threads |
| futex | 202 | locks |
| sched_getaffinity | 204 | CPU topology |
| clock_gettime | 228 | time |
| clock_nanosleep | 230 | time |
| epoll_wait | 232 | event loop |
| epoll_ctl | 233 | event loop |
| exit_group | 231 | processes |
| epoll_create1 | 291 | event loop |
| pipe2 | 293 | files |
| dup3 | 292 | files |
| openat | 257 | files |
| getdents64 | 217 | files |
| pread64 | 17 | files |
| accept4 | 288 | networking |
| timerfd_create | 283 | time |
| eventfd2 | 290 | event loop |
| signalfd4 | 289 | signals |
| getrandom | 318 | entropy |
| membarrier | 324 | memory ordering |
| prlimit64 | 302 | processes |
| statx | 332 | files |
| rseq | 334 | threads |
| clone3 | 435 | threads |
| pidfd_open | 434 | processes |
| io_uring_setup | 425 | event loop |
| io_uring_enter | 426 | event loop |
| io_uring_register | 427 | event loop |
| madvise | 28 | memory |
| ioctl | 16 | devices |
| sendmsg | 46 | networking |
| recvmsg | 47 | networking |
| sendmmsg | 307 | networking |

The table also lists x32 ABI entries (ioctl 514, sendmsg 518, recvmsg 519, sendmmsg 538). They belong to the x32 ABI, not to x86-64, and must not be used for the x86-64 target.

The `SYS_*` constants in `std/sys/linux.xl` for x86-64 match this table (checked: PIPE2 293, GETTID 186, CLONE 56, NANOSLEEP 35, EPOLL_CREATE1 291, EPOLL_CTL 233, EPOLL_WAIT 232, READ 0, WRITE 1, MMAP 9, MUNMAP 11, SCHED_YIELD 24, EXIT 60, EXIT_GROUP 231, FUTEX 202, GETRANDOM 318, CLOCK_GETTIME 228, OPEN 2, CLOSE 3, LSEEK 8, UNLINK 87, GETDENTS64 217, IOCTL 16, MPROTECT 10), as do the numbers in `std/sys/sock.xl` (SOCKET 41, CONNECT 42, ACCEPT 43, BIND 49, LISTEN 50, SETSOCKOPT 54, RT_SIGACTION 13), `std/os/process.xl` (GETPID 39, KILL 62, WAIT4 61, CLONE 56, EXECVE 59, EXIT 60), `std/io/stat.xl` (STATX 332) and `std/sys/uring.xl` (IO_URING_SETUP 425, IO_URING_ENTER 426).

## Not yet verified

The DRM, sysfs and block-device ioctl request numbers (they come from each subsystem's header, not from this table). Each must be taken from the table or header before use.
