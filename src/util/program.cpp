// Where the running program is (see `include/xlang/util/program.h` for the contract).

#include "xlang/util/program.h"

#include "cli/process.h"

#if defined(_WIN32)

#include <windows.h>

#include <process.h>  // `_getpid`

#elif defined(__APPLE__)

#include <mach-o/dyld.h>

#include <unistd.h>

#else

#include <unistd.h>

#endif

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xlang {

#if defined(_WIN32)

namespace {

/// UTF-16 from a Win32 `...W` call to UTF-8.
[[nodiscard]] std::string utf8(const wchar_t* text) {
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string narrow(static_cast<std::size_t>(needed - 1), '\0'); // -1: without the terminator
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), needed, nullptr, nullptr);
    return narrow;
}

} // namespace

std::string selfExecutable(const char* argv0) {
    // https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew
    // The buffer is grown until the name fits: 32767 is the longest path a Win32 call accepts.
    for (std::uint32_t size = MAX_PATH; size <= 32768; size *= 2) {
        std::wstring buffer(size, L'\0');
        const DWORD copied = ::GetModuleFileNameW(nullptr, buffer.data(), size);
        if (copied == 0) {
            break;
        }
        if (copied < size) {
            buffer.resize(copied);
            std::error_code ec;
            const std::filesystem::path p =
                std::filesystem::absolute(std::filesystem::path(buffer), ec);
            return ec ? utf8(buffer.c_str()) : p.string();
        }
    }
    std::error_code ec;
    return std::filesystem::absolute(argv0, ec).string();
}

#else

std::string selfExecutable(const char* argv0) {
#if defined(__APPLE__)
    // dyld reports the path of the image; the buffer is grown when the first one is too small.
    for (std::uint32_t size = 4096;; size *= 2) {
        std::vector<char> buf(size);
        std::uint32_t got = size;
        if (_NSGetExecutablePath(buf.data(), &got) == 0) {
            std::error_code ec;
            const std::filesystem::path p = std::filesystem::canonical(buf.data(), ec);
            return ec ? std::string(buf.data()) : p.string();
        }
        if (got <= size) {
            break; // reported the same size again: something else is wrong
        }
    }
#else
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return p.string();
    }
#endif
    std::error_code fallback;
    return std::filesystem::absolute(argv0, fallback).string();
}

#endif

std::string programOutput(const std::string& program, const std::vector<std::string>& args) {
    std::vector<std::string> argv;
    argv.reserve(args.size() + 1);
    argv.push_back(program);
    argv.insert(argv.end(), args.begin(), args.end());
    return host::run(argv, host::RunOptions{.stdio = host::Stdio::Captured}).out;
}

int processId() {
    return ::_getpid();
}

} // namespace xlang
