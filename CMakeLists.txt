cmake_minimum_required(VERSION 3.16)
project(xlank VERSION 0.1.0 LANGUAGES CXX)

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
add_custom_command(
    OUTPUT "${EMBEDDED_RUNTIME_CPP}"
    COMMAND ${CMAKE_COMMAND}
        -DRUNTIME_FILE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/runtime.xlang
        -DOUTPUT_FILE=${EMBEDDED_RUNTIME_CPP}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_runtime.cmake
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/runtime.xlang
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_runtime.cmake
    VERBATIM
)
add_custom_target(embedded_runtime DEPENDS "${EMBEDDED_RUNTIME_CPP}")

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
    "${EMBEDDED_RUNTIME_CPP}"
)

add_dependencies(xlank embedded_runtime)

target_include_directories(xlank PRIVATE include)
target_link_libraries(xlank PRIVATE CLI11::CLI11)

install(TARGETS xlank RUNTIME DESTINATION bin)
