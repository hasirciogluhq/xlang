// Plan 4.1-4.12: scopes + name resolution + type checking of function bodies, lowered to HIR in
// one pass over the whole program. Module-level services (type resolution, constants, layouts)
// stay in `Sema`; this file adds cross-module entities, class vtables, interface tables,
// visibility and mangling.

#include "xlang/hir/hir.h"
#include "xlang/sema/compilation.h"
#include "xlang/sema/conversion.h"
#include "xlang/sema/scope.h"
#include "xlang/sema/sema.h"

#include <algorithm>
#include <format>
#include <set>

namespace xlang {

hir::Module lowerProgram(const std::vector<Sema*>& modules);

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

/// Default type of an untyped `const` from its value: integers i32 (i64 when they do not fit),
/// floats f64, bools, strings str. nullopt when the value is not a literal constant.
[[nodiscard]] std::optional<Type> literalConstType(Sema& sema, const Expr& init) {
    const auto v = sema.evalConst(init);
    if (!v) {
        return std::nullopt;
    }
    switch (v->kind) {
    case ConstValue::Kind::Int:
        return (v->int_value > 2147483647 || v->int_value < -2147483648LL)
                   ? Type::makeInt(64, true)
                   : Type::makeInt(32, true);
    case ConstValue::Kind::Float: return Type::makeFloat(64);
    case ConstValue::Kind::Bool: return Type::makeBool();
    case ConstValue::Kind::String: return Type::makeStr();
    }
    return std::nullopt;
}

/// Name of the variable at the root of a place (`c` for `c.x[1]`), without its module prefix.
[[nodiscard]] std::string placeRootName(const hir::Expr& e) {
    const hir::Expr* cur = &e;
    while ((cur->kind == hir::ExprKind::Field || cur->kind == hir::ExprKind::Index) && cur->lhs) {
        cur = cur->lhs.get();
    }
    const auto dot = cur->text.rfind('.');
    return dot == std::string::npos ? cur->text : cur->text.substr(dot + 1);
}

/// `s[i]`, `s.ptr` or `s.len` where `s` is a `str` (or a pointer to one): never assignable.
[[nodiscard]] bool isStrPlace(const hir::Expr& e) {
    if (e.kind == hir::ExprKind::Index) {
        return e.lhs && e.lhs->type.kind == TypeKind::Str;
    }
    if (e.kind == hir::ExprKind::Field && e.lhs) {
        const Type* owner = &e.lhs->type;
        if (e.through_pointer) {
            if (!e.lhs->type.elem) {
                return false;
            }
            owner = e.lhs->type.elem.get();
        }
        return owner->kind == TypeKind::Str;
    }
    return false;
}

/// Maximum number of syscall arguments (after the number) for the target; nullopt = no fixed
/// limit in the target table (Windows NT: the compiler spills to the stack, plan 5.3).
[[nodiscard]] std::optional<unsigned> syscallMaxArgs(const TargetInfo& t) {
    if (t.os == "windows") {
        return std::nullopt;
    }
    return 6;
}

[[nodiscard]] const Attribute* findAttribute(const std::vector<Attribute>& attrs,
                                             std::string_view name) {
    for (const Attribute& a : attrs) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

} // namespace

/// State shared by the per-module lowerers of one program.
struct LowerShared {
    struct ImplInfo {
        const ImplDecl* decl{nullptr};
        Sema* owner{nullptr};
        const Item* item{nullptr};
        std::string type_q;
        std::string iface_q;
        std::string prefix; ///< `Type.Iface.`
        Span span;
    };
    struct SlotInfo {
        hir::VtableSlot slot;
        const FnDecl* decl{nullptr};
        Sema* owner{nullptr};
    };

    hir::Module module;
    std::vector<ImplInfo> impls;
    std::map<std::string, std::vector<SlotInfo>> vtables;
    std::set<std::string> vtables_in_progress;
    struct Role {
        Span span;
        std::string name;
    };
    std::map<std::string, Role> roles;
    std::map<std::string, Span> link_names;
};

class Lowerer {
  public:
    Lowerer(Sema& sema, LowerShared& shared) : s_(sema), sh_(shared), mod_(shared.module) {}

    void collectImpls();
    void checkRegistrations();
    void run();

  private:
    using EP = hir::ExprPtr;
    using ImplInfo = LowerShared::ImplInfo;
    using SlotInfo = LowerShared::SlotInfo;

    struct MethodRef {
        enum class Kind { Direct, Virtual, Itable } kind{Kind::Direct};
        std::string fn_name;
        const FnDecl* decl{nullptr};
        Sema* owner{nullptr};   ///< module that resolves the declaration's types
        std::uint32_t index{0}; ///< Itable: method index, Virtual: vtable slot
        bool from_impl{false};
    };

    struct FieldInfo {
        const FieldDecl* decl;
        Sema* owner;
    };

    // --- setup
    void buildModuleScope();
    [[nodiscard]] ScopeEntry
    entryOf(Sema::EntityRef ref, const std::string& local, const Span& span);

    // --- module items
    void lowerItems();
    void lowerFunction(const FnDecl& f,
                       const Modifiers& mods,
                       const std::string& qname,
                       const std::string& local_name,
                       const std::vector<Attribute>& attrs,
                       const std::string& owner,
                       const Type* self_type);
    void lowerGlobal(const GlobalDecl& g, const Modifiers& mods);
    void lowerLocalConst(const Stmt& s);
    void lowerEnum(const EnumDecl& e);
    void lowerAggregate(const Item& item);
    void checkImpl(const ImplInfo& info);
    [[nodiscard]] bool cCompatible(Sema& sema, const Type& t);
    void assignSymbol(const std::vector<Attribute>& attrs,
                      bool export_c,
                      bool declared,
                      const std::string& local_name,
                      const std::vector<Type>& params,
                      std::string& symbol,
                      const Span& at);

    // --- statements
    void lowerBlock(const Block& block, std::vector<hir::Stmt>& out, bool new_scope);
    void lowerStmt(const Stmt& s, std::vector<hir::Stmt>& out);
    void lowerAssign(const Expr& target,
                     const Expr& value,
                     const Span& span,
                     std::vector<hir::Stmt>& out);
    void lowerCompound(const Stmt& s, std::vector<hir::Stmt>& out);
    void lowerFor(const Stmt& s, std::vector<hir::Stmt>& out);
    void lowerForRange(const Stmt& s, std::vector<hir::Stmt>& out);
    void lowerSwitch(const Stmt& s, std::vector<hir::Stmt>& out);
    void lowerJump(const Stmt& s, std::vector<hir::Stmt>& out);
    /// Appends clones of the deferred statements of the frames `[from, defers_.size())`, innermost
    /// first.
    void emitDefers(std::size_t from, std::vector<hir::Stmt>& out) const;
    EP ternaryExpr(const Expr& e, const Type* expected);
    hir::LocalId hiddenLocal(const char* prefix, const Type& type, const Span& span);
    hir::LocalId
    addLocal(const std::string& name, const Type& type, bool is_param, const Span& span);
    void bindLocal(const std::string& name, hir::LocalId id, const Type& type, const Span& span);

    // --- expressions
    EP expr(const Expr& e, const Type* expected);
    EP nameExpr(const Expr& e, const Type* expected);
    EP fromEntry(const ScopeEntry& entry, const Expr& at, const Type* expected);
    EP fieldExpr(const Expr& e);
    EP unaryExpr(const Expr& e, const Type* expected);
    EP binaryExpr(const Expr& e, const Type* expected);
    EP castExpr(const Expr& e);
    EP callExpr(const Expr& e, const Type* expected);
    EP callEntry(const Expr& e, const ScopeEntry& entry, const std::string& name);
    EP methodCall(const Expr& e, const Expr& recv_ast);
    EP syscallExpr(const Expr& e);
    EP builtinCall(const Expr& e, const std::string& name);
    EP asmCall(const Expr& e);
    std::optional<std::uint32_t> memoryOrder(const Expr& arg);
    EP indexExpr(const Expr& e);
    EP arrayExpr(const Expr& e, const Type* expected);
    EP structLiteral(const Expr& e);
    EP newExpr(const Expr& e);
    EP constExpr(const Expr& e, const Type* expected);
    EP fromConst(const ConstValue& v, const Type* expected, const Span& span);
    EP stringLiteral(const std::string& text, const Type* expected, const Span& span);

    bool lowerArgs(const Expr& call,
                   const std::vector<Type>& params,
                   std::vector<EP>& out,
                   const std::string& callee);
    void lowerFields(const Expr& e, const Type& agg, std::vector<hir::FieldInit>& out);

    EP coerce(EP value, const Type& to, const Span& at);
    EP castTo(EP value, const Type& to, bool implicit);
    EP toInterface(EP value, const Type& iface, const Span& at);
    [[nodiscard]] bool castable(const Type& from, const Type& to) const;

    // --- queries
    [[nodiscard]] Type resolvedIn(Sema& sema, const Type& t);
    [[nodiscard]] Type resolved(const Type& t) { return resolvedIn(s_, t); }
    /// Declared type of a global; for an untyped `const`, the default type of its literal value.
    [[nodiscard]] Type globalType(Sema& sema, const GlobalDecl& g);
    [[nodiscard]] Type normalized(Sema& sema, const Type& t);
    [[nodiscard]] Type fnTypeOf(Sema& owner, const FnDecl& f);
    [[nodiscard]] std::optional<MethodRef> findMethod(const Type& agg, const std::string& name);
    [[nodiscard]] const ImplInfo* findImpl(const std::string& type_q,
                                           const std::string& iface_q) const;
    [[nodiscard]] bool hasAllocMethod(const Type& t);
    [[nodiscard]] std::optional<Sema::EntityRef> entityOfType(const Type& t);
    void collectFields(const std::string& qname, std::vector<FieldInfo>& out);
    [[nodiscard]] const std::vector<SlotInfo>& vtableOf(const std::string& qname);
    [[nodiscard]] bool isConstValue(const hir::Expr& e, const hir::Expr** offender) const;
    [[nodiscard]] std::optional<std::uint64_t> arrayLength(Sema& sema, const Type& t);
    [[nodiscard]] const ScopeEntry* lookupName(const std::string& name);
    [[nodiscard]] const ScopeEntry* chainEntry(const Expr& e);
    [[nodiscard]] const ScopeEntry*
    namespaceMember(const ScopeEntry& ns, const std::string& name, const Span& at, bool report);
    [[nodiscard]] bool isRegisteredName(const std::string& name) const;

    // --- helpers
    [[nodiscard]] EP mk(hir::ExprKind kind, Type type, const Span& span) const;
    [[nodiscard]] EP intConst(std::uint64_t v, Type type, const Span& span) const;
    void err(const char* code, const Span& at, std::string message, std::string label);
    [[nodiscard]] bool inUnsafe() const { return unsafe_ > 0; }
    [[nodiscard]] unsigned ptrBits() const { return s_.config_.target.pointer_width; }

