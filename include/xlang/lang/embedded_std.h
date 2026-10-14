#pragma once

namespace xlang {

/// One file of the standard library compiled into the compiler (`std/**/*.xl`).
struct EmbeddedFile {
    const char* path; ///< relative to the stdlib root, e.g. `mem.xl`, `sys/linux.xl`
    const unsigned char* data;
    unsigned long size;
};

extern const EmbeddedFile kEmbeddedStd[];
extern const unsigned long kEmbeddedStdCount;

} // namespace xlang
