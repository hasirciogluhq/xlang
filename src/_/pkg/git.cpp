#include "xlang/pkg/git.h"

#include "cli/process.h"
#include "xlang/pkg/error.h"
#include "xlang/util/program.h"

#include <cstdlib>
#include <fstream>

namespace xlang::pkg {

ProcessResult runProcess(const std::vector<std::string>& argv, const std::filesystem::path& cwd) {
    // Two separate pipes, `GIT_TERMINAL_PROMPT=0` for the child, the exit code: `cli/process.h`
    // holds the one implementation (`fork`/`poll` on POSIX, `CreateProcessW`/`PeekNamedPipe` on
    // Windows), so the platform branch is not repeated here. A child that cannot start comes back
    // with `RunResult::status` still at -1, which `gitFailed` reports as "git is not installed".
    const host::RunResult r = host::run(
        argv,
        {.stdio = host::Stdio::Captured, .cwd = cwd.empty() ? std::string{} : cwd.string()});
    return {.status = r.status, .out = r.out, .err = r.err};
}

namespace {

[[nodiscard]] bool isLocalSource(const std::string& s) {
    if (s.starts_with("/") || s.starts_with("./") || s.starts_with("../") ||
        s.starts_with("file://")) {
        return true;
    }
    // A Windows drive path (`C:\repo`) is absolute but starts with no `/`: `std::filesystem` knows
    // both spellings on every OS.
    if (std::filesystem::path{s}.is_absolute()) {
        return true;
    }
    std::error_code ec;
    return s.find("://") == std::string::npos && s.find('@') == std::string::npos &&
           std::filesystem::is_directory(s, ec);
}

[[nodiscard]] std::string firstLine(const std::string& s) {
    const std::size_t end = s.find('\n');
    return end == std::string::npos ? s : s.substr(0, end);
}

[[nodiscard]] std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

[[noreturn]] void
gitFailed(const std::string& what, const std::string& url, const ProcessResult& r) {
    std::string detail =
        r.status == -1 ? "could not run `git` (is it installed and on PATH?)" : trimmed(r.err);
    throw PackageError("E0744",
                       what + " `" + url + "` failed: " + firstLine(detail),
                       "check the repository path and your network access; private repositories "
                       "need git credentials "
                       "that work without a prompt (SSH key or credential helper)");
}

} // namespace

std::string gitUrl(const std::string& import_path, const std::string& source) {
    if (source.empty()) {
        return "https://" + import_path + ".git";
    }
    if (source.starts_with("file://")) {
        return source;
    }
    if (isLocalSource(source)) {
        std::error_code ec;
        return "file://" + std::filesystem::weakly_canonical(source, ec).generic_string();
    }
    return source;
}

std::vector<TagRef> listTags(const std::string& url) {
    const ProcessResult r = runProcess({"git", "ls-remote", "--tags", url});
    if (r.status != 0) {
        gitFailed("listing the tags of", url, r);
    }
    std::vector<TagRef> tags;
    std::size_t start = 0;
    while (start < r.out.size()) {
        std::size_t end = r.out.find('\n', start);
        if (end == std::string::npos) {
            end = r.out.size();
        }
        const std::string line = r.out.substr(start, end - start);
        start = end + 1;
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || !line.substr(tab + 1).starts_with("refs/tags/")) {
            continue;
        }
        std::string name = line.substr(tab + 1 + 10);
        const std::string commit = line.substr(0, tab);
        const bool peeled = name.ends_with("^{}");
        if (peeled) {
            name.resize(name.size() - 3);
        }
        // An annotated tag is listed twice; the peeled line carries the commit.
        bool found = false;
        for (TagRef& t : tags) {
            if (t.name == name) {
                if (peeled) {
                    t.commit = commit;
                }
                found = true;
            }
        }
        if (!found) {
            tags.push_back({name, commit});
        }
    }
    return tags;
}

std::string
fetchTag(const std::string& url, const std::string& tag, const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    const std::filesystem::path tmp =
        dest.parent_path() / (dest.filename().string() + ".tmp-" + std::to_string(processId()));
    std::filesystem::remove_all(tmp, ec);
    const ProcessResult clone = runProcess({"git",
                                            "-c",
                                            "advice.detachedHead=false",
                                            "clone",
                                            "--quiet",
                                            "--depth",
                                            "1",
                                            "--no-tags",
                                            "--branch",
                                            tag,
                                            url,
                                            tmp.string()});
    if (clone.status != 0) {
        std::filesystem::remove_all(tmp, ec);
        gitFailed("downloading tag `" + tag + "` of", url, clone);
    }
    const ProcessResult head = runProcess({"git", "rev-parse", "HEAD"}, tmp);
    if (head.status != 0) {
        std::filesystem::remove_all(tmp, ec);
        gitFailed("reading the commit of", url, head);
    }
    const std::string commit = trimmed(head.out);
    std::filesystem::remove_all(tmp / ".git", ec);
    {
        std::ofstream marker(tmp / kCommitMarker);
        marker << commit << "\n";
    }
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        std::filesystem::remove_all(tmp, ec);
        if (!std::filesystem::is_directory(dest)) { // another process won the race: that is fine
            throw PackageError("E0744",
                               "cannot move the download into `" + dest.generic_string() + "`");
        }
    }
    return commit;
}

} // namespace xlang::pkg
