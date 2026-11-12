#include "xlang/pkg/manifest.h"

#include "xlang/pkg/error.h"
#include "xlang/pkg/semver.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace xlang::pkg {

namespace {

[[nodiscard]] std::string trim(std::string_view s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return std::string(s.substr(a, b - a));
}

[[noreturn]] void bad(const std::filesystem::path& file, int line, const std::string& what,
                      const std::string& fix = {}) {
    std::string where = file.empty() ? std::string("xlang.toml") : file.generic_string();
    if (line > 0) {
        where += ":" + std::to_string(line);
    }
    throw PackageError("E0740", where + ": " + what, fix);
}

void onlyKeys(const TomlValue& table, const std::set<std::string>& allowed,
              const std::filesystem::path& file, const std::string& where) {
    for (const auto& [key, value] : table.table) {
        if (!allowed.contains(key)) {
            std::string list;
            for (const std::string& a : allowed) {
                list += (list.empty() ? "`" : ", `") + a + "`";
            }
            bad(file, value.line, "unknown key `" + key + "` in " + where, "allowed keys: " + list);
        }
    }
}

[[nodiscard]] std::string stringField(const TomlValue& table, const std::string& key,
                                      const std::filesystem::path& file, const std::string& where) {
    const TomlValue* v = table.get(key);
    if (v == nullptr) {
        return {};
    }
    if (!v->isString()) {
        bad(file, v->line, "`" + key + "` in " + where + " must be a string, not " + TomlValue::kindName(v->kind));
    }
    return v->string;
}

[[nodiscard]] std::filesystem::path absolutePath(const std::filesystem::path& base, const std::string& text) {
    std::filesystem::path p = text;
    if (p.is_relative()) {
        p = base / p;
    }
    return p.lexically_normal();
}

/// `source` values that are directories are made absolute against the manifest; URLs stay as written.
[[nodiscard]] std::string resolveSource(const std::filesystem::path& base, const std::string& source) {
    if (source.starts_with("./") || source.starts_with("../")) {
        return absolutePath(base, source).generic_string();
    }
    return source;
}

void checkVersion(const std::filesystem::path& file, int line, const std::string& path, const std::string& version) {
    if (version == "latest") {
        return;
    }
    if (const std::string problem = tagProblem(version); !problem.empty()) {
        throw PackageError("E0742",
                           file.generic_string() + ":" + std::to_string(line) + ": dependency `" + path + "`: " + problem,
                           "use a tag such as `v1.2.0`, or `latest`");
    }
}

[[nodiscard]] Dependency parseDependency(const std::string& path, const TomlValue& v, const std::filesystem::path& file,
                                         const std::filesystem::path& dir, bool path_is_key) {
    Dependency d;
    d.path = path;
    const std::string where = "dependency `" + path + "`";
    if (v.isString()) {
        d.version = v.string;
    } else if (v.isTable()) {
        onlyKeys(v, path_is_key ? std::set<std::string>{"version", "source", "path"}
                                : std::set<std::string>{"package", "version", "source", "path"},
                 file, where);
        d.version = stringField(v, "version", file, where);
        d.source = resolveSource(dir, stringField(v, "source", file, where));
        const std::string local = stringField(v, "path", file, where);
        if (!local.empty()) {
            if (!d.version.empty() || !d.source.empty()) {
                bad(file, v.line, where + ": `path` cannot be combined with `version` or `source`");
            }
            d.local = absolutePath(dir, local);
        }
    } else {
        bad(file, v.line, where + " must be a version string or a table, not " + TomlValue::kindName(v.kind));
    }
    if (!d.isLocal()) {
        if (d.version.empty()) {
            d.version = "latest";
        }
        checkVersion(file, v.line, d.path, d.version);
    }
    return d;
}

}  // namespace

bool validImportPath(std::string_view path) {
    if (path.empty()) {
        return false;
    }
    std::size_t start = 0;
    bool first = true;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view seg = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (seg.empty() || seg == "." || seg == "..") {
            return false;
        }
        for (const char c : seg) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_' && c != '~') {
                return false;
            }
        }
        if (first && seg.find('.') == std::string_view::npos) {
            return false;
        }
        first = false;
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return !first && path.find('/') != std::string_view::npos;
}

bool looksLikePackageImport(std::string_view spec) {
    return validImportPath(spec);
}

