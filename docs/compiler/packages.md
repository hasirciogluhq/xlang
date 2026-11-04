# xlang Packages

A package is a git repository of plain `.xl` source. `xlang package install github.com/user/repo` downloads its newest release into the project, and `import "github.com/user/repo"` uses it. That is the whole system.

## Vision

- **No registry, no central authority.** A package is named by its repository path. The repository host is the registry.
- **No required layout.** There is no `cmd/`, no `apps/`, no workspace, no monorepo or polyrepo rule. A package is a directory with `.xl` files; `xlang.toml` is optional inside it. Anything this system does, you can also do by hand with `git clone` and a relative import; the tool only saves the typing.
- **The compiler does not care.** Standards and conventions are not enforced. The compiler resolves an import to a directory, and the package manager decides which directory that is.
- **The standard library is a package too.** The compiler embeds a default copy, and any project may replace it with a local directory or a repository at a chosen version, so an old compiler can use another stdlib and a new stdlib can be tried on an old compiler.
- **Source first, binaries when you want them.** Packages are pure `.xl` by default. A package may also ship prebuilt files for some platforms; the compiler links them.

## Files

| File | Written by | Committed | Role |
|------|-----------|-----------|------|
| `xlang.toml` | you (and `package install/remove/update` edit it in place, keeping comments) | yes | dependencies, `[std]`, `[[native]]` |
| `xlang.lock` | `package install/update/remove` | yes | exact version and commit of every downloaded package |
| `.xlang/cache/` | `package install` | no (in `.gitignore`) | the downloaded sources: `.xlang/cache/<repository path>@<tag>/` |

The project root is the directory of the nearest `xlang.toml`, searched upward from the directory of the file being built. The cache lives in the project root, like `node_modules`; there is no global cache. Without any `xlang.toml`, nothing about packages applies and the embedded stdlib is used.

## xlang.toml

```toml
[package]
name = "myapp"                       # optional, informational

[dependencies]
"github.com/user/repo" = "v1.2.0"    # exact release tag
"github.com/user/other" = "latest"   # newest stable release, pinned by xlang.lock
"git.example.org/team/lib" = { version = "v0.3.1", source = "git@git.example.org:team/lib.git" }
"example.org/me/local" = { path = "../local" }   # a directory on disk, not downloaded

[std]                                # optional: replaces the embedded stdlib
package = "github.com/user/std"
version = "v0.4.0"                   # or: path = "../my-std"

[[native]]                           # prebuilt files this package ships
os = "linux"                         # optional: linux | macos | windows
arch = "x86_64"                      # optional: x86_64 | aarch64
link = ["lib/linux-x86_64/libfoo.a"] # object files / archives, relative to the package root
```

Rules:

- A dependency key is a **repository path**: `host.tld/segment/...`. The first segment contains a dot; no segment is empty, `.` or `..`; `std` is not a dependency name. Keys must be quoted (they contain `.` and `/`).
- A value is a version string, or a table with `version`, `source`, `path`. `path` cannot be combined with the others. `version` defaults to `latest`.
- `source` replaces the default URL `https://<repository path>.git`. It may be any URL `git` understands (ssh, https, `file://`) or a directory (`./` and `../` are relative to the manifest).
- Unknown keys are errors (`E0740`), so typos do not pass silently.
- `latest` is allowed in a **project** manifest only. A package that others depend on must pin exact tags for its own dependencies (`E0742`). The lock file keeps `latest` reproducible.
- The format is a TOML subset: tables, arrays of tables, quoted and dotted keys, strings, integers, booleans, arrays, inline tables. Multi-line strings, floats and dates are rejected.

## Version tags

Packages are versioned by git tags and nothing else; there is no version field to keep in sync.

