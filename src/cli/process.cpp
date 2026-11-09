// Host process spawning (see `cli/process.h` for the contract).

#include "cli/process.h"

#if defined(_WIN32)

// Windows: `CreateProcessW` with two anonymous pipes.
//   https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
//   https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createpipe

#include <windows.h>

#else

// POSIX: `fork`/`execv`, two pipes, `poll` over both read ends.

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>

extern char** environ;

#endif

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace xlang::host {

#if defined(_WIN32)

namespace {

void closeHandle(HANDLE& h) {
    if (h != nullptr) {
        ::CloseHandle(h);
        h = nullptr;
    }
}

/// UTF-8 (`std::string`, what the rest of the compiler holds) to the UTF-16 a Win32 `...W` call
/// takes.
[[nodiscard]] std::u16string utf16(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int needed =
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    // wchar_t is 16 bits on Windows, so u16string and wstring hold the same bytes.
    std::u16string wide(static_cast<std::size_t>(needed), u'\0');
    ::MultiByteToWideChar(CP_UTF8,
                          0,
                          text.data(),
                          static_cast<int>(text.size()),
                          reinterpret_cast<wchar_t*>(wide.data()),
                          needed);
    return wide;
}

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

/// Appends `arg` to a command line, quoted by the rules `CommandLineToArgvW` undoes:
/// https://learn.microsoft.com/en-us/cpp/cpp/main-function-command-line-args
/// A backslash is literal unless it stands directly before a `"` (then it escapes it) or at the end
/// of the argument inside the quotes (then it has to be doubled), so the run of backslashes before
/// a quote is doubled and the quote itself escaped.
void appendQuoted(std::u16string& line, const std::u16string& arg) {
    if (!line.empty()) {
        line.push_back(u' ');
    }
    if (!arg.empty() && arg.find_first_of(u" \t\n\v\"") == std::u16string::npos) {
        line += arg; // nothing to quote
        return;
    }
    line.push_back(u'"');
    for (std::size_t i = 0;;) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == u'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            line.append(backslashes * 2,
                        u'\\'); // doubled: otherwise they would escape the closing quote
            break;
        }
        if (arg[i] == u'"') {
            line.append(backslashes * 2, u'\\').push_back(u'\\');
        } else {
            line.append(backslashes, u'\\');
        }
        line.push_back(arg[i]);
        ++i;
    }
    line.push_back(u'"');
}

/// The command line of `argv` (already UTF-16): every argument quoted, the program name first.
[[nodiscard]] std::u16string commandLine(const std::vector<std::u16string>& argv) {
    std::u16string line;
    for (const std::u16string& arg : argv) {
        appendQuoted(line, arg);
    }
    return line;
}

/// `name` with `.exe` appended when it has no extension; a Win32 program is always `name.exe`, so a
/// caller that names `git` means `git.exe` (a name that already carries `.exe` or another extension
/// is left alone).
[[nodiscard]] std::u16string executableName(std::u16string name) {
    if (name.find(u'.') == std::u16string::npos) {
        name += u".exe";
    }
    return name;
}

/// The child's environment (`CREATE_UNICODE_ENVIRONMENT`): this process's environment with
/// `GIT_TERMINAL_PROMPT=0` set, so a private repository fails instead of waiting for a password.
/// Windows has no `setenv` that reaches a child, so the block is built explicitly.
[[nodiscard]] std::u16string childEnvironment() {
    constexpr auto kPrompt = u"GIT_TERMINAL_PROMPT=0";
    if (const wchar_t* block = ::GetEnvironmentStringsW(); block != nullptr) {
        std::u16string text;
        for (const wchar_t* entry = block; *entry != L'\0';) {
            const std::u16string line(entry); // up to and including the terminator
            const bool prompt = line.starts_with(u"GIT_TERMINAL_PROMPT=");
            entry += line.size();
            if (prompt) {
                continue; // replaced below
            }
            text += line;
        }
        ::FreeEnvironmentStringsW(const_cast<wchar_t*>(block));
        text += kPrompt;
        text.push_back(u'\0');
        text.push_back(u'\0');
        return text;
    }
    std::u16string text(kPrompt);
    text.push_back(u'\0');
    text.push_back(u'\0');
    return text;
}

/// Reads what is available on `pipe` into `sink`. False once the pipe is done: the child closed its
/// end (`ERROR_BROKEN_PIPE`) or the handle failed some other way.
[[nodiscard]] bool drain(HANDLE& pipe, std::string& sink) {
    if (pipe == nullptr) {
        return false;
    }
    std::vector<char> buffer(4096);
    DWORD available = 0;
    if (::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) == 0) {
        // https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-peeknamedpipe
        closeHandle(pipe);
        return false;
    }
    if (available == 0) {
        return true; // nothing yet, or nothing more; the other pipe still has to be looked at
    }
    DWORD got = 0;
    if (::ReadFile(pipe,
                   buffer.data(),
                   static_cast<DWORD>(std::min<std::size_t>(available, buffer.size())),
                   &got,
                   nullptr) == 0) {
        closeHandle(pipe);
        return false;
    }
    sink.append(buffer.data(), got);
    return true;
}

} // namespace

