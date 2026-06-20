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

bool paramTypesMatch(const std::vector<Type>& expected, const std::vector<Type>& actual,
                     bool variadic) {
    if (variadic) {
        if (actual.size() < expected.size()) {
            return false;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (!typesEqual(expected[i], actual[i])) {
                return false;
            }
        }
        return true;
    }
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

bool paramTypesMatchWithWidening(const std::vector<Type>& expected,
                                 const std::vector<Type>& actual, bool variadic) {
    if (variadic) {
        if (actual.size() < expected.size()) {
            return false;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (typesEqual(expected[i], actual[i])) {
                continue;
            }
            if (expected[i].kind == TypeKind::Int64 && actual[i].kind == TypeKind::Int32) {
                continue;
            }
            return false;
        }
        return true;
    }
    if (expected.size() != actual.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (typesEqual(expected[i], actual[i])) {
            continue;
        }
        if (expected[i].kind == TypeKind::Int64 && actual[i].kind == TypeKind::Int32) {
            continue;
        }
        return false;
    }
    return true;
}

std::optional<FunctionSignature> findMatchingFunctionImpl(
    const std::string& name, const std::vector<Type>& arg_types,
    const std::vector<FunctionSignature>& candidates, bool allow_widening) {
    const FunctionSignature* match = nullptr;
    for (const FunctionSignature& candidate : candidates) {
        if (candidate.name != name) {
            continue;
        }
        const std::vector<Type> expected = paramTypes(candidate.params);
        const bool matches = allow_widening ? paramTypesMatchWithWidening(expected, arg_types,
                                                                        candidate.variadic)
                                          : paramTypesMatch(expected, arg_types,
                                                            candidate.variadic);
        if (!matches) {
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

std::optional<FunctionSignature> findMatchingFunction(
    const std::string& name, const std::vector<Type>& arg_types,
    const std::vector<FunctionSignature>& candidates) {
    if (const std::optional<FunctionSignature> exact =
            findMatchingFunctionImpl(name, arg_types, candidates, false)) {
        return exact;
    }
    return findMatchingFunctionImpl(name, arg_types, candidates, true);
}

std::vector<FunctionSignature> collectDefinedFunctions(const Program& program) {
    std::vector<FunctionSignature> functions;
    for (const Function& function : program.functions) {
        if (function.syscall) {
            FunctionSignature signature;
            signature.name = function.name;
            signature.params = function.params;
            signature.return_type = function.return_type;
            signature.variadic = function.variadic;
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
        signature.variadic = function.variadic;
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
        if (!paramTypesMatch(paramTypes(function.params), param_types, function.variadic)) {
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
    if (expr.kind == Expr::Kind::MethodCall && expr.object) {
        return exprUsesString(*expr.object);
    }
    if (expr.kind == Expr::Kind::Cast && expr.object) {
        return exprUsesString(*expr.object);
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
    if (stmt.condition && exprUsesString(*stmt.condition)) {
        return true;
    }
    if (stmt.expr && exprUsesString(*stmt.expr)) {
        return true;
    }
    if (stmt.return_value && exprUsesString(*stmt.return_value)) {
        return true;
    }
    if (stmt.target && exprUsesString(*stmt.target)) {
        return true;
    }
    if (stmt.then_block) {
        for (const Stmt& inner : stmt.then_block->statements) {
            if (stmtUsesString(inner)) {
                return true;
            }
        }
    }
    if (stmt.else_block) {
        for (const Stmt& inner : stmt.else_block->statements) {
            if (stmtUsesString(inner)) {
                return true;
            }
        }
    }
    if (stmt.loop_body) {
        for (const Stmt& inner : stmt.loop_body->statements) {
            if (stmtUsesString(inner)) {
                return true;
            }
        }
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
    if (expr.kind == Expr::Kind::MethodCall && expr.object) {
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

bool exprHasArray(const Expr& expr) {
    if (expr.kind == Expr::Kind::NewArray) {
        return true;
    }
    if (expr.kind == Expr::Kind::Index && expr.object) {
        return true;
    }
    if (expr.object && exprHasArray(*expr.object)) {
        return true;
    }
    if (expr.left && exprHasArray(*expr.left)) {
        return true;
    }
    if (expr.right && exprHasArray(*expr.right)) {
        return true;
    }
    for (const auto& arg : expr.args) {
        if (exprHasArray(*arg)) {
            return true;
        }
    }
    return false;
}

bool stmtHasArray(const Stmt& stmt) {
    if (stmt.type.isArray()) {
        return true;
    }
    if (stmt.expr && exprHasArray(*stmt.expr)) {
        return true;
    }
    if (stmt.return_value && exprHasArray(*stmt.return_value)) {
        return true;
    }
    if (stmt.target && exprHasArray(*stmt.target)) {
        return true;
    }
    if (stmt.index_target && exprHasArray(*stmt.index_target)) {
        return true;
    }
    if (stmt.condition && exprHasArray(*stmt.condition)) {
        return true;
    }
    if (stmt.then_block) {
        for (const Stmt& inner : stmt.then_block->statements) {
            if (stmtHasArray(inner)) {
                return true;
            }
        }
    }
    if (stmt.else_block) {
        for (const Stmt& inner : stmt.else_block->statements) {
            if (stmtHasArray(inner)) {
                return true;
            }
        }
    }
    if (stmt.loop_body) {
        for (const Stmt& inner : stmt.loop_body->statements) {
            if (stmtHasArray(inner)) {
                return true;
            }
        }
    }
    return false;
}

bool programUsesArrays(const Program& program) {
    for (const GlobalVar& global : program.globals) {
        if (global.type.isArray()) {
            return true;
        }
        if (global.init && exprHasArray(*global.init)) {
            return true;
        }
    }
    for (const Function& function : program.functions) {
        for (const Stmt& stmt : function.body.statements) {
            if (stmtHasArray(stmt)) {
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
    for (const ImportDecl& import : program.imports) {
        if (!import.alias.empty()) {
            cg.import_aliases_[import.alias] = import.alias;
        }
    }
    cg.needs_heap_ = programUsesHeap(program) || programUsesStrings(program) || programUsesArrays(program);
    cg.needs_strings_ = programUsesStrings(program);
    cg.needs_arrays_ = programUsesArrays(program);
    cg.collectSyscalls(program);
    cg.emitPrelude(program);
    cg.emitStructTypes(program);
    if (cg.needs_arrays_) {
        cg.emitArrayRuntimeSupport();
    }
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
            cg.defined_functions_.insert(
                mangleFunctionName(function.name, types, function.variadic));
            cg.emitFunction(function);
        }
    }
    cg.emitDeferredThunks();
    cg.emitSyscallLowering();

    CodegenResult result;
    result.ir = std::move(cg.output_);
    result.syscalls = std::move(cg.syscalls_);
    result.needs_thread_link = syscallsNeedThreadLink(result.syscalls);
    result.needs_ssl_link = syscallsNeedSslLink(result.syscalls);
    result.needs_server_link = syscallsNeedServerLink(result.syscalls);
    result.needs_panic_link = syscallsNeedPanicLink(result.syscalls);
    result.needs_process_link = syscallsNeedProcessLink(result.syscalls);
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
    const std::string triple =
        options_.target_triple.empty() ? "unknown-unknown-unknown" : options_.target_triple;
    writeln("target triple = \"" + triple + "\"");
    writeln("");

    if (needs_heap_) {
        writeln("declare i8* @malloc(i64)");
        writeln("declare void @free(i8*)");
        writeln("");
    }

    writeln("declare i32 @printf(i8*, ...)");
    writeln("@__xlang_print_nl = private unnamed_addr constant [2 x i8] c\"\\0A\\00\"");
    writeln("@__xlang_print_fmt_s = private unnamed_addr constant [3 x i8] c\"%s\\00\"");
    writeln("@__xlang_print_fmt_d = private unnamed_addr constant [3 x i8] c\"%d\\00\"");
    writeln("@__xlang_print_fmt_f = private unnamed_addr constant [3 x i8] c\"%f\\00\"");
    writeln("");

    for (const Function& function : program.functions) {
        if (function.external) {
            const std::vector<Type> types = paramTypes(function.params);
            defined_functions_.insert(
                mangleFunctionName(function.name, types, function.external ? false : function.variadic));
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
    writeln("declare i32 @strcmp(i8*, i8*)");
    writeln("declare i8* @strstr(i8*, i8*)");
    writeln("declare i8* @strncpy(i8*, i8*, i64)");
    writeln("");
    writeln("define internal i32 @__xlang_str_len(i8* %s) {");
    writeln("  %n = call i64 @strlen(i8* %s)");
    writeln("  %n32 = trunc i64 %n to i32");
    writeln("  ret i32 %n32");
    writeln("}");
    writeln("");
    writeln("define internal i32 @__xlang_str_eq(i8* %a, i8* %b) {");
    writeln("  %rc = call i32 @strcmp(i8* %a, i8* %b)");
    writeln("  %eq = icmp eq i32 %rc, 0");
    writeln("  %one = select i1 %eq, i32 1, i32 0");
    writeln("  ret i32 %one");
    writeln("}");
    writeln("");
    writeln("define internal i32 @__xlang_str_byte(i8* %s, i32 %i) {");
    writeln("  %len = call i64 @strlen(i8* %s)");
    writeln("  %i64 = sext i32 %i to i64");
    writeln("  %bad = icmp uge i64 %i64, %len");
    writeln("  br i1 %bad, label %out, label %load");
    writeln("load:");
    writeln("  %p = getelementptr i8, i8* %s, i64 %i64");
    writeln("  %c = load i8, i8* %p");
    writeln("  %u = zext i8 %c to i32");
    writeln("  ret i32 %u");
    writeln("out:");
    writeln("  ret i32 -1");
    writeln("}");
    writeln("");
    writeln("define internal i32 @__xlang_str_find(i8* %hay, i8* %needle) {");
    writeln("  %hit = call i8* @strstr(i8* %hay, i8* %needle)");
    writeln("  %miss = icmp eq i8* %hit, null");
    writeln("  br i1 %miss, label %none, label %found");
    writeln("found:");
    writeln("  %off = ptrtoint i8* %hit to i64");
    writeln("  %base = ptrtoint i8* %hay to i64");
    writeln("  %idx64 = sub i64 %off, %base");
    writeln("  %idx = trunc i64 %idx64 to i32");
    writeln("  ret i32 %idx");
    writeln("none:");
    writeln("  ret i32 -1");
    writeln("}");
    writeln("");
    writeln("define internal i8* @__xlang_str_sub(i8* %s, i32 %start, i32 %len) {");
    writeln("  %total = call i64 @strlen(i8* %s)");
    writeln("  %start64 = sext i32 %start to i64");
    writeln("  %len64 = sext i32 %len to i64");
    writeln("  %bad_start = icmp uge i64 %start64, %total");
    writeln("  %end64 = add i64 %start64, %len64");
    writeln("  %bad_end = icmp ugt i64 %end64, %total");
    writeln("  %bad = or i1 %bad_start, %bad_end");
    writeln("  br i1 %bad, label %empty, label %copy");
    writeln("empty:");
    writeln("  %z = call i8* @malloc(i64 1)");
    writeln("  store i8 0, i8* %z");
    writeln("  ret i8* %z");
    writeln("copy:");
    writeln("  %size = add i64 %len64, 1");
    writeln("  %buf = call i8* @malloc(i64 %size)");
    writeln("  %src = getelementptr i8, i8* %s, i64 %start64");
    writeln("  call i8* @strncpy(i8* %buf, i8* %src, i64 %len64)");
    writeln("  %end = getelementptr i8, i8* %buf, i64 %len64");
    writeln("  store i8 0, i8* %end");
    writeln("  ret i8* %buf");
    writeln("}");
    writeln("");
}

void Codegen::emitDeferredThunks() {
    if (spawn_cap_globals_.empty() && spawn_thunks_.empty()) {
        return;
    }
    for (const std::string& cap : spawn_cap_globals_) {
        writeln(cap);
    }
    if (!spawn_cap_globals_.empty()) {
        writeln("");
    }
    for (const std::string& thunk : spawn_thunks_) {
        writeln(thunk);
    }
    writeln("");
}

std::pair<Type, std::string> Codegen::emitPrintCall(
    const std::vector<std::unique_ptr<Expr>>& args,
    const std::unordered_map<std::string, std::string>& locals) {
    needs_printf_ = true;
    if (args.empty()) {
        throw XlangError("print requires at least one argument");
    }

    const auto emitPrintf = [&](const std::string& fmt, const std::vector<std::pair<Type, std::string>>& values) {
        std::ostringstream call_args;
        call_args << "i8* " << fmt;
        for (const auto& [ty, val] : values) {
            call_args << ", " << llvmTypeName(ty) << " " << val;
        }
        const std::string tmp = freshTmp();
        writeln("  " + tmp + " = call i32 (i8*, ...) @printf(" + call_args.str() + ")");
        return tmp;
    };

    if (args.size() == 1) {
        const auto [ty, val] = emitExpr(*args[0], locals);
        if (isStringType(ty)) {
            emitPrintf("getelementptr inbounds ([3 x i8], [3 x i8]* @__xlang_print_fmt_s, i32 0, i32 0)",
                       {{ty, val}});
        } else if (ty.kind == TypeKind::Int32) {
            emitPrintf("getelementptr inbounds ([3 x i8], [3 x i8]* @__xlang_print_fmt_d, i32 0, i32 0)",
                       {{ty, val}});
        } else if (ty.isFloating()) {
            emitPrintf("getelementptr inbounds ([3 x i8], [3 x i8]* @__xlang_print_fmt_f, i32 0, i32 0)",
                       {{ty, val}});
        } else {
            throw XlangError("print supports string and numeric types");
        }
    } else {
        const auto [fmt_ty, fmt_val] = emitExpr(*args[0], locals);
        if (!isStringType(fmt_ty)) {
            throw XlangError("print format string must be a string");
        }
        std::vector<std::pair<Type, std::string>> values;
        for (std::size_t i = 1; i < args.size(); ++i) {
            values.push_back(emitExpr(*args[i], locals));
        }
        emitPrintf(fmt_val, values);
    }

    const std::string nl = freshTmp();
    writeln("  " + nl +
            " = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([2 x i8], "
            "[2 x i8]* @__xlang_print_nl, i32 0, i32 0))");
    (void)nl;
    const std::string tmp = freshTmp();
    writeln("  " + tmp + " = add i32 0, 0");
    return {Type{TypeKind::Int32}, tmp};
}

std::string Codegen::emitSpawnEntry(const Expr& arg,
                                    const std::unordered_map<std::string, std::string>& locals) {
    if (arg.kind == Expr::Kind::FunctionRef) {
        const Function* function = findUniqueFunctionByName(*program_, arg.name);
        if (function == nullptr) {
            throw XlangError("unknown function `" + arg.name + "` for spawn");
        }
        if (!function->params.empty()) {
            throw XlangError("spawn requires a bound call such as spawn(worker(1, 2))");
        }
        const std::string llvm_name =
            mangleFunctionName(function->name, paramTypes(function->params), function->variadic);
        const std::string tmp = freshTmp();
        writeln("  " + tmp + " = ptrtoint i32 ()* @" + llvm_name + " to i64");
        return tmp;
    }

    if (arg.kind != Expr::Kind::Call) {
        throw XlangError("spawn requires a function call such as spawn(worker(1, 2))");
    }

    std::vector<Type> arg_types;
    std::vector<std::pair<Type, std::string>> arg_values;
    for (const auto& inner_arg : arg.args) {
        const auto emitted = emitExpr(*inner_arg, locals);
        arg_types.push_back(emitted.first);
        arg_values.push_back(emitted);
    }

    const std::optional<FunctionSignature> resolved =
        resolveFunctionCall(arg.name, arg_types);
    if (!resolved) {
        throw XlangError("no matching function for spawn target `" + arg.name + "`");
    }

    const std::string inner_llvm =
        mangleFunctionName(resolved->name, paramTypes(resolved->params), resolved->variadic);

    const std::uint32_t id = spawn_thunk_counter_++;
    std::ostringstream thunk;
    std::vector<std::string> cap_globals;
    for (std::size_t i = 0; i < arg_values.size(); ++i) {
        const std::string cap = "@__spawn_cap_" + std::to_string(id) + "_" + std::to_string(i);
        cap_globals.push_back(cap);
        const Type& ty = arg_values[i].first;
        if (ty.kind == TypeKind::Struct) {
            throw XlangError("spawn does not support struct arguments yet");
        }
        spawn_cap_globals_.push_back(cap + " = internal global " + llvmTypeName(ty) +
                                       " zeroinitializer");
        writeln("  store " + llvmTypeName(ty) + " " + arg_values[i].second + ", " +
                llvmTypeName(ty) + "* " + cap + ", align " + std::to_string(llvmTypeAlign(ty)));
    }

    const std::string thunk_name = "__spawn_thunk_" + std::to_string(id);
    thunk << "define internal i32 @" << thunk_name << "() {\n";
    std::ostringstream call_args;
    for (std::size_t i = 0; i < arg_values.size(); ++i) {
        const Type& ty = arg_values[i].first;
        const std::string loaded = "%cap" + std::to_string(i);
        if (ty.kind == TypeKind::Struct) {
            thunk << "  " << loaded << " = load " << structTypeName(ty.struct_name) << ", "
                  << structTypeName(ty.struct_name) << "* " << cap_globals[i] << ", align 8\n";
        } else {
            thunk << "  " << loaded << " = load " << llvmTypeName(ty) << ", " << llvmTypeName(ty)
                  << "* " << cap_globals[i] << ", align " << std::to_string(llvmTypeAlign(ty))
                  << "\n";
        }
        if (i > 0) {
            call_args << ", ";
        }
        call_args << llvmTypeName(ty) << " " << loaded;
    }
    thunk << "  call " << llvmTypeName(resolved->return_type) << " @" << inner_llvm << "("
          << call_args.str() << ")\n";
    thunk << "  ret i32 0\n";
    thunk << "}\n";
    spawn_thunks_.push_back(thunk.str());

    const std::string entry = freshTmp();
    writeln("  " + entry + " = ptrtoint i32 ()* @" + thunk_name + " to i64");
    return entry;
}

void Codegen::emitArrayRuntimeSupport() {
    writeln("%array.hdr = type { i8*, i64, i64, i64 }");
    writeln("declare i8* @realloc(i8*, i64)");
    writeln("declare i8* @memcpy(i8*, i8*, i64)");
    writeln("");
    writeln("define internal %array.hdr* @__xlang_array_new(i64 %elem_size) {");
    writeln("  %raw = call i8* @malloc(i64 32)");
    writeln("  %arr = bitcast i8* %raw to %array.hdr*");
    writeln("  %cap_bytes = mul i64 %elem_size, 4");
    writeln("  %data = call i8* @malloc(i64 %cap_bytes)");
    writeln("  %data_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 0");
    writeln("  store i8* %data, i8** %data_ptr");
    writeln("  %len_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 1");
    writeln("  store i64 0, i64* %len_ptr");
    writeln("  %cap_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 2");
    writeln("  store i64 4, i64* %cap_ptr");
    writeln("  %head_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 3");
    writeln("  store i64 0, i64* %head_ptr");
    writeln("  ret %array.hdr* %arr");
    writeln("}");
    writeln("");
    writeln("define internal i64 @__xlang_array_len(%array.hdr* %arr) {");
    writeln("  %len_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 1");
    writeln("  %len = load i64, i64* %len_ptr");
    writeln("  ret i64 %len");
    writeln("}");
    writeln("");
    writeln("define internal void @__xlang_array_push(%array.hdr* %arr, i8* %elem, i64 %elem_size) {");
    writeln("  %len_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 1");
    writeln("  %cap_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 2");
    writeln("  %head_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 3");
    writeln("  %data_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 0");
    writeln("  %len = load i64, i64* %len_ptr");
    writeln("  %cap = load i64, i64* %cap_ptr");
    writeln("  %head = load i64, i64* %head_ptr");
    writeln("  %tail = add i64 %head, %len");
    writeln("  %full = icmp uge i64 %len, %cap");
    writeln("  br i1 %full, label %grow, label %write");
    writeln("grow:");
    writeln("  %new_cap = mul i64 %cap, 2");
    writeln("  %new_bytes = mul i64 %new_cap, %elem_size");
    writeln("  %old_data = load i8*, i8** %data_ptr");
    writeln("  %new_data = call i8* @realloc(i8* %old_data, i64 %new_bytes)");
    writeln("  store i8* %new_data, i8** %data_ptr");
    writeln("  store i64 %new_cap, i64* %cap_ptr");
    writeln("  br label %write");
    writeln("write:");
    writeln("  %data = load i8*, i8** %data_ptr");
    writeln("  %offset = mul i64 %tail, %elem_size");
    writeln("  %slot = getelementptr i8, i8* %data, i64 %offset");
    writeln("  call i8* @memcpy(i8* %slot, i8* %elem, i64 %elem_size)");
    writeln("  %next_len = add i64 %len, 1");
    writeln("  store i64 %next_len, i64* %len_ptr");
    writeln("  ret void");
    writeln("}");
    writeln("");
    writeln("define internal i8* @__xlang_array_pop_front(%array.hdr* %arr, i64 %elem_size) {");
    writeln("  %len_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 1");
    writeln("  %head_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 3");
    writeln("  %data_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 0");
    writeln("  %len = load i64, i64* %len_ptr");
    writeln("  %head = load i64, i64* %head_ptr");
    writeln("  %data = load i8*, i8** %data_ptr");
    writeln("  %offset = mul i64 %head, %elem_size");
    writeln("  %slot = getelementptr i8, i8* %data, i64 %offset");
    writeln("  %buf = call i8* @malloc(i64 %elem_size)");
    writeln("  call i8* @memcpy(i8* %buf, i8* %slot, i64 %elem_size)");
    writeln("  %next_head = add i64 %head, 1");
    writeln("  store i64 %next_head, i64* %head_ptr");
    writeln("  %next_len = sub i64 %len, 1");
    writeln("  store i64 %next_len, i64* %len_ptr");
    writeln("  ret i8* %buf");
    writeln("}");
    writeln("");
    writeln("define internal i8* @__xlang_array_get_raw(%array.hdr* %arr, i64 %index, i64 %elem_size) {");
    writeln("  %len_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 1");
    writeln("  %head_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 3");
    writeln("  %data_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 0");
    writeln("  %len = load i64, i64* %len_ptr");
    writeln("  %head = load i64, i64* %head_ptr");
    writeln("  %data = load i8*, i8** %data_ptr");
    writeln("  %pos = add i64 %head, %index");
    writeln("  %offset = mul i64 %pos, %elem_size");
    writeln("  %slot = getelementptr i8, i8* %data, i64 %offset");
    writeln("  %buf = call i8* @malloc(i64 %elem_size)");
    writeln("  call i8* @memcpy(i8* %buf, i8* %slot, i64 %elem_size)");
    writeln("  ret i8* %buf");
    writeln("}");
    writeln("");
    writeln("define internal i8* @__xlang_array_pop_raw(%array.hdr* %arr, i64 %elem_size) {");
    writeln("  %len_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 1");
    writeln("  %head_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 3");
    writeln("  %data_ptr = getelementptr %array.hdr, %array.hdr* %arr, i32 0, i32 0");
    writeln("  %len = load i64, i64* %len_ptr");
    writeln("  %last = sub i64 %len, 1");
    writeln("  %head = load i64, i64* %head_ptr");
    writeln("  %data = load i8*, i8** %data_ptr");
    writeln("  %pos = add i64 %head, %last");
    writeln("  %offset = mul i64 %pos, %elem_size");
    writeln("  %slot = getelementptr i8, i8* %data, i64 %offset");
    writeln("  %buf = call i8* @malloc(i64 %elem_size)");
    writeln("  call i8* @memcpy(i8* %buf, i8* %slot, i64 %elem_size)");
    writeln("  store i64 %last, i64* %len_ptr");
    writeln("  ret i8* %buf");
    writeln("}");
    writeln("");
}

std::string Codegen::freshLabel() {
    return "L" + std::to_string(label_counter_++);
}

std::size_t Codegen::typeSizeBytes(const Type& type) const {
    if (type.kind == TypeKind::Struct) {
        const StructDecl* decl = findStruct(type.struct_name);
        if (decl == nullptr) {
            throw XlangError("unknown struct `" + type.struct_name + "`");
        }
        return structSizeBytes(*decl);
    }
    switch (type.kind) {
        case TypeKind::Int32:
        case TypeKind::Float:
            return 4;
        case TypeKind::Int64:
        case TypeKind::Double:
        case TypeKind::BigInt:
        case TypeKind::String:
        case TypeKind::Pointer:
        case TypeKind::Array:
            return 8;
        case TypeKind::Bool:
        case TypeKind::Char:
            return 1;
        default:
            return llvmTypeAlign(type);
    }
}

std::size_t Codegen::elementSizeBytes(const Type& type) const {
    return typeSizeBytes(type);
}

void Codegen::emitBlock(const Block& block, std::unordered_map<std::string, std::string>& locals,
                        bool& has_return) {
    for (const Stmt& stmt : block.statements) {
        if (emitStatement(stmt, locals)) {
            has_return = true;
            return;
        }
    }
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
            case '/': escaped += "\\2F"; break;
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
    if (stmt.condition) {
        collectStringLiteralsFromExpr(*stmt.condition);
    }
    if (stmt.then_block) {
        for (const Stmt& inner : stmt.then_block->statements) {
            collectStringLiteralsFromStmt(inner);
        }
    }
    if (stmt.else_block) {
        for (const Stmt& inner : stmt.else_block->statements) {
            collectStringLiteralsFromStmt(inner);
        }
    }
    if (stmt.loop_body) {
        for (const Stmt& inner : stmt.loop_body->statements) {
            collectStringLiteralsFromStmt(inner);
        }
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
    auto emitOne = [&](const StructDecl& decl) {
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
    };

    std::unordered_set<std::string> emitted;
    for (const StructDecl& decl : program.structs) {
        emitOne(decl);
        emitted.insert(decl.name);
    }
    for (const StructDecl& decl : options_.runtime_structs) {
        if (emitted.find(decl.name) != emitted.end()) {
            continue;
        }
        emitOne(decl);
        emitted.insert(decl.name);
    }
    if (!emitted.empty()) {
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
        if (paramTypesMatch(paramTypes(function.params), param_types, function.variadic)) {
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

std::optional<FunctionSignature> Codegen::resolveMethodCall(
    const std::string& name, const Type& receiver_type,
    const std::vector<Type>& arg_types) const {
    std::vector<Type> full_args;
    full_args.push_back(receiver_type);
    full_args.insert(full_args.end(), arg_types.begin(), arg_types.end());
    if (const std::optional<FunctionSignature> exact = resolveFunctionCall(name, full_args)) {
        return exact;
    }

    const std::string suffix = "_" + name;
    const FunctionSignature* match = nullptr;
    const std::vector<FunctionSignature> defined = collectDefinedFunctions(*program_);
    for (const FunctionSignature& candidate : defined) {
        if (candidate.params.empty()) {
            continue;
        }
        if (!typesEqual(candidate.params[0].type, receiver_type)) {
            continue;
        }
        if (candidate.name.size() < suffix.size()) {
            continue;
        }
        if (candidate.name.substr(candidate.name.size() - suffix.size()) != suffix) {
            continue;
        }
        std::vector<Type> expected = paramTypes(candidate.params);
        if (!paramTypesMatchWithWidening(expected, full_args, candidate.variadic)) {
            continue;
        }
        if (match != nullptr) {
            throw XlangError("ambiguous method call `" + name + "`");
        }
        match = &candidate;
    }
    if (match == nullptr) {
        return std::nullopt;
    }
    return *match;
}

std::string Codegen::importPrefixedName(const std::string& alias,
                                        const std::string& method) const {
    return alias + "_" + method;
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
        if (!global.init && global.type.isArray()) {
            writeln(globalName(global.name) + " = " + linkage + "global " + llvm_ty + " null");
            continue;
        }
        if (global.init && global.init->kind == Expr::Kind::IntLiteral &&
            (global.type.kind == TypeKind::Int32 || global.type.kind == TypeKind::Int64)) {
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
        if (global.init->kind == Expr::Kind::IntLiteral &&
            (global.type.kind == TypeKind::Int32 || global.type.kind == TypeKind::Int64)) {
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
        if (global.init->kind == Expr::Kind::IntLiteral &&
            (global.type.kind == TypeKind::Int32 || global.type.kind == TypeKind::Int64)) {
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
    const std::string llvm_name =
        mangleFunctionName(function.name, param_type_list, function.variadic);
    std::ostringstream params;
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i > 0) {
            params << ", ";
        }
        params << llvmTypeName(function.params[i].type);
    }
    if (function.variadic) {
        if (!function.params.empty()) {
            params << ", ";
        }
        params << "...";
    }
    writeln("declare " + llvmTypeName(function.return_type) + " @" + llvm_name + "(" +
            params.str() + ")");
    writeln("");
}

void Codegen::emitDeclareFunction(const Function& function) {
    FunctionSignature signature;
    signature.name = function.name;
    signature.params = function.params;
    signature.return_type = function.return_type;
    signature.variadic = function.variadic;
    emitDeclareFunction(signature);
}

void Codegen::emitFunction(const Function& function) {
    const std::vector<Type> param_type_list = paramTypes(function.params);
    const std::string llvm_name =
        mangleFunctionName(function.name, param_type_list, function.variadic);
    std::ostringstream params;
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i > 0) {
            params << ", ";
        }
        params << llvmTypeName(function.params[i].type) << " %" << function.params[i].name;
    }
    if (function.variadic) {
        if (!function.params.empty()) {
            params << ", ";
        }
        params << "...";
    }

    writeln("define " + fnLinkage(function) + llvmTypeName(function.return_type) + " @" +
            llvm_name + "(" + params.str() + ") {");

    if (function.variadic && function.name == "print") {
        writeln("  ret i32 0");
        writeln("}");
        writeln("");
        return;
    }

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
    } else if (type.kind == TypeKind::Array) {
        writeln("  " + ptr + " = alloca %array.hdr*, align 8");
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
            allocLocal(stmt.name, ty, locals);
            storeValue(ty, val, localPtr(stmt.name), locals);
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
        case Stmt::Kind::IndexAssign: {
            const auto [arr_ty, arr] = emitExpr(*stmt.index_target->object, locals);
            if (!arr_ty.isArray()) {
                throw XlangError("index assignment requires array");
            }
            const auto [_, idx] = emitExpr(*stmt.index_target->right, locals);
            const auto [val_ty, val] = emitExpr(*stmt.expr, locals);
            (void)val_ty;
            const Type elem = arr_ty.arrayElementType();
            const std::size_t sz = elementSizeBytes(elem);
            const std::string idx64 = freshTmp();
            writeln("  " + idx64 + " = sext i32 " + idx + " to i64");
            const std::string head_p = freshTmp();
            writeln("  " + head_p + " = getelementptr %array.hdr, %array.hdr* " + arr +
                    ", i32 0, i32 3");
            const std::string head = freshTmp();
            writeln("  " + head + " = load i64, i64* " + head_p);
            const std::string pos = freshTmp();
            writeln("  " + pos + " = add i64 " + head + ", " + idx64);
            const std::string data_p = freshTmp();
            writeln("  " + data_p + " = getelementptr %array.hdr, %array.hdr* " + arr +
                    ", i32 0, i32 0");
            const std::string data = freshTmp();
            writeln("  " + data + " = load i8*, i8** " + data_p);
            const std::string off = freshTmp();
            writeln("  " + off + " = mul i64 " + pos + ", " + std::to_string(sz));
            const std::string slot = freshTmp();
            writeln("  " + slot + " = getelementptr i8, i8* " + data + ", i64 " + off);
            const std::string casted = freshTmp();
            if (elem.kind == TypeKind::Struct) {
                writeln("  " + casted + " = bitcast i8* " + slot + " to " +
                        structTypeName(elem.struct_name));
                storeValue(elem, val, casted, locals);
            } else {
                writeln("  " + casted + " = bitcast i8* " + slot + " to " +
                        llvmTypeName(elem) + "*");
                storeValue(elem, val, casted, locals);
            }
            return false;
        }
        case Stmt::Kind::If: {
            const auto [_, cond] = emitExpr(*stmt.condition, locals);
            const std::string cond_i1 = freshTmp();
            writeln("  " + cond_i1 + " = icmp ne i8 " + cond + ", 0");
            const std::string then_label = freshLabel();
            const std::string else_label = freshLabel();
            const std::string merge_label = freshLabel();
            if (stmt.else_block) {
                writeln("  br i1 " + cond_i1 + ", label %" + then_label + ", label %" +
                        else_label);
            } else {
                writeln("  br i1 " + cond_i1 + ", label %" + then_label + ", label %" +
                        merge_label);
            }
            writeln(then_label + ":");
            bool branch_return = false;
            emitBlock(*stmt.then_block, locals, branch_return);
            if (!branch_return) {
                writeln("  br label %" + merge_label);
            }
            if (stmt.else_block) {
                writeln(else_label + ":");
                branch_return = false;
                emitBlock(*stmt.else_block, locals, branch_return);
                if (!branch_return) {
                    writeln("  br label %" + merge_label);
                }
            }
            writeln(merge_label + ":");
            return false;
        }
        case Stmt::Kind::While: {
            const std::string cond_label = freshLabel();
            const std::string body_label = freshLabel();
            const std::string exit_label = freshLabel();
            writeln("  br label %" + cond_label);
            writeln(cond_label + ":");
            const auto [_, cond] = emitExpr(*stmt.condition, locals);
            const std::string cond_i1 = freshTmp();
            writeln("  " + cond_i1 + " = icmp ne i8 " + cond + ", 0");
            writeln("  br i1 " + cond_i1 + ", label %" + body_label + ", label %" + exit_label);
            writeln(body_label + ":");
            bool branch_return = false;
            emitBlock(*stmt.loop_body, locals, branch_return);
            if (!branch_return) {
                writeln("  br label %" + cond_label);
            }
            writeln(exit_label + ":");
            return false;
        }
        case Stmt::Kind::Return: {
            if (stmt.return_value) {
                auto [ty, val] = emitExpr(*stmt.return_value, locals);
                if (ty.kind == TypeKind::Int32 && current_return_type_.kind == TypeKind::Int64) {
                    const std::string widened = freshTmp();
                    writeln("  " + widened + " = sext i32 " + val + " to i64");
                    val = widened;
                    ty = current_return_type_;
                } else if (ty.kind == TypeKind::Int64 &&
                           current_return_type_.kind == TypeKind::Int32) {
                    const std::string narrowed = freshTmp();
                    writeln("  " + narrowed + " = trunc i64 " + val + " to i32");
                    val = narrowed;
                    ty = current_return_type_;
                }
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
        case Expr::Kind::MethodCall: {
            std::vector<Type> arg_types;
            std::vector<std::string> arg_values;
            for (const std::unique_ptr<Expr>& arg_expr : expr.args) {
                const auto [ty, val] = emitExpr(*arg_expr, locals);
                arg_types.push_back(ty);
                arg_values.push_back(val);
            }

            if (expr.object->kind == Expr::Kind::Variable &&
                import_aliases_.find(expr.object->name) != import_aliases_.end()) {
                const std::string alias = import_aliases_.at(expr.object->name);
                const std::string fn_name = importPrefixedName(alias, expr.name);
                const std::optional<FunctionSignature> resolved =
                    resolveFunctionCall(fn_name, arg_types);
                if (!resolved) {
                    throw XlangError("unknown import call `" + alias + "." + expr.name + "`");
                }
                const std::string llvm_name = mangleFunctionName(resolved->name,
                                                                  paramTypes(resolved->params),
                                                                  resolved->variadic);
                std::ostringstream args;
                for (std::size_t i = 0; i < arg_values.size(); ++i) {
                    if (i > 0) {
                        args << ", ";
                    }
                    Type arg_ty = arg_types[i];
                    std::string val = arg_values[i];
                    const Type param_ty = resolved->params[i].type;
                    if (arg_ty.kind == TypeKind::Int32 && param_ty.kind == TypeKind::Int64) {
                        const std::string widened = freshTmp();
                        writeln("  " + widened + " = sext i32 " + val + " to i64");
                        val = widened;
                    }
                    args << llvmTypeName(param_ty) << " " << val;
                }
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call " + llvmTypeName(resolved->return_type) + " @" +
                        llvm_name + "(" + args.str() + ")");
                return {resolved->return_type, tmp};
            }

            const auto [recv_ty, recv_val] = emitExpr(*expr.object, locals);
            const std::optional<FunctionSignature> resolved =
                resolveMethodCall(expr.name, recv_ty, arg_types);
            if (!resolved) {
                throw XlangError("no matching method `" + expr.name + "` for `" +
                                 typeToString(recv_ty) + "`");
            }

            std::ostringstream args;
            args << llvmTypeName(resolved->params[0].type) << " " << recv_val;
            for (std::size_t i = 0; i < arg_values.size(); ++i) {
                args << ", ";
                Type arg_ty = arg_types[i];
                std::string val = arg_values[i];
                const Type param_ty = resolved->params[i + 1].type;
                if (arg_ty.kind == TypeKind::Int32 && param_ty.kind == TypeKind::Int64) {
                    const std::string widened = freshTmp();
                    writeln("  " + widened + " = sext i32 " + val + " to i64");
                    val = widened;
                }
                args << llvmTypeName(param_ty) << " " << val;
            }
            const std::string llvm_name =
                mangleFunctionName(resolved->name, paramTypes(resolved->params), resolved->variadic);
            const std::string tmp = freshTmp();
            writeln("  " + tmp + " = call " + llvmTypeName(resolved->return_type) + " @" +
                    llvm_name + "(" + args.str() + ")");
            return {resolved->return_type, tmp};
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
        case Expr::Kind::NewArray: {
            const std::size_t elem_size = elementSizeBytes(expr.new_type);
            const std::string tmp = freshTmp();
            writeln("  " + tmp + " = call %array.hdr* @__xlang_array_new(i64 " +
                    std::to_string(elem_size) + ")");
            return {Type::makeArray(expr.new_type), tmp};
        }
        case Expr::Kind::Cast: {
            const auto [from_ty, val] = emitExpr(*expr.object, locals);
            const Type target = expr.new_type;
            if (typesEqual(from_ty, target)) {
                return {target, val};
            }
            if (from_ty.kind == TypeKind::Int32 && target.kind == TypeKind::Int64) {
                const std::string widened = freshTmp();
                writeln("  " + widened + " = sext i32 " + val + " to i64");
                return {target, widened};
            }
            if (from_ty.kind == TypeKind::Int64 && target.kind == TypeKind::Int32) {
                const std::string narrowed = freshTmp();
                writeln("  " + narrowed + " = trunc i64 " + val + " to i32");
                return {target, narrowed};
            }
            if (from_ty.kind == TypeKind::Struct && target.kind == TypeKind::Struct) {
                const std::string casted = freshTmp();
                writeln("  " + casted + " = bitcast " + llvmTypeName(from_ty) + " " + val +
                        " to " + llvmTypeName(target));
                return {target, casted};
            }
            if (from_ty.kind == TypeKind::Struct && target.kind == TypeKind::Interface) {
                const std::string casted = freshTmp();
                writeln("  " + casted + " = bitcast " + llvmTypeName(from_ty) + " " + val +
                        " to i8*");
                return {target, casted};
            }
            if (from_ty.kind == TypeKind::Interface && target.kind == TypeKind::Struct) {
                const std::string casted = freshTmp();
                writeln("  " + casted + " = bitcast i8* " + val + " to " + llvmTypeName(target));
                return {target, casted};
            }
            throw XlangError("unsupported cast from `" + typeToString(from_ty) + "` to `" +
                             typeToString(target) + "`");
        }
        case Expr::Kind::Index: {
            const auto [arr_ty, arr] = emitExpr(*expr.object, locals);
            if (!arr_ty.isArray()) {
                throw XlangError("index access requires array");
            }
            const auto [_, idx] = emitExpr(*expr.right, locals);
            const Type elem = arr_ty.arrayElementType();
            const std::size_t sz = elementSizeBytes(elem);
            const std::string idx64 = freshTmp();
            writeln("  " + idx64 + " = sext i32 " + idx + " to i64");
            const std::string head_p = freshTmp();
            writeln("  " + head_p + " = getelementptr %array.hdr, %array.hdr* " + arr +
                    ", i32 0, i32 3");
            const std::string head = freshTmp();
            writeln("  " + head + " = load i64, i64* " + head_p);
            const std::string pos = freshTmp();
            writeln("  " + pos + " = add i64 " + head + ", " + idx64);
            const std::string data_p = freshTmp();
            writeln("  " + data_p + " = getelementptr %array.hdr, %array.hdr* " + arr +
                    ", i32 0, i32 0");
            const std::string data = freshTmp();
            writeln("  " + data + " = load i8*, i8** " + data_p);
            const std::string off = freshTmp();
            writeln("  " + off + " = mul i64 " + pos + ", " + std::to_string(sz));
            const std::string slot = freshTmp();
            writeln("  " + slot + " = getelementptr i8, i8* " + data + ", i64 " + off);
            if (elem.kind == TypeKind::Struct) {
                const std::string typed = freshTmp();
                writeln("  " + typed + " = bitcast i8* " + slot + " to " +
                        structTypeName(elem.struct_name));
                return loadValue(elem, typed, locals);
            }
            const std::string typed = freshTmp();
            writeln("  " + typed + " = bitcast i8* " + slot + " to " + llvmTypeName(elem) + "*");
            return loadValue(elem, typed, locals);
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

            if (expr.bin_op == BinOp::And || expr.bin_op == BinOp::Or) {
                const std::string left_i1 = freshTmp();
                const std::string right_i1 = freshTmp();
                writeln("  " + left_i1 + " = icmp ne i8 " + left + ", 0");
                writeln("  " + right_i1 + " = icmp ne i8 " + right + ", 0");
                const std::string tmp_i1 = freshTmp();
                const std::string op = expr.bin_op == BinOp::And ? "and" : "or";
                writeln("  " + tmp_i1 + " = " + op + " i1 " + left_i1 + ", " + right_i1);
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = zext i1 " + tmp_i1 + " to i8");
                return {Type{TypeKind::Bool}, tmp};
            }

            if (expr.bin_op >= BinOp::Eq && expr.bin_op <= BinOp::Ge) {
                const std::string tmp_i1 = freshTmp();
                std::string pred;
                switch (expr.bin_op) {
                    case BinOp::Eq: pred = "eq"; break;
                    case BinOp::Ne: pred = "ne"; break;
                    case BinOp::Lt: pred = "slt"; break;
                    case BinOp::Le: pred = "sle"; break;
                    case BinOp::Gt: pred = "sgt"; break;
                    case BinOp::Ge: pred = "sge"; break;
                    default: pred = "eq"; break;
                }
                Type cmp_ty = left_ty;
                std::string left_val = left;
                std::string right_val = right;
                if (left_ty.isFloating() || right_ty.isFloating()) {
                    cmp_ty = Type{TypeKind::Double};
                } else if (left_ty.kind == TypeKind::Int64 || right_ty.kind == TypeKind::Int64) {
                    cmp_ty = Type{TypeKind::Int64};
                    if (left_ty.kind == TypeKind::Int32) {
                        const std::string widened = freshTmp();
                        writeln("  " + widened + " = sext i32 " + left_val + " to i64");
                        left_val = widened;
                    }
                    if (right_ty.kind == TypeKind::Int32) {
                        const std::string widened = freshTmp();
                        writeln("  " + widened + " = sext i32 " + right_val + " to i64");
                        right_val = widened;
                    }
                }
                if (cmp_ty.isFloating()) {
                    writeln("  " + tmp_i1 + " = fcmp o" + pred + " " + llvmTypeName(cmp_ty) +
                            " " + left_val + ", " + right_val);
                } else {
                    writeln("  " + tmp_i1 + " = icmp " + pred + " " + llvmTypeName(cmp_ty) +
                            " " + left_val + ", " + right_val);
                }
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = zext i1 " + tmp_i1 + " to i8");
                return {Type{TypeKind::Bool}, tmp};
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
                default: op = "add"; break;
            }
            writeln("  " + tmp + " = " + op + " " + llvmTypeName(result_ty) + " " + left + ", " +
                    right);
            return {result_ty, tmp};
        }
        case Expr::Kind::Call: {
            if (expr.name == "print") {
                return emitPrintCall(expr.args, locals);
            }

            if (expr.name == "spawn") {
                if (expr.args.size() != 1) {
                    throw XlangError("spawn requires exactly one bound call argument");
                }
                const std::string entry = emitSpawnEntry(*expr.args[0], locals);
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i32 @spawn$i64(i64 " + entry + ")");
                return {Type{TypeKind::Int32}, tmp};
            }

            std::vector<Type> arg_types;
            std::vector<std::string> arg_values;
            arg_types.reserve(expr.args.size());
            arg_values.reserve(expr.args.size());
            for (const std::unique_ptr<Expr>& arg_expr : expr.args) {
                const auto [ty, val] = emitExpr(*arg_expr, locals);
                arg_types.push_back(ty);
                arg_values.push_back(val);
            }

            if (expr.name == "invoke0" && expr.args.size() == 1) {
                const auto [_, entry] = emitExpr(*expr.args[0], locals);
                const std::string fn = freshTmp();
                writeln("  " + fn + " = inttoptr i64 " + entry + " to i32 ()*");
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i32 " + fn + "()");
                return {Type{TypeKind::Int32}, tmp};
            }

            if (expr.name == "invoke" && expr.args.size() == 2) {
                const auto [_, entry] = emitExpr(*expr.args[0], locals);
                const auto [__, arg] = emitExpr(*expr.args[1], locals);
                const std::string fn = freshTmp();
                writeln("  " + fn + " = inttoptr i64 " + entry + " to i32 (i32)*");
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i32 " + fn + "(i32 " + arg + ")");
                return {Type{TypeKind::Int32}, tmp};
            }

            if (expr.name == "array_len" && expr.args.size() == 1) {
                const auto [arr_ty, arr] = emitExpr(*expr.args[0], locals);
                (void)arr_ty;
                const std::string len64 = freshTmp();
                writeln("  " + len64 + " = call i64 @__xlang_array_len(%array.hdr* " + arr + ")");
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = trunc i64 " + len64 + " to i32");
                return {Type{TypeKind::Int32}, tmp};
            }

            if (expr.name == "str_len" && expr.args.size() == 1) {
                const auto [ty, val] = emitExpr(*expr.args[0], locals);
                if (!isStringType(ty)) {
                    throw XlangError("str_len requires string");
                }
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i32 @__xlang_str_len(i8* " + val + ")");
                return {Type{TypeKind::Int32}, tmp};
            }

            if (expr.name == "str_eq" && expr.args.size() == 2) {
                const auto [a_ty, a] = emitExpr(*expr.args[0], locals);
                const auto [b_ty, b] = emitExpr(*expr.args[1], locals);
                if (!isStringType(a_ty) || !isStringType(b_ty)) {
                    throw XlangError("str_eq requires two strings");
                }
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i32 @__xlang_str_eq(i8* " + a + ", i8* " + b + ")");
                return {Type{TypeKind::Int32}, tmp};
            }

            if (expr.name == "str_cat" && expr.args.size() == 2) {
                const auto [a_ty, a] = emitExpr(*expr.args[0], locals);
                const auto [b_ty, b] = emitExpr(*expr.args[1], locals);
                if (!isStringType(a_ty) || !isStringType(b_ty)) {
                    throw XlangError("str_cat requires two strings");
                }
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i8* @__xlang_str_concat(i8* " + a + ", i8* " + b + ")");
                return {Type{TypeKind::String}, tmp};
            }

            if (expr.name == "str_byte" && expr.args.size() == 2) {
                const auto [s_ty, s] = emitExpr(*expr.args[0], locals);
                const auto [_, i] = emitExpr(*expr.args[1], locals);
                if (!isStringType(s_ty)) {
                    throw XlangError("str_byte requires string");
                }
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i32 @__xlang_str_byte(i8* " + s + ", i32 " + i + ")");
                return {Type{TypeKind::Int32}, tmp};
            }

            if (expr.name == "str_find" && expr.args.size() == 2) {
                const auto [a_ty, a] = emitExpr(*expr.args[0], locals);
                const auto [b_ty, b] = emitExpr(*expr.args[1], locals);
                if (!isStringType(a_ty) || !isStringType(b_ty)) {
                    throw XlangError("str_find requires two strings");
                }
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i32 @__xlang_str_find(i8* " + a + ", i8* " + b + ")");
                return {Type{TypeKind::Int32}, tmp};
            }

            if (expr.name == "str_sub" && expr.args.size() == 3) {
                const auto [s_ty, s] = emitExpr(*expr.args[0], locals);
                const auto [_, start] = emitExpr(*expr.args[1], locals);
                const auto [__, len] = emitExpr(*expr.args[2], locals);
                if (!isStringType(s_ty)) {
                    throw XlangError("str_sub requires string");
                }
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i8* @__xlang_str_sub(i8* " + s + ", i32 " + start +
                        ", i32 " + len + ")");
                return {Type{TypeKind::String}, tmp};
            }

            if (expr.name == "array_push" && expr.args.size() == 2) {
                const auto [arr_ty, arr] = emitExpr(*expr.args[0], locals);
                const auto [val_ty, val] = emitExpr(*expr.args[1], locals);
                const std::size_t sz = elementSizeBytes(arr_ty.arrayElementType());
                const std::string raw = freshTmp();
                if (val_ty.kind == TypeKind::Struct) {
                    writeln("  " + raw + " = bitcast " + llvmTypeName(val_ty) + " " + val +
                            " to i8*");
                } else {
                    const std::string slot = freshTmp();
                    writeln("  " + slot + " = alloca " + llvmTypeName(val_ty) + ", align " +
                            std::to_string(llvmTypeAlign(val_ty)));
                    storeValue(val_ty, val, slot, locals);
                    writeln("  " + raw + " = bitcast " + llvmTypeName(val_ty) + "* " + slot +
                            " to i8*");
                }
                writeln("  call void @__xlang_array_push(%array.hdr* " + arr + ", i8* " + raw +
                        ", i64 " + std::to_string(sz) + ")");
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = add i32 0, 0");
                return {Type{TypeKind::Int32}, tmp};
            }

            if (expr.name == "array_pop_front" && expr.args.size() == 1) {
                const auto [arr_ty, arr] = emitExpr(*expr.args[0], locals);
                const Type elem = arr_ty.arrayElementType();
                const std::size_t sz = elementSizeBytes(elem);
                const std::string raw = freshTmp();
                writeln("  " + raw + " = call i8* @__xlang_array_pop_front(%array.hdr* " + arr +
                        ", i64 " + std::to_string(sz) + ")");
                if (elem.kind == TypeKind::Struct) {
                    const std::string typed = freshTmp();
                    writeln("  " + typed + " = bitcast i8* " + raw + " to " +
                            structTypeName(elem.struct_name));
                    return {elem, typed};
                }
                const std::string typed = freshTmp();
                writeln("  " + typed + " = bitcast i8* " + raw + " to " + llvmTypeName(elem) +
                        "*");
                return loadValue(elem, typed, locals);
            }

            if (expr.name == "array_get" && expr.args.size() == 2) {
                const auto [arr_ty, arr] = emitExpr(*expr.args[0], locals);
                const auto [_, idx] = emitExpr(*expr.args[1], locals);
                const Type elem = arr_ty.arrayElementType();
                const std::size_t sz = elementSizeBytes(elem);
                const std::string idx64 = freshTmp();
                writeln("  " + idx64 + " = sext i32 " + idx + " to i64");
                const std::string raw = freshTmp();
                writeln("  " + raw + " = call i8* @__xlang_array_get_raw(%array.hdr* " + arr +
                        ", i64 " + idx64 + ", i64 " + std::to_string(sz) + ")");
                if (elem.kind == TypeKind::Struct) {
                    const std::string typed = freshTmp();
                    writeln("  " + typed + " = bitcast i8* " + raw + " to " +
                            structTypeName(elem.struct_name));
                    return {elem, typed};
                }
                const std::string typed = freshTmp();
                writeln("  " + typed + " = bitcast i8* " + raw + " to " + llvmTypeName(elem) +
                        "*");
                return loadValue(elem, typed, locals);
            }

            if (expr.name == "array_pop" && expr.args.size() == 1) {
                const auto [arr_ty, arr] = emitExpr(*expr.args[0], locals);
                const Type elem = arr_ty.arrayElementType();
                const std::size_t sz = elementSizeBytes(elem);
                const std::string raw = freshTmp();
                writeln("  " + raw + " = call i8* @__xlang_array_pop_raw(%array.hdr* " + arr +
                        ", i64 " + std::to_string(sz) + ")");
                if (elem.kind == TypeKind::Struct) {
                    const std::string typed = freshTmp();
                    writeln("  " + typed + " = bitcast i8* " + raw + " to " +
                            structTypeName(elem.struct_name));
                    return {elem, typed};
                }
                const std::string typed = freshTmp();
                writeln("  " + typed + " = bitcast i8* " + raw + " to " + llvmTypeName(elem) +
                        "*");
                return loadValue(elem, typed, locals);
            }

            if (expr.name == "str_from_int" && expr.args.size() == 1) {
                const auto [ty, val] = emitExpr(*expr.args[0], locals);
                (void)ty;
                const std::string tmp = freshTmp();
                writeln("  " + tmp + " = call i8* @__xlang_int_to_str(i32 " + val + ")");
                return {Type{TypeKind::String}, tmp};
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
                return mangleFunctionName(resolved->name, paramTypes(resolved->params),
                                        resolved->variadic);
            }();

            std::ostringstream args;
            for (std::size_t i = 0; i < arg_values.size(); ++i) {
                if (i > 0) {
                    args << ", ";
                }
                Type arg_ty = arg_types[i];
                std::string val = arg_values[i];
                const Type param_ty = resolved->params[i].type;
                if (arg_ty.kind == TypeKind::Int32 && param_ty.kind == TypeKind::Int64) {
                    const std::string widened = freshTmp();
                    writeln("  " + widened + " = sext i32 " + val + " to i64");
                    val = widened;
                    arg_ty = param_ty;
                }
                args << llvmTypeName(param_ty) << " " << val;
            }

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
