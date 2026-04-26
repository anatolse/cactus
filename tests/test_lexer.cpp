// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/token.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cactus;

// Helper: tokenize and return tokens (excluding final EOF)
static std::vector<Token> lex(const std::string& source) {
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    return tokens;
}

// Helper: tokenize expecting errors
static std::vector<Token> lex_with_errors(const std::string& source, ErrorReporter& errors) {
    Lexer lexer(source, "test.cactus", errors);
    return lexer.tokenize();
}

// Helper: find token types (excluding NEWLINE and EOF for cleaner assertions)
static std::vector<TokenType> token_types(const std::vector<Token>& tokens) {
    std::vector<TokenType> types;
    for (const auto& t : tokens) {
        if (t.type != TokenType::NEWLINE && t.type != TokenType::EOF_TOKEN) {
            types.push_back(t.type);
        }
    }
    return types;
}

TEST_CASE("Lexer: keywords are recognized", "[lexer]") {
    auto tokens = lex("module use const struct enum trait unit system view event func interface");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 12);
    CHECK(types[0] == TokenType::MODULE);
    CHECK(types[1] == TokenType::USE);
    CHECK(types[2] == TokenType::CONST);
    CHECK(types[3] == TokenType::STRUCT);
    CHECK(types[4] == TokenType::ENUM);
    CHECK(types[5] == TokenType::TRAIT);
    CHECK(types[6] == TokenType::UNIT);
    CHECK(types[7] == TokenType::SYSTEM);
    CHECK(types[8] == TokenType::VIEW);
    CHECK(types[9] == TokenType::EVENT);
    CHECK(types[10] == TokenType::FUNC);
    CHECK(types[11] == TokenType::INTERFACE);
}

TEST_CASE("Lexer: modifier keywords", "[lexer]") {
    auto tokens = lex("let var persist sync pub");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 5);
    CHECK(types[0] == TokenType::LET);
    CHECK(types[1] == TokenType::VAR);
    CHECK(types[2] == TokenType::PERSIST);
    CHECK(types[3] == TokenType::SYNC);
    CHECK(types[4] == TokenType::PUB);
}

TEST_CASE("Lexer: keyword vs identifier", "[lexer]") {
    auto tokens = lex("system system_name");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 2);
    CHECK(types[0] == TokenType::SYSTEM);
    CHECK(types[1] == TokenType::IDENTIFIER);
    CHECK(tokens[1].value == "system_name");
}

TEST_CASE("Lexer: apply and config are identifiers", "[lexer]") {
    auto tokens = lex("apply config");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 2);
    CHECK(types[0] == TokenType::IDENTIFIER);
    CHECK(types[1] == TokenType::IDENTIFIER);
    CHECK(tokens[0].value == "apply");
    CHECK(tokens[1].value == "config");
}

TEST_CASE("Lexer: integer literal", "[lexer]") {
    auto tokens = lex("42");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::INT_LITERAL);
    CHECK(tokens[0].value == "42");
}

TEST_CASE("Lexer: float literal", "[lexer]") {
    auto tokens = lex("3.14");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::FLOAT_LITERAL);
    CHECK(tokens[0].value == "3.14");
}

TEST_CASE("Lexer: string literal", "[lexer]") {
    auto tokens = lex("\"Hello World\"");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::STRING_LITERAL);
    CHECK(tokens[0].value == "Hello World");
}

TEST_CASE("Lexer: hex color RGB", "[lexer]") {
    auto tokens = lex("#FF0000");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::HEX_COLOR);
    CHECK(tokens[0].value == "FF0000");
}

TEST_CASE("Lexer: hex color RGBA", "[lexer]") {
    auto tokens = lex("#FF000080");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::HEX_COLOR);
    CHECK(tokens[0].value == "FF000080");
}

TEST_CASE("Lexer: comment skipping", "[lexer]") {
    auto tokens = lex("# this is a comment\nvar x: int");
    auto types  = token_types(tokens);
    CHECK(types[0] == TokenType::VAR);
    CHECK(types[1] == TokenType::IDENTIFIER);
}

