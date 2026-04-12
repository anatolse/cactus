#include "frontend/parser.h"

#include <stdexcept>

namespace cactus {

Parser::Parser(std::vector<Token> tokens, ErrorReporter& errors)
    : tokens_(std::move(tokens)), errors_(errors) {}

// ── Token Navigation ────────────────────────────────────────────────────────

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::peek_next() const {
    if (current_ + 1 < tokens_.size()) {
        return tokens_[current_ + 1];
    }
    return tokens_.back();
}

Token Parser::advance() {
    auto tok = tokens_[current_];
    if (current_ < tokens_.size() - 1) {
        ++current_;
    }
    return tok;
}

Token Parser::consume(TokenType type, const std::string& msg) {
    if (check(type)) {
        return advance();
    }
    errors_.error(peek().location, msg + ", got " + token_type_to_string(peek().type));
    return peek();
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::is_synchronization_point() const {
    // Recovery boundaries:
    // - NEWLINE: end of the current statement/item
    // - DEDENT: exit from the current indented block
    // - EOF: stop at end of token stream
    // - declaration keywords: safe places to resume top-level parsing
    switch (peek().type) {
        case TokenType::NEWLINE:
        case TokenType::DEDENT:
        case TokenType::EOF_TOKEN:
        case TokenType::TRAIT:
        case TokenType::SYSTEM:
        case TokenType::FUNC:
        case TokenType::STRUCT:
        case TokenType::ENUM:
        case TokenType::EVENT:
        case TokenType::UNIT:
        case TokenType::TEMPLATE:
        case TokenType::VIEW:
        case TokenType::INTERFACE:
        case TokenType::ASSET:
        case TokenType::INPUT:
        case TokenType::MODULE:
        case TokenType::USE:
        case TokenType::CONST:
        case TokenType::PUB:
            return true;
        default:
            return false;
    }
}

void Parser::synchronize() {
    if (check(TokenType::EOF_TOKEN)) {
        return;
    }

    if (check(TokenType::NEWLINE)) {
        advance();
        skip_newlines();
        return;
    }

    while (!check(TokenType::EOF_TOKEN) && !is_synchronization_point()) {
        advance();
    }

    if (check(TokenType::NEWLINE)) {
        advance();
        skip_newlines();
    }
}

void Parser::skip_newlines() {
    while (check(TokenType::NEWLINE)) {
        advance();
    }
}

void Parser::expect_newline() {
    if (!match(TokenType::NEWLINE)) {
        // Allow EOF as implicit newline
        if (!check(TokenType::EOF_TOKEN) && !check(TokenType::DEDENT)) {
            errors_.error(peek().location, "expected newline");
        }
    }
}

void Parser::expect_indent() {
    skip_newlines();
    consume(TokenType::INDENT, "expected indented block");
}

void Parser::expect_dedent() {
    skip_newlines();
    if (!match(TokenType::DEDENT)) {
        if (!check(TokenType::EOF_TOKEN)) {
            errors_.error(peek().location, "expected dedent");
        }
    }
}

// ── Program ─────────────────────────────────────────────────────────────────

ProgramNode Parser::parse_program() {
    ProgramNode program;
    program.location = peek().location;
    skip_newlines();
    while (!check(TokenType::EOF_TOKEN)) {
        program.declarations.push_back(parse_declaration());
        skip_newlines();
    }
    return program;
}

Declaration Parser::parse_declaration() { // NOLINT(readability-function-cognitive-complexity)
    skip_newlines();
    const auto& tok = peek();

    if (tok.type == TokenType::MODULE) {
        return parse_module();
    }
    if (tok.type == TokenType::USE) {
        return parse_use();
    }
    if (tok.type == TokenType::CONST) {
        return parse_const_block();
    }
    if (tok.type == TokenType::STRUCT) {
        return parse_struct();
    }
    if (tok.type == TokenType::ENUM) {
        return parse_enum();
    }
    if (tok.type == TokenType::TRAIT) {
        return parse_trait();
    }
    if (tok.type == TokenType::SYSTEM) {
        return parse_system();
    }
    if (tok.type == TokenType::VIEW) {
        return parse_view();
    }
    if (tok.type == TokenType::EVENT) {
        return parse_event();
    }
    if (tok.type == TokenType::INTERFACE) {
        return parse_interface();
    }

    if (tok.type == TokenType::TEMPLATE) {
        return parse_template(false);
    }

    if (tok.type == TokenType::ASSET) {
        return parse_asset_decl(false);
    }
    if (tok.type == TokenType::INPUT) {
        return parse_input_decl(false);
    }

    if (tok.type == TokenType::PUB) {
        advance();
        skip_newlines();
        if (check(TokenType::TRAIT)) {
            auto t = parse_trait();
            t.is_pub = true;
            return t;
        }
        if (check(TokenType::UNIT)) {
            return parse_unit(true);
        }
        if (check(TokenType::TEMPLATE)) {
            return parse_template(true);
        }
        if (check(TokenType::FUNC)) {
            return parse_func(true);
        }
        if (check(TokenType::EXTERN)) {
            if (peek_next().type == TokenType::SYSTEM) {
                return parse_extern_system();
            }
            return parse_extern_func(true);
        }
        if (check(TokenType::ASSET)) {
            return parse_asset_decl(true);
        }
        if (check(TokenType::INPUT)) {
            return parse_input_decl(true);
        }
        if (check(TokenType::EVENT)) {
            return parse_event(true);
        }
        // pub enum / pub struct — visibility currently not tracked, parsed as module-private
        if (check(TokenType::ENUM)) {
            return parse_enum();
        }
        if (check(TokenType::STRUCT)) {
            return parse_struct();
        }
        errors_.error(peek().location, "expected trait, unit, template, func, extern func, asset, input, event, enum, or struct after 'pub'");
    }

    if (tok.type == TokenType::UNIT) {
        return parse_unit(false);
    }
    if (tok.type == TokenType::FUNC) {
        return parse_func(false);
    }
    if (tok.type == TokenType::EXTERN) {
        if (peek_next().type == TokenType::SYSTEM) {
            return parse_extern_system();
        }
        return parse_extern_func(false);
    }

    errors_.error(tok.location, "expected declaration (module, use, const, struct, enum, trait, unit, system, view, event, func, extern, interface, asset, input)");
    advance();  // skip bad token
    return ModuleNode{.name = "<error>", .location = tok.location};
}

// ── Helpers ─────────────────────────────────────────────────────────────────

std::string Parser::parse_dotted_name() {
    // Helper: consume an IDENTIFIER or any keyword token as a name segment.
    // This allows module paths that include keyword-named components (e.g. `std.input`).
    auto consume_name_segment = [&](const std::string& msg) -> std::string {
        if (check(TokenType::IDENTIFIER)) {
            return advance().value;
        }
        // Accept any keyword whose value is non-empty (e.g. INPUT -> "input")
        const auto& t = peek();
        if (t.type != TokenType::NEWLINE && t.type != TokenType::EOF_TOKEN &&
            t.type != TokenType::DOT && t.type != TokenType::COLON &&
            t.type != TokenType::INDENT && t.type != TokenType::DEDENT &&
            !t.value.empty()) {
            return advance().value;
        }
        errors_.error(t.location, msg + ", got " + token_type_to_string(t.type));
        return "<error>";
    };

    auto name = consume_name_segment("expected name");
    while (check(TokenType::DOT)) {
        advance();
        name += ".";
        name += consume_name_segment("expected name after '.'");
    }
    return name;
}

// ── Module & Use ────────────────────────────────────────────────────────────

ModuleNode Parser::parse_module() {
    auto loc = peek().location;
    consume(TokenType::MODULE, "expected 'module'");
    auto name = parse_dotted_name();
    expect_newline();
    return {.name = name, .location = loc};
}

UseNode Parser::parse_use() {
    auto loc = peek().location;
    consume(TokenType::USE, "expected 'use'");
    auto name = parse_dotted_name();
    std::optional<std::string> alias;
    if (match(TokenType::AS)) {
        alias = consume(TokenType::IDENTIFIER, "expected alias name").value;
    }
    expect_newline();
    return {.module_name = name, .alias = alias, .location = loc};
}

// ── Const Block ─────────────────────────────────────────────────────────────

ConstBlockNode Parser::parse_const_block() {
    auto loc = peek().location;
    consume(TokenType::CONST, "expected 'const'");
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    ConstBlockNode node;
    node.location = loc;

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto assign_loc = peek().location;
        auto name = consume(TokenType::IDENTIFIER, "expected constant name").value;
        consume(TokenType::ASSIGN, "expected '='");
        auto value = parse_expression();
        expect_newline();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        ConstAssignment assign;
        assign.name = name;
        assign.value = std::move(value);
        assign.location = assign_loc;
        node.assignments.push_back(std::move(assign));
    }

    expect_dedent();
    return node;
}

// ── Struct ──────────────────────────────────────────────────────────────────

StructNode Parser::parse_struct() {
    auto loc = peek().location;
    consume(TokenType::STRUCT, "expected 'struct'");
    auto name = consume(TokenType::IDENTIFIER, "expected struct name").value;
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    StructNode node;
    node.name = name;
    node.location = loc;

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        // Struct fields: name: type (no let/var modifiers)
        auto field_loc = peek().location;
        auto field_name = consume(TokenType::IDENTIFIER, "expected field name").value;
        consume(TokenType::COLON, "expected ':'");
        auto type = parse_type_ref();
        expect_newline();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        FieldNode field;
        field.name = field_name;
        field.type = std::move(type);
        field.location = field_loc;
        node.fields.push_back(std::move(field));
    }

