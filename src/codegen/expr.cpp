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

std::pair<Type, llvm::Value*> Codegen::emitExpr(const Expr& expr, const LocalMap& locals) {
    switch (expr.kind) {
    case Expr::Kind::IntLiteral:
        return {Type{TypeKind::Int32}, b().constI32(expr.int_value)};
    case Expr::Kind::FloatLiteral:
        return {Type{TypeKind::Double}, b().constF64(expr.float_value)};
    case Expr::Kind::BoolLiteral:
        return {Type{TypeKind::Bool}, b().constI8(expr.bool_value ? 1 : 0)};
    case Expr::Kind::Null:
        return {Type{TypeKind::String}, b().constNullPtr()};
    case Expr::Kind::StringLiteral:
        return {Type{TypeKind::String}, emitStringLiteral(expr.string_value)};
    case Expr::Kind::Variable: {
        const Type var_type = resolveVarType(expr.name, locals);
        return loadValue(var_type, resolveVar(expr.name, locals));
    }
    case Expr::Kind::FunctionRef: {
        const Function* function = findUniqueFunctionByName(*program_, expr.name);
        if (function == nullptr) {
            throw XlangError(std::format("unknown function reference `{}`", expr.name));
        }
        const std::string llvm_name =
            mangleFunctionName(function->name, paramTypes(function->params));
        std::vector<llvm::Type*> params;
        for (const TypedName& p : function->params) {
            params.push_back(llvmType(p.type));
        }
        llvm::Function* fn =
            ensureFn(llvm_name, b().functionType(llvmType(function->return_type), params,
                                                 function->variadic));
        return {Type{TypeKind::Int64}, b().emitPtrToInt(fn, b().i64Ty())};
    }
    case Expr::Kind::FieldAccess: {
        const auto [obj_ty, obj_val] = emitExpr(*expr.object, locals);
        if (obj_ty.kind != TypeKind::Struct) {
            throw XlangError("field access requires struct object");
        }
        const StructDecl* decl = findStruct(obj_ty.struct_name);
        if (decl == nullptr) {
            throw XlangError(std::format("unknown struct `{}`", obj_ty.struct_name));
        }
        const std::size_t index = structFieldIndex(*decl, expr.name);
        const Type field_type = decl->fields[index].type;
        llvm::Value* gep = b().emitStructGEP(structBodyType(decl->name), obj_val,
                                             static_cast<unsigned>(index));
        return loadValue(field_type, gep);
    }
    case Expr::Kind::MethodCall: {
        std::vector<Type> arg_types;
        std::vector<llvm::Value*> arg_values;
        for (const std::unique_ptr<Expr>& arg_expr : expr.args) {
            const auto [ty, val] = emitExpr(*arg_expr, locals);
            arg_types.push_back(ty);
            arg_values.push_back(val);
        }

        if (expr.object->kind == Expr::Kind::Variable &&
            import_aliases_.contains(expr.object->name)) {
            const std::string alias = import_aliases_.at(expr.object->name);
            const std::string fn_name = importPrefixedName(alias, expr.name);
            std::optional<FunctionSignature> resolved = resolveFunctionCall(fn_name, arg_types);
            if (!resolved) {
                resolved = resolveFunctionCall(expr.name, arg_types);
            }
            if (!resolved) {
                throw XlangError(
                    std::format("unknown import call `{}.{}`", alias, expr.name));
            }
            const std::string llvm_name = mangleFunctionName(
                resolved->name, paramTypes(resolved->params), resolved->variadic);
            std::vector<llvm::Type*> param_tys;
            std::vector<llvm::Value*> call_args;
            for (std::size_t i = 0; i < arg_values.size(); ++i) {
                const Type param_ty = resolved->params[i].type;
                param_tys.push_back(llvmType(param_ty));
                call_args.push_back(coerceInt(arg_values[i], arg_types[i], param_ty));
            }
            llvm::Function* fn = ensureFn(
                llvm_name, b().functionType(llvmType(resolved->return_type), param_tys,
                                            resolved->variadic));
            return {resolved->return_type, b().emitCall(fn, call_args)};
        }

        const auto [recv_ty, recv_val] = emitExpr(*expr.object, locals);
        const std::optional<FunctionSignature> resolved =
            resolveMethodCall(expr.name, recv_ty, arg_types);
        if (!resolved) {
            throw XlangError(std::format("no matching method `{}` for `{}`", expr.name,
                                         typeToString(recv_ty)));
        }
        std::vector<llvm::Type*> param_tys;
        std::vector<llvm::Value*> call_args;
        param_tys.push_back(llvmType(resolved->params[0].type));
        call_args.push_back(recv_val);
        for (std::size_t i = 0; i < arg_values.size(); ++i) {
            const Type param_ty = resolved->params[i + 1].type;
            param_tys.push_back(llvmType(param_ty));
            call_args.push_back(coerceInt(arg_values[i], arg_types[i], param_ty));
        }
        const std::string llvm_name = mangleFunctionName(
            resolved->name, paramTypes(resolved->params), resolved->variadic);
        llvm::Function* fn = ensureFn(
            llvm_name, b().functionType(llvmType(resolved->return_type), param_tys,
                                        resolved->variadic));
        return {resolved->return_type, b().emitCall(fn, call_args)};
    }
    case Expr::Kind::New: {
        const StructDecl* decl = findStruct(expr.name);
        if (decl == nullptr) {
            throw XlangError(std::format("unknown struct `{}`", expr.name));
        }
        const std::size_t size = structSizeBytes(*decl);
        llvm::Function* malloc_fn =
            ensureFn("malloc", b().functionType(b().ptrTy(), {b().i64Ty()}));
        llvm::Value* raw =
            b().emitCall(malloc_fn, {b().constI64(static_cast<std::int64_t>(size))});
        const Type struct_type = Type::makeStruct(expr.name);
        for (const FieldInit& init : expr.field_inits) {
            const std::size_t index = structFieldIndex(*decl, init.name);
            const Type field_type = decl->fields[index].type;
            const auto [_, val] = emitExpr(*init.value, locals);
            llvm::Value* gep = b().emitStructGEP(structBodyType(decl->name), raw,
                                                 static_cast<unsigned>(index));
            storeValue(field_type, val, gep);
        }
        return {struct_type, raw};
    }
    case Expr::Kind::NewArray: {
        const std::size_t elem_size = typeSizeBytes(expr.type);
        llvm::Function* fn = b().getFunction("__xlang_array_new");
        if (fn == nullptr) {
            throw XlangError("array runtime not initialized");
        }
        llvm::Value* arr =
            b().emitCall(fn, {b().constI64(static_cast<std::int64_t>(elem_size))});
        return {Type::makeArray(expr.type), arr};
    }
    case Expr::Kind::Cast: {
        const auto [from_ty, val] = emitExpr(*expr.object, locals);
        const Type target = expr.type;
        if (typesEqual(from_ty, target)) {
            return {target, val};
        }
        if (from_ty.kind == TypeKind::Int32 && target.kind == TypeKind::Int64) {
            return {target, b().emitSExt(val, b().i64Ty())};
        }
        if (from_ty.kind == TypeKind::Int64 && target.kind == TypeKind::Int32) {
            return {target, b().emitTrunc(val, b().i32Ty())};
        }
        if ((from_ty.kind == TypeKind::Struct || from_ty.kind == TypeKind::Interface) &&
            (target.kind == TypeKind::Struct || target.kind == TypeKind::Interface)) {
            return {target, b().emitBitCast(val, llvmType(target))};
        }
        if (from_ty.kind == TypeKind::Int64 && target.kind == TypeKind::Struct) {
            return {target, b().emitIntToPtr(val, b().ptrTy())};
        }
        throw XlangError(std::format("unsupported cast from `{}` to `{}`", typeToString(from_ty),
                                     typeToString(target)));
    }
    case Expr::Kind::Index: {
        const auto [arr_ty, arr] = emitExpr(*expr.object, locals);
        if (!arr_ty.isArray()) {
            throw XlangError("index access requires array");
        }
        const auto [_, idx] = emitExpr(*expr.index, locals);
        const Type elem = arr_ty.arrayElementType();
        const std::size_t sz = typeSizeBytes(elem);
        llvm::Value* idx64 = b().emitSExt(idx, b().i64Ty());
        llvm::Value* head =
            b().emitLoad(b().i64Ty(), b().emitStructGEP(array_hdr_ty_, arr, 3));
        llvm::Value* pos = b().emitAdd(head, idx64);
        llvm::Value* data =
            b().emitLoad(b().ptrTy(), b().emitStructGEP(array_hdr_ty_, arr, 0));
        llvm::Value* off = b().emitMul(pos, b().constI64(static_cast<std::int64_t>(sz)));
        llvm::Value* slot = b().emitGEP(b().i8Ty(), data, {off});
        if (elem.kind == TypeKind::Struct) {
            return loadValue(elem, slot);
        }
        return loadValue(elem, slot);
    }
    case Expr::Kind::Binary: {
        const auto [left_ty, left] = emitExpr(*expr.left, locals);
        const auto [right_ty, right] = emitExpr(*expr.right, locals);

        if (expr.bin_op == BinOp::Add && (isStringType(left_ty) || isStringType(right_ty))) {
            llvm::Value* left_str = left;
            llvm::Value* right_str = right;
            if (!isStringType(left_ty)) {
                if (left_ty.kind == TypeKind::Int32 || left_ty.kind == TypeKind::Int64 ||
                    left_ty.kind == TypeKind::Bool || left_ty.kind == TypeKind::Char) {
                    left_str = emitIntToString(left);
                } else {
                    throw XlangError("cannot concatenate string with this type");
                }
            }
            if (!isStringType(right_ty)) {
                if (right_ty.kind == TypeKind::Int32 || right_ty.kind == TypeKind::Int64 ||
                    right_ty.kind == TypeKind::Bool || right_ty.kind == TypeKind::Char) {
                    right_str = emitIntToString(right);
                } else {
                    throw XlangError("cannot concatenate string with this type");
                }
            }
            return {Type{TypeKind::String}, emitStringConcat(left_str, right_str)};
        }

        if (expr.bin_op == BinOp::And || expr.bin_op == BinOp::Or) {
            llvm::Value* left_i1 = boolToI1(left);
            llvm::Value* right_i1 = boolToI1(right);
            llvm::Value* tmp_i1 = expr.bin_op == BinOp::And ? b().emitAnd(left_i1, right_i1)
                                                           : b().emitOr(left_i1, right_i1);
            return {Type{TypeKind::Bool}, b().emitZExt(tmp_i1, b().i8Ty())};
        }

        if (expr.bin_op >= BinOp::Eq && expr.bin_op <= BinOp::Ge) {
            Type cmp_ty = left_ty;
            llvm::Value* left_val = left;
            llvm::Value* right_val = right;
            if (left_ty.isFloating() || right_ty.isFloating()) {
                cmp_ty = Type{TypeKind::Double};
            } else if (left_ty.kind == TypeKind::Int64 || right_ty.kind == TypeKind::Int64) {
                cmp_ty = Type{TypeKind::Int64};
                left_val = coerceInt(left_val, left_ty, cmp_ty);
                right_val = coerceInt(right_val, right_ty, cmp_ty);
            }
            llvm::CmpInst::Predicate pred = llvm::CmpInst::ICMP_EQ;
            switch (expr.bin_op) {
            case BinOp::Eq:
                pred = cmp_ty.isFloating() ? llvm::CmpInst::FCMP_OEQ : llvm::CmpInst::ICMP_EQ;
                break;
            case BinOp::Ne:
                pred = cmp_ty.isFloating() ? llvm::CmpInst::FCMP_ONE : llvm::CmpInst::ICMP_NE;
                break;
            case BinOp::Lt:
                pred = cmp_ty.isFloating() ? llvm::CmpInst::FCMP_OLT : llvm::CmpInst::ICMP_SLT;
                break;
            case BinOp::Le:
                pred = cmp_ty.isFloating() ? llvm::CmpInst::FCMP_OLE : llvm::CmpInst::ICMP_SLE;
                break;
            case BinOp::Gt:
                pred = cmp_ty.isFloating() ? llvm::CmpInst::FCMP_OGT : llvm::CmpInst::ICMP_SGT;
                break;
            case BinOp::Ge:
                pred = cmp_ty.isFloating() ? llvm::CmpInst::FCMP_OGE : llvm::CmpInst::ICMP_SGE;
                break;
            default:
                break;
            }
            llvm::Value* cmp = cmp_ty.isFloating() ? b().emitFCmp(pred, left_val, right_val)
                                                   : b().emitICmp(pred, left_val, right_val);
            return {Type{TypeKind::Bool}, b().emitZExt(cmp, b().i8Ty())};
        }

        Type result_ty = left_ty;
        if (left_ty.isFloating() || right_ty.isFloating()) {
            result_ty = Type{TypeKind::Double};
        }
        llvm::Value* out = nullptr;
        switch (expr.bin_op) {
        case BinOp::Add:
            out = result_ty.isFloating() ? b().emitFAdd(left, right) : b().emitAdd(left, right);
            break;
        case BinOp::Sub:
            out = result_ty.isFloating() ? b().emitFSub(left, right) : b().emitSub(left, right);
            break;
        case BinOp::Mul:
            out = result_ty.isFloating() ? b().emitFMul(left, right) : b().emitMul(left, right);
            break;
        case BinOp::Div:
            out = result_ty.isFloating() ? b().emitFDiv(left, right) : b().emitSDiv(left, right);
            break;
        default:
            out = b().emitAdd(left, right);
            break;
        }
        return {result_ty, out};
    }
    case Expr::Kind::NativeSyscall: {
        if (!expr.left) {
            throw XlangError("@syscall requires a syscall number");
        }
        const auto [nr_ty, nr_val] = emitExpr(*expr.left, locals);
        std::vector<llvm::Value*> args;
        args.reserve(expr.args.size());
        for (const auto& arg : expr.args) {
            const auto [ty, val] = emitExpr(*arg, locals);
            args.push_back(asI64(ty, val));
        }
        return {Type{TypeKind::Int64}, b().emitNativeSyscall(asI64(nr_ty, nr_val), args)};
    }
    case Expr::Kind::Call: {
        if (expr.name == "print") {
            return emitPrintCall(expr.args, locals);
        }
        if (expr.name == "spawn") {
            if (expr.args.size() != 1) {
                throw XlangError("spawn requires exactly one bound call argument");
            }
            llvm::Value* entry = emitSpawnEntry(*expr.args[0], locals);
            llvm::Function* spawn_fn =
                ensureFn("spawn$i64", b().functionType(b().i32Ty(), {b().i64Ty()}));
            return {Type{TypeKind::Int32}, b().emitCall(spawn_fn, {entry})};
        }

        std::vector<Type> arg_types;
        std::vector<llvm::Value*> arg_values;
        for (const auto& arg_expr : expr.args) {
            const auto [ty, val] = emitExpr(*arg_expr, locals);
            arg_types.push_back(ty);
            arg_values.push_back(val);
        }

        if (expr.name == "invoke0" && expr.args.size() == 1) {
            llvm::Value* fn_ptr = b().emitIntToPtr(arg_values[0], b().ptrTy());
            auto* fty = b().functionType(b().i32Ty(), {});
            return {Type{TypeKind::Int32},
                    b().emitCall(llvm::FunctionCallee(fty, fn_ptr), {})};
        }
        if (expr.name == "ref" && expr.args.size() == 1) {
            if (arg_types[0].kind != TypeKind::Struct) {
                throw XlangError("ref requires struct value");
            }
            return {Type{TypeKind::Int64}, b().emitPtrToInt(arg_values[0], b().i64Ty())};
        }
        if ((expr.name == "invoke1" || expr.name == "invoke") && expr.args.size() == 2) {
            auto* fty = b().functionType(b().i32Ty(), {llvmType(arg_types[1])});
            llvm::Value* fn_ptr = b().emitIntToPtr(arg_values[0], b().ptrTy());
            return {Type{TypeKind::Int32},
                    b().emitCall(llvm::FunctionCallee(fty, fn_ptr), {arg_values[1]})};
        }
        if (expr.name == "array_len" && expr.args.size() == 1) {
            llvm::Function* fn = b().getFunction("__xlang_array_len");
            llvm::Value* len64 = b().emitCall(fn, {arg_values[0]});
            return {Type{TypeKind::Int32}, b().emitTrunc(len64, b().i32Ty())};
        }
        if (expr.name == "str_len" && expr.args.size() == 1) {
            return {Type{TypeKind::Int32},
                    b().emitCall(b().getFunction("__xlang_str_len"), {arg_values[0]})};
        }
        if (expr.name == "str_eq" && expr.args.size() == 2) {
            return {Type{TypeKind::Int32}, b().emitCall(b().getFunction("__xlang_str_eq"),
                                                       {arg_values[0], arg_values[1]})};
        }
        if (expr.name == "str_concat" && expr.args.size() == 2) {
            return {Type{TypeKind::String},
                    emitStringConcat(arg_values[0], arg_values[1])};
        }
        if (expr.name == "str_byte" && expr.args.size() == 2) {
            return {Type{TypeKind::Int32}, b().emitCall(b().getFunction("__xlang_str_byte"),
                                                       {arg_values[0], arg_values[1]})};
        }
        if (expr.name == "str_find" && expr.args.size() == 2) {
            return {Type{TypeKind::Int32}, b().emitCall(b().getFunction("__xlang_str_find"),
                                                       {arg_values[0], arg_values[1]})};
        }
        if (expr.name == "str_sub" && expr.args.size() == 3) {
            return {Type{TypeKind::String},
                    b().emitCall(b().getFunction("__xlang_str_sub"),
                                 {arg_values[0], arg_values[1], arg_values[2]})};
        }
        if (expr.name == "str_from_int" && expr.args.size() == 1) {
            return {Type{TypeKind::String}, emitIntToString(arg_values[0])};
        }
        if (expr.name == "array_push" && expr.args.size() == 2) {
            const Type elem = arg_types[0].arrayElementType();
            const std::size_t sz = typeSizeBytes(elem);
            llvm::Value* raw = arg_values[1];
            if (arg_types[1].kind != TypeKind::Struct) {
                llvm::AllocaInst* slot = b().emitAlloca(llvmType(arg_types[1]));
                storeValue(arg_types[1], arg_values[1], slot);
                raw = b().emitBitCast(slot, b().ptrTy());
            }
            b().emitCall(b().getFunction("__xlang_array_push"),
                         {arg_values[0], raw, b().constI64(static_cast<std::int64_t>(sz))});
            return {Type{TypeKind::Int32}, b().constI32(0)};
        }
        if (expr.name == "array_pop_front" && expr.args.size() == 1) {
            const Type elem = arg_types[0].arrayElementType();
            const std::size_t sz = typeSizeBytes(elem);
            llvm::Value* raw = b().emitCall(
                b().getFunction("__xlang_array_pop_front"),
                {arg_values[0], b().constI64(static_cast<std::int64_t>(sz))});
            if (elem.kind == TypeKind::Struct) {
                return {elem, raw};
            }
            return loadValue(elem, raw);
        }
        if (expr.name == "array_get" && expr.args.size() == 2) {
            const Type elem = arg_types[0].arrayElementType();
            const std::size_t sz = typeSizeBytes(elem);
            llvm::Value* idx64 = b().emitSExt(arg_values[1], b().i64Ty());
            llvm::Value* raw = b().emitCall(
                b().getFunction("__xlang_array_get_raw"),
                {arg_values[0], idx64, b().constI64(static_cast<std::int64_t>(sz))});
            if (elem.kind == TypeKind::Struct) {
                return {elem, raw};
            }
            return loadValue(elem, raw);
        }
        if (expr.name == "array_pop" && expr.args.size() == 1) {
            const Type elem = arg_types[0].arrayElementType();
            const std::size_t sz = typeSizeBytes(elem);
            llvm::Value* raw = b().emitCall(
                b().getFunction("__xlang_array_pop_raw"),
                {arg_values[0], b().constI64(static_cast<std::int64_t>(sz))});
            if (elem.kind == TypeKind::Struct) {
                return {elem, raw};
            }
            return loadValue(elem, raw);
        }

        const std::optional<FunctionSignature> resolved =
            resolveFunctionCall(expr.name, arg_types);
        if (!resolved) {
            std::string args_list;
            for (std::size_t i = 0; i < arg_types.size(); ++i) {
                if (i > 0) {
                    args_list += ", ";
                }
                args_list += typeToString(arg_types[i]);
            }
            throw XlangError(
                std::format("no matching overload for `{}({})`", expr.name, args_list));
        }

        std::string llvm_name =
            mangleFunctionName(resolved->name, paramTypes(resolved->params), resolved->variadic);
        if (const Function* definition =
                findFunctionDefinition(*program_, resolved->name, arg_types);
            definition != nullptr && (definition->syscall || definition->external)) {
            // Native-syscall wrappers and bridge/C symbols keep the raw name.
            llvm_name = definition->name;
        } else if (findMatchingFunction(resolved->name, paramTypes(resolved->params),
                                        options_.runtime_syscalls)) {
            // Runtime package bridge declares (plain `declare name`).
            llvm_name = resolved->name;
        }

        std::vector<llvm::Type*> param_tys;
        std::vector<llvm::Value*> call_args;
        for (std::size_t i = 0; i < arg_values.size(); ++i) {
            const Type param_ty = resolved->params[i].type;
            param_tys.push_back(llvmType(param_ty));
            call_args.push_back(coerceInt(arg_values[i], arg_types[i], param_ty));
        }
        llvm::Function* fn = ensureFn(
            llvm_name, b().functionType(llvmType(resolved->return_type), param_tys,
                                        resolved->variadic));
        return {resolved->return_type, b().emitCall(fn, call_args)};
    }
    }
    throw XlangError("invalid expression");
}


}  // namespace xlang
