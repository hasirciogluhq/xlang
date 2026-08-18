#include "xlang/types.h"

#include "xlang/error.h"

namespace xlang {

bool Type::isInteger() const {
    return kind == TypeKind::Int32 || kind == TypeKind::Int64 || kind == TypeKind::BigInt ||
           kind == TypeKind::Bool || kind == TypeKind::Char;
}

bool Type::isFloating() const {
    return kind == TypeKind::Float || kind == TypeKind::Double;
}

bool Type::isPtrLike() const {
    return kind == TypeKind::Pointer || kind == TypeKind::Struct || kind == TypeKind::Interface ||
           kind == TypeKind::String;
}

Type Type::dereferenced() const {
    if (kind != TypeKind::Pointer || !inner) {
        return *this;
    }
    return *inner;
}

Type Type::makeStruct(std::string name) {
    Type type;
    type.kind = TypeKind::Struct;
    type.struct_name = std::move(name);
    return type;
}

Type Type::makeInterface(std::string name) {
    Type type;
    type.kind = TypeKind::Interface;
    type.struct_name = std::move(name);
    return type;
}

Type Type::makePointer(Type pointee) {
    Type type;
    type.kind = TypeKind::Pointer;
    type.inner = std::make_shared<Type>(std::move(pointee));
    return type;
}

Type Type::parse(std::string_view name) {
    if (name == "void") {
        return Type{TypeKind::Void};
    }
    if (name == "int" || name == "int32") {
        return Type{TypeKind::Int32};
    }
    if (name == "int64") {
        return Type{TypeKind::Int64};
    }
    if (name == "bigint") {
        return Type{TypeKind::BigInt};
    }
    if (name == "float" || name == "float32") {
        return Type{TypeKind::Float};
    }
    if (name == "double" || name == "float64") {
        return Type{TypeKind::Double};
    }
    if (name == "bool") {
        return Type{TypeKind::Bool};
    }
    if (name == "char") {
        return Type{TypeKind::Char};
    }
    if (name == "string") {
        return Type{TypeKind::String};
    }
    return makeStruct(std::string(name));
}

Type defaultType() {
    return Type{TypeKind::Int32};
}

std::string typeMangleComponent(const Type& type) {
    switch (type.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::Int32:
        return "i32";
    case TypeKind::Int64:
        return "i64";
    case TypeKind::BigInt:
        return "i128";
    case TypeKind::Float:
        return "f32";
    case TypeKind::Double:
        return "f64";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::Char:
        return "char";
    case TypeKind::String:
        return "str";
    case TypeKind::Struct:
        return "S" + type.struct_name;
    case TypeKind::Interface:
        return "I" + type.struct_name;
    case TypeKind::Pointer:
        return "P" + typeMangleComponent(type.dereferenced());
    }
    return "unknown";
}

std::string
mangleFunctionName(const std::string& name, const std::vector<Type>& param_types, bool variadic) {
    std::string result = name;
    for (const Type& param_type : param_types) {
        result += "$" + typeMangleComponent(param_type);
    }
    if (variadic) {
        result += "$v";
    }
    return result;
}

bool typesEqual(const Type& left, const Type& right) {
    if (left.kind != right.kind) {
        return false;
    }
    if (left.kind == TypeKind::Struct || left.kind == TypeKind::Interface) {
        return left.struct_name == right.struct_name;
    }
    if (left.kind == TypeKind::Pointer) {
        if (!left.inner || !right.inner) {
            return left.inner == right.inner;
        }
        return typesEqual(*left.inner, *right.inner);
    }
    return true;
}

std::string typeToString(const Type& type) {
    switch (type.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::Int32:
        return "int32";
    case TypeKind::Int64:
        return "int64";
    case TypeKind::BigInt:
        return "bigint";
    case TypeKind::Float:
        return "float";
    case TypeKind::Double:
        return "double";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::Char:
        return "char";
    case TypeKind::String:
        return "string";
    case TypeKind::Struct:
        return type.struct_name;
    case TypeKind::Interface:
        return type.struct_name;
    case TypeKind::Pointer:
        return "*" + typeToString(type.dereferenced());
    }
    return "unknown";
}

std::string llvmTypeName(const Type& type) {
    switch (type.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::Int32:
        return "i32";
    case TypeKind::Int64:
        return "i64";
    case TypeKind::BigInt:
        return "i128";
    case TypeKind::Float:
        return "float";
    case TypeKind::Double:
        return "double";
    case TypeKind::Bool:
        return "i8";
    case TypeKind::Char:
        return "i8";
    case TypeKind::String:
        return "i8*";
    case TypeKind::Struct:
        return "%struct." + type.struct_name + "*";
    case TypeKind::Interface:
        return "i8*";
    case TypeKind::Pointer:
        return "ptr";
    }
    throw XlangError("invalid type for LLVM lowering");
}

std::size_t llvmTypeAlign(const Type& type) {
    switch (type.kind) {
    case TypeKind::Void:
        return 1;
    case TypeKind::Int32:
    case TypeKind::Float:
        return 4;
    case TypeKind::Int64:
    case TypeKind::Double:
    case TypeKind::BigInt:
    case TypeKind::String:
    case TypeKind::Struct:
    case TypeKind::Interface:
    case TypeKind::Pointer:
        return 8;
    case TypeKind::Bool:
    case TypeKind::Char:
        return 1;
    }
    return 4;
}

}  // namespace xlang
