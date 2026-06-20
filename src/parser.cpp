#include "xlang/parser.h"

namespace xlang {

Program parseSource(const std::string& source) {
    return Parser(tokenize(source)).parseProgram();
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

        if (check(TokenKind::Ident) && tokens_[pos_ + 1].kind == TokenKind::Eq) {
            program.globals.push_back(parseGlobalVar(modifiers));
            continue;
        }

        if (check(TokenKind::Ident) && tokens_[pos_ + 1].kind == TokenKind::Colon &&
            tokens_[pos_ + 2].kind == TokenKind::Ident &&
            tokens_[pos_ + 3].kind == TokenKind::Eq) {
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

ImportDecl Parser::parseImport() {
    ImportDecl decl;
    decl.span = currentSpan();

    if (match(TokenKind::Import)) {
        decl.is_from = false;
        decl.module = consume(TokenKind::Ident, "expected module name").text;
        if (match(TokenKind::As)) {
            decl.alias = consume(TokenKind::Ident, "expected alias name").text;
        }
        consumeEndOfStatement();
        return decl;
    }

    consume(TokenKind::From, "expected 'from'");
    decl.is_from = true;
    decl.module = consume(TokenKind::Ident, "expected module name").text;
    consume(TokenKind::Import, "expected 'import'");
    decl.names = parseImportNames();
    consumeEndOfStatement();
    return decl;
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

GlobalVar Parser::parseGlobalVar(const ItemModifiers& modifiers) {
    GlobalVar global;
    global.span = currentSpan();
    global.exported = modifiers.exported;
    global.external = modifiers.external;
    global.name = consume(TokenKind::Ident, "expected variable name").text;
    global.type = parseOptionalTypeAfterName(defaultType());
    consume(TokenKind::Eq, "expected '='");
    global.init = parseExpr();
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
    std::vector<TypedName> params = parseParams();
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
    std::vector<TypedName> params = parseParams();
    consume(TokenKind::RParen, "expected ')'");
    consumeEndOfStatement();

    Function function;
    function.name = name.text;
    function.params = std::move(params);
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
    std::vector<TypedName> params = parseParams();
    consume(TokenKind::RParen, "expected ')'");
    if (match(TokenKind::Colon)) {
        (void)parseType();
    }
    consumeEndOfStatement();

    Function function;
    function.name = name.text;
    function.params = std::move(params);
    function.external = true;
    function.span = start;
    registerFunction(function.name);
    (void)modifiers;
    return function;
}

std::vector<TypedName> Parser::parseParams() {
    std::vector<TypedName> params;
    if (check(TokenKind::RParen)) {
        return params;
    }

    do {
        TypedName param;
        param.name = consume(TokenKind::Ident, "expected parameter name").text;
        param.type = parseOptionalTypeAfterName(defaultType());
        params.push_back(std::move(param));
    } while (match(TokenKind::Comma));

    return params;
}

Type Parser::parseType() {
    if (match(TokenKind::Star)) {
        return Type::makePointer(parseType());
    }

    const Token name = consume(TokenKind::Ident, "expected type name");
    Type type = Type::parse(name.text);
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
        check(TokenKind::New)) {
        return;
    }
    throw error("expected ';' or newline");
}

std::unique_ptr<Expr> Parser::parseExpr() { return parseAdditive(); }

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
    auto left = parsePostfix(parsePrimary());
    while (true) {
        if (match(TokenKind::Star)) {
            const Span span = left->span;
            auto right = parsePostfix(parsePrimary());
            left = Expr::makeBinary(BinOp::Mul, std::move(left), std::move(right), span);
        } else if (match(TokenKind::Slash)) {
            const Span span = left->span;
            auto right = parsePostfix(parsePrimary());
            left = Expr::makeBinary(BinOp::Div, std::move(left), std::move(right), span);
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> expr) {
    while (match(TokenKind::Dot)) {
        const std::string field = consume(TokenKind::Ident, "expected field name").text;
        const Span span = expr->span;
        expr = Expr::makeFieldAccess(std::move(expr), field, span);
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
        if (isFunctionName(name)) {
            return Expr::makeFunctionRef(name, span);
        }
        return Expr::makeVar(name, span);
    }

    throw error("expected expression");
}

std::unique_ptr<Expr> Parser::parseNewExpr(const Span& span) {
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

const StructDecl* Parser::findStruct(const std::string& name) const {
    const auto it = struct_defs_.find(name);
    if (it == struct_defs_.end()) {
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
