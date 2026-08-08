#include "xlang/host/platform.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace xlang::platform::macosx_detail {

std::vector<std::string> extraSyslibFlags() {
    return {};
}

std::filesystem::path defaultCacheDir() {
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Caches" / "xlang";
    }
    return {};
}

}  // namespace xlang::platform::macosx_detail
