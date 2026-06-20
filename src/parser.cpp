#include "xlang/parser.h"

namespace xlang {

Program parseSource(const std::string& source) {
    return parseSource(source, {});
}

Program parseSource(const std::string& source, const std::vector<StructDecl>& prelude) {
    Parser parser(tokenize(source));
    parser.seedStructs(prelude);
    return parser.parseProgram();
}

std::vector<ImportDecl> parseImportDecls(const std::string& source) {
    return Parser::scanImports(source);
}

std::vector<ImportDecl> Parser::scanImports(const std::string& source) {
    Parser parser(tokenize(source));
    std::vector<ImportDecl> imports;
    while (!parser.isAtEnd() && (parser.check(TokenKind::Import) || parser.check(TokenKind::From))) {
        imports.push_back(parser.parseImport());
    }
    return imports;
}

void Parser::seedStructs(const std::vector<StructDecl>& decls) {
    for (const StructDecl& decl : decls) {
        registerStruct(decl);
    }
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
    tokens_.push_back(Token{TokenKind::End, "", 0, 0.0, 0, 0});
    for (std::size_t i = 0; i + 1 < tokens_.size(); ++i) {
        if (tokens_[i].kind == TokenKind::Fn && tokens_[i + 1].kind == TokenKind::Ident) {
            function_names_.insert(tokens_[i + 1].text);
        }
    }
}

ItemModifiers Parser::parseModifiers() {
    ItemModifiers modifiers;
    while (true) {
        if (match(TokenKind::Export)) {
            modifiers.exported = true;
            continue;
        }
        if (match(TokenKind::External)) {
            modifiers.external = true;
            continue;
        }
        if (match(TokenKind::Declare)) {
            modifiers.declare = true;
            continue;
        }
        break;
    }
    return modifiers;
}

void Parser::validateModifiers(const ItemModifiers& modifiers, const std::string& kind) const {
    if (modifiers.declare && modifiers.exported) {
        throw error("'declare' cannot be combined with 'export'");
    }
    (void)kind;
}

Program Parser::parseProgram() {
    Program program;
    while (!isAtEnd()) {
        if (check(TokenKind::Import) || check(TokenKind::From)) {
            program.imports.push_back(parseImport());
            continue;
        }

        const ItemModifiers modifiers = parseModifiers();

        if (check(TokenKind::Struct)) {
            program.structs.push_back(parseStruct(modifiers));
            continue;
        }

        if (check(TokenKind::Interface)) {
            program.interfaces.push_back(parseInterface(modifiers));
            continue;
        }

        if (modifiers.declare) {
            validateModifiers(modifiers, "declare");
            if (match(TokenKind::Syscall)) {
                program.functions.push_back(parseDeclareSyscall());
                continue;
            }
            if (check(TokenKind::Fn)) {
                program.functions.push_back(parseDeclareFunction(modifiers));
            } else if (check(TokenKind::Ident)) {
                program.globals.push_back(parseDeclareGlobal(modifiers));
            } else {
                throw error("expected fn or global name after declare");
            }
            continue;
        }

        if (check(TokenKind::Fn)) {
            program.functions.push_back(parseFunction(modifiers));
            continue;
        }

        if (check(TokenKind::Ident) &&
            (tokens_[pos_ + 1].kind == TokenKind::Eq ||
             tokens_[pos_ + 1].kind == TokenKind::Colon)) {
            program.globals.push_back(parseGlobalVar(modifiers));
            continue;
        }

        if (modifiers.exported || modifiers.external) {
            throw error("expected struct, fn, or global after modifier");
        }

        throw error("expected import, struct, declare, fn, or global assignment");
    }
    return program;
}

ImportSpec Parser::parseImportClause() {
    ImportSpec spec;
    if (match(TokenKind::Star)) {
        spec.wildcard = true;
        consume(TokenKind::As, "expected 'as' after '*'");
        spec.alias = consume(TokenKind::Ident, "expected namespace alias").text;
        return spec;
    }
    spec.name = consume(TokenKind::Ident, "expected import name").text;
    if (match(TokenKind::As)) {
        spec.alias = consume(TokenKind::Ident, "expected alias name").text;
    }
    return spec;
}

ImportDecl Parser::parseImportClausesFrom() {
    ImportDecl decl;
    decl.is_clause_import = true;
    decl.span = currentSpan();
    decl.names.push_back(parseImportClause());
    while (match(TokenKind::Comma)) {
        decl.names.push_back(parseImportClause());
    }
    consume(TokenKind::From, "expected 'from'");
    decl.module = parseModulePath();
    consumeEndOfStatement();
    return decl;
}

ImportDecl Parser::parseImport() {
    ImportDecl decl;
    decl.span = currentSpan();

    if (match(TokenKind::Import)) {
        if (check(TokenKind::Star)) {
            return parseImportClausesFrom();
        }

        const std::string first = consume(TokenKind::Ident, "expected module or import name").text;
        if (match(TokenKind::Comma)) {
            decl.is_clause_import = true;
            ImportSpec spec;
            spec.name = first;
            decl.names.push_back(std::move(spec));
            while (true) {
                decl.names.push_back(parseImportClause());
                if (!match(TokenKind::Comma)) {
                    break;
                }
            }
            consume(TokenKind::From, "expected 'from'");
            decl.module = parseModulePath();
            consumeEndOfStatement();
            return decl;
        }

        if (match(TokenKind::From)) {
            decl.module = parseModulePath();
            if (decl.module.find('/') != std::string::npos || first == decl.module) {
                decl.alias = first;
                consumeEndOfStatement();
                return decl;
            }
            decl.is_clause_import = true;
            ImportSpec spec;
            spec.name = first;
            decl.names.push_back(std::move(spec));
            consumeEndOfStatement();
            return decl;
        }

        decl.module = first;
        while (match(TokenKind::Slash)) {
            decl.module += "/";
            decl.module += consume(TokenKind::Ident, "expected module path segment").text;
        }
        if (match(TokenKind::As)) {
            decl.alias = consume(TokenKind::Ident, "expected alias name").text;
        }
        consumeEndOfStatement();
        return decl;
    }

    consume(TokenKind::From, "expected 'from'");
    decl.is_from = true;
    decl.module = parseModulePath();
    consume(TokenKind::Import, "expected 'import'");
    decl.names = parseImportNames();
    consumeEndOfStatement();
    return decl;
}

std::string Parser::parseModulePath() {
    std::string path = consume(TokenKind::Ident, "expected module name").text;
    while (match(TokenKind::Slash)) {
        path += "/";
        path += consume(TokenKind::Ident, "expected module path segment").text;
    }
    return path;
}

std::vector<ImportSpec> Parser::parseImportNames() {
    std::vector<ImportSpec> names;
    do {
        ImportSpec spec;
        spec.name = consume(TokenKind::Ident, "expected symbol name").text;
        if (match(TokenKind::As)) {
            spec.alias = consume(TokenKind::Ident, "expected alias name").text;
        }
        names.push_back(std::move(spec));
    } while (match(TokenKind::Comma));
    return names;
}

StructDecl Parser::parseStruct(const ItemModifiers& modifiers) {
    StructDecl decl;
    decl.span = currentSpan();
    decl.exported = modifiers.exported;
    consume(TokenKind::Struct, "expected 'struct'");
    decl.name = consume(TokenKind::Ident, "expected struct name").text;
    consume(TokenKind::LBrace, "expected '{'");

    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        StructField field;
        field.name = consume(TokenKind::Ident, "expected field name").text;
        consume(TokenKind::Colon, "expected ':' after field name");
        field.type = parseType();
        consumeEndOfStatement();
        decl.fields.push_back(std::move(field));
    }