RunResult run(const std::vector<std::string>& argv, const RunOptions& options) {
    RunResult r;
    if (argv.empty()) {
        return r;
    }
    const bool captured = options.stdio == Stdio::Captured;

    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    HANDLE err_read = nullptr;
    HANDLE err_write = nullptr;
    HANDLE null_input = nullptr;
    if (captured) {
        if (::CreatePipe(&out_read, &out_write, &inheritable, 0) == 0) {
            return r;
        }
        if (::CreatePipe(&err_read, &err_write, &inheritable, 0) == 0) {
            closeHandle(out_read);
            closeHandle(out_write);
            return r;
        }
        // The read ends stay in this process: the child sees only the write ends.
        ::SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
        ::SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
        null_input = ::CreateFileW(L"NUL",
                                   GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &inheritable,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (captured) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = null_input != nullptr ? null_input : ::GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = out_write;
        startup.hStdError = err_write;
    } else {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    }

    std::vector<std::u16string> wide;
    wide.reserve(argv.size());
    for (const std::string& a : argv) {
        wide.push_back(utf16(a));
    }
    wide[0] = executableName(wide[0]);
    // `lpApplicationName` names the image; `lpCommandLine` carries the arguments, since that is
    // what a program reads. A relative name is looked up in `PATH` by `CreateProcessW` while
    // `lpApplicationName` is null, so only an absolute path is passed there.
    const std::u16string application =
        std::filesystem::path(utf8(wide[0].c_str())).is_absolute() ? wide[0] : std::u16string();
    std::u16string line = commandLine(wide);
    std::u16string directory = utf16(options.cwd);
    std::u16string environment = childEnvironment();

    PROCESS_INFORMATION child{};
    // CREATE_NO_WINDOW: no console window flashes up for `git`. CREATE_UNICODE_ENVIRONMENT: the
    // environment block above is UTF-16.
    const BOOL started = ::CreateProcessW(
        application.empty() ? nullptr : reinterpret_cast<const wchar_t*>(application.data()),
        reinterpret_cast<wchar_t*>(line.data()),
        nullptr,
        nullptr,
        inheritable.bInheritHandle,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment.data() == nullptr ? nullptr : reinterpret_cast<void*>(environment.data()),
        directory.empty() ? nullptr : reinterpret_cast<const wchar_t*>(directory.data()),
        &startup,
        &child);

    // The child holds its own copies of the write ends now; the parent must drop its copies or the
    // pipes never reach end-of-file.
    closeHandle(out_write);
    closeHandle(err_write);
    closeHandle(null_input);
    if (started == 0) {
        closeHandle(out_read);
        closeHandle(err_read);
        return r;
    }
    ::CloseHandle(child.hThread);

    if (captured) {
        // Deadlock avoidance: the child may fill one pipe while this process reads the other, and a
        // full pipe blocks the child. So the two pipes are drained *interleaved* in one loop and
        // every read is non-blocking (`PeekNamedPipe` reports how many bytes are ready), which
        // keeps both pipes empty and needs no thread. A blocked child is fine while it produces
        // nothing: the loop waits on it below and its pipes keep their data for the next pass.
        while (out_read != nullptr || err_read != nullptr) {
            const bool out_open = drain(out_read, r.out);
            const bool err_open = drain(err_read, r.err);
            if (out_open && err_open) {
                ::WaitForSingleObject(child.hProcess, 10);
            }
        }
    }
    ::WaitForSingleObject(child.hProcess, INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(child.hProcess, &code);
    ::CloseHandle(child.hProcess);
    r.status = static_cast<int>(code);
    return r;
}

#else

RunResult run(const std::vector<std::string>& argv, const RunOptions& options) {
    RunResult r;
    if (argv.empty()) {
        return r;
    }
    const bool captured = options.stdio == Stdio::Captured;

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (captured) {
        if (::pipe(out_pipe) != 0) {
            return r;
        }
        if (::pipe(err_pipe) != 0) {
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            return r;
        }
    }
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& a : argv) {
        args.push_back(const_cast<char*>(a.c_str()));
    }
    args.push_back(nullptr);

    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        for (const int fd : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
        return r;
    }
    if (pid == 0) {
        if (captured) {
            ::dup2(out_pipe[1], STDOUT_FILENO);
            ::dup2(err_pipe[1], STDERR_FILENO);
            for (const int fd : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) {
                ::close(fd);
            }
            const int null =
                ::open("/dev/null", O_RDONLY); // the child never reads: no stray prompt
            if (null >= 0) {
                ::dup2(null, STDIN_FILENO);
            }
            ::setenv("GIT_TERMINAL_PROMPT", "0", 1);
        }
        if (!options.cwd.empty() && ::chdir(options.cwd.c_str()) != 0) {
            ::_exit(127);
        }
        ::execv(args[0], args.data());
        ::_exit(127);
    }
    if (captured) {
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);
        struct pollfd fds[2] = {{out_pipe[0], POLLIN, 0}, {err_pipe[0], POLLIN, 0}};
        int open_fds = 2;
        char buf[4096];
        while (open_fds > 0) {
            if (::poll(fds, 2, -1) < 0 && errno != EINTR) {
                break;
            }
            for (int k = 0; k < 2; ++k) {
                if (fds[k].fd < 0 || (fds[k].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                    continue;
                }
                const ssize_t n = ::read(fds[k].fd, buf, sizeof buf);
                if (n > 0) {
                    (k == 0 ? r.out : r.err).append(buf, static_cast<std::size_t>(n));
                } else if (n == 0 || errno != EINTR) {
                    ::close(fds[k].fd);
                    fds[k].fd = -1;
                    --open_fds;
                }
            }
        }
    }
    int wstatus = 0;
    while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(wstatus)) {
        r.status = WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        r.status = 128 + WTERMSIG(wstatus); // what a shell reports
    }
    return r;
}

#endif

} // namespace xlang::host
