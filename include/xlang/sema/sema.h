#pragma once

#include "xlang/lang/ast.h"
#include "xlang/hir/hir.h"
#include "xlang/lang/config.h"
#include "xlang/sema/layout.h"
#include "xlang/util/diagnostics.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace xlang {

class Lowerer;
class Compilation;

using Int128 = __int128;

/// Compile-time value (plan 3.6): literals, `define` constants, `-D` values, enum members and the
/// layout queries.
struct ConstValue {
    enum class Kind { Int, Float, Bool, String } kind{Kind::Int};
    Int128 int_value{0};
    double float_value{0.0};
    bool bool_value{false};
    std::string string_value;

    [[nodiscard]] static ConstValue makeInt(Int128 v);
    [[nodiscard]] static ConstValue makeFloat(double v);
    [[nodiscard]] static ConstValue makeBool(bool v);
    [[nodiscard]] static ConstValue makeString(std::string v);
    [[nodiscard]] std::string toString() const;
};

/// Single-module semantic services that do not need function bodies typed (plan phases 2 and
/// 3.6/3.8): name table, type alias resolution, enums, constant evaluation, layouts,
/// `staticAssert`. Cross-module names (`alias.Name`) stay unresolved until module loading.
class Sema {
public:
    Sema(const Program& program, const BuildConfig& config, DiagnosticEngine& diagnostics,
         const LayoutHistory* history = nullptr);

    /// Runs every check over the module: duplicate names, aliases, enums, array lengths in all
    /// types, layouts of all structs/classes (with `declare` warnings), module-level
    /// `staticAssert`s.
    void analyze();

    /// Follows aliases and turns `Named` into Struct/Class/Interface/Enum. Reports unknown
    /// names (E0706) and cyclic aliases (E0704); nullopt on failure.
    [[nodiscard]] std::optional<Type> resolve(const Type& type);

    [[nodiscard]] std::optional<ConstValue> evalConst(const Expr& expr);

    [[nodiscard]] std::optional<TypeLayout> layoutOf(const Type& type, const Span& at);
    /// Layout of the struct/class declared with `name`, nullopt when unknown or unsized.
    [[nodiscard]] std::optional<TypeLayout> layoutOfName(const std::string& name, const Span& at);

    /// Names of the struct and class declarations, in source order.
    [[nodiscard]] std::vector<std::string> aggregateNames() const;

    /// Value of `Enum.Member` (auto-numbering applied).
    [[nodiscard]] std::optional<ConstValue> enumMember(const std::string& enum_name,
                                                       const std::string& member, const Span& at);

    /// Plan 4.1-4.3: scopes, name resolution and type checking of every function body, lowered to
    /// HIR in the same pass. Run `analyze()` first; diagnostics go to the engine, the returned
    /// module is only meaningful when no error was reported.
    [[nodiscard]] hir::Module lower();

    [[nodiscard]] const BuildConfig& config() const { return config_; }

    // --- module awareness (plan 4.6); a standalone Sema has no compilation and resolves imports
    // --- as external names.

    struct Symbol {
        enum class Kind { Struct, Class, Interface, Enum, Alias, Define, Global, Fn, Macro } kind;
        const Item* item{nullptr};
    };

    /// An import binding made by the compilation: a name that stands for an entity of another
    /// module, or a whole module (`import "m" as a`).
    struct ImportBinding {
        enum class Kind { Entity, Namespace } kind{Kind::Entity};
        std::string module_key;  ///< defining module (Entity) / the module itself (Namespace)
        std::string name;        ///< Entity: name inside that module
        Span span;
    };

    /// An entity found through scopes: the module that owns it and its local name there.
    struct EntityRef {
        Sema* owner{nullptr};
        const Symbol* sym{nullptr};
        std::string local;
        [[nodiscard]] std::string qname() const;
    };

    void attach(Compilation* compilation, std::string module_key);
    void bindImport(const std::string& local_name, ImportBinding binding);
    [[nodiscard]] const std::map<std::string, ImportBinding>& imports() const { return imports_; }
    [[nodiscard]] const std::string& moduleKey() const { return key_; }
    [[nodiscard]] Compilation* compilation() const { return comp_; }
    [[nodiscard]] const Program& program() const { return program_; }
    /// `key::name` for imported modules, plain `name` for the root module and builtins.
    [[nodiscard]] std::string qualify(const std::string& local) const;
    /// Local definition, import, or registered global, in that order.
    [[nodiscard]] std::optional<EntityRef> lookupEntity(const std::string& name);
    /// `ns.Name`
    [[nodiscard]] std::optional<EntityRef> lookupQualified(const std::vector<std::string>& path);
    /// By qualified name (as stored in resolved types).
    [[nodiscard]] std::optional<EntityRef> entityByQName(const std::string& qname);
    [[nodiscard]] const Symbol* findLocal(const std::string& name) const { return find(name); }

private:
    friend class Lowerer;
    friend class Compilation;

    void collect();
    void checkType(const Type& type, const Span& at);
    void checkStatements(const Block& block);
    void checkExpr(const Expr& expr);
    void checkEnum(const EnumDecl& decl);
    void checkAggregate(const Item& item);
    void checkStaticAssert(const TopLevelCallDecl& call);
    void checkStaticAssertCall(const Expr& call);

    [[nodiscard]] const Symbol* find(const std::string& name) const;
    [[nodiscard]] std::optional<Type> exprToType(const Expr& expr);
    [[nodiscard]] std::optional<EntityRef> entityOfExpr(const Expr& expr);
    [[nodiscard]] std::optional<ConstValue> evalBinary(const Expr& expr);
    [[nodiscard]] std::optional<ConstValue> evalCast(const Expr& expr);
    [[nodiscard]] std::optional<ConstValue> evalCall(const Expr& expr);
    [[nodiscard]] std::optional<ConstValue> fromConfig(const ConfigValue& v);
    [[nodiscard]] std::optional<std::uint64_t> primitiveSize(const Type& t, std::uint64_t& align);
    [[nodiscard]] std::optional<TypeLayout> aggregateLayout(const std::string& name, const Item& item,
                                                            const Span& at);
    void notConstant(const Span& at, std::string why);
    void error(const char* code, const Span& at, std::string message, std::string label);

    const Program& program_;
    const BuildConfig& config_;
    DiagnosticEngine& diag_;
    const LayoutHistory* history_;

    Item memory_order_item_;
    std::map<std::string, Symbol> symbols_;
    std::vector<std::string> aggregate_order_;
    std::map<std::string, std::optional<ConstValue>> define_cache_;
    std::set<std::string> define_in_progress_;
    std::set<std::string> alias_in_progress_;
    std::map<std::string, std::optional<TypeLayout>> layout_cache_;
    std::set<std::string> layout_in_progress_;
    std::map<std::string, std::vector<std::optional<ConstValue>>> enum_values_;
    std::set<std::string> enum_in_progress_;
    std::set<std::string> reported_;  ///< dedup key for repeated diagnostics
    Compilation* comp_{nullptr};
    std::string key_;
    std::map<std::string, ImportBinding> imports_;
};

}  // namespace xlang
