---
title: xlang documentation
---

xlang is a low-level systems language. Its compiler is written in C++ and uses LLVM for code generation. Its standard library is written in xlang and lives in `std/`. There is no C runtime: the compiler knows only the language, the CPU primitives and a closed list of builtins, and everything that happens at run time is ordinary xlang code.

Source files use the `.xl` extension. This site is the only documentation source for the project; people and AI tools read the same pages.

## Sections

- **Language**: the language reference (types, statements, control flow, generics, tuple values, attributes, modules, conditional compilation, builtins), plus the smaller pages on classes, `defer`, the entry point and variadic functions.
- **Compiler**: installing the compiler, linking, packages, the runtime (panic, allocator and unwinding roles), testing, and known limitations.
- **Standard library**: one page per area (strings, collections, cryptography, mathematics, encodings, compression, HTTP, cloud storage, and so on) and the system call number tables.

## Where things live

- Language reference: [Language](language/)
- Build and install: [Install](compiler/install.html)
- Running the test suites: [Testing](compiler/testing.html)
- Examples: [Examples](examples.html)
- Standard library overview: [Standard library](std/)
- Open decisions and deferred work: [Known limitations](known-limitations.html)

## Source

The repository is at [github.com/hasirciogluhq/xlang](https://github.com/hasirciogluhq/xlang).
