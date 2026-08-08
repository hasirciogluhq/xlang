#include "xlang/host/platform.h"

#include <cstdlib>
#include <string>
#include <vector>

#if defined(__linux__)
#  define XLANG_HOST_LINUX 1
#elif defined(__APPLE__)
#  define XLANG_HOST_MACOSX 1
#elif defined(_WIN32)
#  define XLANG_HOST_WINDOWS 1
#endif

namespace xlang::platform {

Os hostOs() {
#if defined(XLANG_HOST_LINUX)
    return Os::Linux;
#elif defined(XLANG_HOST_MACOSX)
    return Os::Macosx;
#elif defined(XLANG_HOST_WINDOWS)
    return Os::Windows;
#else
    return Os::Unknown;
#endif
}

Arch hostArch() {
#if defined(__x86_64__) || defined(_M_X64)
    return Arch::X64;
#elif defined(__i386__) || defined(_M_IX86)
    return Arch::X86;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return Arch::Arm64;
#else
    return Arch::Unknown;
#endif
}

std::filesystem::path homeDir() {
#if defined(XLANG_HOST_WINDOWS)
    if (const char* profile = std::getenv("USERPROFILE")) {
        return profile;
    }
    return {};
#else
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
    return {};
#endif
}

char pathListSeparator() {
#if defined(XLANG_HOST_WINDOWS)
    return ';';
#else
    return ':';
#endif
}

std::vector<std::string> baselineSyslibFlags(Os os) {
    switch (os) {
    case Os::Linux:
        return {"-pthread", "-ldl", "-lm"};
    case Os::Macosx:
        return {"-pthread"};
    case Os::Windows:
        return {"kernel32.lib", "advapi32.lib"};
    default:
        return {};
    }
}

std::string exeSuffix(Os os) {
    return os == Os::Windows ? ".exe" : "";
}

std::string staticLibPrefix(Os os) {
    return os == Os::Windows ? "" : "lib";
}

std::string staticLibSuffix(Os os) {
    return os == Os::Windows ? ".lib" : ".a";
}

std::string sharedLibSuffix(Os os) {
    switch (os) {
    case Os::Windows:
        return ".dll";
    case Os::Macosx:
        return ".dylib";
    default:
        return ".so";
    }
}

std::string objectSuffix(Os os) {
    return os == Os::Windows ? ".obj" : ".o";
}

std::string defaultTriple(Os os, Arch arch) {
    std::string arch_s;
    switch (arch) {
    case Arch::X86:
        arch_s = "i386";
        break;
    case Arch::X64:
        arch_s = "x86_64";
        break;
    case Arch::Arm64:
        arch_s = "aarch64";
        break;
    default:
        arch_s = "unknown";
        break;
    }
    switch (os) {
    case Os::Linux:
        return arch_s + "-unknown-linux-gnu";
    case Os::Macosx:
        return arch_s + "-apple-darwin";
    case Os::Windows:
        return arch_s + "-pc-windows-msvc";
    default:
        return arch_s + "-unknown-unknown";
    }
}

}  // namespace xlang::platform
