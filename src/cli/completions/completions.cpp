#include "cli/completions/completions.h"

#include "cli/app/style.h"

#include <CLI/CLI.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace xlang::cli {

namespace {

struct Flag {
    std::vector<std::string> spellings; ///< "--emit", "-O", ...
    std::string description;
    std::vector<std::string> values; ///< fixed value set; empty when free-form
};

struct Command {
    std::vector<std::string> names; ///< name first, then aliases; empty for the root
    std::string description;
    std::vector<Flag> flags;
    std::vector<std::string> positional_values; ///< fixed values of the first positional
};

std::string oneLine(std::string text) {
    std::ranges::replace(text, '\n', ' ');
    return text;
}

/// `TEXT:{auto,always,never}` -> {auto, always, never}.
std::vector<std::string> memberValues(const CLI::Option& option) {
    const std::string type = option.get_type_name();
    const std::size_t open = type.find('{');
    const std::size_t close = type.find('}', open == std::string::npos ? 0 : open);
    std::vector<std::string> values;
    if (open == std::string::npos || close == std::string::npos) {
        return values;
    }
    std::stringstream stream(type.substr(open + 1, close - open - 1));
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            values.push_back(item);
        }
    }
    return values;
}

Command collect(const CLI::App& app) {
    Command cmd;
    if (!app.get_name().empty() && app.get_parent() != nullptr) {
        cmd.names.push_back(app.get_name());
        for (const std::string& alias : app.get_aliases()) {
            cmd.names.push_back(alias);
        }
    }
    cmd.description = oneLine(app.get_description());
    for (const CLI::Option* option : app.get_options()) {
        if (option->get_positional()) {
            if (cmd.positional_values.empty()) {
                cmd.positional_values = memberValues(*option);
            }
            continue;
        }
        Flag flag;
        for (const std::string& l : option->get_lnames()) {
            flag.spellings.push_back("--" + l);
        }
        for (const std::string& s : option->get_snames()) {
            flag.spellings.push_back("-" + s);
        }
        if (flag.spellings.empty()) {
            continue;
        }
        flag.description = oneLine(option->get_description());
        flag.values = memberValues(*option);
        cmd.flags.push_back(std::move(flag));
    }
    return cmd;
}

std::string join(const std::vector<std::string>& items, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += (i == 0 ? "" : sep) + items[i];
    }
    return out;
}

std::vector<std::string> spellingsOf(const Command& cmd) {
    std::vector<std::string> all;
    for (const Flag& f : cmd.flags) {
        all.insert(all.end(), f.spellings.begin(), f.spellings.end());
    }
    return all;
}

/// flag spelling -> values, over the root and every subcommand.
std::map<std::string, std::vector<std::string>> valueTable(const std::vector<Command>& all) {
    std::map<std::string, std::vector<std::string>> table;
    for (const Command& cmd : all) {
        for (const Flag& f : cmd.flags) {
            if (f.values.empty()) {
                continue;
            }
            for (const std::string& s : f.spellings) {
                table[s] = f.values;
            }
        }
    }
    return table;
}

