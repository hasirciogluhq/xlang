#include "xlang/codegen.h"

#include "xlang/error.h"
#include "xlang/syscalls.h"

#include <sstream>
#include <unordered_map>

namespace xlang {

namespace {

std::vector<Type> paramTypes(const std::vector<TypedName>& params) {
    std::vector<Type> types;
    types.reserve(params.size());
    for (const TypedName& param : params) {
        types.push_back(param.type);
    }
    return types;
}

bool paramTypesMatch(const std::vector<Type>& expected, const std::vector<Type>& actual) {
    if (expected.size() != actual.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!typesEqual(expected[i], actual[i])) {
            return false;
        }
    }
    return true;
}

std::optional<FunctionSignature> findMatchingFunction(
    const std::string& name, const std::vector<Type>& arg_types,
    const std::vector<FunctionSignature>& candidates) {
    const FunctionSignature* match = nullptr;
    for (const FunctionSignature& candidate : candidates) {
        if (candidate.name != name) {
            continue;
        }
        const std::vector<Type> expected = paramTypes(candidate.params);
        if (!paramTypesMatch(expected, arg_types)) {
            continue;
        }
        if (match != nullptr) {
            throw XlangError("ambiguous call to `" + name + "`");
        }
        match = &candidate;
    }
    if (match == nullptr) {
        return std::nullopt;
    }
    return *match;
}

std::vector<FunctionSignature> collectDefinedFunctions(const Program& program) {
    std::vector<FunctionSignature> functions;
    for (const Function& function : program.functions) {
        if (function.syscall) {
            FunctionSignature signature;
            signature.name = function.name;
            signature.params = function.params;
            signature.return_type = function.return_type;
            functions.push_back(std::move(signature));
            continue;
        }
        if (function.body.statements.empty() && !function.external) {
            continue;
        }
        FunctionSignature signature;
        signature.name = function.name;
        signature.params = function.params;
        signature.return_type = function.return_type;
        functions.push_back(std::move(signature));
    }
    return functions;
}

const Function* findFunctionDefinition(const Program& program, const std::string& name,
                                       const std::vector<Type>& param_types) {
    for (const Function& function : program.functions) {
        if (function.name != name) {
            continue;
        }
        if (!paramTypesMatch(paramTypes(function.params), param_types)) {
            continue;
        }
        return &function;
    }
    return nullptr;
}

const Function* findUniqueFunctionByName(const Program& program, const std::string& name) {
    const Function* match = nullptr;
    for (const Function& function : program.functions) {
        if (function.name != name || function.syscall) {
            continue;
        }
        if (function.body.statements.empty() && !function.external) {
            continue;
        }
        if (match != nullptr) {
            throw XlangError("ambiguous function reference `" + name + "`");
        }
        match = &function;
    }
    return match;
}

std::string functionPointerType(const Function& function) {
    std::ostringstream type;
    type << llvmTypeName(function.return_type) << " (";
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i > 0) {
            type << ", ";
        }
        type << llvmTypeName(function.params[i].type);
    }
    type << ")*";
    return type.str();
}

}  // namespace

bool exprUsesString(const Expr& expr) {
    if (expr.kind == Expr::Kind::StringLiteral) {
        return true;
    }
    if (expr.kind == Expr::Kind::Binary && expr.bin_op == BinOp::Add) {
        if (expr.left && exprUsesString(*expr.left)) {
            return true;
        }
        if (expr.right && exprUsesString(*expr.right)) {
            return true;
        }
    }
    if (expr.object && exprUsesString(*expr.object)) {
        return true;
    }
    if (expr.left && exprUsesString(*expr.left)) {
        return true;
    }
    if (expr.right && exprUsesString(*expr.right)) {
        return true;
    }
    for (const auto& arg : expr.args) {
        if (exprUsesString(*arg)) {
            return true;
        }
    }
    for (const FieldInit& init : expr.field_inits) {
        if (init.value && exprUsesString(*init.value)) {
            return true;
        }
    }
    return false;
}

bool stmtUsesString(const Stmt& stmt) {
    if (stmt.expr && exprUsesString(*stmt.expr)) {
        return true;
    }
    if (stmt.return_value && exprUsesString(*stmt.return_value)) {
        return true;
    }
    if (stmt.target && exprUsesString(*stmt.target)) {
        return true;
    }
    return false;
}

