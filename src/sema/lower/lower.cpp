// Plan 4.1-4.12: scopes + name resolution + type checking of function bodies, lowered to HIR in
// one pass over the whole program. Module-level services (type resolution, constants, layouts)
// stay in `Sema`; this file adds cross-module entities, class vtables, interface tables,
// visibility and mangling. Entry point and the cross-module orchestration live here; the
// `Lowerer` members are compiled by `lower_items.cpp`, `lower_stmt.cpp` and `lower_expr.cpp`.

#include "sema/lower/lower.hpp"

#include <format>
#include <ranges>

namespace xlang {

namespace lower {

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

Type Lowerer::resolvedIn(Sema& sema, const Type& t) {
    auto r = sema.resolve(t);
    if (!r) {
        return unknownType();
    }
    return normalized(sema, *r);
}

Type Lowerer::globalType(Sema& sema, const GlobalDecl& g) {
    if (g.has_type) {
        return resolvedIn(sema, g.type);
    }
    if (g.init) {
        if (const auto t = literalConstType(sema, *g.init)) {
            return *t;
        }
    }
    return unknownType();
}

/// HIR array types carry a literal length, never a `define` name.
Type Lowerer::normalized(Sema& sema, const Type& t) {
    Type out = t;
    if (t.kind == TypeKind::Array && t.length != nullptr) {
        if (const auto n = arrayLength(sema, t)) {
            auto len = std::make_shared<Expr>();
            len->kind = Expr::Kind::IntLiteral;
            len->int_value = *n;
            len->span = t.length->span;
            out.length = len;
        }
    }
    if ((t.kind == TypeKind::Array || t.kind == TypeKind::Pointer || t.kind == TypeKind::Enum) &&
        t.elem) {
        out.elem = std::make_shared<const Type>(normalized(sema, *t.elem));
    }
    if (t.kind == TypeKind::FnPtr) {
        for (Type& p : out.params) {
            p = normalized(sema, p);
        }
        if (t.ret) {
            out.ret = std::make_shared<const Type>(normalized(sema, *t.ret));
        }
    }
    return out;
}

std::optional<std::uint64_t> Lowerer::arrayLength(Sema& sema, const Type& t) {
    if (t.length == nullptr) {
        return std::nullopt;
    }
    const auto v = sema.evalConst(*t.length);
    if (!v || v->kind != ConstValue::Kind::Int || v->int_value < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(v->int_value);
}

Type Lowerer::fnTypeOf(Sema& owner, const FnDecl& f) {
    std::vector<Type> params;
    for (const Param& p : f.params) {
        if (!p.is_self) {
            params.push_back(resolvedIn(owner, p.type));
        }
    }
    return Type::makeFnPtr(std::move(params), resolvedIn(owner, f.return_type));
}

std::optional<Sema::EntityRef> Lowerer::entityOfType(const Type& t) {
    if (t.path.empty()) {
        return std::nullopt;
    }
    return s_.entityByQName(t.path.front());
}

void Lowerer::collectFields(const std::string& qname, std::vector<FieldInfo>& out) {
    const auto ref = s_.entityByQName(qname);
    if (!ref) {
        return;
    }
    if (const auto* s = std::get_if<StructDecl>(&ref->sym->item->decl)) {
        for (const FieldDecl& f : s->fields) {
            out.push_back({&f, ref->owner});
        }
        return;
    }
    const auto* c = std::get_if<ClassDecl>(&ref->sym->item->decl);
    if (c == nullptr) {
        return;
    }
    if (c->has_base) {
        Type base = Type::makeNamed(c->base);
        if (const auto r = ref->owner->resolve(base);
            r && r->kind == TypeKind::Class && r->path.front() != qname) {
            collectFields(r->path.front(), out);
        }
    }
    for (const FieldDecl& f : c->fields) {
        out.push_back({&f, ref->owner});
    }
}

const Lowerer::ImplInfo* Lowerer::findImplExact(const std::string& type_q,
                                                const std::string& iface_q) const {
    for (const ImplInfo& i : sh_.impls) {
        if (i.type_q == type_q && i.iface_q == iface_q) {
            return &i;
        }
    }
    return nullptr;
}

const Lowerer::ImplInfo* Lowerer::findImpl(const std::string& type_q, const std::string& iface_q) {
    if (const ImplInfo* found = findImplExact(type_q, iface_q)) {
        return found;
    }
    if (instantiateImpls()) {
        return findImplExact(type_q, iface_q);
    }
    return nullptr;
}

bool Lowerer::instantiateImpls() {
    bool any = false;
    for (bool created = true; created;) {
        created = false;
        for (const LowerShared::ImplTemplate& t : sh_.impl_templates) {
            const auto& decl = std::get<ImplDecl>(t.item->decl);
            // The generic aggregate the template's target names (E0706 was reported in collect).
            const auto ref = decl.target.path.size() == 1
                                 ? t.owner->lookupEntity(decl.target.path.front())
                                 : t.owner->lookupQualified(decl.target.path);
            if (!ref || ref->sym->item == nullptr ||
                Sema::genericParamsOf(*ref->sym->item).empty()) {
                continue;
            }
            const std::string generic = ref->qname();
            for (Sema* m : sh_.modules) {
                for (const auto& [local, info] : m->instances()) {
                    if (info.generic != generic || info.args.size() != decl.target.args.size()) {
                        continue;
                    }
                    if (!sh_.impl_done.insert({t.item, m->qualify(local)}).second) {
                        continue;
                    }
                    created = true;
                    instantiateImpl(t, *m, local, info);
                }
            }
        }
        any = any || created;
    }
    return any;
}

void Lowerer::instantiateImpl(const LowerShared::ImplTemplate& t,
                              Sema& m,
                              const std::string& local,
                              const Sema::InstanceInfo& info) {
    const auto& decl = std::get<ImplDecl>(t.item->decl);
    std::map<std::string, Type> bound;
    for (std::size_t i = 0; i < info.args.size(); ++i) {
        if (!unifyParam(decl.target.args[i], info.args[i], decl.generic_params, bound, *t.owner)) {
            return; // this impl does not apply to the instance
        }
    }
    Type target;
    target.kind =
        m.findLocal(local)->kind == Sema::Symbol::Kind::Class ? TypeKind::Class : TypeKind::Struct;
    target.path = {m.qualify(local)};
    target.args = info.args;
    target.span = decl.span;
    Type iface = Type::makeNamed(decl.interface_path);
    iface.span = decl.span;
    for (const Type& a : decl.interface_args) {
        iface.args.push_back(substituteType(a, bound));
    }
    const auto ir = t.owner->resolve(iface);
    if (!ir) {
        return;
    }
    if (ir->kind != TypeKind::Interface) {
        err("E0706",
            decl.span,
            std::format("`{}` is not an interface", joinPath(decl.interface_path)),
            "not declared as an interface");
        return;
    }
    const std::string type_q = target.path.front();
    const std::string iface_q = ir->path.front();
    if (findImplExact(type_q, iface_q) != nullptr) {
        err("E0731",
            decl.span,
            std::format("`{}` implements `{}` twice", type_q, iface_q),
            "keep one `impl` for this type and interface");
        return;
    }
    Item inst = instantiateItem(*t.item, bound, type_q + "." + iface_q);
    std::get<ImplDecl>(inst.decl).target = target;
    const Item* item = t.owner->addImplInstance(std::move(inst));
    ImplInfo out;
    out.decl = &std::get<ImplDecl>(item->decl);
    out.owner = t.owner;
    out.item = item;
    out.type_q = type_q;
    out.iface_q = iface_q;
    out.prefix = type_q + "." + iface_q + ".";
    out.span = decl.span;
    out.instance = true;
    sh_.impls.push_back(std::move(out));
}

bool Lowerer::isRegisteredName(const std::string& name) const {
    if (s_.comp_ != nullptr) {
        return s_.comp_->findRegistered(name).has_value();
    }
    return registered_names_.contains(name);
}

} // namespace lower

} // namespace xlang