    consume(TokenKind::RBrace, "expected '}'");
    registerStruct(decl);
    return decl;
}

InterfaceDecl Parser::parseInterface(const ItemModifiers& modifiers) {
    InterfaceDecl decl;
    decl.span = currentSpan();
    decl.exported = modifiers.exported;
    consume(TokenKind::Interface, "expected 'interface'");
    decl.name = consume(TokenKind::Ident, "expected interface name").text;
    consume(TokenKind::LBrace, "expected '{'");

    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        InterfaceMethod method;
        method.name = consume(TokenKind::Ident, "expected method name").text;
        consume(TokenKind::LParen, "expected '('");
        bool variadic = false;
        method.params = parseParams(&variadic);
        (void)variadic;
        consume(TokenKind::RParen, "expected ')'");
        consume(TokenKind::Colon, "expected ':' after method params");
        method.return_type = parseType();
        consumeEndOfStatement();
        decl.methods.push_back(std::move(method));
    }

    consume(TokenKind::RBrace, "expected '}'");
    registerInterface(decl);
    return decl;
}

GlobalVar Parser::parseGlobalVar(const ItemModifiers& modifiers) {
    GlobalVar global;
    global.span = currentSpan();
    global.exported = modifiers.exported;
    global.external = modifiers.external;
    global.name = consume(TokenKind::Ident, "expected variable name").text;
    global.type = parseOptionalTypeAfterName(defaultType());
    if (match(TokenKind::Eq)) {
        global.init = parseExpr();
    }
    consumeEndOfStatement();
    return global;
}

