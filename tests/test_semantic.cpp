#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "frontend/semantic_analyzer.h"

using namespace cactus;

static DecoratedProgram analyze(const std::string& source) {
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(program);
    REQUIRE_FALSE(errors.has_errors());
    return result;
}

static bool analyze_has_errors(const std::string& source) {
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    if (errors.has_errors()) return true;
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    if (errors.has_errors()) return true;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program);
    return errors.has_errors();
}

TEST_CASE("Semantic: type resolution — built-in types", "[semantic]") {
    auto result = analyze("trait Pos:\n    var x: float\n    var y: int\n");
    REQUIRE(result.traits.count("Pos"));
    auto& trait = result.traits["Pos"];
    REQUIRE(trait.fields.size() == 2);
    CHECK(trait.fields[0].type.kind == TypeKind::Float);
    CHECK(trait.fields[1].type.kind == TypeKind::Int);
}

TEST_CASE("Semantic: type resolution — struct type", "[semantic]") {
    auto result = analyze("struct Item:\n    price: int\ntrait Inv:\n    var item: Item\n");
    REQUIRE(result.traits.count("Inv"));
    CHECK(result.traits["Inv"].fields[0].type.kind == TypeKind::Struct);
    CHECK(result.traits["Inv"].fields[0].type.name == "Item");
}

TEST_CASE("Semantic: type resolution — enum type", "[semantic]") {
    auto result = analyze("enum Color:\n    Red\n    Green\ntrait Paint:\n    var color: Color\n");
    CHECK(result.traits["Paint"].fields[0].type.kind == TypeKind::Enum);
}

TEST_CASE("Semantic: type resolution — list type", "[semantic]") {
    auto result = analyze("trait Bag:\n    var items: list[int]\n");
    CHECK(result.traits["Bag"].fields[0].type.kind == TypeKind::List);
}

TEST_CASE("Semantic: unknown type error", "[semantic]") {
    CHECK(analyze_has_errors("trait Bad:\n    var x: UnknownType\n"));
}

TEST_CASE("Semantic: const string — allowed in const block", "[semantic]") {
    CHECK_FALSE(analyze_has_errors("const:\n    NAME = \"hello\"\n"));
}

TEST_CASE("Semantic: const string — rejected in func", "[semantic]") {
    CHECK(analyze_has_errors("func test() int:\n    x = \"bad\"\n    return 0\n"));
}

TEST_CASE("Semantic: func purity — emit rejected", "[semantic]") {
    CHECK(analyze_has_errors(
        "event Boom:\n    var x: int\n"
        "func bad():\n    emit Boom(1)\n"));
}

TEST_CASE("Semantic: func purity — pure func allowed", "[semantic]") {
    CHECK_FALSE(analyze_has_errors("func add(a: int, b: int) int:\n    return a + b\n"));
}

TEST_CASE("Semantic: no recursion — direct", "[semantic]") {
    CHECK(analyze_has_errors("func loop(x: int) int:\n    return loop(x)\n"));
}

TEST_CASE("Semantic: persist on var — allowed", "[semantic]") {
    CHECK_FALSE(analyze_has_errors("trait Save:\n    persist var data: int\n"));
}

TEST_CASE("Semantic: persist on let — rejected", "[semantic]") {
    CHECK(analyze_has_errors("trait Bad:\n    persist let data: int = 0\n"));
}

TEST_CASE("Semantic: sync on let — rejected", "[semantic]") {
    CHECK(analyze_has_errors("trait Bad:\n    sync let data: int = 0\n"));
}

TEST_CASE("Semantic: persist sync on var — allowed", "[semantic]") {
    CHECK_FALSE(analyze_has_errors("trait Net:\n    persist sync var pos: float\n"));
}

TEST_CASE("Semantic: system filter — valid trait", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        "trait Pos:\n    var x: float\n"
        "system Move:\n    filter: [Pos]\n    on tick(dt: float):\n        x = x + dt\n"));
}

TEST_CASE("Semantic: system filter — unknown trait", "[semantic]") {
    CHECK(analyze_has_errors(
        "system Bad:\n    filter: [NonExistent]\n    on tick(dt: float):\n        x = 0\n"));
}

TEST_CASE("Semantic: event handler — valid event", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        "trait Pos:\n    var x: float\n"
        "event Hit:\n    var dmg: int\n"
        "system Combat:\n    filter: [Pos]\n    on Hit(dmg: int):\n        x = x + 1.0\n"));
}

