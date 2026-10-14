#include "xlang/lang/ast.h"
#include "xlang/lang/config.h"
#include "xlang/lang/frontend.h"
#include "xlang/sema/sema.h"
#include "xlang/util/diagnostics.h"
#include "xlang/util/error.h"
#include "xlang/util/source.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct DiagCliOptions {
    std::vector<std::string> allow;
    std::vector<std::string> deny;
    bool warn_all{false};
    bool allow_all{false};
    std::size_t context{0};
    std::size_t context_errors{10};
    std::size_t context_warnings{2};
    std::string log_path;
    std::string format{"text"};
    std::string log_level{"all"};
};

struct BuildCliOptions {
    std::vector<std::string> defines;
    std::vector<std::string> undefs;
    std::string target;
    std::string opt;
    bool debug{false};
    std::string cpu{"generic"};
    bool print_cfg{false};
    std::string mangle{"xl"};
    std::string safety{"checked"};
    bool no_std{false};
    std::string std_path;
    std::vector<std::string> std_features;
    std::string layout_scheme;
    std::string layout_history;
};

void addDiagOptions(CLI::App* cmd, DiagCliOptions& o) {
    cmd->add_option("--allow", o.allow, "Silence warning CODE (repeatable); `-Wno-CODE` is the same");
    cmd->add_option("--deny", o.deny, "Turn warning CODE into an error (repeatable)");
    cmd->add_flag("--warn-all", o.warn_all, "Enable every warning, ignoring --allow and @allow");
    cmd->add_flag("--allow-all", o.allow_all, "Silence every warning");
    cmd->add_option("--context", o.context, "Context lines around marks for errors and warnings")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--context-errors", o.context_errors, "Context lines for errors (default 10)");
    cmd->add_option("--context-warnings", o.context_warnings, "Context lines for warnings (default 2)");
    cmd->add_option("--diag-log", o.log_path, "Write every diagnostic (suppressed ones included) to FILE");
    cmd->add_option("--diag-format", o.format, "Diagnostic format: text|json")
        ->check(CLI::IsMember({"text", "json"}));
    cmd->add_option("--diag-log-level", o.log_level, "Log level: all|warn|error")
        ->check(CLI::IsMember({"all", "warn", "error"}));
}

void addBuildOptions(CLI::App* cmd, BuildCliOptions& o) {
    cmd->add_option("-D,--define", o.defines, "NAME or NAME=VALUE (repeatable)");
    cmd->add_option("-U,--undef", o.undefs, "Remove a user definition (repeatable)");
    cmd->add_option("--target", o.target, "Target triple, e.g. aarch64-apple-darwin");
    cmd->add_option("-O", o.opt, "Optimization level 0..3 (`-O2`)")->check(CLI::IsMember({"0", "1", "2", "3"}));
    cmd->add_flag("-g", o.debug, "Debug info (`debug` = true)");
    cmd->add_option("--cpu", o.cpu, "Target CPU (default generic)");
    cmd->add_flag("--print-cfg", o.print_cfg, "Print every resolved definition");
    cmd->add_option("--mangle", o.mangle, "xl|none")->check(CLI::IsMember({"xl", "none"}));
    cmd->add_option("--safety", o.safety, "checked|unchecked")->check(CLI::IsMember({"checked", "unchecked"}));
    cmd->add_flag("--no-std", o.no_std, "Do not load the standard library");
    cmd->add_option("--std", o.std_path, "Use another standard library");
    cmd->add_option("--std-features", o.std_features, "Standard library features, e.g. -default-allocator");
    cmd->add_option("--layout-scheme", o.layout_scheme, "Pin the default layout scheme (v1)");
    cmd->add_option("--layout-history", o.layout_history, "Layout history file (default xl.layout)");
}

