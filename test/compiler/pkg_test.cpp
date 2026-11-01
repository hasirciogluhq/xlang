#include "check.h"

#include "xlang/pkg/error.h"
#include "xlang/pkg/git.h"
#include "xlang/pkg/manifest.h"
#include "xlang/pkg/project.h"
#include "xlang/pkg/semver.h"
#include "xlang/pkg/toml.h"
#include "xlang/sema/compilation.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace xlang;
namespace fs = std::filesystem;

namespace {

void testToml() {
    const pkg::TomlValue v = pkg::parseToml(
        "# comment\n"
        "[package]\nname = \"app\" # trailing\n\n"
        "[dependencies]\n\"github.com/u/r\" = \"v1.2.0\"\n\"github.com/u/s\" = { version = 'v2.0.0', source = \"../s\" }\n"
        "[[native]]\nos = \"linux\"\nlink = [\n  \"a.a\",\n  \"b.o\",\n]\n"
        "[[native]]\narch = \"aarch64\"\nlink = [\"c.a\"]\nflag = true\nn = 1_000\n");
    CHECK(v.get("package")->get("name")->string == "app");
    CHECK(v.get("dependencies")->get("github.com/u/r")->string == "v1.2.0");
    CHECK(v.get("dependencies")->get("github.com/u/s")->get("source")->string == "../s");
    const pkg::TomlValue* native = v.get("native");
    CHECK(native != nullptr && native->array.size() == 2);
    CHECK(native->array[0].get("link")->array.size() == 2);
    CHECK(native->array[1].get("flag")->boolean);
    CHECK(native->array[1].get("n")->integer == 1000);

    const auto fails = [](const char* text) {
        try {
            (void)pkg::parseToml(text);
        } catch (const pkg::TomlError&) {
            return true;
        }
        return false;
    };
    CHECK(fails("a = \n"));
    CHECK(fails("a = 1\na = 2\n"));
    CHECK(fails("[t]\n[t]\n"));
    CHECK(fails("a = \"x\nb = 1\n"));
    CHECK(fails("a = 1.5\n"));
    CHECK(fails("a = \"\"\"x\"\"\"\n"));
    CHECK(pkg::tomlQuote("a\"b\\c") == "\"a\\\"b\\\\c\"");
}

void testSemver() {
    const auto t = pkg::parseReleaseTag("v1.2.3");
    CHECK(t && t->major == 1 && t->minor == 2 && t->patch == 3 && t->stable());
    CHECK(pkg::parseReleaseTag("1.2.3").has_value());
    CHECK(!pkg::parseReleaseTag("1.2").has_value());
    CHECK(!pkg::parseReleaseTag("release-1").has_value());
    CHECK(!pkg::parseReleaseTag("V1.2.3").has_value());
    CHECK(!pkg::parseReleaseTag("v1.2.3-").has_value());
    CHECK(pkg::parseReleaseTag("v1.2.3-test")->isTest());
    CHECK(pkg::parseReleaseTag("v1.2.3-rc-test")->isTest());
    CHECK(!pkg::parseReleaseTag("v1.2.3-rc.1")->isTest());
    CHECK(pkg::tagProblem("v1.2.3").empty());
    CHECK(pkg::tagProblem("v1.2.3-rc.1").empty());
    CHECK(pkg::tagProblem("v1.2.3-test").empty());  // installable when named explicitly
    CHECK(!pkg::tagProblem("main").empty());

    // Numeric order (not text order), pre-releases / test tags / non-versions never win.
    const auto latest = pkg::latestRelease({"v1.9.0", "v1.10.0", "v2.0.0-rc.1", "v3.0.0-test", "nightly", "1.2.0"});
    CHECK(latest && latest->tag == "v1.10.0");
    CHECK(!pkg::latestRelease({"v1.0.0-test", "main"}).has_value());
    CHECK(pkg::latestRelease({"1.0.0", "v1.0.0"})->tag == "v1.0.0");
}

void testManifest() {
    const fs::path file = "/p/xlang.toml";
    const pkg::Manifest m = pkg::parseManifest(
        "[package]\nname = \"app\"\n"
        "[dependencies]\n\"github.com/u/r\" = \"v1.2.0\"\n\"github.com/u/l\" = \"latest\"\n"
        "\"gitlab.com/g/s\" = { version = \"1.0.0\", source = \"./vendor/s\" }\n"
        "\"example.org/x/y\" = { path = \"../y\" }\n"
        "[std]\npackage = \"github.com/x/std\"\nversion = \"v0.3.0\"\n"
        "[[native]]\nos = \"macos\"\narch = \"aarch64\"\nlink = [\"lib/a.a\"]\n",
        file);
    CHECK(m.name == "app");
    CHECK(m.dependencies.size() == 4);
    // dependencies are ordered by repository path
    CHECK(m.dependencies[0].isLocal() && m.dependencies[0].local == fs::path("/y"));
    CHECK(m.dependencies[3].source == "/p/vendor/s");
    CHECK(m.dependencies[1].version == "latest");
    CHECK(m.std_package && m.std_package->path == "github.com/x/std" && m.std_package->version == "v0.3.0");
    CHECK(m.natives.size() == 1 && m.natives[0].os == "macos");

    const auto codeOf = [&](const char* text) {
        try {
            (void)pkg::parseManifest(text, file);
        } catch (const pkg::PackageError& e) {
            return e.code();
        }
        return std::string{};
    };
    CHECK(codeOf("[dependencies]\n\"github.com/u/r\" = \"main\"\n") == "E0742");
    CHECK(codeOf("[dependencies]\n\"github.com/u/r\" = \"v1.0.0-test\"\n").empty());
    CHECK(codeOf("[dependencies]\n\"nodot/r\" = \"v1.0.0\"\n") == "E0740");
    CHECK(codeOf("[dependencies]\n\"github.com/../r\" = \"v1.0.0\"\n") == "E0740");
    CHECK(codeOf("[dependencies]\n\"std/x\" = \"v1.0.0\"\n") == "E0740");
    CHECK(codeOf("[dependencies]\n\"github.com/u/r\" = { versoin = \"v1.0.0\" }\n") == "E0740");
    CHECK(codeOf("[dependencies]\n\"github.com/u/r\" = { path = \"../r\", version = \"v1.0.0\" }\n") == "E0740");
    CHECK(codeOf("[wat]\n") == "E0740");
    CHECK(codeOf("[[native]]\nos = \"plan9\"\nlink = [\"a\"]\n") == "E0740");
    CHECK(codeOf("[[native]]\nlink = [\"../a.a\"]\n") == "E0740");
    CHECK(codeOf("[[native]]\nos = \"linux\"\n") == "E0740");
    CHECK(codeOf("[std]\n") == "E0740");
    CHECK(codeOf("") .empty());

    CHECK(pkg::validImportPath("github.com/u/r"));
    CHECK(pkg::validImportPath("git.example.org/a/b/c"));
    CHECK(!pkg::validImportPath("std/mem"));
    CHECK(!pkg::validImportPath("github.com"));
    CHECK(!pkg::validImportPath("github.com//r"));

    const pkg::Dependency d = pkg::parseDependencySpec("github.com/u/r@v1.4.0");
    CHECK(d.path == "github.com/u/r" && d.version == "v1.4.0");
    CHECK(pkg::parseDependencySpec("github.com/u/r").version == "latest");
}

void testManifestEdits() {
    std::string text = "# my app\n[package]\nname = \"app\"\n";
    text = pkg::setDependencyText(text, "github.com/u/r", "v1.0.0");
    CHECK_EQ(text, std::string("# my app\n[package]\nname = \"app\"\n\n[dependencies]\n\"github.com/u/r\" = \"v1.0.0\"\n"));
    text = pkg::setDependencyText(text, "github.com/u/s", "v2.0.0");
    text = pkg::setDependencyText(text, "github.com/u/r", "v1.1.0");
    CHECK(text.find("\"github.com/u/r\" = \"v1.1.0\"") != std::string::npos);
    CHECK(text.find("v1.0.0") == std::string::npos);
    CHECK(text.find("# my app") == 0);

    // A table that follows [dependencies] stays intact; inline tables keep their other keys.
    std::string t2 =
        "[dependencies]\n\"github.com/u/r\" = { version = \"v1.0.0\", source = \"../r\" }\n\n[[native]]\nlink = [\"a\"]\n";
    t2 = pkg::setDependencyText(t2, "github.com/u/r", "v1.5.0");
    CHECK(t2.find("{ version = \"v1.5.0\", source = \"../r\" }") != std::string::npos);
    t2 = pkg::setDependencyText(t2, "github.com/u/n", "v1.0.0");
    CHECK(t2.find("[[native]]") > t2.find("github.com/u/n"));
    CHECK(pkg::parseManifest(t2, "/p/xlang.toml").dependencies.size() == 2);

    CHECK(pkg::removeDependencyText(t2, "github.com/u/n"));
    CHECK(!pkg::removeDependencyText(t2, "github.com/u/n"));
    CHECK(t2.find("github.com/u/n") == std::string::npos);

    std::string s = pkg::setStdText("", "github.com/x/std", "v1.0.0");
    s = pkg::setStdText(s + "[dependencies]\n", "github.com/x/std", "v1.1.0");
    const pkg::Manifest m = pkg::parseManifest(s, "/p/xlang.toml");
    CHECK(m.std_package && m.std_package->version == "v1.1.0");
}

// ---- resolver against real (local) git repositories

bool git(const fs::path& dir, std::vector<std::string> args) {
    args.insert(args.begin(), {"git", "-c", "user.email=t@t", "-c", "user.name=t", "-c", "commit.gpgsign=false", "-c",
                               "tag.gpgsign=false"});
    return pkg::runProcess(args, dir).status == 0;
}

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file) << text;
}