Manifest parseManifest(std::string_view text, const std::filesystem::path& file) {
    TomlValue root;
    try {
        root = parseToml(text);
    } catch (const TomlError& e) {
        throw PackageError("E0740", file.generic_string() + ": " + e.what());
    }
    Manifest m;
    m.file = file;
    m.dir = file.parent_path();
    onlyKeys(root, {"package", "dependencies", "std", "native"}, file, "the manifest");

    if (const TomlValue* p = root.get("package")) {
        if (!p->isTable()) {
            bad(file, p->line, "`package` must be a table");
        }
        onlyKeys(*p, {"name"}, file, "[package]");
        m.name = stringField(*p, "name", file, "[package]");
    }

    if (const TomlValue* deps = root.get("dependencies")) {
        if (!deps->isTable()) {
            bad(file, deps->line, "`dependencies` must be a table");
        }
        for (const auto& [path, value] : deps->table) {
            if (!validImportPath(path)) {
                bad(file, value.line,
                    "`" + path + "` is not a repository path",
                    "dependencies are named by repository path, for example \"github.com/user/repo\"");
            }
            if (path == "std" || path.starts_with("std/")) {
                bad(file, value.line, "`std` is not a dependency name; use the `[std]` table");
            }
            m.dependencies.push_back(parseDependency(path, value, file, m.dir, true));
        }
    }

    if (const TomlValue* s = root.get("std")) {
        if (!s->isTable()) {
            bad(file, s->line, "`std` must be a table");
        }
        const std::string pkg = stringField(*s, "package", file, "[std]");
        const TomlValue* local = s->get("path");
        if (pkg.empty() && local == nullptr) {
            bad(file, s->line, "`[std]` needs `package = \"host/user/repo\"` (with `version`) or `path = \"dir\"`");
        }
        if (!pkg.empty() && !validImportPath(pkg)) {
            bad(file, s->line, "`[std] package` = `" + pkg + "` is not a repository path");
        }
        TomlValue copy = *s;
        copy.table.erase("package");
        m.std_package = parseDependency(pkg.empty() ? "std" : pkg, copy, file, m.dir, true);
    }

    if (const TomlValue* nat = root.get("native")) {
        if (!nat->isArray()) {
            bad(file, nat->line, "`native` must be an array of tables: write `[[native]]`");
        }
        for (const TomlValue& n : nat->array) {
            onlyKeys(n, {"os", "arch", "link"}, file, "[[native]]");
            NativeEntry e;
            e.os = stringField(n, "os", file, "[[native]]");
            e.arch = stringField(n, "arch", file, "[[native]]");
            if (!e.os.empty() && e.os != "linux" && e.os != "macos" && e.os != "windows") {
                bad(file, n.line, "`os` must be linux, macos or windows, not `" + e.os + "`");
            }
            if (!e.arch.empty() && e.arch != "x86_64" && e.arch != "aarch64") {
                bad(file, n.line, "`arch` must be x86_64 or aarch64, not `" + e.arch + "`");
            }
            const TomlValue* link = n.get("link");
            if (link == nullptr || !link->isArray() || link->array.empty()) {
                bad(file, n.line, "`[[native]]` needs `link = [\"file.a\", ...]`");
            }
            for (const TomlValue& item : link->array) {
                if (!item.isString() || item.string.empty()) {
                    bad(file, item.line, "`link` entries must be file names (strings)");
                }
                const std::filesystem::path p = item.string;
                if (p.is_absolute() || std::any_of(p.begin(), p.end(), [](const auto& part) { return part == ".."; })) {
                    bad(file, item.line, "`link` entry `" + item.string + "` must stay inside the package",
                        "use a path relative to the package root, without `..`");
                }
                e.link.push_back(item.string);
            }
            m.natives.push_back(std::move(e));
        }
    }
    return m;
}

Manifest loadManifest(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw PackageError("E0740", "cannot read `" + file.generic_string() + "`");
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parseManifest(text.str(), file);
}

std::optional<std::filesystem::path> findManifest(const std::filesystem::path& start) {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::absolute(start, ec).lexically_normal();
    if (dir.empty()) {
        return std::nullopt;
    }
    for (;;) {
        const std::filesystem::path candidate = dir / kManifestName;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir || parent.empty()) {
            return std::nullopt;
        }
        dir = parent;
    }
}

