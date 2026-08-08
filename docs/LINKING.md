# xlang Linking Reference

This document is the source of truth for how the **linker** attaches objects, static libraries, and external symbols when producing an executable or library. Compile stages are owned by the compiler; dependency acquisition is owned by the package manager. CLI entry points are those in the [Package Manager](PACKAGE_MANAGER.md) reference (`build` / `compile`, `run`, and related flags).

---

## Table of Contents

- [Overview](#overview)
- [Pipeline](#pipeline)
- [What gets linked](#what-gets-linked)
- [External symbols](#external-symbols)
- [Static libraries](#static-libraries)
- [Search paths](#search-paths)
- [Source vs prebuilt](#source-vs-prebuilt)
- [Examples](#examples)
- [Related documents](#related-documents)

---

# Overview

Linking runs whenever `xlang build` / `xlang compile` produces an executable (or static archive that needs members resolved), and for `xlang run`. Object-only output stops after codegen: no final executable link.

The compiler emits objects (and optionally static archives). When linking, the linker resolves:

- Project objects for the selected entry
- Runtime (linked by default for runnable programs)
- Declared dependencies (source-built objects or static libraries)
- Explicit extra objects passed on the command line

Cross-compilation selects platform-appropriate static libraries when those artifacts exist.

---

# Pipeline

```text
sources → parse/check → codegen → object (.o) / .a
                              ↘ link → executable   (when not object-only)
```

| Stage | Owner |
|-------|--------|
| Parse / check / codegen | Compiler (`parse`, `build` / `compile`, `test`) |
| Registry / dependency materialization | Package manager (`add`, `install`, …) |
| Symbol resolution and final image | Linker (executable `build` / `compile` and `run`) |

---

# What gets linked

| Input | How it appears |
|-------|----------------|
| Selected package entry | Object(s) from that entry’s sources |
| Shared project sources | Objects pulled in via imports |
| Runtime | Linked by default for programs |
| Dependencies from project config | Auto-linked when required by imports or static declarations |
| Explicit `.o` / `.a` on the CLI | Passed through to the linker |
| `declare external fn` | Must be satisfied by some linked object or archive |

You do not manually copy libraries into the project tree for normal dependency use. Installed registries and project cache supply paths; see [Installing](INSTALLING.md).

---

# External symbols

Language form (see also the language reference):

```xlang
declare external fn helper(x)
```

The implementation must exist in another translation unit that is part of the link line (another `.xlang` module built as an object, a static library, or an explicit `.o`).

Manual object workflow:

```sh
xlang compile lib.xlang --build=lib -o lib.o
xlang build main.xlang lib.o -o app
xlang run main.xlang lib.o
```

`build` and `compile` are interchangeable here. Package-managed dependencies do not need this manual step after `add` / `install` have registered them.

---

# Static libraries

When a dependency is added or installed with static delivery (`--static` where applicable):

1. The static archive is stored in the project cache and/or registry.
2. At link time the linker selects the archive matching the **target platform**.
3. Naming conflicts between publishers are avoided by qualified package names; the linker uses the resolved identity from the package manager.

Full static-library rules: [Static Library](STATIC_LIBRARY.md).

---

# Search paths

The linker and library resolver consult, in order consistent with install scopes:

1. Paths supplied on the command line
2. Project cache / build outputs
3. User registry (`~/.xlang/…`)
4. Root / `XLANG_HOME` layouts
5. Environment overrides such as `XLANG_LIB` / `XLANG_PATH` when set

User-installed packages are preferred over root when both exist, matching [Installing](INSTALLING.md).

---

# Source vs prebuilt

If a dependency is available both as source and as a static library:

- **Default:** compile from source and link the resulting objects.
- **Override:** project configuration prefers the prebuilt static library when `prefer_static` is set.

This is how the toolchain resolves the choice unless project config says otherwise.

---

# Examples

Build the default package entry and link dependencies automatically:

```sh
xlang build
xlang compile              # same as build
```

Build a named package from the project:

```sh
xlang build server
xlang compile server
```

Object-only, then link into an executable:

```sh
xlang compile lib.xlang --build=lib -o lib.o
xlang build main.xlang lib.o -o app
```

Run with automatic link of project dependencies:

```sh
xlang run cli
```

Add a static dependency, then build (install happens as needed):

```sh
xlang add john/network@1.0.0 --static
xlang build
```

---

# Related documents

| Document | Role |
|----------|------|
| [Package Manager](PACKAGE_MANAGER.md) | `build` / `compile` / `run` command surface |
| [Static Library](STATIC_LIBRARY.md) | Static artifact placement and flags |
| [Installing](INSTALLING.md) | Where registries live |
| [Project Config](PROJECT_CONFIG.md) | Entries, dependencies, source-vs-lib overrides |
| [Language Reference](LANGUAGE.md) | `declare external`, imports |
