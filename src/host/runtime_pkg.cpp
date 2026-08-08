#include "xlang/host/runtime_pkg.h"
#include "xlang/host/platform.h"

#include <fstream>
#include <sstream>

namespace xlang {
namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

}  // namespace

std::optional<RuntimeManifest> readRuntimeManifest(const std::filesystem::path& root) {
    RuntimeManifest manifest;
    const auto version_path = root / "RUNTIME_VERSION";
    const auto supported_path = root / "SUPPORTED_COMPILERS";

    if (!std::filesystem::exists(version_path)) {
        return std::nullopt;
    }

    {
        std::ifstream in(version_path);
        std::getline(in, manifest.runtime_version);
        manifest.runtime_version = trim(manifest.runtime_version);
        if (manifest.runtime_version.empty()) {
            return std::nullopt;
        }
    }

    if (std::filesystem::exists(supported_path)) {
        std::ifstream in(supported_path);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty() && line[0] != '#') {
                manifest.supported_compilers.push_back(line);
            }
        }
    }
    return manifest;
}

bool compilerSupportedByRuntime(const RuntimeManifest& manifest,
                                std::string_view compiler_version) {
    if (manifest.supported_compilers.empty()) {
        return true;
    }
    for (const auto& entry : manifest.supported_compilers) {
        if (entry == "*" || entry == compiler_version) {
            return true;
        }
        // Simple prefix match: "0.1" matches "0.1.2"
        if (compiler_version.rfind(entry, 0) == 0) {
            return true;
        }
    }
    return false;
}

std::filesystem::path defaultRuntimeInstallDir() {
    const auto home = platform::homeDir();
    if (home.empty()) {
        return std::filesystem::temp_directory_path() / "xlang" / "runtime";
    }
    return home / ".xlang" / "runtime";
}

}  // namespace xlang