bool programUsesStrings(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.init && exprUsesString(*global.init)) {
            return true;
        }
    }
    for (const Function& function : program.functions) {
        for (const Stmt& stmt : function.body.statements) {
            if (stmtUsesString(stmt)) {
                return true;
            }
        }
    }
    return false;
}

bool exprUsesHeap(const Expr& expr) {
    if (expr.kind == Expr::Kind::New) {
        return true;
    }
    if (expr.kind == Expr::Kind::FieldAccess && expr.object) {
        return exprUsesHeap(*expr.object);
    }
    if (expr.left && exprUsesHeap(*expr.left)) {
        return true;
    }
    if (expr.right && exprUsesHeap(*expr.right)) {
        return true;
    }
    for (const auto& arg : expr.args) {
        if (exprUsesHeap(*arg)) {
            return true;
        }
    }
    for (const FieldInit& init : expr.field_inits) {
        if (init.value && exprUsesHeap(*init.value)) {
            return true;
        }
    }
    return false;
}

bool stmtUsesHeap(const Stmt& stmt) {
    if (stmt.kind == Stmt::Kind::Delete) {
        return true;
    }
    if (stmt.expr && exprUsesHeap(*stmt.expr)) {
        return true;
    }
    if (stmt.return_value && exprUsesHeap(*stmt.return_value)) {
        return true;
    }
    if (stmt.target && exprUsesHeap(*stmt.target)) {
        return true;
    }
    return false;
}

bool programUsesHeap(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.init && exprUsesHeap(*global.init)) {
            return true;
        }
    }
    for (const Function& function : program.functions) {
        for (const Stmt& stmt : function.body.statements) {
            if (stmtUsesHeap(stmt)) {
                return true;
            }
        }
    }
    return false;
}

Codegen::Codegen(CodegenOptions options) : options_(std::move(options)) {}

CodegenResult Codegen::generate(const Program& program, const CodegenOptions& options) {
    Codegen cg(options);
    cg.program_ = &program;
    cg.needs_heap_ = programUsesHeap(program) || programUsesStrings(program);
    cg.needs_strings_ = programUsesStrings(program);
    cg.collectSyscalls(program);
    cg.emitPrelude(program);
    cg.emitStructTypes(program);
    if (cg.needs_strings_) {
        cg.emitStringRuntimeSupport();
        cg.preemitStringLiterals(program);
    }
    cg.emitGlobals(program);
    cg.emitGlobalInit(program);
    for (const Function& function : program.functions) {
        if (function.syscall) {
            continue;
        }
        if (function.external) {
            cg.emitDeclareFunction(function);
        } else if (!function.body.statements.empty()) {
            const std::vector<Type> types = paramTypes(function.params);
            cg.defined_functions_.insert(mangleFunctionName(function.name, types));
            cg.emitFunction(function);
        }
    }
    cg.emitSyscallLowering();

    CodegenResult result;
    result.ir = std::move(cg.output_);
    result.syscalls = std::move(cg.syscalls_);
    result.needs_thread_link = syscallsNeedThreadLink(result.syscalls);
    return result;
}

void Codegen::collectSyscalls(const Program& program) {
    for (const Function& function : program.functions) {
        if (function.syscall) {
            syscalls_.insert(function.name);
        }
    }
}

void Codegen::emitSyscallLowering() {
    if (syscalls_.empty()) {
        return;
    }
    emitSyscallDefinitions(output_, syscalls_);
}

void Codegen::emitPrelude(const Program& program) {
    writeln("; generated by xlank");
    writeln("target triple = \"native\"");
    writeln("");

    if (needs_heap_) {
        writeln("declare i8* @malloc(i64)");
        writeln("declare void @free(i8*)");
        writeln("");
    }

    for (const Function& function : program.functions) {
        if (function.external) {
            const std::vector<Type> types = paramTypes(function.params);
            defined_functions_.insert(mangleFunctionName(function.name, types));
        }
    }

    emitRuntimeDeclares(program);
}

