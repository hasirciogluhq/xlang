#pragma once

// Host process spawning: the one place that starts a child program and collects its exit status.
//
// POSIX (macOS, Linux) uses `fork`/`execv` and `pipe`/`poll`; Windows uses `CreateProcessW` and
// `CreatePipe`/`PeekNamedPipe`. The public functions are the same on both, so `xl run`,
// `xl test`, `xl install` and `xl self-update` carry no platform branch.
//
// Internal header: included as `cli/process.h` by `src/cli/*`, `src/pkg/*` and `src/driver/*`.

#include <string>
#include <vector>

namespace xlang::host {

enum class Stdio {
    /// stdout and stderr go to the two pipes of the parent (`fork` + `dup2`, or `STARTUPINFO`'s
    /// `hStdOutput`/`hStdError`). stdin is the null device.
    Captured,
    /// The child inherits this process's stdin, stdout and stderr: the interactive form, used by
    /// `xl run` and `xl test`, where the program under test owns the terminal.
    Inherited,
};

struct RunOptions {
    Stdio stdio{Stdio::Captured};
    /// Working directory of the child. Empty means "stay where the parent is".
    std::string cwd;
};

struct RunResult {
    /// Exit status of the child: the process exit code, or 128+signal on POSIX when the program was
    /// killed by a signal. -1 when it could not be started.
    int status{-1};
    std::string out; ///< stdout, `Stdio::Captured` only
    std::string err; ///< stderr, `Stdio::Captured` only
};

/// Starts `argv[0]` (an absolute path, or a name searched in `PATH`) with the arguments
/// `argv[1..]`, waits for it, and returns its exit status. stdout and stderr are collected
/// separately in `RunOptions::Captured` mode; they are never merged, because a caller reports which
/// stream said what.
[[nodiscard]] RunResult run(const std::vector<std::string>& argv, const RunOptions& options = {});

} // namespace xlang::host
