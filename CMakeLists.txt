cmake_minimum_required(VERSION 3.16)
project(xlank VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(CLI11)

set(EMBEDDED_RUNTIME_CPP "${CMAKE_CURRENT_BINARY_DIR}/embedded_runtime.cpp")
file(GLOB RUNTIME_XLANG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.xlang")
add_custom_command(
    OUTPUT "${EMBEDDED_RUNTIME_CPP}"
    COMMAND ${CMAKE_COMMAND}
        -DRUNTIME_DIR=${CMAKE_CURRENT_SOURCE_DIR}/runtime
        -DOUTPUT_FILE=${EMBEDDED_RUNTIME_CPP}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_runtime.cmake
    DEPENDS
        ${RUNTIME_XLANG_FILES}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_runtime.cmake
    VERBATIM
)
add_custom_target(embedded_runtime DEPENDS "${EMBEDDED_RUNTIME_CPP}")

find_package(OpenSSL REQUIRED)

add_library(xlang_tls_bridge STATIC src/tls_bridge.c)
target_include_directories(xlang_tls_bridge PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(xlang_tls_bridge PUBLIC OpenSSL::SSL OpenSSL::Crypto)

add_executable(xlank
    src/main.cpp
    src/ast.cpp
    src/lexer.cpp
    src/parser.cpp
    src/codegen.cpp
    src/compiler.cpp
    src/module.cpp
    src/runtime.cpp
    src/syscalls.cpp
    src/input.cpp
    src/types.cpp
    "${EMBEDDED_RUNTIME_CPP}"
)

add_dependencies(xlank embedded_runtime)

target_include_directories(xlank PRIVATE include)
target_compile_definitions(xlank PRIVATE
    "XLANG_RUNTIME_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/runtime\""
    "XLANG_TLS_BRIDGE=\"$<TARGET_FILE:xlang_tls_bridge>\""
    "XLANG_OPENSSL_SSL=\"${OPENSSL_SSL_LIBRARY}\""
    "XLANG_OPENSSL_CRYPTO=\"${OPENSSL_CRYPTO_LIBRARY}\""
)
target_link_libraries(xlank PRIVATE CLI11::CLI11)
add_dependencies(xlank xlang_tls_bridge)

install(TARGETS xlank RUNTIME DESTINATION bin)