void Codegen::emitStringRuntimeSupport() {
    writeln("declare i32 @sprintf(i8*, i8*, ...)");
    writeln("declare i64 @strlen(i8*)");
    writeln("declare i8* @strcpy(i8*, i8*)");
    writeln("declare i8* @strcat(i8*, i8*)");
    writeln("@__xlang_int_fmt = private unnamed_addr constant [3 x i8] c\"%d\\00\"");
    writeln("");
    writeln("define internal i8* @__xlang_int_to_str(i32 %n) {");
    writeln("  %buf = call i8* @malloc(i64 32)");
    writeln("  call i32 (i8*, i8*, ...) @sprintf(i8* %buf, i8* getelementptr inbounds "
            "([3 x i8], [3 x i8]* @__xlang_int_fmt, i32 0, i32 0), i32 %n)");
    writeln("  ret i8* %buf");
    writeln("}");
    writeln("");
    writeln("define internal i8* @__xlang_str_concat(i8* %a, i8* %b) {");
    writeln("  %la = call i64 @strlen(i8* %a)");
    writeln("  %lb = call i64 @strlen(i8* %b)");
    writeln("  %sum = add i64 %la, %lb");
    writeln("  %size = add i64 %sum, 1");
    writeln("  %buf = call i8* @malloc(i64 %size)");
    writeln("  call i8* @strcpy(i8* %buf, i8* %a)");
    writeln("  call i8* @strcat(i8* %buf, i8* %b)");
    writeln("  ret i8* %buf");
    writeln("}");
    writeln("");
}

bool Codegen::isStringType(const Type& type) const {
    return type.kind == TypeKind::String;
}

std::string Codegen::stringLiteralGlobalName(const std::string& text) const {
    const auto it = string_literal_globals_.find(text);
    if (it == string_literal_globals_.end()) {
        throw XlangError("missing string literal global");
    }
    return it->second;
}

void Codegen::ensureStringLiteralGlobal(const std::string& text) {
    if (string_literal_globals_.find(text) != string_literal_globals_.end()) {
        return;
    }

    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '\\': escaped += "\\5C"; break;
            case '"': escaped += "\\22"; break;
            case '\n': escaped += "\\0A"; break;
            case '\t': escaped += "\\09"; break;
            default: escaped += c; break;
        }
    }

    const std::string name = "@__xlang_str." + std::to_string(string_literal_counter_++);
    const std::size_t size = text.size() + 1;
    writeln(name + " = private unnamed_addr constant [" + std::to_string(size) + " x i8] c\"" +
            escaped + "\\00\"");
    string_literal_globals_.emplace(text, name);
}

std::string Codegen::emitStringLiteral(const std::string& text) {
    ensureStringLiteralGlobal(text);
    const std::string name = stringLiteralGlobalName(text);
    const std::size_t size = text.size() + 1;
    const std::string tmp = freshTmp();
    writeln("  " + tmp + " = getelementptr inbounds [" + std::to_string(size) + " x i8], [" +
            std::to_string(size) + " x i8]* " + name + ", i32 0, i32 0");
    return tmp;
}

void Codegen::collectStringLiteralsFromExpr(const Expr& expr) {
    if (expr.kind == Expr::Kind::StringLiteral) {
        ensureStringLiteralGlobal(expr.name);
        return;
    }
    if (expr.object) {
        collectStringLiteralsFromExpr(*expr.object);
    }
    if (expr.left) {
        collectStringLiteralsFromExpr(*expr.left);
    }
    if (expr.right) {
        collectStringLiteralsFromExpr(*expr.right);
    }
    for (const auto& arg : expr.args) {
        collectStringLiteralsFromExpr(*arg);
    }
    for (const FieldInit& init : expr.field_inits) {
        if (init.value) {
            collectStringLiteralsFromExpr(*init.value);
        }
    }
}

void Codegen::collectStringLiteralsFromStmt(const Stmt& stmt) {
    if (stmt.expr) {
        collectStringLiteralsFromExpr(*stmt.expr);
    }
    if (stmt.return_value) {
        collectStringLiteralsFromExpr(*stmt.return_value);
    }
    if (stmt.target) {
        collectStringLiteralsFromExpr(*stmt.target);
    }
}

void Codegen::preemitStringLiterals(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.init) {
            collectStringLiteralsFromExpr(*global.init);
        }
    }
    for (const Function& function : program.functions) {
        for (const Stmt& stmt : function.body.statements) {
            collectStringLiteralsFromStmt(stmt);
        }
    }
    if (!string_literal_globals_.empty()) {
        writeln("");
    }
}

std::string Codegen::emitIntToString(const std::string& int_value) {
    const std::string tmp = freshTmp();
    writeln("  " + tmp + " = call i8* @__xlang_int_to_str(i32 " + int_value + ")");
    return tmp;
}

std::string Codegen::emitStringConcat(const std::string& left, const std::string& right) {
    const std::string tmp = freshTmp();
    writeln("  " + tmp + " = call i8* @__xlang_str_concat(i8* " + left + ", i8* " + right + ")");
    return tmp;
}