    expect_dedent();
    return node;
}

// ── Enum ────────────────────────────────────────────────────────────────────

EnumNode Parser::parse_enum() {
    auto loc = peek().location;
    consume(TokenType::ENUM, "expected 'enum'");
    auto name = consume(TokenType::IDENTIFIER, "expected enum name").value;
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    EnumNode node;
    node.name = name;
    node.location = loc;

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto var_loc = peek().location;
        auto var_name = consume(TokenType::IDENTIFIER, "expected variant name").value;
        std::optional<int> val;
        if (match(TokenType::ASSIGN)) {
            val = std::stoi(consume(TokenType::INT_LITERAL, "expected integer value").value);
        }
        expect_newline();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        node.variants.push_back({.name = var_name, .value = val, .location = var_loc});
    }

    expect_dedent();
    return node;
}

// ── Trait ────────────────────────────────────────────────────────────────────

TraitNode Parser::parse_trait() {
    auto loc = peek().location;
    consume(TokenType::TRAIT, "expected 'trait'");
    auto name = consume(TokenType::IDENTIFIER, "expected trait name").value;

    TraitNode node;
    node.name = name;
    node.location = loc;

    // Marker trait: no colon, no body (e.g., `trait Persistent`)
    if (!check(TokenType::COLON)) {
        expect_newline();
        return node;
    }

    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        if (check(TokenType::ON)) {
            errors_.error(peek().location,
                "event handlers are not allowed in trait bodies; declare a system instead");
            parse_event_handler();  // consume to avoid cascading errors
        } else if (check(TokenType::FUNC)) {
            errors_.error(peek().location,
                "func declarations are not allowed in trait bodies; use a top-level func instead");
            parse_func(false);  // consume to avoid cascading errors
        } else {
            node.fields.push_back(parse_field());
        }
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    expect_dedent();
    return node;
}

// ── Field ───────────────────────────────────────────────────────────────────

FieldModifiers Parser::parse_field_modifiers() {
    FieldModifiers mods;
    while (true) {
        if (check(TokenType::PERSIST)) {
            advance();
            mods.is_persist = true;
        }
        else if (check(TokenType::SYNC)) {
            advance();
            mods.is_sync = true;
        }
        else if (check(TokenType::PUB)) {
            advance();
            mods.is_pub = true;
        } else {
            break;
        }
    }
    return mods;
}

FieldNode Parser::parse_field() {
    auto loc = peek().location;
    auto mods = parse_field_modifiers();

    if (check(TokenType::LET)) { advance(); mods.is_let = true; }
    else if (check(TokenType::VAR)) { advance(); mods.is_var = true; }
    else { errors_.error(peek().location, "expected 'let' or 'var'"); }

    auto name = consume(TokenType::IDENTIFIER, "expected field name").value;
    consume(TokenType::COLON, "expected ':'");
    auto type = parse_type_ref();

    std::optional<std::unique_ptr<ExprNode>> default_val;
    if (match(TokenType::ASSIGN)) {
        default_val = parse_expression();
    }
    expect_newline();

    FieldNode field;
    field.modifiers = mods;
    field.name = name;
    field.type = std::move(type);
    field.default_value = std::move(default_val);
    field.location = loc;
    return field;
}

namespace {
bool is_event_field_modifier_token(TokenType type) {
    switch (type) {
        case TokenType::LET:
        case TokenType::VAR:
        case TokenType::PERSIST:
        case TokenType::SYNC:
        case TokenType::PUB:
            return true;
        default:
            return false;
    }
}
}  // namespace

// ── Lifecycle Event Name Helper ─────────────────────────────────────────────

// Task 4.10: Accept keyword tokens as lifecycle event names
// (spawn/destroy/load/unload/fixed_tick/late_tick/input are keywords, not identifiers)
std::string Parser::parse_lifecycle_event_name() {
    switch (peek().type) {
        case TokenType::SPAWN:      { auto v = advance().value; return v; }
        case TokenType::DESTROY:    { auto v = advance().value; return v; }
        case TokenType::LOAD:       { auto v = advance().value; return v; }
        case TokenType::UNLOAD:     { auto v = advance().value; return v; }
        case TokenType::FIXED_TICK: { auto v = advance().value; return v; }
        case TokenType::LATE_TICK:  { auto v = advance().value; return v; }
        case TokenType::INPUT:      { auto v = advance().value; return v; }
        case TokenType::IDENTIFIER: return advance().value;
        default:
            errors_.error(peek().location, "expected event name");
            return "<error>";
    }
}