TEST_CASE("Semantic: event handler — unknown event", "[semantic]") {
    CHECK(analyze_has_errors(
        "trait Pos:\n    var x: float\n"
        "system Bad:\n    filter: [Pos]\n    on FakeEvent(x: int):\n        x = 0\n"));
}

TEST_CASE("Semantic: tick handler — always valid", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        "trait Pos:\n    var x: float\n"
        "system Move:\n    filter: [Pos]\n    on tick(dt: float):\n        x = x + dt\n"));
}

TEST_CASE("Semantic: dependency graph built", "[semantic]") {
    auto result = analyze(
        "trait Pos:\n    var x: float\n"
        "system Move:\n    filter: [Pos]\n    on tick(dt: float):\n        x = x + dt\n");
    REQUIRE(result.dependency_graph.size() == 1);
    CHECK(result.dependency_graph[0].system_name == "Move");
    CHECK(result.dependency_graph[0].reads.count("Pos"));
    CHECK(result.dependency_graph[0].writes.count("x"));
}

TEST_CASE("Semantic: duplicate struct error", "[semantic]") {
    CHECK(analyze_has_errors("struct A:\n    x: int\nstruct A:\n    y: int\n"));
}

TEST_CASE("Semantic: resolved struct fields", "[semantic]") {
    auto result = analyze("struct Vec2:\n    x: float\n    y: float\n");
    REQUIRE(result.structs.count("Vec2"));
    CHECK(result.structs["Vec2"].fields.size() == 2);
}

// ── extern-func semantic tests (task 4.9) ─────────────────────────────────────

TEST_CASE("Semantic: extern func produces ResolvedFunc with is_extern=true", "[semantic][extern-func]") {
    auto result = analyze("pub extern func lerp(a: float, b: float, t: float) float\n");
    REQUIRE(result.funcs.count("lerp") == 1);
    auto& rf = result.funcs.at("lerp");
    CHECK(rf.name == "lerp");
    CHECK(rf.is_pub);
    CHECK(rf.is_extern);
    REQUIRE(rf.params.size() == 3);
    CHECK(rf.params[0].name == "a");
    CHECK(rf.params[0].type.kind == TypeKind::Float);
    CHECK(rf.params[1].name == "b");
    CHECK(rf.params[2].name == "t");
    REQUIRE(rf.return_type.has_value());
    CHECK(rf.return_type->kind == TypeKind::Float);
}

TEST_CASE("Semantic: non-pub extern func is in funcs map but not pub", "[semantic][extern-func]") {
    auto result = analyze("extern func internal_helper(x: int) int\n");
    REQUIRE(result.funcs.count("internal_helper") == 1);
    CHECK_FALSE(result.funcs.at("internal_helper").is_pub);
    CHECK(result.funcs.at("internal_helper").is_extern);
}

TEST_CASE("Semantic: extern func without return type has no return_type", "[semantic][extern-func]") {
    auto result = analyze("pub extern func init()\n");
    REQUIRE(result.funcs.count("init") == 1);
    CHECK(result.funcs.at("init").is_extern);
    CHECK_FALSE(result.funcs.at("init").return_type.has_value());
}

TEST_CASE("Semantic: extern func not flagged by purity check", "[semantic][extern-func]") {
    // extern func should not be checked for purity (it has no body)
    CHECK_FALSE(analyze_has_errors("pub extern func lerp(a: float, b: float, t: float) float\n"));
}

TEST_CASE("Semantic: non-extern func with emit is still flagged", "[semantic][extern-func]") {
    // Regular func with emit still fails purity check
    CHECK(analyze_has_errors(
        "event Boom:\n    var x: int\n"
        "func bad():\n    emit Boom(1)\n"));
}

TEST_CASE("Semantic: multiple extern funcs resolve correctly", "[semantic][extern-func]") {
    auto result = analyze(
        "pub extern func sin(a: float) float\n"
        "pub extern func cos(a: float) float\n"
        "pub extern func sqrt(v: float) float\n");
    REQUIRE(result.funcs.size() == 3);
    CHECK(result.funcs.count("sin") == 1);
    CHECK(result.funcs.count("cos") == 1);
    CHECK(result.funcs.count("sqrt") == 1);
    for (auto& [name, func] : result.funcs) {
        CHECK(func.is_extern);
        CHECK(func.is_pub);
    }
}