void Codegen::emitStructTypes(const Program& program) {
    for (const StructDecl& decl : program.structs) {
        std::ostringstream body;
        for (std::size_t i = 0; i < decl.fields.size(); ++i) {
            if (i > 0) {
                body << ", ";
            }
            Type field_type = decl.fields[i].type;
            if (field_type.kind == TypeKind::Struct) {
                body << structValueTypeName(field_type.struct_name);
            } else {
                body << llvmTypeName(field_type);
            }
        }
        if (decl.fields.empty()) {
            writeln(structValueTypeName(decl.name) + " = type { }");
        } else {
            writeln(structValueTypeName(decl.name) + " = type { " + body.str() + " }");
        }
    }
    if (!program.structs.empty()) {
        writeln("");
    }
}

void Codegen::emitRuntimeDeclares(const Program& program) {
    if (options_.runtime_exports.empty()) {
        return;
    }

    for (const FunctionSignature& runtime_fn : options_.runtime_exports) {
        if (definesFunction(program, runtime_fn.name, paramTypes(runtime_fn.params))) {
            continue;
        }
        emitDeclareFunction(runtime_fn);
    }
}

bool Codegen::definesFunction(const Program& program, const std::string& name,
                              const std::vector<Type>& param_types) const {
    const std::string mangled = mangleFunctionName(name, param_types);
    if (defined_functions_.find(mangled) != defined_functions_.end()) {
        return true;
    }
    for (const Function& function : program.functions) {
        if (function.name != name || function.body.statements.empty()) {
            continue;
        }
        if (paramTypesMatch(paramTypes(function.params), param_types)) {
            return true;
        }
    }
    return false;
}

std::optional<FunctionSignature> Codegen::resolveFunctionCall(
    const std::string& name, const std::vector<Type>& arg_types) const {
    const std::vector<FunctionSignature> defined = collectDefinedFunctions(*program_);
    if (const std::optional<FunctionSignature> match = findMatchingFunction(name, arg_types, defined)) {
        return match;
    }
    if (const std::optional<FunctionSignature> match =
            findMatchingFunction(name, arg_types, options_.runtime_exports)) {
        return match;
    }
    return std::nullopt;
}

const StructDecl* Codegen::findStruct(const std::string& name) const {
    for (const StructDecl& decl : program_->structs) {
        if (decl.name == name) {
            return &decl;
        }
    }
    return nullptr;
}

std::string Codegen::structTypeName(const std::string& name) const {
    return "%struct." + name + "*";
}

std::string Codegen::structValueTypeName(const std::string& name) const {
    return "%struct." + name;
}

int Codegen::structFieldIndex(const StructDecl& decl, const std::string& field) const {
    for (std::size_t i = 0; i < decl.fields.size(); ++i) {
        if (decl.fields[i].name == field) {
            return static_cast<int>(i);
        }
    }
    throw XlangError("unknown field `" + field + "` on struct `" + decl.name + "`");
}

std::size_t Codegen::structSizeBytes(const StructDecl& decl) const {
    std::size_t size = 0;
    for (const StructField& field : decl.fields) {
        size += llvmTypeAlign(field.type);
    }
    return size == 0 ? 1 : size;
}

void Codegen::emitGlobals(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        globals_.insert(global.name);
        global_types_[global.name] = global.type;

        if (global.external) {
            writeln(globalName(global.name) + " = external global " +
                    llvmTypeName(global.type));
            continue;
        }

        const std::string linkage = globalLinkage(global);
        const std::string llvm_ty = llvmTypeName(global.type);
        if (global.init && global.init->kind == Expr::Kind::IntLiteral &&
            global.type.kind == TypeKind::Int32) {
            writeln(globalName(global.name) + " = " + linkage + "global " + llvm_ty + " " +
                    std::to_string(global.init->int_value));
        } else if (global.init && global.init->kind == Expr::Kind::FloatLiteral &&
                   global.type.isFloating()) {
            writeln(globalName(global.name) + " = " + linkage + "global " + llvm_ty + " " +
                    std::to_string(global.init->float_value));
        } else if (global.init && global.init->kind == Expr::Kind::BoolLiteral &&
                   global.type.kind == TypeKind::Bool) {
            writeln(globalName(global.name) + " = " + linkage + "global " + llvm_ty + " " +
                    std::to_string(global.init->bool_value ? 1 : 0));
        } else if (global.init && global.init->kind == Expr::Kind::Null &&
                   (global.type.isPointer() || global.type.kind == TypeKind::String ||
                    global.type.kind == TypeKind::Struct)) {
            writeln(globalName(global.name) + " = " + linkage + "global " + llvm_ty + " null");
        } else {
            writeln(globalName(global.name) + " = " + linkage + "global " + llvm_ty + " zeroinitializer");
        }
    }
    if (!program.globals.empty()) {
        writeln("");
    }
}

