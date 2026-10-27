#include "test_runner.h"
#include "xlang/codegen/codegen.h"
#include "xlang/codegen/link.h"
#include "xlang/lang/ast.h"
#include "xlang/lang/config.h"
#include "xlang/lang/frontend.h"
#include "xlang/sema/compilation.h"
#include "xlang/sema/sema.h"
#include "xlang/util/diagnostics.h"
#include "xlang/util/error.h"
#include "xlang/util/source.h"

#include <CLI/CLI.hpp>

#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

/// `@library("x")` imports of the program, for the Windows linker step.
void collectImports(const xlang::hir::Module& module, xlang::codegen::LinkRequest& req) {
    for (const auto& f : module.functions) {
        if (f.declared && !f.import_library.empty()) {
            req.imports.emplace_back(f.import_library, f.symbol);
        }
    }
}

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
    cmd->add_option(
        "--allow", o.allow, "Silence warning CODE (repeatable); `-Wno-CODE` is the same");
    cmd->add_option("--deny", o.deny, "Turn warning CODE into an error (repeatable)");
    cmd->add_flag("--warn-all", o.warn_all, "Enable every warning, ignoring --allow and @allow");
    cmd->add_flag("--allow-all", o.allow_all, "Silence every warning");
    cmd->add_option("--context", o.context, "Context lines around marks for errors and warnings")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--context-errors", o.context_errors, "Context lines for errors (default 10)");
    cmd->add_option(
        "--context-warnings", o.context_warnings, "Context lines for warnings (default 2)");
    cmd->add_option(
        "--diag-log", o.log_path, "Write every diagnostic (suppressed ones included) to FILE");
    cmd->add_option("--diag-format", o.format, "Diagnostic format: text|json")
        ->check(CLI::IsMember({"text", "json"}));
    cmd->add_option("--diag-log-level", o.log_level, "Log level: all|warn|error")
        ->check(CLI::IsMember({"all", "warn", "error"}));
}

void addBuildOptions(CLI::App* cmd, BuildCliOptions& o) {
    cmd->add_option("-D,--define", o.defines, "NAME or NAME=VALUE (repeatable)");
    cmd->add_option("-U,--undef", o.undefs, "Remove a user definition (repeatable)");
    cmd->add_option("--target", o.target, "Target triple, e.g. aarch64-apple-darwin");
    cmd->add_option("-O", o.opt, "Optimization level 0..3 (`-O2`)")
        ->check(CLI::IsMember({"0", "1", "2", "3"}));
    cmd->add_flag("-g", o.debug, "Debug info (`debug` = true)");
    cmd->add_option("--cpu", o.cpu, "Target CPU (default generic)");
    cmd->add_flag("--print-cfg", o.print_cfg, "Print every resolved definition");
    cmd->add_option("--mangle", o.mangle, "xl|none")->check(CLI::IsMember({"xl", "none"}));
    cmd->add_option("--safety", o.safety, "checked|unchecked")
        ->check(CLI::IsMember({"checked", "unchecked"}));
    cmd->add_flag("--no-std", o.no_std, "Do not load the standard library");
    cmd->add_option("--std", o.std_path, "Use another standard library");
    cmd->add_option("--std-features",
                    o.std_features,
                    "Standard library features, e.g. sched,-default-allocator")
        ->delimiter(',');
    cmd->add_option("--layout-scheme", o.layout_scheme, "Pin the default layout scheme (v1)");
    cmd->add_option(
        "--layout-history", o.layout_history, "Layout history file (default xl.layout)");
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
        config.defines[name] =
            xlang::ConfigValue::parse(eq == std::string::npos ? "" : def.substr(eq + 1));
    }
    for (const std::string& name : o.undefs) {
        if (config.defines.erase(name) == 0 && config.lookup(name)) {
            throw xlang::XlangError("`" + name +
                                    "` is a builtin definition and cannot be undefined");
        }
    }
    return config;
}

