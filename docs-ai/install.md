# install

Prebuilt compilers exist for macos and linux on arm64 and amd64, and there is no native windows build.
The install script picks the release archive by the host operating system and CPU and installs the latest release unless a version is requested.
Both the install script and self-update verify the archive against the release checksums file and refuse an archive without a checksum entry.
A new binary must start with the version flag before it replaces the installed one, and the replacement is a rename that keeps the old binary on any failure.
The self-update command is also named update and upgrade, prints that the compiler is up to date when no newer release exists, and installs nothing under the check flag.
Self-update leaves a homebrew install alone and points to brew upgrade, and it needs curl on the path.
The latest release never means a pre-release, and a tag that contains a dash is a pre-release.
The completions command prints a bash, zsh or fish completion script generated from the command tree, and no installer edits shell startup files.
Output is colored only on a terminal, NO_COLOR turns colors off, the color option overrides both, and log files and json output are never colored.