GlobalVar Parser::parseDeclareGlobal(const ItemModifiers& modifiers) {
    GlobalVar global;
    global.span = currentSpan();
    global.external = true;
    global.name = consume(TokenKind::Ident, "expected variable name").text;
    global.type = parseOptionalTypeAfterName(defaultType());
    consumeEndOfStatement();
    (void)modifiers;
    return global;
}

Function Parser::parseFunction(const ItemModifiers& modifiers) {
    const Span start = currentSpan();
    consume(TokenKind::Fn, "expected 'fn'");
    const Token name = consume(TokenKind::Ident, "expected function name");
    consume(TokenKind::LParen, "expected '('");
    bool variadic = false;
    std::vector<TypedName> params = parseParams(&variadic);
    consume(TokenKind::RParen, "expected ')'");
    Type return_type = defaultType();
    if (match(TokenKind::Colon)) {
        return_type = parseType();
    }
    Block body = parseBlock();

    Function function;
    function.name = name.text;
    function.params = std::move(params);
    function.return_type = return_type;
    function.variadic = variadic;
    function.body = std::move(body);
    function.exported = modifiers.exported;
    function.external = modifiers.external;
    function.span = start;
    registerFunction(function.name);
    return function;
}

Function Parser::parseDeclareSyscall() {
    const Span start = currentSpan();
    if (match(TokenKind::Fn)) {
    }
    const Token name = consume(TokenKind::Ident, "expected syscall name");
    consume(TokenKind::LParen, "expected '('");
    bool variadic = false;
    std::vector<TypedName> params = parseParams(&variadic);
    consume(TokenKind::RParen, "expected ')'");
    Type return_type = defaultType();
    if (match(TokenKind::Colon)) {
        return_type = parseType();
    }
    consumeEndOfStatement();

    Function function;
    function.name = name.text;
    function.params = std::move(params);
    function.return_type = return_type;
    function.variadic = variadic;
    function.syscall = true;
    function.span = start;
    registerFunction(function.name);
    return function;
}

Function Parser::parseDeclareFunction(const ItemModifiers& modifiers) {
    const Span start = currentSpan();
    consume(TokenKind::Fn, "expected 'fn'");
    const Token name = consume(TokenKind::Ident, "expected function name");
    consume(TokenKind::LParen, "expected '('");
    bool variadic = false;
    std::vector<TypedName> params = parseParams(&variadic);
    consume(TokenKind::RParen, "expected ')'");
    if (match(TokenKind::Colon)) {
        (void)parseType();
    }
    consumeEndOfStatement();

    Function function;
    function.name = name.text;
    function.params = std::move(params);
    function.variadic = variadic;
    function.external = true;
    function.span = start;
    registerFunction(function.name);
    (void)modifiers;
    return function;
}