- A release tag is `vMAJOR.MINOR.PATCH` or `MAJOR.MINOR.PATCH`: `v1.2.0`, `1.2.0`. `v` is lowercase, all three numbers are required.
- A tag with a suffix after `-` (`v1.2.0-rc.1`) is a pre-release. It is never chosen as `latest`; it is installed only when named exactly.
- A tag whose suffix ends in `test` (`v1.2.0-test`, `v1.2.0-rc-test`) is a test tag. It is never chosen as `latest`, like any pre-release, but it installs when named: `xlang package install host/user/repo@v1.2.0-test`.
- `latest` (the default when no version is given) means the highest **stable** release (numeric order, so `v1.10.0` beats `v1.9.0`). Tags that are not versions (`main`, `nightly`), pre-releases and test tags are ignored when looking for it. It never falls back to them: a repository with no stable release tag fails with `E0744` and nothing is written.
- Naming a version that breaks the format (`"main"`, `"1.2"`) in `xlang.toml` or on the command line is `E0742`, before anything is downloaded.
- `v1.0.0` and `1.0.0` are the same version; if both exist, the `v` spelling is used.
- A release tag must never move. The lock stores the commit of each tag; if a tag later points to another commit, installing fails with `E0744`.

## Commands

All commands act on the `xlang.toml` found from the current directory (`install` with a repository creates one in the current directory when there is none).

| Command | Effect |
|---------|--------|
| `xlang package install` | Download everything `xlang.toml` lists (and what those packages list), write `xlang.lock`. With a lock, the locked versions and commits are used; the network is touched only for what the cache lacks. |
| `xlang package install host/user/repo` | Add the repository at its newest stable release; the exact tag is written to `xlang.toml`. |
| `xlang package install host/user/repo@v1.2.0` | Add that exact tag. Any tag of the version format works, including pre-release and `-test` tags (`@v1.2.0-test`). An existing entry keeps its `source`. |
| `xlang package install --std=host/user/std[@tag]` | Record the repository as `[std]` and download it. |
| `xlang package install --update` | Ignore the lock for `latest` entries. |
| `xlang package update [repository...]` | Move all (or the named) git dependencies and a git `[std]` to their newest stable release, rewriting their versions in `xlang.toml`. |
| `xlang package remove repository...` | Delete the entry from `xlang.toml` and refresh the lock. Cached sources stay until `clean`. |
| `xlang package list` | Show each package with its version and commit, `[std]`, and `[indirect]` for packages only another package needs. Exit status 1 when something is missing. |
| `xlang package clean` | Delete `.xlang/cache`. |

`install`, `update` and `remove` change `xlang.toml` and `xlang.lock` only when everything resolved; a failure leaves both untouched. Edits keep your comments and layout.

**Builds never use the network.** `build`, `run`, `check`, `test` read the lock and the cache only. A missing package is `E0741` with the fix `run xlang package install`. This keeps builds fast, offline and deterministic.

`git` must be on `PATH`. It is run without a shell, never prompts for a password (`GIT_TERMINAL_PROMPT=0`), and uses your normal git configuration, so private repositories work with an SSH key or a credential helper.

## Using a package

```
from "github.com/user/repo" import connect          // the repository root: index.xl
from "github.com/user/repo/sub" import helper       // sub.xl or sub/index.xl inside it
import "github.com/user/repo" as repo
```

- An import path that equals a dependency key, or continues it with `/`, is resolved inside that package's directory. The longest matching dependency wins.
- Inside a package, `from "util" import x` is relative to the importing file, as everywhere else. A package may also import its own dependencies by repository path.
- A path that looks like a repository path but matches no dependency is `E0745`; one that matches but names a missing file is `E0711`.
- The module key of a package file is its import path (`github.com/user/repo/sub`), which is also what appears in mangled names.
- There is one copy of each package per build. If two packages need different versions of one repository, the version in the project's own `[dependencies]` wins; if the project does not name it, the build stops with `E0743`. There is no automatic "highest version" rule.
- Local `path` dependencies are used in place (not copied, not locked); their own dependencies are resolved like any other package's.

## The standard library

Order of precedence, first match wins:

