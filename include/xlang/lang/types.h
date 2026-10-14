#pragma once

#include "xlang/util/source.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xlang {

struct Expr;

enum class TypeKind {
    Void,
    Bool,
    Int,      ///< i8..i64, isize, ui8..ui64, usize (see Type::bits / is_signed)
    Float,    ///< f32, f64
    Pointer,  ///< *T
    FnPtr,    ///< fn(A, B) -> R
    Array,    ///< [T; N]
    Enum,     ///< enum with `elem` as underlying type (null = default i32)
    Struct,
    Class,
    Interface,
    Named,    ///< unresolved reference to a user type or alias; sema resolves it to one of the above
};

/// A type tree. Parsing produces `Void/Bool/Int/Float/Pointer/FnPtr/Array/Named`; `Enum`,
/// `Struct`, `Class` and `Interface` are filled in by sema when `Named` is resolved.
struct Type {
    TypeKind kind{TypeKind::Void};
    /// Int: 8/16/32/64, or 0 for the pointer-width types `isize`/`usize`. Float: 32/64.
    unsigned bits{0};
    bool is_signed{false};
    /// Named/Struct/Class/Interface/Enum: qualified name (`io.Writer` = {"io", "Writer"}).
    std::vector<std::string> path;
    /// Pointer/Array: pointee or element type. Enum: underlying type.
    std::shared_ptr<const Type> elem;
    /// FnPtr: parameter and return types.
    std::vector<Type> params;
    std::shared_ptr<const Type> ret;
    /// Array: compile-time constant length expression.
    std::shared_ptr<const Expr> length;
    Span span;

    [[nodiscard]] bool isVoid() const { return kind == TypeKind::Void; }
    [[nodiscard]] bool isInteger() const { return kind == TypeKind::Int; }
    [[nodiscard]] bool isFloat() const { return kind == TypeKind::Float; }
    [[nodiscard]] bool isPointer() const { return kind == TypeKind::Pointer; }

    [[nodiscard]] static Type makeVoid();
    [[nodiscard]] static Type makeBool();
    [[nodiscard]] static Type makeInt(unsigned bits, bool is_signed);
    [[nodiscard]] static Type makeFloat(unsigned bits);
    [[nodiscard]] static Type makePointer(Type pointee);
    [[nodiscard]] static Type makeFnPtr(std::vector<Type> params, Type ret);
    [[nodiscard]] static Type makeArray(Type element, std::shared_ptr<const Expr> length);
    [[nodiscard]] static Type makeNamed(std::vector<std::string> path);
};

/// `i32`, `ui8`, `usize`, `f64`, `bool`, `void` -> the type; nullopt for any other name.
[[nodiscard]] std::optional<Type> primitiveFromName(std::string_view name);
[[nodiscard]] bool isPrimitiveTypeName(std::string_view name);

/// Source-like spelling: `*ui8`, `[i32; 4]`, `fn(i32) -> i32`, `io.Writer`.
[[nodiscard]] std::string typeToString(const Type& type);

}  // namespace xlang
