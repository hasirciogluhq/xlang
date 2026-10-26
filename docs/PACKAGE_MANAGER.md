# xlang Package Manager Reference

This document is the **source of truth** for the xlang package manager CLI. Other documents (publishing, installing, linking, static libraries, project config) specialize behavior for their domain; when those documents conflict with this one on command names, flags, or defaults, this document wins.

> **Implementation status:** the package manager, registries, `xlang.conf` project files, checksums and publishing described in this document and in INSTALLING, PROJECT_CONFIG, PUBLISHING and STATIC_LIBRARY are a design and are **not implemented**. The CLI today has only `build`, `run`, `check`, `parse`, `layout` and `test` (see the sections below, which describe the real behavior), and takes a single `.xl` source file. There is no `compile`, `add`, `remove`, `install`, `uninstall`, `info`, `search` or `publish` command.

---

## Table of Contents

- [Overview](#overview)
- [Scopes](#scopes)
- [Identity and naming](#identity-and-naming)
- [Integrity](#integrity)
- [Command reference](#command-reference)
  - [add](#add)
  - [remove](#remove)
  - [install](#install)
  - [uninstall](#uninstall)
  - [info](#info)
  - [search](#search)
  - [publish](#publish)
  - [build](#build)
  - [run](#run)
  - [parse and check](#parse-and-check)
  - [test](#test)
- [Responsibility split](#responsibility-split)
- [Related documents](#related-documents)

---

# Overview

The package manager is the unified front door for dependency management, local registry operations, and common compile/run workflows. It installs packages at **user scope by default**. Root (system) packages remain searchable and usable when present. Packages may be low-level machine-facing libraries or optional higher-level APIs (`net`/`http`, database clients, and similar); the language itself stays low-level either way.

A project is described by its [project configuration](PROJECT_CONFIG.md). One project may declare several packages — typically different entry points that share the same source tree (similar to a `cmd/` layout).

---

# Scopes

| Scope | Meaning | Default for install? |
|-------|---------|----------------------|
| **User** | Registry under the current user’s home (for example `~/.xlang/`) | Yes |
| **Root** | System-wide registry (requires elevated privileges to write) | No |

Resolution order when looking up an installed package:

1. Project-local cache / lock (if present)
2. User registry
3. Root registry

`search` and `info` scan the same order unless a scope flag restricts them. Write operations (`install`, `uninstall`, `publish` into a local registry) default to user scope. Pass `--root` (alias `--global`) only when you intend to write to the root registry.

---

# Identity and naming

Packages use a **qualified name**:

```text
<publisher>/<name>
```

Optional version:

```text
<publisher>/<name>@<version>
```

Examples: `john/network`, `zona/http@1.2.0`.

If two publishers ship the same unqualified name, the compiler reports a conflict unless the import or dependency uses a qualified identity. Prefer qualified names for anything that leaves your machine.

---

# Integrity

Artifacts are identified by content **hash / checksum** as well as `publisher/name@version`. Full rules live in [Installing](INSTALLING.md#integrity-hash--checksum). Short form:

```text
check → install
check → compile
check → link
```

Any failed check **aborts**. `add` (when it installs) and `install` never register a package that fails verification. `build` / `compile` never compile from a failed artifact. The linker never links a failed `.o` / `.a`.

---

# Command reference

All examples below use the `xlang` binary. This is how the CLI works.

## add

Add a dependency to the current project and ensure it is available locally.

```sh
xlang add <publisher/name[@version]>
xlang add <publisher/name[@version]> --static
xlang add <publisher/name[@version]> --root
```

| Behavior | Detail |
|----------|--------|
| Project record | Records the dependency and its content hash in the project configuration / lock |
| Missing package | Checks checksum, then installs (user scope by default); abort on hash mismatch |
| `--static` | Prefer / cache a static library artifact for linking; see [Static Library](STATIC_LIBRARY.md) |
| `--root` / `--global` | Install into the root registry if the package is not already available |

`add` is project-facing: it declares the package as a dependency of this project.

## remove

Remove a dependency from the current project. Does **not** uninstall the package from the user or root registry.

```sh
xlang remove <publisher/name>
```

After `remove`, the project no longer declares the dependency. Artifacts may remain installed for other projects; use `uninstall` to delete them from a registry.

## install

Install a package into a registry without necessarily adding it to the current project.

```sh
xlang install <publisher/name[@version]>
xlang install <publisher/name[@version]> --root
xlang install                          # install all project dependencies
```

| Behavior | Detail |
|----------|--------|
| Default scope | User registry |
| `--root` / `--global` | Root registry |
| No arguments | Resolve and install every dependency declared by the project |
| Integrity | Checksum verified before any registry write; abort on mismatch |

Libraries are **built/compiled** before they are installed as static artifacts. See [Installing](INSTALLING.md).

## uninstall

Remove a package from a registry.

```sh
xlang uninstall <publisher/name[@version]>
xlang uninstall <publisher/name[@version]> --root
```

| Behavior | Detail |
|----------|--------|
| Default scope | User registry |
| `--root` / `--global` | Root registry |

`uninstall` does not rewrite project dependency lists. Use `remove` for that.

## info

Show metadata for a package (identity, versions, description, artifacts, content hash, install location).

```sh
xlang info <publisher/name[@version]>
```

Looks up user scope first, then root, unless restricted by flags documented with install scopes.

## search

Search known registries and indexes for packages matching a query.

```sh
xlang search <query>
```

Results may include packages available only in the root registry even when your default write scope is user.

## publish

Publish a package from the current project (or a selected project package) to the configured publish target.

```sh
xlang publish
xlang publish <package-name>
```

Publishing rules, versioning, immutability (version + git hash permanent), and artifact layout are defined in [Publishing](PUBLISHING.md). The command surface here is the source of truth for invocation.

## build

`build` runs the compiler (and, when producing an executable, shared library or archive, the linker). It takes one source file; there is no `compile` alias.

```sh
xlang build main.xl                          # executable (default, --emit=exe)
xlang build main.xl -o app
xlang build lib.xl --emit=obj -o lib.o       # object
xlang build lib.xl --emit=static -o lib.a    # static library
```

| Mode | Flags | Result |
|------|-------|--------|
| Executable | default or `--emit=exe` | Linked program |
| Object | `--emit=obj` | Object file |
| LLVM IR / bitcode | `--emit=ir`, `--emit=bc` | Text IR / bitcode |
| C header | `--emit=header` | Header for exported items |
| Static library | `--emit=static` | Archive |
| Shared | `--emit=shared` | Shared library |

Linking details live in [Linking](LINKING.md).

## run

Build a source file into a temporary executable, run it and exit with its status.

```sh
xlang run main.xl
```

## parse and check

`parse` preprocesses, expands macros and parses a file, printing a summary (`--ast` prints the full tree). `check` additionally runs the semantic checks (`--hir` prints the typed HIR). Neither links.

```sh
xlang parse main.xl
xlang check main.xl
```

## test

Build and run test files. Patterns are a file, a directory, a glob (`**`, `*`, `?`), `re:<regex>` or a path substring; the default is `**/*.test.xl`.

```sh
xlang test
xlang test <path>
xlang test --list
```

Test files are ordinary programs ending in `.test.xl` that return a non-zero status on failure (see `std/testing`).

---

# Responsibility split

The CLI is one surface; ownership of behavior is split so implementations stay clear:

| Area | Owns | Commands (primary) |
|------|------|--------------------|
| **Package manager** | Registries, scopes, dependency records, search/publish metadata | `add`, `remove`, `install`, `uninstall`, `info`, `search`, `publish` |
| **Compiler** | Parse, type/check pipeline, IR/object generation, tests | `parse`, `check`, `build`, `test` |
| **Linker** | Final executable or shared library | Final stage of `build` and `run` |

Source of truth:

- CLI names and defaults → this document
- Project shape and multi-package entries → [Project Config](PROJECT_CONFIG.md)
- Registry write/read paths → [Installing](INSTALLING.md)
- Artifact publication → [Publishing](PUBLISHING.md)
- Objects and static libs at link time → [Linking](LINKING.md) and [Static Library](STATIC_LIBRARY.md)

If both a dependency’s source and a prebuilt static library are available to a project, **source takes precedence by default** (rebuild from source). Project configuration overrides this; see [Project Config](PROJECT_CONFIG.md).

---

# Related documents

| Document | Role |
|----------|------|
| [Project Config](PROJECT_CONFIG.md) | Project file, packages, entries, overrides |
| [Installing](INSTALLING.md) | User/root registries and hash checks |
| [Publishing](PUBLISHING.md) | How packages leave the machine |
| [Static Library](STATIC_LIBRARY.md) | Static `.a` artifacts and `--static` |
| [Linking](LINKING.md) | Compiler/linker pipeline for dependencies |
| [Language Reference](LANGUAGE.md) | Language syntax, imports, `declare external` |
