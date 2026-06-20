#include "xlang/test.h"

#include "xlang/compiler.h"
#include "xlang/embedded_runtime.h"
#include "xlang/error.h"
#include "xlang/module.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <regex>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef XLANG_RUNTIME_DIR
#define XLANG_RUNTIME_DIR ""
#endif

namespace xlang {

namespace {

struct RunOutput {
    int exit_code{1};
    std::string stdout_text;
};

struct ParsedSummary {
    bool found{false};
    int passed{0};
    int failed{0};
};

void removeTree(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

RunOutput executeProgramCapture(const std::filesystem::path& executable) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        throw XlangError("failed to create output pipe");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        throw XlangError("failed to fork test process");
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        const std::string path = executable.string();
        execl(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    close(pipe_fds[1]);

    RunOutput result;
    char buffer[4096];
    FILE* read_stream = fdopen(pipe_fds[0], "r");
    if (read_stream == nullptr) {
        close(pipe_fds[0]);
        throw XlangError("failed to read test output");
    }

    while (true) {
        const std::size_t n = std::fread(buffer, 1, sizeof(buffer), read_stream);
        if (n == 0) {
            break;
        }
        result.stdout_text.append(buffer, n);
    }

    std::fclose(read_stream);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        throw XlangError("failed to wait for test process");
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = 1;
    }

    return result;
}

std::vector<std::filesystem::path> discoverTestFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(root)) {
        return files;
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (isTestFileName(entry.path())) {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

void ensureNoMain(const Program& program, const std::filesystem::path& file) {
    for (const Function& function : program.functions) {
        if (function.name == "main" && !function.body.statements.empty()) {
            throw XlangError("test file `" + file.string() +
                             "` must not define `main`; use `Test*` functions");
        }
    }
}

ParsedSummary parseTestSummary(const std::string& output) {
    static const std::regex pattern(R"(@xlang-test passed=(\d+) failed=(\d+))");
    std::smatch match;
    if (!std::regex_search(output, match, pattern)) {
        return {};
    }
    ParsedSummary summary;
    summary.found = true;
    summary.passed = std::stoi(match[1].str());
    summary.failed = std::stoi(match[2].str());
    return summary;
}

std::string formatTestOutput(const std::string& output) {
    std::ostringstream formatted;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("@xlang-test", 0) == 0) {
            continue;
        }
        formatted << line << '\n';
    }
    return formatted.str();
}

std::regex makePathPattern(const std::string& query) {
    try {
        return std::regex(query, std::regex::ECMAScript | std::regex::icase);
    } catch (const std::regex_error&) {
        static const std::regex special_chars(R"([.^$|()?*+[\]\\])");
        const std::string escaped = std::regex_replace(query, special_chars, std::string{"\\$&"});
        return std::regex(escaped, std::regex::ECMAScript | std::regex::icase);
    }
}

bool pathMatchesPattern(const std::filesystem::path& file,
                        const std::filesystem::path& search_root,
                        const std::regex& pattern) {
    if (std::regex_search(file.string(), pattern)) {
        return true;
    }
    if (std::regex_search(file.filename().string(), pattern)) {
        return true;
    }
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(file, search_root, ec);
    if (!ec && std::regex_search(relative.string(), pattern)) {
        return true;
    }
    return false;
}

std::vector<std::filesystem::path> resolveTestFiles(const std::filesystem::path& query) {
    const std::filesystem::path default_root = "test/xlang";
    const std::filesystem::path project_root = ".";

    if (query.empty()) {
        return discoverTestFiles(default_root);
    }

    std::error_code ec;
    if (std::filesystem::exists(query, ec) && std::filesystem::is_directory(query, ec)) {
        return discoverTestFiles(query);
    }

    std::string pattern_str = query.string();
    if (std::filesystem::exists(query, ec) && isTestFileName(query)) {
        pattern_str = query.filename().string();
    }

    const std::vector<std::filesystem::path> all = discoverTestFiles(project_root);
    const std::regex pattern = makePathPattern(pattern_str);

    std::vector<std::filesystem::path> matched;
    for (const std::filesystem::path& file : all) {
        if (pathMatchesPattern(file, project_root, pattern)) {
            matched.push_back(file);
        }
    }
    return matched;
}

Stmt makeCallExprStmt(const std::string& name, std::vector<std::unique_ptr<Expr>> args) {
    Stmt stmt;
    stmt.kind = Stmt::Kind::Expr;
    stmt.expr = Expr::makeCall(name, std::move(args), Span{});
    return stmt;
}

Stmt makeReturnCall(const std::string& name, std::vector<std::unique_ptr<Expr>> args) {
    Stmt stmt;
    stmt.kind = Stmt::Kind::Return;
    stmt.return_value = Expr::makeCall(name, std::move(args), Span{});
    return stmt;
}

}  // namespace

