# System calls

The standard library reaches the operating system in two ways: raw system calls through the `syscall` builtin, and calls into OS libraries declared with `@library`. Both stay behind `#if os` in `std/sys`, and user programs never name an OS call directly.

System call numbers are recorded per operating system and architecture, from the primary source named in each page. A number that has not been read from a primary source is not used in code.

- [Linux x86-64](linux-x86_64.html)
- [Linux arm64](linux-arm64.html)
- [macOS (XNU)](macos-xnu.html)

Windows has no stable system call numbers. The standard library calls documented functions in `ntdll.dll`, `kernel32.dll`, `ws2_32.dll` and `bcrypt.dll` instead, and each one is checked against Microsoft's documentation before use.

The numbers are verified against the kernel tables and the XNU `syscalls.master` file. The policy is that a number is written into these pages before any code uses it.
