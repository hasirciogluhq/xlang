// Plan 4.1-4.3: scopes + name resolution + type checking of function bodies, lowered to HIR in
// one pass. The module-level services (type resolution, constants, layouts) stay in `Sema`.

#include "xlang/hir/hir.h"
#include "xlang/sema/conversion.h"
#include "xlang/sema/scope.h"
#include "xlang/sema/sema.h"

#include <algorithm>
#include <format>
#include <set>

namespace xlang {

namespace {

[[nodiscard]] Type unknownType() {
    return Type::makeNamed({});
}

[[nodiscard]] bool isUnknown(const Type& t) {
    return t.kind == TypeKind::Named;
}

[[nodiscard]] bool isNumeric(const Type& t) {
    return t.kind == TypeKind::Int || t.kind == TypeKind::Float;
}

[[nodiscard]] bool sameType(const Type& a, const Type& b) {
    return typeToString(a) == typeToString(b);
}

[[nodiscard]] std::string joinPath(const std::vector<std::string>& path) {
    std::string out;
    for (const std::string& p : path) {
        out += out.empty() ? p : "." + p;
    }
    return out;
}

[[nodiscard]] bool isAggregate(const Type& t) {
    return t.kind == TypeKind::Struct || t.kind == TypeKind::Class;
}

/// Maximum number of syscall arguments (after the number) for the target; nullopt = no fixed
/// limit in the target table (Windows NT: the compiler spills to the stack, plan 5.3).
[[nodiscard]] std::optional<unsigned> syscallMaxArgs(const TargetInfo& t) {
    if (t.os == "windows") {
        return std::nullopt;
    }
    return 6;
}

}  // namespace

class Lowerer {
public:
    explicit Lowerer(Sema& sema) : s_(sema) {}

    hir::Module run();

private:
    using EP = hir::ExprPtr;

    struct ImplInfo {
        const ImplDecl* decl;
        std::string iface;      ///< interface name as written
        std::string type_name;  ///< implementing struct/class
        std::string prefix;     ///< `Type.Iface.`
        bool iface_known{false};
    };

    struct MethodRef {
        enum class Kind { Direct, Virtual, Itable } kind{Kind::Direct};
        std::string fn_name;
        const FnDecl* decl{nullptr};
        std::uint32_t index{0};  ///< Itable: method index
        bool from_impl{false};
    };

    // --- setup
    void buildModuleScope();
    void checkRegistrations();
    void collectImpls();

    // --- module items
    void lowerItems();
    void lowerFunction(const FnDecl& f, const Modifiers& mods, const std::string& name,
                       const std::string& owner, const Type* self_type);
    void lowerGlobal(const GlobalDecl& g, const Modifiers& mods);
    void lowerEnum(const EnumDecl& e);

    // --- statements
    void lowerBlock(const Block& block, std::vector<hir::Stmt>& out, bool new_scope);
    void lowerStmt(const Stmt& s, std::vector<hir::Stmt>& out);
    hir::LocalId addLocal(const std::string& name, const Type& type, bool is_param, const Span& span);
    void bindLocal(const std::string& name, hir::LocalId id, const Type& type, const Span& span);

    // --- expressions
    EP expr(const Expr& e, const Type* expected);
    EP nameExpr(const Expr& e, const Type* expected);
    EP fieldExpr(const Expr& e);
    EP unaryExpr(const Expr& e, const Type* expected);
    EP binaryExpr(const Expr& e, const Type* expected);
    EP castExpr(const Expr& e);
    EP callExpr(const Expr& e, const Type* expected);
    EP methodCall(const Expr& e, const Expr& recv_ast);
    EP syscallExpr(const Expr& e);
    EP indexExpr(const Expr& e);
    EP arrayExpr(const Expr& e, const Type* expected);
    EP structLiteral(const Expr& e);
    EP newExpr(const Expr& e);
    EP constExpr(const Expr& e, const Type* expected);
    EP fromConst(const ConstValue& v, const Type* expected, const Span& span);

    bool lowerArgs(const Expr& call, const std::vector<Type>& params, std::vector<EP>& out,
                   const std::string& callee);
    void lowerFields(const Expr& e, const Type& agg, std::vector<hir::FieldInit>& out);

    EP coerce(EP value, const Type& to, const Span& at);
    EP castTo(EP value, const Type& to, bool implicit);
    EP toInterface(EP value, const Type& iface, const Span& at);
    [[nodiscard]] bool castable(const Type& from, const Type& to) const;

    // --- queries
    [[nodiscard]] Type resolved(const Type& t);
    [[nodiscard]] Type normalized(const Type& t);
    [[nodiscard]] Type fnType(const FnDecl& f);
    [[nodiscard]] std::optional<MethodRef> findMethod(const Type& agg, const std::string& name);
    [[nodiscard]] const ImplInfo* findImpl(const std::string& type_name, const std::string& iface) const;
    [[nodiscard]] bool hasAllocMethod(const Type& t);
    [[nodiscard]] const ClassDecl* classDecl(const std::string& name) const;
    [[nodiscard]] const Item* aggregateItem(const std::string& name) const;
    void collectFields(const std::string& name, std::vector<const FieldDecl*>& out) const;
    [[nodiscard]] bool isConstValue(const hir::Expr& e, const hir::Expr** offender) const;
    [[nodiscard]] std::optional<std::uint64_t> arrayLength(const Type& t);

    // --- helpers
    [[nodiscard]] EP mk(hir::ExprKind kind, Type type, const Span& span) const;
    [[nodiscard]] EP intConst(std::uint64_t v, Type type, const Span& span) const;
    void err(const char* code, const Span& at, std::string message, std::string label);
    [[nodiscard]] bool inUnsafe() const { return unsafe_ > 0; }
    [[nodiscard]] unsigned ptrBits() const { return s_.config_.target.pointer_width; }

