# xlang Publishing Reference

This document describes how xlang packages are prepared and published. Command invocation (`xlang publish`) is defined in the [Package Manager](PACKAGE_MANAGER.md) reference; this document is the source of truth for **what** is published and **how** versions and artifacts are produced.

---

## Table of Contents

- [Overview](#overview)
- [What you publish](#what-you-publish)
- [Versioning](#versioning)
- [Preparing a package](#preparing-a-package)
- [Publish flow](#publish-flow)
- [Artifacts](#artifacts)
- [Naming and ownership](#naming-and-ownership)
- [Related documents](#related-documents)

---

# Overview

Publishing takes one package from the current project and uploads (or registers) its metadata and artifacts to the configured publish target. A project may contain several packages; `xlang publish` without an argument publishes the default package, or you pass an explicit package name from the project configuration.

```sh
xlang publish
xlang publish <package-name>
```

---

# What you publish

A published unit includes:

| Part | Description |
|------|-------------|
| Identity | Qualified name `publisher/name` from project config |
| Version | Semver-style version declared for that package |
| Metadata | Description, license fields, and dependency list as recorded in the project |
| Source or binary artifacts | Source tree and/or prebuilt static library for supported platforms |

Consumers later obtain these via `install` / `add` (see [Installing](INSTALLING.md) and [Package Manager](PACKAGE_MANAGER.md)).

---

# Versioning

- Each package declares its own `version` in the [project configuration](PROJECT_CONFIG.md).
- `publish` refuses to overwrite an already-published identical `publisher/name@version` on the target (immutable versions).
- To ship a fix, bump the version in project config, rebuild if you ship static artifacts, then publish again.

---

# Preparing a package

Before publish:

1. Ensure the project configuration lists the package, its entry (if runnable), and its public exports.
2. Run `xlang parse` and `xlang test` on the relevant paths.
3. If you ship a static library, build/compile it first (`xlang build …` or `xlang compile …`) so the artifact exists. A static library that has not been compiled is not installed or published.
4. Confirm dependency names are qualified (`publisher/name`) to avoid consumer-side conflicts.

---

# Publish flow

```text
project config → select package → validate → collect artifacts → upload/register → done
```

How publish behaves:

- Local registry cache writes stay **user scope**; publishing to a remote index is separate from `install --root`.
- `publish` does not implicitly install into the root registry.
- Failure conditions include missing version, missing identity, failed tests when the project requires them, and missing static artifacts when the package declares `static` delivery.

Remote endpoint configuration lives in project config (publish target / credentials reference). Do not hard-code machine-local paths into published metadata.

---

# Artifacts

| Artifact kind | When included | Notes |
|---------------|---------------|-------|
| Source | Default for library packages | Consumers compile from source |
| Static library | When the package opts into static delivery | Per target platform; see [Static Library](STATIC_LIBRARY.md) |
| Objects / extras | Only if declared | Linked as described in [Linking](LINKING.md) |

If both source and a static library are published, consumers still follow the default rule: **source wins unless project config prefers the prebuilt library**.

---

# Naming and ownership

- The `publisher` segment of the qualified name must match the identity you are allowed to publish under on the target.
- Unqualified names are not accepted for publish.
- Changing `publisher/name` after a public release is a new package, not a rename of the old one.

---

# Related documents

| Document | Role |
|----------|------|
| [Package Manager](PACKAGE_MANAGER.md) | `publish` command surface |
| [Project Config](PROJECT_CONFIG.md) | Package identity, version, entries |
| [Static Library](STATIC_LIBRARY.md) | Static artifact rules |
| [Installing](INSTALLING.md) | How consumers receive published packages |