std::vector<TypedName> Parser::parseParams(bool* variadic_out) {
    std::vector<TypedName> params;
    if (variadic_out != nullptr) {
        *variadic_out = false;
    }
    if (check(TokenKind::RParen)) {
        return params;
    }

    if (match(TokenKind::Ellipsis)) {
        if (variadic_out != nullptr) {
            *variadic_out = true;
        }
        return params;
    }

    do {
        TypedName param;
        param.name = consume(TokenKind::Ident, "expected parameter name").text;
        param.type = parseOptionalTypeAfterName(defaultType());
        params.push_back(std::move(param));
    } while (match(TokenKind::Comma));

    if (match(TokenKind::Ellipsis) && variadic_out != nullptr) {
        *variadic_out = true;
    }

    return params;
}

Type Parser::parseType() {
    if (match(TokenKind::Star)) {
        return Type::makePointer(parseType());
    }

    if (match(TokenKind::Array)) {
        return Type::makeArray(parseType());
    }

    const Token name = consume(TokenKind::Ident, "expected type name");
    Type type = Type::parse(name.text);
    if (type.kind == TypeKind::Struct) {
        if (findInterface(name.text) != nullptr) {
            type = Type::makeInterface(name.text);
        } else if (findStruct(name.text) == nullptr) {
            throw error("unknown type `" + name.text + "`");
        }
    }
    while (match(TokenKind::Star)) {
        type = Type::makePointer(type);
    }
    return type;
}

Type Parser::parseOptionalTypeAfterName(const Type& fallback) {
    if (!match(TokenKind::Colon)) {
        return fallback;
    }
    return parseType();
}

Block Parser::parseBlock() {
    const Span start = currentSpan();
    consume(TokenKind::LBrace, "expected '{'");

    Block block;
    block.span = start;
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        block.statements.push_back(parseStatement());
    }
    consume(TokenKind::RBrace, "expected '}'");
    return block;
}

