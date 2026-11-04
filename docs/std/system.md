# System information

Four small modules read facts the operating system already publishes. None of them opens a device for writing.

| Module | Functions | Source |
|---|---|---|
| `std/cpu` | `cpuFeatures()` (CPU feature flags) | CPUID and XGETBV on x86-64 (Intel SDM Vol. 2); `AT_HWCAP` on arm64 Linux (kernel `hwcap.h`); `sysctlbyname("hw.optional.*")` on arm64 macOS |
| `std/hw` | `hwCpuModel`, `hwMemoryBytes`, `hwProductName` | `/proc/cpuinfo`, `/proc/meminfo` and `/sys/class/dmi/id/product_name` on Linux; `machdep.cpu.brand_string`, `hw.memsize`, `hw.model` on macOS |
| `std/gpu` | `gpuDrmDevices`, `gpuAvailable` | DRM render nodes `/dev/dri/renderD*` on Linux, queried with `DRM_IOCTL_VERSION` (0xC0406400) |
| `std/io/dir` | `fsListDir` | `getdents64` on Linux (x86-64 217, arm64 61) |
| `std/sys` | `envGet(name, out, cap)` | `/proc/self/environ` on Linux; `getenv` from libSystem on macOS |

```xlang
{% include_relative ../examples/system-info.xl %}
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

Windows returns empty or false for every function. macOS has no directory listing (`getdirentries` is not verified in the XNU
table yet) and no GPU enumeration (Metal and IOKit are deferred). The CPU flags do not cover every extension (SSE4A, AVX-VNNI,
AMX and the AVX-512 subsets beyond F, CD, DQ, BW and VL are not reported). Sysfs fields other than `product_name` are not read.

Environment variables: `envGet` reads the process environment (Linux from `/proc/self/environ`, which is the environment the
process started with; macOS through `getenv`). Checked with a variable set in a container and on macOS. Windows is not
implemented.