std::string bashScript(const Command& root, const std::vector<Command>& subs) {
    std::vector<Command> all{root};
    all.insert(all.end(), subs.begin(), subs.end());
    std::vector<std::string> sub_names;
    for (const Command& s : subs) {
        sub_names.insert(sub_names.end(), s.names.begin(), s.names.end());
    }

    std::string out =
        "# bash completion for xlang. Enable with:  source <(xl completions bash)\n"
        "_xlang_complete() {\n"
        "    local cur=\"${COMP_WORDS[COMP_CWORD]}\" prev=\"\" sub=\"\" i\n"
        "    [ \"$COMP_CWORD\" -gt 0 ] && prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
        "    for ((i = 1; i < COMP_CWORD; i++)); do\n"
        "        case \"${COMP_WORDS[i]}\" in\n"
        "            " +
        join(sub_names, "|") +
        ") sub=\"${COMP_WORDS[i]}\"; break ;;\n"
        "        esac\n"
        "    done\n"
        "    case \"$prev\" in\n";
    for (const auto& [flag, values] : valueTable(all)) {
        out += "        " + flag + ") COMPREPLY=($(compgen -W \"" + join(values, " ") +
               "\" -- \"$cur\")); return ;;\n";
    }
    out += "    esac\n"
           "    if [[ \"$cur\" == -* ]]; then\n"
           "        case \"$sub\" in\n";
    for (const Command& s : subs) {
        out += "            " + join(s.names, "|") + ") COMPREPLY=($(compgen -W \"" +
               join(spellingsOf(s), " ") + "\" -- \"$cur\")) ;;\n";
    }
    out += "            *) COMPREPLY=($(compgen -W \"" + join(spellingsOf(root), " ") +
           "\" -- \"$cur\")) ;;\n"
           "        esac\n"
           "        return\n"
           "    fi\n"
           "    case \"$sub\" in\n"
           "        \"\") COMPREPLY=($(compgen -W \"" +
           join(sub_names, " ") + "\" -- \"$cur\")); COMPREPLY+=($(compgen -f -- \"$cur\")) ;;\n";
    for (const Command& s : subs) {
        if (!s.positional_values.empty()) {
            out += "        " + join(s.names, "|") + ") COMPREPLY=($(compgen -W \"" +
                   join(s.positional_values, " ") + "\" -- \"$cur\")) ;;\n";
        }
    }
    out += "        *) COMPREPLY=($(compgen -f -- \"$cur\")) ;;\n"
           "    esac\n"
           "}\n"
           "complete -o filenames -o bashdefault -F _xlang_complete xlang xl\n";
    return out;
}

/// `name:description` for `_describe`; `:` in the name and `'` anywhere are escaped.
std::string zshEntry(const std::string& name, const std::string& description) {
    std::string out;
    const auto add = [&](const std::string& text, bool escape_colon) {
        for (const char c : text) {
            if (c == '\'') {
                out += "'\\''";
            } else if (c == ':' && escape_colon) {
                out += "\\:";
            } else {
                out += c;
            }
        }
    };
    add(name, true);
    out += ':';
    add(description, false);
    return "'" + out + "'";
}

std::string zshFlagArray(const Command& cmd) {
    std::string out;
    for (const Flag& f : cmd.flags) {
        for (const std::string& s : f.spellings) {
            out += " " + zshEntry(s, f.description);
        }
    }
    return out;
}

std::string zshScript(const Command& root, const std::vector<Command>& subs) {
    std::vector<Command> all{root};
    all.insert(all.end(), subs.begin(), subs.end());
    std::vector<std::string> sub_names;
    std::string sub_entries;
    for (const Command& s : subs) {
        for (const std::string& n : s.names) {
            sub_names.push_back(n);
            sub_entries += " " + zshEntry(n, s.description);
        }
    }

    std::string out =
        "# zsh completion for xlang. Enable with:  source <(xl completions zsh)\n"
        "if (( ! $+functions[compdef] )); then autoload -Uz compinit && compinit; fi\n"
        "_xlang() {\n"
        "    local sub=\"\" i\n"
        "    for ((i = 2; i < CURRENT; i++)); do\n"
        "        case \"$words[i]\" in\n"
        "            " +
        join(sub_names, "|") +
        ") sub=\"$words[i]\"; break ;;\n"
        "        esac\n"
        "    done\n"
        "    case \"$words[CURRENT-1]\" in\n";
    for (const auto& [flag, values] : valueTable(all)) {
        out += "        " + flag + ") compadd -- " + join(values, " ") + "; return ;;\n";
    }
    out += "    esac\n"
           "    local -a entries\n"
           "    if [[ \"$words[CURRENT]\" == -* ]]; then\n"
           "        case \"$sub\" in\n";
    for (const Command& s : subs) {
        out += "            " + join(s.names, "|") + ") entries=(" + zshFlagArray(s) + ") ;;\n";
    }
    out += "            *) entries=(" + zshFlagArray(root) +
           ") ;;\n"
           "        esac\n"
           "        _describe -t options 'option' entries\n"
           "        return\n"
           "    fi\n"
           "    case \"$sub\" in\n"
           "        \"\") entries=(" +
           sub_entries + "); _describe -t commands 'command' entries; _files ;;\n";
    for (const Command& s : subs) {
        if (!s.positional_values.empty()) {
            out += "        " + join(s.names, "|") + ") compadd -- " +
                   join(s.positional_values, " ") + " ;;\n";
        }
    }
    out += "        *) _files ;;\n"
           "    esac\n"
           "}\n"
           "compdef _xlang xlang xl\n";
    return out;
}