xlang::DiagnosticOptions toEngineOptions(const CLI::App* cmd, const DiagCliOptions& o) {
    xlang::DiagnosticOptions out;
    out.allow = o.allow;
    out.deny = o.deny;
    out.warn_all = o.warn_all;
    out.allow_all = o.allow_all;
    out.context_errors = o.context_errors;
    out.context_warnings = o.context_warnings;
    if (cmd->count("--context") != 0) {
        out.context_errors = o.context;
        out.context_warnings = o.context;
    }
    if (cmd->count("--context-errors") != 0) {
        out.context_errors = o.context_errors;
    }
    if (cmd->count("--context-warnings") != 0) {
        out.context_warnings = o.context_warnings;
    }
    out.log_path = o.log_path;
    out.format = o.format == "json" ? xlang::DiagFormat::Json : xlang::DiagFormat::Text;
    out.log_level = o.log_level == "error"  ? xlang::DiagLogLevel::Error
                    : o.log_level == "warn" ? xlang::DiagLogLevel::Warn
                                            : xlang::DiagLogLevel::All;
    return out;
}

xlang::BuildConfig toBuildConfig(const BuildCliOptions& o) {
    xlang::BuildConfig config;
    if (!o.target.empty()) {
        auto target = xlang::parseTargetTriple(o.target);
        if (!target) {
            throw xlang::XlangError("unsupported target `" + o.target +
                                    "` (x86_64/aarch64 on linux/macos/windows)");
        }
        config.target = std::move(*target);
    }
    if (!o.opt.empty()) {
        config.opt_level = std::stoi(o.opt);
    }
    config.debug = o.debug;
    config.cpu = o.cpu;
    config.mangle = o.mangle;
    config.safety = o.safety;
    config.no_std = o.no_std;
    config.std_path = o.std_path;
    config.std_features = o.std_features;
    config.layout_scheme = o.layout_scheme;
    config.layout_history = o.layout_history;
    for (const std::string& def : o.defines) {
        const std::size_t eq = def.find('=');
        const std::string name = def.substr(0, eq);
        if (name.empty()) {
            throw xlang::XlangError("empty name in -D");
        }
        config.defines[name] = xlang::ConfigValue::parse(eq == std::string::npos ? "" : def.substr(eq + 1));
    }
    for (const std::string& name : o.undefs) {
        if (config.defines.erase(name) == 0 && config.lookup(name)) {
            throw xlang::XlangError("`" + name + "` is a builtin definition and cannot be undefined");
        }
    }
    return config;
}