Stmt Parser::parseStatement() {
    const Span span = currentSpan();

    if (match(TokenKind::Local)) {
        const Token name = consume(TokenKind::Ident, "expected variable name");
        Type type = parseOptionalTypeAfterName(defaultType());
        consume(TokenKind::Eq, "expected '='");
        auto value = parseExpr();
        consumeEndOfStatement();

        Stmt stmt;
        stmt.kind = Stmt::Kind::Local;
        stmt.span = span;
        stmt.name = name.text;
        stmt.type = type;
        stmt.expr = std::move(value);
        return stmt;
    }

    if (match(TokenKind::Return)) {
        std::unique_ptr<Expr> value;
        if (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace)) {
            value = parseExpr();
        }
        consumeEndOfStatement();

        Stmt stmt;
        stmt.kind = Stmt::Kind::Return;
        stmt.span = span;
        stmt.return_value = std::move(value);
        return stmt;
    }

    if (match(TokenKind::Go)) {
        const Token name = consume(TokenKind::Ident, "expected function name after go");
        consume(TokenKind::LParen, "expected '('");
        auto args = parseCallArgs();
        consume(TokenKind::RParen, "expected ')'");
        consumeEndOfStatement();

        auto bound = Expr::makeCall(name.text, std::move(args), span);
        std::vector<std::unique_ptr<Expr>> spawn_args;
        spawn_args.push_back(std::move(bound));

        Stmt stmt;
        stmt.kind = Stmt::Kind::Expr;
        stmt.span = span;
        stmt.expr = Expr::makeCall("spawn", std::move(spawn_args), span);
        return stmt;
    }

    if (match(TokenKind::If)) {
        auto condition = parseExpr();
        auto then_block = std::make_unique<Block>(parseBlock());
        std::unique_ptr<Block> else_block;
        if (match(TokenKind::Else)) {
            else_block = std::make_unique<Block>(parseBlock());
        }

        Stmt stmt;
        stmt.kind = Stmt::Kind::If;
        stmt.span = span;
        stmt.condition = std::move(condition);
        stmt.then_block = std::move(then_block);
        stmt.else_block = std::move(else_block);
        return stmt;
    }

    if (match(TokenKind::While)) {
        auto condition = parseExpr();
        auto loop_body = std::make_unique<Block>(parseBlock());

        Stmt stmt;
        stmt.kind = Stmt::Kind::While;
        stmt.span = span;
        stmt.condition = std::move(condition);
        stmt.loop_body = std::move(loop_body);
        return stmt;
    }

    if (match(TokenKind::Delete)) {
        auto value = parseExpr();
        consumeEndOfStatement();

        Stmt stmt;
        stmt.kind = Stmt::Kind::Delete;
        stmt.span = span;
        stmt.expr = std::move(value);
        return stmt;
    }

    if (check(TokenKind::Ident)) {
        auto target = parsePostfix(parsePrimary());
        if (match(TokenKind::Eq)) {
            auto value = parseExpr();
            consumeEndOfStatement();

            Stmt stmt;
            stmt.span = span;
            stmt.expr = std::move(value);
            if (target->kind == Expr::Kind::FieldAccess) {
                stmt.kind = Stmt::Kind::MemberAssign;
                stmt.target = std::move(target->object);
                stmt.field = target->name;
            } else if (target->kind == Expr::Kind::Index) {
                stmt.kind = Stmt::Kind::IndexAssign;
                stmt.index_target = std::move(target);
            } else if (target->kind == Expr::Kind::Variable) {
                stmt.kind = Stmt::Kind::Assign;
                stmt.name = target->name;
            } else {
                throw error("invalid assignment target");
            }
            return stmt;
        }

        Stmt stmt;
        stmt.kind = Stmt::Kind::Expr;
        stmt.span = span;
        stmt.expr = std::move(target);
        consumeEndOfStatement();
        return stmt;
    }

    auto expr = parseExpr();
    consumeEndOfStatement();

    Stmt stmt;
    stmt.kind = Stmt::Kind::Expr;
    stmt.span = span;
    stmt.expr = std::move(expr);
    return stmt;
}

void Parser::consumeEndOfStatement() {
    if (check(TokenKind::RBrace) || isAtEnd()) {
        return;
    }
    if (match(TokenKind::Semicolon)) {
        return;
    }
    if (check(TokenKind::Fn) || check(TokenKind::Local) || check(TokenKind::Return) ||
        check(TokenKind::Import) || check(TokenKind::From) || check(TokenKind::Ident) ||
        check(TokenKind::Export) || check(TokenKind::External) || check(TokenKind::Syscall) ||
        check(TokenKind::Declare) || check(TokenKind::Struct) || check(TokenKind::Delete) ||
        check(TokenKind::New) || check(TokenKind::If) || check(TokenKind::While) ||
        check(TokenKind::Go)) {
        return;
    }
    throw error("expected ';' or newline");
}