void Codegen::emitGlobalInit(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.external) {
            continue;
        }
        if (!global.init) {
            continue;
        }
        if (global.init->kind == Expr::Kind::IntLiteral && global.type.kind == TypeKind::Int32) {
            continue;
        }
        if (global.init->kind == Expr::Kind::FloatLiteral && global.type.isFloating()) {
            continue;
        }
        if (global.init->kind == Expr::Kind::BoolLiteral && global.type.kind == TypeKind::Bool) {
            continue;
        }
        if (global.init->kind == Expr::Kind::Null) {
            continue;
        }
        needs_global_init_ = true;
        break;
    }
    if (!needs_global_init_) {
        return;
    }

    const std::string linkage = options_.build_kind == BuildKind::Lib ? "internal " : "";
    writeln("define " + linkage + "void @__xlang_init_globals() {");
    std::unordered_map<std::string, std::string> locals;
    for (const GlobalVar& global : program.globals) {
        if (global.external || !global.init) {
            continue;
        }
        if (global.init->kind == Expr::Kind::IntLiteral && global.type.kind == TypeKind::Int32) {
            continue;
        }
        if (global.init->kind == Expr::Kind::FloatLiteral && global.type.isFloating()) {
            continue;
        }
        if (global.init->kind == Expr::Kind::BoolLiteral && global.type.kind == TypeKind::Bool) {
            continue;
        }
        if (global.init->kind == Expr::Kind::Null) {
            continue;
        }
        const auto [ty, val] = emitExpr(*global.init, locals);
        storeValue(ty, val, globalName(global.name), locals);
    }
    writeln("  ret void");
    writeln("}");
    writeln("");
}

void Codegen::emitDeclareFunction(const FunctionSignature& function) {
    const std::vector<Type> param_type_list = paramTypes(function.params);
    const std::string llvm_name = mangleFunctionName(function.name, param_type_list);
    std::ostringstream params;
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i > 0) {
            params << ", ";
        }
        params << llvmTypeName(function.params[i].type);
    }
    writeln("declare " + llvmTypeName(function.return_type) + " @" + llvm_name + "(" +
            params.str() + ")");
    writeln("");
}

void Codegen::emitDeclareFunction(const Function& function) {
    const std::vector<Type> param_type_list = paramTypes(function.params);
    const std::string llvm_name = mangleFunctionName(function.name, param_type_list);
    std::ostringstream params;
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i > 0) {
            params << ", ";
        }
        params << llvmTypeName(function.params[i].type);
    }
    writeln("declare " + llvmTypeName(function.return_type) + " @" + llvm_name + "(" +
            params.str() + ")");
    writeln("");
}

void Codegen::emitFunction(const Function& function) {
    const std::vector<Type> param_type_list = paramTypes(function.params);
    const std::string llvm_name = mangleFunctionName(function.name, param_type_list);
    std::ostringstream params;
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i > 0) {
            params << ", ";
        }
        params << llvmTypeName(function.params[i].type) << " %" << function.params[i].name;
    }

    writeln("define " + fnLinkage(function) + llvmTypeName(function.return_type) + " @" +
            llvm_name + "(" + params.str() + ") {");

    std::unordered_map<std::string, std::string> locals;
    local_types_.clear();
    current_return_type_ = function.return_type;
    for (const TypedName& param : function.params) {
        allocLocal(param.name, param.type, locals);
        const std::string ptr = localPtr(param.name);
        const std::string llvm_ty = llvmTypeName(param.type);
        writeln("  store " + llvm_ty + " %" + param.name + ", " + llvm_ty + "* " + ptr +
                ", align " + std::to_string(llvmTypeAlign(param.type)));
    }

    if (function.name == "main" && needs_global_init_) {
        writeln("  call void @__xlang_init_globals()");
    }

    bool has_return = false;
    for (const Stmt& stmt : function.body.statements) {
        if (emitStatement(stmt, locals)) {
            has_return = true;
            break;
        }
    }

    if (!has_return) {
        if (function.return_type.kind == TypeKind::Void) {
            writeln("  ret void");
        } else {
            writeln("  ret " + llvmTypeName(function.return_type) + " zeroinitializer");
        }
    }

    writeln("}");
    writeln("");
}