1. `--std=<directory>`: that directory (it must contain `index.xl`).
2. `--std=host/user/repo[@tag]`: that repository, which must have been installed (`xlang package install --std=...`, or listed in `[std]`). Without `@tag` the locked version is used.
3. `[std]` in `xlang.toml`.
4. The stdlib embedded in the compiler.

`--no-std` loads no stdlib. The stdlib package needs `index.xl` at its root; its own `[[native]]` entries are honored, its dependencies are not resolved. Stdlib modules keep the stdlib's special role (their `register`ed names are defaults that user code replaces, their `@init` functions run first), wherever the stdlib came from.

## Prebuilt files (`[[native]]`)

A package may ship compiled code next to its source, for example because a part is written in another language or is too slow as `.xl`. The package's own `xlang.toml` lists it:

```toml
[[native]]
os = "linux"
arch = "x86_64"
link = ["lib/linux-x86_64/libfoo.a"]

[[native]]
os = "macos"
arch = "aarch64"
link = ["lib/macos-aarch64/libfoo.a"]
```

- The package's `.xl` code declares the symbols with `declare fn name(...)`; the linker finds them in the listed files. A package can still be pure source on platforms without an entry; use `#if os == "..."` in its code to choose between a native call and an `.xl` fallback.
- Entries whose `os` and `arch` match the target (`--target` aware) are added to the link line of executables and shared libraries (`--emit=exe|shared`); empty `os` or `arch` match everything. The files are linked in addition to the program's objects and nothing else.
- The files are committed in the repository at the release tag; they are not downloaded separately. A listed file that does not exist is `E0746`.
- `--emit=static` and `--emit=obj` do not include them, because they stop before linking.
- A library built by xlang carries a copy of the stdlib it was built with. Build prebuilt xlang libraries with `--no-std`, or linking them into a program with the stdlib fails with duplicate symbols.
- There is no build step in a package: the compiler never runs commands from a package. Build the binaries yourself before tagging.

## Diagnostics

| Code | Meaning |
|------|---------|
| `E0740` | `xlang.toml` / `xlang.lock` invalid (syntax, unknown key, bad repository path, bad `[[native]]`) |
| `E0741` | package not installed, or the cache does not match the lock |
| `E0742` | version is not in the tag format, or `latest` where an exact tag is required |
| `E0743` | two packages need different versions and the project does not choose |
| `E0744` | `git` failed, no release tag, or a tag moved to another commit |
| `E0745` | import of a repository that is not a dependency |
| `E0746` | a `[[native]]` file does not exist |

`xlang --explain E0741` prints the long explanation.

## Questions

- **Where do packages go?** `<project root>/.xlang/cache/<repository path>@<tag>/`, as a plain source tree without `.git`, plus a `.xlang-commit` file that records its commit. Delete the directory any time; `install` restores it from the lock.
- **Is the download verified?** The commit hash of the tag is recorded in `xlang.lock`. A cache that disagrees with the lock is `E0741`; a tag that moved is `E0744`. There are no separate checksums or signatures.
- **How do I publish?** Push a tag such as `v1.0.0`. There is no publish command.
- **Can I use a package that is not on a git host?** Put it in a directory and use `{ path = "..." }`, or point `source` at any URL or directory `git` can clone.
- **Can I use a fork or a mirror?** Set `source` for that dependency.
- **What if I do not want the tool at all?** Clone the repository yourself and import it by relative path. The compiler needs no manifest to build.
- **Monorepos?** The repository root is the package; `import "host/user/repo/sub"` reaches into it. There are no workspaces and no sub-packages with their own versions.
- **Windows hosts?** The package tool uses POSIX process APIs; the compiler is built for macOS and Linux hosts. Targeting Windows (`--target`) works.

## Related documents

| Document | Role |
|----------|------|
| [Language Reference]({{ site.baseurl }}/language/index.html) | import syntax and module resolution |
| [Linking]({{ site.baseurl }}/compiler/linking.html) | how objects and prebuilt files are linked |
| [Install]({{ site.baseurl }}/compiler/install.html) | installing the compiler itself |
