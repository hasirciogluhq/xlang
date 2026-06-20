#include "xlang/module.h"

#include "xlang/error.h"
#include "xlang/types.h"
#include "xlang/parser.h"

#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cctype>

namespace xlang {

namespace {

std::unique_ptr<Expr> cloneExpr(const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::IntLiteral:
            return Expr::makeInt(expr.int_value, expr.span);
        case Expr::Kind::FloatLiteral:
            return Expr::makeFloat(expr.float_value, expr.span);
        case Expr::Kind::BoolLiteral:
            return Expr::makeBool(expr.bool_value, expr.span);
        case Expr::Kind::StringLiteral:
            return Expr::makeString(expr.name, expr.span);
        case Expr::Kind::Null:
            return Expr::makeNull(expr.span);
        case Expr::Kind::Variable:
            return Expr::makeVar(expr.name, expr.span);
        case Expr::Kind::Binary:
            return Expr::makeBinary(expr.bin_op, cloneExpr(*expr.left), cloneExpr(*expr.right),
                                    expr.span);
        case Expr::Kind::Call: {
            std::vector<std::unique_ptr<Expr>> args;
            for (const auto& arg : expr.args) {
                args.push_back(cloneExpr(*arg));
            }
            return Expr::makeCall(expr.name, std::move(args), expr.span);
        }
        case Expr::Kind::FunctionRef:
            return Expr::makeFunctionRef(expr.name, expr.span);
        case Expr::Kind::FieldAccess:
            return Expr::makeFieldAccess(cloneExpr(*expr.object), expr.name, expr.span);
        case Expr::Kind::MethodCall: {
            std::vector<std::unique_ptr<Expr>> args;
            for (const auto& arg : expr.args) {
                args.push_back(cloneExpr(*arg));
            }
            return Expr::makeMethodCall(cloneExpr(*expr.object), expr.name, std::move(args),
                                        expr.span);
        }
        case Expr::Kind::Cast:
            return Expr::makeCast(cloneExpr(*expr.object), expr.new_type, expr.span);
        case Expr::Kind::New: {
            std::vector<FieldInit> inits;
            for (const FieldInit& init : expr.field_inits) {
                FieldInit copy;
                copy.name = init.name;
                if (init.value) {
                    copy.value = cloneExpr(*init.value);
                }
                inits.push_back(std::move(copy));
            }
            return Expr::makeNew(expr.name, std::move(inits), expr.span);
        }
        case Expr::Kind::NewArray:
            return Expr::makeNewArray(expr.new_type, expr.span);
        case Expr::Kind::Index:
            return Expr::makeIndex(cloneExpr(*expr.object), cloneExpr(*expr.right), expr.span);
    }
    throw XlangError("invalid expression clone");
}

GlobalVar cloneGlobal(const GlobalVar& global) {
    GlobalVar copy;
    copy.name = global.name;
    copy.type = global.type;
    copy.span = global.span;
    copy.exported = global.exported;
    copy.external = global.external;
    if (global.init) {
        copy.init = cloneExpr(*global.init);
    }
    return copy;
}

Stmt cloneStmt(const Stmt& stmt);
Block cloneBlock(const Block& block);

Block cloneBlock(const Block& block) {
    Block copy;
    copy.span = block.span;
    for (const Stmt& inner : block.statements) {
        copy.statements.push_back(cloneStmt(inner));
    }
    return copy;
}

Stmt cloneStmt(const Stmt& stmt) {
    Stmt copied;
    copied.kind = stmt.kind;
    copied.span = stmt.span;
    copied.name = stmt.name;
    copied.type = stmt.type;
    copied.explicit_type = stmt.explicit_type;
    copied.field = stmt.field;
    if (stmt.target) {
        copied.target = cloneExpr(*stmt.target);
    }
    if (stmt.expr) {
        copied.expr = cloneExpr(*stmt.expr);
    }
    if (stmt.return_value) {
        copied.return_value = cloneExpr(*stmt.return_value);
    }
    if (stmt.condition) {
        copied.condition = cloneExpr(*stmt.condition);
    }
    if (stmt.index_target) {
        copied.index_target = cloneExpr(*stmt.index_target);
    }
    if (stmt.then_block) {
        copied.then_block = std::make_unique<Block>(cloneBlock(*stmt.then_block));
    }
    if (stmt.else_block) {
        copied.else_block = std::make_unique<Block>(cloneBlock(*stmt.else_block));
    }
    if (stmt.loop_body) {
        copied.loop_body = std::make_unique<Block>(cloneBlock(*stmt.loop_body));
    }
    return copied;
}

Function cloneFunction(const Function& function) {
    Function copy;
    copy.name = function.name;
    copy.params = function.params;
    copy.return_type = function.return_type;
    copy.span = function.span;
    copy.exported = function.exported;
    copy.external = function.external;
    copy.syscall = function.syscall;
    copy.variadic = function.variadic;
    copy.body.span = function.body.span;
    for (const Stmt& stmt : function.body.statements) {
        copy.body.statements.push_back(cloneStmt(stmt));
    }
    return copy;
}

[[nodiscard]] bool hasGlobal(const Program& program, const std::string& name) {
    for (const GlobalVar& global : program.globals) {
        if (global.name == name) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool hasFunction(const Program& program, const std::string& name) {
    for (const Function& function : program.functions) {
        if (function.name == name) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool hasFunctionOverload(const Program& program, const Function& function) {
    std::vector<Type> signature;
    signature.reserve(function.params.size());
    for (const TypedName& param : function.params) {
        signature.push_back(param.type);
    }

    for (const Function& existing : program.functions) {
        if (existing.name != function.name) {
            continue;
        }
        if (existing.params.size() != function.params.size()) {
            continue;
        }
        bool same = true;
        for (std::size_t i = 0; i < existing.params.size(); ++i) {
            if (!typesEqual(existing.params[i].type, function.params[i].type)) {
                same = false;
                break;
            }
        }
        if (same) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool hasSymbol(const Program& program, const std::string& name) {
    return hasGlobal(program, name) || hasFunction(program, name);
}

void ensureUnique(const Program& into, const std::string& name) {
    if (hasSymbol(into, name)) {
        throw XlangError("duplicate symbol: " + name);
    }
}

void addGlobal(Program& into, GlobalVar global) {
    ensureUnique(into, global.name);
    into.globals.push_back(std::move(global));
}

void addFunction(Program& into, Function function) {
    if (hasFunctionOverload(into, function)) {
        throw XlangError("duplicate function overload: " + function.name);
    }
    into.functions.push_back(std::move(function));
}

const GlobalVar* findGlobal(const Program& program, const std::string& name) {
    for (const GlobalVar& global : program.globals) {
        if (global.name == name) {
            return &global;
        }
    }
    return nullptr;
}

const Function* findFunction(const Program& program, const std::string& name) {
    for (const Function& function : program.functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

[[nodiscard]] bool isImportableGlobal(const GlobalVar& global) {
    return global.exported;
}

[[nodiscard]] bool isImportableFunction(const Function& function) {
    return function.exported && !function.body.statements.empty();
}

[[nodiscard]] bool isImportableSymbol(const Function& function) {
    return function.syscall || isImportableFunction(function);
}

std::string prefixed(const std::string& alias, const std::string& name) {
    return alias + "_" + name;
}

[[nodiscard]] bool iequals(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool isDirectoryModule(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

Program cloneProgramImpl(const Program& program) {
    Program copy;
    copy.imports = program.imports;
    copy.import_aliases = program.import_aliases;
    copy.structs = program.structs;
    copy.interfaces = program.interfaces;
    for (const GlobalVar& global : program.globals) {
        copy.globals.push_back(cloneGlobal(global));
    }
    for (const Function& function : program.functions) {
        copy.functions.push_back(cloneFunction(function));
    }
    return copy;
}

}  // namespace

Program cloneProgram(const Program& program) {
    return cloneProgramImpl(program);
}

void registerImportAlias(Program& into, const std::string& alias) {
    into.import_aliases[alias] = alias;
}

void mergeImportAliases(Program& into, const Program& from) {
    for (const auto& [alias, target] : from.import_aliases) {
        into.import_aliases.emplace(alias, target);
    }
}

Program loadProgram(const std::filesystem::path& entry,
                    const std::vector<std::filesystem::path>& search_paths) {
    return ModuleLoader(entry, search_paths).load();
}

ModuleLoader::ModuleLoader(std::filesystem::path entry,
                           std::vector<std::filesystem::path> search_paths)
    : entry_(std::filesystem::absolute(entry)), search_paths_(std::move(search_paths)) {}

Program ModuleLoader::load() {
    std::error_code ec;
    if (std::filesystem::is_directory(entry_, ec)) {
        return loadPackage(entry_);
    }
    return loadFile(entry_);
}

Program ModuleLoader::loadPackage(const std::filesystem::path& dir) {
    const std::filesystem::path absolute = std::filesystem::absolute(dir);
    const std::string key = absolute.string();

    if (cache_.find(key) != cache_.end()) {
        return cloneProgramImpl(cache_.at(key));
    }
    if (loading_.find(key) != loading_.end()) {
        throw XlangError("circular import: " + key);
    }

    loading_[key] = true;

    Program pkg;
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(absolute)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".xlang") {
            continue;
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        const Program part = loadFile(file);
        mergeAll(pkg, part);
    }

    loading_.erase(key);
    cache_.emplace(key, cloneProgramImpl(pkg));
    return std::move(pkg);
}

Program ModuleLoader::loadFile(const std::filesystem::path& path) {
    const std::filesystem::path absolute = std::filesystem::absolute(path);
    const std::string key = absolute.string();

    if (cache_.find(key) != cache_.end()) {
        return cloneProgramImpl(cache_.at(key));
    }
    if (loading_.find(key) != loading_.end()) {
        throw XlangError("circular import: " + key);
    }

    if (isDirectoryModule(absolute)) {
        return loadPackage(absolute);
    }

    loading_[key] = true;

    std::ifstream in(absolute);
    if (!in) {
        throw XlangError("failed to read module: " + absolute.string());
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source_text = buffer.str();

    const std::vector<ImportDecl> import_decls = parseImportDecls(source_text);
    std::vector<StructDecl> prelude_structs;
    std::unordered_set<std::string> seen_structs;
    for (const ImportDecl& import : import_decls) {
        collectPreludeStructs(import, absolute.parent_path(), prelude_structs, seen_structs);
    }

    const Program source = parseSource(source_text, prelude_structs);

    Program merged;
    merged.imports = source.imports;
    for (ImportDecl& import : merged.imports) {
        if (import.is_clause_import) {
            mergeFromClauses(merged, import, absolute.parent_path());
            continue;
        }
        const std::filesystem::path dep = resolveModule(absolute.parent_path(), import.module);
        const Program dep_program = loadFile(dep);
        if (import.is_from) {
            mergeSelected(merged, dep_program, import);
        } else if (!import.alias.empty()) {
            if (import.module.find('/') != std::string::npos) {
                registerImportAlias(merged, import.alias);
                mergePrefixed(merged, dep_program, import.alias);
            } else {
                ImportDecl selective;
                selective.module = import.module;
                selective.is_clause_import = true;
                ImportSpec spec;
                spec.name = import.alias;
                selective.names.push_back(std::move(spec));
                mergeFromClauses(merged, selective, absolute.parent_path());
            }
        } else {
            mergeAll(merged, dep_program);
        }
    }

    for (const StructDecl& decl : source.structs) {
        for (const StructDecl& existing : merged.structs) {
            if (existing.name == decl.name) {
                throw XlangError("duplicate struct: " + decl.name);
            }
        }
        merged.structs.push_back(decl);
    }

    for (const InterfaceDecl& decl : source.interfaces) {
        for (const InterfaceDecl& existing : merged.interfaces) {
            if (existing.name == decl.name) {
                throw XlangError("duplicate interface: " + decl.name);
            }
        }
        merged.interfaces.push_back(decl);
    }

    for (const GlobalVar& global : source.globals) {
        addGlobal(merged, cloneGlobal(global));
    }
    for (const Function& function : source.functions) {
        addFunction(merged, cloneFunction(function));
    }

    loading_.erase(key);
    cache_.emplace(key, cloneProgramImpl(merged));
    return std::move(merged);
}

std::filesystem::path ModuleLoader::resolveModule(const std::filesystem::path& from,
                                                  const std::string& name) const {
    auto tryResolve = [&](const std::filesystem::path& root) -> std::filesystem::path {
        const std::filesystem::path file = root / (name + ".xlang");
        if (std::filesystem::exists(file)) {
            return file;
        }
        const std::filesystem::path dir = root / name;
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec)) {
            return dir;
        }
        return {};
    };

    for (const std::filesystem::path& search_root : search_paths_) {
        if (const std::filesystem::path candidate = tryResolve(search_root); !candidate.empty()) {
            return candidate;
        }
    }

    if (const std::filesystem::path relative = tryResolve(from); !relative.empty()) {
        return relative;
    }

    if (const char* search = std::getenv("XLANG_PATH")) {
        std::string paths = search;
        std::size_t start = 0;
        while (start <= paths.size()) {
            const std::size_t end = paths.find(':', start);
            const std::string dir = paths.substr(start, end - start);
            if (!dir.empty()) {
                if (const std::filesystem::path candidate =
                        tryResolve(std::filesystem::path(dir));
                    !candidate.empty()) {
                    return candidate;
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    throw XlangError("module not found: " + name);
}

std::optional<std::string> ModuleLoader::findSubmodulePath(const std::string& package,
                                                           const std::string& name) const {
    auto scanDir = [&](const std::filesystem::path& pkg_dir) -> std::optional<std::string> {
        std::error_code ec;
        if (!std::filesystem::is_directory(pkg_dir, ec)) {
            return std::nullopt;
        }
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(pkg_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() != ".xlang") {
                continue;
            }
            if (iequals(entry.path().stem().string(), name)) {
                return package + "/" + entry.path().stem().string();
            }
        }
        return std::nullopt;
    };

    for (const std::filesystem::path& search_root : search_paths_) {
        if (const std::optional<std::string> found = scanDir(search_root / package)) {
            return found;
        }
    }

    if (const char* search = std::getenv("XLANG_PATH")) {
        std::string paths = search;
        std::size_t start = 0;
        while (start <= paths.size()) {
            const std::size_t end = paths.find(':', start);
            const std::string dir = paths.substr(start, end - start);
            if (!dir.empty()) {
                if (const std::optional<std::string> found =
                        scanDir(std::filesystem::path(dir) / package)) {
                    return found;
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    return std::nullopt;
}

void mergeStructs(Program& into, const Program& from) {
    for (const StructDecl& decl : from.structs) {
        bool exists = false;
        for (const StructDecl& existing : into.structs) {
            if (existing.name == decl.name) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            into.structs.push_back(decl);
        }
    }
    for (const InterfaceDecl& decl : from.interfaces) {
        bool exists = false;
        for (const InterfaceDecl& existing : into.interfaces) {
            if (existing.name == decl.name) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            into.interfaces.push_back(decl);
        }
    }
}

void ModuleLoader::mergeAll(Program& into, const Program& from) {
    mergeStructs(into, from);
    mergeImportAliases(into, from);
    for (const GlobalVar& global : from.globals) {
        if (hasGlobal(into, global.name)) {
            continue;
        }
        addGlobal(into, cloneGlobal(global));
    }
    for (const Function& function : from.functions) {
        if (function.body.statements.empty() && !function.external && !function.syscall) {
            continue;
        }
        if (hasFunctionOverload(into, function)) {
            continue;
        }
        addFunction(into, cloneFunction(function));
    }
}

void ModuleLoader::mergePrefixed(Program& into, const Program& from, const std::string& alias) {
    registerImportAlias(into, alias);
    mergeStructs(into, from);
    mergeImportAliases(into, from);

    for (const GlobalVar& global : from.globals) {
        if (isImportableGlobal(global)) {
            continue;
        }
        if (hasGlobal(into, global.name)) {
            continue;
        }
        addGlobal(into, cloneGlobal(global));
    }

    for (const Function& function : from.functions) {
        if (function.body.statements.empty() && !function.external && !function.syscall) {
            continue;
        }
        if (function.syscall) {
            if (hasFunctionOverload(into, function)) {
                continue;
            }
            addFunction(into, cloneFunction(function));
            continue;
        }
        if (isImportableFunction(function)) {
            Function copy = cloneFunction(function);
            copy.name = prefixed(alias, function.name);
            if (hasFunctionOverload(into, copy)) {
                continue;
            }
            addFunction(into, std::move(copy));
            continue;
        }
        if (hasFunctionOverload(into, function)) {
            continue;
        }
        addFunction(into, cloneFunction(function));
    }
}

void ModuleLoader::collectPreludeStructs(const ImportDecl& import,
                                         const std::filesystem::path& from_dir,
                                         std::vector<StructDecl>& out,
                                         std::unordered_set<std::string>& seen) {
    auto addStructs = [&](const Program& program) {
        for (const StructDecl& decl : program.structs) {
            if (seen.insert(decl.name).second) {
                out.push_back(decl);
            }
        }
    };

    if (import.is_clause_import) {
        const std::filesystem::path dep_path = resolveModule(from_dir, import.module);
        addStructs(loadFile(dep_path));
        for (const ImportSpec& spec : import.names) {
            if (spec.wildcard) {
                continue;
            }
            if (const std::optional<std::string> submodule =
                    findSubmodulePath(import.module, spec.name)) {
                const std::filesystem::path sub_path = resolveModule(from_dir, *submodule);
                addStructs(loadFile(sub_path));
            }
        }
        return;
    }

    const std::filesystem::path dep_path = resolveModule(from_dir, import.module);
    addStructs(loadFile(dep_path));
}

void ModuleLoader::mergeFromClauses(Program& into, ImportDecl& import,
                                    const std::filesystem::path& from_dir) {
    const std::filesystem::path dep_path = resolveModule(from_dir, import.module);
    const Program dep_program = loadFile(dep_path);

    for (ImportSpec& spec : import.names) {
        if (spec.wildcard) {
            spec.use_prefix = true;
            spec.bound_alias = spec.alias;
            registerImportAlias(into, spec.alias);
            mergePrefixed(into, dep_program, spec.alias);
            continue;
        }

        if (const std::optional<std::string> submodule = findSubmodulePath(import.module, spec.name)) {
            const std::filesystem::path sub_path = resolveModule(from_dir, *submodule);
            const Program sub_program = loadFile(sub_path);
            const std::string alias = spec.alias.empty() ? spec.name : spec.alias;
            spec.use_prefix = true;
            spec.bound_alias = alias;
            registerImportAlias(into, alias);
            mergePrefixed(into, sub_program, alias);
            continue;
        }

        const std::string target = spec.alias.empty() ? spec.name : spec.alias;
        if (const GlobalVar* global = findGlobal(dep_program, spec.name)) {
            if (!isImportableGlobal(*global)) {
                throw XlangError("cannot import private symbol `" + spec.name + "` from `" +
                                 import.module + "`");
            }
            GlobalVar copy = cloneGlobal(*global);
            copy.name = target;
            addGlobal(into, std::move(copy));
            continue;
        }
        if (const Function* function = findFunction(dep_program, spec.name)) {
            if (!isImportableSymbol(*function)) {
                throw XlangError("cannot import private symbol `" + spec.name + "` from `" +
                                 import.module + "`");
            }
            Function copy = cloneFunction(*function);
            copy.name = target;
            copy.syscall = function->syscall;
            copy.external = function->external;
            addFunction(into, std::move(copy));
            continue;
        }
        throw XlangError("symbol not found in module `" + import.module + "`: " + spec.name);
    }
}

void ModuleLoader::mergeSelected(Program& into, const Program& from, const ImportDecl& import) {
    mergeStructs(into, from);
    for (const ImportSpec& spec : import.names) {
        const std::string target = spec.alias.empty() ? spec.name : spec.alias;

        if (const GlobalVar* global = findGlobal(from, spec.name)) {
            if (!isImportableGlobal(*global)) {
                throw XlangError("cannot import private symbol `" + spec.name + "` from `" +
                                 import.module + "`");
            }
            GlobalVar copy = cloneGlobal(*global);
            copy.name = target;
            addGlobal(into, std::move(copy));
            continue;
        }
        if (const Function* function = findFunction(from, spec.name)) {
            if (!isImportableSymbol(*function)) {
                throw XlangError("cannot import private symbol `" + spec.name + "` from `" +
                                 import.module + "`");
            }
            Function copy = cloneFunction(*function);
            copy.name = target;
            copy.syscall = function->syscall;
            copy.external = function->external;
            addFunction(into, std::move(copy));
            continue;
        }
        throw XlangError("symbol not found in module `" + import.module + "`: " + spec.name);
    }
}

}  // namespace xlang
