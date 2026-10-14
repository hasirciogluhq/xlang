#include "xlang/sema/compilation.h"

#include "xlang/lang/frontend.h"
#include "xlang/util/error.h"

#include <algorithm>
#include <format>
#include <sstream>

namespace xlang {

hir::Module lowerProgram(const std::vector<Sema*>& modules);

namespace {

[[nodiscard]] std::string normalizedKey(const std::filesystem::path& p) {
    return p.lexically_normal().generic_string();
}

[[nodiscard]] std::string lastSegment(const std::string& spec) {
    const std::size_t slash = spec.find_last_of('/');
    return slash == std::string::npos ? spec : spec.substr(slash + 1);
}

}  // namespace

Compilation::Compilation(SourceManager& sources, const BuildConfig& config, DiagnosticEngine& diagnostics,
                         const LayoutHistory* history)
    : sources_(sources), config_(config), diag_(diagnostics), history_(history) {}

Compilation::~Compilation() = default;

void Compilation::addVirtualFile(const std::string& path, std::string text) {
    virtual_files_[normalizedKey(path)] = std::move(text);
}

bool Compilation::fileExists(const std::filesystem::path& p) const {
    if (virtual_files_.contains(normalizedKey(p))) {
        return true;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

std::string Compilation::keyFor(const std::filesystem::path& path, bool is_root) const {
    if (is_root) {
        return {};
    }
    std::filesystem::path p = path.lexically_normal();
    const auto stripIndex = [](std::string s) {
        if (s.ends_with("/index")) {
            s.resize(s.size() - 6);
        } else if (s == "index") {
            s.clear();
        }
        return s;
    };
    const auto relativeTo = [&](const std::filesystem::path& base) -> std::optional<std::string> {
        if (base.empty()) {
            return std::nullopt;
        }
        const std::filesystem::path rel = p.lexically_relative(base.lexically_normal());
        if (rel.empty() || *rel.begin() == "..") {
            return std::nullopt;
        }
        std::filesystem::path stem = rel;
        stem.replace_extension();
        return stripIndex(stem.generic_string());
    };
    if (const auto r = relativeTo(std_root_)) {
        return r->empty() ? "std" : "std/" + *r;
    }
    if (const auto r = relativeTo(root_dir_)) {
        return *r;
    }
    std::filesystem::path stem = p;
    stem.replace_extension();
    return stripIndex(stem.generic_string());
}

std::optional<std::filesystem::path> Compilation::locate(const std::string& spec, const ModuleInfo* from,
                                                         const Span& at) {
    std::vector<std::filesystem::path> bases;
    std::string rest = spec;
    if (spec == "std" || spec.starts_with("std/")) {
        if (!std_root_.empty()) {
            bases.push_back(std_root_);
            rest = spec == "std" ? "" : spec.substr(4);
        }
    }
    if (bases.empty()) {
        if (from != nullptr && !from->path.empty()) {
            bases.push_back(from->path.parent_path());
        }
        if (!root_dir_.empty()) {
            bases.push_back(root_dir_);
        }
        if (bases.empty()) {
            bases.push_back(std::filesystem::path{});
        }
    }
    for (const std::filesystem::path& base : bases) {
        const std::filesystem::path as_file = rest.empty() ? base / "index.xl" : base / (rest + ".xl");
        const std::filesystem::path as_dir = rest.empty() ? base / "index.xl" : base / rest / "index.xl";
        const bool f = fileExists(as_file);
        const bool d = fileExists(as_dir);
        if (f && d && as_file != as_dir) {
            diag_.error("E0711", at, std::format("module path `{}` is ambiguous", spec),
                        std::format("both `{}` and `{}` exist", as_file.generic_string(), as_dir.generic_string()));
            return std::nullopt;
        }
        if (f) {
            return as_file;
        }
        if (d) {
            return as_dir;
        }
    }
    return std::nullopt;
}

ModuleInfo* Compilation::importTarget(ModuleInfo& from, const std::string& spec, const Span& at) {
    const auto key = std::make_pair(spec, from.key);
    if (const auto it = targets_.find(key); it != targets_.end()) {
        return it->second;
    }
    const auto path = locate(spec, &from, at);
    ModuleInfo* target = nullptr;
    if (path) {
        target = loadModule(*path, false);
    } else if (!diag_.hasErrors() || true) {
        diag_.error("E0711", at, std::format("module `{}` not found", spec),
                    "no `.xl` file or `index.xl` directory with that path");
    }
    targets_[key] = target;
    return target;
}

void Compilation::collectMacros(ModuleInfo& dep, MacroTable& table, std::set<const ModuleInfo*>& seen) {
    if (!seen.insert(&dep).second) {
        return;
    }
    for (const Item& item : dep.program.items) {
        if (const auto* m = std::get_if<DefineMacroDecl>(&item.decl); m != nullptr && item.modifiers.exported) {
            MacroDef def;
            def.name = m->name;
            def.params = m->params;
            def.body = m->body;
            def.span = m->name_span;
            table.macros.emplace(def.name, std::move(def));
        }
        if (const auto* e = std::get_if<ExportFromDecl>(&item.decl)) {
            const auto it = targets_.find({e->module, dep.key});
            if (it != targets_.end() && it->second != nullptr) {
                collectMacros(*it->second, table, seen);
            }
        }
    }
}

ModuleInfo* Compilation::loadModule(const std::filesystem::path& path, bool is_root) {
    const std::string norm = normalizedKey(path);
    if (const auto it = by_path_.find(norm); it != by_path_.end()) {
        return it->second.get();
    }
    auto owned = std::make_unique<ModuleInfo>();
    ModuleInfo* m = owned.get();
    by_path_[norm] = std::move(owned);
    m->path = path;
    if (is_root) {
        root_ = m;
        root_dir_ = path.parent_path();
    }
    m->key = keyFor(path, is_root);
    {
        std::string name = m->key;
        if (name.empty()) {
            name = path.stem().string();
        }
        std::replace(name.begin(), name.end(), '/', '.');
        m->mangle_name = name;
    }
    by_key_[m->key] = m;

    if (const auto it = virtual_files_.find(norm); it != virtual_files_.end()) {
        m->file = sources_.addFile(path, it->second);
    } else {
        m->file = sources_.loadFile(path);
    }

    // Pass 0 (silent): find the imports so exported macros of the dependencies can be seeded.
    std::ostringstream sink;
    DiagnosticEngine quiet(sources_, sink);
    MacroTable discard;  // pass 0 also collects the module's own macros: keep them out of the seed
    Program first = parseSource(sources_, m->file, config_, quiet, discard);
    MacroTable seed;
    std::set<const ModuleInfo*> seen;
    seen.insert(m);
    for (const Item& item : first.items) {
        std::string spec;
        Span at;
        if (const auto* imp = std::get_if<ImportDecl>(&item.decl)) {
            spec = imp->module;
            at = imp->module_span;
        } else if (const auto* ex = std::get_if<ExportFromDecl>(&item.decl)) {
            spec = ex->module;
            at = ex->module_span;
        } else {
            continue;
        }
        if (ModuleInfo* dep = importTarget(*m, spec, at)) {
            collectMacros(*dep, seed, seen);
            if (std::find(m->deps.begin(), m->deps.end(), dep) == m->deps.end()) {
                m->deps.push_back(dep);
            }
        }
    }

    // Pass 1: the real parse with the imported macros known.
    m->program = parseSource(sources_, m->file, config_, diag_, seed);
    m->sema = std::make_unique<Sema>(m->program, config_, diag_, history_);
    m->sema->attach(this, m->key);
    order_.push_back(m);
    return m;
}

ModuleInfo* Compilation::loadRoot(const std::filesystem::path& path) {
    return loadModule(path, true);
}

Sema* Compilation::semaOf(const std::string& key) {
    const auto it = by_key_.find(key);
    return it == by_key_.end() ? nullptr : it->second->sema.get();
}

ModuleInfo* Compilation::moduleOf(const std::string& key) {
    const auto it = by_key_.find(key);
    return it == by_key_.end() ? nullptr : it->second;
}

std::optional<Sema::EntityRef> Compilation::exportedEntity(const std::string& module_key, const std::string& name) {
    ModuleInfo* m = moduleOf(module_key);
    if (m == nullptr) {
        return std::nullopt;
    }
    const auto it = m->exports.find(name);
    if (it == m->exports.end() || it->second.is_namespace || it->second.module == nullptr) {
        return std::nullopt;
    }
    Sema* owner = it->second.module->sema.get();
    const Sema::Symbol* sym = owner->findLocal(it->second.name);
    if (sym == nullptr) {
        return std::nullopt;
    }
    return Sema::EntityRef{owner, sym, it->second.name};
}

std::optional<Sema::EntityRef> Compilation::findRegistered(const std::string& name) {
    const auto it = registered_.find(name);
    if (it == registered_.end()) {
        return std::nullopt;
    }
    return it->second;
}

// ---------------------------------------------------------------------------------------------
// Exports (plan 4.6a)

void Compilation::resolveExports(ModuleInfo& m) {
    if (m.exports_done || m.exports_in_progress) {
        return;
    }
    m.exports_in_progress = true;

    const auto add = [&](const std::string& name, ModuleInfo::Export e) {
        const auto [it, inserted] = m.exports.emplace(name, e);
        if (inserted) {
            return;
        }
        const ModuleInfo::Export& old = it->second;
        if (old.module == e.module && old.name == e.name && old.is_namespace == e.is_namespace) {
            return;  // the same entity exported twice
        }
        Diagnostic d;
        d.code = "E0401";
        d.severity = Severity::Error;
        d.message = std::format("`{}` is exported twice", name);
        d.labels.push_back({old.span, "first export"});
        d.labels.push_back({e.span, "second export"});
        d.fix = "resolve with `except`, `as`, or `export * as ns`";
        diag_.report(std::move(d));
    };

    for (const Item& item : m.program.items) {
        const bool exported = item.modifiers.exported || item.modifiers.export_c || item.modifiers.registered;
        if (!exported) {
            continue;
        }
        if (std::holds_alternative<ImportDecl>(item.decl) || std::holds_alternative<ExportFromDecl>(item.decl) ||
            std::holds_alternative<ImplDecl>(item.decl) || std::holds_alternative<MacroCallDecl>(item.decl) ||
            std::holds_alternative<TopLevelCallDecl>(item.decl)) {
            continue;
        }
        const DeclBase& base = declBase(item);
        ModuleInfo::Export e;
        e.module = &m;
        e.name = base.name;
        e.span = base.name_span;
        add(base.name, e);
    }

    for (const Item& item : m.program.items) {
        const auto* ex = std::get_if<ExportFromDecl>(&item.decl);
        if (ex == nullptr) {
            continue;
        }
        const auto it = targets_.find({ex->module, m.key});
        if (it == targets_.end() || it->second == nullptr) {
            continue;
        }
        ModuleInfo& target = *it->second;
        resolveExports(target);
        switch (ex->form) {
        case ExportFromDecl::Form::Star: {
            for (const auto& [name, e] : target.exports) {
                const bool skipped = std::ranges::any_of(ex->except, [&](const ImportName& x) { return x.name == name; });
                if (skipped) {
                    continue;
                }
                ModuleInfo::Export copy = e;
                copy.span = ex->module_span;
                add(name, copy);
            }
            break;
        }
        case ExportFromDecl::Form::StarAs: {
            ModuleInfo::Export e;
            e.is_namespace = true;
            e.module = &target;
            e.span = ex->module_span;
            add(ex->alias, e);
            break;
        }
        case ExportFromDecl::Form::Names: {
            for (const ImportName& n : ex->names) {
                const auto found = target.exports.find(n.name);
                if (found == target.exports.end()) {
                    diag_.error("E0711", n.span, std::format("`{}` is not exported by `{}`", n.name, ex->module),
                                "only `export`ed names can be re-exported");
                    continue;
                }
                ModuleInfo::Export copy = found->second;
                copy.span = n.span;
                add(n.alias.empty() ? n.name : n.alias, copy);
            }
            break;
        }
        }
    }
    m.exports_in_progress = false;
    m.exports_done = true;
}

// ---------------------------------------------------------------------------------------------
// Imports (plan 4.6a): bindings per module; conflicts are reported on the import line.

void Compilation::linkImports(ModuleInfo& m) {
    struct Bound {
        Span span;
        const ModuleInfo* module;  // defining module / namespace target
        std::string name;
        bool is_namespace;
    };
    std::map<std::string, Bound> bound;

    const auto bind = [&](const std::string& local, Sema::ImportBinding b, const ModuleInfo* module,
                          const std::string& name, bool is_ns, const Span& span) {
        if (const Sema::Symbol* own = m.sema->findLocal(local)) {
            Diagnostic d;
            d.code = "E0401";
            d.severity = Severity::Error;
            d.message = std::format("`{}` is defined here and imported", local);
            d.labels.push_back({declBase(*own->item).name_span, "local definition"});
            d.labels.push_back({span, "imported here"});
            d.fix = "rename the import with `as`";
            diag_.report(std::move(d));
            return;
        }
        const auto [it, inserted] = bound.emplace(local, Bound{span, module, name, is_ns});
        if (!inserted) {
            const Bound& old = it->second;
            Diagnostic d;
            d.severity = Severity::Error;
            d.labels.push_back({old.span, "first binding"});
            d.labels.push_back({span, "second binding"});
            if (old.module == module && old.name == name && old.is_namespace == is_ns) {
                d.code = "W0401";
                d.severity = Severity::Warning;
                d.message = std::format("`{}` is imported twice as the same entity", local);
                d.fix = "import it through one path, or silence with `@allow(W0401)`";
            } else {
                d.code = "E0401";
                d.message = std::format("different entities are bound to `{}`", local);
                d.fix = "rename one with `as`, or use `import * as ns from ...`";
                diag_.report(std::move(d));
                return;
            }
            diag_.report(std::move(d));
            return;
        }
        m.sema->bindImport(local, std::move(b));
    };

    const auto bindExport = [&](const std::string& local, const ModuleInfo::Export& e, const Span& span) {
        Sema::ImportBinding b;
        b.span = span;
        if (e.is_namespace) {
            b.kind = Sema::ImportBinding::Kind::Namespace;
            b.module_key = e.module->key;
            bind(local, std::move(b), e.module, {}, true, span);
        } else {
            b.kind = Sema::ImportBinding::Kind::Entity;
            b.module_key = e.module->key;
            b.name = e.name;
            bind(local, std::move(b), e.module, e.name, false, span);
        }
    };

    for (const Item& item : m.program.items) {
        const auto* imp = std::get_if<ImportDecl>(&item.decl);
        if (imp == nullptr) {
            continue;
        }
        const auto tit = targets_.find({imp->module, m.key});
        if (tit == targets_.end() || tit->second == nullptr) {
            continue;
        }
        ModuleInfo& target = *tit->second;
        resolveExports(target);
        switch (imp->form) {
        case ImportDecl::Form::Module:
        case ImportDecl::Form::StarAs: {
            ModuleInfo::Export ns;
            ns.is_namespace = true;
            ns.module = &target;
            const std::string name = !imp->alias.empty() ? imp->alias : lastSegment(imp->module);
            bindExport(name, ns, imp->module_span);
            break;
        }
        case ImportDecl::Form::Star:
            for (const auto& [name, e] : target.exports) {
                bindExport(name, e, imp->module_span);
            }
            break;
        case ImportDecl::Form::Names:
            for (const ImportName& n : imp->names) {
                const auto found = target.exports.find(n.name);
                if (found == target.exports.end()) {
                    diag_.error("E0711", n.span, std::format("`{}` is not exported by `{}`", n.name, imp->module),
                                "only names marked `export` can be imported");
                    continue;
                }
                bindExport(n.alias.empty() ? n.name : n.alias, found->second, n.span);
            }
            break;
        }
    }
}

void Compilation::collectRegistered() {
    for (ModuleInfo* m : order_) {
        for (const Item& item : m->program.items) {
            if (!item.modifiers.registered) {
                continue;
            }
            const DeclBase& base = declBase(item);
            const Sema::Symbol* sym = m->sema->findLocal(base.name);
            if (sym == nullptr) {
                continue;
            }
            const auto [it, inserted] =
                registered_.emplace(base.name, Sema::EntityRef{m->sema.get(), sym, base.name});
            if (!inserted) {
                Diagnostic d;
                d.code = "E0502";
                d.severity = Severity::Error;
                d.message = std::format("`{}` is registered twice", base.name);
                d.labels.push_back({declBase(*it->second.sym->item).name_span, "first registration"});
                d.labels.push_back({base.name_span, "second registration"});
                d.fix = "register it once; turn off the std feature that registers it if needed";
                diag_.report(std::move(d));
            }
        }
    }
}

void Compilation::finish() {
    if (!config_.no_std && root_ != nullptr) {
        const std::filesystem::path std_index = std_root_ / "index.xl";
        if (!std_root_.empty() && fileExists(std_index)) {
            ModuleInfo* std_module = loadModule(std_index, false);
            (void)std_module;
        } else {
            diag_.error("E0711", Span{}, "standard library not found",
                        "use `--no-std`, or `--std=<dir>` with a directory that holds `index.xl`");
        }
    }
    for (ModuleInfo* m : order_) {
        resolveExports(*m);
    }
    for (ModuleInfo* m : order_) {
        linkImports(*m);
    }
    collectRegistered();
}

void Compilation::analyzeAll() {
    for (ModuleInfo* m : order_) {
        m->sema->analyze();
    }
}

hir::Module Compilation::lowerAll() {
    std::vector<Sema*> modules;
    for (ModuleInfo* m : order_) {
        modules.push_back(m->sema.get());
    }
    hir::Module out = lowerProgram(modules);
    // Plan 4.6b: a `declare`d symbol nothing in this compilation defines is only a warning (it is
    // looked up at link time; with `--emit=obj` that is normal).
    std::set<std::string> defined;
    for (const hir::Function& f : out.functions) {
        if (!f.declared && f.has_body) {
            defined.insert(f.symbol);
        }
    }
    for (const hir::Global& g : out.globals) {
        if (!g.declared) {
            defined.insert(g.symbol);
        }
    }
    const auto warn = [&](const std::string& symbol, const Span& span, const char* what) {
        Diagnostic d;
        d.code = "W0301";
        d.severity = Severity::Warning;
        d.message = std::format("declared {} `{}` is not defined in this compilation", what, symbol);
        d.labels.push_back({span, "resolved at link time"});
        d.detail = "No module of this compilation defines this symbol.";
        d.reason = "The linker must find it in another object or library; with `--emit=obj` this is expected.";
        d.fix = "link the object/library that defines it, or silence with `@allow(W0301)`";
        diag_.report(std::move(d));
    };
    for (const hir::Function& f : out.functions) {
        if (f.declared && !defined.contains(f.symbol)) {
            warn(f.symbol, f.span, "function");
        }
    }
    for (const hir::Global& g : out.globals) {
        if (g.declared && !defined.contains(g.symbol)) {
            warn(g.symbol, g.span, "global");
        }
    }
    return out;
}

}  // namespace xlang