// ── Event Handler ───────────────────────────────────────────────────────────

EventHandlerNode Parser::parse_event_handler() {
    auto loc = peek().location;
    consume(TokenType::ON, "expected 'on'");
    // Accept lifecycle keywords (spawn/destroy/load/unload) as event names
    auto event_name = parse_lifecycle_event_name();

    // Optional 'as alias' clause
    std::optional<std::string> alias;
    if (match(TokenType::AS)) {
        alias = consume(TokenType::IDENTIFIER, "expected alias name after 'as'").value;
    }

    consume(TokenType::COLON, "expected ':'");
    expect_newline();

    auto body = parse_block();

    EventHandlerNode handler;
    handler.event_name = event_name;
    handler.alias = alias;
    handler.body = std::move(body);
    handler.location = loc;
    return handler;
}

// ── Type Reference ──────────────────────────────────────────────────────────

TypeRef Parser::parse_type_ref() {
    auto loc = peek().location;
    auto name = consume(TokenType::IDENTIFIER, "expected type name").value;
    TypeRef ref;
    ref.name = name;
    ref.location = loc;
    if (match(TokenType::LBRACKET)) {
        ref.param = std::make_unique<TypeRef>(parse_type_ref());
        consume(TokenType::RBRACKET, "expected ']'");
    }
    return ref;
}

// ── Parameters ──────────────────────────────────────────────────────────────

FuncParam Parser::parse_param() {
    auto loc = peek().location;
    auto name = consume(TokenType::IDENTIFIER, "expected parameter name").value;
    consume(TokenType::COLON, "expected ':'");
    auto type = parse_type_ref();
    return {.name = name, .type = std::move(type), .location = loc};
}

std::vector<FuncParam> Parser::parse_param_list() {
    std::vector<FuncParam> params;
    if (!check(TokenType::RPAREN)) {
        params.push_back(parse_param());
        while (match(TokenType::COMMA)) {
            params.push_back(parse_param());
        }
    }
    return params;
}

FieldAssignment Parser::parse_field_assignment() {
    auto loc = peek().location;
    auto name = consume(TokenType::IDENTIFIER, "expected field name").value;
    consume(TokenType::ASSIGN, "expected '='");
    auto value = parse_expression();
    expect_newline();
    FieldAssignment assign;
    assign.name = name;
    assign.value = std::move(value);
    assign.location = loc;
    return assign;
}

std::vector<FieldAssignment> Parser::parse_field_assignment_block() {
    expect_newline();
    expect_indent();
    std::vector<FieldAssignment> assignments;
    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto assignment = parse_field_assignment();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        assignments.push_back(std::move(assignment));
    }
    expect_dedent();
    return assignments;
}

ArchetypeTraitEntry Parser::parse_archetype_trait_entry() {
    auto loc = peek().location;
    auto trait_name = consume(TokenType::IDENTIFIER, "expected trait name").value;

    ArchetypeTraitEntry entry;
    entry.trait_name = trait_name;
    entry.location = loc;

    if (check(TokenType::COLON)) {
        advance();
        entry.assignments = parse_field_assignment_block();
        return entry;
    }

    expect_newline();
    return entry;
}

std::vector<ArchetypeTraitEntry> Parser::parse_archetype_trait_entries() {
    std::vector<ArchetypeTraitEntry> entries;
    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto entry = parse_archetype_trait_entry();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<ArchetypeTraitEntry> Parser::parse_archetype_trait_entry_block() {
    expect_newline();
    expect_indent();
    auto entries = parse_archetype_trait_entries();
    expect_dedent();
    return entries;
}

// ── Unit ────────────────────────────────────────────────────────────────────

UnitNode Parser::parse_unit(bool is_pub) {
    auto loc = peek().location;
    consume(TokenType::UNIT, "expected 'unit'");
    auto name = consume(TokenType::IDENTIFIER, "expected unit name").value;
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    UnitNode node;
    node.name = name;
    node.is_pub = is_pub;
    node.location = loc;

    node.traits = parse_archetype_trait_entries();

    skip_newlines();
    expect_dedent();
    return node;
}

// Task 4.1: Template declaration parser
TemplateNode Parser::parse_template(bool is_pub) {
    auto loc = peek().location;
    consume(TokenType::TEMPLATE, "expected 'template'");
    auto name = consume(TokenType::IDENTIFIER, "expected template name").value;
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    TemplateNode node;
    node.name = name;
    node.is_pub = is_pub;
    node.location = loc;

    node.traits = parse_archetype_trait_entries();

    skip_newlines();
    expect_dedent();
    return node;
}

// ── System ──────────────────────────────────────────────────────────────────

SystemNode Parser::parse_system() { // NOLINT(readability-function-cognitive-complexity)
    auto loc = peek().location;
    consume(TokenType::SYSTEM, "expected 'system'");
    auto name = consume(TokenType::IDENTIFIER, "expected system name").value;
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    SystemNode node;
    node.name = name;
    node.location = loc;

    skip_newlines();
    if (check(TokenType::FILTER)) {
        auto error_count_before = errors_.error_count();
        node.filter = parse_filter_clause();
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    // Task 4.4: Parse optional exclude: clause
    skip_newlines();
    if (check(TokenType::EXCLUDE)) {
        auto error_count_before = errors_.error_count();
        node.exclude = parse_exclude_clause();
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    skip_newlines();
    if (check(TokenType::IDENTIFIER) && peek().value == "order" &&
        peek_next().type == TokenType::IDENTIFIER && peek_next().value == "by") {
        auto error_count_before = errors_.error_count();
        node.order_by = parse_order_by_clause();
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    // Parse optional after: clause (block format: AFTER COLON NEWLINE INDENT { IDENT NEWLINE } DEDENT)
    skip_newlines();
    if (check(TokenType::AFTER)) {
        auto after_loc = peek().location;
        auto error_count_before = errors_.error_count();
        advance();  // consume 'after'
        consume(TokenType::COLON, "expected ':'");
        expect_newline();
        expect_indent();
        bool any = false;
        while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
            skip_newlines();
            if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
                break;
            }
            node.after_systems.push_back(
                consume(TokenType::IDENTIFIER, "expected system name in after: block").value);
            expect_newline();
            any = true;
            if (errors_.error_count() > error_count_before) {
                synchronize();
                break;
            }
        }
        expect_dedent();
        if (!any) {
            errors_.error(after_loc, "after: block must contain at least one system name");
        }
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    skip_newlines();
    if (check(TokenType::TARGET)) {
        advance();
        consume(TokenType::COLON, "expected ':'");
        node.target = consume(TokenType::IDENTIFIER, "expected 'cpu' or 'gpu'").value;
        expect_newline();
    }

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto handler = parse_event_handler();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        node.handlers.push_back(std::move(handler));
    }

    expect_dedent();
    return node;
}

FilterClause Parser::parse_filter_clause() {
    auto loc = peek().location;
    consume(TokenType::FILTER, "expected 'filter'");
    consume(TokenType::COLON, "expected ':'");

    FilterClause clause;
    clause.location = loc;

    // Block syntax (consistent with exclude: and after:)
    expect_newline();
    expect_indent();
    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto entry_loc = peek().location;
        auto qname = parse_dotted_name();
        std::optional<std::string> alias;
        if (match(TokenType::AS)) {
            alias = consume(TokenType::IDENTIFIER, "expected alias name").value;
        }
        clause.entries.push_back({.qualified_name = qname, .alias = alias, .location = entry_loc});
        auto dot_pos = qname.rfind('.');
        clause.trait_names.push_back(dot_pos != std::string::npos ? qname.substr(dot_pos + 1) : qname);
        expect_newline();
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }
    expect_dedent();
    return clause;
}

std::vector<SortKey> Parser::parse_order_by_clause() {
    auto loc = peek().location;
    auto order_kw = consume(TokenType::IDENTIFIER, "expected 'order'");
    if (order_kw.value != "order") {
        errors_.error(order_kw.location, "expected 'order by:'");
    }
    auto by_kw = consume(TokenType::IDENTIFIER, "expected 'by' after 'order'");
    if (by_kw.value != "by") {
        errors_.error(by_kw.location, "expected 'by' after 'order'");
    }
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    std::vector<SortKey> keys;
    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }

        auto key_loc = peek().location;
        auto alias = consume(TokenType::IDENTIFIER, "expected sort key alias").value;
        consume(TokenType::DOT, "expected '.' after sort key alias");
        auto field = consume(TokenType::IDENTIFIER, "expected sort key field").value;
        while (match(TokenType::DOT)) {
            field += ".";
            field += consume(TokenType::IDENTIFIER, "expected field member after '.'").value;
        }

        bool descending = false;
        if (check(TokenType::IDENTIFIER)) {
            std::string direction = advance().value;
            if (direction == "asc") {
                descending = false;
            } else if (direction == "desc") {
                descending = true;
            } else {
                errors_.error(key_loc, "expected 'asc' or 'desc' in order by clause");
            }
        }
        expect_newline();
        keys.push_back({.alias = alias, .field = field, .descending = descending, .location = key_loc});
    }

    expect_dedent();
    if (keys.empty()) {
        errors_.error(loc, "order by: block must contain at least one sort key");
    }
    return keys;
}

