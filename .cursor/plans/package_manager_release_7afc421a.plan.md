---
name: Package Manager Release
overview: "GitHub Releases for the xlang toolchain and package-manager web stack: one shared version, multiple C++ entrypoints plus one web repo, all published together under that version."
todos: []
isProject: false
---

# Package Manager + Toolchain Release

## Outcome only

- **One version number** drives everything. When that version ships, every product in the release set updates to it at the same time.
- **Classic GitHub Releases** are the distribution channel (no custom release protocol in this plan).
- **Local toolchain:** a single CLI binary named `xlang` (compiler + package-manager commands in one app).
- **Multiple C++ entrypoints** exist for the toolchain/repo; each is an xmake target. In release mode, each target that belongs to the release set is built and published.
- **If one version contains N shippable products, publish N GitHub release assets/artifacts (or N coordinated release publications) all tagged with the same version.** Nothing ships on a different version in that cut.
- **Web side** (package-manager CDN / site / web services) lives in **one repo** and is released as **part of the same version cut** as the CLI/toolchain—not a separate versioning scheme.
- **Local vs release:** day-to-day local work builds what you need; **release mode** is what produces the versioned GitHub release set end-to-end.
- **Released `xlang` carries embedded default runtime + bridges** (see Compiler Runtime Rebuild). Release mode must complete the **two-phase bootstrap** before the final binary is published: stage-1 compiler builds xlang runtime objects, then the compiler is rebuilt with those objects embedded. User-facing overrides (`--runtime=`, `--bridge=`) remain available after install; embed is the default payload, not the only source.

## Non-goals (do not invent here)

- Exact folder trees, CDN URL layouts, CI YAML, or asset filename schemas beyond “GitHub Release + shared version”.
- Compiler/runtime internals beyond the release/embed/bootstrap outcome (see Compiler Runtime Rebuild).

## Done when

A single version bump produces a complete GitHub Release cut where the `xlang` CLI (with embed bootstrap done) and the web/CDN package-manager surface (and any other C++ entrypoints in that cut) all appear under that same version together.
