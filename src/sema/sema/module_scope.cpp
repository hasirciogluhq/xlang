#include "sema/sema/sema.hpp"

#include "xlang/sema/compilation.h"

#include <algorithm>
#include <format>
#include <functional>
#include <numeric>
#include <random>

namespace xlang {

// Module awareness (plan 4.6): import bindings, qualified lookup, the entity behind a name
// expression, and the type a type expression names.


// ---------------------------------------------------------------------------------------------
// Modules

std::string Sema::EntityRef::qname() const {
    return owner->qualify(local);
}

void Sema::attach(Compilation* compilation, std::string module_key) {
    comp_ = compilation;
    key_ = std::move(module_key);
}

void Sema::bindImport(const std::string& local_name, ImportBinding binding) {
    imports_[local_name] = std::move(binding);
}

std::string Sema::qualify(const std::string& local) const {
    if (key_.empty() || local == "MemoryOrder") {
        return local;
    }
    return key_ + "::" + local;
}

std::optional<Sema::EntityRef> Sema::lookupEntity(const std::string& name) {
    if (const Symbol* s = find(name)) {
        return EntityRef{this, s, name};
    }
    if (const auto it = imports_.find(name); it != imports_.end() && comp_ != nullptr &&
                                             it->second.kind == ImportBinding::Kind::Entity) {
        if (Sema* owner = comp_->semaOf(it->second.module_key)) {
            if (const Symbol* s = owner->find(it->second.name)) {
                return EntityRef{owner, s, it->second.name};
            }
        }
    }
    if (comp_ != nullptr) {
        return comp_->findRegistered(name);
    }
    return std::nullopt;
}

std::optional<Sema::EntityRef> Sema::lookupQualified(const std::vector<std::string>& path) {
    if (comp_ == nullptr || path.size() != 2) {
        return std::nullopt;
    }
    const auto it = imports_.find(path[0]);
    if (it == imports_.end() || it->second.kind != ImportBinding::Kind::Namespace) {
        return std::nullopt;
    }
    return comp_->exportedEntity(it->second.module_key, path[1]);
}

std::optional<Sema::EntityRef> Sema::entityByQName(const std::string& qname) {
    const std::size_t sep = qname.rfind("::");
    if (comp_ == nullptr) {
        if (const Symbol* s = find(qname)) {
            return EntityRef{this, s, qname};
        }
        return std::nullopt;
    }
    Sema* owner = comp_->semaOf(sep == std::string::npos ? std::string{} : qname.substr(0, sep));
    if (owner == nullptr) {
        return std::nullopt;
    }
    const std::string local = sep == std::string::npos ? qname : qname.substr(sep + 2);
    if (const Symbol* s = owner->find(local)) {
        return EntityRef{owner, s, local};
    }
    return std::nullopt;
}

std::optional<Sema::EntityRef> Sema::entityOfExpr(const Expr& expr) {
    if (expr.kind == Expr::Kind::Name) {
        return lookupEntity(expr.text);
    }
    if (expr.kind == Expr::Kind::Field && expr.lhs->kind == Expr::Kind::Name) {
        return lookupQualified({expr.lhs->text, expr.text});
    }
    return std::nullopt;
}

std::optional<Type> Sema::exprToType(const Expr& expr) {
    switch (expr.kind) {
    case Expr::Kind::Name: {
        if (expr.type.kind != TypeKind::Void) {
            // A type parameter substituted by an instance: already resolved (see cloneExpr).
            Type t = expr.type;
            t.span = expr.span;
            return t;
        }
        if (auto prim = primitiveFromName(expr.text)) {
            prim->span = expr.span;
            return prim;
        }
        Type t = Type::makeNamed({expr.text});
        t.span = expr.span;
        return t;
    }
    case Expr::Kind::Field: {
        std::vector<std::string> path;
        const Expr* e = &expr;
        while (e->kind == Expr::Kind::Field) {
            path.insert(path.begin(), e->text);
            e = e->lhs.get();
        }
        if (e->kind != Expr::Kind::Name) {
            return std::nullopt;
        }
        path.insert(path.begin(), e->text);
        Type t = Type::makeNamed(std::move(path));
        t.span = expr.span;
        return t;
    }
    case Expr::Kind::Unary:
        if (expr.unary_op == UnaryOp::Deref) {
            if (auto inner = exprToType(*expr.lhs)) {
                Type t = Type::makePointer(std::move(*inner));
                t.span = expr.span;
                return t;
            }
        }
        return std::nullopt;
    case Expr::Kind::ArrayRepeat: {
        if (auto elem = exprToType(*expr.lhs)) {
            auto len = std::make_shared<Expr>();
            len->kind = expr.rhs->kind;
            // Share the length expression by re-evaluating it eagerly.
            auto value = evalConst(*expr.rhs);
            if (!value || value->kind != ConstValue::Kind::Int) {
                return std::nullopt;
            }
            len->kind = Expr::Kind::IntLiteral;
            len->int_value = static_cast<std::uint64_t>(value->int_value);
            len->span = expr.rhs->span;
            Type t = Type::makeArray(std::move(*elem), len);
            t.span = expr.span;
            return t;
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}


}  // namespace xlang
