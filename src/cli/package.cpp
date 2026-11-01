#include "package.h"

#include "style.h"
#include "xlang/pkg/error.h"
#include "xlang/pkg/git.h"
#include "xlang/pkg/manifest.h"
#include "xlang/pkg/project.h"
#include "xlang/pkg/semver.h"
#include "xlang/sema/compilation.h"
#include "xlang/util/diagnostics.h"

#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace xlang::cli {

namespace {

[[nodiscard]] bool looksLikeStdSpec(const std::string& arg) {
    return pkg::validImportPath(arg.substr(0, arg.find('@')));
}

void report(const pkg::Problem& p, DiagnosticEngine& diagnostics) {
    diagnostics.error(p.code, Span{}, p.message, p.fix);
}

[[nodiscard]] std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void writeFile(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw pkg::PackageError("E0740", "cannot write `" + file.generic_string() + "`");
    }
    out << text;
}

void printProblem(const pkg::Problem& p) {
    const Palette c{useColor(STDERR_FILENO)};
    std::cerr << c.boldRed() << "error[" << p.code << "]:" << c.reset() << ' ' << p.message << '\n';
    if (!p.fix.empty()) {
        std::cerr << "  fix: " << p.fix << '\n';
    }
}

[[nodiscard]] std::string shortCommit(const std::string& c) {
    return c.substr(0, 12);
}

/// Highest stable release tag of `path` (online).
[[nodiscard]] std::string latestTagOf(const std::string& path, const std::string& source) {
    std::vector<std::string> names;
    for (const pkg::TagRef& t : pkg::listTags(pkg::gitUrl(path, source))) {
        names.push_back(t.name);
    }
    const auto latest = pkg::latestRelease(names);
    if (!latest) {
        throw pkg::PackageError("E0744", "`" + path + "` has no stable release tag, so there is no latest version",
                                "`latest` ignores pre-release and `-test` tags; name one explicitly, e.g. `" + path +
                                    "@v1.2.0-test`");
    }
    return latest->tag;
}

[[nodiscard]] const pkg::Dependency* findDependency(const pkg::Manifest& m, const std::string& path) {
    for (const pkg::Dependency& d : m.dependencies) {
        if (d.path == path) {
            return &d;
        }
    }
    return nullptr;
}

/// Resolves online, prints problems; returns false when any.
bool resolveAndStore(const std::filesystem::path& file, const std::string& text, const pkg::ResolveOptions& options) {
    const pkg::Manifest manifest = pkg::parseManifest(text, file);
    pkg::Project project = pkg::resolveProject(manifest, options);
    if (!project.problems.empty()) {
        for (const pkg::Problem& p : project.problems) {
            printProblem(p);
        }
        return false;
    }
    if (readFile(file) != text) {
        writeFile(file, text);
    }
    pkg::writeLock(project);
    const auto line = [](const pkg::ResolvedPackage& rp) {
        std::cout << "  " << rp.path << ' ' << (rp.local ? "(local)" : rp.version + " " + shortCommit(rp.commit)) << '\n';
    };
    if (project.std_package) {
        std::cout << "std:\n";
        line(*project.std_package);
    }
    std::cout << project.packages.size() << " package(s):\n";
    for (const pkg::ResolvedPackage& rp : project.packages) {
        line(rp);
    }
    return true;
}

