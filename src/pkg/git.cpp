#include "xlang/pkg/git.h"

#include "xlang/pkg/error.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>

extern char** environ;

namespace xlang::pkg {

ProcessResult runProcess(const std::vector<std::string>& argv, const std::filesystem::path& cwd) {
    ProcessResult r;
    int out_pipe[2];
    int err_pipe[2];
    if (::pipe(out_pipe) != 0) {
        return r;
    }
    if (::pipe(err_pipe) != 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return r;
    }
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& a : argv) {
        args.push_back(const_cast<char*>(a.c_str()));
    }
    args.push_back(nullptr);
    const std::string cwd_text = cwd.string();

    const pid_t pid = ::fork();
    if (pid < 0) {
        for (int fd : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) {
            ::close(fd);
        }
        return r;
    }
    if (pid == 0) {
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        for (int fd : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) {
            ::close(fd);
        }
        const int null = ::open("/dev/null", O_RDONLY);
        if (null >= 0) {
            ::dup2(null, STDIN_FILENO);
        }
        ::setenv("GIT_TERMINAL_PROMPT", "0", 1);
        if (!cwd_text.empty() && ::chdir(cwd_text.c_str()) != 0) {
            ::_exit(127);
        }
        ::execvp(args[0], args.data());
        ::_exit(127);
    }
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
    int wstatus = 0;
    while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {
    }
    r.status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
    return r;
}

namespace {

[[nodiscard]] bool isLocalSource(const std::string& s) {
    if (s.starts_with("/") || s.starts_with("./") || s.starts_with("../") || s.starts_with("file://")) {
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

[[noreturn]] void gitFailed(const std::string& what, const std::string& url, const ProcessResult& r) {
    std::string detail = r.status == -1 ? "could not run `git` (is it installed and on PATH?)" : trimmed(r.err);
    throw PackageError("E0744", what + " `" + url + "` failed: " + firstLine(detail),
                       "check the repository path and your network access; private repositories need git credentials "
                       "that work without a prompt (SSH key or credential helper)");
}

}  // namespace

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

std::string fetchTag(const std::string& url, const std::string& tag, const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    const std::filesystem::path tmp = dest.parent_path() / (dest.filename().string() + ".tmp-" + std::to_string(::getpid()));
    std::filesystem::remove_all(tmp, ec);
    const ProcessResult clone = runProcess({"git", "-c", "advice.detachedHead=false", "clone", "--quiet", "--depth", "1",
                                            "--no-tags", "--branch", tag, url, tmp.string()});
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
        if (!std::filesystem::is_directory(dest)) {  // another process won the race: that is fine
            throw PackageError("E0744", "cannot move the download into `" + dest.generic_string() + "`");
        }
    }
    return commit;
}

}  // namespace xlang::pkg