// ── View ────────────────────────────────────────────────────────────────────

ViewNode Parser::parse_view() {
    auto loc = peek().location;
    consume(TokenType::VIEW, "expected 'view'");
    auto name = consume(TokenType::IDENTIFIER, "expected view name").value;
    consume(TokenType::LPAREN, "expected '('");
    auto params = parse_param_list();
    consume(TokenType::RPAREN, "expected ')'");
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    ViewNode node;
    node.name = name;
    node.params = std::move(params);
    node.location = loc;

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto element = parse_view_element();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        node.elements.push_back(std::move(element));
    }

    expect_dedent();
    return node;
}

ViewElement Parser::parse_view_element() {
    auto loc = peek().location;
    auto tag = consume(TokenType::IDENTIFIER, "expected element tag").value;
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    ViewElement elem;
    elem.tag_name = tag;
    elem.location = loc;

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();

        // Check if this is a nested element (identifier followed by colon then newline)
        // or a property (identifier = expression)
        if (check(TokenType::IDENTIFIER) && peek_next().type == TokenType::COLON) {
            // Could be nested element or property with type — peek further
            // If after COLON there's NEWLINE, it's a nested element
            // Save position and check
            auto saved = current_;
            advance();  // skip identifier
            advance();  // skip colon
            if (check(TokenType::NEWLINE) || check(TokenType::INDENT)) {
                current_ = saved;
                elem.children.push_back(parse_view_element());
            } else {
                current_ = saved;
                // It's a property: name = expr
                auto prop_loc = peek().location;
                auto prop_name = advance().value;
                consume(TokenType::ASSIGN, "expected '='");
                auto value = parse_expression();
                expect_newline();
                FieldAssignment prop;
                prop.name = prop_name;
                prop.value = std::move(value);
                prop.location = prop_loc;
                elem.props.push_back(std::move(prop));
            }
        } else if (check(TokenType::IDENTIFIER) && peek_next().type == TokenType::ASSIGN) {
            auto prop_loc = peek().location;
            auto prop_name = advance().value;
            consume(TokenType::ASSIGN, "expected '='");
            auto value = parse_expression();
            expect_newline();
            FieldAssignment prop;
            prop.name = prop_name;
            prop.value = std::move(value);
            prop.location = prop_loc;
            elem.props.push_back(std::move(prop));
        } else {
            errors_.error(peek().location, "expected property or nested element in view");
        }

        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
    }

    expect_dedent();
    return elem;
}

// ── Event Declaration ───────────────────────────────────────────────────────

EventNode Parser::parse_event(bool is_pub) {
    auto loc = peek().location;
    consume(TokenType::EVENT, "expected 'event'");
    auto name = parse_lifecycle_event_name();

    EventNode node;
    node.name = name;
    node.is_pub = is_pub;
    node.location = loc;

    // Marker event: no colon, no body (e.g., `pub event spawn`)
    if (!check(TokenType::COLON)) {
        expect_newline();
        return node;
    }

    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto field_loc = peek().location;
        if (is_event_field_modifier_token(peek().type)) {
            errors_.error(peek().location,
                          "event fields use bare `name: type` syntax; trait field modifiers are not allowed in event declarations");
            while (is_event_field_modifier_token(peek().type)) {
                advance();
            }
        }

        auto field_name = consume(TokenType::IDENTIFIER, "expected event field name").value;
        consume(TokenType::COLON, "expected ':'");
        auto field_type = parse_type_ref();

        std::optional<std::unique_ptr<ExprNode>> default_val;
        if (match(TokenType::ASSIGN)) {
            default_val = parse_expression();
        }
        expect_newline();

        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        FieldNode field;
        field.name = field_name;
        field.type = std::move(field_type);
        field.default_value = std::move(default_val);
        field.location = field_loc;
        node.fields.push_back(std::move(field));
    }

    expect_dedent();
    return node;
}

// ── Func ────────────────────────────────────────────────────────────────────

FuncNode Parser::parse_func(bool is_pub) {
    auto loc = peek().location;
    consume(TokenType::FUNC, "expected 'func'");
    auto name = consume(TokenType::IDENTIFIER, "expected function name").value;
    consume(TokenType::LPAREN, "expected '('");
    auto params = parse_param_list();
    consume(TokenType::RPAREN, "expected ')'");

    std::optional<TypeRef> return_type;
    // Task 8.1/8.2: Return type follows ')' directly — no arrow.
    // Produce an error if old '->' syntax is used.
    if (check(TokenType::ARROW)) {
        errors_.error(peek().location, "unexpected '->'; return type follows ')' directly");
        advance();  // consume and skip the arrow
    }
    if (check(TokenType::IDENTIFIER)) {
        return_type = parse_type_ref();
    }

    consume(TokenType::COLON, "expected ':'");
    expect_newline();

    auto body = parse_block();

    FuncNode node;
    node.name = name;
    node.is_pub = is_pub;
    node.params = std::move(params);
    node.return_type = std::move(return_type);
    node.body = std::move(body);
    node.location = loc;
    return node;
}

