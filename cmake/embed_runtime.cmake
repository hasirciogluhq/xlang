if(NOT DEFINED RUNTIME_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "embed_runtime.cmake: RUNTIME_FILE and OUTPUT_FILE required")
endif()

file(READ "${RUNTIME_FILE}" CONTENT)

file(WRITE "${OUTPUT_FILE}"
"#include \"xlang/embedded_runtime.h\"

namespace xlang {

constexpr const char kEmbeddedRuntimeSource[] = R\"XLANG_RT(${CONTENT})XLANG_RT\";

}  // namespace xlang
")
