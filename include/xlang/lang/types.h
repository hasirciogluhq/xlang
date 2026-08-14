#pragma once

#include "xlang/error.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace xlang {

enum class TypeKind {
    Void,
    Int32,
    Int64,
    BigInt,
    Float,
    Double,
    Bool,
    Char,
    String,
    Struct,
    Interface,
    Pointer,
    Array,
};

struct Type {
    TypeKind kind{TypeKind::Int32};
    std::string struct_name;
    /// Pointee for Pointer; element type for Array. Nested `*T` / `array *T` keep full Type.
    std::shared_ptr<Type> inner;

    [[nodiscard]] bool isVoid() const { return kind == TypeKind::Void; }
    [[nodiscard]] bool isArray() const { return kind == TypeKind::Array; }
    [[nodiscard]] bool isStruct() const { return kind == TypeKind::Struct; }
    [[nodiscard]] bool isPointer() const { return kind == TypeKind::Pointer; }
    [[nodiscard]] bool isInteger() const;
    [[nodiscard]] bool isFloating() const;
    [[nodiscard]] bool isPtrLike() const;
    [[nodiscard]] Type dereferenced() const;

    [[nodiscard]] static Type makeStruct(std::string name);
    [[nodiscard]] static Type makeInterface(std::string name);
    [[nodiscard]] static Type makePointer(Type inner);
    [[nodiscard]] static Type makeArray(Type element);
    [[nodiscard]] Type arrayElementType() const;
    [[nodiscard]] static Type parse(std::string_view name);
};

[[nodiscard]] Type defaultType();
[[nodiscard]] std::string typeToString(const Type& type);
[[nodiscard]] std::string typeMangleComponent(const Type& type);
[[nodiscard]] std::string mangleFunctionName(const std::string& name,
                                             const std::vector<Type>& param_types,
                                             bool variadic = false);
[[nodiscard]] bool typesEqual(const Type& left, const Type& right);
[[nodiscard]] std::string llvmTypeName(const Type& type);
[[nodiscard]] std::string arrayTypeName(const Type& element_type);
[[nodiscard]] std::size_t llvmTypeAlign(const Type& type);

}  // namespace xlang