int run(const PackageOptions& o) {
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    auto manifest_file = pkg::findManifest(cwd);

    if (o.action == "clean") {
        const std::filesystem::path root = manifest_file ? manifest_file->parent_path() : cwd;
        std::filesystem::remove_all(pkg::cacheDir(root), ec);
        std::cout << "removed " << pkg::cacheDir(root).generic_string() << '\n';
        return 0;
    }

    if (!manifest_file) {
        if (o.action != "install" || (o.specs.empty() && o.std_spec.empty())) {
            throw pkg::PackageError("E0740", "no xlang.toml found in `" + cwd.generic_string() + "` or its parents",
                                    "run `xlang package install <host/user/repo>` in your project directory to create one");
        }
        manifest_file = cwd / pkg::kManifestName;
        writeFile(*manifest_file, "");
    }
    std::string text = readFile(*manifest_file);
    const pkg::Manifest current = pkg::parseManifest(text, *manifest_file);

    if (o.action == "list") {
        pkg::Project project = pkg::resolveProject(current, {});
        const auto line = [](const pkg::ResolvedPackage& rp, const char* tag) {
            std::cout << rp.path << "  " << (rp.local ? "(local) " + rp.dir.generic_string() : rp.version + "  " + shortCommit(rp.commit))
                      << tag << '\n';
        };
        if (project.std_package) {
            line(*project.std_package, "  [std]");
        }
        for (const pkg::ResolvedPackage& rp : project.packages) {
            line(rp, rp.from_root ? "" : "  [indirect]");
        }
        for (const pkg::Problem& p : project.problems) {
            printProblem(p);
        }
        return project.problems.empty() ? 0 : 1;
    }

    pkg::ResolveOptions options;
    options.online = true;

    if (o.action == "install") {
        options.update = o.update;
        for (const std::string& spec : o.specs) {
            pkg::Dependency dep = pkg::parseDependencySpec(spec);
            const pkg::Dependency* existing = findDependency(current, dep.path);
            if (dep.version == "latest") {
                dep.version = latestTagOf(dep.path, existing != nullptr ? existing->source : std::string{});
            }
            text = pkg::setDependencyText(text, dep.path, dep.version);
        }
        if (!o.std_spec.empty()) {
            pkg::Dependency dep = pkg::parseDependencySpec(o.std_spec);
            if (dep.version == "latest") {
                dep.version = latestTagOf(dep.path, {});
            }
            text = pkg::setStdText(text, dep.path, dep.version);
        }
    } else if (o.action == "update") {
        options.update = true;
        for (const std::string& name : o.specs) {
            if (findDependency(current, name) == nullptr && !(current.std_package && current.std_package->path == name)) {
                throw pkg::PackageError("E0740", "`" + name + "` is not a dependency of this project");
            }
        }
        const auto wanted = [&](const std::string& path) {
            return o.specs.empty() || std::find(o.specs.begin(), o.specs.end(), path) != o.specs.end();
        };
        for (const pkg::Dependency& d : current.dependencies) {
            if (!d.isLocal() && wanted(d.path)) {
                text = pkg::setDependencyText(text, d.path, latestTagOf(d.path, d.source));
            }
        }
        if (current.std_package && !current.std_package->isLocal() && wanted(current.std_package->path)) {
            text = pkg::setStdText(text, current.std_package->path,
                                   latestTagOf(current.std_package->path, current.std_package->source));
        }
    } else if (o.action == "remove") {
        if (o.specs.empty()) {
            throw pkg::PackageError("E0740", "`xlang package remove` needs a repository path");
        }
        for (const std::string& name : o.specs) {
            if (!pkg::removeDependencyText(text, name)) {
                throw pkg::PackageError("E0740", "`" + name + "` is not in the `[dependencies]` of xlang.toml");
            }
        }
    }
    return resolveAndStore(*manifest_file, text, options) ? 0 : 1;
}

}  // namespace

int runPackage(const PackageOptions& options) {
    try {
        return run(options);
    } catch (const pkg::PackageError& e) {
        printProblem({e.code(), e.text(), e.fix()});
        return 1;
    }
}

std::vector<std::filesystem::path> attachProject(const std::filesystem::path& root_file, const BuildConfig& config,
                                                 Compilation& compilation, DiagnosticEngine& diagnostics) {
    const std::string& arg = config.std_path;
    std::error_code ec;
    const bool std_dir = !arg.empty() && std::filesystem::is_directory(arg, ec);
    const std::string std_spec = (!arg.empty() && !std_dir && looksLikeStdSpec(arg)) ? arg : std::string{};

    pkg::Manifest manifest;
    const std::filesystem::path start = root_file.has_parent_path() ? root_file.parent_path() : std::filesystem::path(".");
    if (const auto file = pkg::findManifest(start)) {
        try {
            manifest = pkg::loadManifest(*file);
        } catch (const pkg::PackageError& e) {
            report({e.code(), e.text(), e.fix()}, diagnostics);
            return {};
        }
    } else {
        manifest.dir = std::filesystem::absolute(start, ec).lexically_normal();
    }
    if (config.no_std || !arg.empty()) {
        manifest.std_package.reset();  // the command line wins over `[std]`
    }

    if (manifest.file.empty() && std_spec.empty()) {
        if (!arg.empty() && !config.no_std) {
            compilation.setStdRoot(arg);
        }
        return {};
    }

    pkg::ResolveOptions options;
    options.std_spec = config.no_std ? std::string{} : std_spec;
    pkg::Project project = pkg::resolveProject(std::move(manifest), options);
    for (const pkg::Problem& p : project.problems) {
        report(p, diagnostics);
    }
    for (const pkg::ResolvedPackage& rp : project.packages) {
        compilation.addPackage(rp.path, rp.dir);
    }
    if (!config.no_std) {
        if (std_dir) {
            compilation.setStdRoot(arg);
        } else if (project.std_package) {
            compilation.setStdRoot(project.std_package->dir);
        } else if (!arg.empty()) {
            compilation.setStdRoot(arg);
        }
    }
    std::vector<pkg::Problem> native_problems;
    std::vector<std::filesystem::path> natives = project.nativeInputs(config.target, native_problems);
    for (const pkg::Problem& p : native_problems) {
        report(p, diagnostics);
    }
    return natives;
}

}  // namespace xlang::cli
