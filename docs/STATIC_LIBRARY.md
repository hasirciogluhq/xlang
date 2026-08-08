# xlang Static Library Reference

This document provides a professional overview of the static library system in the xlang programming language. It explains how static library artifacts are produced, registered, and linked. Command names and install defaults are defined in the [Package Manager](PACKAGE_MANAGER.md) and [Installing](INSTALLING.md) documents; this document is the source of truth for **static-specific** rules.

---

## Table of Contents

- [Overview](#overview)
- [Where static libraries live](#where-static-libraries-live)
- [Adding a static dependency](#adding-a-static-dependency)
- [Compiler and linker rules](#compiler-and-linker-rules)
- [Related documents](#related-documents)

---

# Overview

The static library system in xlang follows principles similar to static libraries in C. At compile time, the appropriate static libraries are linked for the **target platform**, and cross-compilation is supported.

A static library is an installable artifact (typically `.a` / platform equivalent) associated with a qualified package identity such as `john/network`. Build/compile the library before installing or publishing it as a static artifact.

---

# Where static libraries live

You do not manually place static libraries into ad-hoc project folders for normal use. The package manager registers them in a registry and/or the project cache.

| Scope | Role |
|-------|------|
| **User** (default) | Installs under the user registry (for example `~/.xlang/`) |
| **Root** | System-wide registry; searchable and usable even when you install as user |
| **Project cache** | Materialized copies for the current project’s builds |

Resolution follows user-then-root order unless project configuration pins otherwise. See [Installing](INSTALLING.md).

Naming matters. If two publishers provide the same unqualified name, you get a compile-time conflict. Use qualified names (`john/network`, `zona/network`) so the compiler and linker can pick the correct archive.

---

# Adding a static dependency

```sh
xlang add <publisher/name@version> --static
```

Behavior (see [Package Manager](PACKAGE_MANAGER.md)):

- If the package is not installed, `add` installs it (**user scope by default**).
- `--root` / `--global` writes the install to the root registry when needed.
- `--static` requests static delivery: the archive is cached for the project and linked automatically during compilation when selected.
- Clearing the project cache is a package-manager concern; after a clear, the next build/compile re-fetches or rebuilds as needed.

If both library artifacts and source for the same dependency are available, **source takes precedence by default**. Override this in [project configuration](PROJECT_CONFIG.md).

---

# Compiler and linker rules

- The compiler emits or selects platform-specific static archives when building a `lib` package for static delivery (`build` / `compile`).
- Object-only output stops at `.o`; executable output links those archives as described in [Linking](LINKING.md).
- Project configuration governs prefer-source vs prefer-static and cache location.
- When documents disagree on CLI defaults, [Package Manager](PACKAGE_MANAGER.md) wins; on link order and symbol resolution, [Linking](LINKING.md) wins; this document wins on static-artifact semantics.

---

# Related documents

| Document | Role |
|----------|------|
| [Package Manager](PACKAGE_MANAGER.md) | `add --static`, scopes, command surface |
| [Installing](INSTALLING.md) | User/root registries |
| [Linking](LINKING.md) | How `.a` files attach |
| [Publishing](PUBLISHING.md) | Shipping static artifacts |
| [Project Config](PROJECT_CONFIG.md) | prefer_source / prefer_static |
| [Language Reference](LANGUAGE.md) | Imports and `declare external` |
