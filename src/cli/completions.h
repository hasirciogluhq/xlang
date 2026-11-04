#pragma once

#include <string>

namespace CLI {
class App;
}

namespace xlang::cli {

/// Shell completion script for `shell` ("bash", "zsh" or "fish"), generated from the command tree
/// of `app`: subcommands (and their aliases), every flag, and the values of options that take a
/// fixed set (`--color`, `--emit`, ...). Subcommands that take a fixed positional (`completions`)
/// complete its values; every other positional completes file names. Empty for an unknown shell.
std::string generateCompletions(CLI::App& app, const std::string& shell);

struct CompletionInstallOptions {
    /// Only rewrite completion files that already exist (what `self-update` does); never add the
    /// startup-file block, so a user who removed it is not opted back in.
    bool refresh{false};
    bool color{false};
};

/// `xlang completions install`: writes the completion scripts for the shells in use and makes each
/// shell load them, with no manual step. Idempotent.
///   fish  `$XDG_CONFIG_HOME/fish/completions/{xlang,xl}.fish` (fish autoloads it; no startup edit)
///   bash  `$XDG_DATA_HOME/xlang/completions/xlang.bash` + a marked block in `~/.bash_profile`
///         (macOS) or `~/.bashrc` (Linux)
///   zsh   `$XDG_DATA_HOME/xlang/completions/xlang.zsh` + a marked block in `$ZDOTDIR/.zshrc`
/// A shell is set up when it is the login shell (`$SHELL`) or its startup file / config directory
/// already exists. Returns the process exit code.
int installCompletions(CLI::App& app, const CompletionInstallOptions& options);

}  // namespace xlang::cli
