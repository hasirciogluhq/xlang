#pragma once

namespace xlang {

// Output / build types (PACKAGE_MANAGER / LINKING).
// Shared is experimental / limited support.
enum class BuildKind {
    Executable,  // linked program (default)
    Static,      // static library (.a / .lib)
    Shared,      // shared library (.so / .dylib / .dll) — experimental
    Object,      // single object file (.o / .obj)
};

// Legacy aliases used during migration.
inline constexpr BuildKind BuildKind_Exe = BuildKind::Executable;
inline constexpr BuildKind BuildKind_Lib = BuildKind::Object;

[[nodiscard]] inline bool isLibraryKind(BuildKind kind) {
    return kind == BuildKind::Static || kind == BuildKind::Shared || kind == BuildKind::Object;
}

[[nodiscard]] inline bool linksFinalImage(BuildKind kind) {
    return kind == BuildKind::Executable || kind == BuildKind::Shared;
}

}  // namespace xlang
