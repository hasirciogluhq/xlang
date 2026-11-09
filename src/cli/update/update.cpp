#include "cli/update/update.h"

#include "cli/app/style.h"
#include "cli/app/version.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace xlang::cli {

namespace {

constexpr const char* kRepo = "hasirciogluhq/xlang";

/// Single-quotes `s` for `sh -c`.
std::string quote(const std::string& s) {
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    return out + "'";
}

struct Captured {
    int code{-1};
    std::string out;
};

/// Runs `command` through `sh`, collecting stdout (stderr is dropped).
Captured capture(const std::string& command) {
    Captured result;
    std::fflush(stdout);
    std::fflush(stderr);
    FILE* pipe = ::popen((command + " 2>/dev/null").c_str(), "r");
    if (pipe == nullptr) {
        return result;
    }
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0) {
        result.out.append(buf, n);
    }
    const int status = ::pclose(pipe);
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

/// Runs `command` through `sh` with stdout/stderr inherited (curl's progress bar shows).
int run(const std::string& command) {
    std::fflush(stdout);
    std::fflush(stderr);
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool hasTool(const std::string& name) {
    return run("command -v " + quote(name) + " >/dev/null 2>&1") == 0;
}

std::optional<std::string> hostOs() {
#if defined(__APPLE__)
    return "darwin";
#elif defined(__linux__)
    return "linux";
#else
    return std::nullopt;
#endif
}

std::optional<std::string> hostArch() {
#if defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(__x86_64__)
    return "amd64";
#else
    return std::nullopt;
#endif
}

/// "v1.2.3-rc1" -> {1, 2, 3}; the pre-release part is ignored (the latest-release endpoint never
/// returns pre-releases).
std::vector<long> parseVersion(std::string text) {
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) {
        text.erase(0, 1);
    }
    text = text.substr(0, text.find_first_of("-+"));
    std::vector<long> parts;
    std::stringstream stream(text);
    std::string piece;
    while (std::getline(stream, piece, '.')) {
        parts.push_back(piece.empty() ? 0 : std::strtol(piece.c_str(), nullptr, 10));
    }
    return parts;
}

/// <0, 0, >0 as `a` is older than, equal to, or newer than `b`.
int compareVersions(const std::vector<long>& a, const std::vector<long>& b) {
    const std::size_t n = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const long x = i < a.size() ? a[i] : 0;
        const long y = i < b.size() ? b[i] : 0;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

/// The string value of `"key": "..."` in a flat JSON object; empty when absent.
std::string jsonString(const std::string& json, const std::string& key) {
    const std::size_t at = json.find("\"" + key + "\"");
    if (at == std::string::npos) {
        return {};
    }
    std::size_t i = json.find(':', at);
    if (i == std::string::npos) {
        return {};
    }
    i = json.find('"', i);
    if (i == std::string::npos) {
        return {};
    }
    std::string value;
    for (++i; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            ++i;
        }
        value += json[i];
    }
    return value;
}

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

/// The hash recorded for `name` in a `checksums.txt` ("<hash>  <name>" per line).
std::string expectedHash(const std::string& checksums, const std::string& name) {
    std::stringstream stream(checksums);
    std::string line;
    while (std::getline(stream, line)) {
        std::stringstream fields(line);
        std::string hash;
        std::string file;
        if ((fields >> hash >> file) && file == name) {
            return lower(hash);
        }
    }
    return {};
}

/// Removes a directory tree on scope exit.
struct TempDir {
    fs::path path;
    ~TempDir() {
        std::error_code ec;
        if (!path.empty()) {
            fs::remove_all(path, ec);
        }
    }
};

} // namespace