void printConfig(const xlang::BuildConfig& config) {
    std::cout << "target = \"" << config.target.triple << "\"\n";
    for (const auto& [name, value] : config.resolved()) {
        std::cout << name << " = " << value.toString() << '\n';
    }
    std::cout << "cpu = \"" << config.cpu << "\"\n"
              << "mangle = \"" << config.mangle << "\"\n"
              << "safety = \"" << config.safety << "\"\n"
              << "std = " << (config.no_std ? "none" : (config.std_path.empty() ? "embedded" : config.std_path))
              << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    // `-Wno-CODE` is spelled like a single flag; rewrite it to `--allow=CODE` for CLI11.
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (i > 0 && arg.starts_with("-Wno-") && arg.size() > 5) {
            arg = "--allow=" + arg.substr(5);
        }
        args.push_back(std::move(arg));
    }
    std::vector<char*> raw;
    for (std::string& a : args) {
        raw.push_back(a.data());
    }

    CLI::App app{"xlang compiler (frontend)"};
    app.require_subcommand(0, 1);

    std::string explain_code;
    app.add_option("--explain", explain_code, "Print the long explanation of a diagnostic code");

    DiagCliOptions diag_options;
    BuildCliOptions build_options;

    std::filesystem::path input;
    bool ast = false;
    CLI::App* parse = app.add_subcommand("parse", "Preprocess, expand macros and parse; print the AST summary");
    parse->add_option("file", input, "Source file (.xl)")->required();
    parse->add_flag("--ast", ast, "Print the full tree instead of the item summary");
    addBuildOptions(parse, build_options);
    addDiagOptions(parse, diag_options);

    std::filesystem::path check_input;
    CLI::App* check = app.add_subcommand("check", "Parse and run the module checks (types, enums, layouts, staticAssert)");
    check->add_option("file", check_input, "Source file (.xl)")->required();
    bool dump_hir = false;
    check->add_flag("--hir", dump_hir, "Print the typed HIR after a successful check");
    addBuildOptions(check, build_options);
    addDiagOptions(check, diag_options);

    std::filesystem::path layout_input;
    std::string layout_type;
    bool layout_record = false;
    CLI::App* layout = app.add_subcommand("layout", "Print the layout (offset/size/align) of structs and classes");
    layout->add_option("file", layout_input, "Source file (.xl)")->required();
    layout->add_option("type", layout_type, "Only this type");
    layout->add_flag("--layout-record", layout_record, "Write the printed layouts to the history file");
    addBuildOptions(layout, build_options);
    addDiagOptions(layout, diag_options);

    try {
        app.parse(static_cast<int>(raw.size()), raw.data());
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    try {
        if (!explain_code.empty()) {
            const std::string text = xlang::DiagnosticEngine::explain(explain_code);
            if (text.empty()) {
                std::cerr << "error: unknown diagnostic code `" << explain_code << "`\n";
                return 1;
            }
            std::cout << text;
            return 0;
        }

        CLI::App* active = parse->parsed() ? parse : (check->parsed() ? check : (layout->parsed() ? layout : nullptr));
        if (active == nullptr) {
            std::cout << app.help();
            return 0;
        }
        const std::filesystem::path& path = parse->parsed() ? input : (check->parsed() ? check_input : layout_input);

        xlang::BuildConfig config = toBuildConfig(build_options);
        if (build_options.print_cfg) {
            printConfig(config);
            return 0;
        }

        xlang::SourceManager sources;
        const xlang::FileId file = sources.loadFile(path);
        xlang::DiagnosticEngine diagnostics(sources, std::cerr);
        diagnostics.configure(toEngineOptions(active, diag_options));

        const xlang::Program program = xlang::parseSource(sources, file, config, diagnostics);

        if (parse->parsed()) {
            diagnostics.flush();
            if (diagnostics.hasErrors()) {
                std::cerr << diagnostics.errorCount() << " error(s), " << diagnostics.warningCount() << " warning(s)\n";
                return 1;
            }
            xlang::dumpProgram(std::cout, program, !ast);
            return 0;
        }

        std::string history_path = config.layout_history;
        if (history_path.empty()) {
            history_path = (path.parent_path() / "xl.layout").string();
        }
        xlang::LayoutHistory history = xlang::LayoutHistory::load(history_path);

        xlang::Sema sema(program, config, diagnostics, &history);
        if (!diagnostics.hasErrors()) {
            sema.analyze();
        }

        if (check->parsed() && !diagnostics.hasErrors()) {
            xlang::hir::Module module = sema.lower();
            if (dump_hir && !diagnostics.hasErrors()) {
                diagnostics.flush();
                xlang::hir::dumpModule(std::cout, module);
            }
        }

        if (layout->parsed() && !diagnostics.hasErrors()) {
            const std::vector<std::string> names = layout_type.empty()
                                                       ? sema.aggregateNames()
                                                       : std::vector<std::string>{layout_type};
            for (const std::string& name : names) {
                auto l = sema.layoutOfName(name, xlang::Span{});
                if (!l) {
                    continue;
                }
                xlang::LayoutRecord record{name, *l};
                std::cout << xlang::formatLayoutRecord(record);
                if (layout_record) {
                    history.put(std::move(record));
                }
            }
            if (layout_record && !diagnostics.hasErrors()) {
                history.save(history_path);
            }
        }

        diagnostics.flush();
        if (diagnostics.hasErrors()) {
            std::cerr << diagnostics.errorCount() << " error(s), " << diagnostics.warningCount() << " warning(s)\n";
            return 1;
        }
        return 0;
    } catch (const xlang::XlangError& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
