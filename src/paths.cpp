#include "xlang/paths.h"

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>

namespace xlang {
namespace {

void pushUnique(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        return;
    }
    for (const std::filesystem::path& existing : paths) {
        if (existing == absolute) {
            return;
        }
    }
    paths.push_back(absolute);
}

std::filesystem::path homeDir() {
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
    if (const char* profile = std::getenv("USERPROFILE")) {
        return profile;
    }
    return {};
}

void appendEnvPathList(std::vector<std::filesystem::path>& paths, const char* env_name) {
    const char* value = std::getenv(env_name);
    if (!value || value[0] == '\0') {
        return;
    }
    std::string raw(value);
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t end = raw.find(':', start);
        const std::string part =
            raw.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            pushUnique(paths, part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

}  // namespace

std::vector<std::filesystem::path> defaultLibSearchPaths() {
    std::vector<std::filesystem::path> paths;

    appendEnvPathList(paths, "XLANG_LIB");
    appendEnvPathList(paths, "XLANG_PATH");

    if (const char* home_env = std::getenv("XLANG_HOME")) {
        pushUnique(paths, std::filesystem::path(home_env) / "lib");
    }

    const std::filesystem::path home = homeDir();
    if (!home.empty()) {
        pushUnique(paths, home / ".xlang" / "lib");
    }

    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        pushUnique(paths, cwd / "lib");
        pushUnique(paths, cwd / "build");
        pushUnique(paths, cwd / "build" / "lib");
    }

    return paths;
}

std::vector<std::filesystem::path> defaultModuleSearchPaths(bool include_runtime) {
    std::vector<std::filesystem::path> paths = defaultLibSearchPaths();
    (void)include_runtime;

    auto pushFrontendTree = [&](const std::filesystem::path& frontend) {
        std::error_code ec;
        if (!std::filesystem::is_directory(frontend, ec)) {
            return;
        }
        pushUnique(paths, frontend);
        // Domain folders (net/, filesystem/, …) are also roots so nested
        // packages like net/http resolve as `http` globally.
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(frontend, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_directory()) {
                pushUnique(paths, entry.path());
            }
        }
    };

    auto pushFrontend = [&](const std::filesystem::path& root) {
        if (root.empty()) {
            return;
        }
        pushFrontendTree(root / "src" / "runtime" / "frontend");
        pushFrontendTree(root / "runtime" / "frontend");
        pushFrontendTree(root / "frontend");
    };

    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        pushFrontend(cwd);
    }

    if (const char* home_env = std::getenv("XLANG_HOME")) {
        pushFrontend(home_env);
    }

    const std::filesystem::path home = homeDir();
    if (!home.empty()) {
        pushFrontend(home / ".xlang");
    }

    return paths;
}

std::optional<std::filesystem::path> findLibrary(std::string_view name) {
    const std::string name_str(name);
    const std::array candidates{
        std::string("lib") + name_str + ".a",
        name_str + ".a",
        std::string("lib") + name_str + ".lib",
        name_str + ".lib",
        name_str + ".o",
        std::string("lib") + name_str + ".o",
    };

    for (const std::filesystem::path& dir : defaultLibSearchPaths()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        for (const std::string& file : candidates) {
            const std::filesystem::path path = dir / file;
            if (std::filesystem::is_regular_file(path, ec)) {
                return path;
            }
        }
    }
    return std::nullopt;
}

}  // namespace xlang
