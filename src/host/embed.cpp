#include "xlang/host/embed.h"
#include "xlang/host/layout.h"

#include <filesystem>
#include <fstream>

namespace xlang {
namespace {

// Weak placeholders — release stage-2 links real xxd/objcopy payloads via
// -DXLANG_HAS_EMBEDDED_RUNTIME / XLANG_HAS_EMBEDDED_BRIDGES and generated .c files.
#if defined(XLANG_HAS_EMBEDDED_RUNTIME)
extern "C" const unsigned char xlang_embedded_runtime[];
extern "C" const unsigned long xlang_embedded_runtime_size;
#endif

#if defined(XLANG_HAS_EMBEDDED_BRIDGES)
extern "C" const unsigned char xlang_embedded_bridges[];
extern "C" const unsigned long xlang_embedded_bridges_size;
#endif

}  // namespace

EmbeddedBlob embeddedRuntime() {
#if defined(XLANG_HAS_EMBEDDED_RUNTIME)
    return {xlang_embedded_runtime, xlang_embedded_runtime_size, "runtime"};
#else
    return {nullptr, 0, "runtime"};
#endif
}

EmbeddedBlob embeddedBridges() {
#if defined(XLANG_HAS_EMBEDDED_BRIDGES)
    return {xlang_embedded_bridges, xlang_embedded_bridges_size, "bridges"};
#else
    return {nullptr, 0, "bridges"};
#endif
}

bool extractEmbeddedBlob(const EmbeddedBlob& blob, const std::filesystem::path& dest) {
    if (blob.data == nullptr || blob.size == 0) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(blob.data),
              static_cast<std::streamsize>(blob.size));
    return static_cast<bool>(out);
}

std::optional<std::filesystem::path>
resolveRuntimeArtifact(const std::optional<std::filesystem::path>& override_path,
                       bool skip_runtime, const std::filesystem::path& extract_dir) {
    if (skip_runtime) {
        return std::nullopt;
    }
    if (override_path && !override_path->empty()) {
        // Override may be a .xlang entry (compiled elsewhere) or a prebuilt .o/.a.
        const auto ext = override_path->extension().string();
        if (ext == ".o" || ext == ".a" || ext == ".obj" || ext == ".lib") {
            return *override_path;
        }
        // Non-object override is handled by runtime loader; no artifact here.
        return std::nullopt;
    }

    const auto embed_dest = extract_dir / "embedded_runtime.a";
    if (extractEmbeddedBlob(embeddedRuntime(), embed_dest)) {
        return embed_dest;
    }

    if (const auto prebuilt = findLibrary("runtime")) {
        return *prebuilt;
    }
    return std::nullopt;
}

std::vector<std::filesystem::path>
resolveBridgeArtifacts(const std::optional<std::filesystem::path>& override_path,
                       bool skip_bridge, const std::filesystem::path& extract_dir) {
    if (skip_bridge) {
        return {};
    }

    if (override_path && !override_path->empty()) {
        std::vector<std::filesystem::path> out;
        if (std::filesystem::is_directory(*override_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(*override_path)) {
                const auto ext = entry.path().extension().string();
                if (ext == ".a" || ext == ".o" || ext == ".lib" || ext == ".obj") {
                    out.push_back(entry.path());
                }
            }
            return out;
        }
        return {*override_path};
    }

    const auto embed_dest = extract_dir / "embedded_bridges.a";
    if (extractEmbeddedBlob(embeddedBridges(), embed_dest)) {
        return {embed_dest};
    }

    // Dev/in-tree: link individual bridge static libs when present.
    static constexpr const char* kBridges[] = {
        "filesystem", "net", "tls", "process", "time", "panic", "thread", "sync",
    };
    std::vector<std::filesystem::path> out;
    for (const char* name : kBridges) {
        if (const auto path = findLibrary(name)) {
            out.push_back(*path);
        }
    }
    return out;
}

}  // namespace xlang
