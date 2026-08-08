#include "xlang/host/platform.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// Linux-specific hooks (baseline flags live in common.cpp).
namespace xlang::platform::linux_detail {

std::vector<std::string> extraSyslibFlags() {
    return {"-ldl", "-lm"};
}

std::filesystem::path defaultCacheDir() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        return std::filesystem::path(xdg) / "xlang";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".cache" / "xlang";
    }
    return {};
}

}  // namespace xlang::platform::linux_detail