// ── Extern Func ──────────────────────────────────────────────────────────────

FuncNode Parser::parse_extern_func(bool is_pub) {
    auto loc = peek().location;
    consume(TokenType::EXTERN, "expected 'extern'");
    consume(TokenType::FUNC, "expected 'func' after 'extern'");
    auto name = consume(TokenType::IDENTIFIER, "expected function name").value;
    consume(TokenType::LPAREN, "expected '('");
    auto params = parse_param_list();
    consume(TokenType::RPAREN, "expected ')'");

    // Return type directly follows ')', no arrow, no colon, no body
    std::optional<TypeRef> return_type;
    if (check(TokenType::ARROW)) {
        errors_.error(peek().location, "unexpected '->'; return type follows ')' directly");
        advance();  // consume and skip the arrow
    }
    if (check(TokenType::IDENTIFIER)) {
        return_type = parse_type_ref();
    }

    expect_newline();

    FuncNode node;
    node.name = name;
    node.is_pub = is_pub;
    node.is_extern = true;
    node.params = std::move(params);
    node.return_type = std::move(return_type);
    // body intentionally empty for extern
    node.location = loc;
    return node;
}

// ── Interface ───────────────────────────────────────────────────────────────

InterfaceNode Parser::parse_interface() {
    auto loc = peek().location;
    consume(TokenType::INTERFACE, "expected 'interface'");
    auto name = consume(TokenType::IDENTIFIER, "expected interface name").value;
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    InterfaceNode node;
    node.name = name;
    node.location = loc;

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto sig_loc = peek().location;
        consume(TokenType::FUNC, "expected 'func'");
        auto sig_name = consume(TokenType::IDENTIFIER, "expected method name").value;
        consume(TokenType::LPAREN, "expected '('");
        auto sig_params = parse_param_list();
        consume(TokenType::RPAREN, "expected ')'");
        std::optional<TypeRef> ret;
        if (match(TokenType::ARROW)) {
            ret = parse_type_ref();
        }
        expect_newline();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        node.methods.push_back(
            {.name = sig_name, .params = std::move(sig_params), .return_type = std::move(ret), .location = sig_loc});
    }

    expect_dedent();
    return node;
}

// ── Statements ──────────────────────────────────────────────────────────────

std::vector<std::unique_ptr<StmtNode>> Parser::parse_block() {
    expect_indent();
    std::vector<std::unique_ptr<StmtNode>> stmts;
    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto stmt = parse_statement();
        if (errors_.error_count() > error_count_before) {
            synchronize();
            continue;
        }
        stmts.push_back(std::move(stmt));
    }
    expect_dedent();
    return stmts;
}

AddTraitStmt Parser::parse_add_trait_stmt() {
    auto loc = peek().location;
    consume(TokenType::ADD, "expected 'add'");
    auto trait_name = consume(TokenType::IDENTIFIER, "expected trait name").value;

    AddTraitStmt stmt;
    stmt.trait_name = trait_name;
    stmt.location = loc;

    if (match(TokenType::TO)) {
        stmt.target_expr = parse_expression();
    }

    if (check(TokenType::COLON)) {
        advance();
        stmt.args = parse_field_assignment_block();
    } else {
        expect_newline();
    }

    return stmt;
}

RemoveTraitStmt Parser::parse_remove_trait_stmt() {
    auto loc = peek().location;
    consume(TokenType::REMOVE, "expected 'remove'");
    auto trait_name = consume(TokenType::IDENTIFIER, "expected trait name").value;

    RemoveTraitStmt stmt;
    stmt.trait_name = trait_name;
    stmt.location = loc;

    if (match(TokenType::FROM)) {
        stmt.target_expr = parse_expression();
    }

    expect_newline();
    return stmt;
}

TraitMatchStmt Parser::parse_trait_match_stmt() {
    auto loc = peek().location;
    consume(TokenType::MATCH, "expected 'match'");

    TraitMatchStmt stmt;
    stmt.location = loc;
    stmt.subject = parse_expression();

    consume(TokenType::COLON, "expected ':' after match subject");
    expect_newline();
    expect_indent();

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }

        auto arm_loc = peek().location;
        if (check(TokenType::IDENTIFIER) && peek().value == "_") {
            advance();
            consume(TokenType::FAT_ARROW, "expected '=>' after '_' in wildcard arm");
            expect_newline();

            WildcardMatchArm wildcard;
            wildcard.location = arm_loc;
            wildcard.body = parse_block();
            stmt.wildcard = std::move(wildcard);
            skip_newlines();
            if (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
                errors_.error(peek().location, "wildcard arm `_ =>` must be the last arm in a trait match");
            }
            continue;
        }

        TraitMatchArm arm;
        arm.location = arm_loc;
        arm.trait_name = consume(TokenType::IDENTIFIER, "expected trait name or '_' in match arm").value;
        if (match(TokenType::AS)) {
            arm.alias = consume(TokenType::IDENTIFIER, "expected alias name after 'as'").value;
        }
        consume(TokenType::FAT_ARROW, "expected '=>' after trait match arm");
        expect_newline();
        arm.body = parse_block();
        stmt.arms.push_back(std::move(arm));
    }

    expect_dedent();
    return stmt;
}

