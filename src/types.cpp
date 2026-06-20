#include "xlang/types.h"

namespace xlang {

bool Type::isInteger() const {
    return kind == TypeKind::Int32 || kind == TypeKind::Int64 || kind == TypeKind::BigInt ||
           kind == TypeKind::Bool || kind == TypeKind::Char;
}

bool Type::isFloating() const {
    return kind == TypeKind::Float || kind == TypeKind::Double;
}

Type Type::dereferenced() const {
    if (kind != TypeKind::Pointer) {
        return *this;
    }
    Type inner;
    inner.kind = pointer_to;
    inner.struct_name = pointer_struct_name;
    return inner;
}

Type Type::makeStruct(std::string name) {
    Type type;
    type.kind = TypeKind::Struct;
    type.struct_name = std::move(name);
    return type;
}

Type Type::makePointer(Type inner) {
    Type type;
    type.kind = TypeKind::Pointer;
    type.pointer_to = inner.kind;
    type.pointer_struct_name = inner.struct_name;
    return type;
}

Type Type::parse(const std::string& name) {
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
    return makeStruct(name);
}

Type defaultType() {
    return Type{TypeKind::Int32};
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
        case TypeKind::Pointer: {
            Type inner;
            inner.kind = type.pointer_to;
            inner.struct_name = type.pointer_struct_name;
            return typeToString(inner) + "*";
        }
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
        case TypeKind::Pointer: {
            Type inner;
            inner.kind = type.pointer_to;
            inner.struct_name = type.pointer_struct_name;
            if (inner.kind == TypeKind::Struct) {
                return "%struct." + inner.struct_name + "*";
            }
            return llvmTypeName(inner) + "*";
        }
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
        case TypeKind::Pointer:
            return 8;
        case TypeKind::Bool:
        case TypeKind::Char:
            return 1;
    }
    return 4;
}

}  // namespace xlang
