#include "xlang/host/resolve.h"

#include <cctype>
#include <cstdlib>

namespace xlang {
namespace {

std::string toLower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

platform::Os parseOs(std::string_view name) {
    const std::string n = toLower(name);
    if (n == "linux") {
        return platform::Os::Linux;
    }
    if (n == "macosx" || n == "macos" || n == "darwin" || n == "osx") {
        return platform::Os::Macosx;
    }
    if (n == "windows" || n == "win32" || n == "win") {
        return platform::Os::Windows;
    }
    return platform::Os::Unknown;
}

platform::Arch parseArch(std::string_view name) {
    const std::string n = toLower(name);
    if (n == "x86" || n == "i386" || n == "i686") {
        return platform::Arch::X86;
    }
    if (n == "x64" || n == "x86_64" || n == "amd64") {
        return platform::Arch::X64;
    }
    if (n == "arm64" || n == "aarch64") {
        return platform::Arch::Arm64;
    }
    return platform::Arch::Unknown;
}

std::string osName(platform::Os os) {
    switch (os) {
    case platform::Os::Linux:
        return "linux";
    case platform::Os::Macosx:
        return "macosx";
    case platform::Os::Windows:
        return "windows";
    default:
        return "unknown";
    }
}

std::string archName(platform::Arch arch) {
    switch (arch) {
    case platform::Arch::X86:
        return "x86";
    case platform::Arch::X64:
        return "x64";
    case platform::Arch::Arm64:
        return "arm64";
    default:
        return "unknown";
    }
}

TargetSpec resolveTarget(std::optional<std::string> os_override,
                         std::optional<std::string> arch_override,
                         std::optional<std::string> triple_override) {
    TargetSpec spec;

    std::optional<std::string> os = os_override;
    std::optional<std::string> arch = arch_override;

#if defined(XLANG_TARGET_OS)
    if (!os) {
        os = XLANG_TARGET_OS;
    }
#endif
#if defined(XLANG_TARGET_ARCH)
    if (!arch) {
        arch = XLANG_TARGET_ARCH;
    }
#endif
    if (!os) {
        if (const char* env = std::getenv("XLANG_TARGET_OS")) {
            os = env;
        }
    }
    if (!arch) {
        if (const char* env = std::getenv("XLANG_TARGET_ARCH")) {
            arch = env;
        }
    }

    spec.os = os ? parseOs(*os) : platform::hostOs();
    spec.arch = arch ? parseArch(*arch) : platform::hostArch();
    if (triple_override && !triple_override->empty()) {
        spec.triple = *triple_override;
    } else if (const char* env = std::getenv("XLANG_TARGET_TRIPLE")) {
        spec.triple = env;
    } else {
        spec.triple = platform::defaultTriple(spec.os, spec.arch);
    }
    return spec;
}

}  // namespace xlang