TEST_CASE("Lexer: inline comment", "[lexer]") {
    auto tokens = lex("var x: int  # inline comment");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 4);
    CHECK(types[0] == TokenType::VAR);
    CHECK(types[1] == TokenType::IDENTIFIER);
    CHECK(types[2] == TokenType::COLON);
    CHECK(types[3] == TokenType::IDENTIFIER);  // "int" is a keyword-like identifier
}

TEST_CASE("Lexer: operators", "[lexer]") {
    auto tokens = lex("-> => == != <= >= += -=");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 8);
    CHECK(types[0] == TokenType::ARROW);
    CHECK(types[1] == TokenType::FAT_ARROW);
    CHECK(types[2] == TokenType::EQUALS);
    CHECK(types[3] == TokenType::NOT_EQUALS);
    CHECK(types[4] == TokenType::LESS_EQ);
    CHECK(types[5] == TokenType::GREATER_EQ);
    CHECK(types[6] == TokenType::PLUS_ASSIGN);
    CHECK(types[7] == TokenType::MINUS_ASSIGN);
}

TEST_CASE("Lexer: single-char punctuation", "[lexer]") {
    auto tokens = lex(": , . ( ) [ ] + - * / %");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 12);
    CHECK(types[0] == TokenType::COLON);
    CHECK(types[1] == TokenType::COMMA);
    CHECK(types[2] == TokenType::DOT);
    CHECK(types[3] == TokenType::LPAREN);
    CHECK(types[4] == TokenType::RPAREN);
    CHECK(types[5] == TokenType::LBRACKET);
    CHECK(types[6] == TokenType::RBRACKET);
    CHECK(types[7] == TokenType::PLUS);
    CHECK(types[8] == TokenType::MINUS);
    CHECK(types[9] == TokenType::STAR);
    CHECK(types[10] == TokenType::SLASH);
    CHECK(types[11] == TokenType::PERCENT);
}

TEST_CASE("Lexer: INDENT and DEDENT", "[lexer]") {
    auto tokens = lex("trait Player:\n    var health: int\n");
    // Expected: TRAIT IDENTIFIER COLON NEWLINE INDENT VAR IDENTIFIER COLON IDENTIFIER NEWLINE DEDENT NEWLINE EOF
    bool found_indent = false;
    bool found_dedent = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::INDENT) {
            found_indent = true;
        }
        if (t.type == TokenType::DEDENT) {
            found_dedent = true;
        }
    }
    CHECK(found_indent);
    CHECK(found_dedent);
}

TEST_CASE("Lexer: multiple dedent levels", "[lexer]") {
    std::string src  = "a:\n    b:\n        c\nd\n";
    auto tokens      = lex(src);
    int dedent_count = 0;
    for (auto& t : tokens) {
        if (t.type == TokenType::DEDENT) {
            ++dedent_count;
        }
    }
    CHECK(dedent_count == 2);
}

TEST_CASE("Lexer: tab rejection", "[lexer]") {
    ErrorReporter errors;
    lex_with_errors("\tvar x: int", errors);
    CHECK(errors.has_errors());
}

TEST_CASE("Lexer: unterminated string", "[lexer]") {
    ErrorReporter errors;
    lex_with_errors("\"unterminated\n", errors);
    CHECK(errors.has_errors());
}

TEST_CASE("Lexer: boolean literals", "[lexer]") {
    auto tokens = lex("true false");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 2);
    CHECK(types[0] == TokenType::TRUE_LIT);
    CHECK(types[1] == TokenType::FALSE_LIT);
}

TEST_CASE("Lexer: logical operators as keywords", "[lexer]") {
    auto tokens = lex("and or not");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 3);
    CHECK(types[0] == TokenType::AND);
    CHECK(types[1] == TokenType::OR);
    CHECK(types[2] == TokenType::NOT);
}

TEST_CASE("Lexer: source locations are tracked", "[lexer]") {
    auto tokens = lex("var x: int");
    CHECK(tokens[0].location.line == 1);
    CHECK(tokens[0].location.column == 1);
}

TEST_CASE("Lexer: EOF token at end", "[lexer]") {
    auto tokens = lex("x");
    CHECK(tokens.back().type == TokenType::EOF_TOKEN);
}