void testResolver() {
    if (pkg::runProcess({"git", "--version"}).status != 0) {
        std::cerr << "git not found: skipping the resolver tests\n";
        return;
    }
    const fs::path root = fs::temp_directory_path() / ("xlang-pkg-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    const fs::path repo = root / "repo";
    const fs::path proj = root / "proj";
    fs::create_directories(repo);
    fs::create_directories(proj);

    CHECK(git(repo, {"init", "-q", "-b", "main"}));
    write(repo / "index.xl", "export fn one() -> i32 { return 1 }\n");
    CHECK(git(repo, {"add", "."}));
    CHECK(git(repo, {"commit", "-q", "-m", "one"}));
    CHECK(git(repo, {"tag", "v1.0.0"}));
    write(repo / "index.xl", "export fn one() -> i32 { return 11 }\n");
    write(repo / "sub" / "index.xl", "export fn two() -> i32 { return 2 }\n");
    CHECK(git(repo, {"add", "."}));
    CHECK(git(repo, {"commit", "-q", "-m", "two"}));
    CHECK(git(repo, {"tag", "-a", "-m", "annotated", "1.1.0"}));
    CHECK(git(repo, {"tag", "v1.2.0-test"}));
    CHECK(git(repo, {"tag", "v2.0.0-rc.1"}));
    CHECK(git(repo, {"tag", "nightly"}));

    // tags
    const auto tags = pkg::listTags(pkg::gitUrl("example.org/u/repo", repo.string()));
    CHECK(tags.size() == 5);

    write(proj / "xlang.toml",
          "[dependencies]\n\"example.org/u/repo\" = { version = \"latest\", source = \"" + repo.string() + "\" }\n");
    const pkg::Manifest manifest = pkg::loadManifest(proj / "xlang.toml");

    // offline before install: not installed
    {
        const pkg::Project p = pkg::resolveProject(manifest, {});
        CHECK(p.problems.size() == 1 && p.problems[0].code == "E0741");
    }
    // install: latest = 1.1.0 (test, pre-release and non-version tags ignored)
    pkg::Project online;
    {
        pkg::ResolveOptions o;
        o.online = true;
        online = pkg::resolveProject(manifest, o);
        CHECK(online.problems.empty());
        CHECK(online.packages.size() == 1);
        if (!online.packages.empty()) {
            const pkg::ResolvedPackage& rp = online.packages[0];
            CHECK(rp.version == "1.1.0");
            CHECK(rp.commit.size() == 40);
            CHECK(fs::exists(rp.dir / "sub" / "index.xl"));
            CHECK(!fs::exists(rp.dir / ".git"));
            CHECK(rp.dir == proj / ".xlang" / "cache" / "example.org/u/repo@1.1.0");
        }
        pkg::writeLock(online);
        CHECK(fs::exists(proj / "xlang.lock"));
    }
    // offline now works, from lock + cache
    {
        const pkg::Project p = pkg::resolveProject(manifest, {});
        CHECK(p.problems.empty());
        CHECK(p.packages.size() == 1 && p.packages[0].version == "1.1.0");
        CHECK_EQ(p.lock.text(), online.lock.text());
        CHECK_EQ(pkg::Lock::load(proj / "xlang.lock").text(), online.lock.text());
    }
    // a tampered cache no longer matches the lock
    {
        write(proj / ".xlang/cache/example.org/u/repo@1.1.0" / pkg::kCommitMarker, "deadbeef\n");
        const pkg::Project p = pkg::resolveProject(manifest, {});
        CHECK(p.problems.size() == 1 && p.problems[0].code == "E0741");
        // online repairs it from the lock
        pkg::ResolveOptions o;
        o.online = true;
        const pkg::Project fixed = pkg::resolveProject(manifest, o);
        CHECK(fixed.problems.empty());
    }
    // a moved tag is detected
    {
        write(repo / "index.xl", "export fn one() -> i32 { return 99 }\n");
        CHECK(git(repo, {"add", "."}));
        CHECK(git(repo, {"commit", "-q", "-m", "three"}));
        CHECK(git(repo, {"tag", "-f", "-a", "-m", "moved", "1.1.0"}));
        fs::remove_all(proj / ".xlang");
        pkg::ResolveOptions o;
        o.online = true;
        const pkg::Project moved = pkg::resolveProject(manifest, o);
        CHECK(moved.problems.size() == 1 && moved.problems[0].code == "E0744");
        CHECK(!fs::exists(proj / ".xlang/cache/example.org/u/repo@1.1.0"));
        // `--update` picks the newest release again, but the moved pin stays an error
        CHECK(git(repo, {"tag", "v1.3.0"}));
        o.update = true;
        const pkg::Project upd = pkg::resolveProject(manifest, o);
        CHECK(upd.problems.empty() && upd.packages.size() == 1 && upd.packages[0].version == "v1.3.0");
    }
    // explicit pins: a `-test` tag installs when named; a malformed tag is refused before any download
    {
        write(proj / "xlang.toml",
              "[dependencies]\n\"example.org/u/repo\" = { version = \"v1.2.0-test\", source = \"" + repo.string() + "\" }\n");
        fs::remove_all(proj / ".xlang");
        fs::remove(proj / "xlang.lock");
        pkg::ResolveOptions o;
        o.online = true;
        const pkg::Project pinned = pkg::resolveProject(pkg::loadManifest(proj / "xlang.toml"), o);
        CHECK(pinned.problems.empty() && pinned.packages.size() == 1 && pinned.packages[0].version == "v1.2.0-test");

        write(proj / "xlang.toml",
              "[dependencies]\n\"example.org/u/repo\" = { version = \"main\", source = \"" + repo.string() + "\" }\n");
        try {
            (void)pkg::loadManifest(proj / "xlang.toml");
            CHECK(false);
        } catch (const pkg::PackageError& e) {
            CHECK(e.code() == "E0742");
        }
    }
    // a repository with only test / pre-release tags has no latest: error, not a silent pick
    {
        const fs::path only = root / "only";
        fs::create_directories(only);
        CHECK(git(only, {"init", "-q", "-b", "main"}));
        write(only / "index.xl", "export fn o() -> i32 { return 1 }\n");
        CHECK(git(only, {"add", "."}));
        CHECK(git(only, {"commit", "-q", "-m", "o"}));
        CHECK(git(only, {"tag", "v1.0.0-test"}));
        CHECK(git(only, {"tag", "v1.0.0-rc.1"}));
        write(proj / "xlang.toml",
              "[dependencies]\n\"example.org/u/only\" = { version = \"latest\", source = \"" + only.string() + "\" }\n");
        fs::remove_all(proj / ".xlang");
        fs::remove(proj / "xlang.lock");
        pkg::ResolveOptions o;
        o.online = true;
        const pkg::Project none = pkg::resolveProject(pkg::loadManifest(proj / "xlang.toml"), o);
        CHECK(none.problems.size() == 1 && none.problems[0].code == "E0744");
        CHECK(none.packages.empty());
    }
    // transitive dependencies, native entries, root wins over a conflicting indirect version
    {
        const fs::path dep = root / "dep";
        fs::create_directories(dep);
        CHECK(git(dep, {"init", "-q", "-b", "main"}));
        write(dep / "index.xl", "export fn d() -> i32 { return 3 }\n");
        write(dep / "lib" / "libfoo.a", "!<arch>\n");
        write(dep / "xlang.toml",
              "[dependencies]\n\"example.org/u/repo\" = { version = \"v1.0.0\", source = \"" + repo.string() +
                  "\" }\n[[native]]\nos = \"linux\"\nlink = [\"lib/libfoo.a\"]\n[[native]]\nos = \"macos\"\nlink = [\"lib/missing.a\"]\n");
        CHECK(git(dep, {"add", "."}));
        CHECK(git(dep, {"commit", "-q", "-m", "d"}));
        CHECK(git(dep, {"tag", "v0.1.0"}));
        write(proj / "xlang.toml",
              "[dependencies]\n\"example.org/u/dep\" = { version = \"v0.1.0\", source = \"" + dep.string() + "\" }\n");
        pkg::ResolveOptions o;
        o.online = true;
        pkg::Project p = pkg::resolveProject(pkg::loadManifest(proj / "xlang.toml"), o);
        CHECK(p.problems.empty());
        CHECK(p.packages.size() == 2);  // dep + its dependency (repo at its pinned v1.0.0)
        CHECK(p.lock.entries.size() == 2);
        TargetInfo linux_target;
        linux_target.os = "linux";
        linux_target.arch = "x86_64";
        std::vector<pkg::Problem> problems;
        const auto files = p.nativeInputs(linux_target, problems);
        CHECK(files.size() == 1 && problems.empty());
        TargetInfo mac;
        mac.os = "macos";
        mac.arch = "aarch64";
        problems.clear();
        (void)p.nativeInputs(mac, problems);
        CHECK(problems.size() == 1 && problems[0].code == "E0746");
        TargetInfo win;
        win.os = "windows";
        problems.clear();
        CHECK(p.nativeInputs(win, problems).empty() && problems.empty());

        // the project pins the indirect dependency itself: the project wins, no conflict
        write(proj / "xlang.toml",
              "[dependencies]\n\"example.org/u/dep\" = { version = \"v0.1.0\", source = \"" + dep.string() +
                  "\" }\n\"example.org/u/repo\" = { version = \"1.1.0\", source = \"" + repo.string() + "\" }\n");
        fs::remove_all(proj / ".xlang");
        fs::remove(proj / "xlang.lock");
        // 1.1.0 was moved above, so the download is refused only because no lock exists: succeeds
        const pkg::Project pinned = pkg::resolveProject(pkg::loadManifest(proj / "xlang.toml"), o);
        CHECK(pinned.problems.empty());

        // two packages that need different versions and the project is silent: E0743
        const fs::path dep2 = root / "dep2";
        fs::create_directories(dep2);
        CHECK(git(dep2, {"init", "-q", "-b", "main"}));
        write(dep2 / "index.xl", "export fn e() -> i32 { return 4 }\n");
        write(dep2 / "xlang.toml",
              "[dependencies]\n\"example.org/u/repo\" = { version = \"1.1.0\", source = \"" + repo.string() + "\" }\n");
        CHECK(git(dep2, {"add", "."}));
        CHECK(git(dep2, {"commit", "-q", "-m", "e"}));
        CHECK(git(dep2, {"tag", "v0.2.0"}));
        write(proj / "xlang.toml",
              "[dependencies]\n\"example.org/u/dep\" = { version = \"v0.1.0\", source = \"" + dep.string() +
                  "\" }\n\"example.org/u/dep2\" = { version = \"v0.2.0\", source = \"" + dep2.string() + "\" }\n");
        fs::remove_all(proj / ".xlang");
        fs::remove(proj / "xlang.lock");
        const pkg::Project conflict = pkg::resolveProject(pkg::loadManifest(proj / "xlang.toml"), o);
        bool saw = false;
        for (const pkg::Problem& pr : conflict.problems) {
            saw = saw || pr.code == "E0743";
        }
        CHECK(saw);
    }
    fs::remove_all(root);
}

// ---- import resolution through the compiler

void testPackageImports() {
    const auto build = [](const char* main_text, const char* import_path, bool add, std::string& diagnostics) {
        SourceManager sources;
        std::ostringstream out;
        BuildConfig cfg;
        cfg.no_std = true;
        DiagnosticEngine diag(sources, out);
        Compilation comp(sources, cfg, diag);
        comp.addVirtualFile("app/main.xl", main_text);
        comp.addVirtualFile("/cache/example.org/u/repo@v1/index.xl",
                            "from \"util\" import helper\nexport fn f() -> i32 { return helper() }\n");
        comp.addVirtualFile("/cache/example.org/u/repo@v1/util.xl", "export fn helper() -> i32 { return 7 }\n");
        comp.addVirtualFile("/cache/example.org/u/repo@v1/sub/index.xl", "export fn g() -> i32 { return 8 }\n");
        if (add) {
            comp.addPackage(import_path, "/cache/example.org/u/repo@v1");
        }
        comp.loadRoot("app/main.xl");
        comp.finish();
        if (!diag.hasErrors()) {
            comp.analyzeAll();
        }
        bool ok = !diag.hasErrors();
        if (ok) {
            (void)comp.lowerAll();
            ok = !diag.hasErrors();
        }
        const bool keyed = comp.moduleOf("example.org/u/repo") != nullptr;
        diag.flush();
        diagnostics = out.str();
        return std::make_pair(ok, keyed);
    };
    std::string d;
    {
        const auto [ok, keyed] = build("from \"example.org/u/repo\" import f\nfn main() -> i32 { return f() }\n",
                                       "example.org/u/repo", true, d);
        CHECK(ok);
        CHECK(keyed);
    }
    {
        const auto [ok, keyed] = build("from \"example.org/u/repo/sub\" import g\nfn main() -> i32 { return g() }\n",
                                       "example.org/u/repo", true, d);
        CHECK(ok);
        (void)keyed;
    }
    {
        const auto [ok, keyed] = build("from \"example.org/u/repo\" import f\nfn main() -> i32 { return f() }\n",
                                       "example.org/u/repo", false, d);
        CHECK(!ok);
        CHECK(d.find("E0745") != std::string::npos);
        (void)keyed;
    }
    {
        const auto [ok, keyed] = build("from \"example.org/u/repo/missing\" import f\nfn main() -> i32 { return 0 }\n",
                                       "example.org/u/repo", true, d);
        CHECK(!ok);
        CHECK(d.find("E0711") != std::string::npos);
        (void)keyed;
    }
}

}  // namespace

void runPackageTests() {
    testToml();
    testSemver();
    testManifest();
    testManifestEdits();
    testPackageImports();
    testResolver();
}
