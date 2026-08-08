# xlang Installing and Uninstalling

This document is the source of truth for **registry scopes**, install locations, and uninstall behavior. CLI names and flags are defined in the [Package Manager](PACKAGE_MANAGER.md) reference; this document specializes how those commands touch disk.

---

## Table of Contents

- [Overview](#overview)
- [Scopes and locations](#scopes-and-locations)
- [Integrity (hash / checksum)](#integrity-hash--checksum)
- [install](#install)
- [uninstall](#uninstall)
- [Relationship to add and remove](#relationship-to-add-and-remove)
- [Resolution when both scopes have a package](#resolution-when-both-scopes-have-a-package)
- [Static libraries](#static-libraries)
- [Related documents](#related-documents)

---

# Overview

xlang keeps installed packages in registries. **User scope is the default** for every write. Root scope is optional: the toolchain **searches and uses** root packages even when installs go to the user registry.

```sh
xlang install <publisher/name[@version]>
xlang install <publisher/name[@version]> --root
xlang uninstall <publisher/name[@version]>
xlang uninstall <publisher/name[@version]> --root
```

---

# Scopes and locations

| Scope | Typical location | Who can write | Default |
|-------|------------------|---------------|---------|
| User | Under the home directory (for example `~/.xlang/`) | Current user | Yes |
| Root | System prefix (for example under `/usr/local` or an `XLANG_HOME` root layout) | Elevated / admin | No |

Environment hints used when resolving layouts:

- User registry under `~/.xlang/`
- Optional `XLANG_HOME` for a custom root layout
- Project cache at `.xlang/cache` for dependency materialization during builds

Do not hard-code absolute install paths in source; use package identities and the resolver.

---

# Integrity (hash / checksum)

Every packaged artifact carries a **content hash** (checksum). The package manager, compiler, and linker verify that hash before they proceed. Mismatch or missing expected hash → **abort** with an error. No install, no compile, no link of untrusted or corrupted bytes.

| Stage | When | On failure |
|-------|------|------------|
| **Install** | Before writing into the user/root registry or project cache | Abort; nothing is registered |
| **Compile** | Before `build` / `compile` consumes dependency source or artifacts | Abort; no codegen |
| **Link** | Before the linker attaches `.o` / `.a` from a dependency | Abort; no executable |

Expected hashes come from:

1. The published package metadata ([Publishing](PUBLISHING.md))
2. The project lock / dependency record written by `add` / `install`

`add` and `install` always **check first, then install**. `build` / `compile` always **check first, then compile**. Executable link always **check first, then link**.

Verification covers the artifact actually used (source tree archive, static library, or locked object). Tampered cache entries fail the same way as a bad download.

---

# install

Install places a package into a registry so compilers and linkers can find it.

| Form | Behavior |
|------|----------|
| `xlang install <id>` | Install into the **user** registry |
| `xlang install <id> --root` | Install into the **root** registry |
| `xlang install` | Install all dependencies declared by the current project (user scope by default) |

Rules:

1. **Check** the artifact checksum against the expected hash; abort on mismatch.
2. Prefer an already-satisfying version in the target scope whose hash still matches; otherwise fetch, **check**, then materialize.
3. Static library packages are **compiled** (`build` / `compile`) before their `.a` (or equivalent) artifact is registered. Source-only packages install without a prior local build (still checksum-checked).
4. `install` does not add a dependency line to the project. Use `add` when the project declares the package.
5. After install, imports and automatic linking resolve the package according to [Linking](LINKING.md), with the same hash checks at compile and link.

---

# uninstall

Uninstall deletes a package from a registry.

| Form | Behavior |
|------|----------|
| `xlang uninstall <id>` | Remove from the **user** registry |
| `xlang uninstall <id> --root` | Remove from the **root** registry |

Rules:

1. Uninstall does not edit project configuration. Orphaned dependency entries remain until `remove`.
2. Uninstall fails clearly if the package is not present in the selected scope.
3. Other projects that still declare the package need a fresh `install` / `add` before their next build.

---

# Relationship to add and remove

| Command | Project dependency list | Registry |
|---------|-------------------------|----------|
| `add` | Writes | Installs if missing (user by default) |
| `remove` | Writes (deletes entry) | Leaves registry unchanged |
| `install` | Unchanged | Writes |
| `uninstall` | Unchanged | Deletes |

Typical project workflow:

```sh
xlang add zona/http@1.2.0
# later
xlang remove zona/http
xlang uninstall zona/http          # optional: free user-registry space
```

---

# Resolution when both scopes have a package

Lookup order:

1. Project-local cache / locked materialization
2. User registry
3. Root registry

`info` and `search` report which scope satisfied the query. Builds use the first hit unless project configuration pins a scope or path.

---

# Static libraries

- `xlang add … --static` (and install paths that request static delivery) register a static artifact into the project cache and/or registry as described in [Static Library](STATIC_LIBRARY.md).
- Clearing project cache is a package-manager concern; after a cache clear, the next build/compile re-fetches or rebuilds static artifacts.

---

# Related documents

| Document | Role |
|----------|------|
| [Package Manager](PACKAGE_MANAGER.md) | Command surface and defaults |
| [Publishing](PUBLISHING.md) | Producer side of the same artifacts |
| [Static Library](STATIC_LIBRARY.md) | Static-specific install notes |
| [Project Config](PROJECT_CONFIG.md) | Declared dependencies and overrides |
| [Linking](LINKING.md) | How installed artifacts attach at link time |
