#pragma once

// Host queries about programs and paths. The CLI starts child programs (`xlang run`, `xlang test`,
// `xlang self-update`) and has to know where its own image, `git`, `curl` and the linker tools
// live, so the platform answer to "where is this program?" lives here instead of being repeated per
// file.

#include <filesystem>
#include <string>
#include <vector>

namespace xlang {

/// Path of the running executable. `argv0` is the fallback: not every OS tells where the image
/// lives, so the caller's first argument is used when the OS query fails.
///
///   macOS:   `_NSGetExecutablePath` (dyld), canonicalised.
///   Linux:   `/proc/self/exe`.
///   Windows: `GetModuleFileNameW`
///            (https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew),
///            converted from UTF-16 to UTF-8 (CLI11 already decoded the command line, but the image
///            path is asked for directly).
///
/// Used by `xlang run`/`xlang test` to re-invoke the compiler and by `xlang self-update` to replace
/// the binary it is running from.
[[nodiscard]] std::string selfExecutable(const char* argv0);

/// Runs `program args...` with the two output pipes kept apart and returns what `--version`,
/// `--help` and similar print on stdout; empty when the program does not start or fails. The name
/// is resolved the way the OS runs a child (`PATH` on POSIX, `name.exe` on Windows).
[[nodiscard]] std::string programOutput(const std::string& program,
                                        const std::vector<std::string>& args);

/// This process's id, for unique temporary names (`getpid` on POSIX, `_getpid` on Windows).
[[nodiscard]] int processId();

} // namespace xlang