/// The build options typed on a subcommand, re-spelled for a `xlang build` child process.
std::vector<std::string> forwardBuildArgs(const CLI::App* cmd, const BuildCliOptions& b) {
    std::vector<std::string> a;
    for (const std::string& d : b.defines) {
        a.push_back("-D");
        a.push_back(d);
    }
    for (const std::string& u : b.undefs) {
        a.push_back("-U");
        a.push_back(u);
    }
    if (!b.target.empty()) {
        a.push_back("--target=" + b.target);
    }
    if (!b.opt.empty()) {
        a.push_back("-O" + b.opt);
    }
    if (b.debug) {
        a.push_back("-g");
    }
    if (cmd->count("--cpu") != 0) {
        a.push_back("--cpu=" + b.cpu);
    }
    if (cmd->count("--mangle") != 0) {
        a.push_back("--mangle=" + b.mangle);
    }
    if (cmd->count("--safety") != 0) {
        a.push_back("--safety=" + b.safety);
    }
    if (b.no_std) {
        a.push_back("--no-std");
    }
    if (!b.std_path.empty()) {
        a.push_back("--std=" + b.std_path);
    }
    if (!b.std_features.empty()) {
        std::string joined;
        for (const std::string& f : b.std_features) {
            joined += (joined.empty() ? "" : ",") + f;
        }
        a.push_back("--std-features=" + joined);
    }
    return a;
}