std::unique_ptr<StmtNode> Parser::parse_statement() { // NOLINT(readability-function-cognitive-complexity)
    auto loc = peek().location;

    if (check(TokenType::LET)) {
        advance();
        auto name = consume(TokenType::IDENTIFIER, "expected binding name").value;
        consume(TokenType::ASSIGN, "expected '='");
        auto value = parse_expression();
        expect_newline();
        LetStmt let_stmt;
        let_stmt.name = name;
        let_stmt.value = std::move(value);
        let_stmt.location = loc;
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(let_stmt)}, loc);
    }

    // emit statement
    if (check(TokenType::EMIT)) {
        advance();
        auto event_name = consume(TokenType::IDENTIFIER, "expected event name").value;
        std::optional<std::unique_ptr<ExprNode>> target;
        if (match(TokenType::TO)) {
            target = parse_expression();
        }
        consume(TokenType::COLON, "expected ':'");
        EmitStmt emit;
        emit.event_name = event_name;
        emit.target = std::move(target);
        emit.payload = parse_field_assignment_block();
        emit.location = loc;
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(emit)}, loc);
    }

    // return statement
    if (check(TokenType::RETURN)) {
        advance();
        std::optional<std::unique_ptr<ExprNode>> value;
        if (!check(TokenType::NEWLINE) && !check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
            value = parse_expression();
        }
        expect_newline();
        ReturnStmt ret;
        ret.value = std::move(value);
        ret.location = loc;
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(ret)}, loc);
    }

    // if statement
    if (check(TokenType::IF)) {
        advance();
        auto condition = parse_expression();
        consume(TokenType::COLON, "expected ':'");

        // Check if inline (expression follows on same line) or block (newline + indent)
        if (check(TokenType::NEWLINE) || check(TokenType::INDENT)) {
            expect_newline();
            auto then_body = parse_block();
            std::vector<std::unique_ptr<StmtNode>> else_body;
            skip_newlines();
            if (match(TokenType::ELSE)) {
                consume(TokenType::COLON, "expected ':'");
                expect_newline();
                else_body = parse_block();
            }
            IfStmt if_stmt;
            if_stmt.condition = std::move(condition);
            if_stmt.then_body = std::move(then_body);
            if_stmt.else_body = std::move(else_body);
            if_stmt.location = loc;
            return std::make_unique<StmtNode>(StmtNode::Variant{std::move(if_stmt)}, loc);
        }
        // Inline if: if cond: stmt
        auto inner = parse_statement();
        std::vector<std::unique_ptr<StmtNode>> then_body;
        then_body.push_back(std::move(inner));
        IfStmt if_stmt;
        if_stmt.condition = std::move(condition);
        if_stmt.then_body = std::move(then_body);
        if_stmt.location  = loc;
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(if_stmt)}, loc);
    }

    if (check(TokenType::MATCH)) {
        auto match_stmt = parse_trait_match_stmt();
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(match_stmt)}, loc);
    }

    // Task 4.5-4.6: spawn statement — spawn TemplateName(field = expr, ...)
    if (check(TokenType::SPAWN)) {
        advance();
        auto template_name = consume(TokenType::IDENTIFIER, "expected template name").value;
        SpawnStmt spawn_stmt;
        spawn_stmt.template_name = template_name;
        spawn_stmt.location = loc;
        consume(TokenType::COLON, "expected ':'");
        spawn_stmt.overrides = parse_archetype_trait_entry_block();
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(spawn_stmt)}, loc);
    }

    // Task 4.7: destroy statement
    if (check(TokenType::DESTROY)) {
        advance();
        DestroyStmt destroy;
        if (!check(TokenType::NEWLINE)) {
            destroy.target_expr = parse_expression();
        }
        expect_newline();
        destroy.location = loc;
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(destroy)}, loc);
    }

    // Task 4.8: load statement — load module.name
    if (check(TokenType::LOAD)) {
        advance();
        auto module_name = parse_dotted_name();
        expect_newline();
        LoadStmt load;
        load.module_name = module_name;
        load.location = loc;
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(load)}, loc);
    }

    if (check(TokenType::ADD)) {
        auto add = parse_add_trait_stmt();
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(add)}, loc);
    }

    if (check(TokenType::REMOVE)) {
        auto remove = parse_remove_trait_stmt();
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(remove)}, loc);
    }

    // Assignment or expression statement
    // Check for: IDENTIFIER (= | += | -=) expression
    if (check(TokenType::IDENTIFIER) &&
        (peek_next().type == TokenType::ASSIGN || peek_next().type == TokenType::PLUS_ASSIGN ||
         peek_next().type == TokenType::MINUS_ASSIGN)) {
        auto name = advance().value;
        auto op = advance().value;
        auto value = parse_expression();
        expect_newline();
        VarAssign assign;
        assign.name = name;
        assign.op = op;
        assign.value = std::move(value);
        assign.location = loc;
        return std::make_unique<StmtNode>(StmtNode::Variant{std::move(assign)}, loc);
    }

    // Expression statement
    auto expr = parse_expression();
    expect_newline();
    ExprStmt expr_stmt;
    expr_stmt.expr = std::move(expr);
    expr_stmt.location = loc;
    return std::make_unique<StmtNode>(StmtNode::Variant{std::move(expr_stmt)}, loc);
}

// ── Expressions ─────────────────────────────────────────────────────────────

std::unique_ptr<ExprNode> Parser::parse_expression() {
    return parse_or_expr();
}

