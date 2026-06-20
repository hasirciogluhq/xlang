cmake_minimum_required(VERSION 3.16)
project(xlank VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
)

target_include_directories(xlank PRIVATE include)

install(TARGETS xlank RUNTIME DESTINATION bin)
