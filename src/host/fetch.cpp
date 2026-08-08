#include "xlang/host/fetch.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace xlang {

bool fetchGithubReleaseAsset(const std::string& owner, const std::string& repo,
                             const std::string& tag, const std::string& asset_name,
                             const std::filesystem::path& dest) {
    if (owner.empty() || repo.empty() || tag.empty() || asset_name.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);

    const std::string url = "https://github.com/" + owner + "/" + repo + "/releases/download/" +
                            tag + "/" + asset_name;

    // Prefer curl when available; fall back to false.
    std::ostringstream cmd;
    cmd << "curl -fsSL -o " << dest.string() << " " << url;
    const int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        std::error_code remove_ec;
        std::filesystem::remove(dest, remove_ec);
        return false;
    }
    return std::filesystem::exists(dest);
}

}  // namespace xlang