std::unique_ptr<ExprNode> Parser::parse_or_expr() {
    auto left = parse_and_expr();
    while (check(TokenType::OR)) {
        auto loc = peek().location;
        advance();
        auto right = parse_and_expr();
        BinaryExpr bin;
        bin.op = "or";
        bin.left = std::move(left);
        bin.right = std::move(right);
        bin.location = loc;
        left = std::make_unique<ExprNode>(ExprNode::Variant{std::move(bin)}, loc);
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_and_expr() {
    auto left = parse_equality_expr();
    while (check(TokenType::AND)) {
        auto loc = peek().location;
        advance();
        auto right = parse_equality_expr();
        BinaryExpr bin;
        bin.op = "and";
        bin.left = std::move(left);
        bin.right = std::move(right);
        bin.location = loc;
        left = std::make_unique<ExprNode>(ExprNode::Variant{std::move(bin)}, loc);
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_equality_expr() {
    auto left = parse_comparison_expr();
    while (check(TokenType::EQUALS) || check(TokenType::NOT_EQUALS)) {
        auto loc = peek().location;
        auto op = advance().value;
        auto right = parse_comparison_expr();
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::move(left);
        bin.right = std::move(right);
        bin.location = loc;
        left = std::make_unique<ExprNode>(ExprNode::Variant{std::move(bin)}, loc);
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_comparison_expr() {
    auto left = parse_additive_expr();
    while (check(TokenType::LESS) || check(TokenType::GREATER) || check(TokenType::LESS_EQ) ||
           check(TokenType::GREATER_EQ)) {
        auto loc = peek().location;
        auto op = advance().value;
        auto right = parse_additive_expr();
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::move(left);
        bin.right = std::move(right);
        bin.location = loc;
        left = std::make_unique<ExprNode>(ExprNode::Variant{std::move(bin)}, loc);
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_additive_expr() {
    auto left = parse_multiplicative_expr();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        auto loc = peek().location;
        auto op = advance().value;
        auto right = parse_multiplicative_expr();
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::move(left);
        bin.right = std::move(right);
        bin.location = loc;
        left = std::make_unique<ExprNode>(ExprNode::Variant{std::move(bin)}, loc);
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_multiplicative_expr() {
    auto left = parse_unary_expr();
    while (check(TokenType::STAR) || check(TokenType::SLASH) || check(TokenType::PERCENT)) {
        auto loc = peek().location;
        auto op = advance().value;
        auto right = parse_unary_expr();
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::move(left);
        bin.right = std::move(right);
        bin.location = loc;
        left = std::make_unique<ExprNode>(ExprNode::Variant{std::move(bin)}, loc);
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_unary_expr() {
    if (check(TokenType::NOT)) {
        auto loc = peek().location;
        advance();
        auto operand = parse_unary_expr();
        UnaryExpr un;
        un.op = "not";
        un.operand = std::move(operand);
        un.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(un)}, loc);
    }
    if (check(TokenType::MINUS)) {
        auto loc = peek().location;
        advance();
        auto operand = parse_unary_expr();
        UnaryExpr un;
        un.op = "-";
        un.operand = std::move(operand);
        un.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(un)}, loc);
    }
    return parse_postfix_expr();
}

std::unique_ptr<ExprNode> Parser::parse_postfix_expr() {
    auto expr = parse_primary_expr();

    while (true) {
        if (check(TokenType::DOT)) {
            auto loc = peek().location;
            advance();
            auto member = consume(TokenType::IDENTIFIER, "expected member name").value;

            // Check for method call: .member(args)
            if (check(TokenType::LPAREN)) {
                advance();
                std::vector<std::unique_ptr<ExprNode>> args;
                if (!check(TokenType::RPAREN)) {
                    args.push_back(parse_expression());
                    while (match(TokenType::COMMA)) {
                        args.push_back(parse_expression());
                    }
                }
                consume(TokenType::RPAREN, "expected ')'");
                CallExpr call;
                MemberExpr mem;
                mem.object = std::move(expr);
                mem.member = member;
                mem.location = loc;
                call.callee = std::make_unique<ExprNode>(ExprNode::Variant{std::move(mem)}, loc);
                call.args = std::move(args);
                call.location = loc;
                expr = std::make_unique<ExprNode>(ExprNode::Variant{std::move(call)}, loc);
            } else {
                MemberExpr mem;
                mem.object = std::move(expr);
                mem.member = member;
                mem.location = loc;
                expr = std::make_unique<ExprNode>(ExprNode::Variant{std::move(mem)}, loc);
            }
        } else if (check(TokenType::LPAREN)) {
            auto loc = peek().location;
            advance();
            std::vector<std::unique_ptr<ExprNode>> args;
            if (!check(TokenType::RPAREN)) {
                args.push_back(parse_expression());
                while (match(TokenType::COMMA)) {
                    args.push_back(parse_expression());
                }
            }
            consume(TokenType::RPAREN, "expected ')'");
            CallExpr call;
            call.callee = std::move(expr);
            call.args = std::move(args);
            call.location = loc;
            expr = std::make_unique<ExprNode>(ExprNode::Variant{std::move(call)}, loc);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::parse_primary_expr() { // NOLINT(readability-function-cognitive-complexity)
    auto loc = peek().location;

    if (check(TokenType::SPAWN)) {
        advance();
        auto template_name = consume(TokenType::IDENTIFIER, "expected template name").value;
        consume(TokenType::COLON, "expected ':'");
        SpawnExpr spawn;
        spawn.template_name = template_name;
        spawn.overrides = parse_archetype_trait_entry_block();
        spawn.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(spawn)}, loc);
    }

    // Integer literal
    if (check(TokenType::INT_LITERAL)) {
        auto val = advance().value;
        LiteralExpr lit;
        lit.kind = LiteralExpr::Kind::Int;
        lit.value = val;
        lit.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(lit)}, loc);
    }

    // Float literal
    if (check(TokenType::FLOAT_LITERAL)) {
        auto val = advance().value;
        LiteralExpr lit;
        lit.kind = LiteralExpr::Kind::Float;
        lit.value = val;
        lit.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(lit)}, loc);
    }

    // String literal
    if (check(TokenType::STRING_LITERAL)) {
        auto val = advance().value;
        LiteralExpr lit;
        lit.kind = LiteralExpr::Kind::String;
        lit.value = val;
        lit.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(lit)}, loc);
    }

    // Hex color
    if (check(TokenType::HEX_COLOR)) {
        auto val = advance().value;
        LiteralExpr lit;
        lit.kind = LiteralExpr::Kind::HexColor;
        lit.value = val;
        lit.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(lit)}, loc);
    }

    // Boolean literals
    if (check(TokenType::TRUE_LIT)) {
        advance();
        LiteralExpr lit;
        lit.kind = LiteralExpr::Kind::Bool;
        lit.value = "true";
        lit.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(lit)}, loc);
    }
    if (check(TokenType::FALSE_LIT)) {
        advance();
        LiteralExpr lit;
        lit.kind = LiteralExpr::Kind::Bool;
        lit.value = "false";
        lit.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(lit)}, loc);
    }

    // Parenthesized expression
    if (check(TokenType::LPAREN)) {
        advance();
        auto expr = parse_expression();
        consume(TokenType::RPAREN, "expected ')'");
        return expr;
    }

    // List literal
    if (check(TokenType::LBRACKET)) {
        advance();
        ListExpr list;
        list.location = loc;
        if (!check(TokenType::RBRACKET)) {
            list.elements.push_back(parse_expression());
            while (match(TokenType::COMMA)) {
                list.elements.push_back(parse_expression());
            }
        }
        consume(TokenType::RBRACKET, "expected ']'");
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(list)}, loc);
    }

    // `input` keyword used as built-in object in expressions (e.g. input.axis(MoveX))
    if (check(TokenType::INPUT)) {
        auto name = advance().value;  // "input"
        IdentExpr ident;
        ident.name = name;
        ident.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(ident)}, loc);
    }

    if (check(TokenType::SELF)) {
        advance();
        SelfExpr self_expr;
        self_expr.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(self_expr)}, loc);
    }

    // Identifier — possibly lambda (ident => expr)
    if (check(TokenType::IDENTIFIER)) {
        auto name = advance().value;

        // Lambda: ident => expr
        if (check(TokenType::FAT_ARROW)) {
            advance();
            auto body = parse_expression();
            LambdaExpr lambda;
            lambda.params = {name};
            lambda.body = std::move(body);
            lambda.location = loc;
            return std::make_unique<ExprNode>(ExprNode::Variant{std::move(lambda)}, loc);
        }

        IdentExpr ident;
        ident.name = name;
        ident.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(ident)}, loc);
    }

    // Match expression
    if (check(TokenType::MATCH)) {
        advance();
        auto subject = parse_expression();
        consume(TokenType::COLON, "expected ':'");
        expect_newline();
        expect_indent();

        MatchExpr match_expr;
        match_expr.subject = std::move(subject);
        match_expr.location = loc;

        while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
            skip_newlines();
            if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
                break;
            }
            auto error_count_before = errors_.error_count();
            auto arm_loc = peek().location;
            auto pattern = parse_expression();
            consume(TokenType::FAT_ARROW, "expected '=>'");
            auto body = parse_expression();
            expect_newline();
            if (errors_.error_count() > error_count_before) {
                synchronize();
                continue;
            }
            MatchArm arm;
            arm.pattern = std::move(pattern);
            arm.body = std::move(body);
            arm.location = arm_loc;
            match_expr.arms.push_back(std::move(arm));
        }

        expect_dedent();
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(match_expr)}, loc);
    }

    // If expression (inline): if cond: expr else: expr
    if (check(TokenType::IF)) {
        advance();
        auto condition = parse_expression();
        consume(TokenType::COLON, "expected ':'");
        auto then_expr = parse_expression();
        consume(TokenType::ELSE, "expected 'else'");
        consume(TokenType::COLON, "expected ':'");
        auto else_expr = parse_expression();
        IfExpr if_expr;
        if_expr.condition = std::move(condition);
        if_expr.then_expr = std::move(then_expr);
        if_expr.else_expr = std::move(else_expr);
        if_expr.location = loc;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(if_expr)}, loc);
    }

    errors_.error(loc, "expected expression");
    // Advance past the unrecognised token to prevent infinite error loops
    if (!check(TokenType::NEWLINE) && !check(TokenType::DEDENT) &&
        !check(TokenType::EOF_TOKEN)) {
        advance();
    }
    IdentExpr err;
    err.name = "<error>";
    err.location = loc;
    return std::make_unique<ExprNode>(ExprNode::Variant{std::move(err)}, loc);
}

