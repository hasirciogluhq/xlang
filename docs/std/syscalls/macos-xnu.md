# macOS (XNU) BSD system call numbers

Source: `bsd/kern/syscalls.master` from the apple-oss-distributions/xnu main branch, read at
https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/kern/syscalls.master.
The table is per XNU release; any value used by code must be checked again against the release
that the target macOS ships. The release tag of the table read is not yet recorded in this file: todo.

arm64 BSD calls (read from `bsd/dev/arm/systemcalls.c`, function `arm_get_syscall_number()`):
- The number is taken from the register named by the macro `ARM64_SYSCALL_CODE_REG_NUM`. That macro is not defined in the file read; its definition (expected to be `x16`) must be read from the arm64 headers before the register is written in code: todo.
- If that register holds zero, the code uses `x0` as the number and the first argument moves to `x1` (indirect syscall).
- Errors: `x0` holds the errno and the carry bit (`PSR64_CF`) of `cpsr` is set. Success: the result is in `x0`; for two-register return types `x1` is also used; the carry bit is cleared before the call.
- The trap instruction and its immediate (`svc #0x80` is the value commonly used, not yet read from the source): todo.
- `ERESTART` moves the program counter back by 4 bytes (one `svc`).
- There is no BSD `mprotect` entry in this table: the guarded mapping uses BSD number 74, as recorded in the comment in `std/sys/macos.xl`.

Mach traps are a separate table (`osfmk/mach/syscall_sw.h`) and are not recorded here: todo.

## Verified numbers

| Name | Number |
|---|---|
| read | 3 |
| write | 4 |
| open | 5 |
| close | 6 |
| access | 33 |
| mmap | 197 |
| accept | 30 |
| socket | 97 |
| connect | 98 |
| bind | 104 |
| listen | 106 |
| ioctl | 54 |
| select | 93 |
| poll | 230 |
| shm_open | 266 |
| psynch_mutexwait | 301 |
| workq_kernreturn | 368 |
| kqueue | 362 |
| kevent | 363 |
| kevent64 | 369 |
| thread_selfid | 372 |
| getentropy | 500 |

Also verified in the same table: munmap 73, bsdthread_create 360, bsdthread_terminate 361, unlink 10, lseek 199, gettimeofday 116, exit 1, read 3, write 4, open 5, close 6. The socket calls are in the table too: socket 97, connect 98, bind 104, setsockopt 105, listen 106, accept 30 (used by `std/sys/sock.xl`).

`sched_yield` is absent by name from this table; the former `SYS_SCHED_YIELD = 331` in `std/sys/macos.xl` was removed because no entry was verified. Yield on macOS is a no-op until a verified call is recorded.

Checked and absent at the number tried: `__sysctl` (the entry at that position is ntp_adjtime, 527),
and `accept_nocancel` (366 is bsdthread_register). Numbers 16 and 366 are not the calls they are
on other systems, so do not assume a Linux or FreeBSD number carries over.
