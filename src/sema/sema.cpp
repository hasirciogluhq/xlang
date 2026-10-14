#include "xlang/sema/sema.h"

#include <algorithm>
#include <format>
#include <functional>
#include <numeric>
#include <random>

namespace xlang {

// ---------------------------------------------------------------------------------------------
// ConstValue

ConstValue ConstValue::makeInt(Int128 v) {
    ConstValue c;
    c.kind = Kind::Int;
    c.int_value = v;
    return c;
}

ConstValue ConstValue::makeFloat(double v) {
    ConstValue c;
    c.kind = Kind::Float;
    c.float_value = v;
    return c;
}

ConstValue ConstValue::makeBool(bool v) {
    ConstValue c;
    c.kind = Kind::Bool;
    c.bool_value = v;
    return c;
}

ConstValue ConstValue::makeString(std::string v) {
    ConstValue c;
    c.kind = Kind::String;
    c.string_value = std::move(v);
    return c;
}

namespace {

[[nodiscard]] std::string int128ToString(Int128 v) {
    if (v == 0) {
        return "0";
    }
    const bool negative = v < 0;
    unsigned __int128 u = negative ? static_cast<unsigned __int128>(-(v + 1)) + 1
                                   : static_cast<unsigned __int128>(v);
    std::string out;
    while (u != 0) {
        out += static_cast<char>('0' + static_cast<int>(u % 10));
        u /= 10;
    }
    if (negative) {
        out += '-';
    }
    std::reverse(out.begin(), out.end());
    return out;
}

[[nodiscard]] std::uint64_t alignUp(std::uint64_t value, std::uint64_t align) {
    return align <= 1 ? value : (value + align - 1) / align * align;
}

[[nodiscard]] const Attribute* findAttr(const std::vector<Attribute>& attrs, std::string_view name) {
    for (const Attribute& a : attrs) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

[[nodiscard]] bool attrHasIdent(const Attribute& a, std::string_view text) {
    return std::ranges::any_of(a.args, [&](const AttributeArg& arg) {
        return arg.key.empty() && arg.kind == AttributeArg::Kind::Ident && arg.text == text;
    });
}

[[nodiscard]] const AttributeArg* attrKey(const Attribute& a, std::string_view key) {
    for (const AttributeArg& arg : a.args) {
        if (arg.key == key) {
            return &arg;
        }
    }
    return nullptr;
}

/// Wraps `value` to the range of an integer type.
[[nodiscard]] Int128 wrapInt(Int128 value, unsigned bits, bool is_signed) {
    if (bits >= 64) {
        const auto u = static_cast<std::uint64_t>(value);
        return is_signed ? static_cast<Int128>(static_cast<std::int64_t>(u)) : static_cast<Int128>(u);
    }
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
    const std::uint64_t u = static_cast<std::uint64_t>(value) & mask;
    if (is_signed && (u >> (bits - 1)) != 0) {
        return static_cast<Int128>(u) - (static_cast<Int128>(1) << bits);
    }
    return static_cast<Int128>(u);
}

}  // namespace

std::string ConstValue::toString() const {
    switch (kind) {
    case Kind::Int: return int128ToString(int_value);
    case Kind::Float: return std::format("{}", float_value);
    case Kind::Bool: return bool_value ? "true" : "false";
    case Kind::String: return "\"" + string_value + "\"";
    }
    return {};
}

// ---------------------------------------------------------------------------------------------
// Sema: setup and symbols

Sema::Sema(const Program& program, const BuildConfig& config, DiagnosticEngine& diagnostics,
           const LayoutHistory* history)
    : program_(program), config_(config), diag_(diagnostics), history_(history) {
    // Builtin `MemoryOrder` is an ordinary enum (plan 6.1 / 2.6).
    EnumDecl order;
    order.name = "MemoryOrder";
    order.has_underlying = true;
    order.underlying = Type::makeInt(8, false);
    for (const char* member : {"relaxed", "acquire", "release", "acqRel", "seqCst"}) {
        EnumMember m;
        m.name = member;
        order.members.push_back(std::move(m));
    }
    memory_order_item_.decl = std::move(order);
    collect();
}

void Sema::error(const char* code, const Span& at, std::string message, std::string label) {
    const std::string key = std::format("{}:{}:{}:{}:{}", code, at.file, at.line, at.column, message);
    if (!reported_.insert(key).second) {
        return;
    }
    diag_.error(code, at, std::move(message), std::move(label));
}

void Sema::notConstant(const Span& at, std::string why) {
    error("E0707", at, "expression is not a compile-time constant", std::move(why));
}

void Sema::collect() {
    symbols_["MemoryOrder"] = Symbol{Symbol::Kind::Enum, &memory_order_item_};
    for (const Item& item : program_.items) {
        Symbol sym;
        sym.item = &item;
        const DeclBase& base = declBase(item);
        bool named = true;
        if (std::holds_alternative<StructDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Struct;
            aggregate_order_.push_back(base.name);
        } else if (std::holds_alternative<ClassDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Class;
            aggregate_order_.push_back(base.name);
        } else if (std::holds_alternative<InterfaceDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Interface;
        } else if (std::holds_alternative<EnumDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Enum;
        } else if (std::holds_alternative<TypeAliasDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Alias;
        } else if (std::holds_alternative<DefineConstDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Define;
        } else if (std::holds_alternative<GlobalDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Global;
        } else if (std::holds_alternative<FnDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Fn;
        } else if (std::holds_alternative<DefineMacroDecl>(item.decl)) {
            sym.kind = Symbol::Kind::Macro;
        } else {
            named = false;
        }
        if (!named) {
            continue;
        }
        const auto [it, inserted] = symbols_.emplace(base.name, sym);
        if (!inserted && it->second.item != &memory_order_item_) {
            const DeclBase& first = declBase(*it->second.item);
            Diagnostic d;
            d.code = "E0401";
            d.severity = Severity::Error;
            d.message = std::format("`{}` is defined twice", base.name);
            d.labels.push_back({first.name_span, "first definition"});
            d.labels.push_back({base.name_span, "second definition"});
            d.fix = "rename one of them";
            diag_.report(std::move(d));
        }
    }
}

const Sema::Symbol* Sema::find(const std::string& name) const {
    const auto it = symbols_.find(name);
    return it == symbols_.end() ? nullptr : &it->second;
}

std::vector<std::string> Sema::aggregateNames() const {
    return aggregate_order_;
}

// ---------------------------------------------------------------------------------------------
// Types

std::optional<Type> Sema::resolve(const Type& type) {
    switch (type.kind) {
    case TypeKind::Pointer: {
        auto inner = resolve(*type.elem);
        if (!inner) {
            return std::nullopt;
        }
        Type out = type;
        out.elem = std::make_shared<const Type>(std::move(*inner));
        return out;
    }
    case TypeKind::Array: {
        auto inner = resolve(*type.elem);
        if (!inner) {
            return std::nullopt;
        }
        Type out = type;
        out.elem = std::make_shared<const Type>(std::move(*inner));
        return out;
    }
    case TypeKind::FnPtr: {
        Type out = type;
        out.params.clear();
        for (const Type& p : type.params) {
            auto r = resolve(p);
            if (!r) {
                return std::nullopt;
            }
            out.params.push_back(std::move(*r));
        }
        auto ret = resolve(*type.ret);
        if (!ret) {
            return std::nullopt;
        }
        out.ret = std::make_shared<const Type>(std::move(*ret));
        return out;
    }
    case TypeKind::Named:
        break;
    default:
        return type;
    }

    if (type.path.size() != 1) {
        return type;  // `alias.Name`: needs module loading
    }
    const std::string& name = type.path.front();
    const Symbol* sym = find(name);
    if (sym == nullptr) {
        error("E0706", type.span, std::format("unknown type `{}`", name), "not declared in this module");
        return std::nullopt;
    }
    Type out = type;
    switch (sym->kind) {
    case Symbol::Kind::Struct: out.kind = TypeKind::Struct; return out;
    case Symbol::Kind::Class: out.kind = TypeKind::Class; return out;
    case Symbol::Kind::Interface: out.kind = TypeKind::Interface; return out;
    case Symbol::Kind::Enum: {
        const auto& decl = std::get<EnumDecl>(sym->item->decl);
        out.kind = TypeKind::Enum;
        auto under = resolve(decl.underlying);
        if (!under) {
            return std::nullopt;
        }
        out.elem = std::make_shared<const Type>(std::move(*under));
        return out;
    }
    case Symbol::Kind::Alias: {
        if (!alias_in_progress_.insert(name).second) {
            error("E0704", type.span, std::format("type alias `{}` refers to itself", name),
                  "cyclic alias");
            return std::nullopt;
        }
        auto target = resolve(std::get<TypeAliasDecl>(sym->item->decl).target);
        alias_in_progress_.erase(name);
        if (!target) {
            return std::nullopt;
        }
        target->span = type.span;
        return target;
    }
    default:
        error("E0706", type.span, std::format("`{}` is not a type", name), "this name is not a type");
        return std::nullopt;
    }
}

std::optional<Type> Sema::exprToType(const Expr& expr) {
    switch (expr.kind) {
    case Expr::Kind::Name: {
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

// ---------------------------------------------------------------------------------------------
// Constant evaluation

std::optional<ConstValue> Sema::fromConfig(const ConfigValue& v) {
    switch (v.kind) {
    case ConfigValue::Kind::Bool: return ConstValue::makeBool(v.bool_value);
    case ConfigValue::Kind::Int: return ConstValue::makeInt(v.int_value);
    case ConfigValue::Kind::String: return ConstValue::makeString(v.string_value);
    }
    return std::nullopt;
}

std::optional<ConstValue> Sema::evalConst(const Expr& expr) {
    switch (expr.kind) {
    case Expr::Kind::IntLiteral: return ConstValue::makeInt(static_cast<Int128>(expr.int_value));
    case Expr::Kind::FloatLiteral: return ConstValue::makeFloat(expr.float_value);
    case Expr::Kind::BoolLiteral: return ConstValue::makeBool(expr.bool_value);
    case Expr::Kind::StringLiteral: return ConstValue::makeString(expr.text);
    case Expr::Kind::Name: {
        // `-D NAME=...` and builtins (`os`, `pointerWidth`, ...) come first: CLI wins over `define`.
        if (const auto cfg = config_.lookup(expr.text)) {
            return fromConfig(*cfg);
        }
        const Symbol* sym = find(expr.text);
        if (sym == nullptr || sym->kind != Symbol::Kind::Define) {
            notConstant(expr.span, std::format("`{}` is not a `define` constant", expr.text));
            return std::nullopt;
        }
        if (const auto it = define_cache_.find(expr.text); it != define_cache_.end()) {
            return it->second;
        }
        if (!define_in_progress_.insert(expr.text).second) {
            error("E0707", expr.span, std::format("`define {}` depends on itself", expr.text),
                  "cyclic constant");
            return std::nullopt;
        }
        const auto& decl = std::get<DefineConstDecl>(sym->item->decl);
        std::optional<ConstValue> value = evalConst(*decl.value);
        define_in_progress_.erase(expr.text);
        define_cache_[expr.text] = value;
        return value;
    }
    case Expr::Kind::Field: {
        if (expr.lhs->kind == Expr::Kind::Name) {
            const Symbol* sym = find(expr.lhs->text);
            if (sym != nullptr && sym->kind == Symbol::Kind::Enum) {
                return enumMember(expr.lhs->text, expr.text, expr.span);
            }
        }
        notConstant(expr.span, "only enum members can be read as constants");
        return std::nullopt;
    }
    case Expr::Kind::Unary: {
        auto v = evalConst(*expr.lhs);
        if (!v) {
            return std::nullopt;
        }
        if (expr.unary_op == UnaryOp::Neg) {
            if (v->kind == ConstValue::Kind::Int) {
                return ConstValue::makeInt(-v->int_value);
            }
            if (v->kind == ConstValue::Kind::Float) {
                return ConstValue::makeFloat(-v->float_value);
            }
        } else if (expr.unary_op == UnaryOp::Not && v->kind == ConstValue::Kind::Bool) {
            return ConstValue::makeBool(!v->bool_value);
        }
        notConstant(expr.span, "operator does not apply to this constant");
        return std::nullopt;
    }
    case Expr::Kind::Binary:
        return evalBinary(expr);
    case Expr::Kind::Cast:
        return evalCast(expr);
    case Expr::Kind::Call:
        return evalCall(expr);
    default:
        notConstant(expr.span, "this form cannot be evaluated at compile time");
        return std::nullopt;
    }
}

std::optional<ConstValue> Sema::evalBinary(const Expr& expr) {
    auto l = evalConst(*expr.lhs);
    if (!l) {
        return std::nullopt;
    }
    if (expr.binary_op == BinaryOp::And || expr.binary_op == BinaryOp::Or) {
        if (l->kind != ConstValue::Kind::Bool) {
            notConstant(expr.lhs->span, "`&&`/`||` need bool operands");
            return std::nullopt;
        }
        // Short-circuit: the unevaluated side may be non-constant.
        if (expr.binary_op == BinaryOp::And && !l->bool_value) {
            return ConstValue::makeBool(false);
        }
        if (expr.binary_op == BinaryOp::Or && l->bool_value) {
            return ConstValue::makeBool(true);
        }
        auto r = evalConst(*expr.rhs);
        if (!r) {
            return std::nullopt;
        }
        if (r->kind != ConstValue::Kind::Bool) {
            notConstant(expr.rhs->span, "`&&`/`||` need bool operands");
            return std::nullopt;
        }
        return ConstValue::makeBool(r->bool_value);
    }
    auto r = evalConst(*expr.rhs);
    if (!r) {
        return std::nullopt;
    }

    const auto cmpResult = [&](int c) {
        switch (expr.binary_op) {
        case BinaryOp::Eq: return c == 0;
        case BinaryOp::Ne: return c != 0;
        case BinaryOp::Lt: return c < 0;
        case BinaryOp::Le: return c <= 0;
        case BinaryOp::Gt: return c > 0;
        default: return c >= 0;
        }
    };
    const bool is_cmp = expr.binary_op >= BinaryOp::Eq && expr.binary_op <= BinaryOp::Ge;

    if (l->kind == ConstValue::Kind::String || r->kind == ConstValue::Kind::String) {
        if (l->kind == r->kind && is_cmp) {
            return ConstValue::makeBool(cmpResult(l->string_value.compare(r->string_value)));
        }
        notConstant(expr.span, "strings only support comparison with another string");
        return std::nullopt;
    }
    if (l->kind == ConstValue::Kind::Bool && r->kind == ConstValue::Kind::Bool && is_cmp) {
        return ConstValue::makeBool(cmpResult(static_cast<int>(l->bool_value) - static_cast<int>(r->bool_value)));
    }
    if (l->kind == ConstValue::Kind::Bool || r->kind == ConstValue::Kind::Bool) {
        notConstant(expr.span, "bool operands only support `==`, `!=`, `&&`, `||`");
        return std::nullopt;
    }

    if (l->kind == ConstValue::Kind::Int && r->kind == ConstValue::Kind::Int) {
        const Int128 a = l->int_value;
        const Int128 b = r->int_value;
        if (is_cmp) {
            return ConstValue::makeBool(cmpResult(a < b ? -1 : (a > b ? 1 : 0)));
        }
        switch (expr.binary_op) {
        case BinaryOp::Add: return ConstValue::makeInt(a + b);
        case BinaryOp::Sub: return ConstValue::makeInt(a - b);
        case BinaryOp::Mul: return ConstValue::makeInt(a * b);
        case BinaryOp::Div:
        case BinaryOp::Mod:
            if (b == 0) {
                error("E0707", expr.span, "division by zero in a constant expression", "divisor is 0");
                return std::nullopt;
            }
            return ConstValue::makeInt(expr.binary_op == BinaryOp::Div ? a / b : a % b);
        default: break;
        }
    }

    const auto toDouble = [](const ConstValue& v) {
        return v.kind == ConstValue::Kind::Int ? static_cast<double>(v.int_value) : v.float_value;
    };
    const double a = toDouble(*l);
    const double b = toDouble(*r);
    if (is_cmp) {
        return ConstValue::makeBool(cmpResult(a < b ? -1 : (a > b ? 1 : 0)));
    }
    switch (expr.binary_op) {
    case BinaryOp::Add: return ConstValue::makeFloat(a + b);
    case BinaryOp::Sub: return ConstValue::makeFloat(a - b);
    case BinaryOp::Mul: return ConstValue::makeFloat(a * b);
    case BinaryOp::Div: return ConstValue::makeFloat(a / b);
    default:
        notConstant(expr.span, "`%` needs integer operands");
        return std::nullopt;
    }
}

std::optional<ConstValue> Sema::evalCast(const Expr& expr) {
    auto v = evalConst(*expr.lhs);
    if (!v) {
        return std::nullopt;
    }
    auto target = resolve(expr.type);
    if (!target) {
        return std::nullopt;
    }
    if (target->kind == TypeKind::Enum) {
        return v->kind == ConstValue::Kind::Int ? v : std::optional<ConstValue>{};
    }
    switch (target->kind) {
    case TypeKind::Int: {
        Int128 raw = 0;
        switch (v->kind) {
        case ConstValue::Kind::Int: raw = v->int_value; break;
        case ConstValue::Kind::Float: raw = static_cast<Int128>(v->float_value); break;
        case ConstValue::Kind::Bool: raw = v->bool_value ? 1 : 0; break;
        case ConstValue::Kind::String:
            notConstant(expr.span, "a string address is not a constant integer");
            return std::nullopt;
        }
        const unsigned bits = target->bits == 0 ? config_.target.pointer_width : target->bits;
        return ConstValue::makeInt(wrapInt(raw, bits, target->is_signed));
    }
    case TypeKind::Float:
        if (v->kind == ConstValue::Kind::Int) {
            return ConstValue::makeFloat(static_cast<double>(v->int_value));
        }
        if (v->kind == ConstValue::Kind::Float) {
            return v->float_value == v->float_value && target->bits == 32
                       ? ConstValue::makeFloat(static_cast<double>(static_cast<float>(v->float_value)))
                       : v;
        }
        break;
    case TypeKind::Bool:
        if (v->kind == ConstValue::Kind::Bool) {
            return v;
        }
        if (v->kind == ConstValue::Kind::Int) {
            return ConstValue::makeBool(v->int_value != 0);
        }
        break;
    default:
        break;
    }
    notConstant(expr.span, std::format("cast to `{}` is not a constant", typeToString(*target)));
    return std::nullopt;
}

std::optional<ConstValue> Sema::evalCall(const Expr& expr) {
    if (expr.lhs->kind != Expr::Kind::Name) {
        notConstant(expr.span, "only `sizeOf`, `alignOf` and `offsetOf` calls are constants");
        return std::nullopt;
    }
    const std::string& callee = expr.lhs->text;
    if (callee == "sizeOf" || callee == "alignOf") {
        if (expr.args.size() != 1) {
            error("E0707", expr.span, std::format("`{}` takes one type argument", callee), "wrong argument count");
            return std::nullopt;
        }
        auto type = exprToType(*expr.args[0]);
        if (!type) {
            notConstant(expr.args[0]->span, "expected a type");
            return std::nullopt;
        }
        auto layout = layoutOf(*type, expr.args[0]->span);
        if (!layout) {
            return std::nullopt;
        }
        return ConstValue::makeInt(callee == "sizeOf" ? layout->size : layout->align);
    }
    if (callee == "offsetOf") {
        if (expr.args.size() != 2 || expr.args[1]->kind != Expr::Kind::Name) {
            error("E0707", expr.span, "`offsetOf` takes a type and a field name", "wrong arguments");
            return std::nullopt;
        }
        auto type = exprToType(*expr.args[0]);
        auto resolved = type ? resolve(*type) : std::nullopt;
        if (!resolved || (resolved->kind != TypeKind::Struct && resolved->kind != TypeKind::Class)) {
            error("E0707", expr.args[0]->span, "`offsetOf` needs a struct or class", "not an aggregate");
            return std::nullopt;
        }
        auto layout = layoutOf(*resolved, expr.args[0]->span);
        if (!layout) {
            return std::nullopt;
        }
        for (const FieldLayout& f : layout->fields) {
            if (f.name == expr.args[1]->text) {
                return ConstValue::makeInt(f.offset);
            }
        }
        error("E0707", expr.args[1]->span,
              std::format("`{}` has no field `{}`", typeToString(*resolved), expr.args[1]->text),
              "unknown field");
        return std::nullopt;
    }
    notConstant(expr.span, std::format("call of `{}` cannot be evaluated at compile time", callee));
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// Enums

std::optional<ConstValue> Sema::enumMember(const std::string& enum_name, const std::string& member,
                                           const Span& at) {
    const Symbol* sym = find(enum_name);
    if (sym == nullptr || sym->kind != Symbol::Kind::Enum) {
        error("E0706", at, std::format("unknown enum `{}`", enum_name), "not an enum");
        return std::nullopt;
    }
    const auto& decl = std::get<EnumDecl>(sym->item->decl);
    auto& values = enum_values_[enum_name];
    if (values.empty() && !decl.members.empty()) {
        if (!enum_in_progress_.insert(enum_name).second) {
            return std::nullopt;
        }
        const auto under = resolve(decl.underlying);
        const bool integral = under && under->kind == TypeKind::Int;
        Int128 next = 0;
        for (const EnumMember& m : decl.members) {
            std::optional<ConstValue> v;
            if (m.value) {
                v = evalConst(*m.value);
                if (v && v->kind == ConstValue::Kind::Int) {
                    next = v->int_value + 1;
                }
            } else if (integral) {
                v = ConstValue::makeInt(next);
                ++next;
            }
            values.push_back(std::move(v));
        }
        enum_in_progress_.erase(enum_name);
    }
    for (std::size_t i = 0; i < decl.members.size() && i < values.size(); ++i) {
        if (decl.members[i].name == member) {
            return values[i];
        }
    }
    error("E0706", at, std::format("enum `{}` has no member `{}`", enum_name, member), "unknown member");
    return std::nullopt;
}

void Sema::checkEnum(const EnumDecl& decl) {
    const auto under = resolve(decl.underlying);
    if (!under) {
        return;
    }
    const bool integral = under->kind == TypeKind::Int;
    const bool constant_capable = integral || under->kind == TypeKind::Float ||
                                  under->kind == TypeKind::Bool ||
                                  (under->kind == TypeKind::Pointer && under->elem &&
                                   under->elem->kind == TypeKind::Int && under->elem->bits == 8 &&
                                   !under->elem->is_signed);
    if (!constant_capable) {
        error("E0705", decl.underlying.span.valid() ? decl.underlying.span : decl.name_span,
              std::format("`{}` cannot be the underlying type of an enum", typeToString(*under)),
              "must hold a constant: integer, float, bool or *ui8");
        return;
    }
    std::set<std::string> names;
    std::vector<std::pair<Int128, std::string>> seen;
    for (const EnumMember& m : decl.members) {
        if (!names.insert(m.name).second) {
            error("E0705", m.span, std::format("enum member `{}` is defined twice", m.name), "duplicate member");
        }
        if (!m.value && !integral) {
            error("E0705", m.span, std::format("member `{}` needs an explicit value", m.name),
                  "only integer enums are auto-numbered");
            continue;
        }
        const auto value = enumMember(decl.name, m.name, m.span);
        if (!value) {
            continue;
        }
        if (m.value) {
            const bool ok = (integral && value->kind == ConstValue::Kind::Int) ||
                            (under->kind == TypeKind::Float && value->kind != ConstValue::Kind::String &&
                             value->kind != ConstValue::Kind::Bool) ||
                            (under->kind == TypeKind::Bool && value->kind == ConstValue::Kind::Bool) ||
                            (under->kind == TypeKind::Pointer && value->kind == ConstValue::Kind::String);
            if (!ok) {
                error("E0705", m.value->span,
                      std::format("value of `{}` does not match the underlying type `{}`", m.name,
                                  typeToString(*under)),
                      "wrong kind of constant");
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Layout

std::optional<std::uint64_t> Sema::primitiveSize(const Type& t, std::uint64_t& align) {
    switch (t.kind) {
    case TypeKind::Bool:
        align = 1;
        return 1;
    case TypeKind::Int: {
        const unsigned bits = t.bits == 0 ? config_.target.pointer_width : t.bits;
        align = bits / 8;
        return bits / 8;
    }
    case TypeKind::Float:
        align = t.bits / 8;
        return t.bits / 8;
    case TypeKind::Pointer:
    case TypeKind::FnPtr:
        align = config_.target.pointer_width / 8;
        return align;
    case TypeKind::Interface:
        align = config_.target.pointer_width / 8;
        return 2 * align;
    default:
        return std::nullopt;
    }
}

std::optional<TypeLayout> Sema::layoutOf(const Type& type, const Span& at) {
    auto resolved = resolve(type);
    if (!resolved) {
        return std::nullopt;
    }
    const Type& t = *resolved;
    TypeLayout out;
    out.scheme = config_.layout_scheme.empty() ? kCurrentLayoutScheme : config_.layout_scheme;
    switch (t.kind) {
    case TypeKind::Void:
        error("E0703", at, "`void` has no size", "sizeless type");
        return std::nullopt;
    case TypeKind::Enum:
        return layoutOf(*t.elem, at);
    case TypeKind::Array: {
        const Expr* len = t.length.get();
        auto value = len != nullptr ? evalConst(*len) : std::nullopt;
        if (!value || value->kind != ConstValue::Kind::Int || value->int_value < 0) {
            if (value) {
                error("E0707", len->span, "array length must be a non-negative integer constant",
                      "invalid length");
            }
            return std::nullopt;
        }
        auto elem = layoutOf(*t.elem, at);
        if (!elem) {
            return std::nullopt;
        }
        out.align = elem->align;
        out.size = elem->size * static_cast<std::uint64_t>(value->int_value);
        out.data_size = out.size;
        return out;
    }
    case TypeKind::Struct:
    case TypeKind::Class:
        return layoutOfName(t.path.front(), at);
    case TypeKind::Named:
        error("E0703", at, std::format("size of `{}` is unknown", typeToString(t)),
              "type comes from another module (module loading not available here)");
        return std::nullopt;
    default: {
        std::uint64_t align = 1;
        const auto size = primitiveSize(t, align);
        if (!size) {
            return std::nullopt;
        }
        out.size = *size;
        out.align = align;
        out.data_size = *size;
        return out;
    }
    }
}

std::optional<TypeLayout> Sema::layoutOfName(const std::string& name, const Span& at) {
    if (const auto it = layout_cache_.find(name); it != layout_cache_.end()) {
        return it->second;
    }
    const Symbol* sym = find(name);
    if (sym == nullptr || (sym->kind != Symbol::Kind::Struct && sym->kind != Symbol::Kind::Class)) {
        error("E0706", at, std::format("unknown type `{}`", name), "not a struct or class");
        return std::nullopt;
    }
    if (!layout_in_progress_.insert(name).second) {
        error("E0703", at, std::format("`{}` contains itself by value", name),
              "recursive type has infinite size; use a pointer");
        return std::nullopt;
    }
    auto result = aggregateLayout(name, *sym->item, at);
    layout_in_progress_.erase(name);
    layout_cache_[name] = result;
    return result;
}

std::optional<TypeLayout> Sema::aggregateLayout(const std::string& name, const Item& item, const Span& at) {
    const DeclBase& base_decl = declBase(item);
    const std::vector<FieldDecl>* fields = nullptr;
    const ClassDecl* cls = nullptr;
    bool opaque = false;
    if (const auto* s = std::get_if<StructDecl>(&item.decl)) {
        fields = &s->fields;
        opaque = s->opaque;
    } else {
        cls = &std::get<ClassDecl>(item.decl);
        fields = &cls->fields;
        opaque = cls->opaque;
    }
    if (opaque) {
        error("E0703", at, std::format("`{}` is opaque and has no known size", name),
              "opaque `declare` types can only be used behind a pointer");
        return std::nullopt;
    }

    TypeLayout out;
    out.scheme = config_.layout_scheme.empty() ? kCurrentLayoutScheme : config_.layout_scheme;
    const Attribute* repr = findAttr(base_decl.attributes, "repr");
    out.repr_c = repr != nullptr && attrHasIdent(*repr, "c");
    out.packed = findAttr(base_decl.attributes, "packed") != nullptr;
    if (const Attribute* layout_attr = findAttr(base_decl.attributes, "layout")) {
        if (const AttributeArg* scheme = attrKey(*layout_attr, "scheme")) {
            if (scheme->text != kCurrentLayoutScheme) {
                error("E0705", scheme->span, std::format("unknown layout scheme `{}`", scheme->text),
                      "known schemes: v1");
            }
            out.scheme = scheme->text;
        }
        if (attrHasIdent(*layout_attr, "shuffle")) {
            out.shuffled = true;
            const AttributeArg* seed = attrKey(*layout_attr, "seed");
            out.seed = seed != nullptr ? static_cast<std::uint64_t>(seed->number)
                                       : std::hash<std::string>{}(name);
        }
    }
    std::uint64_t min_align = 1;
    if (const Attribute* a = findAttr(base_decl.attributes, "align")) {
        if (!a->args.empty() && a->args[0].kind == AttributeArg::Kind::Int && a->args[0].number > 0 &&
            (a->args[0].number & (a->args[0].number - 1)) == 0) {
            min_align = static_cast<std::uint64_t>(a->args[0].number);
        } else {
            error("E0705", a->span, "`@align(N)` needs a power-of-two integer", "invalid alignment");
        }
    }

    // Declared types without `@repr(c)`: recorded layout -> project history -> C assumption.
    if (item.modifiers.declared && !out.repr_c) {
        if (history_ != nullptr) {
            if (const LayoutRecord* rec = history_->find(name)) {
                TypeLayout recorded = rec->layout;
                recorded.source = "record";
                const std::string current = config_.layout_scheme.empty() ? kCurrentLayoutScheme
                                                                          : config_.layout_scheme;
                if (recorded.scheme != current) {
                    diag_.warning("W0203", base_decl.name_span,
                                  std::format("layout record of `{}` uses scheme {}, current is {}", name,
                                              recorded.scheme, current),
                                  "record scheme differs");
                }
                std::vector<std::string> declared_names;
                for (const FieldDecl& f : *fields) {
                    declared_names.push_back(f.name);
                }
                std::vector<std::string> recorded_names;
                for (const FieldLayout& f : recorded.fields) {
                    recorded_names.push_back(f.name);
                }
                std::sort(declared_names.begin(), declared_names.end());
                std::sort(recorded_names.begin(), recorded_names.end());
                if (!fields->empty() && declared_names != recorded_names) {
                    diag_.warning("W0202", base_decl.name_span,
                                  std::format("declared fields of `{}` differ from its recorded layout", name),
                                  "fields do not match the record");
                }
                // Use the record's offsets, keep types from the declaration where names match.
                for (FieldLayout& rf : recorded.fields) {
                    for (const FieldDecl& f : *fields) {
                        if (f.name == rf.name) {
                            rf.type = f.type;
                        }
                    }
                }
                return recorded;
            }
        }
        Diagnostic d;
        d.code = "W0201";
        d.severity = Severity::Warning;
        d.message = std::format("layout of `{}` is unknown, C layout assumed", name);
        d.labels.push_back({base_decl.name_span, "this type comes from outside"});
        d.detail = std::format("No layout record for `{}` in `xl.layout`.", name);
        d.reason = "The alignment and padding of the other side cannot be verified.";
        d.fix = "add `@repr(c)` if the other side is C/C++, build it with xl so a record exists, "
                "or silence with `@allow(W0201)`";
        diag_.report(std::move(d));
        out.repr_c = true;
        out.source = "assumed-c";
    }

    const std::uint64_t ptr = config_.target.pointer_width / 8;
    std::uint64_t cursor = 0;
    std::uint64_t align = 1;

    if (cls != nullptr) {
        out.has_vptr = std::ranges::any_of(cls->methods, [](const FnDecl& m) { return m.is_virtual; });
        if (cls->has_base) {
            if (cls->base.size() != 1) {
                error("E0703", base_decl.name_span, "base class from another module is not resolvable here",
                      "module loading not available");
                return std::nullopt;
            }
            const Symbol* bsym = find(cls->base[0]);
            if (bsym == nullptr || bsym->kind != Symbol::Kind::Class) {
                error("E0706", base_decl.name_span, std::format("base `{}` is not a class", cls->base[0]),
                      "single inheritance needs a class base");
                return std::nullopt;
            }
            auto base = layoutOfName(cls->base[0], at);
            if (!base) {
                return std::nullopt;
            }
            for (FieldLayout f : base->fields) {
                f.inherited = true;
                out.fields.push_back(std::move(f));
            }
            out.has_vptr = out.has_vptr || base->has_vptr;
            // Itanium: a polymorphic (non-POD) base lets derived members reuse its tail padding.
            cursor = (out.repr_c && base->has_vptr) ? base->data_size : base->size;
            align = base->align;
        } else if (out.has_vptr) {
            cursor = ptr;
            align = ptr;
        }
        if (out.has_vptr && cursor < ptr) {
            cursor = ptr;
        }
    }

    struct Pending {
        const FieldDecl* decl;
        std::uint64_t size;
        std::uint64_t align;
        std::size_t index;
    };
    std::vector<Pending> pending;
    for (std::size_t i = 0; i < fields->size(); ++i) {
        const FieldDecl& f = (*fields)[i];
        auto resolved = resolve(f.type);
        auto fl = resolved ? layoutOf(*resolved, f.span) : std::nullopt;
        if (!fl) {
            return std::nullopt;
        }
        pending.push_back({&f, fl->size, out.packed ? 1 : fl->align, i});
    }

    std::vector<std::size_t> order(pending.size());
    std::iota(order.begin(), order.end(), 0);
    if (!out.repr_c && !out.packed) {
        // Scheme v1: stable sort by alignment, largest first. Frozen once released.
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return pending[a].align > pending[b].align;
        });
        if (out.shuffled) {
            std::mt19937_64 rng(out.seed);
            std::shuffle(order.begin(), order.end(), rng);
        }
    }

    std::vector<FieldLayout> placed(pending.size());
    for (const std::size_t idx : order) {
        const Pending& p = pending[idx];
        const std::uint64_t off = alignUp(cursor, p.align);
        FieldLayout fl;
        fl.name = p.decl->name;
        fl.type = p.decl->type;
        fl.offset = off;
        fl.size = p.size;
        fl.align = p.align;
        placed[idx] = std::move(fl);
        cursor = off + p.size;
        align = std::max(align, p.align);
    }
    for (FieldLayout& f : placed) {
        out.fields.push_back(std::move(f));
    }

    out.data_size = cursor;
    align = std::max(align, min_align);
    if (cls != nullptr && !cls->has_base && !out.has_vptr && fields->empty() && out.repr_c) {
        cursor = std::max<std::uint64_t>(cursor, 1);  // C++: an empty class is one byte
    }
    out.align = align;
    out.size = alignUp(cursor, align);
    if (out.source == "computed" && item.modifiers.declared) {
        out.source = out.repr_c ? "computed" : out.source;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Whole-module checks

void Sema::checkType(const Type& type, const Span& at) {
    auto resolved = resolve(type);
    if (!resolved) {
        return;
    }
    // Array lengths anywhere in the tree must be constants.
    const std::function<void(const Type&)> walk = [&](const Type& t) {
        if (t.kind == TypeKind::Array) {
            if (t.length != nullptr) {
                const auto v = evalConst(*t.length);
                if (v && (v->kind != ConstValue::Kind::Int || v->int_value < 0)) {
                    error("E0707", t.length->span, "array length must be a non-negative integer constant",
                          "invalid length");
                }
            }
            if (t.elem) {
                walk(*t.elem);
            }
        } else if (t.kind == TypeKind::Pointer) {
            if (t.elem) {
                walk(*t.elem);
            }
        } else if (t.kind == TypeKind::FnPtr) {
            for (const Type& p : t.params) {
                walk(p);
            }
            if (t.ret) {
                walk(*t.ret);
            }
        }
    };
    walk(type);
    (void)at;
}

void Sema::checkExpr(const Expr& e) {
    if (e.kind == Expr::Kind::Cast || e.kind == Expr::Kind::New || e.kind == Expr::Kind::StructLiteral) {
        checkType(e.type, e.span);
    }
    if (e.lhs) {
        checkExpr(*e.lhs);
    }
    if (e.rhs) {
        checkExpr(*e.rhs);
    }
    for (const ExprPtr& a : e.args) {
        checkExpr(*a);
    }
    for (const FieldInit& f : e.fields) {
        if (f.value) {
            checkExpr(*f.value);
        }
    }
}

void Sema::checkStatements(const Block& block) {
    for (const Stmt& s : block.statements) {
        if (s.has_type) {
            checkType(s.type, s.span);
        }
        if (s.target) {
            checkExpr(*s.target);
        }
        if (s.expr) {
            checkExpr(*s.expr);
        }
        if (s.body) {
            checkStatements(*s.body);
        }
        if (s.else_body) {
            checkStatements(*s.else_body);
        }
    }
}

void Sema::checkStaticAssert(const TopLevelCallDecl& call) {
    checkStaticAssertCall(*call.call);
}

void Sema::checkStaticAssertCall(const Expr& c) {
    if (c.lhs->kind != Expr::Kind::Name || c.lhs->text != "staticAssert") {
        return;
    }
    if (c.args.empty() || c.args.size() > 2) {
        error("E0702", c.span, "`staticAssert` takes a condition and an optional message", "wrong arguments");
        return;
    }
    const auto value = evalConst(*c.args[0]);
    if (!value) {
        return;
    }
    if (value->kind != ConstValue::Kind::Bool) {
        error("E0702", c.args[0]->span, "`staticAssert` condition must be a bool constant", "not a bool");
        return;
    }
    if (!value->bool_value) {
        std::string message = "static assertion failed";
        if (c.args.size() == 2 && c.args[1]->kind == Expr::Kind::StringLiteral) {
            message += ": " + c.args[1]->text;
        }
        error("E0702", c.span, std::move(message), "this condition is false");
    }
}

void Sema::checkAggregate(const Item& item) {
    const DeclBase& base = declBase(item);
    for (const Attribute& a : base.attributes) {
        (void)a;
    }
    (void)layoutOfName(base.name, base.name_span);
}

void Sema::analyze() {
    for (const Item& item : program_.items) {
        const DeclBase& base = declBase(item);
        if (const auto* e = std::get_if<EnumDecl>(&item.decl)) {
            checkEnum(*e);
        } else if (const auto* a = std::get_if<TypeAliasDecl>(&item.decl)) {
            (void)resolve(a->target);
            checkType(a->target, base.span);
            Type self = Type::makeNamed({base.name});
            self.span = base.name_span;
            (void)resolve(self);  // cyclic aliases
        } else if (std::holds_alternative<DefineConstDecl>(item.decl)) {
            Expr name;
            name.kind = Expr::Kind::Name;
            name.text = base.name;
            name.span = base.name_span;
            if (!config_.lookup(base.name)) {
                (void)evalConst(name);
            }
        } else if (const auto* g = std::get_if<GlobalDecl>(&item.decl)) {
            checkType(g->type, base.span);
            if (g->init) {
                checkExpr(*g->init);
            }
        } else if (const auto* f = std::get_if<FnDecl>(&item.decl)) {
            for (const Param& p : f->params) {
                if (!p.is_self) {
                    checkType(p.type, p.span);
                }
            }
            checkType(f->return_type, base.span);
            if (f->has_body) {
                checkStatements(f->body);
            }
        } else if (std::holds_alternative<StructDecl>(item.decl) ||
                   std::holds_alternative<ClassDecl>(item.decl)) {
            const auto* fields = std::holds_alternative<StructDecl>(item.decl)
                                     ? &std::get<StructDecl>(item.decl).fields
                                     : &std::get<ClassDecl>(item.decl).fields;
            for (const FieldDecl& f : *fields) {
                checkType(f.type, f.span);
            }
            if (const auto* c = std::get_if<ClassDecl>(&item.decl)) {
                for (const FnDecl& m : c->methods) {
                    if (m.has_body) {
                        checkStatements(m.body);
                    }
                }
            }
            const bool opaque = std::holds_alternative<StructDecl>(item.decl)
                                    ? std::get<StructDecl>(item.decl).opaque
                                    : std::get<ClassDecl>(item.decl).opaque;
            if (!opaque) {
                checkAggregate(item);
            }
        } else if (const auto* impl = std::get_if<ImplDecl>(&item.decl)) {
            for (const FnDecl& m : impl->methods) {
                if (m.has_body) {
                    checkStatements(m.body);
                }
            }
        } else if (const auto* call = std::get_if<TopLevelCallDecl>(&item.decl)) {
            checkStaticAssert(*call);
        }
    }
}

}  // namespace xlang
