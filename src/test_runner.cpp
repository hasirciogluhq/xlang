#include "xlang/test.h"

#include "xlang/compiler.h"
#include "xlang/error.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <regex>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace xlang {

namespace {

struct ParsedTestCounts {
    int passed{0};
    int failed{0};
    bool found{false};
};

struct RunOutput {
    int exit_code{1};
    std::string stdout_text;
};

int runCommand(const std::string& command) {
    return std::system(command.c_str());
}

void removeTree(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

ParsedTestCounts parseTestMarker(const std::string& output) {
    static const std::regex pattern(R"(@xlang-test passed=(\d+) failed=(\d+))");
    std::smatch match;
    if (!std::regex_search(output, match, pattern)) {
        return {};
    }
    ParsedTestCounts counts;
    counts.passed = std::stoi(match[1].str());
    counts.failed = std::stoi(match[2].str());
    counts.found = true;
    return counts;
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
        std::cout.write(buffer, static_cast<std::streamsize>(n));
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

}  // namespace

bool isTestFileName(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    const std::string suffix = ".test.xlang";
    if (filename.size() <= suffix.size()) {
        return false;
    }
    return filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
}

TestSuiteResult runTestSuite(const TestOptions& options) {
    const std::vector<std::filesystem::path> files = discoverTestFiles(options.root);

    TestSuiteResult suite;
    suite.files_total = static_cast<int>(files.size());

    if (files.empty()) {
        std::cout << "No test files found in " << options.root << " (*.test.xlang)\n";
        suite.exit_code = 1;
        return suite;
    }

    for (const std::filesystem::path& file : files) {
        std::cout << "\n RUN  " << file.string() << "\n\n";

        const std::filesystem::path work_dir = makeBuildWorkDir();
        const std::filesystem::path executable = work_dir / "test-program";

        CompileOptions compile_options;
        compile_options.input = file;
        compile_options.output = executable;
        compile_options.ir_output = work_dir / "test-program.ll";
        compile_options.clang = options.clang;
        compile_options.keep_ir = options.keep_artifacts;
        compile_options.build_kind = BuildKind::Exe;
        compile_options.work_dir = work_dir;
        compile_options.runtime_override = options.runtime_override;

        try {
            const CompileResult compiled = compileFile(compile_options);
            const RunOutput run = executeProgramCapture(compiled.executable);
            const ParsedTestCounts counts = parseTestMarker(run.stdout_text);

            if (counts.found) {
                suite.tests_passed += counts.passed;
                suite.tests_failed += counts.failed;
            }

            if (run.exit_code == 0) {
                suite.files_passed += 1;
                std::cout << "\n ✓ " << file.string();
                if (counts.found) {
                    std::cout << " (" << counts.passed << " tests)";
                }
                std::cout << "\n";
            } else {
                suite.files_failed += 1;
                std::cout << "\n ✗ " << file.string();
                if (counts.found) {
                    std::cout << " (" << counts.failed << " failed)";
                } else {
                    std::cout << " (exit " << run.exit_code << ")";
                }
                std::cout << "\n";
            }
        } catch (const std::exception& error) {
            suite.files_failed += 1;
            std::cout << " ✗ " << file.string() << " [compile/run error: " << error.what()
                      << "]\n";
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
