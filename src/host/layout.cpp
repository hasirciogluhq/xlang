#include "xlang/host/layout.h"
#include "xlang/host/platform.h"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace xlang {
namespace {

void pushUnique(std::vector<std::filesystem::path>& out, const std::filesystem::path& path) {
    for (const auto& existing : out) {
        if (existing == path) {
            return;
        }
    }
    out.push_back(path);
}

void appendEnvPaths(std::vector<std::filesystem::path>& out, const char* env_name) {
    const char* value = std::getenv(env_name);
    if (value == nullptr || *value == '\0') {
        return;
    }
    const char sep = platform::pathListSeparator();
    std::string_view remaining(value);
    while (!remaining.empty()) {
        const auto pos = remaining.find(sep);
        const std::string_view part =
            pos == std::string_view::npos ? remaining : remaining.substr(0, pos);
        if (!part.empty()) {
            pushUnique(out, std::filesystem::path(part));
        }
        if (pos == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(pos + 1);
    }
}

std::filesystem::path projectRootGuess() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    if (std::filesystem::exists(cwd / "xmake.lua")) {
        return cwd;
    }
    if (std::filesystem::exists(cwd.parent_path() / "xmake.lua")) {
        return cwd.parent_path();
    }
    return cwd;
}

}  // namespace

std::vector<std::filesystem::path> defaultLibSearchPaths() {
    std::vector<std::filesystem::path> paths;
    appendEnvPaths(paths, "XLANG_LIB_PATH");

    const std::filesystem::path root = projectRootGuess();
    pushUnique(paths, root / "build");
    pushUnique(paths, root / "build" / "lib");

    const std::filesystem::path home = platform::homeDir();
    if (!home.empty()) {
        pushUnique(paths, home / ".xlang" / "lib");
        pushUnique(paths, home / ".local" / "lib" / "xlang");
    }

    return paths;
}

std::vector<std::filesystem::path> defaultModuleSearchPaths() {
    std::vector<std::filesystem::path> paths;
    appendEnvPaths(paths, "XLANG_MODULE_PATH");

    const std::filesystem::path root = projectRootGuess();
    pushUnique(paths, root / "src");
    pushUnique(paths, std::filesystem::current_path());

    const std::filesystem::path home = platform::homeDir();
    if (!home.empty()) {
        pushUnique(paths, home / ".xlang" / "modules");
    }
    return paths;
}

}  // namespace xlang
