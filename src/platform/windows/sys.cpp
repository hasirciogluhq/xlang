#include "xlang/host/platform.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace xlang::platform::windows_detail {

std::vector<std::string> extraSyslibFlags() {
    return {"kernel32.lib", "advapi32.lib", "ws2_32.lib"};
}

std::filesystem::path defaultCacheDir() {
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(local) / "xlang" / "cache";
    }
    if (const char* profile = std::getenv("USERPROFILE")) {
        return std::filesystem::path(profile) / "AppData" / "Local" / "xlang" / "cache";
    }
    return {};
}

}  // namespace xlang::platform::windows_detail