Dependency parseDependencySpec(std::string_view spec) {
    Dependency d;
    const std::size_t at = spec.find('@');
    d.path = std::string(spec.substr(0, at));
    d.version = at == std::string_view::npos ? "latest" : std::string(spec.substr(at + 1));
    if (!validImportPath(d.path)) {
        throw PackageError("E0740", "`" + d.path + "` is not a repository path",
                           "name a repository like `github.com/user/repo` or `github.com/user/repo@v1.2.0`");
    }
    if (d.version.empty()) {
        throw PackageError("E0742", "`" + std::string(spec) + "` has an empty version after `@`");
    }
    if (d.version != "latest") {
        if (const std::string problem = tagProblem(d.version); !problem.empty()) {
            throw PackageError("E0742", problem, "use a tag such as `v1.2.0`, or leave the version out for the latest release");
        }
    }
    return d;
}

namespace {

[[nodiscard]] std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

[[nodiscard]] std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) {
        out += l + "\n";
    }
    return out;
}

/// [first, last) line range of the `[dependencies]` body; first == npos when there is no table.
struct Section {
    std::size_t header{std::string::npos};
    std::size_t end{0};
};

[[nodiscard]] Section findDependencies(const std::vector<std::string>& lines) {
    Section s;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (s.header == std::string::npos) {
            if (t == "[dependencies]" || t.starts_with("[dependencies]#") || t.starts_with("[dependencies] #")) {
                s.header = i;
                s.end = lines.size();
            }
        } else if (t.starts_with("[")) {
            s.end = i;
            break;
        }
    }
    return s;
}

[[nodiscard]] bool isKeyLine(const std::string& line, const std::string& path) {
    const std::string t = trim(line);
    for (const std::string& quoted : {"\"" + path + "\"", "'" + path + "'"}) {
        if (t.starts_with(quoted)) {
            const std::string rest = trim(t.substr(quoted.size()));
            if (rest.starts_with("=")) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::string setDependencyText(const std::string& manifest, const std::string& path, const std::string& version) {
    std::vector<std::string> lines = splitLines(manifest);
    const std::string entry = tomlQuote(path) + " = " + tomlQuote(version);
    Section s = findDependencies(lines);
    if (s.header == std::string::npos) {
        if (!lines.empty() && !trim(lines.back()).empty()) {
            lines.emplace_back();
        }
        lines.emplace_back("[dependencies]");
        lines.push_back(entry);
        return joinLines(lines);
    }
    for (std::size_t i = s.header + 1; i < s.end; ++i) {
        if (!isKeyLine(lines[i], path)) {
            continue;
        }
        // An inline table keeps its other keys: only the version string changes.
        std::string& line = lines[i];
        if (line.find('{') != std::string::npos) {
            const std::size_t v = line.find("version");
            const std::size_t q1 = v == std::string::npos ? v : line.find('"', v);
            const std::size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
            if (q2 != std::string::npos) {
                line = line.substr(0, q1) + tomlQuote(version) + line.substr(q2 + 1);
                return joinLines(lines);
            }
        }
        line = entry;
        return joinLines(lines);
    }
    std::size_t at = s.header + 1;
    for (std::size_t i = s.header + 1; i < s.end; ++i) {
        if (!trim(lines[i]).empty()) {
            at = i + 1;
        }
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), entry);
    return joinLines(lines);
}

std::string setStdText(const std::string& manifest, const std::string& package, const std::string& version) {
    std::vector<std::string> lines = splitLines(manifest);
    std::size_t header = std::string::npos;
    std::size_t end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (header == std::string::npos) {
            if (t == "[std]") {
                header = i;
            }
        } else if (t.starts_with("[")) {
            end = i;
            break;
        }
    }
    const std::vector<std::string> body = {"[std]", "package = " + tomlQuote(package), "version = " + tomlQuote(version)};
    if (header == std::string::npos) {
        if (!lines.empty() && !trim(lines.back()).empty()) {
            lines.emplace_back();
        }
        lines.insert(lines.end(), body.begin(), body.end());
    } else {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(header), lines.begin() + static_cast<std::ptrdiff_t>(end));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(header), body.begin(), body.end());
        if (end < lines.size() + 0 && header + body.size() < lines.size()) {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(header + body.size()), std::string{});
        }
    }
    return joinLines(lines);
}

bool removeDependencyText(std::string& manifest, const std::string& path) {
    std::vector<std::string> lines = splitLines(manifest);
    const Section s = findDependencies(lines);
    if (s.header == std::string::npos) {
        return false;
    }
    for (std::size_t i = s.header + 1; i < s.end; ++i) {
        if (isKeyLine(lines[i], path)) {
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
            manifest = joinLines(lines);
            return true;
        }
    }
    return false;
}

}  // namespace xlang::pkg