void printConfig(const xlang::BuildConfig& config) {
    std::cout << "target = \"" << config.target.triple << "\"\n";
    for (const auto& [name, value] : config.resolved()) {
        std::cout << name << " = " << value.toString() << '\n';
    }
    std::cout << "cpu = \"" << config.cpu << "\"\n"
              << "mangle = \"" << config.mangle << "\"\n"
              << "safety = \"" << config.safety << "\"\n"
              << "std = "
              << (config.no_std ? "none" : (config.std_path.empty() ? "embedded" : config.std_path))
              << '\n';
}

} // namespace

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
    CLI::App* parse =
        app.add_subcommand("parse", "Preprocess, expand macros and parse; print the AST summary");
    parse->add_option("file", input, "Source file (.xl)")->required();
    parse->add_flag("--ast", ast, "Print the full tree instead of the item summary");
    addBuildOptions(parse, build_options);
    addDiagOptions(parse, diag_options);

    std::filesystem::path check_input;
    CLI::App* check = app.add_subcommand(
        "check", "Parse and run the module checks (types, enums, layouts, staticAssert)");
    check->add_option("file", check_input, "Source file (.xl)")->required();
    bool dump_hir = false;
    check->add_flag("--hir", dump_hir, "Print the typed HIR after a successful check");
    addBuildOptions(check, build_options);
    addDiagOptions(check, diag_options);

    std::filesystem::path build_input;
    std::filesystem::path build_output;
    std::string emit{"exe"};
    CLI::App* build = app.add_subcommand("build", "Compile a program (default --emit=exe)");
    build->add_option("file", build_input, "Source file (.xl)")->required();
    build->add_option("-o,--output", build_output, "Output file");
    bool incremental = false;
    std::filesystem::path cache_dir;
    build->add_flag("--incremental",
                    incremental,
                    "One object per module, reused from the cache when unchanged");
    build->add_option("--cache-dir",
                      cache_dir,
                      "Object cache directory (default: .xlang-cache next to the output)");
    build->add_option("--emit", emit, "exe|obj|ir|bc|header|static|shared")
        ->check(CLI::IsMember({"exe", "obj", "ir", "bc", "header", "static", "shared"}));
    addBuildOptions(build, build_options);
    addDiagOptions(build, diag_options);

    std::filesystem::path layout_input;
    std::string layout_type;
    bool layout_record = false;
    CLI::App* layout =
        app.add_subcommand("layout", "Print the layout (offset/size/align) of structs and classes");
    layout->add_option("file", layout_input, "Source file (.xl)")->required();
    layout->add_option("type", layout_type, "Only this type");
    layout->add_flag(
        "--layout-record", layout_record, "Write the printed layouts to the history file");
    addBuildOptions(layout, build_options);
    addDiagOptions(layout, diag_options);

    xlang::cli::TestRunOptions test_options;
    bool test_no_color = false;
    BuildCliOptions test_build_options;
    CLI::App* test = app.add_subcommand(
        "test",
        "Build and run test files (`*.test.xl`); patterns: file, directory, glob (`**`, `*`, `?`), "
        "`re:<regex>` or a path substring");
    test->add_option("patterns", test_options.patterns, "Tests to run (default: **/*.test.xl)");
    test->add_option("-x,--exclude", test_options.excludes, "Skip files matching this pattern (repeatable)");
    test->add_flag("--list", test_options.list_only, "Only print the matching test files");
    test->add_flag("--no-color", test_no_color, "Plain output");
    addBuildOptions(test, test_build_options);

    std::filesystem::path run_input;
    BuildCliOptions run_build_options;
    CLI::App* run = app.add_subcommand("run", "Build a program into a temporary file, run it and exit with its status");
    run->add_option("file", run_input, "Source file (.xl)")->required();
    addBuildOptions(run, run_build_options);

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

        if (run->parsed()) {
            return xlang::cli::runProgram(xlang::cli::selfExecutable(argv[0]),
                                          run_input.string(),
                                          forwardBuildArgs(run, run_build_options));
        }

        if (test->parsed()) {
            const char* no_color_env = std::getenv("NO_COLOR");
            test_options.color =
                !test_no_color && ::isatty(1) != 0 && (no_color_env == nullptr || *no_color_env == '\0');
            test_options.build_args = forwardBuildArgs(test, test_build_options);
            return xlang::cli::runTests(test_options, xlang::cli::selfExecutable(argv[0]));
        }

        CLI::App* active = parse->parsed()   ? parse
                           : check->parsed() ? check
                           : build->parsed() ? build
                                             : (layout->parsed() ? layout : nullptr);
        if (active == nullptr) {
            std::cout << app.help();
            return 0;
        }
        const std::filesystem::path& path = parse->parsed()   ? input
                                            : check->parsed() ? check_input
                                            : build->parsed() ? build_input
                                                              : layout_input;

        xlang::BuildConfig config = toBuildConfig(build_options);
        if (build_options.print_cfg) {
            printConfig(config);
            return 0;
        }

        xlang::SourceManager sources;
        xlang::DiagnosticEngine diagnostics(sources, std::cerr);
        diagnostics.configure(toEngineOptions(active, diag_options));

        if (parse->parsed()) {
            const xlang::FileId file = sources.loadFile(path);
            const xlang::Program program = xlang::parseSource(sources, file, config, diagnostics);
            diagnostics.flush();
            if (diagnostics.hasErrors()) {
                std::cerr << diagnostics.errorCount() << " error(s), " << diagnostics.warningCount()
                          << " warning(s)\n";
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

        // Whole program: root + imports + stdlib (unless --no-std), then analysis and lowering.
        xlang::Compilation compilation(sources, config, diagnostics, &history);
        if (!config.std_path.empty()) {
            compilation.setStdRoot(config.std_path); // otherwise the embedded stdlib is used
        }
        compilation.loadRoot(path);
        compilation.finish();
        if (!diagnostics.hasErrors()) {
            compilation.analyzeAll();
        }
        xlang::Sema& sema = *compilation.rootSema();

        if (check->parsed() && !diagnostics.hasErrors()) {
            xlang::hir::Module module = compilation.lowerAll();
            if (dump_hir && !diagnostics.hasErrors()) {
                diagnostics.flush();
                xlang::hir::dumpModule(std::cout, module);
            }
        }

        if (build->parsed() && !diagnostics.hasErrors()) {
            xlang::hir::Module module = compilation.lowerAll();
            if (!diagnostics.hasErrors()) {
                xlang::Sema& root = sema;
                const std::string stem = path.stem().string();
                const auto layoutOf = [&root](const xlang::Type& t) {
                    return root.layoutOf(t, xlang::Span{});
                };
                const bool windows = config.target.os == "windows";
                std::filesystem::path out = build_output;
                if (out.empty()) {
                    const std::filesystem::path dir = path.parent_path();
                    if (emit == "exe") {
                        out = dir / (stem + (windows ? ".exe" : ""));
                    } else if (emit == "obj") {
                        out = dir / (stem + (windows ? ".obj" : ".o"));
                    } else if (emit == "ir") {
                        out = dir / (stem + ".ll");
                    } else if (emit == "bc") {
                        out = dir / (stem + ".bc");
                    } else if (emit == "header") {
                        out = dir / (stem + ".h");
                    } else if (emit == "static") {
                        out = dir / (windows ? stem + ".lib" : "lib" + stem + ".a");
                    } else {
                        const char* ext =
                            windows ? ".dll" : (config.target.os == "macos" ? ".dylib" : ".so");
                        out = dir / ((windows ? "" : "lib") + stem + ext);
                    }
                }
                if (emit == "header") {
                    std::string guard = "XLANG_" + stem + "_H";
                    for (char& ch : guard) {
                        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                    }
                    std::ofstream h(out);
                    h << xlang::codegen::generateHeader(module, layoutOf, guard);
                } else if (incremental && (emit == "exe" || emit == "static" || emit == "shared")) {
                    // Plan 8.1/8.2: one object per module, keyed by the content hash of the module,
                    // its dependencies and the build configuration.
                    const std::filesystem::path dir =
                        cache_dir.empty() ? out.parent_path() / ".xlang-cache" : cache_dir;
                    std::filesystem::create_directories(dir);
                    std::map<const xlang::ModuleInfo*, std::uint64_t> base;
                    const auto mix = [](std::uint64_t h, const std::string& s) {
                        for (const unsigned char ch : s) {
                            h ^= ch;
                            h *= 1099511628211ULL;
                        }
                        h ^= 0xff;
                        h *= 1099511628211ULL;
                        return h;
                    };
                    std::string cfg =
                        "xlang-0.1|" + config.target.triple + "|" + config.target.os + "|" +
                        config.target.arch + "|O" + std::to_string(config.opt_level) + "|" +
                        (config.debug ? "g" : "-") + "|" + config.cpu + "|" + config.mangle + "|" +
                        config.safety + "|" + (config.no_std ? "nostd" : "std") + "|" +
                        config.layout_scheme + "|" + emit;
                    for (const std::string& f : config.std_features) {
                        cfg += "|f:" + f;
                    }
                    for (const auto& [k, v] : config.defines) {
                        cfg += "|D:" + k + "=" + v.toString();
                    }
                    const std::function<std::uint64_t(const xlang::ModuleInfo&)> baseHash =
                        [&](const xlang::ModuleInfo& m) -> std::uint64_t {
                        if (const auto it = base.find(&m); it != base.end()) {
                            return it->second;
                        }
                        base[&m] = 0; // import cycles terminate here
                        std::uint64_t h = mix(14695981039346656037ULL, cfg);
                        h = mix(h, m.key);
                        h = mix(h, sources.file(m.file).text());
                        for (const xlang::ModuleInfo* d : m.deps) {
                            h = mix(h, std::to_string(baseHash(*d)));
                        }
                        return base[&m] = h;
                    };
                    // Registered names (panic, allocator, ...) are visible everywhere: they salt
                    // every module hash.
                    std::string salt;
                    for (const xlang::ModuleInfo* m : compilation.order()) {
                        for (const xlang::Item& item : m->program.items) {
                            if (item.modifiers.registered) {
                                salt += std::to_string(baseHash(*m)) + ";";
                                break;
                            }
                        }
                    }
                    std::vector<std::filesystem::path> objects;
                    for (xlang::ModuleInfo* m : compilation.order()) {
                        bool has_code = m->key.empty();
                        for (const auto& f : module.functions) {
                            has_code = has_code || f.module == m->key;
                        }
                        for (const auto& g : module.globals) {
                            has_code = has_code || g.module == m->key;
                        }
                        if (!has_code) {
                            continue;
                        }
                        const std::uint64_t h = mix(baseHash(*m), salt);
                        std::string name = m->key.empty() ? stem : m->key;
                        for (char& ch : name) {
                            if (ch == '/') {
                                ch = '.';
                            }
                        }
                        const std::filesystem::path obj =
                            dir / std::format("{}-{:016x}{}", name, h, windows ? ".obj" : ".o");
                        if (!std::filesystem::exists(obj)) {
                            xlang::codegen::CodegenInput cg{module,
                                                            config,
                                                            sources,
                                                            diagnostics,
                                                            layoutOf,
                                                            name,
                                                            emit == "exe"};
                            cg.only_module = m->key;
                            auto artifact = xlang::codegen::generate(cg);
                            if (!artifact) {
                                // The back end failed without reporting why: say so, so the
                                // build does not end with no error at all.
                                if (!diagnostics.hasErrors()) {
                                    diagnostics.error(
                                        "E0714",
                                        xlang::Span{},
                                        std::format("code generation for module `{}` failed "
                                                    "without a diagnostic",
                                                    name),
                                        "internal error: the back end returned no object");
                                }
                                break;
                            }
                            artifact->optimize(config.opt_level);
                            artifact->writeObject(obj);
                        }
                        objects.push_back(obj);
                    }
                    if (!diagnostics.hasErrors()) {
                        if (emit == "static") {
                            xlang::codegen::archive(objects, out, config);
                        } else {
                            xlang::codegen::LinkRequest req;
                            req.objects = objects;
                            req.output = out;
                            req.kind = emit == "shared" ? xlang::codegen::LinkKind::Shared
                                                        : xlang::codegen::LinkKind::Executable;
                            req.config = &config;
                            collectImports(module, req);
                            xlang::codegen::link(req);
                        }
                    }
                } else {
                    xlang::codegen::CodegenInput cg{
                        module, config, sources, diagnostics, layoutOf, stem, emit == "exe"};
                    auto artifact = xlang::codegen::generate(cg);
                    if (artifact) {
                        artifact->optimize(config.opt_level);
                        if (emit == "ir") {
                            artifact->writeIR(out);
                        } else if (emit == "bc") {
                            artifact->writeBitcode(out);
                        } else if (emit == "obj") {
                            artifact->writeObject(out);
                        } else {
                            const std::filesystem::path obj =
                                std::filesystem::temp_directory_path() /
                                (stem + "." + std::to_string(::getpid()) +
                                 (windows ? ".obj" : ".o"));
                            artifact->writeObject(obj);
                            try {
                                if (emit == "static") {
                                    xlang::codegen::archive({obj}, out, config);
                                } else {
                                    xlang::codegen::LinkRequest req;
                                    req.objects = {obj};
                                    req.output = out;
                                    req.kind = emit == "shared"
                                                   ? xlang::codegen::LinkKind::Shared
                                                   : xlang::codegen::LinkKind::Executable;
                                    req.config = &config;
                                    collectImports(module, req);
                                    xlang::codegen::link(req);
                                }
                            } catch (...) {
                                std::filesystem::remove(obj);
                                throw;
                            }
                            std::filesystem::remove(obj);
                        }
                    }
                }
            }
        }

        if (layout->parsed() && !diagnostics.hasErrors()) {
            const std::vector<std::string> names =
                layout_type.empty() ? sema.aggregateNames() : std::vector<std::string>{layout_type};
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
            std::cerr << diagnostics.errorCount() << " error(s), " << diagnostics.warningCount()
                      << " warning(s)\n";
            return 1;
        }
        return 0;
    } catch (const xlang::XlangError& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