std::string Codegen::fnLinkage(const Function& function) const {
    if (function.external) {
        return "";
    }
    if (!function.exported && options_.build_kind == BuildKind::Lib) {
        return "internal ";
    }
    return "";
}

std::string Codegen::globalLinkage(const GlobalVar& global) const {
    if (global.external) {
        return "";
    }
    if (!global.exported && options_.build_kind == BuildKind::Lib) {
        return "internal ";
    }
    return "";
}

void Codegen::allocLocal(const std::string& name, const Type& type,
                         std::unordered_map<std::string, std::string>& locals) {
    const std::string ptr = localPtr(name);
    const std::string llvm_ty = llvmTypeName(type);
    if (type.kind == TypeKind::Struct) {
        writeln("  " + ptr + " = alloca " + structTypeName(type.struct_name) + ", align 8");
    } else {
        writeln("  " + ptr + " = alloca " + llvm_ty + ", align " +
                std::to_string(llvmTypeAlign(type)));
    }
    locals[name] = ptr;
    local_types_[name] = type;
}

void Codegen::storeValue(const Type& type, const std::string& value, const std::string& ptr,
                         const std::unordered_map<std::string, std::string>& locals) {
    (void)locals;
    const std::string llvm_ty = llvmTypeName(type);
    if (type.kind == TypeKind::Struct) {
        writeln("  store " + structTypeName(type.struct_name) + " " + value + ", " +
                structTypeName(type.struct_name) + "* " + ptr + ", align 8");
        return;
    }
    writeln("  store " + llvm_ty + " " + value + ", " + llvm_ty + "* " + ptr + ", align " +
            std::to_string(llvmTypeAlign(type)));
}

std::pair<Type, std::string> Codegen::loadValue(
    const Type& type, const std::string& ptr,
    const std::unordered_map<std::string, std::string>& locals) {
    (void)locals;
    const std::string tmp = freshTmp();
    if (type.kind == TypeKind::Struct) {
        writeln("  " + tmp + " = load " + structTypeName(type.struct_name) + ", " +
                structTypeName(type.struct_name) + "* " + ptr + ", align 8");
        return {type, tmp};
    }
    const std::string llvm_ty = llvmTypeName(type);
    writeln("  " + tmp + " = load " + llvm_ty + ", " + llvm_ty + "* " + ptr + ", align " +
            std::to_string(llvmTypeAlign(type)));
    return {type, tmp};
}

bool Codegen::emitStatement(const Stmt& stmt, std::unordered_map<std::string, std::string>& locals) {
    switch (stmt.kind) {
        case Stmt::Kind::Local: {
            const auto [ty, val] = emitExpr(*stmt.expr, locals);
            allocLocal(stmt.name, stmt.type, locals);
            storeValue(stmt.type, val, localPtr(stmt.name), locals);
            (void)ty;
            return false;
        }
        case Stmt::Kind::Assign: {
            const Type var_type = resolveVarType(stmt.name, locals);
            const auto [ty, val] = emitExpr(*stmt.expr, locals);
            (void)ty;
            storeValue(var_type, val, resolveVar(stmt.name, locals), locals);
            return false;
        }
        case Stmt::Kind::MemberAssign: {
            const auto [obj_ty, obj_ptr] = emitExpr(*stmt.target, locals);
            if (obj_ty.kind != TypeKind::Struct) {
                throw XlangError("field assignment requires struct object");
            }
            const StructDecl* decl = findStruct(obj_ty.struct_name);
            if (decl == nullptr) {
                throw XlangError("unknown struct `" + obj_ty.struct_name + "`");
            }
            const int index = structFieldIndex(*decl, stmt.field);
            const Type field_type = decl->fields[static_cast<std::size_t>(index)].type;
            const auto [_, val] = emitExpr(*stmt.expr, locals);
            const std::string tmp = freshTmp();
            writeln("  " + tmp + " = getelementptr " + structValueTypeName(decl->name) + ", " +
                    structTypeName(decl->name) + " " + obj_ptr + ", i32 0, i32 " +
                    std::to_string(index));
            storeValue(field_type, val, tmp, locals);
            return false;
        }
        case Stmt::Kind::Return: {
            if (stmt.return_value) {
                const auto [ty, val] = emitExpr(*stmt.return_value, locals);
                writeln("  ret " + llvmTypeName(ty) + " " + val);
            } else if (current_return_type_.kind == TypeKind::Void) {
                writeln("  ret void");
            } else {
                writeln("  ret " + llvmTypeName(current_return_type_) + " zeroinitializer");
            }
            return true;
        }
        case Stmt::Kind::Delete: {
            const auto [ty, val] = emitExpr(*stmt.expr, locals);
            if (ty.kind != TypeKind::Struct && !(ty.isPointer())) {
                throw XlangError("delete expects pointer or struct handle");
            }
            std::string ptr = val;
            if (ty.kind == TypeKind::Struct) {
                ptr = val;
            }
            const std::string casted = freshTmp();
            writeln("  " + casted + " = bitcast " + llvmTypeName(ty) + " " + ptr + " to i8*");
            writeln("  call void @free(i8* " + casted + ")");
            return false;
        }
        case Stmt::Kind::Expr: {
            const auto [_, val] = emitExpr(*stmt.expr, locals);
            writeln("  ; discard " + val);
            return false;
        }
    }
    return false;
}

