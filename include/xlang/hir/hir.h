#pragma once

#include "xlang/lang/ast.h"
#include "xlang/lang/types.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace xlang::hir {

// HIR (plan 4.3): types resolved, implicit conversions explicit (`Cast`), sugar lowered, class
// methods are plain functions `fn(self: *T, ...)`, interface calls are explicit itable calls.
// The tree is typed: every expression carries its `Type`. A type of kind `Named` marks an unknown
// type (a name from a module that is not loaded); codegen never sees it.

using LocalId = std::uint32_t;

struct Local {
    std::string name;
    Type type;
    bool is_param{false};
    Span span;
};

enum class ExprKind {
    IntConst,       ///< int_value (two's complement bits), type Int/Enum
    FloatConst,     ///< float_value
    BoolConst,      ///< bool_value
    StrConst,       ///< text; type `*ui8` (or an enum over `*ui8`), or `str` = { ptr, len }
    Nil,            ///< `nil`, typed as the pointer/function pointer it was converted to
    LocalRef,       ///< local
    GlobalRef,      ///< text = global name
    FuncRef,        ///< text = function name, type FnPtr
    External,       ///< text = module, member = name; unresolved until module loading (4.6)
    Unary,          ///< unary_op (Neg, Not); lhs
    Binary,         ///< binary_op; lhs, rhs already converted to a common type; `checked` for int / %
    Select,         ///< lhs ? rhs : args[0]; only the chosen branch is evaluated
    Cast,           ///< lhs -> type; `implicit` when the compiler inserted it
    Call,           ///< direct call: text = function name, args (methods: args[0] is `self`)
    CallIndirect,   ///< lhs = function pointer value, args
    CallVirtual,    ///< vtable call: index = slot, text = statically resolved function; args[0] is `self`
    CallItable,     ///< lhs = interface value, text = method, index = method index in the interface, args
    ExternalCall,   ///< text = module, member = name, args (unresolved until module loading)
    Syscall,        ///< args[0] = number, rest = arguments; type i64
    Field,          ///< lhs.text; `through_pointer` when lhs is a pointer to the aggregate; index = field index
    Index,          ///< lhs[rhs]; `checked` for arrays under --safety=checked
    Deref,          ///< *lhs
    AddrOf,         ///< &lhs
    ArrayList,      ///< [args...]
    ArrayRepeat,    ///< [lhs; int_value]
    StructLit,      ///< type { fields }
    New,            ///< allocate `target` through `lhs` (allocator value), then init fields; type `*target`;
                    ///< `registered` when lhs is the registered allocator global
    ToInterface,    ///< lhs (a pointer to the object) -> interface `type`; text = impl type name
    AtomicLoad,     ///< lhs = pointer, index = MemoryOrder
    AtomicStore,    ///< lhs = pointer, rhs = value, index = MemoryOrder; void
    AtomicXchg,     ///< lhs = pointer, rhs = value; returns the old value
    AtomicCas,      ///< lhs = pointer, rhs = expected, args[0] = desired; returns the old value
    AtomicAdd,      ///< lhs = pointer, rhs = value; returns the old value
    Fence,          ///< index = MemoryOrder; void
    Asm,            ///< text = template, asm_operands, clobbers, nomem/noreturn; void
};


struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct FieldInit {
    std::string name;
    std::uint32_t index{0};
    ExprPtr value;
};

/// `in(reg) x` / `out("rax") y` / `inout(reg) z` of `__builtin_asm`; `value` is the input value
/// (In) or the place that receives the output (Out, InOut: also read first).
struct AsmOperand {
    enum class Kind { In, Out, InOut } kind{Kind::In};
    std::string reg;  ///< `reg` (any register) or a register name
    ExprPtr value;
};

struct Expr {
    ExprKind kind{ExprKind::IntConst};
    Type type;
    Span span;
    bool lvalue{false};

