cmake_minimum_required(VERSION 3.16)
project(xlang VERSION 0.1.0 LANGUAGES C CXX)

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
file(GLOB_RECURSE RUNTIME_XLANG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.xlang")
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

set(EMBEDDED_LIBS_CPP "${CMAKE_CURRENT_BINARY_DIR}/embedded_libs.cpp")
file(GLOB_RECURSE LIBS_XLANG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/libs/*.xlang")
add_custom_command(
    OUTPUT "${EMBEDDED_LIBS_CPP}"
    COMMAND ${CMAKE_COMMAND}
        -DLIBS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/libs
        -DOUTPUT_FILE=${EMBEDDED_LIBS_CPP}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_libs.cmake
    DEPENDS
        ${LIBS_XLANG_FILES}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_libs.cmake
    VERBATIM
)
add_custom_target(embedded_libs DEPENDS "${EMBEDDED_LIBS_CPP}")

find_package(OpenSSL REQUIRED)

add_library(xlang_net_server STATIC src/bridge/net_server.c)
add_library(xlang_panic_bridge STATIC src/bridge/panic_bridge.c)
add_library(xlang_process_bridge STATIC src/bridge/process_bridge.c)
add_library(xlang_time_bridge STATIC src/bridge/time_bridge.c)
add_library(xlang_file_bridge STATIC src/bridge/file_bridge.cpp)

add_library(xlang_tls_bridge STATIC src/bridge/tls_bridge.c)
target_include_directories(xlang_tls_bridge PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(xlang_tls_bridge PUBLIC OpenSSL::SSL OpenSSL::Crypto)

add_executable(xlang
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
    src/test_runner.cpp
    "${EMBEDDED_RUNTIME_CPP}"
    "${EMBEDDED_LIBS_CPP}"
)

add_dependencies(xlang embedded_runtime embedded_libs)

target_include_directories(xlang PRIVATE include)
target_compile_definitions(xlang PRIVATE
    "XLANG_RUNTIME_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/runtime\""
    "XLANG_LIBS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/libs\""
    "XLANG_TLS_BRIDGE=\"$<TARGET_FILE:xlang_tls_bridge>\""
    "XLANG_OPENSSL_SSL=\"${OPENSSL_SSL_LIBRARY}\""
    "XLANG_OPENSSL_CRYPTO=\"${OPENSSL_CRYPTO_LIBRARY}\""
    "XLANG_NET_SERVER=\"$<TARGET_FILE:xlang_net_server>\""
    "XLANG_PANIC_BRIDGE=\"$<TARGET_FILE:xlang_panic_bridge>\""
    "XLANG_PROCESS_BRIDGE=\"$<TARGET_FILE:xlang_process_bridge>\""
    "XLANG_TIME_BRIDGE=\"$<TARGET_FILE:xlang_time_bridge>\""
    "XLANG_FILE_BRIDGE=\"$<TARGET_FILE:xlang_file_bridge>\""
)
target_link_libraries(xlang PRIVATE CLI11::CLI11)
add_dependencies(xlang xlang_tls_bridge xlang_net_server xlang_panic_bridge xlang_process_bridge xlang_time_bridge xlang_file_bridge)

install(TARGETS xlang RUNTIME DESTINATION bin)