std::pair<Type, std::string> Codegen::emitExpr(
    const Expr& expr, const std::unordered_map<std::string, std::string>& locals) {
    switch (expr.kind) {
        case Expr::Kind::IntLiteral:
            return {Type{TypeKind::Int32}, std::to_string(expr.int_value)};
        case Expr::Kind::FloatLiteral:
            return {Type{TypeKind::Double}, std::to_string(expr.float_value)};
        case Expr::Kind::BoolLiteral:
            return {Type{TypeKind::Bool}, expr.bool_value ? "1" : "0"};
        case Expr::Kind::Null:
            return {Type{TypeKind::String}, "null"};
        case Expr::Kind::StringLiteral: {
            const std::string ptr = emitStringLiteral(expr.name);
            return {Type{TypeKind::String}, ptr};
        }
        case Expr::Kind::Variable: {
            const Type var_type = resolveVarType(expr.name, locals);
            return loadValue(var_type, resolveVar(expr.name, locals), locals);
        }
        case Expr::Kind::FunctionRef: {
            const Function* function = findUniqueFunctionByName(*program_, expr.name);
            if (function == nullptr) {
                throw XlangError("unknown function reference `" + expr.name + "`");
            }
            const std::string llvm_name =
                mangleFunctionName(function->name, paramTypes(function->params));
            const std::string tmp = freshTmp();
            writeln("  " + tmp + " = ptrtoint " + functionPointerType(*function) + " @" +
                    llvm_name + " to i64");
            return {Type{TypeKind::Int64}, tmp};
        }
        case Expr::Kind::FieldAccess: {
            const auto [obj_ty, obj_val] = emitExpr(*expr.object, locals);
            if (obj_ty.kind != TypeKind::Struct) {
                throw XlangError("field access requires struct object");
            }
            const StructDecl* decl = findStruct(obj_ty.struct_name);
            if (decl == nullptr) {
                throw XlangError("unknown struct `" + obj_ty.struct_name + "`");
            }
            const int index = structFieldIndex(*decl, expr.name);
            const Type field_type = decl->fields[static_cast<std::size_t>(index)].type;
            const std::string gep = freshTmp();
            writeln("  " + gep + " = getelementptr " + structValueTypeName(decl->name) + ", " +
                    structTypeName(decl->name) + " " + obj_val + ", i32 0, i32 " +
                    std::to_string(index));
            return loadValue(field_type, gep, locals);
        }
        case Expr::Kind::New: {
            const StructDecl* decl = findStruct(expr.name);
            if (decl == nullptr) {
                throw XlangError("unknown struct `" + expr.name + "`");
            }
            const std::size_t size = structSizeBytes(*decl);
            const std::string raw = freshTmp();
            writeln("  " + raw + " = call i8* @malloc(i64 " + std::to_string(size) + ")");
            const Type struct_type = Type::makeStruct(expr.name);
            const std::string typed = freshTmp();
            writeln("  " + typed + " = bitcast i8* " + raw + " to " +
                    structTypeName(expr.name));

            for (const FieldInit& init : expr.field_inits) {
                const int index = structFieldIndex(*decl, init.name);
                const Type field_type = decl->fields[static_cast<std::size_t>(index)].type;
                const auto [_, val] = emitExpr(*init.value, locals);
                const std::string gep = freshTmp();
                writeln("  " + gep + " = getelementptr " + structValueTypeName(decl->name) +
                        ", " + structTypeName(decl->name) + " " + typed + ", i32 0, i32 " +
                        std::to_string(index));
                storeValue(field_type, val, gep, locals);
            }

            return {struct_type, typed};
        }
        case Expr::Kind::Binary: {
            const auto [left_ty, left] = emitExpr(*expr.left, locals);
            const auto [right_ty, right] = emitExpr(*expr.right, locals);

            if (expr.bin_op == BinOp::Add &&
                (isStringType(left_ty) || isStringType(right_ty))) {
                std::string left_str = left;
                std::string right_str = right;
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
                const std::string tmp = emitStringConcat(left_str, right_str);
                return {Type{TypeKind::String}, tmp};
            }

            Type result_ty = left_ty;
            if (left_ty.isFloating() || right_ty.isFloating()) {
                result_ty = Type{TypeKind::Double};
            }
            const std::string tmp = freshTmp();
            std::string op;
            switch (expr.bin_op) {
                case BinOp::Add: op = left_ty.isFloating() ? "fadd" : "add"; break;
                case BinOp::Sub: op = left_ty.isFloating() ? "fsub" : "sub"; break;
                case BinOp::Mul: op = left_ty.isFloating() ? "fmul" : "mul"; break;
                case BinOp::Div: op = left_ty.isFloating() ? "fdiv" : "sdiv"; break;
            }
            writeln("  " + tmp + " = " + op + " " + llvmTypeName(result_ty) + " " + left + ", " +
                    right);
            return {result_ty, tmp};
        }
        case Expr::Kind::Call: {
            std::vector<Type> arg_types;
            std::ostringstream args;
            for (std::size_t i = 0; i < expr.args.size(); ++i) {
                if (i > 0) {
                    args << ", ";
                }
                const auto [ty, val] = emitExpr(*expr.args[i], locals);
                arg_types.push_back(ty);
                args << llvmTypeName(ty) << " " << val;
            }

            const std::optional<FunctionSignature> resolved =
                resolveFunctionCall(expr.name, arg_types);
            if (!resolved) {
                std::ostringstream message;
                message << "no matching overload for `" << expr.name << "(";
                for (std::size_t i = 0; i < arg_types.size(); ++i) {
                    if (i > 0) {
                        message << ", ";
                    }
                    message << typeToString(arg_types[i]);
                }
                message << ")`";
                throw XlangError(message.str());
            }

            const std::string llvm_name = [&]() {
                if (const Function* definition =
                        findFunctionDefinition(*program_, resolved->name, arg_types);
                    definition != nullptr && definition->syscall) {
                    return definition->name;
                }
                return mangleFunctionName(resolved->name, paramTypes(resolved->params));
            }();
            const std::string tmp = freshTmp();
            writeln("  " + tmp + " = call " + llvmTypeName(resolved->return_type) + " @" +
                    llvm_name + "(" + args.str() + ")");
            return {resolved->return_type, tmp};
        }
    }
    throw XlangError("invalid expression");
}

Type Codegen::resolveVarType(const std::string& name,
                             const std::unordered_map<std::string, std::string>& locals) const {
    (void)locals;
    const auto local_it = local_types_.find(name);
    if (local_it != local_types_.end()) {
        return local_it->second;
    }
    const auto global_it = global_types_.find(name);
    if (global_it != global_types_.end()) {
        return global_it->second;
    }
    return defaultType();
}

std::string Codegen::resolveVar(const std::string& name,
                                const std::unordered_map<std::string, std::string>& locals) const {
    const auto it = locals.find(name);
    if (it != locals.end()) {
        return it->second;
    }
    if (globals_.find(name) != globals_.end()) {
        return globalName(name);
    }
    throw XlangError("undefined variable `" + name + "`");
}

std::string Codegen::freshTmp() {
    return "%t" + std::to_string(tmp_counter_++);
}

std::string Codegen::localPtr(const std::string& name) {
    return "%" + name + ".addr";
}

std::string Codegen::globalName(const std::string& name) {
    return "@g_" + name;
}

void Codegen::writeln(const std::string& line) {
    output_ += line;
    output_ += '\n';
}

}  // namespace xlang
