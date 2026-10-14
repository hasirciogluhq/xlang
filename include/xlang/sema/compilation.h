#pragma once

#include "xlang/hir/hir.h"
#include "xlang/lang/config.h"
#include "xlang/lang/macro.h"
#include "xlang/sema/sema.h"
#include "xlang/util/diagnostics.h"
#include "xlang/util/source.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xlang {

/// One source module of the compilation (plan 4.6): its parsed program, its `Sema`, and what it
/// exports. `key` is "" for the root module and the module path (`std/mem`) otherwise.
struct ModuleInfo {
    struct Export {
        bool is_namespace{false};  ///< `export * as ns from "m"`: `module` is the namespaced module
        ModuleInfo* module{nullptr};  ///< defining module (entities) / target module (namespaces)
        std::string name;             ///< name inside `module` (entities)
        Span span;                    ///< where it is exported
    };

    std::string key;
    std::vector<ModuleInfo*> deps;  ///< modules this one imports / re-exports from
    std::string mangle_name;  ///< readable module name for symbol mangling (`std.mem`, root: file stem)
    std::filesystem::path path;
    FileId file{kNoFile};
    Program program;
    std::unique_ptr<Sema> sema;
    std::map<std::string, Export> exports;
    bool exports_done{false};
    bool exports_in_progress{false};
};

/// Whole-program driver of the semantic phases: loads modules, resolves exports/imports and
/// registrations, runs `Sema::analyze` on every module and lowers them to one HIR module.
class Compilation {
public:
    Compilation(SourceManager& sources, const BuildConfig& config, DiagnosticEngine& diagnostics,
                const LayoutHistory* history = nullptr);
    ~Compilation();

    /// Where `std`, `std/<x>` are looked up (`--std=<path>` or the installed library).
    void setStdRoot(std::filesystem::path root) { std_root_ = std::move(root); }
    /// In-memory file (tests): found by `loadRoot`/imports like a file on disk.
    void addVirtualFile(const std::string& path, std::string text);

    /// Loads the root module and everything it imports; call `finish()` afterwards.
    ModuleInfo* loadRoot(const std::filesystem::path& path);
    /// Loads the standard library root (unless `--no-std`), resolves exports and imports,
    /// collects `register`ed names.
    void finish();
    void analyzeAll();
    [[nodiscard]] hir::Module lowerAll();

    // --- services for `Sema` / `Lowerer`
    [[nodiscard]] Sema* semaOf(const std::string& key);
    [[nodiscard]] ModuleInfo* moduleOf(const std::string& key);
    [[nodiscard]] Sema* rootSema() { return root_ != nullptr ? root_->sema.get() : nullptr; }
    [[nodiscard]] ModuleInfo* root() { return root_; }
    [[nodiscard]] std::optional<Sema::EntityRef> exportedEntity(const std::string& module_key, const std::string& name);
    [[nodiscard]] std::optional<Sema::EntityRef> findRegistered(const std::string& name);
    /// Modules in dependency order (a module comes after the modules it imports).
    [[nodiscard]] const std::vector<ModuleInfo*>& order() const { return order_; }
    [[nodiscard]] const BuildConfig& config() const { return config_; }
    [[nodiscard]] DiagnosticEngine& diagnostics() { return diag_; }

private:
    [[nodiscard]] bool fileExists(const std::filesystem::path& p) const;
    [[nodiscard]] std::optional<std::filesystem::path> locate(const std::string& spec, const ModuleInfo* from,
                                                              const Span& at);
    ModuleInfo* loadModule(const std::filesystem::path& path, bool is_root);
    ModuleInfo* importTarget(ModuleInfo& from, const std::string& spec, const Span& at);
    [[nodiscard]] std::string keyFor(const std::filesystem::path& path, bool is_root) const;
    void collectMacros(ModuleInfo& dep, MacroTable& table, std::set<const ModuleInfo*>& seen);
    void resolveExports(ModuleInfo& m);
    void linkImports(ModuleInfo& m);
    void collectRegistered();

    SourceManager& sources_;
    const BuildConfig& config_;
    DiagnosticEngine& diag_;
    const LayoutHistory* history_;
    std::filesystem::path std_root_;
    std::filesystem::path root_dir_;
    std::map<std::string, std::string> virtual_files_;
    std::map<std::string, std::unique_ptr<ModuleInfo>> by_path_;
    std::map<std::string, ModuleInfo*> by_key_;
    std::vector<ModuleInfo*> order_;
    ModuleInfo* root_{nullptr};
    std::map<std::string, Sema::EntityRef> registered_;
    /// (module spec text, importing module key) -> module, so `finish` can look targets up again.
    std::map<std::pair<std::string, std::string>, ModuleInfo*> targets_;
};

}  // namespace xlang
