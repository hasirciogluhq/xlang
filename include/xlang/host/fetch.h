#pragma once

#include <filesystem>
#include <string>

namespace xlang {

// Download a GitHub release asset into dest. Returns false on failure.
[[nodiscard]] bool fetchGithubReleaseAsset(const std::string& owner, const std::string& repo,
                                           const std::string& tag, const std::string& asset_name,
                                           const std::filesystem::path& dest);

}  // namespace xlang
