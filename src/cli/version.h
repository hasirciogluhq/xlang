#pragma once

// The release version, injected by xmake/compiler.lua from `set_version` in xmake.lua.
#ifndef XLANG_VERSION
#define XLANG_VERSION "0.0.0"
#endif

namespace xlang::cli {

inline constexpr const char* kVersion = XLANG_VERSION;

}  // namespace xlang::cli
