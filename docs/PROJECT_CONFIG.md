# xlang Project Configuration

This document is the source of truth for **project-level** configuration: identity, packages, entries, dependencies, and overrides. The package manager, compiler, and linker read this file; they do not invent project shape on their own.

---

## Table of Contents

- [Overview](#overview)
- [File location and format](#file-location-and-format)
- [Core fields](#core-fields)
- [Packages and entries](#packages-and-entries)
- [Dependencies](#dependencies)
- [Build and link overrides](#build-and-link-overrides)
- [Publish fields](#publish-fields)
- [Examples](#examples)
- [Related documents](#related-documents)

---

# Overview

An xlang project is a single configuration root. The configuration may declare **one or more packages**. Multiple packages in one project are different **entry points** that share the same codebase — for example a CLI and a server under a `cmd/`-style layout — not separate disconnected products.

The configuration answers:

- What is this project called?
- Which packages (entries) can be built, run, tested, or published?
- Which dependencies does the project need?
- Should builds prefer source or prebuilt static libraries?

CLI defaults such as `xlang build` / `xlang compile` and `xlang run` with no path select the project’s default package.

---

# File location and format

| Item | Value |
|------|--------|
| Default filename | `xlang.conf` at the project root |
| Format | Structured text (TOML-like tables) using the schema below |

If both a legacy or alternate project file and `xlang.conf` exist, **`xlang.conf` wins**.

---

# Core fields

| Field | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Project name (human-facing; not necessarily a publish identity) |
| `version` | No | Default version inherited by packages that omit their own |
| `description` | No | Short summary |
| `default_package` | No | Package name selected by bare `build` / `compile` / `run` / `publish` |

---

# Packages and entries

Each package block defines one buildable/runnable (or library) unit:

| Field | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Package name unique within the project |
| `version` | No | Overrides project `version` for publish |
| `publisher` | For publish | Publisher segment of `publisher/name` |
| `entry` | For runnable packages | Path to the main source (for example `cmd/cli/main.xlang`) |
| `kind` | No | `bin` (default for entries with `entry`) or `lib` |
| `description` | No | Package-level summary |

Shared code lives elsewhere in the tree and is pulled in through normal `import` resolution. Packages do not each need a private copy of shared modules.

Layout:

```text
project/
  xlang.conf
  cmd/
    cli/main.xlang
    server/main.xlang
  libs/
    shared/...
```

---

# Dependencies

Dependencies are declared at project level and/or per package. Identifiers must be qualified:

```text
publisher/name@version
```

| Field | Description |
|-------|-------------|
| `dependencies` | List of package identities required to build |
| `static` | Optional boolean or list marking which deps use static delivery |

`xlang add` / `xlang remove` mutate these declarations. `xlang install` with no arguments installs everything listed here into the user registry by default ([Installing](INSTALLING.md)).

---

# Build and link overrides

| Field | Default | Description |
|-------|---------|-------------|
| `prefer_source` | `true` | When both source and a static library exist, compile from source |
| `prefer_static` | `false` | Prefer prebuilt static libraries when available |
| `cache_dir` | `.xlang/cache` | Project cache for materialized static libs and build outputs |

If both `prefer_source` and `prefer_static` are set, **`prefer_source` wins** unless `prefer_source = false` and `prefer_static = true`.

Linker search still respects user-then-root registry order ([Linking](LINKING.md)).

---

# Publish fields

| Field | Description |
|-------|-------------|
| `publisher` | Default publisher for packages that omit their own |
| `publish.target` | Remote or registry target identifier |
| `publish.include_static` | Whether to attach static artifacts on publish |

Full publish behavior: [Publishing](PUBLISHING.md).

---

# Examples

Minimal single-entry project:

```toml
name = "demo"
version = "0.1.0"
default_package = "cli"

[[package]]
name = "cli"
entry = "cmd/cli/main.xlang"
publisher = "zona"

[[dependencies]]
name = "zona/http"
version = "1.2.0"
```

Two entries sharing the same tree:

```toml
name = "shop"
version = "0.3.0"
default_package = "cli"
prefer_source = true

[[package]]
name = "cli"
entry = "cmd/cli/main.xlang"
publisher = "zona"

[[package]]
name = "server"
entry = "cmd/server/main.xlang"
publisher = "zona"

[[dependencies]]
name = "john/network"
version = "1.0.0"
static = true
```

Commands against this project:

```sh
xlang build              # builds default_package (cli)
xlang compile server     # same job as build; server package
xlang run cli
xlang test
xlang publish server
```

---

# Related documents

| Document | Role |
|----------|------|
| [Package Manager](PACKAGE_MANAGER.md) | How CLI commands consume this file |
| [Publishing](PUBLISHING.md) | Using `publisher` / version / artifacts |
| [Installing](INSTALLING.md) | Materializing `dependencies` |
| [Linking](LINKING.md) | Applying prefer_source / prefer_static |
| [Static Library](STATIC_LIBRARY.md) | Static dependency delivery |
