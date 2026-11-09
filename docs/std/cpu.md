# CPU features

`std/cpu` reports which instruction-set extensions the processor has and the operating system enables. The call is
`cpuFeatures()`, which returns a `CpuFeatures` struct of flags. Nothing is cached: each call asks the processor again.

```xlang
{% include cpu-features.xl %}
```

## Where the flags come from

| Platform | Source | Reference |
|---|---|---|
| x86-64 | `CPUID` leaves 0, 1, 7 (sub-leaf 0) and `0x80000001`; `XGETBV` for the AVX and AVX-512 state | Intel SDM Vol. 2 (CPUID); the bit positions are from the Intel CPUID reference (https://www.felixcloutier.com/x86/cpuid) |
| arm64 Linux | `AT_HWCAP` (auxiliary vector entry 16, `/proc/self/auxv`) | `arch/arm64/include/uapi/asm/hwcap.h` (https://github.com/torvalds/linux) and `include/uapi/linux/auxvec.h` |
| arm64 macOS | `sysctlbyname("hw.optional.*")`, the names `hw.optional.neon` and `hw.optional.arm.FEAT_*` | the names read from `sysctl -a` on the target |
| Windows x86-64 | the same CPUID/XGETBV path as x86-64 Linux (processor instructions, no OS call) | Intel SDM Vol. 2 (CPUID) |
| Windows arm64 | `IsProcessorFeaturePresent` (`PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE` 30, `PF_ARM_V8_1_ATOMIC_INSTRUCTIONS_AVAILABLE` 34, `PF_ARM_V8_2_DP_INSTRUCTIONS_AVAILABLE` 43) | https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent |

AVX and AVX-512 flags are reported only when the OS saves the matching register state (XCR0 bits 1 and 2 for AVX, bits 5 to 7
for AVX-512). A processor that has AVX but an OS that has not enabled it reports `avx = false`.

## Checked

- x86-64 (run in an amd64 Linux container): the vendor string, the leaf 1 and leaf 7 flags match the container's
  `/proc/cpuinfo`. One difference: the emulated CPU reports the LZCNT bit (CPUID `0x80000001` ECX bit 5 = 1), while the kernel
  shows no `abm` flag in that container. The module follows the CPUID bit.
- arm64 Linux (run in an arm64 container): every flag matches the `Features` line of `/proc/cpuinfo`.
- arm64 macOS: the NEON, AES, PMULL, SHA, CRC32, LSE and dot-product flags are read through sysctl.
- arm64 Windows: NEON is true unconditionally (ASIMD is baseline on arm64 Windows), the Armv8 crypto query fills
  `aes`, `pmull`, `sha1` and `sha2`, and LSE and dot-product come from their own feature numbers. `sha3`, `sha512` and
  `crc32` stay false: Windows has no feature query for those instructions.

## Not included

SSE4A (AMD) is not reported: its bit is in the AMD manual and was not checked against it. Other x86 flags (AVX-VNNI, AMX,
the AVX-512 subsets beyond F, CD, DQ, BW, VL) and arm64 flags beyond those listed are not reported yet. Windows arm64 has no
query for the SHA-3, SHA-512 and CRC32 instructions, so those three flags are always false there.