int selfUpdate(const SelfUpdateOptions& options, const std::string& self) {
    const Palette c{options.color};
    const auto step = [&](const std::string& text) {
        std::cout << c.boldCyan() << "==> " << c.reset() << c.bold() << text << c.reset() << '\n';
    };
    const auto note = [&](const std::string& text) {
        std::cout << "    " << c.dim() << text << c.reset() << '\n';
    };
    const auto fail = [&](const std::string& text) {
        std::cerr << c.boldRed() << "error: " << c.reset() << text << '\n';
        return 1;
    };

    const std::string current = kVersion;
    const auto os = hostOs();
    const auto arch = hostArch();
    if (!os || !arch) {
        return fail("there are no prebuilt xlang releases for this operating system or CPU");
    }
    if (self.find("/Cellar/") != std::string::npos) {
        return fail("this xlang was installed with Homebrew; update it with `brew upgrade xlang`");
    }
    if (!hasTool("curl")) {
        return fail("`curl` is required for self-update but was not found on PATH");
    }

    step("Checking for updates (installed " + current + ", " + *os + "/" + *arch + ")");
    const Captured latest_json =
        capture("curl -fsSL --max-time 20 -H 'Accept: application/vnd.github+json' "
                "-H 'User-Agent: xlang-self-update' " +
                quote(std::string("https://api.github.com/repos/") + kRepo + "/releases/latest"));
    const std::string tag = latest_json.code == 0 ? jsonString(latest_json.out, "tag_name") : "";
    if (tag.empty()) {
        return fail("could not read the latest release: none is published yet, or the network is "
                    "unreachable");
    }
    std::string latest = tag;
    if (!latest.empty() && latest[0] == 'v') {
        latest.erase(0, 1);
    }

    const bool newer = compareVersions(parseVersion(latest), parseVersion(current)) > 0;
    if (!newer && !options.force) {
        std::cout << c.boldGreen() << "xlang " << current << " is up to date" << c.reset() << '\n';
        return 0;
    }
    if (options.check_only) {
        std::cout << c.boldYellow() << "update available: " << c.reset() << current << " -> "
                  << c.bold() << latest << c.reset() << '\n';
        note("run `xlang self-update` to install it");
        return 0;
    }

    // Everything is staged in a private temporary directory; `self` is touched only at the end.
    TempDir temp;
    {
        std::error_code ec;
        std::string pattern = (fs::temp_directory_path(ec) / "xlang-update-XXXXXX").string();
        if (ec || ::mkdtemp(pattern.data()) == nullptr) {
            return fail("could not create a temporary directory");
        }
        temp.path = pattern;
    }
    const std::string asset = "xlang_" + latest + "_" + *os + "_" + *arch + ".tar.gz";
    const std::string base =
        std::string("https://github.com/") + kRepo + "/releases/download/" + tag + "/";
    const fs::path archive = temp.path / asset;
    const fs::path sums = temp.path / "checksums.txt";

    step("Downloading " + asset);
    if (run("curl -fSL --progress-bar -o " + quote(archive.string()) + " " + quote(base + asset)) !=
        0) {
        return fail("download failed: " + base + asset);
    }
    if (run("curl -fsSL -o " + quote(sums.string()) + " " + quote(base + "checksums.txt")) != 0) {
        return fail("download failed: " + base + "checksums.txt");
    }

    step("Verifying checksum");
    const std::string expected = [&] {
        const Captured text = capture("cat " + quote(sums.string()));
        return expectedHash(text.out, asset);
    }();
    if (expected.empty()) {
        return fail("checksums.txt has no entry for " + asset +
                    "; refusing to install an unverified binary");
    }
    const std::string sha_command =
        hasTool("sha256sum") ? "sha256sum " : (hasTool("shasum") ? "shasum -a 256 " : "");
    if (sha_command.empty()) {
        return fail("`sha256sum` or `shasum` is required to verify the download");
    }
    const Captured actual = capture(sha_command + quote(archive.string()));
    std::stringstream first(actual.out);
    std::string actual_hash;
    first >> actual_hash;
    if (lower(actual_hash) != expected) {
        return fail("checksum mismatch for " + asset + " (expected " + expected + ", got " +
                    actual_hash + ")");
    }
    note("sha256 " + expected);

    step("Installing");
    if (run("tar -xzf " + quote(archive.string()) + " -C " + quote(temp.path.string())) != 0) {
        return fail("could not extract " + asset);
    }
    const fs::path fresh = temp.path / "xlang";
    if (!fs::exists(fresh)) {
        return fail("the archive does not contain an `xlang` binary");
    }
    ::chmod(fresh.c_str(), 0755);
    const Captured smoke = capture(quote(fresh.string()) + " --version");
    if (smoke.code != 0) {
        return fail("the downloaded binary does not start; keeping the installed one");
    }

    // Stage next to `self` so the final rename stays on one file system and is atomic.
    const fs::path target(self);
    const fs::path staged = target.parent_path() / (".xlang-update-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::copy_file(fresh, staged, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
        ::chmod(staged.c_str(), 0755);
        fs::rename(staged, target, ec);
    }
    if (ec) {
        std::error_code ignore;
        fs::remove(staged, ignore);
        return fail("cannot write " + target.parent_path().string() + " (" + ec.message() +
                    "); run with sufficient permissions or reinstall with the install script");
    }

    // The new binary knows the new commands: regenerate completion scripts that are installed.
    // Best effort; a failure here never fails the update.
    run(quote(target.string()) + " completions install --refresh >/dev/null 2>&1");

    std::cout << c.boldGreen() << "updated xlang " << current << " -> " << latest << c.reset()
              << '\n';
    return 0;
}

} // namespace xlang::cli