    Sema& s_;
    LowerShared& sh_;
    hir::Module& mod_;
    ScopeStack scopes_;
    hir::Function* fn_{nullptr};
    Type ret_;
    int unsafe_{0};
    struct LoopCtx {
        std::string label;
        std::size_t defer_mark; ///< `defers_.size()` when the loop was entered
    };
    std::vector<LoopCtx> loops_; ///< enclosing loops of the statement being lowered, innermost last
    /// One frame per block being lowered: the lowered `defer` statements registered in it, in
    /// order.
    std::vector<std::vector<hir::Stmt>> defers_;
    unsigned hidden_counter_{0}; ///< unique suffix for compiler-made locals
    bool star_import_{false};
    std::set<std::string> registered_names_;
    std::map<std::string, ScopeEntry> extra_entries_; ///< registered globals / namespace members
};

// ---------------------------------------------------------------------------------------------
// Helpers

/// Deep copy of an AST expression (used to desugar `a op= b` and `switch`).
static ExprPtr cloneAst(const Expr& e) {
    auto c = std::make_unique<Expr>();
    c->kind = e.kind;
    c->span = e.span;
    c->int_value = e.int_value;
    c->float_value = e.float_value;
    c->bool_value = e.bool_value;
    c->text = e.text;
    c->unary_op = e.unary_op;
    c->binary_op = e.binary_op;
    c->type = e.type;
    c->macro_args = e.macro_args;
    if (e.lhs) {
        c->lhs = cloneAst(*e.lhs);
    }
    if (e.rhs) {
        c->rhs = cloneAst(*e.rhs);
    }
    for (const ExprPtr& a : e.args) {
        c->args.push_back(cloneAst(*a));
    }
    for (const FieldInit& f : e.fields) {
        c->fields.push_back({f.name, cloneAst(*f.value), f.span});
    }
    return c;
}

/// An expression whose evaluation may have side effects (so it must not be evaluated twice).
static bool astHasSideEffects(const Expr& e) {
    switch (e.kind) {
    case Expr::Kind::Call:
    case Expr::Kind::MacroCall:
    case Expr::Kind::New:
        return true;
    default:
        break;
    }
    if (e.lhs && astHasSideEffects(*e.lhs)) {
        return true;
    }
    if (e.rhs && astHasSideEffects(*e.rhs)) {
        return true;
    }
    for (const ExprPtr& a : e.args) {
        if (astHasSideEffects(*a)) {
            return true;
        }
    }
    for (const FieldInit& f : e.fields) {
        if (astHasSideEffects(*f.value)) {
            return true;
        }
    }
    return false;
}

static ExprPtr astName(const std::string& name, const Span& span) {
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::Name;
    e->text = name;
    e->span = span;
    return e;
}

static ExprPtr astUnary(UnaryOp op, ExprPtr operand, const Span& span) {
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::Unary;
    e->unary_op = op;
    e->lhs = std::move(operand);
    e->span = span;
    return e;
}

static ExprPtr astBinary(BinaryOp op, ExprPtr lhs, ExprPtr rhs, const Span& span) {
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::Binary;
    e->binary_op = op;
    e->lhs = std::move(lhs);
    e->rhs = std::move(rhs);
    e->span = span;
    return e;
}

static hir::ExprPtr cloneHirExpr(const hir::Expr& e) {
    auto c = std::make_unique<hir::Expr>();
    c->kind = e.kind;
    c->type = e.type;
    c->span = e.span;
    c->lvalue = e.lvalue;
    c->int_value = e.int_value;
    c->float_value = e.float_value;
    c->bool_value = e.bool_value;
    c->text = e.text;
    c->member = e.member;
    c->local = e.local;
    c->unary_op = e.unary_op;
    c->binary_op = e.binary_op;
    c->clobbers = e.clobbers;
    c->target = e.target;
    c->index = e.index;
    c->implicit = e.implicit;
    c->checked = e.checked;
    c->through_pointer = e.through_pointer;
    c->registered = e.registered;
    c->nomem = e.nomem;
    c->noreturn = e.noreturn;
    if (e.lhs) {
        c->lhs = cloneHirExpr(*e.lhs);
    }
    if (e.rhs) {
        c->rhs = cloneHirExpr(*e.rhs);
    }
    for (const hir::ExprPtr& a : e.args) {
        c->args.push_back(cloneHirExpr(*a));
    }
    for (const hir::FieldInit& f : e.fields) {
        c->fields.push_back({f.name, f.index, cloneHirExpr(*f.value)});
    }
    for (const hir::AsmOperand& op : e.asm_operands) {
        c->asm_operands.push_back({op.kind, op.reg, cloneHirExpr(*op.value)});
    }
    return c;
}

static hir::Stmt cloneHirStmt(const hir::Stmt& s) {
    hir::Stmt c;
    c.kind = s.kind;
    c.span = s.span;
    c.local = s.local;
    c.post_test = s.post_test;
    c.depth = s.depth;
    if (s.target) {
        c.target = cloneHirExpr(*s.target);
    }
    if (s.value) {
        c.value = cloneHirExpr(*s.value);
    }
    for (const hir::Stmt& b : s.body) {
        c.body.push_back(cloneHirStmt(b));
    }
    for (const hir::Stmt& b : s.else_body) {
        c.else_body.push_back(cloneHirStmt(b));
    }
    for (const hir::Stmt& b : s.step) {
        c.step.push_back(cloneHirStmt(b));
    }
    return c;
}

/// Does `body` contain a `break` that leaves the loop whose body it is (nesting level `level`)?
static bool breaksOut(const std::vector<hir::Stmt>& body, std::uint32_t level) {
    for (const hir::Stmt& s : body) {
        switch (s.kind) {
        case hir::StmtKind::Break:
            if (s.depth == level) {
                return true;
            }
            break;
        case hir::StmtKind::If:
            if (breaksOut(s.body, level) || breaksOut(s.else_body, level)) {
                return true;
            }
            break;
        case hir::StmtKind::Block:
            if (breaksOut(s.body, level)) {
                return true;
            }
            break;
        case hir::StmtKind::While:
            if (breaksOut(s.body, level + 1)) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

/// True when control cannot fall off the end of `body` (every path returns, loops forever or
/// calls a `noreturn` function).
static bool terminates(const std::vector<hir::Stmt>& body, const std::string& panic_fn) {
    for (const hir::Stmt& s : body) {
        switch (s.kind) {
        case hir::StmtKind::Return:
            return true;
        case hir::StmtKind::Block:
            if (terminates(s.body, panic_fn)) {
                return true;
            }
            break;
        case hir::StmtKind::If:
            if (!s.else_body.empty() && terminates(s.body, panic_fn) &&
                terminates(s.else_body, panic_fn)) {
                return true;
            }
            break;
        case hir::StmtKind::While:
            if (s.value && s.value->kind == hir::ExprKind::BoolConst && s.value->bool_value &&
                !breaksOut(s.body, 0)) {
                return true;
            }
            break;
        case hir::StmtKind::Expr:
            if (s.value->kind == hir::ExprKind::Asm && s.value->noreturn) {
                return true;
            }
            if (s.value->kind == hir::ExprKind::Call && !panic_fn.empty() &&
                s.value->text == panic_fn) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

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

const Lowerer::ImplInfo* Lowerer::findImpl(const std::string& type_q,
                                           const std::string& iface_q) const {
    for (const ImplInfo& i : sh_.impls) {
        if (i.type_q == type_q && i.iface_q == iface_q) {
            return &i;
        }
    }
    return nullptr;
}

bool Lowerer::isRegisteredName(const std::string& name) const {
    if (s_.comp_ != nullptr) {
        return s_.comp_->findRegistered(name).has_value();
    }
    return registered_names_.contains(name);
}

// ---------------------------------------------------------------------------------------------
// Vtables (plan 4.4)

const std::vector<LowerShared::SlotInfo>& Lowerer::vtableOf(const std::string& qname) {
    static const std::vector<SlotInfo> empty;
    if (const auto it = sh_.vtables.find(qname); it != sh_.vtables.end()) {
        return it->second;
    }
    const auto ref = s_.entityByQName(qname);
    const ClassDecl* cd = ref ? std::get_if<ClassDecl>(&ref->sym->item->decl) : nullptr;
    if (cd == nullptr || !sh_.vtables_in_progress.insert(qname).second) {
        return empty;
    }
    Sema& owner = *ref->owner;
    std::vector<SlotInfo> slots;
    if (cd->has_base) {
        Type base = Type::makeNamed(cd->base);
        base.span = cd->name_span;
        if (const auto r = owner.resolve(base); r && r->kind == TypeKind::Class) {
            slots = vtableOf(r->path.front());
        }
    }
    const auto signature = [&](Sema& sema, const FnDecl& f) {
        std::string s;
        for (const Param& p : f.params) {
            if (!p.is_self) {
                s += typeToString(resolvedIn(sema, p.type)) + ",";
            }
        }
        return s + "->" + typeToString(resolvedIn(sema, f.return_type));
    };
    for (const FnDecl& m : cd->methods) {
        const std::string fname = qname + "." + m.name;
        std::size_t idx = slots.size();
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].slot.method == m.name) {
                idx = i;
            }
        }
        const bool found = idx != slots.size();
        if (m.is_override) {
            if (!found) {
                owner.error("E0712",
                            m.name_span,
                            std::format("`{}` overrides nothing", m.name),
                            "no virtual method with this name in a base class");
                continue;
            }
            if (signature(owner, m) != signature(*slots[idx].owner, *slots[idx].decl)) {
                owner.error("E0712",
                            m.name_span,
                            std::format("override `{}` has a different signature", m.name),
                            "parameters and return type must match the base method");
            }
            slots[idx] = SlotInfo{{m.name, fname}, &m, &owner};
        } else if (m.is_virtual) {
            if (found) {
                owner.error("E0712",
                            m.name_span,
                            std::format("`{}` is already virtual in a base class", m.name),
                            "write `override fn` to replace it");
                continue;
            }
            slots.push_back(SlotInfo{{m.name, fname}, &m, &owner});
        } else if (found) {
            owner.error("E0712",
                        m.name_span,
                        std::format("`{}` hides a virtual method of a base class", m.name),
                        "write `override fn` to replace it");
        }
    }
    sh_.vtables_in_progress.erase(qname);
    return sh_.vtables[qname] = std::move(slots);
}

// ---------------------------------------------------------------------------------------------
// Module scope, registrations, impls

ScopeEntry Lowerer::entryOf(Sema::EntityRef ref, const std::string& local, const Span& span) {
    ScopeEntry e;
    e.name = local;
    e.span = span;
    e.item = ref.sym->item;
    e.owner = ref.owner;
    e.qname = ref.qname();
    switch (ref.sym->kind) {
    case Sema::Symbol::Kind::Fn:
        e.kind = ScopeEntry::Kind::Function;
        break;
    case Sema::Symbol::Kind::Global: {
        const auto& decl = std::get<GlobalDecl>(ref.sym->item->decl);
        e.kind = ScopeEntry::Kind::Global;
        e.type = globalType(*ref.owner, decl);
        e.readonly = decl.is_const;
        break;
    }
    case Sema::Symbol::Kind::Define:
        e.kind = ScopeEntry::Kind::Constant;
        break;
    default:
        e.kind = ScopeEntry::Kind::Type;
        break;
    }
    return e;
}

void Lowerer::buildModuleScope() {
    scopes_.push(ScopeKind::Module);
    {
        ScopeEntry e;
        e.kind = ScopeEntry::Kind::Type;
        e.name = "MemoryOrder";
        e.item = &s_.memory_order_item_;
        e.owner = &s_;
        e.qname = "MemoryOrder";
        (void)scopes_.declare(std::move(e));
    }
    for (const Item& item : s_.program_.items) {
        const DeclBase& base = declBase(item);
        if (item.modifiers.registered) {
            registered_names_.insert(base.name);
        }
        if (const auto* imp = std::get_if<ImportDecl>(&item.decl)) {
            if (s_.comp_ != nullptr) {
                continue; // bindings come from the compilation below
            }
            // Standalone module: imports stay external until module loading is available.
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
            case ImportDecl::Form::Module:
            case ImportDecl::Form::StarAs: {
                ScopeEntry ns;
                ns.kind = ScopeEntry::Kind::Namespace;
                ns.name = imp->alias.empty() ? imp->module : imp->alias;
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
        ScopeEntry e;
        e.name = base.name;
        e.span = base.name_span;
        e.item = &item;
        e.owner = &s_;
        e.qname = s_.qualify(base.name);
        if (std::holds_alternative<FnDecl>(item.decl)) {
            e.kind = ScopeEntry::Kind::Function;
        } else if (const auto* g = std::get_if<GlobalDecl>(&item.decl)) {
            e.kind = ScopeEntry::Kind::Global;
            e.type = globalType(s_, *g);
            e.readonly = g->is_const;
        } else if (std::holds_alternative<DefineConstDecl>(item.decl)) {
            e.kind = ScopeEntry::Kind::Constant;
        } else if (std::holds_alternative<StructDecl>(item.decl) ||
                   std::holds_alternative<ClassDecl>(item.decl) ||
                   std::holds_alternative<InterfaceDecl>(item.decl) ||
                   std::holds_alternative<EnumDecl>(item.decl) ||
                   std::holds_alternative<TypeAliasDecl>(item.decl)) {
            e.kind = ScopeEntry::Kind::Type;
        } else {
            continue;
        }
        // Duplicate definitions were already reported by `Sema::collect` (E0401).
        (void)scopes_.declare(std::move(e));
    }
    // Imports bound by the compilation.
    for (const auto& [name, b] : s_.imports_) {
        if (scopes_.lookupModule(name) != nullptr) {
            continue;
        }
        if (b.kind == Sema::ImportBinding::Kind::Namespace) {
            ScopeEntry ns;
            ns.kind = ScopeEntry::Kind::Namespace;
            ns.name = name;
            ns.module = b.module_key;
            ns.span = b.span;
            (void)scopes_.declare(std::move(ns));
            continue;
        }
        Sema* owner = s_.comp_->semaOf(b.module_key);
        const Sema::Symbol* sym = owner != nullptr ? owner->findLocal(b.name) : nullptr;
        if (sym == nullptr) {
            continue;
        }
        ScopeEntry e = entryOf(Sema::EntityRef{owner, sym, b.name}, name, b.span);
        (void)scopes_.declare(std::move(e));
    }
}

const ScopeEntry* Lowerer::lookupName(const std::string& name) {
    if (const ScopeEntry* e = scopes_.lookup(name)) {
        return e;
    }
    if (s_.comp_ != nullptr) {
        if (const auto it = extra_entries_.find("reg:" + name); it != extra_entries_.end()) {
            return &it->second;
        }
        if (const auto ref = s_.comp_->findRegistered(name)) {
            return &(extra_entries_["reg:" + name] =
                         entryOf(*ref, name, declBase(*ref->sym->item).name_span));
        }
    }
    return nullptr;
}

const ScopeEntry* Lowerer::namespaceMember(const ScopeEntry& ns,
                                           const std::string& name,
                                           const Span& at,
                                           bool report) {
    if (s_.comp_ == nullptr) {
        // Standalone namespace: members are external names.
        const std::string key = "ext:" + ns.module + "." + name;
        ScopeEntry& e = extra_entries_[key];
        e.kind = ScopeEntry::Kind::External;
        e.name = name;
        e.module = ns.module;
        e.member = name;
        e.span = at;
        return &e;
    }
    ModuleInfo* m = s_.comp_->moduleOf(ns.module);
    if (m == nullptr) {
        return nullptr;
    }
    const auto it = m->exports.find(name);
    if (it == m->exports.end()) {
        if (report) {
            err("E0706",
                at,
                std::format("module `{}` has no exported name `{}`", ns.name, name),
                "only `export`ed names are visible");
        }
        return nullptr;
    }
    const std::string key = "ns:" + ns.module + "." + name;
    if (const auto cached = extra_entries_.find(key); cached != extra_entries_.end()) {
        return &cached->second;
    }
    ScopeEntry e;
    if (it->second.is_namespace) {
        e.kind = ScopeEntry::Kind::Namespace;
        e.name = name;
        e.module = it->second.module->key;
        e.span = at;
    } else if (const auto ref = s_.comp_->exportedEntity(ns.module, name)) {
        e = entryOf(*ref, name, at);
    } else {
        return nullptr;
    }
    return &(extra_entries_[key] = std::move(e));
}

/// The entity a (possibly module-qualified) expression names without being a value expression:
/// `x`, `ns.x`, `ns.sub.x`. Null when the chain is not rooted in a name binding.
const ScopeEntry* Lowerer::chainEntry(const Expr& e) {
    if (e.kind == Expr::Kind::Name) {
        return lookupName(e.text);
    }
    if (e.kind == Expr::Kind::Field) {
        const ScopeEntry* base = chainEntry(*e.lhs);
        if (base != nullptr && base->kind == ScopeEntry::Kind::Namespace) {
            return namespaceMember(*base, e.text, e.span, true);
        }
    }
    return nullptr;
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
                err("E0706",
                    impl->span,
                    std::format("`{}` cannot implement an interface", typeToString(target)),
                    "only structs and classes have `impl`");
            }
            continue;
        }
        Type iface = Type::makeNamed(impl->interface_path);
        iface.span = impl->span;
        const auto ir = s_.resolve(iface);
        if (!ir) {
            continue;
        }
        if (ir->kind != TypeKind::Interface) {
            err("E0706",
                impl->span,
                std::format("`{}` is not an interface", joinPath(impl->interface_path)),
                "not declared as an interface");
            continue;
        }
        ImplInfo info;
        info.decl = impl;
        info.owner = &s_;
        info.item = &item;
        info.type_q = target.path.front();
        info.iface_q = ir->path.front();
        info.prefix = info.type_q + "." + info.iface_q + ".";
        info.span = impl->span;
        sh_.impls.push_back(std::move(info));
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
            ok = fn != nullptr && fn->params.size() == 1 && !fn->params[0].is_self &&
                 fn->return_type.isVoid();
            if (ok) {
                const Type p = resolved(fn->params[0].type);
                ok = p.kind == TypeKind::Pointer && p.elem && p.elem->kind == TypeKind::Int &&
                     p.elem->bits == 8 && !p.elem->is_signed;
            }
            why = "`register(panic)` needs `fn(msg: *ui8)` with no return type";
        } else if (role == "allocator") {
            if (global != nullptr) {
                const Type t = resolved(global->type);
                ok = isUnknown(t) || t.kind == TypeKind::Interface || hasAllocMethod(t);
            }
            why = "`register(allocator)` needs a global whose type is an interface or has an "
                  "`impl` with `alloc`";
        } else if (role == "start") {
            ok = fn != nullptr && fn->has_body && fn->params.empty() && fn->return_type.isVoid();
            why = "`register(start)` needs `fn()` with no parameters and no return type";
        } else {
            why = std::format("unknown role `{}`; known roles: panic, allocator, start", role);
        }
        if (!ok) {
            err("E0503",
                base.name_span,
                std::format("`{}` does not fit role `{}`", base.name, role),
                why);
            continue;
        }
        const auto [it, inserted] =
            sh_.roles.emplace(role, LowerShared::Role{base.name_span, base.name});
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
        const std::string qname = s_.qualify(base.name);
        if (role == "panic") {
            mod_.panic_function = qname;
        } else if (role == "allocator") {
            mod_.allocator_global = qname;
        } else {
            mod_.start_function = qname;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Items

bool Lowerer::cCompatible(Sema& sema, const Type& t) {
    switch (t.kind) {
    case TypeKind::Void:
    case TypeKind::Bool:
    case TypeKind::Int:
    case TypeKind::Float:
    case TypeKind::FnPtr:
    case TypeKind::Pointer:
        return true;
    case TypeKind::Enum:
        return t.elem && t.elem->kind == TypeKind::Int;
    case TypeKind::Struct: {
        const auto ref = sema.entityByQName(t.path.front());
        if (!ref) {
            return false;
        }
        const Attribute* repr = findAttribute(declBase(*ref->sym->item).attributes, "repr");
        return repr != nullptr &&
               std::ranges::any_of(repr->args, [](const AttributeArg& a) { return a.text == "c"; });
    }
    default:
        return false;
    }
}

void Lowerer::assignSymbol(const std::vector<Attribute>& attrs,
                           bool export_c,
                           bool declared,
                           const std::string& local_name,
                           const std::vector<Type>& params,
                           std::string& symbol,
                           const Span& at) {
    // Priority: export(c) > @mangle on the item > @file(mangle) > --mangle (plan 4.9).
    std::optional<std::string> plain; // an explicit/unmangled symbol
    if (export_c) {
        plain = local_name;
    } else if (const Attribute* m = findAttribute(attrs, "mangle");
               m != nullptr && !m->args.empty()) {
        const AttributeArg& a = m->args[0];
        plain = a.kind == AttributeArg::Kind::String ? a.text : local_name;
        if (a.kind == AttributeArg::Kind::Ident && a.text != "none") {
            err("E0705", a.span, "`@mangle` takes `none` or a string", "invalid argument");
        }
    } else {
        std::optional<bool> file_none;
        if (const Attribute* f = findAttribute(s_.program_.file_attributes, "file")) {
            for (const AttributeArg& a : f->args) {
                if (a.key == "mangle") {
                    file_none = a.text == "none";
                }
            }
        }
        const bool none = file_none ? *file_none : s_.config_.mangle == "none";
        if (none || declared) {
            plain = local_name;
        }
    }
    if (plain) {
        symbol = *plain;
        const auto [it, inserted] = sh_.link_names.emplace(symbol, at);
        if (!inserted && !declared) {
            Diagnostic d;
            d.code = "E0401";
            d.severity = Severity::Error;
            d.message = std::format("symbol `{}` is defined twice", symbol);
            d.labels.push_back({it->second, "first definition"});
            d.labels.push_back({at, "second definition"});
            d.fix = "enable name mangling or rename one with `@mangle(\"...\")`";
            s_.diag_.report(std::move(d));
        }
        return;
    }
    // `_xl$<module>$<name>$<param types>`; the module is the mangling name of the defining module.
    std::string sig;
    for (const Type& p : params) {
        sig += "$" + typeToString(p);
    }
    std::string module = s_.key_;
    if (module.empty() && s_.comp_ != nullptr && s_.comp_->root() != nullptr) {
        module = s_.comp_->root()->mangle_name;
    }
    if (module.empty()) {
        module = "main";
    }
    std::replace(module.begin(), module.end(), '/', '.');
    symbol = "_xl$" + module + "$" + local_name + sig;
    sh_.link_names.emplace(symbol, at);
}

void Lowerer::lowerEnum(const EnumDecl& decl) {
    hir::Enum out;
    out.name = s_.qualify(decl.name);
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
    out.name = s_.qualify(g.name);
    out.module = s_.key_;
    out.constant = g.is_const;
    out.type = globalType(s_, g);
    if (!g.has_type && g.init && !literalConstType(s_, *g.init)) {
        err("E0715",
            g.init->span,
            "untyped `const` needs a literal value",
            "write its type: `const NAME: T = ...`");
    }
    out.exported = mods.exported || mods.export_c || mods.registered;
    out.export_c = mods.export_c;
    out.declared = mods.declared;
    out.registered = mods.registered;
    out.register_role = mods.register_role;
    out.span = g.span;
    if (mods.export_c && !cCompatible(s_, out.type)) {
        err("E0302",
            g.name_span,
            std::format("`{}` has a type that C cannot express", typeToString(out.type)),
            "`export(c)` needs C-compatible types (integers, floats, bool, pointers, `@repr(c)` "
            "structs)");
    }
    assignSymbol(g.attributes, mods.export_c, mods.declared, g.name, {}, out.symbol, g.name_span);
    if (g.init) {
        EP init = g.has_type ? coerce(expr(*g.init, &out.type), out.type, g.init->span)
                             : expr(*g.init, nullptr);
        if (init && !g.has_type) {
            out.type = init->type;
        }
        if (init) {
            const hir::Expr* offender = nullptr;
            if (!isConstValue(*init, &offender)) {
                const hir::Expr& at = offender != nullptr ? *offender : *init;
                if (g.is_const) {
                    err("E0715",
                        at.span,
                        "`const` initializer is not a compile-time constant",
                        "a `const` holds literals, arithmetic on them, arrays and struct literals; "
                        "it cannot call functions or read other names");
                } else {
                    err("E0707",
                        at.span,
                        "global initializer is not a compile-time constant",
                        "globals are statically initialized: no calls, no reads of other globals");
                }
            }
            out.init = std::move(init);
        }
    }
    mod_.globals.push_back(std::move(out));
}

void Lowerer::lowerLocalConst(const Stmt& s) {
    EP init;
    Type t;
    if (s.has_type) {
        t = resolved(s.type);
        init = coerce(expr(*s.expr, &t), t, s.expr->span);
    } else {
        init = expr(*s.expr, nullptr);
        t = init ? init->type : unknownType();
    }
    if (init) {
        const hir::Expr* offender = nullptr;
        if (!isConstValue(*init, &offender)) {
            const hir::Expr& at = offender != nullptr ? *offender : *init;
            err("E0715",
                at.span,
                "`const` initializer is not a compile-time constant",
                "a `const` holds literals, arithmetic on them, arrays and struct literals; "
                "it cannot call functions or read other names");
            init.reset();
        }
    }
    // A `const` is a private read-only global with a unique name; only the block sees the name.
    hir::Global g;
    g.name = std::format("{}${}${}", fn_->name, s.name, hidden_counter_++);
    g.module = s_.key_;
    g.symbol = g.name;
    g.type = t;
    g.constant = true;
    g.init = std::move(init);
    g.span = s.span;
    ScopeEntry e;
    e.kind = ScopeEntry::Kind::Global;
    e.name = s.name;
    e.type = t;
    e.span = s.span;
    e.owner = &s_;
    e.qname = g.name;
    e.readonly = true;
    mod_.globals.push_back(std::move(g));
    if (const ScopeEntry* prev = scopes_.declare(std::move(e))) {
        Diagnostic d;
        d.code = "E0401";
        d.severity = Severity::Error;
        d.message = std::format("`{}` is defined twice in this scope", s.name);
        d.labels.push_back({prev->span, "first definition"});
        d.labels.push_back({s.span, "second definition"});
        d.fix = "rename one of them";
        s_.diag_.report(std::move(d));
    }
}

void Lowerer::lowerFunction(const FnDecl& f,
                            const Modifiers& mods,
                            const std::string& qname,
                            const std::string& local_name,
                            const std::vector<Attribute>& attrs,
                            const std::string& owner,
                            const Type* self_type) {
    hir::Function out;
    out.name = qname;
    out.module = s_.key_;
    out.owner = owner;
    out.exported = mods.exported || mods.export_c || mods.registered;
    out.export_c = mods.export_c;
    out.declared = mods.declared;
    if (const Attribute* lib = findAttribute(attrs, "library"); lib != nullptr && mods.declared) {
        if (lib->args.size() == 1 && lib->args[0].kind == AttributeArg::Kind::String) {
            out.import_library = lib->args[0].text;
        } else {
            err("E0705",
                f.name_span,
                "`@library` takes one string (the OS library name)",
                "invalid argument");
        }
    }
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
    loops_.clear();
    defers_.clear();
    scopes_.push(ScopeKind::Function);
    std::vector<Type> param_types;
    for (const Param& p : f.params) {
        Type t;
        if (p.is_self) {
            if (self_type == nullptr) {
                err("E0709",
                    p.span,
                    "`self` outside of a method",
                    "only class and `impl` methods have `self`");
                t = unknownType();
            } else {
                t = *self_type;
            }
        } else {
            t = resolved(p.type);
        }
        param_types.push_back(t);
        const hir::LocalId id = addLocal(p.is_self ? "self" : p.name, t, true, p.span);
        bindLocal(p.is_self ? "self" : p.name, id, t, p.span);
    }
    out.param_count = static_cast<std::uint32_t>(out.locals.size());
    if (mods.export_c) {
        bool ok = cCompatible(s_, out.ret);
        for (const Type& t : param_types) {
            ok = ok && cCompatible(s_, t);
        }
        if (!ok) {
            err("E0302",
                f.name_span,
                "`export(c)` signature uses a type that C cannot express",
                "integers, floats, bool, pointers, function pointers, `@repr(c)` structs and "
                "integer enums only");
        }
    }
    assignSymbol(
        attrs, mods.export_c, mods.declared, local_name, param_types, out.symbol, f.name_span);
    if (f.has_body) {
        lowerBlock(f.body, out.body, false);
        if (!out.ret.isVoid() && !isUnknown(out.ret) &&
            !terminates(out.body, mod_.panic_function)) {
            s_.diag_.warning(
                "W0701",
                f.name_span,
                std::format("`{}` can reach the end without returning a value", f.name),
                "not every path returns");
        }
    }
    scopes_.pop();
    fn_ = nullptr;
    mod_.functions.push_back(std::move(out));
}

void Lowerer::lowerAggregate(const Item& item) {
    if (const auto* st = std::get_if<StructDecl>(&item.decl)) {
        hir::Aggregate a;
        a.name = s_.qualify(st->name);
        a.opaque = st->opaque;
        a.span = st->span;
        const Attribute* repr = findAttribute(st->attributes, "repr");
        a.repr_c = repr != nullptr && std::ranges::any_of(repr->args, [](const AttributeArg& x) {
                       return x.text == "c";
                   });
        for (const FieldDecl& f : st->fields) {
            a.fields.push_back({f.name, resolved(f.type)});
        }
        mod_.aggregates.push_back(std::move(a));
    } else if (const auto* cl = std::get_if<ClassDecl>(&item.decl)) {
        hir::Aggregate a;
        a.name = s_.qualify(cl->name);
        a.is_class = true;
        a.opaque = cl->opaque;
        a.span = cl->span;
        const Attribute* repr = findAttribute(cl->attributes, "repr");
        a.repr_c = repr != nullptr && std::ranges::any_of(repr->args, [](const AttributeArg& x) {
                       return x.text == "c";
                   });
        if (cl->has_base) {
            Type base = Type::makeNamed(cl->base);
            base.span = cl->name_span;
            if (const auto r = s_.resolve(base); r && r->kind == TypeKind::Class) {
                a.base = r->path.front();
            }
        }
        for (const FieldDecl& f : cl->fields) {
            a.fields.push_back({f.name, resolved(f.type)});
        }
        for (const FnDecl& m : cl->methods) {
            a.methods.push_back(a.name + "." + m.name);
        }
        for (const SlotInfo& slot : vtableOf(a.name)) {
            a.vtable.push_back(slot.slot);
        }
        mod_.aggregates.push_back(std::move(a));
    }
}

void Lowerer::checkImpl(const ImplInfo& info) {
    const auto iref = s_.entityByQName(info.iface_q);
    if (!iref) {
        return;
    }
    const auto& idecl = std::get<InterfaceDecl>(iref->sym->item->decl);
    std::set<std::string> iface_names;
    for (const FnDecl& im : idecl.methods) {
        iface_names.insert(im.name);
        const FnDecl* found = nullptr;
        for (const FnDecl& m : info.decl->methods) {
            if (m.name == im.name) {
                found = &m;
            }
        }
        if (found == nullptr) {
            err("E0713",
                info.decl->span,
                std::format("`impl {} for {}` is missing `{}`", info.iface_q, info.type_q, im.name),
                "every interface method must be implemented");
            continue;
        }
        const Type want = fnTypeOf(*iref->owner, im);
        const Type have = fnTypeOf(s_, *found);
        if (!sameType(want, have)) {
            err("E0713",
                found->name_span,
                std::format("`{}` has signature `{}`, the interface declares `{}`",
                            im.name,
                            typeToString(have),
                            typeToString(want)),
                "signature differs from the interface");
        }
    }
    for (const FnDecl& m : info.decl->methods) {
        if (!iface_names.contains(m.name)) {
            err("E0713",
                m.name_span,
                std::format("`{}` is not a method of `{}`", m.name, info.iface_q),
                "not part of the interface");
        }
    }
}

void Lowerer::lowerItems() {
    for (const Item& item : s_.program_.items) {
        if (const auto* e = std::get_if<EnumDecl>(&item.decl)) {
            lowerEnum(*e);
        } else if (std::holds_alternative<StructDecl>(item.decl) ||
                   std::holds_alternative<ClassDecl>(item.decl)) {
            lowerAggregate(item);
        } else if (const auto* in = std::get_if<InterfaceDecl>(&item.decl)) {
            hir::Interface i;
            i.name = s_.qualify(in->name);
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
            lowerFunction(
                *f, item.modifiers, s_.qualify(f->name), f->name, f->attributes, {}, nullptr);
        }
    }
    // Methods after all signatures/types exist.
    for (const Item& item : s_.program_.items) {
        if (const auto* cl = std::get_if<ClassDecl>(&item.decl)) {
            const std::string cq = s_.qualify(cl->name);
            const Type self = Type::makePointer(resolved(Type::makeNamed({cl->name})));
            for (const FnDecl& m : cl->methods) {
                lowerFunction(m,
                              item.modifiers,
                              cq + "." + m.name,
                              cl->name + "." + m.name,
                              m.attributes,
                              cq,
                              &self);
            }
        }
    }
    for (const ImplInfo& info : sh_.impls) {
        if (info.owner != &s_) {
            continue;
        }
        checkImpl(info);
        Type target_type;
        if (const auto tref = s_.entityByQName(info.type_q)) {
            target_type.kind =
                tref->sym->kind == Sema::Symbol::Kind::Class ? TypeKind::Class : TypeKind::Struct;
            target_type.path = {info.type_q};
        }
        const Type self = Type::makePointer(target_type);
        const auto iref = s_.entityByQName(info.iface_q);
        hir::Impl out;
        out.interface = info.iface_q;
        out.type = info.type_q;
        out.span = info.span;
        for (const FnDecl& m : info.decl->methods) {
            lowerFunction(m,
                          Modifiers{},
                          info.prefix + m.name,
                          info.prefix + m.name,
                          m.attributes,
                          info.type_q,
                          &self);
        }
        if (iref) {
            for (const FnDecl& im : std::get<InterfaceDecl>(iref->sym->item->decl).methods) {
                out.methods.push_back(info.prefix + im.name);
            }
        }
        mod_.impls.push_back(std::move(out));
    }
}

// ---------------------------------------------------------------------------------------------
// Statements

hir::LocalId
Lowerer::addLocal(const std::string& name, const Type& type, bool is_param, const Span& span) {
    fn_->locals.push_back(hir::Local{name, type, is_param, span});
    return static_cast<hir::LocalId>(fn_->locals.size() - 1);
}

void Lowerer::bindLocal(const std::string& name,
                        hir::LocalId id,
                        const Type& type,
                        const Span& span) {
    if (isRegisteredName(name)) {
        s_.diag_.warning("W0402",
                         span,
                         std::format("`{}` shadows a registered global", name),
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
    defers_.emplace_back();
    for (const Stmt& s : block.statements) {
        lowerStmt(s, out);
    }
    // Deferred statements run when the block is left by falling off its end; `return`, `break`
    // and `continue` emit them at the jump.
    const bool jumps = !out.empty() && (out.back().kind == hir::StmtKind::Return ||
                                        out.back().kind == hir::StmtKind::Break ||
                                        out.back().kind == hir::StmtKind::Continue);
    if (!jumps) {
        emitDefers(defers_.size() - 1, out);
    }
    defers_.pop_back();
    if (new_scope) {
        scopes_.pop();
    }
}

void Lowerer::emitDefers(std::size_t from, std::vector<hir::Stmt>& out) const {
    for (std::size_t f = defers_.size(); f-- > from;) {
        const auto& frame = defers_[f];
        for (std::size_t i = frame.size(); i-- > 0;) {
            out.push_back(cloneHirStmt(frame[i]));
        }
    }
}

hir::LocalId Lowerer::hiddenLocal(const char* prefix, const Type& type, const Span& span) {
    return addLocal(std::format("${}{}", prefix, hidden_counter_++), type, false, span);
}

void Lowerer::lowerAssign(const Expr& target_ast,
                          const Expr& value_ast,
                          const Span& span,
                          std::vector<hir::Stmt>& out) {
    EP target = expr(target_ast, nullptr);
    if (target && isStrPlace(*target)) {
        err("E0716",
            target_ast.span,
            "cannot assign through a `str`; it is read-only",
            "copy the bytes into a `[ui8; N]` or a `String`");
        target.reset();
    }
    if (target && target->readonly) {
        err("E0716",
            target_ast.span,
            std::format("cannot assign to `const` `{}`", placeRootName(*target)),
            "a `const` is read-only; copy it into a `local` to change it");
        target.reset();
    }
    if (target && !target->lvalue && !isUnknown(target->type)) {
        err("E0709",
            target_ast.span,
            "cannot assign to this expression",
            "not a place (variable, `*p`, field or element)");
        target.reset();
    }
    const Type want = target ? target->type : unknownType();
    EP value = expr(value_ast, target ? &want : nullptr);
    if (target && value) {
        value = coerce(std::move(value), want, value_ast.span);
    }
    if (!target || !value) {
        return;
    }
    hir::Stmt h;
    h.span = span;
    h.kind = hir::StmtKind::Assign;
    h.target = std::move(target);
    h.value = std::move(value);
    out.push_back(std::move(h));
}

/// `t op= v`  =>  `t = t op v`; a target with side effects is evaluated once through a pointer.
void Lowerer::lowerCompound(const Stmt& s, std::vector<hir::Stmt>& out) {
    if (!astHasSideEffects(*s.target)) {
        const ExprPtr bin =
            astBinary(s.compound_op, cloneAst(*s.target), cloneAst(*s.expr), s.span);
        lowerAssign(*s.target, *bin, s.span, out);
        return;
    }
    EP target = expr(*s.target, nullptr);
    if (!target) {
        return;
    }
    if (target->readonly) {
        err("E0716",
            s.target->span,
            std::format("cannot assign to `const` `{}`", placeRootName(*target)),
            "a `const` is read-only; copy it into a `local` to change it");
        return;
    }
    if (!target->lvalue && !isUnknown(target->type)) {
        err("E0709",
            s.target->span,
            "cannot assign to this expression",
            "not a place (variable, `*p`, field or element)");
        return;
    }
    if (isUnknown(target->type)) {
        return;
    }
    const Type ptr = Type::makePointer(target->type);
    const hir::LocalId id = hiddenLocal("lhs", ptr, s.span);
    const std::string name = fn_->locals[id].name;
    hir::Stmt decl;
    decl.kind = hir::StmtKind::Local;
    decl.span = s.span;
    decl.local = id;
    EP addr = mk(hir::ExprKind::AddrOf, ptr, s.span);
    addr->lhs = std::move(target);
    decl.value = std::move(addr);
    out.push_back(std::move(decl));
    bindLocal(name, id, ptr, s.span);
    const ExprPtr place = astUnary(UnaryOp::Deref, astName(name, s.target->span), s.target->span);
    const ExprPtr bin =
        astBinary(s.compound_op,
                  astUnary(UnaryOp::Deref, astName(name, s.target->span), s.target->span),
                  cloneAst(*s.expr),
                  s.span);
    lowerAssign(*place, *bin, s.span, out);
}

void Lowerer::lowerJump(const Stmt& s, std::vector<hir::Stmt>& out) {
    const bool is_break = s.kind == Stmt::Kind::Break;
    const char* word = is_break ? "break" : "continue";
    if (loops_.empty()) {
        err("E0709",
            s.span,
            std::format("`{}` outside of a loop", word),
            "there is no enclosing loop");
        return;
    }
    std::size_t target = loops_.size() - 1;
    if (!s.name.empty()) {
        bool found = false;
        for (std::size_t i = loops_.size(); i-- > 0;) {
            if (loops_[i].label == s.name) {
                target = i;
                found = true;
                break;
            }
        }
        if (!found) {
            err("E0709",
                s.span,
                std::format("no enclosing loop is labeled `{}`", s.name),
                "write `label: while ...` on the loop first");
            return;
        }
    }
    emitDefers(loops_[target].defer_mark, out);
    hir::Stmt h;
    h.span = s.span;
    h.kind = is_break ? hir::StmtKind::Break : hir::StmtKind::Continue;
    h.depth = static_cast<std::uint32_t>(loops_.size() - 1 - target);
    out.push_back(std::move(h));
}

void Lowerer::lowerFor(const Stmt& s, std::vector<hir::Stmt>& out) {
    const Type boolean = Type::makeBool();
    hir::Stmt block;
    block.kind = hir::StmtKind::Block;
    block.span = s.span;
    scopes_.push(ScopeKind::Block);
    defers_.emplace_back(); // the header scope (no defers can land here, but keeps frames aligned)
    if (s.init) {
        lowerStmt(*s.init, block.body);
    }
    hir::Stmt loop;
    loop.kind = hir::StmtKind::While;
    loop.span = s.span;
    if (s.expr) {
        loop.value = coerce(expr(*s.expr, &boolean), boolean, s.expr->span);
    } else {
        loop.value = mk(hir::ExprKind::BoolConst, boolean, s.span);
        loop.value->bool_value = true;
    }
    loops_.push_back({s.label, defers_.size()});
    lowerBlock(*s.body, loop.body, true);
    loops_.pop_back();
    if (s.step) {
        lowerStmt(*s.step, loop.step);
    }
    defers_.pop_back();
    scopes_.pop();
    if (!loop.value) {
        return;
    }
    block.body.push_back(std::move(loop));
    out.push_back(std::move(block));
}

void Lowerer::lowerForRange(const Stmt& s, std::vector<hir::Stmt>& out) {
    const Type boolean = Type::makeBool();
    EP lo = expr(*s.expr, nullptr);
    if (!lo) {
        (void)expr(*s.expr2, nullptr);
        return;
    }
    const Type t = lo->type;
    EP hi = expr(*s.expr2, &t);
    if (!isUnknown(t) && t.kind != TypeKind::Int) {
        err("E0708",
            s.expr->span,
            std::format("a range needs integer bounds, found `{}`", typeToString(t)),
            "write `for i in 0..n`");
        return;
    }
    hi = hi ? coerce(std::move(hi), t, s.expr2->span) : nullptr;
    if (!hi || isUnknown(t)) {
        return;
    }
    hir::Stmt block;
    block.kind = hir::StmtKind::Block;
    block.span = s.span;
    scopes_.push(ScopeKind::Block);
    defers_.emplace_back();

    const hir::LocalId var = addLocal(s.name, t, false, s.span);
    const hir::LocalId end = hiddenLocal("end", t, s.span);
    {
        hir::Stmt d;
        d.kind = hir::StmtKind::Local;
        d.span = s.span;
        d.local = var;
        d.value = std::move(lo);
        block.body.push_back(std::move(d));
        hir::Stmt e;
        e.kind = hir::StmtKind::Local;
        e.span = s.span;
        e.local = end;
        e.value = std::move(hi);
        block.body.push_back(std::move(e));
    }
    bindLocal(s.name, var, t, s.span);

    const auto ref = [&](hir::LocalId id) {
        EP r = mk(hir::ExprKind::LocalRef, t, s.span);
        r->local = id;
        r->lvalue = true;
        return r;
    };
    hir::Stmt loop;
    loop.kind = hir::StmtKind::While;
    loop.span = s.span;
    EP cond = mk(hir::ExprKind::Binary, boolean, s.span);
    cond->binary_op = BinaryOp::Lt;
    cond->lhs = ref(var);
    cond->rhs = ref(end);
    loop.value = std::move(cond);
    loops_.push_back({s.label, defers_.size()});
    lowerBlock(*s.body, loop.body, true);
    loops_.pop_back();
    {
        hir::Stmt step;
        step.kind = hir::StmtKind::Assign;
        step.span = s.span;
        step.target = ref(var);
        EP add = mk(hir::ExprKind::Binary, t, s.span);
        add->binary_op = BinaryOp::Add;
        add->lhs = ref(var);
        add->rhs = intConst(1, t, s.span);
        step.value = std::move(add);
        loop.step.push_back(std::move(step));
    }
    defers_.pop_back();
    scopes_.pop();
    block.body.push_back(std::move(loop));
    out.push_back(std::move(block));
}

void Lowerer::lowerSwitch(const Stmt& s, std::vector<hir::Stmt>& out) {
    const Type boolean = Type::makeBool();
    EP scrutinee = expr(*s.expr, nullptr);
    const bool usable = scrutinee && (scrutinee->type.kind == TypeKind::Int ||
                                      scrutinee->type.kind == TypeKind::Bool ||
                                      scrutinee->type.kind == TypeKind::Enum);
    if (scrutinee && !usable && !isUnknown(scrutinee->type)) {
        err("E0708",
            s.expr->span,
            std::format("cannot `switch` on `{}`", typeToString(scrutinee->type)),
            "integers, bools and enums can be switched on; use `if` for other types");
    }
    hir::Stmt block;
    block.kind = hir::StmtKind::Block;
    block.span = s.span;
    scopes_.push(ScopeKind::Block);
    const Type st = usable ? scrutinee->type : unknownType();
    const hir::LocalId id = hiddenLocal("sw", st, s.span);
    const std::string name = fn_->locals[id].name;
    if (usable) {
        hir::Stmt d;
        d.kind = hir::StmtKind::Local;
        d.span = s.span;
        d.local = id;
        d.value = std::move(scrutinee);
        block.body.push_back(std::move(d));
    }
    bindLocal(name, id, st, s.span);

    struct Arm {
        EP cond;
        std::vector<hir::Stmt> body;
    };
    std::vector<Arm> arms;
    std::vector<hir::Stmt> default_body;
    bool has_default = false;
    std::set<std::uint64_t> seen;
    for (const SwitchCase& c : s.cases) {
        Arm arm;
        if (c.is_default) {
            if (has_default) {
                err("E0709",
                    c.span,
                    "a `switch` has more than one `default`",
                    "remove the duplicate");
            }
            has_default = true;
            lowerBlock(*c.body, default_body, true);
            continue;
        }
        if (c.values.empty()) {
            err("E0709", c.span, "`case` needs at least one value", "write `case 1, 2 { ... }`");
        }
        ExprPtr cond_ast;
        for (const ExprPtr& v : c.values) {
            ExprPtr eq = astBinary(BinaryOp::Eq, astName(name, v->span), cloneAst(*v), v->span);
            cond_ast = cond_ast
                           ? astBinary(BinaryOp::Or, std::move(cond_ast), std::move(eq), v->span)
                           : std::move(eq);
            const auto cv = s_.evalConst(*v);
            if (cv && cv->kind == ConstValue::Kind::Int &&
                !seen.insert(static_cast<std::uint64_t>(cv->int_value)).second) {
                err("E0709",
                    v->span,
                    "duplicate `case` value",
                    "this value is already handled by an earlier `case`");
            }
        }
        if (cond_ast && usable) {
            arm.cond = coerce(expr(*cond_ast, &boolean), boolean, c.span);
        }
        lowerBlock(*c.body, arm.body, true);
        if (arm.cond) {
            arms.push_back(std::move(arm));
        }
    }
    scopes_.pop();

    std::vector<hir::Stmt> chain = std::move(default_body);
    for (std::size_t i = arms.size(); i-- > 0;) {
        hir::Stmt ifs;
        ifs.kind = hir::StmtKind::If;
        ifs.span = s.span;
        ifs.value = std::move(arms[i].cond);
        ifs.body = std::move(arms[i].body);
        ifs.else_body = std::move(chain);
        chain.clear();
        chain.push_back(std::move(ifs));
    }
    if (!usable) {
        return;
    }
    for (hir::Stmt& c : chain) {
        block.body.push_back(std::move(c));
    }
    out.push_back(std::move(block));
}

Lowerer::EP Lowerer::ternaryExpr(const Expr& e, const Type* expected) {
    const Type boolean = Type::makeBool();
    EP cond = coerce(expr(*e.lhs, &boolean), boolean, e.lhs->span);
    const Expr& then_ast = *e.rhs;
    const Expr& else_ast = *e.args[0];
    const auto isLit = [](const Expr& x) {
        return x.kind == Expr::Kind::IntLiteral || x.kind == Expr::Kind::FloatLiteral;
    };
    EP a;
    EP b;
    if (isLit(then_ast) && !isLit(else_ast)) {
        b = expr(else_ast, expected);
        const Type want = b ? b->type : unknownType();
        a = expr(then_ast, b ? &want : expected);
    } else {
        a = expr(then_ast, expected);
        const Type want = a ? a->type : unknownType();
        b = expr(else_ast, a ? &want : expected);
    }
    if (!cond || !a || !b) {
        return nullptr;
    }
    if (a->type.isVoid() || isUnknown(a->type)) {
        err("E0708",
            e.span,
            "the branches of `?:` need a value",
            "a `void` or unknown expression cannot be selected");
        return nullptr;
    }
    const Type t = a->type;
    b = coerce(std::move(b), t, else_ast.span);
    if (!b) {
        return nullptr;
    }
    EP sel = mk(hir::ExprKind::Select, t, e.span);
    sel->lhs = std::move(cond);
    sel->rhs = std::move(a);
    sel->args.push_back(std::move(b));
    return sel;
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
                err("E0708",
                    s.expr->span,
                    std::format("`{}` cannot be bound to a `void` value", s.name),
                    "the expression has no value");
                init.reset();
                t = unknownType();
            }
        } else {
            err("E0708",
                s.span,
                std::format("cannot infer the type of `{}`", s.name),
                "write a type (`local x: T`) or an initial value");
            t = unknownType();
        }
        h.kind = hir::StmtKind::Local;
        h.local = addLocal(s.name, t, false, s.span);
        h.value = std::move(init);
        bindLocal(
            s.name, h.local, t, s.span); // after the initializer: `local x = x` sees the outer `x`
        break;
    }
    case Stmt::Kind::Const:
        lowerLocalConst(s);
        return;
    case Stmt::Kind::Assign:
        if (s.has_compound_op) {
            lowerCompound(s, out);
        } else {
            lowerAssign(*s.target, *s.expr, s.span, out);
        }
        return;
    case Stmt::Kind::Return: {
        h.kind = hir::StmtKind::Return;
        if (s.expr) {
            if (ret_.isVoid()) {
                err("E0708",
                    s.expr->span,
                    "a `void` function cannot return a value",
                    "remove the value or add `-> T`");
                (void)expr(*s.expr, nullptr);
            } else {
                h.value = coerce(expr(*s.expr, &ret_), ret_, s.expr->span);
                if (!h.value) {
                    return;
                }
            }
        } else if (!ret_.isVoid() && !isUnknown(ret_)) {
            err("E0708",
                s.span,
                std::format("`return` needs a value of type `{}`", typeToString(ret_)),
                "missing value");
        }
        const bool pending = std::ranges::any_of(defers_, [](const auto& f) { return !f.empty(); });
        if (pending) {
            // The returned value is computed before the deferred statements run.
            if (h.value) {
                const Type vt = h.value->type;
                const hir::LocalId id = hiddenLocal("ret", vt, s.span);
                hir::Stmt keep;
                keep.kind = hir::StmtKind::Local;
                keep.span = s.span;
                keep.local = id;
                keep.value = std::move(h.value);
                out.push_back(std::move(keep));
                EP ref = mk(hir::ExprKind::LocalRef, vt, s.span);
                ref->local = id;
                ref->lvalue = true;
                h.value = std::move(ref);
            }
            emitDefers(0, out);
        }
        break;
    }
    case Stmt::Kind::DoWhile: {
        h.kind = hir::StmtKind::While;
        h.post_test = true;
        loops_.push_back({s.label, defers_.size()});
        lowerBlock(*s.body, h.body, true);
        loops_.pop_back();
        h.value = coerce(expr(*s.expr, &boolean), boolean, s.expr->span);
        if (!h.value) {
            return;
        }
        break;
    }
    case Stmt::Kind::For:
        lowerFor(s, out);
        return;
    case Stmt::Kind::ForRange:
        lowerForRange(s, out);
        return;
    case Stmt::Kind::Switch:
        lowerSwitch(s, out);
        return;
    case Stmt::Kind::Defer: {
        const Stmt& inner = *s.step;
        switch (inner.kind) {
        case Stmt::Kind::Return:
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
        case Stmt::Kind::Defer:
        case Stmt::Kind::Local:
        case Stmt::Kind::Const:
            err("E0709",
                inner.span,
                "this statement cannot be deferred",
                "a `defer` runs when the block is left; it cannot return, jump, declare a variable "
                "or defer again");
            return;
        default:
            break;
        }
        std::vector<hir::Stmt> lowered;
        lowerStmt(inner, lowered);
        for (hir::Stmt& d : lowered) {
            defers_.back().push_back(std::move(d));
        }
        return;
    }
    case Stmt::Kind::Expr: {
        if (s.expr->kind == Expr::Kind::Call && s.expr->lhs->kind == Expr::Kind::Name &&
            s.expr->lhs->text == "staticAssert" && lookupName("staticAssert") == nullptr) {
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
        loops_.push_back({s.label, defers_.size()});
        lowerBlock(*s.body, h.body, true);
        loops_.pop_back();
        if (!h.value) {
            return;
        }
        break;
    }
    case Stmt::Kind::Continue:
    case Stmt::Kind::Break:
        lowerJump(s, out);
        return;
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
    const auto addr = [](const Type& t) {
        return t.kind == TypeKind::Pointer || t.kind == TypeKind::FnPtr;
    };
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
    const std::string iface_q = iface.path.empty() ? std::string{} : iface.path.front();
    EP pointer;
    std::string type_q;
    if (value->type.kind == TypeKind::Pointer && value->type.elem &&
        isAggregate(*value->type.elem)) {
        type_q = value->type.elem->path.front();
        pointer = std::move(value);
    } else {
        type_q = value->type.path.front();
        if (!value->lvalue) {
            err("E0709",
                at,
                "cannot convert a temporary to an interface",
                "the interface value points at the object; store it in a variable first");
            return nullptr;
        }
        EP addr = mk(hir::ExprKind::AddrOf, Type::makePointer(value->type), at);
        addr->lhs = std::move(value);
        pointer = std::move(addr);
    }
    if (findImpl(type_q, iface_q) == nullptr) {
        Diagnostic d;
        d.code = "E0504";
        d.severity = Severity::Error;
        d.message = std::format("`{}` does not implement `{}`", type_q, iface_q);
        d.labels.push_back({at, std::format("`{}` -> `{}` needs an `impl`", type_q, iface_q)});
        d.detail = std::format("No `impl {} for {}` was found.", iface_q, type_q);
        d.reason = "Interfaces are nominal; matching method names never count.";
        d.fix = std::format("add `impl {} for {} {{ ... }}`", iface_q, type_q);
        s_.diag_.report(std::move(d));
        return nullptr;
    }
    EP out = mk(hir::ExprKind::ToInterface, iface, at);
    out->text = type_q;
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
        err("E0708",
            at,
            std::format("`nil` cannot be used as `{}`", typeToString(to)),
            "only pointers can be `nil`");
        return nullptr;
    }
    if (to.kind == TypeKind::Interface) {
        if (value->type.kind == TypeKind::Interface) {
            if (sameType(value->type, to)) {
                return value;
            }
            err("E0708",
                at,
                std::format(
                    "expected `{}`, found `{}`", typeToString(to), typeToString(value->type)),
                "different interfaces");
            return nullptr;
        }
        const bool agg =
            isAggregate(value->type) || (value->type.kind == TypeKind::Pointer &&
                                         value->type.elem && isAggregate(*value->type.elem));
        if (agg) {
            return toInterface(std::move(value), to, at);
        }
    }
    const ConversionResult r = implicitConversion(value->type, to, inUnsafe(), ptrBits());
    switch (r.status) {
    case ConversionStatus::Ok:
        return castTo(std::move(value), to, true);
    case ConversionStatus::Warn:
        s_.diag_.warning("W0601",
                         at,
                         std::format("`{}` is implicitly converted to `{}`, precision may be lost",
                                     typeToString(value->type),
                                     typeToString(to)),
                         "write `as` to silence");
        return castTo(std::move(value), to, true);
    case ConversionStatus::Signedness:
        err("E0601",
            at,
            std::format("implicit conversion from `{}` to `{}` changes signedness",
                        typeToString(value->type),
                        typeToString(to)),
            "write `as`, or use `unsafe { }`");
        return nullptr;
    case ConversionStatus::NeedsCast:
        err("E0708",
            at,
            std::format("expected `{}`, found `{}`", typeToString(to), typeToString(value->type)),
            r.note.empty() ? "an explicit `as` is required" : r.note);
        return nullptr;
    case ConversionStatus::Invalid:
        err("E0708",
            at,
            std::format("expected `{}`, found `{}`", typeToString(to), typeToString(value->type)),
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
                 : (v.int_value > 2147483647 || v.int_value < -2147483648LL)
                     ? Type::makeInt(64, true)
                     : Type::makeInt(32, true);
        return intConst(static_cast<std::uint64_t>(v.int_value), std::move(t), span);
    }
    case ConstValue::Kind::Float: {
        EP e = mk(hir::ExprKind::FloatConst,
                  want.kind == TypeKind::Float ? want : Type::makeFloat(64),
                  span);
        e->float_value = v.float_value;
        return e;
    }
    case ConstValue::Kind::Bool: {
        EP e = mk(hir::ExprKind::BoolConst, Type::makeBool(), span);
        e->bool_value = v.bool_value;
        return e;
    }
    case ConstValue::Kind::String: return stringLiteral(v.string_value, expected, span);
    }
    return nullptr;
}

Lowerer::EP Lowerer::constExpr(const Expr& e, const Type* expected) {
    const auto v = s_.evalConst(e);
    return v ? fromConst(*v, expected, e.span) : nullptr;
}

/// A string literal takes its type from the context (see docs/LANGUAGE.md, "Strings"):
/// `*ui8` (raw NUL-terminated bytes), `[ui8; N]` (a mutable zero-padded copy) or `str`.
Lowerer::EP Lowerer::stringLiteral(const std::string& text, const Type* expected, const Span& span) {
    const Type want = expected != nullptr ? resolved(*expected) : unknownType();
    const Type byte = Type::makeInt(8, false);
    const bool raw = want.kind == TypeKind::Pointer ||
                     (want.kind == TypeKind::Enum && want.elem && want.elem->kind == TypeKind::Pointer);
    if (raw) {
        EP p = mk(hir::ExprKind::StrConst, Type::makePointer(byte), span);
        p->text = text;
        return p;
    }
    if (want.kind == TypeKind::Array && want.elem && want.elem->kind == TypeKind::Int &&
        want.elem->bits == 8 && !want.elem->is_signed) {
        if (const auto n = arrayLength(s_, want)) {
            if (text.size() > *n) {
                err("E0717",
                    span,
                    std::format("string literal of {} bytes does not fit in `{}`",
                                text.size(),
                                typeToString(want)),
                    "use a longer array or a `str`");
                return nullptr;
            }
            EP list = mk(hir::ExprKind::ArrayList, want, span);
            for (std::uint64_t i = 0; i < *n; ++i) {
                const std::uint64_t b = i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
                list->args.push_back(intConst(b, byte, span));
            }
            return list;
        }
    }
    EP s = mk(hir::ExprKind::StrConst, Type::makeStr(), span);
    s->text = text;
    return s;
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
        Type t = want.kind == TypeKind::Int    ? want
                 : e.int_value > 2147483647ULL ? Type::makeInt(64, true)
                                               : Type::makeInt(32, true);
        return intConst(e.int_value, std::move(t), e.span);
    }
    case Expr::Kind::FloatLiteral: {
        Type want = expected != nullptr ? resolved(*expected) : unknownType();
        EP f = mk(hir::ExprKind::FloatConst,
                  want.kind == TypeKind::Float ? want : Type::makeFloat(64),
                  e.span);
        f->float_value = e.float_value;
        return f;
    }
    case Expr::Kind::BoolLiteral: {
        EP b = mk(hir::ExprKind::BoolConst, Type::makeBool(), e.span);
        b->bool_value = e.bool_value;
        return b;
    }
    case Expr::Kind::StringLiteral: return stringLiteral(e.text, expected, e.span);
    case Expr::Kind::Name:
        return nameExpr(e, expected);
    case Expr::Kind::Field:
        return fieldExpr(e);
    case Expr::Kind::Unary:
        return unaryExpr(e, expected);
    case Expr::Kind::Binary:
        return binaryExpr(e, expected);
    case Expr::Kind::Ternary:
        return ternaryExpr(e, expected);
    case Expr::Kind::Cast:
        return castExpr(e);
    case Expr::Kind::Call:
        return callExpr(e, expected);
    case Expr::Kind::Index:
        return indexExpr(e);
    case Expr::Kind::ArrayList:
    case Expr::Kind::ArrayRepeat:
        return arrayExpr(e, expected);
    case Expr::Kind::StructLiteral:
        return structLiteral(e);
    case Expr::Kind::New:
        return newExpr(e);
    case Expr::Kind::MacroCall:
        err("E0004",
            e.span,
            std::format("macro `{}!` was not expanded", e.text),
            "no macro with this name is visible; import it");
        return nullptr;
    case Expr::Kind::AsmOperand:
        err("E0709",
            e.span,
            "inline asm operands are only valid inside `__builtin_asm`",
            "misplaced operand");
        return nullptr;
    }
    return nullptr;
}

Lowerer::EP Lowerer::nameExpr(const Expr& e, const Type* expected) {
    const ScopeEntry* entry = lookupName(e.text);
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
        err("E0706",
            e.span,
            std::format("unknown name `{}`", e.text),
            "not declared in this scope");
        return nullptr;
    }
    return fromEntry(*entry, e, expected);
}

Lowerer::EP Lowerer::fromEntry(const ScopeEntry& entry, const Expr& at, const Type* expected) {
    switch (entry.kind) {
    case ScopeEntry::Kind::Local: {
        EP l = mk(hir::ExprKind::LocalRef, entry.type, at.span);
        l->local = entry.local;
        l->text = entry.name;
        l->lvalue = true;
        return l;
    }
    case ScopeEntry::Kind::Global: {
        EP g = mk(hir::ExprKind::GlobalRef, entry.type, at.span);
        g->text = entry.qname.empty() ? entry.name : entry.qname;
        g->lvalue = true;
        g->readonly = entry.readonly;
        return g;
    }
    case ScopeEntry::Kind::Function: {
        Sema& owner = entry.owner != nullptr ? *entry.owner : s_;
        EP f = mk(
            hir::ExprKind::FuncRef, fnTypeOf(owner, std::get<FnDecl>(entry.item->decl)), at.span);
        f->text = entry.qname.empty() ? entry.name : entry.qname;
        return f;
    }
    case ScopeEntry::Kind::Constant: {
        // Evaluated in the module that defines it.
        Sema& owner = entry.owner != nullptr ? *entry.owner : s_;
        Expr name;
        name.kind = Expr::Kind::Name;
        name.text = declBase(*entry.item).name;
        name.span = at.span;
        const auto v = owner.evalConst(name);
        return v ? fromConst(*v, expected, at.span) : nullptr;
    }
    case ScopeEntry::Kind::Type:
        err("E0706",
            at.span,
            std::format("`{}` is a type, not a value", entry.name),
            "a value is expected here");
        return nullptr;
    case ScopeEntry::Kind::Namespace:
        err("E0706",
            at.span,
            std::format("`{}` is a module, not a value", entry.name),
            std::format("use `{}.name`", entry.name));
        return nullptr;
    case ScopeEntry::Kind::External: {
        EP x = mk(hir::ExprKind::External, unknownType(), at.span);
        x->text = entry.module;
        x->member = entry.member;
        return x;
    }
    }
    return nullptr;
}

Lowerer::EP Lowerer::fieldExpr(const Expr& e) {
    if (const ScopeEntry* head = chainEntry(*e.lhs)) {
        if (head->kind == ScopeEntry::Kind::Namespace) {
            const ScopeEntry* member = namespaceMember(*head, e.text, e.span, true);
            return member != nullptr ? fromEntry(*member, e, nullptr) : nullptr;
        }
        if (head->kind == ScopeEntry::Kind::Type && head->item != nullptr &&
            std::holds_alternative<EnumDecl>(head->item->decl)) {
            Sema& owner = head->owner != nullptr ? *head->owner : s_;
            const auto v = owner.enumMember(declBase(*head->item).name, e.text, e.span);
            if (!v) {
                return nullptr;
            }
            Type et = Type::makeNamed({declBase(*head->item).name});
            et.span = e.span;
            const Type enum_type = resolvedIn(owner, et);
            EP out = fromConst(*v, nullptr, e.span);
            if (out && out->kind != hir::ExprKind::FloatConst &&
                out->kind != hir::ExprKind::BoolConst) {
                out->type = enum_type;
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
    if (agg.kind == TypeKind::Str) {
        // `str` has two read-only fields: `ptr` (index 0) and `len` (index 1).
        Type field_type;
        std::uint32_t index = 0;
        if (e.text == "ptr") {
            field_type = Type::makePointer(Type::makeInt(8, false));
        } else if (e.text == "len") {
            field_type = Type::makeInt(0, false);
            index = 1;
        } else {
            err("E0709",
                e.span,
                std::format("`str` has no field `{}`", e.text),
                "the fields are `ptr` and `len`");
            return nullptr;
        }
        EP f = mk(hir::ExprKind::Field, field_type, e.span);
        f->text = e.text;
        f->index = index;
        f->through_pointer = through;
        f->lvalue = false;
        f->lhs = std::move(base);
        return f;
    }
    if (!isAggregate(agg)) {
        err("E0709",
            e.span,
            std::format("`{}` has no field `{}`", typeToString(base->type), e.text),
            "only structs and classes have fields");
        return nullptr;
    }
    std::vector<FieldInfo> fields;
    collectFields(agg.path.front(), fields);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].decl->name == e.text) {
            EP f = mk(
                hir::ExprKind::Field, resolvedIn(*fields[i].owner, fields[i].decl->type), e.span);
            f->text = e.text;
            f->index = static_cast<std::uint32_t>(i);
            f->through_pointer = through;
            f->lvalue = through || base->lvalue;
            f->readonly = !through && base->readonly;
            f->lhs = std::move(base);
            return f;
        }
    }
    err("E0709",
        e.span,
        std::format("`{}` has no field `{}`", typeToString(agg), e.text),
        "unknown field");
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
        if (!(v->type.kind == TypeKind::Float ||
              (v->type.kind == TypeKind::Int && v->type.is_signed))) {
            err("E0709",
                e.span,
                std::format("cannot negate `{}`", typeToString(v->type)),
                "negation needs a signed integer or a float");
            return nullptr;
        }
        EP n = mk(hir::ExprKind::Unary, v->type, e.span);
        n->unary_op = UnaryOp::Neg;
        n->lhs = std::move(v);
        return n;
    }
    case UnaryOp::BitNot: {
        EP v = expr(*e.lhs, expected);
        if (!v || isUnknown(v->type)) {
            return v;
        }
        if (v->type.kind != TypeKind::Int) {
            err("E0709",
                e.span,
                std::format("cannot apply `~` to `{}`", typeToString(v->type)),
                "bitwise not needs an integer");
            return nullptr;
        }
        EP n = mk(hir::ExprKind::Unary, v->type, e.span);
        n->unary_op = UnaryOp::BitNot;
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
            err("E0709",
                e.span,
                std::format("cannot dereference `{}`", typeToString(v->type)),
                "not a pointer");
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
            return v; // `&fn` is the function pointer itself
        }
        if (!isUnknown(v->type) && !v->lvalue) {
            err("E0709",
                e.span,
                "cannot take the address of this expression",
                "not a place (variable, `*p`, field or element)");
            return nullptr;
        }
        EP a = mk(hir::ExprKind::AddrOf,
                  isUnknown(v->type) ? unknownType() : Type::makePointer(v->type),
                  e.span);
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
    const auto isFloatLiteral = [](const Expr& x) {
        return x.kind == Expr::Kind::FloatLiteral ||
               (x.kind == Expr::Kind::Unary && x.lhs && x.lhs->kind == Expr::Kind::FloatLiteral);
    };
    // A bare literal takes the type of the other operand.
    EP l;
    EP r;
    const Type* hint = is_cmp ? nullptr : expected;
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