std::unique_ptr<Expr> Parser::parseExpr() { return parseLogicalOr(); }

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto left = parseLogicalAnd();
    while (match(TokenKind::OrOr)) {
        const Span span = left->span;
        auto right = parseLogicalAnd();
        left = Expr::makeBinary(BinOp::Or, std::move(left), std::move(right), span);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto left = parseEquality();
    while (match(TokenKind::AndAnd)) {
        const Span span = left->span;
        auto right = parseEquality();
        left = Expr::makeBinary(BinOp::And, std::move(left), std::move(right), span);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    auto left = parseComparison();
    while (true) {
        if (match(TokenKind::EqEq)) {
            const Span span = left->span;
            auto right = parseComparison();
            left = Expr::makeBinary(BinOp::Eq, std::move(left), std::move(right), span);
        } else if (match(TokenKind::NotEq)) {
            const Span span = left->span;
            auto right = parseComparison();
            left = Expr::makeBinary(BinOp::Ne, std::move(left), std::move(right), span);
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseComparison() {
    auto left = parseAdditive();
    while (true) {
        if (match(TokenKind::Lt)) {
            const Span span = left->span;
            auto right = parseAdditive();
            left = Expr::makeBinary(BinOp::Lt, std::move(left), std::move(right), span);
        } else if (match(TokenKind::Le)) {
            const Span span = left->span;
            auto right = parseAdditive();
            left = Expr::makeBinary(BinOp::Le, std::move(left), std::move(right), span);
        } else if (match(TokenKind::Gt)) {
            const Span span = left->span;
            auto right = parseAdditive();
            left = Expr::makeBinary(BinOp::Gt, std::move(left), std::move(right), span);
        } else if (match(TokenKind::Ge)) {
            const Span span = left->span;
            auto right = parseAdditive();
            left = Expr::makeBinary(BinOp::Ge, std::move(left), std::move(right), span);
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAdditive() {
    auto left = parseMultiplicative();
    while (true) {
        if (match(TokenKind::Plus)) {
            const Span span = left->span;
            auto right = parseMultiplicative();
            left = Expr::makeBinary(BinOp::Add, std::move(left), std::move(right), span);
        } else if (match(TokenKind::Minus)) {
            const Span span = left->span;
            auto right = parseMultiplicative();
            left = Expr::makeBinary(BinOp::Sub, std::move(left), std::move(right), span);
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseMultiplicative() {
    auto left = parseCast();
    while (true) {
        if (match(TokenKind::Star)) {
            const Span span = left->span;
            auto right = parseCast();
            left = Expr::makeBinary(BinOp::Mul, std::move(left), std::move(right), span);
        } else if (match(TokenKind::Slash)) {
            const Span span = left->span;
            auto right = parseCast();
            left = Expr::makeBinary(BinOp::Div, std::move(left), std::move(right), span);
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseCast() {
    auto expr = parsePostfix(parsePrimary());
    while (match(TokenKind::As)) {
        const Span span = expr->span;
        const Type target_type = parseType();
        expr = Expr::makeCast(std::move(expr), target_type, span);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> expr) {
    while (true) {
        if (match(TokenKind::Dot)) {
            const std::string field = consume(TokenKind::Ident, "expected field name").text;
            const Span span = expr->span;
            if (match(TokenKind::LParen)) {
                auto args = parseCallArgs();
                consume(TokenKind::RParen, "expected ')'");
                expr = Expr::makeMethodCall(std::move(expr), field, std::move(args), span);
                continue;
            }
            expr = Expr::makeFieldAccess(std::move(expr), field, span);
            continue;
        }
        if (match(TokenKind::LBracket)) {
            const Span span = expr->span;
            auto index = parseExpr();
            consume(TokenKind::RBracket, "expected ']'");
            expr = Expr::makeIndex(std::move(expr), std::move(index), span);
            continue;
        }
        break;
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    const Span span = currentSpan();

    if (match(TokenKind::Number)) {
        return Expr::makeInt(previous().number, span);
    }

    if (match(TokenKind::FloatNumber)) {
        return Expr::makeFloat(previous().float_number, span);
    }

    if (match(TokenKind::True)) {
        return Expr::makeBool(true, span);
    }

    if (match(TokenKind::False)) {
        return Expr::makeBool(false, span);
    }

    if (match(TokenKind::Null)) {
        return Expr::makeNull(span);
    }

    if (match(TokenKind::String)) {
        return Expr::makeString(previous().text, span);
    }

    if (match(TokenKind::LParen)) {
        auto expr = parseExpr();
        consume(TokenKind::RParen, "expected ')'");
        return expr;
    }

    if (match(TokenKind::New)) {
        return parseNewExpr(span);
    }

    if (match(TokenKind::Ident)) {
        const std::string name = previous().text;
        if (match(TokenKind::LParen)) {
            auto args = parseCallArgs();
            consume(TokenKind::RParen, "expected ')'");
            return Expr::makeCall(name, std::move(args), span);
        }
        if (check(TokenKind::Dot)) {
            return Expr::makeVar(name, span);
        }
        if (isFunctionName(name)) {
            return Expr::makeFunctionRef(name, span);
        }
        return Expr::makeVar(name, span);
    }

    throw error("expected expression");
}

std::unique_ptr<Expr> Parser::parseNewExpr(const Span& span) {
    if (match(TokenKind::Array)) {
        const Type element_type = parseType();
        return Expr::makeNewArray(element_type, span);
    }

    const std::string struct_name = consume(TokenKind::Ident, "expected struct name").text;
    if (findStruct(struct_name) == nullptr) {
        throw error("unknown struct `" + struct_name + "`");
    }

    std::vector<FieldInit> field_inits;
    if (match(TokenKind::LBrace)) {
        field_inits = parseFieldInits();
        consume(TokenKind::RBrace, "expected '}'");
    } else if (match(TokenKind::LParen)) {
        if (!check(TokenKind::RParen)) {
            do {
                FieldInit init;
                init.value = parseExpr();
                field_inits.push_back(std::move(init));
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RParen, "expected ')'");

        const StructDecl* decl = findStruct(struct_name);
        if (field_inits.size() > decl->fields.size()) {
            throw error("too many arguments for struct `" + struct_name + "`");
        }
        for (std::size_t i = 0; i < field_inits.size(); ++i) {
            field_inits[i].name = decl->fields[i].name;
        }
    } else {
        throw error("expected '{' or '(' after new struct name");
    }

    return Expr::makeNew(struct_name, std::move(field_inits), span);
}

std::vector<FieldInit> Parser::parseFieldInits() {
    std::vector<FieldInit> inits;
    if (check(TokenKind::RBrace)) {
        return inits;
    }

    do {
        FieldInit init;
        init.name = consume(TokenKind::Ident, "expected field name").text;
        consume(TokenKind::Eq, "expected '=' in field initializer");
        init.value = parseExpr();
        inits.push_back(std::move(init));
    } while (match(TokenKind::Comma));

    return inits;
}

std::vector<std::unique_ptr<Expr>> Parser::parseCallArgs() {
    std::vector<std::unique_ptr<Expr>> args;
    if (check(TokenKind::RParen)) {
        return args;
    }

    do {
        args.push_back(parseExpr());
    } while (match(TokenKind::Comma));

    return args;
}

const Token& Parser::peek() const { return tokens_[pos_]; }

const Token& Parser::previous() const { return tokens_[pos_ - 1]; }

bool Parser::isAtEnd() const { return peek().kind == TokenKind::End; }

bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

Token Parser::advance() {
    if (!isAtEnd()) {
        ++pos_;
    }
    return tokens_[pos_ - 1];
}

Token Parser::consume(TokenKind kind, const std::string& message) {
    if (check(kind)) {
        return advance();
    }
    throw error(message);
}

Span Parser::currentSpan() const {
    Span span;
    span.line = peek().line;
    span.column = peek().column;
    return span;
}

void Parser::registerFunction(const std::string& name) {
    function_names_.insert(name);
}

void Parser::registerStruct(const StructDecl& decl) {
    struct_defs_[decl.name] = decl;
}

void Parser::registerInterface(const InterfaceDecl& decl) {
    interface_defs_[decl.name] = decl;
}

const StructDecl* Parser::findStruct(const std::string& name) const {
    const auto it = struct_defs_.find(name);
    if (it == struct_defs_.end()) {
        return nullptr;
    }
    return &it->second;
}

const InterfaceDecl* Parser::findInterface(const std::string& name) const {
    const auto it = interface_defs_.find(name);
    if (it == interface_defs_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool Parser::isFunctionName(const std::string& name) const {
    return function_names_.find(name) != function_names_.end();
}

ParseError Parser::error(const std::string& message) const {
    return ParseError(peek().line, peek().column, message);
}

}  // namespace xlang
