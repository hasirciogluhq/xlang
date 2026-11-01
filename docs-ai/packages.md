# packages

A package is a git repository of plain xl source named by its repository path such as github.com/user/repo, and there is no registry.
The project file is xlang.toml, found from the directory of the built file upward, and it lists dependencies, an optional std table and native entries.
The lock file xlang.lock records the exact tag and commit of every downloaded package, and the sources live in the cache directory .xlang/cache of the project root.
The package command has the actions install, update, remove, list and clean, and builds never use the network and report E0741 for a package that is not installed.
A release tag is v followed by major, minor and patch numbers or the same without the v, a dash suffix makes a pre-release, and a suffix ending in test makes a test tag, and neither is ever chosen as latest but both install when the version is named.
Latest is the default, means the highest stable release tag, never falls back to pre-release or test tags and is E0744 when none exists, and it is allowed only in a project manifest, while a version not in the tag format is E0742 before anything is downloaded.
An import path that equals a dependency path or continues it with a slash resolves inside that package, the longest dependency wins, and an undeclared repository-like path is E0745.
There is one copy of each package per build, the project's own version choice wins over an indirect one, and two different indirect versions are E0743.
The std table, or the std option as a directory or a repository path with an optional tag, replaces the embedded standard library and the command line wins over the manifest.
A native entry with optional os and arch lists prebuilt files that are linked into executables and shared libraries for a matching target, a missing file is E0746, and the compiler never runs commands from a package.
