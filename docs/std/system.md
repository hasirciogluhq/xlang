# System information

Four small modules read facts the operating system already publishes. None of them opens a device for writing.

| Module | Functions | Source |
|---|---|---|
| `std/cpu` | `cpuFeatures()` (CPU feature flags), `cpuCount()`, `cpuRelax()`, `stackPointer()` | CPUID and XGETBV on x86-64 (Intel SDM Vol. 2; the same on every OS, since they are processor instructions); `AT_HWCAP` on arm64 Linux (kernel `hwcap.h`); `sysctlbyname("hw.optional.*")` on arm64 macOS; `IsProcessorFeaturePresent` on arm64 Windows; `cpuCount` reads `sched_getaffinity` on Linux |
| `std/hw` | `hwCpuModel`, `hwMemoryBytes`, `hwProductName`, `hwCacheLineSize` | `/proc/cpuinfo`, `/proc/meminfo`, `/sys/class/dmi/id/product_name` and the sysfs coherency line size on Linux; `machdep.cpu.brand_string`, `hw.memsize`, `hw.model` and `hw.cachelinesize` on macOS; `PROCESSOR_IDENTIFIER` (`GetEnvironmentVariableW`, through `std/sys/windows` `sysGetEnvW`), `GlobalMemoryStatusEx` and a fixed 64 on Windows |
| `std/gpu` | `gpuDrmDevices`, `gpuAvailable` | DRM render nodes `/dev/dri/renderD*` on Linux, queried with `DRM_IOCTL_VERSION` (0xC0406400) |
| `std/io/dir` | `fsListDir` | `getdents64` on Linux (x86-64 217, arm64 61); `opendir`/`readdir`/`closedir` on macOS; `FindFirstFileW`/`FindNextFileW`/`FindClose` on Windows |
| `std/sys` | `envGet(name, out, cap)` | `/proc/self/environ` on Linux; `getenv` from libSystem on macOS; `GetEnvironmentVariableW` (through `sysGetEnvW`) on Windows |

```xlang
{% include system-info.xl %}
```

## What was checked

- CPU flags: the x86-64 probe runs in an amd64 Linux container and matches `/proc/cpuinfo` (the emulated CPU also reports
  LZCNT, which the kernel shows no `abm` flag for; the module follows the CPUID bit). arm64 Linux flags match the `Features` line
  of `/proc/cpuinfo`. macOS arm64 values come from sysctl.
- Hardware: memory and CPU model match `/proc/meminfo` and `/proc/cpuinfo` in containers, and `sysctl` on macOS.
- Directory listing: `fsListDir("/etc")` returns the same count as `ls -A` in a container.
- GPU: the enumeration runs on Linux and macOS and returns 0 where no render node exists; a dummy `renderD128` file is found,
  opened and rejected by the ioctl. A real DRM driver was not available, so the success path (driver name from
  `DRM_IOCTL_VERSION`) is written from `include/uapi/drm/drm.h` but not run.

## Not implemented

Windows returns empty or false where the OS publishes nothing: `hwProductName` has no registry-free source (see
[Known limitations]({{ site.baseurl }}/known-limitations.html)), and `gpuDrmDevices` needs DXGI, which is not wrapped.
The rest of the Windows columns are filled: `cpuFeatures` (CPUID/XGETBV on x86-64, `IsProcessorFeaturePresent` on arm64),
`hwCpuModel` (`PROCESSOR_IDENTIFIER`), `hwMemoryBytes` (`GlobalMemoryStatusEx`), `fsListDir` and the guarded mapping below.
macOS has no GPU enumeration (Metal and IOKit are deferred). The CPU flags do not cover every extension (SSE4A, AVX-VNNI,
AMX and the AVX-512 subsets beyond F, CD, DQ, BW and VL are not reported). Sysfs fields other than `product_name` are not read.

Environment variables: `envGet` reads the process environment (Linux from `/proc/self/environ`, which is the environment the
process started with; macOS through `getenv`; Windows through `GetEnvironmentVariableW`, which widens the ASCII name and takes
the low byte of each UTF-16 unit of the value into `out`). Checked with a variable set in a container and on macOS; the Windows
path is not covered by an e2e entry.

## Processes, file metadata, guarded stacks and io_uring