std::string fishQuote(const std::string& text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "'";
}

/// `-l long -s short` words for one flag.
std::string fishFlagWords(const Flag& f) {
    std::string out;
    for (const std::string& s : f.spellings) {
        out += s.starts_with("--") ? " -l " + s.substr(2) : " -s " + s.substr(1);
    }
    return out;
}

std::string fishScript(const Command& root, const std::vector<Command>& subs) {
    std::string out = "# fish completion for xlang. Enable with:  xl completions fish | source\n"
                      "for c in xlang xl\n";
    for (const Flag& f : root.flags) {
        out += "    complete -c $c -n __fish_use_subcommand" + fishFlagWords(f) + " -d " +
               fishQuote(f.description);
        if (!f.values.empty()) {
            out += " -x -a " + fishQuote(join(f.values, " "));
        }
        out += "\n";
    }
    for (const Command& s : subs) {
        for (const std::string& n : s.names) {
            out += "    complete -c $c -n __fish_use_subcommand -f -a " + n + " -d " +
                   fishQuote(s.description) + "\n";
        }
    }
    for (const Command& s : subs) {
        const std::string seen = "'__fish_seen_subcommand_from " + join(s.names, " ") + "'";
        for (const Flag& f : s.flags) {
            out += "    complete -c $c -n " + seen + fishFlagWords(f) + " -d " +
                   fishQuote(f.description);
            if (!f.values.empty()) {
                out += " -x -a " + fishQuote(join(f.values, " "));
            }
            out += "\n";
        }
        if (!s.positional_values.empty()) {
            out += "    complete -c $c -n " + seen + " -f -a " +
                   fishQuote(join(s.positional_values, " ")) + "\n";
        }
    }
    out += "end\n";
    return out;
}

} // namespace

std::string generateCompletions(CLI::App& app, const std::string& shell) {
    const Command root = collect(app);
    std::vector<Command> subs;
    for (CLI::App* sub : app.get_subcommands(nullptr)) {
        subs.push_back(collect(*sub));
    }
    if (shell == "bash") {
        return bashScript(root, subs);
    }
    if (shell == "zsh") {
        return zshScript(root, subs);
    }
    if (shell == "fish") {
        return fishScript(root, subs);
    }
    return {};
}

namespace {

namespace fs = std::filesystem;

constexpr const char* kBlockBegin = "# >>> xlang completion >>>";
constexpr const char* kBlockEnd = "# <<< xlang completion <<<";

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : fallback;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool writeFile(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    return static_cast<bool>(out);
}

/// `/home/me/x` -> `$HOME/x`, so the block in a startup file survives a moved home directory.
std::string withHomeVar(const fs::path& path, const std::string& home) {
    const std::string text = path.string();
    return text.starts_with(home + "/") ? "$HOME" + text.substr(home.size()) : text;
}

/// Appends the marked block that sources `script` unless the file already has it. Returns true
/// when the file was changed.
bool addSourceBlock(const fs::path& rc, const fs::path& script, const std::string& home) {
    const std::string current = fs::exists(rc) ? readFile(rc) : std::string{};
    if (current.find(kBlockBegin) != std::string::npos) {
        return false;
    }
    std::string block = current.empty() || current.back() == '\n' ? "" : "\n";
    block += std::string(current.empty() ? "" : "\n") + kBlockBegin + "\n" +
             "# Added by `xl completions install`; delete this block to opt out.\n" + "[ -f \"" +
             withHomeVar(script, home) + "\" ] && source \"" + withHomeVar(script, home) + "\"\n" +
             kBlockEnd + "\n";
    std::ofstream out(rc, std::ios::binary | std::ios::app);
    out << block;
    return static_cast<bool>(out);
}

} // namespace