    std::uint64_t int_value{0};
    double float_value{0.0};
    bool bool_value{false};
    std::string text;
    std::string member;
    LocalId local{0};
    UnaryOp unary_op{UnaryOp::Neg};
    BinaryOp binary_op{BinaryOp::Add};
    ExprPtr lhs;
    ExprPtr rhs;
    std::vector<ExprPtr> args;
    std::vector<FieldInit> fields;
    std::vector<AsmOperand> asm_operands;
    std::vector<std::string> clobbers;
    Type target;  ///< New: the allocated type
    std::uint32_t index{0};
    bool implicit{false};
    bool checked{false};
    bool through_pointer{false};
    bool registered{false};
    bool nomem{false};     ///< Asm: `options("nomem")`
    bool noreturn{false};  ///< Asm: `options("noreturn")`
};

enum class StmtKind { Local, Assign, Return, Expr, If, While, Continue, Break, Block };

struct Stmt {
    StmtKind kind{StmtKind::Expr};
    Span span;
    LocalId local{0};    ///< Local
    ExprPtr target;      ///< Assign
    ExprPtr value;       ///< Local init / Assign value / Return value / Expr / If,While condition
    std::vector<Stmt> body;
    std::vector<Stmt> else_body;
    /// While: statements run after the body on every iteration, including after `continue`
    /// (the step of a `for`). Empty for a plain `while`.
    std::vector<Stmt> step;
    /// While: the condition is tested after the body (`do { } while c`).
    bool post_test{false};
    /// Break/Continue: how many enclosing loops to leave (0 = the innermost one).
    std::uint32_t depth{0};
};

struct Function {
    std::string name;          ///< qualified name; methods: `Type.method`, impl methods: `Type.Iface.method`
    std::string module;        ///< defining module (mangling name)
    std::string symbol;        ///< final link symbol (mangling applied, plan 4.9)
    std::string owner;         ///< class/struct name for methods, empty otherwise
    std::vector<Local> locals; ///< parameters first
    std::uint32_t param_count{0};
    Type ret;
    std::vector<Stmt> body;
    bool has_body{false};
    bool exported{false};
    bool export_c{false};
    bool declared{false};
    std::string import_library;  ///< `@library("name")` on a `declare fn`: the OS library it is imported from
    bool registered{false};
    std::string register_role;
    bool is_virtual{false};
    bool is_override{false};
    Span span;
};

struct Global {
    std::string name;
    std::string module;
    std::string symbol;
    Type type;
    ExprPtr init;  ///< compile-time constant; null = zero-initialized / external
    bool exported{false};
    bool export_c{false};
    bool declared{false};
    bool registered{false};
    std::string register_role;
    Span span;
};

struct Field {
    std::string name;
    Type type;
};

struct VtableSlot {
    std::string method;    ///< method name
    std::string function;  ///< implementing function (qualified HIR name)
};

struct Aggregate {
    std::string name;
    std::vector<VtableSlot> vtable;  ///< class: complete vtable (base slots first); empty = no virtuals
    bool repr_c{false};              ///< `@repr(c)`: Itanium vtable (offset-to-top, typeinfo, functions)
    bool is_class{false};
    std::string base;          ///< class: base class name, empty = none
    std::vector<Field> fields; ///< own fields (base fields come first in layouts)
    std::vector<std::string> methods;  ///< function names of the methods
    bool opaque{false};
    Span span;
};

struct InterfaceMethod {
    std::string name;
    std::vector<Type> params;  ///< without `self`
    Type ret;
};

struct Interface {
    std::string name;
    std::vector<InterfaceMethod> methods;
    Span span;
};

struct Impl {
    std::string interface;  ///< qualified interface name
    std::string type;       ///< implementing struct/class (qualified)
    std::vector<std::string> methods;  ///< function names, in the interface's method order (the itable)
    Span span;
};

struct EnumMemberValue {
    std::string name;
    std::int64_t int_value{0};
    std::string text;  ///< string/float/bool enums: the constant spelled out
};

struct Enum {
    std::string name;
    Type underlying;
    std::vector<EnumMemberValue> members;
};

struct Module {
    std::vector<Function> functions;
    std::vector<Global> globals;
    std::vector<Aggregate> aggregates;
    std::vector<Interface> interfaces;
    std::vector<Impl> impls;
    std::vector<Enum> enums;
    std::string panic_function;     ///< `register(panic)` function, empty = plain trap
    std::string allocator_global;   ///< `register(allocator)` global, empty = none
    std::string start_function;     ///< `register(start)` function, empty = none
};

void dumpModule(std::ostream& out, const Module& module);

}  // namespace xlang::hir
