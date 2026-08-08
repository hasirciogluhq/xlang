#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xlang {

struct EmbeddedBlob {
    const unsigned char* data{nullptr};
    unsigned long size{0};
    const char* name{nullptr};
};

/// Embedded default runtime archive/object (may be empty in stage-1 / dev builds).
[[nodiscard]] EmbeddedBlob embeddedRuntime();
/// Embedded default bridges archive (may be empty in stage-1 / dev builds).
[[nodiscard]] EmbeddedBlob embeddedBridges();

/// Write blob to dest; returns false if blob empty.
[[nodiscard]] bool extractEmbeddedBlob(const EmbeddedBlob& blob, const std::filesystem::path& dest);

/// Resolve runtime material: override → extract embed → in-tree/dev findLibrary("runtime").
[[nodiscard]] std::optional<std::filesystem::path>
resolveRuntimeArtifact(const std::optional<std::filesystem::path>& override_path,
                       bool skip_runtime, const std::filesystem::path& extract_dir);

/// Resolve bridge material: override dir/archive → extract embed → in-tree libs.
[[nodiscard]] std::vector<std::filesystem::path>
resolveBridgeArtifacts(const std::optional<std::filesystem::path>& override_path,
                       bool skip_bridge, const std::filesystem::path& extract_dir);

}  // namespace xlang