int installCompletions(CLI::App& app, const CompletionInstallOptions& options) {
    const Palette c{options.color};
    const std::string home = envOr("HOME", "");
    if (home.empty()) {
        std::cerr << c.boldRed() << "error:" << c.reset() << " HOME is not set\n";
        return 1;
    }
    const std::string login = fs::path(envOr("SHELL", "")).filename().string();
    const fs::path data =
        fs::path(envOr("XDG_DATA_HOME", home + "/.local/share")) / "xlang" / "completions";
    const fs::path config = fs::path(envOr("XDG_CONFIG_HOME", home + "/.config"));
#ifdef __APPLE__
    // Terminal.app starts login shells, which read ~/.bash_profile rather than ~/.bashrc.
    const std::vector<fs::path> bash_rcs = {fs::path(home) / ".bash_profile",
                                            fs::path(home) / ".bashrc"};
#else
    const std::vector<fs::path> bash_rcs = {fs::path(home) / ".bashrc"};
#endif
    const fs::path zsh_rc = fs::path(envOr("ZDOTDIR", home)) / ".zshrc";

    bool any = false;
    const auto report = [&](const std::string& shell, const std::string& what) {
        any = true;
        std::cout << c.boldGreen() << "completion " << c.reset() << shell << ": " << what << '\n';
    };
    const auto fail = [&](const std::string& what) {
        std::cerr << c.boldRed() << "error:" << c.reset() << ' ' << what << '\n';
        return 1;
    };

    // fish: autoloaded from its completions directory, one file per command name.
    if (login == "fish" || fs::exists(config / "fish")) {
        const std::string script = generateCompletions(app, "fish");
        bool wrote = false;
        for (const char* name : {"xlang.fish", "xl.fish"}) {
            const fs::path file = config / "fish" / "completions" / name;
            if (options.refresh && !fs::exists(file)) {
                continue;
            }
            if (!writeFile(file, script)) {
                return fail("cannot write " + file.string());
            }
            wrote = true;
        }
        if (wrote) {
            report("fish", "installed in " + (config / "fish" / "completions").string());
        }
    }

    // bash and zsh: a script file plus a marked block in the startup file that sources it.
    struct Source {
        std::string shell;
        fs::path rc;
        bool wanted;
    };
    fs::path bash_rc = bash_rcs.front();
    for (const fs::path& candidate : bash_rcs) {
        if (fs::exists(candidate)) {
            bash_rc = candidate;
            break;
        }
    }
    const bool bash_wanted = login == "bash" || fs::exists(bash_rc);
    const bool zsh_wanted = login == "zsh" || fs::exists(zsh_rc);
    for (const Source& s :
         {Source{"bash", bash_rc, bash_wanted}, Source{"zsh", zsh_rc, zsh_wanted}}) {
        if (!s.wanted) {
            continue;
        }
        const fs::path script = data / ("xlang." + s.shell);
        if (options.refresh && !fs::exists(script)) {
            continue;
        }
        if (!writeFile(script, generateCompletions(app, s.shell))) {
            return fail("cannot write " + script.string());
        }
        bool edited = false;
        if (!options.refresh) {
            edited = addSourceBlock(s.rc, script, home);
        }
        report(s.shell,
               edited ? "installed, loaded from " + s.rc.string()
                      : (options.refresh ? "refreshed " + script.string()
                                         : "up to date (" + s.rc.string() + " already loads it)"));
    }

    if (!any && !options.refresh) {
        std::cout
            << "no bash, zsh or fish setup found; run `xl completions <bash|zsh|fish>` and "
               "source its output\n";
    } else if (any && !options.refresh) {
        std::cout << c.dim() << "open a new terminal to use tab completion" << c.reset() << '\n';
    }
    return 0;
}

} // namespace xlang::cli
