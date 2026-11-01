# install

Prebuilt compilers exist for macos and linux on arm64 and amd64, and there is no native windows build.
The install script picks the release archive by the host operating system and CPU and installs the latest release unless a version is requested, and a tag that contains a dash is a pre-release that is never the latest.
Both the install script and self-update verify the archive against the release checksums file and refuse an archive without a checksum entry.
A new binary must start with the version flag before it replaces the installed one, and the replacement is a rename that keeps the old binary on any failure.
The self-update command is also named update and upgrade, prints that the compiler is up to date when no newer release exists, and installs nothing under the check flag.
Self-update leaves a homebrew install alone and points to brew upgrade, and it needs curl on the path.
The completions command prints a bash, zsh or fish completion script generated from the command tree, and its install action sets up the shells in use.
The install script runs the install action unless told not to, self-update only refreshes scripts that already exist, and the homebrew formula installs the scripts through brew.
The install action adds one marked block to a bash or zsh startup file once, never edits fish startup files, and a user who deleted the block is not opted back in.
Output is colored only on a terminal, NO_COLOR turns colors off, the color option overrides both, and log files and json output are never colored.