    Sema& s_;
    hir::Module mod_;
    ScopeStack scopes_;
    hir::Function* fn_{nullptr};
    Type ret_;
    int unsafe_{0};
    bool star_import_{false};
    std::set<std::string> registered_names_;
    std::vector<ImplInfo> impls_;
};

// ---------------------------------------------------------------------------------------------
// Helpers

void Lowerer::err(const char* code, const Span& at, std::string message, std::string label) {
    s_.error(code, at, std::move(message), std::move(label));
}

Lowerer::EP Lowerer::mk(hir::ExprKind kind, Type type, const Span& span) const {
    auto e = std::make_unique<hir::Expr>();
    e->kind = kind;
    e->type = std::move(type);
    e->span = span;
    return e;
}

Lowerer::EP Lowerer::intConst(std::uint64_t v, Type type, const Span& span) const {
    EP e = mk(hir::ExprKind::IntConst, std::move(type), span);
    e->int_value = v;
    return e;
}

Type Lowerer::resolved(const Type& t) {
    auto r = s_.resolve(t);
    if (!r) {
        return unknownType();
    }
    return normalized(*r);
}

/// HIR array types carry a literal length, never a `define` name.
Type Lowerer::normalized(const Type& t) {
    Type out = t;
    if (t.kind == TypeKind::Array && t.length != nullptr) {
        if (const auto n = arrayLength(t)) {
            auto len = std::make_shared<Expr>();
            len->kind = Expr::Kind::IntLiteral;
            len->int_value = *n;
            len->span = t.length->span;
            out.length = len;
        }
    }
    if ((t.kind == TypeKind::Array || t.kind == TypeKind::Pointer || t.kind == TypeKind::Enum) && t.elem) {
        out.elem = std::make_shared<const Type>(normalized(*t.elem));
    }
    if (t.kind == TypeKind::FnPtr) {
        for (Type& p : out.params) {
            p = normalized(p);
        }
        if (t.ret) {
            out.ret = std::make_shared<const Type>(normalized(*t.ret));
        }
    }
    return out;
}

Type Lowerer::fnType(const FnDecl& f) {
    std::vector<Type> params;
    for (const Param& p : f.params) {
        if (!p.is_self) {
            params.push_back(resolved(p.type));
        }
    }
    return Type::makeFnPtr(std::move(params), resolved(f.return_type));
}

const Item* Lowerer::aggregateItem(const std::string& name) const {
    const Sema::Symbol* sym = s_.find(name);
    if (sym == nullptr || (sym->kind != Sema::Symbol::Kind::Struct && sym->kind != Sema::Symbol::Kind::Class)) {
        return nullptr;
    }
    return sym->item;
}

const ClassDecl* Lowerer::classDecl(const std::string& name) const {
    const Item* item = aggregateItem(name);
    return item != nullptr ? std::get_if<ClassDecl>(&item->decl) : nullptr;
}

void Lowerer::collectFields(const std::string& name, std::vector<const FieldDecl*>& out) const {
    const Item* item = aggregateItem(name);
    if (item == nullptr) {
        return;
    }
    if (const auto* s = std::get_if<StructDecl>(&item->decl)) {
        for (const FieldDecl& f : s->fields) {
            out.push_back(&f);
        }
        return;
    }
    const auto& c = std::get<ClassDecl>(item->decl);
    if (c.has_base && c.base.size() == 1 && c.base[0] != name) {
        collectFields(c.base[0], out);
    }
    for (const FieldDecl& f : c.fields) {
        out.push_back(&f);
    }
}

std::optional<std::uint64_t> Lowerer::arrayLength(const Type& t) {
    if (t.length == nullptr) {
        return std::nullopt;
    }
    const auto v = s_.evalConst(*t.length);
    if (!v || v->kind != ConstValue::Kind::Int || v->int_value < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(v->int_value);
}

const Lowerer::ImplInfo* Lowerer::findImpl(const std::string& type_name, const std::string& iface) const {
    for (const ImplInfo& i : impls_) {
        if (i.type_name == type_name && i.iface == iface) {
            return &i;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Module scope, registrations, impls

void Lowerer::buildModuleScope() {
    scopes_.push(ScopeKind::Module);
    {
        ScopeEntry e;
        e.kind = ScopeEntry::Kind::Type;
        e.name = "MemoryOrder";
        e.item = &s_.memory_order_item_;
        (void)scopes_.declare(std::move(e));
    }
    for (const Item& item : s_.program_.items) {
        const DeclBase& base = declBase(item);
        ScopeEntry e;
        e.name = base.name;
        e.span = base.name_span;
        e.item = &item;
        if (item.modifiers.registered) {
            registered_names_.insert(base.name);
        }
        if (const auto* imp = std::get_if<ImportDecl>(&item.decl)) {
            const auto bind = [&](ScopeEntry entry) {
                if (const ScopeEntry* prev = scopes_.declare(entry)) {
                    Diagnostic d;
                    d.code = "E0401";
                    d.severity = Severity::Error;
                    d.message = std::format("`{}` is bound twice", entry.name);
                    d.labels.push_back({prev->span, "first binding"});
                    d.labels.push_back({entry.span, "second binding"});
                    d.fix = "rename one of them with `as`";
                    s_.diag_.report(std::move(d));
                }
            };
            switch (imp->form) {
            case ImportDecl::Form::Module: {
                ScopeEntry ns;
                ns.kind = ScopeEntry::Kind::Namespace;
                ns.name = imp->alias.empty() ? imp->module : imp->alias;
                ns.module = imp->module;
                ns.span = imp->module_span;
                bind(std::move(ns));
                break;
            }
            case ImportDecl::Form::StarAs: {
                ScopeEntry ns;
                ns.kind = ScopeEntry::Kind::Namespace;
                ns.name = imp->alias;
                ns.module = imp->module;
                ns.span = imp->module_span;
                bind(std::move(ns));
                break;
            }
            case ImportDecl::Form::Star:
                star_import_ = true;
                break;
            case ImportDecl::Form::Names:
                for (const ImportName& n : imp->names) {
                    ScopeEntry ext;
                    ext.kind = ScopeEntry::Kind::External;
                    ext.name = n.alias.empty() ? n.name : n.alias;
                    ext.module = imp->module;
                    ext.member = n.name;
                    ext.span = n.span;
                    bind(std::move(ext));
                }
                break;
            }
            continue;
        }
        if (std::holds_alternative<FnDecl>(item.decl)) {
            e.kind = ScopeEntry::Kind::Function;
        } else if (const auto* g = std::get_if<GlobalDecl>(&item.decl)) {
            e.kind = ScopeEntry::Kind::Global;
            e.type = resolved(g->type);
        } else if (std::holds_alternative<DefineConstDecl>(item.decl)) {
            e.kind = ScopeEntry::Kind::Constant;
        } else if (std::holds_alternative<StructDecl>(item.decl) || std::holds_alternative<ClassDecl>(item.decl) ||
                   std::holds_alternative<InterfaceDecl>(item.decl) || std::holds_alternative<EnumDecl>(item.decl) ||
                   std::holds_alternative<TypeAliasDecl>(item.decl)) {
            e.kind = ScopeEntry::Kind::Type;
        } else {
            continue;
        }
        // Duplicate definitions were already reported by `Sema::collect` (E0401).
        (void)scopes_.declare(std::move(e));
    }
}

void Lowerer::collectImpls() {
    for (const Item& item : s_.program_.items) {
        const auto* impl = std::get_if<ImplDecl>(&item.decl);
        if (impl == nullptr) {
            continue;
        }
        const Type target = resolved(impl->target);
        if (!isAggregate(target)) {
            if (!isUnknown(target)) {
                err("E0706", impl->span, std::format("`{}` cannot implement an interface", typeToString(target)),
                    "only structs and classes have `impl`");
            }
            continue;
        }
        ImplInfo info;
        info.decl = impl;
        info.iface = joinPath(impl->interface_path);
        info.type_name = target.path.front();
        info.prefix = info.type_name + "." + info.iface + ".";
        if (impl->interface_path.size() == 1) {
            const Sema::Symbol* sym = s_.find(impl->interface_path[0]);
            if (sym == nullptr || sym->kind != Sema::Symbol::Kind::Interface) {
                err("E0706", impl->name_span.valid() ? impl->name_span : impl->span,
                    std::format("`{}` is not an interface", info.iface), "not declared as an interface in this module");
                continue;
            }
            info.iface_known = true;
        }
        impls_.push_back(std::move(info));
    }
}

bool Lowerer::hasAllocMethod(const Type& t) {
    const Type* agg = &t;
    if (t.kind == TypeKind::Pointer && t.elem) {
        agg = t.elem.get();
    }
    if (agg->kind == TypeKind::Interface) {
        return findMethod(*agg, "alloc").has_value();
    }
    if (isAggregate(*agg)) {
        const auto m = findMethod(*agg, "alloc");
        return m && m->from_impl;
    }
    return false;
}

void Lowerer::checkRegistrations() {
    struct First {
        Span span;
        std::string name;
    };
    std::map<std::string, First> roles;
    for (const Item& item : s_.program_.items) {
        if (!item.modifiers.registered || item.modifiers.register_role.empty()) {
            continue;
        }
        const DeclBase& base = declBase(item);
        const std::string& role = item.modifiers.register_role;
        const auto* fn = std::get_if<FnDecl>(&item.decl);
        const auto* global = std::get_if<GlobalDecl>(&item.decl);
        bool ok = false;
        std::string why;
        if (role == "panic") {
            ok = fn != nullptr && fn->params.size() == 1 && !fn->params[0].is_self && fn->return_type.isVoid();
            if (ok) {
                const Type p = resolved(fn->params[0].type);
                ok = p.kind == TypeKind::Pointer && p.elem && p.elem->kind == TypeKind::Int && p.elem->bits == 8 &&
                     !p.elem->is_signed;
            }
            why = "`register(panic)` needs `fn(msg: *ui8)` with no return type";
        } else if (role == "allocator") {
            if (global != nullptr) {
                const Type t = resolved(global->type);
                ok = isUnknown(t) || t.kind == TypeKind::Interface || hasAllocMethod(t);
            }
            why = "`register(allocator)` needs a global whose type is an interface or has an `impl` with `alloc`";
        } else {
            why = std::format("unknown role `{}`; known roles: panic, allocator", role);
        }
        if (!ok) {
            err("E0503", base.name_span, std::format("`{}` does not fit role `{}`", base.name, role), why);
            continue;
        }
        const auto [it, inserted] = roles.emplace(role, First{base.name_span, base.name});
        if (!inserted) {
            Diagnostic d;
            d.code = "E0502";
            d.severity = Severity::Error;
            d.message = std::format("role `{}` is registered twice", role);
            d.labels.push_back({it->second.span, "first registration"});
            d.labels.push_back({base.name_span, "second registration"});
            d.fix = "keep one registration per role";
            s_.diag_.report(std::move(d));
            continue;
        }
        (role == "panic" ? mod_.panic_function : mod_.allocator_global) = base.name;
    }
}

// ---------------------------------------------------------------------------------------------
// Items

void Lowerer::lowerEnum(const EnumDecl& decl) {
    hir::Enum out;
    out.name = decl.name;
    out.underlying = resolved(decl.underlying);
    for (const EnumMember& m : decl.members) {
        hir::EnumMemberValue v;
        v.name = m.name;
        if (const auto c = s_.enumMember(decl.name, m.name, m.span)) {
            if (c->kind == ConstValue::Kind::Int) {
                v.int_value = static_cast<std::int64_t>(c->int_value);
            } else {
                v.text = c->toString();
            }
        }
        out.members.push_back(std::move(v));
    }
    mod_.enums.push_back(std::move(out));
}

void Lowerer::lowerGlobal(const GlobalDecl& g, const Modifiers& mods) {
    hir::Global out;
    out.name = g.name;
    out.type = resolved(g.type);
    out.exported = mods.exported || mods.export_c;
    out.export_c = mods.export_c;
    out.declared = mods.declared;
    out.registered = mods.registered;
    out.register_role = mods.register_role;
    out.span = g.span;
    if (g.init) {
        EP init = coerce(expr(*g.init, &out.type), out.type, g.init->span);
        if (init) {
            const hir::Expr* offender = nullptr;
            if (!isConstValue(*init, &offender)) {
                const hir::Expr& at = offender != nullptr ? *offender : *init;
                err("E0707", at.span, "global initializer is not a compile-time constant",
                    "globals are statically initialized: no calls, no reads of other globals");
            }
            out.init = std::move(init);
        }
    }
    mod_.globals.push_back(std::move(out));
}

void Lowerer::lowerFunction(const FnDecl& f, const Modifiers& mods, const std::string& name,
                            const std::string& owner, const Type* self_type) {
    hir::Function out;
    out.name = name;
    out.owner = owner;
    out.exported = mods.exported || mods.export_c;
    out.export_c = mods.export_c;
    out.declared = mods.declared;
    out.registered = mods.registered;
    out.register_role = mods.register_role;
    out.is_virtual = f.is_virtual;
    out.is_override = f.is_override;
    out.has_body = f.has_body;
    out.span = f.span;
    out.ret = resolved(f.return_type);

    fn_ = &out;
    ret_ = out.ret;
    unsafe_ = 0;
    scopes_.push(ScopeKind::Function);
    for (const Param& p : f.params) {
        Type t;
        if (p.is_self) {
            if (self_type == nullptr) {
                err("E0709", p.span, "`self` outside of a method", "only class and `impl` methods have `self`");
                t = unknownType();
            } else {
                t = *self_type;
            }
        } else {
            t = resolved(p.type);
        }
        const hir::LocalId id = addLocal(p.is_self ? "self" : p.name, t, true, p.span);
        bindLocal(p.is_self ? "self" : p.name, id, t, p.span);
    }
    out.param_count = static_cast<std::uint32_t>(out.locals.size());
    if (f.has_body) {
        lowerBlock(f.body, out.body, false);
    }
    scopes_.pop();
    fn_ = nullptr;
    mod_.functions.push_back(std::move(out));
}

void Lowerer::lowerItems() {
    for (const Item& item : s_.program_.items) {
        const DeclBase& base = declBase(item);
        if (const auto* e = std::get_if<EnumDecl>(&item.decl)) {
            lowerEnum(*e);
        } else if (const auto* st = std::get_if<StructDecl>(&item.decl)) {
            hir::Aggregate a;
            a.name = st->name;
            a.opaque = st->opaque;
            a.span = st->span;
            for (const FieldDecl& f : st->fields) {
                a.fields.push_back({f.name, resolved(f.type)});
            }
            mod_.aggregates.push_back(std::move(a));
        } else if (const auto* cl = std::get_if<ClassDecl>(&item.decl)) {
            hir::Aggregate a;
            a.name = cl->name;
            a.is_class = true;
            a.opaque = cl->opaque;
            a.span = cl->span;
            if (cl->has_base) {
                a.base = joinPath(cl->base);
            }
            for (const FieldDecl& f : cl->fields) {
                a.fields.push_back({f.name, resolved(f.type)});
            }
            for (const FnDecl& m : cl->methods) {
                a.methods.push_back(cl->name + "." + m.name);
            }
            mod_.aggregates.push_back(std::move(a));
        } else if (const auto* in = std::get_if<InterfaceDecl>(&item.decl)) {
            hir::Interface i;
            i.name = in->name;
            i.span = in->span;
            for (const FnDecl& m : in->methods) {
                hir::InterfaceMethod im;
                im.name = m.name;
                for (const Param& p : m.params) {
                    if (!p.is_self) {
                        im.params.push_back(resolved(p.type));
                    }
                }
                im.ret = resolved(m.return_type);
                i.methods.push_back(std::move(im));
            }
            mod_.interfaces.push_back(std::move(i));
        } else if (const auto* g = std::get_if<GlobalDecl>(&item.decl)) {
            lowerGlobal(*g, item.modifiers);
        } else if (const auto* f = std::get_if<FnDecl>(&item.decl)) {
            lowerFunction(*f, item.modifiers, f->name, {}, nullptr);
        }
        (void)base;
    }
    // Methods after all signatures/types exist.
    for (const Item& item : s_.program_.items) {
        if (const auto* cl = std::get_if<ClassDecl>(&item.decl)) {
            const Type self = Type::makePointer(resolved(Type::makeNamed({cl->name})));
            for (const FnDecl& m : cl->methods) {
                lowerFunction(m, item.modifiers, cl->name + "." + m.name, cl->name, &self);
            }
        }
    }
    for (const ImplInfo& info : impls_) {
        const Type self = Type::makePointer(resolved(Type::makeNamed({info.type_name})));
        hir::Impl out;
        out.interface = info.iface;
        out.type = info.type_name;
        out.span = info.decl->span;
        for (const FnDecl& m : info.decl->methods) {
            out.methods.push_back(info.prefix + m.name);
            lowerFunction(m, Modifiers{}, info.prefix + m.name, info.type_name, &self);
        }
        mod_.impls.push_back(std::move(out));
    }
}

// ---------------------------------------------------------------------------------------------
// Statements

hir::LocalId Lowerer::addLocal(const std::string& name, const Type& type, bool is_param, const Span& span) {
    fn_->locals.push_back(hir::Local{name, type, is_param, span});
    return static_cast<hir::LocalId>(fn_->locals.size() - 1);
}

void Lowerer::bindLocal(const std::string& name, hir::LocalId id, const Type& type, const Span& span) {
    if (registered_names_.contains(name)) {
        s_.diag_.warning("W0402", span, std::format("`{}` shadows a registered global", name),
                         "a `register`ed name is hidden here; its role binding is unaffected");
    }
    ScopeEntry e;
    e.kind = ScopeEntry::Kind::Local;
    e.name = name;
    e.type = type;
    e.span = span;
    e.local = id;
    if (const ScopeEntry* prev = scopes_.declare(std::move(e))) {
        Diagnostic d;
        d.code = "E0401";
        d.severity = Severity::Error;
        d.message = std::format("`{}` is defined twice in this scope", name);
        d.labels.push_back({prev->span, "first definition"});
        d.labels.push_back({span, "second definition"});
        d.fix = "rename one of them";
        s_.diag_.report(std::move(d));
    }
}

void Lowerer::lowerBlock(const Block& block, std::vector<hir::Stmt>& out, bool new_scope) {
    if (new_scope) {
        scopes_.push(ScopeKind::Block);
    }
    for (const Stmt& s : block.statements) {
        lowerStmt(s, out);
    }
    if (new_scope) {
        scopes_.pop();
    }
}

void Lowerer::lowerStmt(const Stmt& s, std::vector<hir::Stmt>& out) {
    hir::Stmt h;
    h.span = s.span;
    const Type boolean = Type::makeBool();
    switch (s.kind) {
    case Stmt::Kind::Local: {
        Type t;
        EP init;
        if (s.has_type) {
            t = resolved(s.type);
            if (s.expr) {
                init = coerce(expr(*s.expr, &t), t, s.expr->span);
            }
        } else if (s.expr) {
            init = expr(*s.expr, nullptr);
            t = init ? init->type : unknownType();
            if (init && t.isVoid()) {
                err("E0708", s.expr->span, std::format("`{}` cannot be bound to a `void` value", s.name),
                    "the expression has no value");
                init.reset();
                t = unknownType();
            }
        } else {
            err("E0708", s.span, std::format("cannot infer the type of `{}`", s.name),
                "write a type (`local x: T`) or an initial value");
            t = unknownType();
        }
        h.kind = hir::StmtKind::Local;
        h.local = addLocal(s.name, t, false, s.span);
        h.value = std::move(init);
        bindLocal(s.name, h.local, t, s.span);  // after the initializer: `local x = x` sees the outer `x`
        break;
    }
    case Stmt::Kind::Assign: {
        EP target = expr(*s.target, nullptr);
        if (target && !target->lvalue && !isUnknown(target->type)) {
            err("E0709", s.target->span, "cannot assign to this expression", "not a place (variable, `*p`, field or element)");
            target.reset();
        }
        const Type want = target ? target->type : unknownType();
        EP value = expr(*s.expr, target ? &want : nullptr);
        if (target && value) {
            value = coerce(std::move(value), want, s.expr->span);
        }
        if (!target || !value) {
            return;
        }
        h.kind = hir::StmtKind::Assign;
        h.target = std::move(target);
        h.value = std::move(value);
        break;
    }
    case Stmt::Kind::Return: {
        h.kind = hir::StmtKind::Return;
        if (s.expr) {
            if (ret_.isVoid()) {
                err("E0708", s.expr->span, "a `void` function cannot return a value", "remove the value or add `-> T`");
                (void)expr(*s.expr, nullptr);
            } else {
                h.value = coerce(expr(*s.expr, &ret_), ret_, s.expr->span);
                if (!h.value) {
                    return;
                }
            }
        } else if (!ret_.isVoid() && !isUnknown(ret_)) {
            err("E0708", s.span, std::format("`return` needs a value of type `{}`", typeToString(ret_)), "missing value");
        }
        break;
    }
    case Stmt::Kind::Expr: {
        if (s.expr->kind == Expr::Kind::Call && s.expr->lhs->kind == Expr::Kind::Name &&
            s.expr->lhs->text == "staticAssert" && scopes_.lookup("staticAssert") == nullptr) {
            s_.checkStaticAssertCall(*s.expr);
            return;
        }
        EP v = expr(*s.expr, nullptr);
        if (!v) {
            return;
        }
        h.kind = hir::StmtKind::Expr;
        h.value = std::move(v);
        break;
    }
    case Stmt::Kind::If: {
        h.kind = hir::StmtKind::If;
        h.value = coerce(expr(*s.expr, &boolean), boolean, s.expr->span);
        lowerBlock(*s.body, h.body, true);
        if (s.else_body) {
            lowerBlock(*s.else_body, h.else_body, true);
        }
        if (!h.value) {
            return;
        }
        break;
    }
    case Stmt::Kind::While: {
        h.kind = hir::StmtKind::While;
        h.value = coerce(expr(*s.expr, &boolean), boolean, s.expr->span);
        lowerBlock(*s.body, h.body, true);
        if (!h.value) {
            return;
        }
        break;
    }
    case Stmt::Kind::Unsafe:
        ++unsafe_;
        h.kind = hir::StmtKind::Block;
        lowerBlock(*s.body, h.body, true);
        --unsafe_;
        break;
    case Stmt::Kind::Block:
        h.kind = hir::StmtKind::Block;
        lowerBlock(*s.body, h.body, true);
        break;
    }
    out.push_back(std::move(h));
}

// ---------------------------------------------------------------------------------------------
// Conversions

bool Lowerer::castable(const Type& from, const Type& to) const {
    if (isUnknown(from) || isUnknown(to) || sameType(from, to)) {
        return true;
    }
    const auto scalar = [](const Type& t) {
        return t.kind == TypeKind::Int || t.kind == TypeKind::Float || t.kind == TypeKind::Bool;
    };
    const auto addr = [](const Type& t) { return t.kind == TypeKind::Pointer || t.kind == TypeKind::FnPtr; };
    if (from.kind == TypeKind::Enum && from.elem) {
        return castable(*from.elem, to);
    }
    if (to.kind == TypeKind::Enum && to.elem) {
        return castable(from, *to.elem);
    }
    if (scalar(from) && scalar(to)) {
        return true;
    }
    if (addr(from) && addr(to)) {
        return true;
    }
    if ((addr(from) && to.kind == TypeKind::Int) || (from.kind == TypeKind::Int && addr(to))) {
        return true;
    }
    return false;
}

Lowerer::EP Lowerer::castTo(EP value, const Type& to, bool implicit) {
    if (!value || sameType(value->type, to)) {
        return value;
    }
    EP c = mk(hir::ExprKind::Cast, to, value->span);
    c->implicit = implicit;
    c->lhs = std::move(value);
    return c;
}

Lowerer::EP Lowerer::toInterface(EP value, const Type& iface, const Span& at) {
    // `value` is a struct/class value (an lvalue) or a pointer to one.
    const std::string name = iface.path.empty() ? std::string{} : iface.path.front();
    EP pointer;
    std::string type_name;
    if (value->type.kind == TypeKind::Pointer && value->type.elem && isAggregate(*value->type.elem)) {
        type_name = value->type.elem->path.front();
        pointer = std::move(value);
    } else {
        type_name = value->type.path.front();
        if (!value->lvalue) {
            err("E0709", at, "cannot convert a temporary to an interface", "the interface value points at the object; store it in a variable first");
            return nullptr;
        }
        EP addr = mk(hir::ExprKind::AddrOf, Type::makePointer(value->type), at);
        addr->lhs = std::move(value);
        pointer = std::move(addr);
    }
    if (findImpl(type_name, name) == nullptr) {
        Diagnostic d;
        d.code = "E0504";
        d.severity = Severity::Error;
        d.message = std::format("`{}` does not implement `{}`", type_name, name);
        d.labels.push_back({at, std::format("`{}` -> `{}` needs an `impl`", type_name, name)});
        d.detail = std::format("No `impl {} for {}` was found.", name, type_name);
        d.reason = "Interfaces are nominal; matching method names never count.";
        d.fix = std::format("add `impl {} for {} {{ ... }}`", name, type_name);
        s_.diag_.report(std::move(d));
        return nullptr;
    }
    EP out = mk(hir::ExprKind::ToInterface, iface, at);
    out->text = type_name;
    out->lhs = std::move(pointer);
    return out;
}

Lowerer::EP Lowerer::coerce(EP value, const Type& to, const Span& at) {
    if (!value) {
        return nullptr;
    }
    if (isUnknown(value->type) || isUnknown(to)) {
        return value;
    }
    if (value->kind == hir::ExprKind::Nil) {
        if (to.kind == TypeKind::Pointer || to.kind == TypeKind::FnPtr) {
            value->type = to;
            return value;
        }
        err("E0708", at, std::format("`nil` cannot be used as `{}`", typeToString(to)), "only pointers can be `nil`");
        return nullptr;
    }
    if (to.kind == TypeKind::Interface) {
        if (value->type.kind == TypeKind::Interface) {
            if (sameType(value->type, to)) {
                return value;
            }
            err("E0708", at, std::format("expected `{}`, found `{}`", typeToString(to), typeToString(value->type)),
                "different interfaces");
            return nullptr;
        }
        const bool agg = isAggregate(value->type) ||
                         (value->type.kind == TypeKind::Pointer && value->type.elem && isAggregate(*value->type.elem));
        if (agg) {
            return toInterface(std::move(value), to, at);
        }
    }
    const ConversionResult r = implicitConversion(value->type, to, inUnsafe(), ptrBits());
    switch (r.status) {
    case ConversionStatus::Ok:
        return castTo(std::move(value), to, true);
    case ConversionStatus::Warn:
        s_.diag_.warning("W0601", at,
                         std::format("`{}` is implicitly converted to `{}`, precision may be lost",
                                     typeToString(value->type), typeToString(to)),
                         "write `as` to silence");
        return castTo(std::move(value), to, true);
    case ConversionStatus::Signedness:
        err("E0601", at,
            std::format("implicit conversion from `{}` to `{}` changes signedness", typeToString(value->type),
                        typeToString(to)),
            "write `as`, or use `unsafe { }`");
        return nullptr;
    case ConversionStatus::NeedsCast:
        err("E0708", at,
            std::format("expected `{}`, found `{}`", typeToString(to), typeToString(value->type)),
            r.note.empty() ? "an explicit `as` is required" : r.note);
        return nullptr;
    case ConversionStatus::Invalid:
        err("E0708", at, std::format("expected `{}`, found `{}`", typeToString(to), typeToString(value->type)),
            "no conversion exists");
        return nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Expressions

Lowerer::EP Lowerer::fromConst(const ConstValue& v, const Type* expected, const Span& span) {
    Type want = expected != nullptr ? resolved(*expected) : unknownType();
    switch (v.kind) {
    case ConstValue::Kind::Int: {
        if (want.kind == TypeKind::Float) {
            EP e = mk(hir::ExprKind::FloatConst, want, span);
            e->float_value = static_cast<double>(v.int_value);
            return e;
        }
        Type t = (want.kind == TypeKind::Int || want.kind == TypeKind::Enum) ? want
                 : (v.int_value > 2147483647 || v.int_value < -2147483648LL) ? Type::makeInt(64, true)
                                                                              : Type::makeInt(32, true);
        return intConst(static_cast<std::uint64_t>(v.int_value), std::move(t), span);
    }
    case ConstValue::Kind::Float: {
        EP e = mk(hir::ExprKind::FloatConst, want.kind == TypeKind::Float ? want : Type::makeFloat(64), span);
        e->float_value = v.float_value;
        return e;
    }
    case ConstValue::Kind::Bool: {
        EP e = mk(hir::ExprKind::BoolConst, Type::makeBool(), span);
        e->bool_value = v.bool_value;
        return e;
    }
    case ConstValue::Kind::String: {
        EP e = mk(hir::ExprKind::StrConst, Type::makePointer(Type::makeInt(8, false)), span);
        e->text = v.string_value;
        return e;
    }
    }
    return nullptr;
}

Lowerer::EP Lowerer::constExpr(const Expr& e, const Type* expected) {
    const auto v = s_.evalConst(e);
    return v ? fromConst(*v, expected, e.span) : nullptr;
}

Lowerer::EP Lowerer::expr(const Expr& e, const Type* expected) {
    switch (e.kind) {
    case Expr::Kind::IntLiteral: {
        Type want = expected != nullptr ? resolved(*expected) : unknownType();
        if (want.kind == TypeKind::Float) {
            EP f = mk(hir::ExprKind::FloatConst, want, e.span);
            f->float_value = static_cast<double>(e.int_value);
            return f;
        }
        Type t = want.kind == TypeKind::Int ? want
                 : e.int_value > 2147483647ULL ? Type::makeInt(64, true) : Type::makeInt(32, true);
        return intConst(e.int_value, std::move(t), e.span);
    }
    case Expr::Kind::FloatLiteral: {
        Type want = expected != nullptr ? resolved(*expected) : unknownType();
        EP f = mk(hir::ExprKind::FloatConst, want.kind == TypeKind::Float ? want : Type::makeFloat(64), e.span);
        f->float_value = e.float_value;
        return f;
    }
    case Expr::Kind::BoolLiteral: {
        EP b = mk(hir::ExprKind::BoolConst, Type::makeBool(), e.span);
        b->bool_value = e.bool_value;
        return b;
    }
    case Expr::Kind::StringLiteral: {
        EP str = mk(hir::ExprKind::StrConst, Type::makePointer(Type::makeInt(8, false)), e.span);
        str->text = e.text;
        return str;
    }
    case Expr::Kind::Name: return nameExpr(e, expected);
    case Expr::Kind::Field: return fieldExpr(e);
    case Expr::Kind::Unary: return unaryExpr(e, expected);
    case Expr::Kind::Binary: return binaryExpr(e, expected);
    case Expr::Kind::Cast: return castExpr(e);
    case Expr::Kind::Call: return callExpr(e, expected);
    case Expr::Kind::Index: return indexExpr(e);
    case Expr::Kind::ArrayList:
    case Expr::Kind::ArrayRepeat: return arrayExpr(e, expected);
    case Expr::Kind::StructLiteral: return structLiteral(e);
    case Expr::Kind::New: return newExpr(e);
    case Expr::Kind::MacroCall:
        err("E0004", e.span, std::format("macro `{}!` was not expanded", e.text), "macro expansion happens before sema");
        return nullptr;
    case Expr::Kind::AsmOperand:
        err("E0709", e.span, "inline asm operands are not supported yet", "`__builtin_asm` arrives with plan phase 6");
        return nullptr;
    }
    return nullptr;
}

Lowerer::EP Lowerer::nameExpr(const Expr& e, const Type* expected) {
    const ScopeEntry* entry = scopes_.lookup(e.text);
    if (entry == nullptr) {
        if (e.text == "nil") {
            return mk(hir::ExprKind::Nil, Type::makePointer(Type::makeVoid()), e.span);
        }
        if (s_.config_.lookup(e.text)) {
            return constExpr(e, expected);
        }
        if (star_import_) {
            EP x = mk(hir::ExprKind::External, unknownType(), e.span);
            x->text = "*";
            x->member = e.text;
            return x;
        }
        err("E0706", e.span, std::format("unknown name `{}`", e.text), "not declared in this scope");
        return nullptr;
    }
    switch (entry->kind) {
    case ScopeEntry::Kind::Local: {
        EP l = mk(hir::ExprKind::LocalRef, entry->type, e.span);
        l->local = entry->local;
        l->text = e.text;
        l->lvalue = true;
        return l;
    }
    case ScopeEntry::Kind::Global: {
        EP g = mk(hir::ExprKind::GlobalRef, entry->type, e.span);
        g->text = e.text;
        g->lvalue = true;
        return g;
    }
    case ScopeEntry::Kind::Function: {
        EP f = mk(hir::ExprKind::FuncRef, fnType(std::get<FnDecl>(entry->item->decl)), e.span);
        f->text = e.text;
        return f;
    }
    case ScopeEntry::Kind::Constant: return constExpr(e, expected);
    case ScopeEntry::Kind::Type:
        err("E0706", e.span, std::format("`{}` is a type, not a value", e.text), "a value is expected here");
        return nullptr;
    case ScopeEntry::Kind::Namespace:
        err("E0706", e.span, std::format("`{}` is a module, not a value", e.text),
            std::format("use `{}.name`", e.text));
        return nullptr;
    case ScopeEntry::Kind::External: {
        EP x = mk(hir::ExprKind::External, unknownType(), e.span);
        x->text = entry->module;
        x->member = entry->member;
        return x;
    }
    }
    return nullptr;
}

Lowerer::EP Lowerer::fieldExpr(const Expr& e) {
    if (e.lhs->kind == Expr::Kind::Name && scopes_.lookup(e.lhs->text) != nullptr) {
        const ScopeEntry* head = scopes_.lookup(e.lhs->text);
        if (head->kind == ScopeEntry::Kind::Namespace) {
            EP x = mk(hir::ExprKind::External, unknownType(), e.span);
            x->text = head->module;
            x->member = e.text;
            return x;
        }
        if (head->kind == ScopeEntry::Kind::Type && head->item != nullptr &&
            std::holds_alternative<EnumDecl>(head->item->decl)) {
            const auto v = s_.enumMember(e.lhs->text, e.text, e.span);
            if (!v) {
                return nullptr;
            }
            const Type et = resolved(Type::makeNamed({e.lhs->text}));
            EP out = fromConst(*v, nullptr, e.span);
            if (out) {
                out->type = (out->kind == hir::ExprKind::FloatConst || out->kind == hir::ExprKind::BoolConst) ? out->type : et;
            }
            return out;
        }
    }
    EP base = expr(*e.lhs, nullptr);
    if (!base) {
        return nullptr;
    }
    if (isUnknown(base->type)) {
        EP f = mk(hir::ExprKind::Field, unknownType(), e.span);
        f->text = e.text;
        f->lhs = std::move(base);
        return f;
    }
    bool through = false;
    Type agg = base->type;
    if (agg.kind == TypeKind::Pointer && agg.elem) {
        through = true;
        agg = *agg.elem;
    }
    if (!isAggregate(agg)) {
        err("E0709", e.span, std::format("`{}` has no field `{}`", typeToString(base->type), e.text),
            "only structs and classes have fields");
        return nullptr;
    }
    std::vector<const FieldDecl*> fields;
    collectFields(agg.path.front(), fields);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i]->name == e.text) {
            EP f = mk(hir::ExprKind::Field, resolved(fields[i]->type), e.span);
            f->text = e.text;
            f->index = static_cast<std::uint32_t>(i);
            f->through_pointer = through;
            f->lvalue = through || base->lvalue;
            f->lhs = std::move(base);
            return f;
        }
    }
    err("E0709", e.span, std::format("`{}` has no field `{}`", typeToString(agg), e.text), "unknown field");
    return nullptr;
}

Lowerer::EP Lowerer::unaryExpr(const Expr& e, const Type* expected) {
    switch (e.unary_op) {
    case UnaryOp::Neg: {
        const Type* hint = nullptr;
        Type want;
        if (expected != nullptr) {
            want = resolved(*expected);
            if (want.kind == TypeKind::Float || (want.kind == TypeKind::Int && want.is_signed)) {
                hint = &want;
            }
        }
        EP v = expr(*e.lhs, hint);
        if (!v || isUnknown(v->type)) {
            return v;
        }
        if (!(v->type.kind == TypeKind::Float || (v->type.kind == TypeKind::Int && v->type.is_signed))) {
            err("E0709", e.span, std::format("cannot negate `{}`", typeToString(v->type)),
                "negation needs a signed integer or a float");
            return nullptr;
        }
        EP n = mk(hir::ExprKind::Unary, v->type, e.span);
        n->unary_op = UnaryOp::Neg;
        n->lhs = std::move(v);
        return n;
    }
    case UnaryOp::Not: {
        const Type boolean = Type::makeBool();
        EP v = coerce(expr(*e.lhs, &boolean), boolean, e.lhs->span);
        if (!v) {
            return nullptr;
        }
        EP n = mk(hir::ExprKind::Unary, boolean, e.span);
        n->unary_op = UnaryOp::Not;
        n->lhs = std::move(v);
        return n;
    }
    case UnaryOp::Deref: {
        EP v = expr(*e.lhs, nullptr);
        if (!v) {
            return nullptr;
        }
        if (isUnknown(v->type)) {
            return v;
        }
        if (v->type.kind != TypeKind::Pointer || !v->type.elem) {
            err("E0709", e.span, std::format("cannot dereference `{}`", typeToString(v->type)), "not a pointer");
            return nullptr;
        }
        if (v->type.elem->isVoid()) {
            err("E0709", e.span, "cannot dereference `*void`", "cast it to a typed pointer first");
            return nullptr;
        }
        EP d = mk(hir::ExprKind::Deref, *v->type.elem, e.span);
        d->lvalue = true;
        d->lhs = std::move(v);
        return d;
    }
    case UnaryOp::AddrOf: {
        EP v = expr(*e.lhs, nullptr);
        if (!v) {
            return nullptr;
        }
        if (v->kind == hir::ExprKind::FuncRef) {
            return v;  // `&fn` is the function pointer itself
        }
        if (!isUnknown(v->type) && !v->lvalue) {
            err("E0709", e.span, "cannot take the address of this expression", "not a place (variable, `*p`, field or element)");
            return nullptr;
        }
        EP a = mk(hir::ExprKind::AddrOf, isUnknown(v->type) ? unknownType() : Type::makePointer(v->type), e.span);
        a->lhs = std::move(v);
        return a;
    }
    }
    return nullptr;
}

Lowerer::EP Lowerer::binaryExpr(const Expr& e, const Type* expected) {
    const BinaryOp op = e.binary_op;
    const Type boolean = Type::makeBool();
    if (op == BinaryOp::And || op == BinaryOp::Or) {
        EP l = coerce(expr(*e.lhs, &boolean), boolean, e.lhs->span);
        EP r = coerce(expr(*e.rhs, &boolean), boolean, e.rhs->span);
        if (!l || !r) {
            return nullptr;
        }
        EP b = mk(hir::ExprKind::Binary, boolean, e.span);
        b->binary_op = op;
        b->lhs = std::move(l);
        b->rhs = std::move(r);
        return b;
    }
    const bool is_cmp = op >= BinaryOp::Eq && op <= BinaryOp::Ge;
    const auto isLiteral = [](const Expr& x) {
        return x.kind == Expr::Kind::IntLiteral || x.kind == Expr::Kind::FloatLiteral ||
               (x.kind == Expr::Kind::Unary && x.unary_op == UnaryOp::Neg && x.lhs &&
                (x.lhs->kind == Expr::Kind::IntLiteral || x.lhs->kind == Expr::Kind::FloatLiteral));
    };
    // A bare literal takes the type of the other operand.
    EP l;
    EP r;
    const Type* hint = is_cmp ? nullptr : expected;
    const auto isFloatLiteral = [](const Expr& x) {
        return x.kind == Expr::Kind::FloatLiteral ||
               (x.kind == Expr::Kind::Unary && x.lhs && x.lhs->kind == Expr::Kind::FloatLiteral);
    };
    if (isLiteral(*e.lhs) && (!isLiteral(*e.rhs) || isFloatLiteral(*e.rhs))) {
        r = expr(*e.rhs, hint);
        const Type want = r ? r->type : unknownType();
        l = expr(*e.lhs, r ? &want : hint);
    } else {
        l = expr(*e.lhs, hint);
        const Type want = l ? l->type : unknownType();
        r = expr(*e.rhs, l && isNumeric(l->type) ? &want : hint);
    }
    if (!l || !r) {
        return nullptr;
    }
    if (isUnknown(l->type) || isUnknown(r->type)) {
        EP b = mk(hir::ExprKind::Binary, is_cmp ? boolean : unknownType(), e.span);
        b->binary_op = op;
        b->lhs = std::move(l);
        b->rhs = std::move(r);
        return b;
    }

    Type common;
    if (isNumeric(l->type) && isNumeric(r->type)) {
        const CommonTypeResult c = commonBinaryType(l->type, r->type, ptrBits());
        if (c.status == ConversionStatus::Signedness) {
            err("E0601", e.span,
                std::format("`{}` and `{}` have different signedness", typeToString(l->type), typeToString(r->type)),
                "write `as` on one side");
            return nullptr;
        }
        if (c.status != ConversionStatus::Ok) {
            err("E0709", e.span, "operands cannot be combined", "incompatible numeric types");
            return nullptr;
        }
        if (c.warn) {
            s_.diag_.warning("W0601", e.span,
                             std::format("signed integer is implicitly converted to `{}`, precision may be lost",
                                         typeToString(c.type)),
                             "write `as` to silence");
        }
        common = c.type;
        l = castTo(std::move(l), common, true);
        r = castTo(std::move(r), common, true);
    } else if (is_cmp) {
        // Pointers (also against `nil`), bool, enums, same-type values.
        if (l->kind == hir::ExprKind::Nil && r->type.kind == TypeKind::Pointer) {
            l->type = r->type;
        } else if (r->kind == hir::ExprKind::Nil && l->type.kind == TypeKind::Pointer) {
            r->type = l->type;
        }
        const bool eq_only = op == BinaryOp::Eq || op == BinaryOp::Ne;
        const bool scalar = l->type.kind == TypeKind::Pointer || l->type.kind == TypeKind::Bool ||
                            l->type.kind == TypeKind::Enum || l->type.kind == TypeKind::FnPtr;
        if (!sameType(l->type, r->type) || !scalar || (!eq_only && l->type.kind != TypeKind::Enum)) {
            err("E0709", e.span,
                std::format("cannot compare `{}` with `{}`", typeToString(l->type), typeToString(r->type)),
                "operands must have the same comparable type");
            return nullptr;
        }
        common = l->type;
    } else {
        err("E0709", e.span,
            std::format("operator `{}` does not apply to `{}` and `{}`", binaryOpName(op), typeToString(l->type),
                        typeToString(r->type)),
            "arithmetic needs numeric operands; cast pointers with `as usize`");
        return nullptr;
    }
    if (op == BinaryOp::Mod && common.kind == TypeKind::Float) {
        err("E0709", e.span, "`%` needs integer operands", "floats have no remainder operator");
        return nullptr;
    }
    EP b = mk(hir::ExprKind::Binary, is_cmp ? boolean : common, e.span);
    b->binary_op = op;
    b->checked = (op == BinaryOp::Div || op == BinaryOp::Mod) && common.kind == TypeKind::Int;
    b->lhs = std::move(l);
    b->rhs = std::move(r);
    return b;
}

Lowerer::EP Lowerer::castExpr(const Expr& e) {
    const Type to = resolved(e.type);
    EP v = expr(*e.lhs, nullptr);
    if (!v) {
        return nullptr;
    }
    if (v->kind == hir::ExprKind::Nil && (to.kind == TypeKind::Pointer || to.kind == TypeKind::FnPtr)) {
        v->type = to;
        return v;
    }
    if (!castable(v->type, to)) {
        err("E0709", e.span, std::format("cannot cast `{}` to `{}`", typeToString(v->type), typeToString(to)),
            "no explicit conversion exists");
        return nullptr;
    }
    if (isUnknown(v->type) || isUnknown(to)) {
        EP c = mk(hir::ExprKind::Cast, to, e.span);
        c->lhs = std::move(v);
        return c;
    }
    EP c = mk(hir::ExprKind::Cast, to, e.span);
    c->lhs = std::move(v);
    return c;
}

bool Lowerer::lowerArgs(const Expr& call, const std::vector<Type>& params, std::vector<EP>& out,
                        const std::string& callee) {
    if (call.args.size() != params.size()) {
        err("E0710", call.span,
            std::format("`{}` takes {} argument(s), {} given", callee, params.size(), call.args.size()),
            "wrong number of arguments");
        for (const ExprPtr& a : call.args) {
            (void)expr(*a, nullptr);
        }
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < params.size(); ++i) {
        EP a = coerce(expr(*call.args[i], &params[i]), params[i], call.args[i]->span);
        ok = ok && a != nullptr;
        out.push_back(std::move(a));
    }
    return ok;
}

Lowerer::EP Lowerer::syscallExpr(const Expr& e) {
    if (e.args.empty()) {
        err("E0710", e.span, "`syscall` needs the syscall number", "wrong number of arguments");
        return nullptr;
    }
    const auto limit = syscallMaxArgs(s_.config_.target);
    if (limit && e.args.size() - 1 > *limit) {
        err("E0710", e.span,
            std::format("`syscall` has {} argument(s) after the number, `{}` allows at most {}", e.args.size() - 1,
                        s_.config_.target.triple.empty() ? s_.config_.target.os + "/" + s_.config_.target.arch
                                                         : s_.config_.target.triple,
                        *limit),
            "too many arguments for this target");
        return nullptr;
    }
    EP out = mk(hir::ExprKind::Syscall, Type::makeInt(64, true), e.span);
    bool ok = true;
    for (const ExprPtr& a : e.args) {
        EP v = expr(*a, nullptr);
        if (!v) {
            ok = false;
            continue;
        }
        if (!isUnknown(v->type) && v->type.kind != TypeKind::Int && v->type.kind != TypeKind::Pointer) {
            err("E0708", a->span, std::format("`syscall` arguments are integers or pointers, found `{}`", typeToString(v->type)),
                "integer or `*T` expected");
            ok = false;
        }
        out->args.push_back(std::move(v));
    }
    if (!ok) {
        return nullptr;
    }
    return out;
}

std::optional<Lowerer::MethodRef> Lowerer::findMethod(const Type& agg, const std::string& name) {
    if (agg.kind == TypeKind::Interface) {
        const Sema::Symbol* sym = s_.find(agg.path.front());
        if (sym == nullptr || !std::holds_alternative<InterfaceDecl>(sym->item->decl)) {
            return std::nullopt;
        }
        const auto& decl = std::get<InterfaceDecl>(sym->item->decl);
        for (std::size_t i = 0; i < decl.methods.size(); ++i) {
            if (decl.methods[i].name == name) {
                MethodRef m;
                m.kind = MethodRef::Kind::Itable;
                m.decl = &decl.methods[i];
                m.index = static_cast<std::uint32_t>(i);
                return m;
            }
        }
        return std::nullopt;
    }
    std::string cur = agg.path.front();
    std::set<std::string> seen;
    while (!cur.empty() && seen.insert(cur).second) {
        if (const ClassDecl* c = classDecl(cur)) {
            for (const FnDecl& m : c->methods) {
                if (m.name == name) {
                    MethodRef r;
                    r.kind = (m.is_virtual || m.is_override) ? MethodRef::Kind::Virtual : MethodRef::Kind::Direct;
                    r.fn_name = cur + "." + name;
                    r.decl = &m;
                    return r;
                }
            }
        }
        for (const ImplInfo& i : impls_) {
            if (i.type_name != cur) {
                continue;
            }
            for (const FnDecl& m : i.decl->methods) {
                if (m.name == name) {
                    MethodRef r;
                    r.fn_name = i.prefix + name;
                    r.decl = &m;
                    r.from_impl = true;
                    return r;
                }
            }
        }
        const ClassDecl* c = classDecl(cur);
        cur = (c != nullptr && c->has_base && c->base.size() == 1) ? c->base[0] : std::string{};
    }
    return std::nullopt;
}

Lowerer::EP Lowerer::methodCall(const Expr& e, const Expr& recv_ast) {
    const Expr& callee = *e.lhs;
    EP recv = expr(recv_ast, nullptr);
    if (!recv) {
        for (const ExprPtr& a : e.args) {
            (void)expr(*a, nullptr);
        }
        return nullptr;
    }
    if (isUnknown(recv->type)) {
        EP c = mk(hir::ExprKind::CallIndirect, unknownType(), e.span);
        c->lhs = std::move(recv);
        for (const ExprPtr& a : e.args) {
            c->args.push_back(expr(*a, nullptr));
        }
        return c;
    }
    bool through = false;
    Type agg = recv->type;
    if (agg.kind == TypeKind::Pointer && agg.elem) {
        through = true;
        agg = *agg.elem;
    }
    if (agg.kind != TypeKind::Interface && !isAggregate(agg)) {
        err("E0709", callee.span, std::format("`{}` has no method `{}`", typeToString(recv->type), callee.text),
            "only structs, classes and interfaces have methods");
        return nullptr;
    }
    const auto m = findMethod(agg, callee.text);
    if (!m) {
        err("E0709", callee.span, std::format("`{}` has no method `{}`", typeToString(agg), callee.text), "unknown method");
        for (const ExprPtr& a : e.args) {
            (void)expr(*a, nullptr);
        }
        return nullptr;
    }
    std::vector<Type> params;
    for (const Param& p : m->decl->params) {
        if (!p.is_self) {
            params.push_back(resolved(p.type));
        }
    }
    const Type ret = resolved(m->decl->return_type);
    std::vector<EP> args;
    if (!lowerArgs(e, params, args, callee.text)) {
        return nullptr;
    }

    if (m->kind == MethodRef::Kind::Itable) {
        if (through) {  // pointer to an interface value: call through the value
            EP d = mk(hir::ExprKind::Deref, agg, recv->span);
            d->lvalue = true;
            d->lhs = std::move(recv);
            recv = std::move(d);
        }
        EP c = mk(hir::ExprKind::CallItable, ret, e.span);
        c->text = callee.text;
        c->index = m->index;
        c->lhs = std::move(recv);
        c->args = std::move(args);
        return c;
    }

    EP self;
    if (through) {
        self = std::move(recv);
    } else {
        if (!recv->lvalue) {
            err("E0709", recv_ast.span, "a method needs an addressable receiver", "store the value in a variable first");
            return nullptr;
        }
        EP addr = mk(hir::ExprKind::AddrOf, Type::makePointer(agg), recv->span);
        addr->lhs = std::move(recv);
        self = std::move(addr);
    }
    EP c = mk(m->kind == MethodRef::Kind::Virtual ? hir::ExprKind::CallVirtual : hir::ExprKind::Call, ret, e.span);
    c->text = m->fn_name;
    c->args.push_back(std::move(self));
    for (EP& a : args) {
        c->args.push_back(std::move(a));
    }
    return c;
}

Lowerer::EP Lowerer::callExpr(const Expr& e, const Type* expected) {
    const Expr& callee = *e.lhs;
    if (callee.kind == Expr::Kind::Name) {
        const std::string& name = callee.text;
        const ScopeEntry* entry = scopes_.lookup(name);
        const bool shadowed = entry != nullptr && entry->kind != ScopeEntry::Kind::Type;
        if (!shadowed) {
            if (name == "syscall") {
                return syscallExpr(e);
            }
            if (name == "sizeOf" || name == "alignOf" || name == "offsetOf") {
                const Type size_t_type = Type::makeInt(0, false);
                return constExpr(e, &size_t_type);
            }
            if (name == "staticAssert") {
                err("E0709", e.span, "`staticAssert` is a statement", "it has no value");
                return nullptr;
            }
            if (name.starts_with("__builtin_")) {
                err("E0706", callee.span, std::format("builtin `{}` is not supported yet", name),
                    "atomics, fences and inline asm arrive with plan phase 6");
                return nullptr;
            }
        }
        if (entry != nullptr && entry->kind == ScopeEntry::Kind::Function) {
            const auto& decl = std::get<FnDecl>(entry->item->decl);
            std::vector<Type> params;
            for (const Param& p : decl.params) {
                params.push_back(resolved(p.type));
            }
            std::vector<EP> args;
            const Type ret = resolved(decl.return_type);
            if (!lowerArgs(e, params, args, name)) {
                return nullptr;
            }
            EP c = mk(hir::ExprKind::Call, ret, e.span);
            c->text = name;
            c->args = std::move(args);
            return c;
        }
        if (entry != nullptr && entry->kind == ScopeEntry::Kind::External) {
            EP c = mk(hir::ExprKind::ExternalCall, unknownType(), e.span);
            c->text = entry->module;
            c->member = entry->member;
            for (const ExprPtr& a : e.args) {
                c->args.push_back(expr(*a, nullptr));
            }
            return c;
        }
        if (entry == nullptr && star_import_) {
            EP c = mk(hir::ExprKind::ExternalCall, unknownType(), e.span);
            c->text = "*";
            c->member = name;
            for (const ExprPtr& a : e.args) {
                c->args.push_back(expr(*a, nullptr));
            }
            return c;
        }
    } else if (callee.kind == Expr::Kind::Field) {
        // `ns.fn(...)` (module namespace) or `value.method(...)`.
        if (callee.lhs->kind == Expr::Kind::Name) {
            const ScopeEntry* head = scopes_.lookup(callee.lhs->text);
            if (head != nullptr && head->kind == ScopeEntry::Kind::Namespace) {
                EP c = mk(hir::ExprKind::ExternalCall, unknownType(), e.span);
                c->text = head->module;
                c->member = callee.text;
                for (const ExprPtr& a : e.args) {
                    c->args.push_back(expr(*a, nullptr));
                }
                return c;
            }
        }
        return methodCall(e, *callee.lhs);
    }
    (void)expected;
    // Anything else: the callee must be a function pointer value.
    EP f = expr(callee, nullptr);
    if (!f) {
        return nullptr;
    }
    if (isUnknown(f->type)) {
        EP c = mk(hir::ExprKind::CallIndirect, unknownType(), e.span);
        c->lhs = std::move(f);
        for (const ExprPtr& a : e.args) {
            c->args.push_back(expr(*a, nullptr));
        }
        return c;
    }
    if (f->type.kind != TypeKind::FnPtr || !f->type.ret) {
        err("E0709", e.span, std::format("`{}` is not callable", typeToString(f->type)), "expected a function or function pointer");
        return nullptr;
    }
    std::vector<EP> args;
    const std::string label = f->kind == hir::ExprKind::LocalRef ? f->text : "function pointer";
    if (!lowerArgs(e, f->type.params, args, label)) {
        return nullptr;
    }
    EP c = mk(f->kind == hir::ExprKind::FuncRef ? hir::ExprKind::Call : hir::ExprKind::CallIndirect, *f->type.ret, e.span);
    if (f->kind == hir::ExprKind::FuncRef) {
        c->text = f->text;
    } else {
        c->lhs = std::move(f);
    }
    c->args = std::move(args);
    return c;
}

Lowerer::EP Lowerer::indexExpr(const Expr& e) {
    EP base = expr(*e.lhs, nullptr);
    EP idx = expr(*e.rhs, nullptr);
    if (!base || !idx) {
        return nullptr;
    }
    if (isUnknown(base->type)) {
        EP i = mk(hir::ExprKind::Index, unknownType(), e.span);
        i->lhs = std::move(base);
        i->rhs = std::move(idx);
        return i;
    }
    if (!isUnknown(idx->type) && idx->type.kind != TypeKind::Int) {
        err("E0708", e.rhs->span, std::format("index must be an integer, found `{}`", typeToString(idx->type)),
            "integer expected");
        return nullptr;
    }
    if (base->type.kind != TypeKind::Array && base->type.kind != TypeKind::Pointer) {
        err("E0709", e.span, std::format("`{}` cannot be indexed", typeToString(base->type)),
            "only arrays and pointers can be indexed");
        return nullptr;
    }
    const bool is_array = base->type.kind == TypeKind::Array;
    if (!base->type.elem) {
        return nullptr;
    }
    if (!is_array && base->type.elem->isVoid()) {
        err("E0709", e.span, "cannot index `*void`", "cast it to a typed pointer first");
        return nullptr;
    }
    if (is_array && e.rhs->kind == Expr::Kind::IntLiteral) {
        if (const auto len = arrayLength(base->type); len && e.rhs->int_value >= *len) {
            err("E0701", e.rhs->span,
                std::format("index {} is out of bounds for `{}` (length {})", e.rhs->int_value,
                            typeToString(base->type), *len),
                "constant index out of range");
            return nullptr;
        }
    }
    EP i = mk(hir::ExprKind::Index, *base->type.elem, e.span);
    i->checked = is_array && s_.config_.safety == "checked";
    i->lvalue = is_array ? base->lvalue : true;
    i->lhs = std::move(base);
    i->rhs = std::move(idx);
    return i;
}

Lowerer::EP Lowerer::arrayExpr(const Expr& e, const Type* expected) {
    const Type want = expected != nullptr ? resolved(*expected) : unknownType();
    const Type* elem_hint = want.kind == TypeKind::Array && want.elem ? want.elem.get() : nullptr;
    if (e.kind == Expr::Kind::ArrayRepeat) {
        EP v = expr(*e.lhs, elem_hint);
        const auto n = s_.evalConst(*e.rhs);
        if (!v || !n) {
            return nullptr;
        }
        if (n->kind != ConstValue::Kind::Int || n->int_value < 0) {
            err("E0707", e.rhs->span, "array length must be a non-negative integer constant", "invalid length");
            return nullptr;
        }
        auto len = std::make_shared<Expr>();
        len->kind = Expr::Kind::IntLiteral;
        len->int_value = static_cast<std::uint64_t>(n->int_value);
        len->span = e.rhs->span;
        if (elem_hint != nullptr) {
            v = coerce(std::move(v), *elem_hint, e.lhs->span);
            if (!v) {
                return nullptr;
            }
        }
        EP out = mk(hir::ExprKind::ArrayRepeat, Type::makeArray(v->type, len), e.span);
        out->int_value = len->int_value;
        out->lhs = std::move(v);
        return out;
    }
    std::vector<EP> items;
    Type elem;
    bool have = false;
    bool ok = true;
    for (const ExprPtr& a : e.args) {
        EP v = expr(*a, have ? &elem : elem_hint);
        if (v && elem_hint != nullptr) {
            v = coerce(std::move(v), *elem_hint, a->span);
        } else if (v && have) {
            v = coerce(std::move(v), elem, a->span);
        }
        if (!v) {
            ok = false;
            continue;
        }
        if (!have) {
            elem = elem_hint != nullptr ? *elem_hint : v->type;
            have = true;
        }
        items.push_back(std::move(v));
    }
    if (!ok) {
        return nullptr;
    }
    if (!have) {
        if (elem_hint == nullptr) {
            err("E0708", e.span, "cannot infer the element type of an empty array", "write a type: `local a: [T; 0]`");
            return nullptr;
        }
        elem = *elem_hint;
    }
    auto len = std::make_shared<Expr>();
    len->kind = Expr::Kind::IntLiteral;
    len->int_value = items.size();
    len->span = e.span;
    EP out = mk(hir::ExprKind::ArrayList, Type::makeArray(elem, len), e.span);
    out->args = std::move(items);
    return out;
}

void Lowerer::lowerFields(const Expr& e, const Type& agg, std::vector<hir::FieldInit>& out) {
    std::vector<const FieldDecl*> fields;
    collectFields(agg.path.front(), fields);
    std::set<std::string> seen;
    for (const FieldInit& init : e.fields) {
        std::size_t index = fields.size();
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i]->name == init.name) {
                index = i;
            }
        }
        if (index == fields.size()) {
            err("E0709", init.span, std::format("`{}` has no field `{}`", typeToString(agg), init.name), "unknown field");
            if (init.value) {
                (void)expr(*init.value, nullptr);
            }
            continue;
        }
        if (!seen.insert(init.name).second) {
            err("E0709", init.span, std::format("field `{}` is initialized twice", init.name), "duplicate field");
            continue;
        }
        const Type ft = resolved(fields[index]->type);
        EP v = init.value ? coerce(expr(*init.value, &ft), ft, init.value->span) : nullptr;
        if (!v) {
            continue;
        }
        hir::FieldInit h;
        h.name = init.name;
        h.index = static_cast<std::uint32_t>(index);
        h.value = std::move(v);
        out.push_back(std::move(h));
    }
}

Lowerer::EP Lowerer::structLiteral(const Expr& e) {
    const Type t = resolved(e.type);
    if (isUnknown(t)) {
        return nullptr;
    }
    if (!isAggregate(t)) {
        err("E0709", e.span, std::format("`{}` is not a struct or class", typeToString(t)), "cannot build it with `{ ... }`");
        return nullptr;
    }
    EP out = mk(hir::ExprKind::StructLit, t, e.span);
    lowerFields(e, t, out->fields);
    return out;
}

Lowerer::EP Lowerer::newExpr(const Expr& e) {
    const Type t = resolved(e.type);
    if (isUnknown(t)) {
        return nullptr;
    }
    if (!isAggregate(t)) {
        err("E0709", e.span, std::format("`new` needs a struct or class, found `{}`", typeToString(t)), "not allocatable");
        return nullptr;
    }
    EP out = mk(hir::ExprKind::New, Type::makePointer(t), e.span);
    out->target = t;
    lowerFields(e, t, out->fields);
    if (e.lhs) {
        EP a = expr(*e.lhs, nullptr);
        if (!a) {
            return nullptr;
        }
        if (!isUnknown(a->type) && !hasAllocMethod(a->type)) {
            Diagnostic d;
            d.code = "E0504";
            d.severity = Severity::Error;
            d.message = std::format("`{}` is not an allocator", typeToString(a->type));
            d.labels.push_back({e.lhs->span, "no `alloc` through an `impl` or interface"});
            d.reason = "Interfaces are nominal; matching method names never count.";
            d.fix = "add `impl Allocator for T { ... }` or pass an `Allocator` value";
            s_.diag_.report(std::move(d));
            return nullptr;
        }
        out->lhs = std::move(a);
    } else {
        if (mod_.allocator_global.empty()) {
            Diagnostic d;
            d.code = "E0501";
            d.severity = Severity::Error;
            d.message = "`new` has no registered allocator";
            d.labels.push_back({e.span, "no `register(allocator)` global in this compilation"});
            d.reason = "The compiler binds no allocator by name.";
            d.fix = "write `new(a) T { ... }` or register an allocator global";
            s_.diag_.report(std::move(d));
            return nullptr;
        }
        const ScopeEntry* g = scopes_.lookupModule(mod_.allocator_global);
        EP ref = mk(hir::ExprKind::GlobalRef, g != nullptr ? g->type : unknownType(), e.span);
        ref->text = mod_.allocator_global;
        ref->lvalue = true;
        out->lhs = std::move(ref);
        out->registered = true;
    }
    return out;
}

bool Lowerer::isConstValue(const hir::Expr& e, const hir::Expr** offender) const {
    const auto fail = [&](const hir::Expr& at) {
        if (offender != nullptr && *offender == nullptr) {
            *offender = &at;
        }
        return false;
    };
    switch (e.kind) {
    case hir::ExprKind::IntConst:
    case hir::ExprKind::FloatConst:
    case hir::ExprKind::BoolConst:
    case hir::ExprKind::StrConst:
    case hir::ExprKind::Nil:
    case hir::ExprKind::FuncRef:
    case hir::ExprKind::External:
        return true;
    case hir::ExprKind::Unary:
    case hir::ExprKind::Cast:
    case hir::ExprKind::ToInterface:
        return e.lhs && isConstValue(*e.lhs, offender);
    case hir::ExprKind::Binary:
        return e.lhs && e.rhs && isConstValue(*e.lhs, offender) && isConstValue(*e.rhs, offender);
    case hir::ExprKind::ArrayList:
        return std::ranges::all_of(e.args, [&](const hir::ExprPtr& a) { return isConstValue(*a, offender); });
    case hir::ExprKind::ArrayRepeat:
        return e.lhs && isConstValue(*e.lhs, offender);
    case hir::ExprKind::StructLit:
        return std::ranges::all_of(e.fields, [&](const hir::FieldInit& f) { return isConstValue(*f.value, offender); });
    case hir::ExprKind::AddrOf: {
        // `&global`, `&global[const]`, `&global.field`
        const hir::Expr* cur = e.lhs.get();
        while (cur != nullptr) {
            if (cur->kind == hir::ExprKind::GlobalRef) {
                return true;
            }
            if (cur->kind == hir::ExprKind::Index) {
                if (cur->rhs == nullptr || cur->rhs->kind != hir::ExprKind::IntConst) {
                    return fail(*cur);
                }
                cur = cur->lhs.get();
            } else if (cur->kind == hir::ExprKind::Field && !cur->through_pointer) {
                cur = cur->lhs.get();
            } else {
                return fail(e);
            }
        }
        return fail(e);
    }
    default:
        return fail(e);
    }
}

hir::Module Lowerer::run() {
    buildModuleScope();
    collectImpls();
    checkRegistrations();
    lowerItems();
    scopes_.pop();
    return std::move(mod_);
}

hir::Module Sema::lower() {
    Lowerer lowerer(*this);
    return lowerer.run();
}

}  // namespace xlang