// ── New keyword tests (dynamic-ecs-language) ──────────────────────────────────

TEST_CASE("Lexer: new declaration keyword template", "[lexer][dynamic-ecs]") {
    auto tokens = lex("template");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::TEMPLATE);
}

TEST_CASE("Lexer: new action keywords spawn destroy load unload", "[lexer][dynamic-ecs]") {
    auto tokens = lex("spawn destroy load unload");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 4);
    CHECK(types[0] == TokenType::SPAWN);
    CHECK(types[1] == TokenType::DESTROY);
    CHECK(types[2] == TokenType::LOAD);
    CHECK(types[3] == TokenType::UNLOAD);
}

TEST_CASE("Lexer: self is reserved and child is no longer reserved", "[lexer][hierarchy]") {
    auto tokens = lex("self child");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 2);
    CHECK(types[0] == TokenType::SELF);
    CHECK(types[1] == TokenType::IDENTIFIER);
}

TEST_CASE("Lexer: new action keywords add remove to from", "[lexer][dynamic-ecs]") {
    auto tokens = lex("add remove to from");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 4);
    CHECK(types[0] == TokenType::ADD);
    CHECK(types[1] == TokenType::REMOVE);
    CHECK(types[2] == TokenType::TO);
    CHECK(types[3] == TokenType::FROM);
}

TEST_CASE("Lexer: new block keyword exclude", "[lexer][dynamic-ecs]") {
    auto tokens = lex("exclude");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::EXCLUDE);
}

TEST_CASE("Lexer: new keywords are distinct from identifiers with similar names", "[lexer][dynamic-ecs]") {
    // Keywords used as standalone tokens
    auto tokens = lex("spawn spawner spawnable");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 3);
    CHECK(types[0] == TokenType::SPAWN);       // keyword
    CHECK(types[1] == TokenType::IDENTIFIER);  // spawner — not a keyword
    CHECK(types[2] == TokenType::IDENTIFIER);  // spawnable — not a keyword
}

TEST_CASE("Lexer: new keywords rejected as identifiers — template", "[lexer][dynamic-ecs]") {
    // 'template' when used where an identifier is expected should lex as TEMPLATE (keyword)
    // Parser would reject its use as an identifier name — here we just verify the token type
    auto tokens = lex("template");
    auto types  = token_types(tokens);
    CHECK(types[0] == TokenType::TEMPLATE);
    // Value still holds the keyword text
    CHECK(tokens[0].value == "template");
}

TEST_CASE("Lexer: new keywords have correct string representations", "[lexer][dynamic-ecs]") {
    CHECK(std::string(token_type_to_string(TokenType::TEMPLATE)) == "TEMPLATE");
    CHECK(std::string(token_type_to_string(TokenType::SPAWN)) == "SPAWN");
    CHECK(std::string(token_type_to_string(TokenType::DESTROY)) == "DESTROY");
    CHECK(std::string(token_type_to_string(TokenType::LOAD)) == "LOAD");
    CHECK(std::string(token_type_to_string(TokenType::UNLOAD)) == "UNLOAD");
    CHECK(std::string(token_type_to_string(TokenType::ADD)) == "ADD");
    CHECK(std::string(token_type_to_string(TokenType::REMOVE)) == "REMOVE");
    CHECK(std::string(token_type_to_string(TokenType::EXCLUDE)) == "EXCLUDE");
}

TEST_CASE("Lexer: all dynamic ECS keywords in one string", "[lexer][dynamic-ecs]") {
    auto tokens = lex("template spawn destroy load unload add remove to from exclude");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 10);
    CHECK(types[0] == TokenType::TEMPLATE);
    CHECK(types[1] == TokenType::SPAWN);
    CHECK(types[2] == TokenType::DESTROY);
    CHECK(types[3] == TokenType::LOAD);
    CHECK(types[4] == TokenType::UNLOAD);
    CHECK(types[5] == TokenType::ADD);
    CHECK(types[6] == TokenType::REMOVE);
    CHECK(types[7] == TokenType::TO);
    CHECK(types[8] == TokenType::FROM);
    CHECK(types[9] == TokenType::EXCLUDE);
}

