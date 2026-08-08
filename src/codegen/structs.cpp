#include "xlang/codegen/codegen.h"

#include "xlang/codegen/detail/helpers.h"
#include "xlang/codegen/target.h"
#include "xlang/error.h"
#include "xlang/types.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <format>
#include <utility>
#include <vector>

using namespace xlang::codegen_detail;

namespace xlang {

void Codegen::emitStructTypes(const Program& program) {
    for (const StructDecl& decl : program.structs) {
        if (structUsesArrayField(decl)) {
            emitArrayHeaderType();
            break;
        }
    }
    if (!array_hdr_type_emitted_) {
        for (const StructDecl& decl : options_.runtime_structs) {
            if (structUsesArrayField(decl)) {
                emitArrayHeaderType();
                break;
            }
        }
    }

    auto ensureOpaque = [&](const StructDecl& decl) {
        if (!struct_types_.contains(decl.name)) {
            struct_types_[decl.name] =
                llvm::StructType::create(b().context(), "struct." + decl.name);
        }
    };
    for (const StructDecl& decl : program.structs) {
        ensureOpaque(decl);
    }
    for (const StructDecl& decl : options_.runtime_structs) {
        ensureOpaque(decl);
    }

    auto setBody = [&](const StructDecl& decl) {
        std::vector<llvm::Type*> fields;
        fields.reserve(decl.fields.size());
        for (const StructField& field : decl.fields) {
            if (field.type.kind == TypeKind::Struct) {
                fields.push_back(b().ptrTy());
            } else {
                fields.push_back(llvmType(field.type));
            }
        }
        llvm::StructType* st = struct_types_[decl.name];
        if (st->isOpaque()) {
            st->setBody(fields);
        }
    };
    for (const StructDecl& decl : program.structs) {
        setBody(decl);
    }
    for (const StructDecl& decl : options_.runtime_structs) {
        setBody(decl);
    }
}

const StructDecl* Codegen::findStruct(const std::string& name) const {
    for (const StructDecl& decl : program_->structs) {
        if (decl.name == name) {
            return &decl;
        }
    }
    for (const StructDecl& decl : options_.runtime_structs) {
        if (decl.name == name) {
            return &decl;
        }
    }
    return nullptr;
}

std::size_t Codegen::structFieldIndex(const StructDecl& decl, const std::string& field) const {
    for (std::size_t i = 0; i < decl.fields.size(); ++i) {
        if (decl.fields[i].name == field) {
            return i;
        }
    }
    throw XlangError(std::format("unknown field `{}` on struct `{}`", field, decl.name));
}

std::size_t Codegen::structSizeBytes(const StructDecl& decl) const {
    std::size_t size = 0;
    std::size_t max_align = 1;
    for (const StructField& field : decl.fields) {
        const std::size_t align = llvmTypeAlign(field.type);
        max_align = std::max(max_align, align);
        size = (size + align - 1) / align * align;
        size += typeSizeBytes(field.type);
    }
    if (size == 0) {
        return 1;
    }
    return (size + max_align - 1) / max_align * max_align;
}


}  // namespace xlang
