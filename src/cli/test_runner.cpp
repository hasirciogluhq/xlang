#include "test_runner.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <regex>
#include <set>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace xlang::cli {

namespace {

constexpr const char* kTestSuffix = ".test.xl";

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool hasGlob(const std::string& s) {
    return s.find_first_of("*?[") != std::string::npos;
}

/// `**` crosses directories (`a/**/b` also matches `a/b`), `*` and `?` stay inside one path segment,
/// `[abc]` is a character class.
std::regex globToRegex(const std::string& glob) {
    std::string re = "^";
    for (std::size_t i = 0; i < glob.size(); ++i) {
        const char c = glob[i];
        if (c == '*') {
            if (i + 1 < glob.size() && glob[i + 1] == '*') {
                i += 1;
                if (i + 1 < glob.size() && glob[i + 1] == '/') {
                    i += 1;
                    re += "(?:.*/)?";
                } else {
                    re += ".*";
                }
            } else {
                re += "[^/]*";
            }
        } else if (c == '?') {
            re += "[^/]";
        } else if (c == '[') {
            const std::size_t close = glob.find(']', i + 1);
            if (close == std::string::npos) {
                re += "\\[";
            } else {
                std::string cls = glob.substr(i + 1, close - i - 1);
                if (!cls.empty() && cls[0] == '!') {
                    cls[0] = '^';
                }
                re += "[" + cls + "]";
                i = close;
            }
        } else if (std::string(".+()^$|{}\\").find(c) != std::string::npos) {
            re += '\\';
            re += c;
        } else {
            re += c;
        }
    }
    re += "$";
    return std::regex(re);
}

std::string normalize(const std::string& p) {
    std::string s = fs::path(p).lexically_normal().generic_string();
    while (s.starts_with("./")) {
        s.erase(0, 2);
    }
    while (s.size() > 1 && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

bool skippedDir(const std::string& name) {
    return name == "build" || name == "node_modules" || (!name.empty() && name[0] == '.');
}

/// Every `.xl` file under `dir`, as `/`-separated paths relative to the current directory.
std::vector<std::string> walk(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        if (it->is_directory(ec)) {
            if (skippedDir(p.filename().string())) {
                it.disable_recursion_pending();
            }
        } else if (p.extension() == ".xl") {
            out.push_back(normalize(p.string()));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool isTestFile(const std::string& path) {
    return endsWith(path, kTestSuffix);
}

/// True when `path` or one of its parent directories matches `rx`.
bool matchesWithAncestors(const std::string& path, const std::regex& rx) {
    std::string cur = path;
    while (true) {
        if (std::regex_match(cur, rx)) {
            return true;
        }
        const std::size_t slash = cur.rfind('/');
        if (slash == std::string::npos) {
            return false;
        }
        cur.erase(slash);
    }
}

/// Files selected by one pattern; `error` is set when the pattern can never match anything.
std::vector<std::string> select(const std::string& pattern,
                                const std::vector<std::string>& all_files,
                                std::string& error) {
    std::vector<std::string> out;
    if (pattern.starts_with("re:")) {
        std::regex rx;
        try {
            rx = std::regex(pattern.substr(3));
        } catch (const std::regex_error& e) {
            error = "invalid regex `" + pattern.substr(3) + "`: " + e.what();
            return out;
        }
        for (const std::string& f : all_files) {
            if (isTestFile(f) && std::regex_search(f, rx)) {
                out.push_back(f);
            }
        }
        return out;
    }
    if (hasGlob(pattern)) {
        std::regex rx;
        try {
            rx = globToRegex(normalize(pattern));
        } catch (const std::regex_error& e) {
            error = "invalid glob `" + pattern + "`: " + e.what();
            return out;
        }
        const bool explicit_file = endsWith(pattern, ".xl");
        for (const std::string& f : all_files) {
            if ((explicit_file || isTestFile(f)) && matchesWithAncestors(f, rx)) {
                out.push_back(f);
            }
        }
        return out;
    }
    std::error_code ec;
    if (fs::is_directory(pattern, ec)) {
        for (const std::string& f : walk(pattern)) {
            if (isTestFile(f)) {
                out.push_back(f);
            }
        }
        return out;
    }
    if (fs::is_regular_file(pattern, ec)) {
        out.push_back(normalize(pattern));
        return out;
    }
    // Not a path: a substring of the test file path (`xlang test json`).
    for (const std::string& f : all_files) {
        if (isTestFile(f) && f.find(pattern) != std::string::npos) {
            out.push_back(f);
        }
    }
    return out;
}

struct Palette {
    bool on;
    const char* reset() const { return on ? "\x1b[0m" : ""; }
    const char* bold() const { return on ? "\x1b[1m" : ""; }
    const char* dim() const { return on ? "\x1b[2m" : ""; }
    const char* red() const { return on ? "\x1b[31m" : ""; }
    const char* green() const { return on ? "\x1b[32m" : ""; }
    const char* cyan() const { return on ? "\x1b[36m" : ""; }
    const char* inverseGreen() const { return on ? "\x1b[1;30;42m" : ""; }
    const char* inverseRed() const { return on ? "\x1b[1;37;41m" : ""; }
    const char* inverseCyan() const { return on ? "\x1b[1;30;46m" : ""; }
};

/// Runs `argv` and waits; the child inherits stdout/stderr. Returns the exit status (128+signal when
/// killed by a signal, -1 when the process could not be started).
int runProcess(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const std::string& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        ::execv(argv[0], argv.data());
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

}  // namespace

std::string selfExecutable(const char* argv0) {
#ifdef __APPLE__
    char buf[4096];
    std::uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::error_code ec;
        const fs::path p = fs::canonical(buf, ec);
        return ec ? std::string(buf) : p.string();
    }
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return p.string();
    }
#endif
    std::error_code ec2;
    return fs::absolute(argv0, ec2).string();
}

int runProgram(const std::string& self, const std::string& file, const std::vector<std::string>& build_args) {
    std::error_code ec;
    const fs::path exe = fs::temp_directory_path() / ("xlang-run-" + std::to_string(::getpid()));
    std::vector<std::string> build = {self, "build", file, "-o", exe.string()};
    build.insert(build.end(), build_args.begin(), build_args.end());
    int code = runProcess(build);
    if (code == 0) {
        code = runProcess({exe.string()});
    }
    fs::remove(exe, ec);
    return code;
}

int runTests(const TestRunOptions& options, const std::string& self) {
    const Palette c{options.color};
    const std::vector<std::string> all_files = walk(".");

    std::vector<std::string> patterns = options.patterns;
    if (patterns.empty()) {
        patterns.push_back("**/*.test.xl");
    }

    std::set<std::string> chosen;
    for (const std::string& pattern : patterns) {
        std::string error;
        const std::vector<std::string> found = select(pattern, all_files, error);
        if (!error.empty()) {
            std::cerr << c.red() << "error: " << c.reset() << error << '\n';
            return 1;
        }
        if (found.empty()) {
            std::cerr << c.red() << "error: " << c.reset() << "no test files match `" << pattern << "`\n";
            return 1;
        }
        chosen.insert(found.begin(), found.end());
    }
    for (const std::string& pattern : options.excludes) {
        std::string error;
        for (const std::string& f : select(pattern, all_files, error)) {
            chosen.erase(f);
        }
        if (!error.empty()) {
            std::cerr << c.red() << "error: " << c.reset() << error << '\n';
            return 1;
        }
    }
    if (chosen.empty()) {
        std::cerr << c.red() << "error: " << c.reset() << "no test files found\n";
        return 1;
    }

    if (options.list_only) {
        for (const std::string& f : chosen) {
            std::cout << f << '\n';
        }
        return 0;
    }

    std::error_code ec;
    const fs::path work = fs::temp_directory_path() / ("xlang-test-" + std::to_string(::getpid()));
    fs::create_directories(work, ec);

    std::vector<std::string> failed;
    std::size_t passed = 0;
    std::size_t index = 0;
    const auto started = std::chrono::steady_clock::now();
    for (const std::string& file : chosen) {
        const fs::path exe = work / ("t" + std::to_string(index++));
        std::cout << c.inverseCyan() << " RUN " << c.reset() << ' ' << file << std::endl;

        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::string> build = {self, "build", file, "-o", exe.string()};
        build.insert(build.end(), options.build_args.begin(), options.build_args.end());
        if (!options.color) {
            build.push_back("-D");
            build.push_back("xlTestNoColor");
        }
        int code = runProcess(build);
        bool built = code == 0;
        if (built) {
            code = runProcess({exe.string()});
        }
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

        if (code == 0) {
            ++passed;
            std::cout << c.inverseGreen() << " PASS " << c.reset() << ' ' << file << c.dim() << " (" << ms
                      << " ms)" << c.reset() << "\n\n";
        } else {
            failed.push_back(file);
            std::cout << c.inverseRed() << " FAIL " << c.reset() << ' ' << file << c.dim();
            if (!built) {
                std::cout << " (build failed)";
            } else if (code > 128) {
                std::cout << " (killed by signal " << code - 128 << ")";
            } else {
                std::cout << " (exit " << code << ")";
            }
            std::cout << c.reset() << "\n\n";
        }
        fs::remove(exe, ec);
    }
    fs::remove_all(work, ec);

    const auto total_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << c.bold() << "Test files  " << c.reset();
    if (!failed.empty()) {
        std::cout << c.red() << c.bold() << failed.size() << " failed" << c.reset() << ", ";
    }
    std::cout << c.green() << c.bold() << passed << " passed" << c.reset() << c.dim() << " (" << chosen.size()
              << ")" << c.reset() << '\n';
    std::cout << c.bold() << "Duration    " << c.reset() << total_ms << " ms\n";
    for (const std::string& f : failed) {
        std::cout << c.red() << "  \xe2\x9c\x97 " << c.reset() << f << '\n';
    }
    return failed.empty() ? 0 : 1;
}

}  // namespace xlang::cli
