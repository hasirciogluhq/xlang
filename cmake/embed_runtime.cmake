if(NOT DEFINED RUNTIME_DIR OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "embed_runtime.cmake: RUNTIME_DIR and OUTPUT_FILE required")
endif()

file(GLOB RUNTIME_FILES "${RUNTIME_DIR}/*.xlang")
list(SORT RUNTIME_FILES)

set(EMBED_BODY "")
set(ARRAY_INIT "")
foreach(RUNTIME_FILE ${RUNTIME_FILES})
    get_filename_component(RUNTIME_NAME "${RUNTIME_FILE}" NAME)
    string(REPLACE "." "_" RUNTIME_SYMBOL "${RUNTIME_NAME}")
    file(READ "${RUNTIME_FILE}" CONTENT)
    string(APPEND EMBED_BODY "constexpr const char kEmbeddedRuntime_${RUNTIME_SYMBOL}[] = R\"XLANG_RT(${CONTENT})XLANG_RT\";\n\n")
    string(APPEND ARRAY_INIT "    {\"${RUNTIME_NAME}\", kEmbeddedRuntime_${RUNTIME_SYMBOL}},\n")
endforeach()

file(READ "${RUNTIME_DIR}/runtime.xlang" MAIN_CONTENT)

file(WRITE "${OUTPUT_FILE}"
"#include \"xlang/embedded_runtime.h\"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace xlang {
namespace {

struct EmbeddedRuntimeFile {
    const char* name;
    const char* source;
};

${EMBED_BODY}

const EmbeddedRuntimeFile kEmbeddedRuntimeFiles[] = {
${ARRAY_INIT}
};

std::size_t embeddedRuntimeFileCount() {
    return sizeof(kEmbeddedRuntimeFiles) / sizeof(kEmbeddedRuntimeFiles[0]);
}

}  // namespace

const char kEmbeddedRuntimeSource[] = R\"XLANG_RT(${MAIN_CONTENT})XLANG_RT\";

std::filesystem::path materializeEmbeddedRuntime(const std::filesystem::path& work_dir) {
    std::error_code ec;
    std::filesystem::create_directories(work_dir, ec);
    for (std::size_t i = 0; i < embeddedRuntimeFileCount(); ++i) {
        const EmbeddedRuntimeFile& file = kEmbeddedRuntimeFiles[i];
        const std::filesystem::path path = work_dir / file.name;
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error(\"failed to write embedded runtime file: \" + path.string());
        }
        out << file.source;
    }
    return work_dir / \"runtime.xlang\";
}

}  // namespace xlang
")
