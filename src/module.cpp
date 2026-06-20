#include "xlang/module.h"

#include "xlang/error.h"
#include "xlang/parser.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

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

Function cloneFunction(const Function& function) {
    Function copy;
    copy.name = function.name;
    copy.params = function.params;
    copy.return_type = function.return_type;
    copy.span = function.span;
    copy.exported = function.exported;
    copy.external = function.external;
    copy.syscall = function.syscall;
    copy.body.span = function.body.span;
    for (const Stmt& stmt : function.body.statements) {
        Stmt copied;
        copied.kind = stmt.kind;
        copied.span = stmt.span;
        copied.name = stmt.name;
        copied.type = stmt.type;
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
        copy.body.statements.push_back(std::move(copied));
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
    ensureUnique(into, function.name);
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

std::string prefixed(const std::string& alias, const std::string& name) {
    return alias + "_" + name;
}

Program cloneProgram(const Program& program) {
    Program copy;
    copy.imports = program.imports;
    copy.structs = program.structs;
    for (const GlobalVar& global : program.globals) {
        copy.globals.push_back(cloneGlobal(global));
    }
    for (const Function& function : program.functions) {
        copy.functions.push_back(cloneFunction(function));
    }
    return copy;
}

}  // namespace

Program loadProgram(const std::filesystem::path& entry) {
    return ModuleLoader(entry).load();
}

ModuleLoader::ModuleLoader(std::filesystem::path entry)
    : entry_(std::filesystem::absolute(entry)) {}

Program ModuleLoader::load() {
    Program program = loadFile(entry_);
    program.imports.clear();
    return program;
}

Program ModuleLoader::loadFile(const std::filesystem::path& path) {
    const std::filesystem::path absolute = std::filesystem::absolute(path);
    const std::string key = absolute.string();

    if (cache_.find(key) != cache_.end()) {
        return cloneProgram(cache_.at(key));
    }
    if (loading_.find(key) != loading_.end()) {
        throw XlangError("circular import: " + key);
    }

    loading_[key] = true;

    std::ifstream in(absolute);
    if (!in) {
        throw XlangError("failed to read module: " + absolute.string());
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const Program source = parseSource(buffer.str());

    Program merged;
    for (const ImportDecl& import : source.imports) {
        const std::filesystem::path dep = resolveModule(absolute.parent_path(), import.module);
        const Program dep_program = loadFile(dep);
        if (import.is_from) {
            mergeSelected(merged, dep_program, import);
        } else if (!import.alias.empty()) {
            mergePrefixed(merged, dep_program, import.alias);
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

    for (const GlobalVar& global : source.globals) {
        addGlobal(merged, cloneGlobal(global));
    }
    for (const Function& function : source.functions) {
        addFunction(merged, cloneFunction(function));
    }

    loading_.erase(key);
    cache_.emplace(key, cloneProgram(merged));
    return std::move(merged);
}

std::filesystem::path ModuleLoader::resolveModule(const std::filesystem::path& from,
                                                  const std::string& name) const {
    const std::filesystem::path relative = from / (name + ".xlang");
    if (std::filesystem::exists(relative)) {
        return relative;
    }

    if (const char* search = std::getenv("XLANG_PATH")) {
        std::string paths = search;
        std::size_t start = 0;
        while (start <= paths.size()) {
            const std::size_t end = paths.find(':', start);
            const std::string dir = paths.substr(start, end - start);
            if (!dir.empty()) {
                const std::filesystem::path candidate =
                    std::filesystem::path(dir) / (name + ".xlang");
                if (std::filesystem::exists(candidate)) {
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

void ModuleLoader::mergeAll(Program& into, const Program& from) {
    for (const GlobalVar& global : from.globals) {
        if (!isImportableGlobal(global)) {
            continue;
        }
        addGlobal(into, cloneGlobal(global));
    }
    for (const Function& function : from.functions) {
        if (!isImportableFunction(function)) {
            continue;
        }
        addFunction(into, cloneFunction(function));
    }
}

void ModuleLoader::mergePrefixed(Program& into, const Program& from, const std::string& alias) {
    for (const GlobalVar& global : from.globals) {
        if (!isImportableGlobal(global)) {
            continue;
        }
        GlobalVar copy = cloneGlobal(global);
        copy.name = prefixed(alias, global.name);
        addGlobal(into, std::move(copy));
    }
    for (const Function& function : from.functions) {
        if (!isImportableFunction(function)) {
            continue;
        }
        Function copy = cloneFunction(function);
        copy.name = prefixed(alias, function.name);
        addFunction(into, std::move(copy));
    }
}

void ModuleLoader::mergeSelected(Program& into, const Program& from, const ImportDecl& import) {
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
            if (!isImportableFunction(*function)) {
                throw XlangError("cannot import private symbol `" + spec.name + "` from `" +
                                 import.module + "`");
            }
            Function copy = cloneFunction(*function);
            copy.name = target;
            addFunction(into, std::move(copy));
            continue;
        }
        throw XlangError("symbol not found in module `" + import.module + "`: " + spec.name);
    }
}

}  // namespace xlang
