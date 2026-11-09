#include "sema/sema/sema.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <numeric>
#include <random>

namespace xlang {

// Generic instances (plan 6): `instantiate`, method instances and the queues the lowering
// pass drains. Each instance is a cloned `Item` interned in `instances_` and named
// `f<i32, str>`.

using sema_detail::typeNesting;

std::optional<Sema::EntityRef>
Sema::instantiate(const EntityRef& generic, const std::vector<Type>& args, const Span& at) {
    const Item& gitem = *generic.sym->item;
    const std::vector<std::string> params = genericParamsOf(gitem);
    Sema& owner = *generic.owner;
    if (params.size() != args.size()) {
        error("E0724",
              at,
              std::format("`{}` takes {} type argument(s), {} given",
                          generic.local,
                          params.size(),
                          args.size()),
              "write one type per type parameter");
        return std::nullopt;
    }
    int nesting = 0;
    for (const Type& a : args) {
        nesting = std::max(nesting, typeNesting(a));
    }
    if (nesting > 64) {
        error("E0725",
              at,
              "generic instantiation too deep",
              "the type arguments nest more than 64 levels");
        return std::nullopt;
    }
    // Instance names use `.` for qualified type names: `::` separates the module (see qualify).
    std::string shown;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string spelled = typeToString(args[i]);
        for (std::size_t p = spelled.find("::"); p != std::string::npos;
             p = spelled.find("::", p)) {
            spelled.replace(p, 2, ".");
        }
        shown += (i != 0 ? ", " : "") + spelled;
    }
    const std::string local = generic.local + "<" + shown + ">";
    if (owner.symbols_.find(local) == owner.symbols_.end()) {
        TypeEnv env;
        for (std::size_t i = 0; i < params.size(); ++i) {
            env[params[i]] = args[i];
        }
        owner.instances_.push_back(instantiateItem(gitem, env, local));
        const Item* item = &owner.instances_.back();
        Symbol sym;
        sym.item = item;
        if (std::holds_alternative<ClassDecl>(item->decl)) {
            sym.kind = Symbol::Kind::Class;
            owner.aggregate_order_.push_back(local);
        } else if (std::holds_alternative<StructDecl>(item->decl)) {
            sym.kind = Symbol::Kind::Struct;
            owner.aggregate_order_.push_back(local);
        } else if (std::holds_alternative<InterfaceDecl>(item->decl)) {
            sym.kind = Symbol::Kind::Interface;
        } else if (std::holds_alternative<EnumDecl>(item->decl)) {
            // `enum E<T>`: the tag and the union of a generic payload enum are enums too, and a
            // generic value enum keeps being an enum. None of them owns a layout.
            sym.kind = Symbol::Kind::Enum;
        } else {
            sym.kind = Symbol::Kind::Fn;
            sym.overloads = {item};
        }
        owner.symbols_.emplace(local, std::move(sym));
        owner.instance_info_[local] =
            InstanceInfo{generic.qname(), args, at, current_instance_, this};
        owner.item_instance_[item] = local;
        owner.pending_instances_.push_back(item);
    }
    return EntityRef{&owner, &owner.symbols_.at(local), local};
}

// The module key of the generic declaration an instance comes from: the instance's origin is
// `key::name` (`name` alone for the root module), so everything before the last `::`.
std::string Sema::genericOwnerKey(const std::string& local) const {
    const InstanceInfo* info = instanceInfo(local);
    if (info == nullptr) {
        return key_;
    }
    const std::size_t cut = info->generic.rfind("::");
    return cut == std::string::npos ? std::string{} : info->generic.substr(0, cut);
}

std::vector<const Item*> Sema::takePendingInstances() {
    std::vector<const Item*> out;
    out.swap(pending_instances_);
    return out;
}

const Sema::InstanceInfo* Sema::instanceInfo(const std::string& local) const {
    const auto it = instance_info_.find(local);
    return it == instance_info_.end() ? nullptr : &it->second;
}

const Item* Sema::addImplInstance(Item item) {
    instances_.push_back(std::move(item));
    const Item* out = &instances_.back();
    pending_instances_.push_back(out);
    return out;
}

const Sema::MethodInstance* Sema::instantiateMethod(const FnDecl& generic,
                                                    const std::string& cls,
                                                    const std::string& cls_q,
                                                    const std::vector<Type>& args) {
    std::string key = cls_q + "." + generic.name + "<";
    for (std::size_t i = 0; i < args.size(); ++i) {
        key += (i == 0 ? "" : ", ") + typeToString(args[i]);
    }
    key += ">";
    if (const auto it = method_index_.find(key); it != method_index_.end()) {
        return it->second;
    }
    TypeEnv env;
    for (std::size_t i = 0; i < generic.generic_params.size() && i < args.size(); ++i) {
        env[generic.generic_params[i]] = args[i];
    }
    method_instances_.push_back(MethodInstance{instantiateFn(generic, env), key, cls, cls_q});
    const MethodInstance* out = &method_instances_.back();
    method_index_.emplace(key, out);
    pending_methods_.push_back(out);
    return out;
}

std::vector<const Sema::MethodInstance*> Sema::takePendingMethods() {
    std::vector<const MethodInstance*> out;
    out.swap(pending_methods_);
    return out;
}

const Sema::Symbol* Sema::find(const std::string& name) const {
    const auto it = symbols_.find(name);
    return it == symbols_.end() ? nullptr : &it->second;
}

std::vector<std::string> Sema::aggregateNames() const {
    return aggregate_order_;
}

} // namespace xlang
