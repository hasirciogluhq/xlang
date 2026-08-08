# xlang Publishing Reference

This document describes how xlang packages are prepared and published. Command invocation (`xlang publish`) is defined in the [Package Manager](PACKAGE_MANAGER.md) reference; this document is the source of truth for **what** is published and **how** versions and artifacts are produced.

---

## Table of Contents

- [Overview](#overview)
- [What you publish](#what-you-publish)
- [Versioning](#versioning)
- [Immutability](#immutability)
- [Preparing a package](#preparing-a-package)
- [Publish flow](#publish-flow)
- [Artifacts](#artifacts)
- [Naming and ownership](#naming-and-ownership)
- [Package manager server](#package-manager-server)
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
| Git hash | Source control commit hash bound to this publish (permanent with the version) |
| Metadata | Description, license fields, and dependency list as recorded in the project |
| Content hash | Checksum of each published artifact (source archive, static library, …) |
| Source or binary artifacts | Source tree and/or prebuilt static library for supported platforms |

Consumers later obtain these via `install` / `add`. Those commands, then compile and link, verify the hash before proceeding ([Installing](INSTALLING.md#integrity-hash--checksum)).

---

# Versioning

- Each package declares its own `version` in the [project configuration](PROJECT_CONFIG.md).
- `publish` refuses to overwrite an already-published identical `publisher/name@version` on the target.
- To ship a fix, bump the version in project config, rebuild if you ship static artifacts, then publish again.

---

# Immutability

A published `publisher/name@version` is **permanent**. It is never rolled back, replaced, or silently mutated.

- The published **version** and the bound **git hash** are fixed for that release.
- Artifact content hashes for that release stay fixed.
- The publisher cannot undeploy or delete that version through normal CLI commands.
- Removal happens only through a special process (dedicated email or formal request). Without that process, the version stays.

Re-publishing the same version is an error. A new git commit that should ship must use a new version.

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
project config → select package → validate → hash artifacts → collect artifacts → upload/register → done
```

How publish behaves:

- Each artifact’s content hash is computed and stored in published metadata. Consumers use that hash on install, compile, and link.
- The publish records the git hash of the source tree for that release; version + git hash are immutable together.
- Local registry cache writes stay **user scope**; publishing to a remote index is separate from `install --root`.
- `publish` does not implicitly install into the root registry.
- Failure conditions include missing version, missing identity, missing git hash, failed tests when the project requires them, missing content hashes, attempting to overwrite an existing version, and missing static artifacts when the package declares `static` delivery.

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

# Package manager server

There is **no** package manager server/backend in the current system surface described by these docs for local CLI behavior. Client-side publish, install, hash checks, and immutability rules above still hold for whatever target is configured.

### Future of development (package manager server/backend only)

A remote package manager server/backend is planned as future work. That server will enforce the same immutability rules (permanent `version` + git hash; no delete without special email/request). Do not treat other undocumented features as “future of development” unless explicitly marked the same way.

---

# Related documents

| Document | Role |
|----------|------|
| [Package Manager](PACKAGE_MANAGER.md) | `publish` command surface |
| [Project Config](PROJECT_CONFIG.md) | Package identity, version, entries |
| [Static Library](STATIC_LIBRARY.md) | Static artifact rules |
| [Installing](INSTALLING.md) | How consumers receive published packages |
