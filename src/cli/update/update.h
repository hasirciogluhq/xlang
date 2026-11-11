#pragma once

#include <string>

namespace xlang::cli {

struct SelfUpdateOptions {
    bool check_only{false};  ///< report whether a newer release exists; install nothing
    bool force{false};       ///< reinstall the latest release even when this one is current
    bool color{false};
};

/// `xl self-update`: asks GitHub for the latest release, and when it is newer than this binary
/// downloads the archive for the host OS and architecture, verifies it against the release's
/// `checksums.txt`, smoke-tests the new binary and replaces `self` in place. Prints "up to date"
/// when nothing newer exists. Returns the process exit code.
int selfUpdate(const SelfUpdateOptions& options, const std::string& self);

}  // namespace xlang::cli