// ── Exclude Clause ───────────────────────────────────────────────────────────

// Task 4.4: Parse optional exclude: indented block
FilterClause Parser::parse_exclude_clause() {
    auto loc = peek().location;
    consume(TokenType::EXCLUDE, "expected 'exclude'");
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    FilterClause clause;
    clause.location = loc;

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        auto error_count_before = errors_.error_count();
        auto entry_loc = peek().location;
        auto name = consume(TokenType::IDENTIFIER, "expected trait name").value;
        clause.trait_names.push_back(name);
        clause.entries.push_back({.qualified_name = name, .alias = std::nullopt, .location = entry_loc});
        expect_newline();
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    expect_dedent();
    return clause;
}

ExternSystemNode Parser::parse_extern_system() { // NOLINT(readability-function-cognitive-complexity)
    auto loc = peek().location;
    consume(TokenType::EXTERN, "expected 'extern'");
    consume(TokenType::SYSTEM, "expected 'system' after 'extern'");
    auto name = consume(TokenType::IDENTIFIER, "expected system name").value;
    consume(TokenType::COLON, "expected ':'");
    expect_newline();
    expect_indent();

    ExternSystemNode node;
    node.name = name;
    node.location = loc;

    skip_newlines();
    if (check(TokenType::FILTER)) {
        auto error_count_before = errors_.error_count();
        node.filter = parse_filter_clause();
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    skip_newlines();
    if (check(TokenType::EXCLUDE)) {
        auto error_count_before = errors_.error_count();
        node.exclude = parse_exclude_clause();
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    skip_newlines();
    if (check(TokenType::IDENTIFIER) && peek().value == "order" &&
        peek_next().type == TokenType::IDENTIFIER && peek_next().value == "by") {
        auto error_count_before = errors_.error_count();
        node.order_by = parse_order_by_clause();
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    skip_newlines();
    if (check(TokenType::AFTER)) {
        auto after_loc = peek().location;
        auto error_count_before = errors_.error_count();
        advance();
        consume(TokenType::COLON, "expected ':'");
        expect_newline();
        expect_indent();
        bool any = false;
        while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
            skip_newlines();
            if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
                break;
            }
            node.after_systems.push_back(
                consume(TokenType::IDENTIFIER, "expected system name in after: block").value);
            expect_newline();
            any = true;
            if (errors_.error_count() > error_count_before) {
                synchronize();
                break;
            }
        }
        expect_dedent();
        if (!any) {
            errors_.error(after_loc, "after: block must contain at least one system name");
        }
        if (errors_.error_count() > error_count_before) {
            synchronize();
        }
    }

    skip_newlines();
    if (check(TokenType::TARGET)) {
        advance();
        consume(TokenType::COLON, "expected ':'");
        node.target = consume(TokenType::IDENTIFIER, "expected 'cpu' or 'gpu'").value;
        expect_newline();
    }

    while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
        skip_newlines();
        if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
            break;
        }
        if (check(TokenType::ON)) {
            errors_.error(peek().location,
                "`extern system` cannot have event handlers; use `system` instead");
            parse_event_handler();
            continue;
        }
        errors_.error(peek().location,
            "expected extern system clause (filter, exclude, order by, after, target)");
        synchronize();
    }

    expect_dedent();
    return node;
}

// ── Asset Declaration ────────────────────────────────────────────────────────

AssetDeclNode Parser::parse_asset_decl(bool is_pub) {
    auto loc = peek().location;
    consume(TokenType::ASSET, "expected 'asset'");
    auto name = consume(TokenType::IDENTIFIER, "expected asset name").value;
    consume(TokenType::COLON, "expected ':'");

    // Parse asset type: mesh | texture | sound | music | font | material
    AssetKind kind = AssetKind::Mesh;
    if (check(TokenType::IDENTIFIER)) {
        auto type_val = peek().value;
        if      (type_val == "mesh")     { advance(); kind = AssetKind::Mesh; }
        else if (type_val == "texture")  { advance(); kind = AssetKind::Texture; }
        else if (type_val == "sound")    { advance(); kind = AssetKind::Sound; }
        else if (type_val == "music")    { advance(); kind = AssetKind::Music; }
        else if (type_val == "font")     { advance(); kind = AssetKind::Font; }
        else if (type_val == "material") { advance(); kind = AssetKind::Material; }
        else {
            errors_.error(peek().location,
                "expected asset type (mesh, texture, sound, music, font, material), got '" + type_val + "'");
            advance();
        }
    } else {
        errors_.error(peek().location, "expected asset type after ':'");
    }

    consume(TokenType::ASSIGN, "expected '='");
    auto path_tok = consume(TokenType::STRING_LITERAL, "expected resource path string literal");

    expect_newline();

    AssetDeclNode node;
    node.name = name;
    node.is_pub = is_pub;
    node.asset_kind = kind;
    node.path = path_tok.value;
    node.location = loc;
    return node;
}

// ── Input Declaration ────────────────────────────────────────────────────────

InputDeclNode Parser::parse_input_decl(bool is_pub) {
    auto loc = peek().location;
    consume(TokenType::INPUT, "expected 'input'");
    auto name = consume(TokenType::IDENTIFIER, "expected input action name").value;
    consume(TokenType::COLON, "expected ':'");

    // Parse input kind: button | axis
    InputKind kind = InputKind::Button;
    if (check(TokenType::IDENTIFIER)) {
        auto kind_val = peek().value;
        if (kind_val == "button")     { advance(); kind = InputKind::Button; }
        else if (kind_val == "axis")  { advance(); kind = InputKind::Axis; }
        else {
            errors_.error(peek().location,
                "expected 'button' or 'axis' after ':', got '" + kind_val + "'");
            advance();
        }
    } else {
        errors_.error(peek().location, "expected 'button' or 'axis' after ':'");
    }

    expect_newline();

    // Parse indented property block (optional — may be empty)
    InputDeclNode node;
    node.name = name;
    node.is_pub = is_pub;
    node.input_kind = kind;
    node.location = loc;

    // Check if there's an indented body
    skip_newlines();
    if (check(TokenType::INDENT)) {
        advance();  // consume INDENT
        while (!check(TokenType::DEDENT) && !check(TokenType::EOF_TOKEN)) {
            skip_newlines();
            if (check(TokenType::DEDENT) || check(TokenType::EOF_TOKEN)) {
                break;
            }
            auto error_count_before = errors_.error_count();
            auto prop_loc = peek().location;
            auto key = consume(TokenType::IDENTIFIER, "expected property key").value;
            consume(TokenType::ASSIGN, "expected '='");
            auto value = parse_expression();
            expect_newline();
            if (errors_.error_count() > error_count_before) {
                synchronize();
                continue;
            }
            InputPropNode prop;
            prop.key = key;
            prop.value = std::move(value);
            prop.location = prop_loc;
            node.props.push_back(std::move(prop));
        }
        expect_dedent();
    }

    return node;
}

}  // namespace cactus
