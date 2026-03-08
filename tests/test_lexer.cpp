#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "frontend/lexer.h"
#include "frontend/token.h"

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
    for (auto& t : tokens) {
        if (t.type != TokenType::NEWLINE && t.type != TokenType::EOF_TOKEN) {
            types.push_back(t.type);
        }
    }
    return types;
}

TEST_CASE("Lexer: keywords are recognized", "[lexer]") {
    auto tokens = lex("module use const struct enum trait unit system view event func interface");
    auto types = token_types(tokens);
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
    auto types = token_types(tokens);
    REQUIRE(types.size() == 5);
    CHECK(types[0] == TokenType::LET);
    CHECK(types[1] == TokenType::VAR);
    CHECK(types[2] == TokenType::PERSIST);
    CHECK(types[3] == TokenType::SYNC);
    CHECK(types[4] == TokenType::PUB);
}

TEST_CASE("Lexer: keyword vs identifier", "[lexer]") {
    auto tokens = lex("system system_name");
    auto types = token_types(tokens);
    REQUIRE(types.size() == 2);
    CHECK(types[0] == TokenType::SYSTEM);
    CHECK(types[1] == TokenType::IDENTIFIER);
    CHECK(tokens[1].value == "system_name");
}

TEST_CASE("Lexer: integer literal", "[lexer]") {
    auto tokens = lex("42");
    auto types = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::INT_LITERAL);
    CHECK(tokens[0].value == "42");
}

TEST_CASE("Lexer: float literal", "[lexer]") {
    auto tokens = lex("3.14");
    auto types = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::FLOAT_LITERAL);
    CHECK(tokens[0].value == "3.14");
}

TEST_CASE("Lexer: string literal", "[lexer]") {
    auto tokens = lex("\"Hello World\"");
    auto types = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::STRING_LITERAL);
    CHECK(tokens[0].value == "Hello World");
}

TEST_CASE("Lexer: hex color RGB", "[lexer]") {
    auto tokens = lex("#FF0000");
    auto types = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::HEX_COLOR);
    CHECK(tokens[0].value == "FF0000");
}

TEST_CASE("Lexer: hex color RGBA", "[lexer]") {
    auto tokens = lex("#FF000080");
    auto types = token_types(tokens);
    REQUIRE(types.size() == 1);
    CHECK(types[0] == TokenType::HEX_COLOR);
    CHECK(tokens[0].value == "FF000080");
}

TEST_CASE("Lexer: comment skipping", "[lexer]") {
    auto tokens = lex("# this is a comment\nvar x: int");
    auto types = token_types(tokens);
    CHECK(types[0] == TokenType::VAR);
    CHECK(types[1] == TokenType::IDENTIFIER);
}

TEST_CASE("Lexer: inline comment", "[lexer]") {
    auto tokens = lex("var x: int  # inline comment");
    auto types = token_types(tokens);
    REQUIRE(types.size() == 4);
    CHECK(types[0] == TokenType::VAR);
    CHECK(types[1] == TokenType::IDENTIFIER);
    CHECK(types[2] == TokenType::COLON);
    CHECK(types[3] == TokenType::IDENTIFIER);  // "int" is a keyword-like identifier
}

TEST_CASE("Lexer: operators", "[lexer]") {
    auto tokens = lex("-> => == != <= >= += -=");
    auto types = token_types(tokens);
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
    auto types = token_types(tokens);
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
        if (t.type == TokenType::INDENT) found_indent = true;
        if (t.type == TokenType::DEDENT) found_dedent = true;
    }
    CHECK(found_indent);
    CHECK(found_dedent);
}

TEST_CASE("Lexer: multiple dedent levels", "[lexer]") {
    std::string src = "a:\n    b:\n        c\nd\n";
    auto tokens = lex(src);
    int dedent_count = 0;
    for (auto& t : tokens) {
        if (t.type == TokenType::DEDENT) ++dedent_count;
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
    auto types = token_types(tokens);
    REQUIRE(types.size() == 2);
    CHECK(types[0] == TokenType::TRUE_LIT);
    CHECK(types[1] == TokenType::FALSE_LIT);
}

TEST_CASE("Lexer: logical operators as keywords", "[lexer]") {
    auto tokens = lex("and or not");
    auto types = token_types(tokens);
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