bool isTestFileName(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    const std::string suffix = ".test.xlang";
    if (filename.size() <= suffix.size()) {
        return false;
    }
    return filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isTestFunctionName(const std::string& name) {
    const std::string prefix = "Test";
    if (name.size() <= prefix.size()) {
        return false;
    }
    if (name.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const unsigned char next = static_cast<unsigned char>(name[prefix.size()]);
    return next >= 'A' && next <= 'Z';
}

std::vector<std::string> collectTestFunctions(const Program& program) {
    std::vector<std::string> tests;
    for (const Function& function : program.functions) {
        if (function.body.statements.empty()) {
            continue;
        }
        if (!isTestFunctionName(function.name)) {
            continue;
        }
        tests.push_back(function.name);
    }
    std::sort(tests.begin(), tests.end());
    return tests;
}

Program withTestHarness(const Program& program, const std::vector<std::string>& tests,
                        bool parallel, const std::string& file_label) {
    Program copy = cloneProgram(program);

    if (parallel) {
        for (const std::string& test_name : tests) {
            Function thunk;
            thunk.name = "__test_thunk_" + test_name;
            thunk.return_type = Type{TypeKind::Int32};

            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(Expr::makeString(test_name, Span{}));
            args.push_back(Expr::makeFunctionRef(test_name, Span{}));
            thunk.body.statements.push_back(makeReturnCall("test_run_one", std::move(args)));
            copy.functions.push_back(std::move(thunk));
        }
    }

    Function main_fn;
    main_fn.name = "main";
    main_fn.return_type = Type{TypeKind::Int32};

    {
        std::vector<std::unique_ptr<Expr>> init_args;
        init_args.push_back(Expr::makeInt(parallel ? 1 : 0, Span{}));
        init_args.push_back(Expr::makeString(file_label, Span{}));
        main_fn.body.statements.push_back(makeCallExprStmt("test_init_suite", std::move(init_args)));
    }

    if (parallel) {
        for (const std::string& test_name : tests) {
            const std::string thunk_name = "__test_thunk_" + test_name;
            auto bound = Expr::makeCall(thunk_name, std::vector<std::unique_ptr<Expr>>{}, Span{});
            std::vector<std::unique_ptr<Expr>> spawn_args;
            spawn_args.push_back(std::move(bound));
            main_fn.body.statements.push_back(makeCallExprStmt("spawn", std::move(spawn_args)));
        }
        main_fn.body.statements.push_back(
            makeCallExprStmt("wait_all", std::vector<std::unique_ptr<Expr>>{}));
    } else {
        for (const std::string& test_name : tests) {
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(Expr::makeString(test_name, Span{}));
            args.push_back(Expr::makeFunctionRef(test_name, Span{}));
            main_fn.body.statements.push_back(makeCallExprStmt("test_run_one", std::move(args)));
        }
    }

    main_fn.body.statements.push_back(
        makeReturnCall("test_finish_suite", std::vector<std::unique_ptr<Expr>>{}));
    copy.functions.push_back(std::move(main_fn));
    return copy;
}

std::vector<std::filesystem::path> materializeModuleSearchPaths(
    const std::filesystem::path& work_dir, bool skip_runtime) {
    std::vector<std::filesystem::path> paths;
    if (!skip_runtime) {
        const std::filesystem::path embedded_dir = work_dir / "embedded-runtime";
        (void)materializeEmbeddedRuntime(embedded_dir);
        paths.push_back(embedded_dir);
    }
    if (XLANG_RUNTIME_DIR[0] != '\0') {
        paths.emplace_back(XLANG_RUNTIME_DIR);
    }
    return paths;
}

void rejectTestFileForBuildRun(const std::filesystem::path& path) {
    if (isTestFileName(path)) {
        throw XlangError("test files cannot be built or run; use `xlank test`");
    }
}

TestSuiteResult runTestSuite(const TestOptions& options) {
    const std::vector<std::filesystem::path> files = resolveTestFiles(options.root);

    TestSuiteResult suite;
    suite.files_total = static_cast<int>(files.size());

    if (files.empty()) {
        std::cout << "No test files matched `" << options.root.string()
                  << "` (*.test.xlang)\n";
        suite.exit_code = 1;
        return suite;
    }

    for (const std::filesystem::path& file : files) {
        std::cout << "\n RUN  " << file.string();
        if (options.parallel) {
            std::cout << " (parallel)";
        }
        std::cout << "\n\n";

        const std::filesystem::path work_dir = makeBuildWorkDir();
        int file_passed = 0;
        int file_failed = 0;

        try {
            const std::vector<std::filesystem::path> module_search_paths =
                materializeModuleSearchPaths(work_dir, false);
            const Program loaded = loadProgram(file, module_search_paths);
            ensureNoMain(loaded, file);

            const std::vector<std::string> tests = collectTestFunctions(loaded);
            if (tests.empty()) {
                throw XlangError("no `Test*` functions found in `" + file.string() + "`");
            }

            const std::filesystem::path executable = work_dir / "test";

            CompileOptions compile_options;
            compile_options.input = file;
            compile_options.output = executable;
            compile_options.ir_output = work_dir / "test.ll";
            compile_options.clang = options.clang;
            compile_options.keep_ir = options.keep_artifacts;
            compile_options.build_kind = BuildKind::Exe;
            compile_options.work_dir = work_dir;
            compile_options.runtime_override = options.runtime_override;

            const Program program =
                withTestHarness(loaded, tests, options.parallel, file.string());
            const CompileResult compiled = compileProgram(program, compile_options);
            const RunOutput run = executeProgramCapture(compiled.executable);

            const std::string display_output = formatTestOutput(run.stdout_text);
            if (!display_output.empty()) {
                std::cout << display_output;
                if (display_output.back() != '\n') {
                    std::cout << '\n';
                }
            }

            const ParsedSummary summary = parseTestSummary(run.stdout_text);
            if (summary.found) {
                file_passed = summary.passed;
                file_failed = summary.failed;
                suite.tests_passed += summary.passed;
                suite.tests_failed += summary.failed;
            } else if (run.exit_code == 0) {
                file_passed = static_cast<int>(tests.size());
                suite.tests_passed += file_passed;
            } else {
                file_failed = static_cast<int>(tests.size());
                suite.tests_failed += file_failed;
            }

            if (run.exit_code == 0 && file_failed == 0) {
                suite.files_passed += 1;
                std::cout << "\n ✓ " << file.string() << " (" << file_passed << " tests)\n";
            } else {
                suite.files_failed += 1;
                std::cout << "\n ✗ " << file.string() << " (" << file_failed << " failed, "
                          << file_passed << " passed)\n";
            }
        } catch (const std::exception& error) {
            suite.files_failed += 1;
            std::cout << " ✗ " << file.string() << " [error: " << error.what() << "]\n";
        }

        if (!options.keep_artifacts) {
            removeTree(work_dir);
        }
    }

    std::cout << "\n Test Files  " << suite.files_passed << " passed";
    if (suite.files_failed > 0) {
        std::cout << " | " << suite.files_failed << " failed";
    }
    std::cout << " (" << suite.files_total << ")\n";

    std::cout << "      Tests  " << suite.tests_passed << " passed";
    if (suite.tests_failed > 0) {
        std::cout << " | " << suite.tests_failed << " failed";
    }
    std::cout << " (" << (suite.tests_passed + suite.tests_failed) << ")\n\n";

    suite.exit_code = (suite.files_failed > 0 || suite.tests_failed > 0) ? 1 : 0;
    return suite;
}

}  // namespace xlang