// ── New keyword tests (dsl-spec-new-features) ─────────────────────────────────

TEST_CASE("Lexer: new keyword asset", "[lexer][dsl-spec-new-features]") {
    auto tokens = lex("asset");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::ASSET);
    CHECK(tokens[0].value == "asset");
}

TEST_CASE("Lexer: new keyword input", "[lexer][dsl-spec-new-features]") {
    auto tokens = lex("input");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::INPUT);
    CHECK(tokens[0].value == "input");
}

TEST_CASE("Lexer: new keyword fixed_tick", "[lexer][dsl-spec-new-features]") {
    auto tokens = lex("fixed_tick");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::FIXED_TICK);
    CHECK(tokens[0].value == "fixed_tick");
}

TEST_CASE("Lexer: new keyword late_tick", "[lexer][dsl-spec-new-features]") {
    auto tokens = lex("late_tick");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::LATE_TICK);
    CHECK(tokens[0].value == "late_tick");
}

TEST_CASE("Lexer: all 4 new dsl-spec-new-features keywords", "[lexer][dsl-spec-new-features]") {
    auto tokens = lex("asset input fixed_tick late_tick");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 4);
    CHECK(types[0] == TokenType::ASSET);
    CHECK(types[1] == TokenType::INPUT);
    CHECK(types[2] == TokenType::FIXED_TICK);
    CHECK(types[3] == TokenType::LATE_TICK);
}

TEST_CASE("Lexer: new keywords distinct from identifier prefixes", "[lexer][dsl-spec-new-features]") {
    auto tokens = lex("asset assets input inputs fixed_tick fixed_ticks late_tick late_ticks");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 8);
    CHECK(types[0] == TokenType::ASSET);
    CHECK(types[1] == TokenType::IDENTIFIER);  // "assets" not a keyword
    CHECK(types[2] == TokenType::INPUT);
    CHECK(types[3] == TokenType::IDENTIFIER);  // "inputs" not a keyword
    CHECK(types[4] == TokenType::FIXED_TICK);
    CHECK(types[5] == TokenType::IDENTIFIER);  // "fixed_ticks" not a keyword
    CHECK(types[6] == TokenType::LATE_TICK);
    CHECK(types[7] == TokenType::IDENTIFIER);  // "late_ticks" not a keyword
}

TEST_CASE("Lexer: new keyword string representations", "[lexer][dsl-spec-new-features]") {
    CHECK(std::string(token_type_to_string(TokenType::ASSET)) == "ASSET");
    CHECK(std::string(token_type_to_string(TokenType::INPUT)) == "INPUT");
    CHECK(std::string(token_type_to_string(TokenType::FIXED_TICK)) == "FIXED_TICK");
    CHECK(std::string(token_type_to_string(TokenType::LATE_TICK)) == "LATE_TICK");
}

// ── extern-func keyword tests ─────────────────────────────────────────────────

TEST_CASE("Lexer: extern keyword", "[lexer][extern-func]") {
    auto tokens = lex("extern");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::EXTERN);
    CHECK(tokens[0].value == "extern");
}

TEST_CASE("Lexer: extern_value tokenizes as IDENTIFIER", "[lexer][extern-func]") {
    auto tokens = lex("extern_value");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::IDENTIFIER);
    CHECK(tokens[0].value == "extern_value");
}

TEST_CASE("Lexer: extern string representation", "[lexer][extern-func]") {
    CHECK(std::string(token_type_to_string(TokenType::EXTERN)) == "EXTERN");
}

TEST_CASE("Lexer: extern keyword distinct from identifier prefixes", "[lexer][extern-func]") {
    auto tokens = lex("extern externals external_data");
    auto types  = token_types(tokens);
    REQUIRE(types.size() == 3);
    CHECK(types[0] == TokenType::EXTERN);      // keyword
    CHECK(types[1] == TokenType::IDENTIFIER);  // "externals" — not a keyword
    CHECK(types[2] == TokenType::IDENTIFIER);  // "external_data" — not a keyword
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