| Module | Functions | Source |
|---|---|---|
| `std/os/process` | `processId`, `processKill`, `processWait`, `processSpawn`, `processSpawnOut` (stdout and stderr into a file: Linux dup3 292/24, macOS `posix_spawn_file_actions_addopen`/`_adddup2`; Windows returns -1) | Linux: getpid 39/172, kill 62/129, wait4 61/260, clone 56/220 (SIGCHLD), execve 59/221, exit 60/93; macOS: libSystem getpid, kill, wait4, posix_spawn |
| `std/io/stat` | `fsStat` (mode, size, mtime) | Linux: statx 332/291 (stx_mode 28, stx_size 40, stx_mtime 112); macOS: libSystem stat (st_mode 4, st_size 96, st_mtime 48); Windows: `NtCreateFile` with `FILE_READ_ATTRIBUTES | SYNCHRONIZE` and `FILE_OPEN` (no `FILE_NON_DIRECTORY_FILE`, so a directory opens too), then `NtQueryInformationFile` class 4 `FileBasicInformation` ([MS-FSCC 2.4.7](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/16023025-8a78-492f-8b96-c873b042ac50): CreationTime 0, LastAccessTime 8, LastWriteTime 16, ChangeTime 24, each an `i64` of 100 ns ticks since 1601-01-01, then FileAttributes `ui32` at 32; 40 bytes) and class 5 `FileStandardInformation` (MS-FSCC 2.4.41: AllocationSize 0, EndOfFile 8, NumberOfLinks `ui32` 16, DeletePending 20, Directory 21; 24 bytes). Mode is synthesized: `FILE_ATTRIBUTE_DIRECTORY` (0x10) gives `S_IFDIR | 0755`, else `S_IFREG | 0644`, and `FILE_ATTRIBUTE_READONLY` (0x1) clears the write bits; mtime is `LastWriteTime / 10^7 - 11644473600` (seconds between 1601 and 1970) |
| `std/io/dir` | `fsListDir` (macOS and Windows now too) | macOS: libSystem opendir, readdir, closedir (struct dirent: d_type 20, d_name 21); Windows: kernel32 FindFirstFileW, FindNextFileW, FindClose (WIN32_FIND_DATAW: dwFileAttributes 0, cFileName 44) |
| `std/sys` | `sysMapGuarded`, `sysUnmapGuarded` | Linux: mprotect 10/arm64 226; macOS: BSD number 74 (the comment in `std/macos.xl`): a PROT_NONE page below the region; Windows: `VirtualAlloc` (MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE) over `size + dwPageSize`, then `VirtualProtect` of the lowest page to PAGE_NOACCESS, released with `VirtualFree(MEM_RELEASE)` |
| `std/sys/uring` | `uringAvailable`, `uringSetup`, `uringNop`, `uringClose` (Linux) | io_uring_setup 425, io_uring_enter 426; structure layouts from include/uapi/linux/io_uring.h |
| `std/sys` | `sysClockNanos` (wall clock in nanoseconds since the Unix epoch) | Linux: `clock_gettime(CLOCK_REALTIME)` (x86-64 228, arm64 113; `timespec` tv_sec 0, tv_nsec 8); macOS: libSystem `gettimeofday` (`timeval` tv_sec 0, tv_usec `i32` at 8, so microsecond resolution); Windows: `KUSER_SHARED_DATA.SystemTime` at 0x7FFE0014 (`KSYSTEM_TIME`: LowPart, High1Time, High2Time; 100 ns ticks since 1601-01-01, re-read until High1Time equals High2Time), minus 116444736000000000 ticks |
| `std/sys/proc` | `procList` (and `procListFrom`, Linux only; `ProcInfo`: pid, ppid, threads, rss, startTime, name) | Linux `/proc/<pid>/stat`; macOS `sysctl(CTL_KERN, KERN_PROC, KERN_PROC_ALL)` (`kinfo_proc`) plus `proc_name`; Windows a Toolhelp `CreateToolhelp32Snapshot` walked with `Process32FirstW`/`Process32NextW`, the `WCHAR` name converted with `std/encoding/utf16` `toUtf8` |
| `std/hw` | `hwCacheLineSize` | Linux: sysfs coherency_line_size; macOS: hw.cachelinesize; Windows: 64 |

Windows: memory (`GlobalMemoryStatusEx`), environment (`GetEnvironmentVariableW`), the CPU description (`PROCESSOR_IDENTIFIER`),
directory listing (ASCII and UTF-16 names), process listing (`procList`/`procListFrom` over a Toolhelp snapshot) and the
guarded mapping are wired through kernel32. The Windows file code is
cross-built for `x86_64-pc-windows-msvc` and runs under CrossOver/Wine only through the e2e programs that use it; the guarded
mapping and `fsListDir` have no dedicated e2e entry yet.

The process module starts children with an empty environment on both systems. The io_uring module exposes a ring and a NOP round trip; the event loop does not use it yet (epoll stays the default).
