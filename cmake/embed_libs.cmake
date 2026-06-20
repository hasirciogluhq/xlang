if(NOT DEFINED LIBS_DIR OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "embed_libs.cmake: LIBS_DIR and OUTPUT_FILE required")
endif()

file(GLOB_RECURSE LIBS_FILES "${LIBS_DIR}/*.xlang")
list(SORT LIBS_FILES)

set(EMBED_BODY "")
set(ARRAY_INIT "")
foreach(LIBS_FILE ${LIBS_FILES})
    file(RELATIVE_PATH LIBS_REL "${LIBS_DIR}" "${LIBS_FILE}")
    string(REPLACE "." "_" LIBS_SYMBOL "${LIBS_REL}")
    string(REPLACE "/" "_" LIBS_SYMBOL "${LIBS_SYMBOL}")
    file(READ "${LIBS_FILE}" CONTENT)
    string(APPEND EMBED_BODY "constexpr const char kEmbeddedLibs_${LIBS_SYMBOL}[] = R\"XLANG_LIB(${CONTENT})XLANG_LIB\";\n\n")
    string(APPEND ARRAY_INIT "    {\"${LIBS_REL}\", kEmbeddedLibs_${LIBS_SYMBOL}},\n")
endforeach()

file(WRITE "${OUTPUT_FILE}"
"#include \"xlang/embedded_libs.h\"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace xlang {
namespace {

struct EmbeddedLibsFile {
    const char* name;
    const char* source;
};

${EMBED_BODY}

const EmbeddedLibsFile kEmbeddedLibsFiles[] = {
${ARRAY_INIT}
};

std::size_t embeddedLibsFileCount() {
    return sizeof(kEmbeddedLibsFiles) / sizeof(kEmbeddedLibsFiles[0]);
}

}  // namespace

std::filesystem::path materializeEmbeddedLibs(const std::filesystem::path& work_dir) {
    std::error_code ec;
    std::filesystem::create_directories(work_dir, ec);
    for (std::size_t i = 0; i < embeddedLibsFileCount(); ++i) {
        const EmbeddedLibsFile& file = kEmbeddedLibsFiles[i];
        const std::filesystem::path path = work_dir / file.name;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error(\"failed to write embedded libs file: \" + path.string());
        }
        out << file.source;
    }
    return work_dir;
}

}  // namespace xlang
")
