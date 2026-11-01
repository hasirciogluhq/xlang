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

}  // namespace xlang::cli