    if (op == BinaryOp::Shl || op == BinaryOp::Shr) {
        // The result has the type of the left operand; the count may be any integer type and is
        // converted to it. A count >= the bit width yields 0 (`>>` of a negative signed value: -1).
        if (l->type.kind != TypeKind::Int || r->type.kind != TypeKind::Int) {
            err("E0709",
                e.span,
                std::format("operator `{}` does not apply to `{}` and `{}`",
                            binaryOpName(op),
                            typeToString(l->type),
                            typeToString(r->type)),
                "shifts need integer operands");
            return nullptr;
        }
        if (!sameType(l->type, r->type)) {
            EP c = mk(hir::ExprKind::Cast, l->type, e.rhs->span);
            c->lhs = std::move(r);
            r = std::move(c);
        }
        EP b = mk(hir::ExprKind::Binary, l->type, e.span);
        b->binary_op = op;
        b->lhs = std::move(l);
        b->rhs = std::move(r);
        return b;
    }
    Type common;
    if (isNumeric(l->type) && isNumeric(r->type)) {
        const CommonTypeResult c = commonBinaryType(l->type, r->type, ptrBits());
        if (c.status == ConversionStatus::Signedness) {
            err("E0601",
                e.span,
                std::format("`{}` and `{}` have different signedness",
                            typeToString(l->type),
                            typeToString(r->type)),
                "write `as` on one side");
            return nullptr;
        }
        if (c.status != ConversionStatus::Ok) {
            err("E0709", e.span, "operands cannot be combined", "incompatible numeric types");
            return nullptr;
        }
        if (c.warn) {
            s_.diag_.warning(
                "W0601",
                e.span,
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
        if (!sameType(l->type, r->type) || !scalar ||
            (!eq_only && l->type.kind != TypeKind::Enum)) {
            err("E0709",
                e.span,
                std::format(
                    "cannot compare `{}` with `{}`", typeToString(l->type), typeToString(r->type)),
                "operands must have the same comparable type");
            return nullptr;
        }
        common = l->type;
    } else {
        err("E0709",
            e.span,
            std::format("operator `{}` does not apply to `{}` and `{}`",
                        binaryOpName(op),
                        typeToString(l->type),
                        typeToString(r->type)),
            "arithmetic needs numeric operands; cast pointers with `as usize`");
        return nullptr;
    }
    if (op == BinaryOp::Mod && common.kind == TypeKind::Float) {
        err("E0709", e.span, "`%` needs integer operands", "floats have no remainder operator");
        return nullptr;
    }
    if ((op == BinaryOp::BitAnd || op == BinaryOp::BitOr || op == BinaryOp::BitXor) &&
        common.kind != TypeKind::Int) {
        err("E0709",
            e.span,
            std::format("operator `{}` needs integer operands", binaryOpName(op)),
            "bitwise operators apply to integers only");
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
    if (v->kind == hir::ExprKind::Nil &&
        (to.kind == TypeKind::Pointer || to.kind == TypeKind::FnPtr)) {
        v->type = to;
        return v;
    }
    if (!castable(v->type, to)) {
        err("E0709",
            e.span,
            std::format("cannot cast `{}` to `{}`", typeToString(v->type), typeToString(to)),
            "no explicit conversion exists");
        return nullptr;
    }
    EP c = mk(hir::ExprKind::Cast, to, e.span);
    c->lhs = std::move(v);
    return c;
}

bool Lowerer::lowerArgs(const Expr& call,
                        const std::vector<Type>& params,
                        std::vector<EP>& out,
                        const std::string& callee) {
    if (call.args.size() != params.size()) {
        err("E0710",
            call.span,
            std::format(
                "`{}` takes {} argument(s), {} given", callee, params.size(), call.args.size()),
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
        err("E0710",
            e.span,
            std::format("`syscall` has {} argument(s) after the number, `{}` allows at most {}",
                        e.args.size() - 1,
                        s_.config_.target.triple.empty()
                            ? s_.config_.target.os + "/" + s_.config_.target.arch
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
        if (!isUnknown(v->type) && v->type.kind != TypeKind::Int &&
            v->type.kind != TypeKind::Pointer) {
            err("E0708",
                a->span,
                std::format("`syscall` arguments are integers or pointers, found `{}`",
                            typeToString(v->type)),
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

/// A compile-time `MemoryOrder` member (`MemoryOrder.seqCst`): 0 relaxed .. 4 seqCst.
std::optional<std::uint32_t> Lowerer::memoryOrder(const Expr& arg) {
    EP v = expr(arg, nullptr);
    if (!v) {
        return std::nullopt;
    }
    if (v->kind != hir::ExprKind::IntConst || v->type.kind != TypeKind::Enum ||
        v->type.path.empty() || v->type.path.front() != "MemoryOrder") {
        err("E0708",
            arg.span,
            "the memory order must be a `MemoryOrder` member",
            "write `MemoryOrder.seqCst` (a compile-time constant)");
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(v->int_value);
}

Lowerer::EP Lowerer::builtinCall(const Expr& e, const std::string& name) {
    const bool is_load = name == "__builtin_atomicLoad";
    const bool is_store = name == "__builtin_atomicStore";
    const bool is_xchg = name == "__builtin_atomicXchg";
    const bool is_cas = name == "__builtin_atomicCas";
    const bool is_add = name == "__builtin_atomicAdd";
    if (name == "__builtin_asm") {
        return asmCall(e);
    }
    if (name == "__builtin_fence") {
        if (e.args.size() != 1) {
            err("E0710",
                e.span,
                "`__builtin_fence` takes the memory order",
                "wrong number of arguments");
            return nullptr;
        }
        const auto order = memoryOrder(*e.args[0]);
        if (!order) {
            return nullptr;
        }
        if (*order == 0) {
            err("E0708",
                e.args[0]->span,
                "a fence cannot be `relaxed`",
                "use acquire, release, acqRel or seqCst");
            return nullptr;
        }
        EP f = mk(hir::ExprKind::Fence, Type::makeVoid(), e.span);
        f->index = *order;
        return f;
    }
    if (!(is_load || is_store || is_xchg || is_cas || is_add)) {
        err("E0706",
            e.lhs->span,
            std::format("unknown builtin `{}`", name),
            "not in the closed builtin list");
        return nullptr;
    }
    const std::size_t want_args = is_load ? 2 : (is_cas ? 4 : 3);
    if (e.args.size() != want_args) {
        err("E0710",
            e.span,
            std::format("`{}` takes {} arguments, {} given", name, want_args, e.args.size()),
            "wrong number of arguments");
        for (const ExprPtr& a : e.args) {
            (void)expr(*a, nullptr);
        }
        return nullptr;
    }
    EP ptr = expr(*e.args[0], nullptr);
    if (!ptr) {
        return nullptr;
    }
    if (isUnknown(ptr->type)) {
        return mk(hir::ExprKind::AtomicLoad, unknownType(), e.span);
    }
    if (ptr->type.kind != TypeKind::Pointer || !ptr->type.elem) {
        err("E0708",
            e.args[0]->span,
            std::format("atomics take a pointer, found `{}`", typeToString(ptr->type)),
            "pointer expected");
        return nullptr;
    }
    const Type elem = *ptr->type.elem;
    const bool ok_type =
        elem.kind == TypeKind::Int || elem.kind == TypeKind::Bool || elem.kind == TypeKind::Pointer;
    if (!ok_type) {
        err("E0708",
            e.args[0]->span,
            std::format("atomics work on integers, `bool` and pointers, not `{}`",
                        typeToString(elem)),
            "unsupported atomic type");
        return nullptr;
    }
    if (is_add && elem.kind != TypeKind::Int) {
        err("E0708", e.args[0]->span, "`__builtin_atomicAdd` needs an integer", "not an integer");
        return nullptr;
    }
    const auto order = memoryOrder(*e.args[want_args - 1]);
    if (!order) {
        return nullptr;
    }
    if (is_load && (*order == 2 || *order == 3)) {
        err("E0708",
            e.args[want_args - 1]->span,
            "a load cannot be `release` or `acqRel`",
            "use relaxed, acquire or seqCst");
        return nullptr;
    }
    if (is_store && (*order == 1 || *order == 3)) {
        err("E0708",
            e.args[want_args - 1]->span,
            "a store cannot be `acquire` or `acqRel`",
            "use relaxed, release or seqCst");
        return nullptr;
    }
    const auto valueArg = [&](std::size_t i) {
        return coerce(expr(*e.args[i], &elem), elem, e.args[i]->span);
    };
    hir::ExprKind kind = hir::ExprKind::AtomicLoad;
    Type result = elem;
    if (is_store) {
        kind = hir::ExprKind::AtomicStore;
        result = Type::makeVoid();
    } else if (is_xchg) {
        kind = hir::ExprKind::AtomicXchg;
    } else if (is_cas) {
        kind = hir::ExprKind::AtomicCas;
    } else if (is_add) {
        kind = hir::ExprKind::AtomicAdd;
    }
    EP out = mk(kind, result, e.span);
    out->index = *order;
    out->lhs = std::move(ptr);
    if (is_store || is_xchg || is_add) {
        out->rhs = valueArg(1);
        if (!out->rhs) {
            return nullptr;
        }
    } else if (is_cas) {
        out->rhs = valueArg(1);
        EP desired = valueArg(2);
        if (!out->rhs || !desired) {
            return nullptr;
        }
        out->args.push_back(std::move(desired));
    }
    return out;
}

Lowerer::EP Lowerer::asmCall(const Expr& e) {
    if (e.args.empty() || e.args[0]->kind != Expr::Kind::StringLiteral) {
        err("E0709",
            e.span,
            "`__builtin_asm` needs a template string first",
            "write `__builtin_asm(\"nop\")`");
        return nullptr;
    }
    EP out = mk(hir::ExprKind::Asm, Type::makeVoid(), e.span);
    out->text = e.args[0]->text;
    bool ok = true;
    for (std::size_t i = 1; i < e.args.size(); ++i) {
        const Expr& a = *e.args[i];
        if (a.kind == Expr::Kind::AsmOperand) {
            hir::AsmOperand op;
            op.kind = a.text == "in"    ? hir::AsmOperand::Kind::In
                      : a.text == "out" ? hir::AsmOperand::Kind::Out
                                        : hir::AsmOperand::Kind::InOut;
            if (a.lhs->kind == Expr::Kind::StringLiteral) {
                op.reg = a.lhs->text;
            } else if (a.lhs->kind == Expr::Kind::Name && a.lhs->text == "reg") {
                op.reg = "reg";
            } else {
                err("E0709",
                    a.lhs->span,
                    "register must be `reg` or a register name string",
                    "invalid register");
                ok = false;
                continue;
            }
            EP v = expr(*a.rhs, nullptr);
            if (!v) {
                ok = false;
                continue;
            }
            if (!isUnknown(v->type) && v->type.kind != TypeKind::Int &&
                v->type.kind != TypeKind::Pointer && v->type.kind != TypeKind::Float &&
                v->type.kind != TypeKind::FnPtr) {
                err("E0708",
                    a.rhs->span,
                    std::format("asm operands are scalars, found `{}`", typeToString(v->type)),
                    "integer, float or pointer expected");
                ok = false;
                continue;
            }
            if (op.kind != hir::AsmOperand::Kind::In && !v->lvalue) {
                err("E0709",
                    a.rhs->span,
                    "an asm output needs a place",
                    "write a variable or `*p`");
                ok = false;
                continue;
            }
            op.value = std::move(v);
            out->asm_operands.push_back(std::move(op));
        } else if (a.kind == Expr::Kind::Call && a.lhs->kind == Expr::Kind::Name &&
                   (a.lhs->text == "clobber" || a.lhs->text == "options")) {
            for (const ExprPtr& arg : a.args) {
                if (arg->kind != Expr::Kind::StringLiteral) {
                    err("E0709",
                        arg->span,
                        std::format("`{}` takes strings", a.lhs->text),
                        "string expected");
                    ok = false;
                    continue;
                }
                if (a.lhs->text == "clobber") {
                    out->clobbers.push_back(arg->text);
                } else if (arg->text == "nomem") {
                    out->nomem = true;
                } else if (arg->text == "noreturn") {
                    out->noreturn = true;
                } else {
                    err("E0709",
                        arg->span,
                        std::format("unknown asm option `{}`", arg->text),
                        "known options: nomem, noreturn");
                    ok = false;
                }
            }
        } else {
            err("E0709",
                a.span,
                "unexpected `__builtin_asm` argument",
                "operands are `in(..)`, `out(..)`, `inout(..)`, `clobber(..)`, `options(..)`");
            ok = false;
        }
    }
    if (!ok) {
        return nullptr;
    }
    return out;
}

std::optional<Lowerer::MethodRef> Lowerer::findMethod(const Type& agg, const std::string& name) {
    if (agg.kind == TypeKind::Interface) {
        const auto ref = entityOfType(agg);
        if (!ref || !std::holds_alternative<InterfaceDecl>(ref->sym->item->decl)) {
            return std::nullopt;
        }
        const auto& decl = std::get<InterfaceDecl>(ref->sym->item->decl);
        for (std::size_t i = 0; i < decl.methods.size(); ++i) {
            if (decl.methods[i].name == name) {
                MethodRef m;
                m.kind = MethodRef::Kind::Itable;
                m.decl = &decl.methods[i];
                m.owner = ref->owner;
                m.index = static_cast<std::uint32_t>(i);
                return m;
            }
        }
        return std::nullopt;
    }
    std::string cur = agg.path.front();
    std::set<std::string> seen;
    while (!cur.empty() && seen.insert(cur).second) {
        const auto ref = s_.entityByQName(cur);
        const ClassDecl* c = ref ? std::get_if<ClassDecl>(&ref->sym->item->decl) : nullptr;
        if (c != nullptr) {
            for (const FnDecl& m : c->methods) {
                if (m.name == name) {
                    MethodRef r;
                    r.kind = (m.is_virtual || m.is_override) ? MethodRef::Kind::Virtual
                                                             : MethodRef::Kind::Direct;
                    r.fn_name = cur + "." + name;
                    r.decl = &m;
                    r.owner = ref->owner;
                    if (r.kind == MethodRef::Kind::Virtual) {
                        const auto& slots = vtableOf(agg.path.front());
                        for (std::size_t i = 0; i < slots.size(); ++i) {
                            if (slots[i].slot.method == name) {
                                r.index = static_cast<std::uint32_t>(i);
                                r.fn_name = slots[i].slot.function;
                            }
                        }
                    }
                    return r;
                }
            }
        }
        for (const ImplInfo& i : sh_.impls) {
            if (i.type_q != cur) {
                continue;
            }
            for (const FnDecl& m : i.decl->methods) {
                if (m.name == name) {
                    MethodRef r;
                    r.fn_name = i.prefix + name;
                    r.decl = &m;
                    r.owner = i.owner;
                    r.from_impl = true;
                    return r;
                }
            }
        }
        cur.clear();
        if (c != nullptr && c->has_base) {
            Type base = Type::makeNamed(c->base);
            if (const auto r = ref->owner->resolve(base); r && r->kind == TypeKind::Class) {
                cur = r->path.front();
            }
        }
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
        err("E0709",
            callee.span,
            std::format("`{}` has no method `{}`", typeToString(recv->type), callee.text),
            "only structs, classes and interfaces have methods");
        return nullptr;
    }
    // `value.field(args)` where `field` holds a function pointer: an indirect call.
    if (isAggregate(agg)) {
        std::vector<FieldInfo> fields;
        collectFields(agg.path.front(), fields);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].decl->name != callee.text) {
                continue;
            }
            const Type ft = resolvedIn(*fields[i].owner, fields[i].decl->type);
            if (ft.kind != TypeKind::FnPtr || !ft.ret) {
                break;
            }
            EP f = mk(hir::ExprKind::Field, ft, callee.span);
            f->text = callee.text;
            f->index = static_cast<std::uint32_t>(i);
            f->through_pointer = through;
            f->lvalue = true;
            f->lhs = std::move(recv);
            std::vector<EP> args;
            if (!lowerArgs(e, ft.params, args, callee.text)) {
                return nullptr;
            }
            EP c = mk(hir::ExprKind::CallIndirect, *ft.ret, e.span);
            c->lhs = std::move(f);
            c->args = std::move(args);
            return c;
        }
    }
    const auto m = findMethod(agg, callee.text);
    if (!m) {
        err("E0709",
            callee.span,
            std::format("`{}` has no method `{}`", typeToString(agg), callee.text),
            "unknown method");
        for (const ExprPtr& a : e.args) {
            (void)expr(*a, nullptr);
        }
        return nullptr;
    }
    std::vector<Type> params;
    for (const Param& p : m->decl->params) {
        if (!p.is_self) {
            params.push_back(resolvedIn(*m->owner, p.type));
        }
    }
    const Type ret = resolvedIn(*m->owner, m->decl->return_type);
    std::vector<EP> args;
    if (!lowerArgs(e, params, args, callee.text)) {
        return nullptr;
    }

    if (m->kind == MethodRef::Kind::Itable) {
        if (through) { // pointer to an interface value: call through the value
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
            err("E0709",
                recv_ast.span,
                "a method needs an addressable receiver",
                "store the value in a variable first");
            return nullptr;
        }
        EP addr = mk(hir::ExprKind::AddrOf, Type::makePointer(agg), recv->span);
        addr->lhs = std::move(recv);
        self = std::move(addr);
    }
    EP c =
        mk(m->kind == MethodRef::Kind::Virtual ? hir::ExprKind::CallVirtual : hir::ExprKind::Call,
           ret,
           e.span);
    c->text = m->fn_name;
    c->index = m->index;
    c->member = callee.text;
    c->args.push_back(std::move(self));
    for (EP& a : args) {
        c->args.push_back(std::move(a));
    }
    return c;
}

Lowerer::EP Lowerer::callEntry(const Expr& e, const ScopeEntry& entry, const std::string& name) {
    if (entry.kind == ScopeEntry::Kind::Function) {
        Sema& owner = entry.owner != nullptr ? *entry.owner : s_;
        const auto& decl = std::get<FnDecl>(entry.item->decl);
        std::vector<Type> params;
        for (const Param& p : decl.params) {
            params.push_back(resolvedIn(owner, p.type));
        }
        std::vector<EP> args;
        const Type ret = resolvedIn(owner, decl.return_type);
        if (!lowerArgs(e, params, args, name)) {
            return nullptr;
        }
        EP c = mk(hir::ExprKind::Call, ret, e.span);
        c->text = entry.qname.empty() ? entry.name : entry.qname;
        c->args = std::move(args);
        return c;
    }
    if (entry.kind == ScopeEntry::Kind::External) {
        EP c = mk(hir::ExprKind::ExternalCall, unknownType(), e.span);
        c->text = entry.module;
        c->member = entry.member;
        for (const ExprPtr& a : e.args) {
            c->args.push_back(expr(*a, nullptr));
        }
        return c;
    }
    return nullptr;
}

Lowerer::EP Lowerer::callExpr(const Expr& e, const Type* expected) {
    const Expr& callee = *e.lhs;
    (void)expected;
    if (callee.kind == Expr::Kind::Name) {
        const std::string& name = callee.text;
        const ScopeEntry* entry = lookupName(name);
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
                return builtinCall(e, name);
            }
        }
        if (entry != nullptr) {
            if (EP c = callEntry(e, *entry, name)) {
                return c;
            }
            if (entry->kind == ScopeEntry::Kind::Function ||
                entry->kind == ScopeEntry::Kind::External) {
                return nullptr; // diagnosed
            }
        } else if (star_import_) {
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
        if (const ScopeEntry* head = chainEntry(*callee.lhs);
            head != nullptr && head->kind == ScopeEntry::Kind::Namespace) {
            const ScopeEntry* member = namespaceMember(*head, callee.text, callee.span, true);
            if (member == nullptr) {
                return nullptr;
            }
            if (EP c = callEntry(e, *member, callee.text)) {
                return c;
            }
            if (member->kind == ScopeEntry::Kind::Function ||
                member->kind == ScopeEntry::Kind::External) {
                return nullptr;
            }
        }
        if (const ScopeEntry* ent = chainEntry(callee); ent == nullptr ||
                                                        ent->kind == ScopeEntry::Kind::Local ||
                                                        ent->kind == ScopeEntry::Kind::Global) {
            return methodCall(e, *callee.lhs);
        }
    }
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
        err("E0709",
            e.span,
            std::format("`{}` is not callable", typeToString(f->type)),
            "expected a function or function pointer");
        return nullptr;
    }
    std::vector<EP> args;
    const std::string label = f->kind == hir::ExprKind::LocalRef ? f->text : "function pointer";
    if (!lowerArgs(e, f->type.params, args, label)) {
        return nullptr;
    }
    EP c = mk(f->kind == hir::ExprKind::FuncRef ? hir::ExprKind::Call : hir::ExprKind::CallIndirect,
              *f->type.ret,
              e.span);
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
        err("E0708",
            e.rhs->span,
            std::format("index must be an integer, found `{}`", typeToString(idx->type)),
            "integer expected");
        return nullptr;
    }
    if (base->type.kind == TypeKind::Str) {
        // Reads one byte; checked against `len` at run time under --safety=checked. Never a place.
        EP i = mk(hir::ExprKind::Index, Type::makeInt(8, false), e.span);
        i->checked = s_.config_.safety == "checked";
        i->lvalue = false;
        i->lhs = std::move(base);
        i->rhs = std::move(idx);
        return i;
    }
    if (base->type.kind != TypeKind::Array && base->type.kind != TypeKind::Pointer) {
        err("E0709",
            e.span,
            std::format("`{}` cannot be indexed", typeToString(base->type)),
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
        if (const auto len = arrayLength(s_, base->type); len && e.rhs->int_value >= *len) {
            err("E0701",
                e.rhs->span,
                std::format("index {} is out of bounds for `{}` (length {})",
                            e.rhs->int_value,
                            typeToString(base->type),
                            *len),
                "constant index out of range");
            return nullptr;
        }
    }
    EP i = mk(hir::ExprKind::Index, *base->type.elem, e.span);
    i->checked = is_array && s_.config_.safety == "checked";
    i->lvalue = is_array ? base->lvalue : true;
    i->readonly = is_array && base->readonly;
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
            err("E0707",
                e.rhs->span,
                "array length must be a non-negative integer constant",
                "invalid length");
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
            err("E0708",
                e.span,
                "cannot infer the element type of an empty array",
                "write a type: `local a: [T; 0]`");
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
    std::vector<FieldInfo> fields;
    collectFields(agg.path.front(), fields);
    std::set<std::string> seen;
    for (const FieldInit& init : e.fields) {
        std::size_t index = fields.size();
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].decl->name == init.name) {
                index = i;
            }
        }
        if (index == fields.size()) {
            err("E0709",
                init.span,
                std::format("`{}` has no field `{}`", typeToString(agg), init.name),
                "unknown field");
            if (init.value) {
                (void)expr(*init.value, nullptr);
            }
            continue;
        }
        if (!seen.insert(init.name).second) {
            err("E0709",
                init.span,
                std::format("field `{}` is initialized twice", init.name),
                "duplicate field");
            continue;
        }
        const Type ft = resolvedIn(*fields[index].owner, fields[index].decl->type);
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
        err("E0709",
            e.span,
            std::format("`{}` is not a struct or class", typeToString(t)),
            "cannot build it with `{ ... }`");
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
        err("E0709",
            e.span,
            std::format("`new` needs a struct or class, found `{}`", typeToString(t)),
            "not allocatable");
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
        const auto ref = s_.entityByQName(mod_.allocator_global);
        Type gt = unknownType();
        if (ref && std::holds_alternative<GlobalDecl>(ref->sym->item->decl)) {
            gt = resolvedIn(*ref->owner, std::get<GlobalDecl>(ref->sym->item->decl).type);
        }
        EP g = mk(hir::ExprKind::GlobalRef, gt, e.span);
        g->text = mod_.allocator_global;
        g->lvalue = true;
        out->lhs = std::move(g);
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
        return std::ranges::all_of(
            e.args, [&](const hir::ExprPtr& a) { return isConstValue(*a, offender); });
    case hir::ExprKind::ArrayRepeat:
        return e.lhs && isConstValue(*e.lhs, offender);
    case hir::ExprKind::StructLit:
        return std::ranges::all_of(
            e.fields, [&](const hir::FieldInit& f) { return isConstValue(*f.value, offender); });
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

void Lowerer::run() {
    buildModuleScope();
    lowerItems();
    scopes_.pop();
}

hir::Module lowerProgram(const std::vector<Sema*>& modules) {
    LowerShared shared;
    std::vector<std::unique_ptr<Lowerer>> lowerers;
    for (Sema* s : modules) {
        lowerers.push_back(std::make_unique<Lowerer>(*s, shared));
    }
    for (auto& l : lowerers) {
        l->collectImpls();
    }
    for (auto& l : lowerers) {
        l->checkRegistrations();
    }
    for (auto& l : lowerers) {
        l->run();
    }
    return std::move(shared.module);
}

hir::Module Sema::lower() {
    return lowerProgram({this});
}

} // namespace xlang
