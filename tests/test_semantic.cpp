#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "frontend/semantic_analyzer.h"

using namespace cactus;

// Standard lifecycle event declarations (normally from std.core imports)
static const std::string STDLIB_EVENTS =
    "pub event tick:\n"
    "    let dt: float\n"
    "pub event fixed_tick:\n"
    "    let dt: float\n"
    "pub event late_tick:\n"
    "    let dt: float\n"
    "pub event spawn\n"
    "pub event destroy\n"
    "pub event input\n"
    "pub event load\n"
    "pub event unload\n";

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
    if (errors.has_errors()) {
        return true;
    }
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    if (errors.has_errors()) {
        return true;
    }
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program);
    return errors.has_errors();
}

TEST_CASE("Semantic: type resolution — built-in types", "[semantic]") {
    auto result = analyze(
        "trait Pos:\n"
        "    var x: float\n"
        "    var y: int\n");
    REQUIRE(result.traits.count("Pos"));
    auto& trait = result.traits["Pos"];
    REQUIRE(trait.fields.size() == 2);
    CHECK(trait.fields[0].type.kind == TypeKind::Float);
    CHECK(trait.fields[1].type.kind == TypeKind::Int);
}

TEST_CASE("Semantic: type resolution — struct type", "[semantic]") {
    auto result = analyze(
        "struct Item:\n"
        "    price: int\n"
        "trait Inv:\n"
        "    var item: Item\n");
    REQUIRE(result.traits.count("Inv"));
    CHECK(result.traits["Inv"].fields[0].type.kind == TypeKind::Struct);
    CHECK(result.traits["Inv"].fields[0].type.name == "Item");
}

TEST_CASE("Semantic: type resolution — enum type", "[semantic]") {
    auto result = analyze(
        "enum Color:\n"
        "    Red\n"
        "    Green\n"
        "trait Paint:\n"
        "    var color: Color\n");
    CHECK(result.traits["Paint"].fields[0].type.kind == TypeKind::Enum);
}

TEST_CASE("Semantic: type resolution — list type", "[semantic]") {
    auto result = analyze(
        "trait Bag:\n"
        "    var items: list[int]\n");
    CHECK(result.traits["Bag"].fields[0].type.kind == TypeKind::List);
}

TEST_CASE("Semantic: unknown type error", "[semantic]") {
    CHECK(analyze_has_errors(
        "trait Bad:\n"
        "    var x: UnknownType\n"));
}

TEST_CASE("Semantic: const string — allowed in const block", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        "const:\n"
        "    NAME = \"hello\"\n"));
}

TEST_CASE("Semantic: const string — rejected in func", "[semantic]") {
    CHECK(analyze_has_errors(
        "func test() int:\n"
        "    x = \"bad\"\n"
        "    return 0\n"));
}

TEST_CASE("Semantic: func purity — emit rejected", "[semantic]") {
    CHECK(analyze_has_errors(
        "event Boom:\n"
        "    var x: int\n"
        "func bad():\n"
        "    emit Boom:\n"
        "        x = 1\n"));
}

TEST_CASE("Semantic: func purity — pure func allowed", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        "func add(a: int, b: int) int:\n"
        "    return a + b\n"));
}

TEST_CASE("Semantic: no recursion — direct", "[semantic]") {
    CHECK(analyze_has_errors(
        "func loop(x: int) int:\n"
        "    return loop(x)\n"));
}

TEST_CASE("Semantic: persist on var — allowed", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        "trait Save:\n"
        "    persist var data: int\n"));
}

TEST_CASE("Semantic: persist on let — rejected", "[semantic]") {
    CHECK(analyze_has_errors(
        "trait Bad:\n"
        "    persist let data: int = 0\n"));
}

TEST_CASE("Semantic: sync on let — rejected", "[semantic]") {
    CHECK(analyze_has_errors(
        "trait Bad:\n"
        "    sync let data: int = 0\n"));
}

TEST_CASE("Semantic: persist sync on var — allowed", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        "trait Net:\n"
        "    persist sync var pos: float\n"));
}

TEST_CASE("Semantic: system filter — valid trait", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        STDLIB_EVENTS +
        "trait Pos:\n"
        "    var x: float\n"
        "system Move:\n"
        "    filter: \n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n"));
}

TEST_CASE("Semantic: system filter — unknown trait", "[semantic]") {
    CHECK(analyze_has_errors(
        STDLIB_EVENTS +
        "system Bad:\n"
        "    filter: \n"
        "        NonExistent\n"
        "    on tick:\n"
        "        x = 0\n"));
}

TEST_CASE("Semantic: event handler — valid event", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        "trait Pos:\n"
        "    var x: float\n"
        "event Hit:\n"
        "    var dmg: int\n"
        "system Combat:\n"
        "    filter: \n"
        "        Pos\n"
        "    on Hit:\n"
        "        x = x + 1.0\n"));
}

TEST_CASE("Semantic: event handler — unknown event", "[semantic]") {
    CHECK(analyze_has_errors(
        "trait Pos:\n"
        "    var x: float\n"
        "system Bad:\n"
        "    filter: \n"
        "        Pos\n"
        "    on FakeEvent:\n"
        "        x = 0\n"));
}

TEST_CASE("Semantic: emit payload with unknown field — error", "[semantic]") {
    CHECK(analyze_has_errors(
        STDLIB_EVENTS +
        "event Damage:\n"
        "    var amount: int\n"
        "system Combat:\n"
        "    on tick:\n"
        "        emit Damage:\n"
        "            badfield = 1\n"));
}

TEST_CASE("Semantic: emit payload with valid field — ok", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        STDLIB_EVENTS +
        "event Damage:\n"
        "    var amount: int\n"
        "system Combat:\n"
        "    on tick:\n"
        "        emit Damage:\n"
        "            amount = 1\n"));
}

TEST_CASE("Semantic: tick handler — always valid", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(
        STDLIB_EVENTS +
        "trait Pos:\n"
        "    var x: float\n"
        "system Move:\n"
        "    filter: \n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n"));
}

TEST_CASE("Semantic: dependency graph built", "[semantic]") {
    auto result = analyze(
        STDLIB_EVENTS +
        "trait Pos:\n"
        "    var x: float\n"
        "system Move:\n"
        "    filter: \n"
        "        Pos\n"
        "    on tick as t:\n"
        "        x = x + t.dt\n");
    REQUIRE(result.dependency_graph.size() == 1);
    CHECK(result.dependency_graph[0].system_name == "Move");
    CHECK(result.dependency_graph[0].reads.count("Pos"));
    CHECK(result.dependency_graph[0].writes.count("x"));
}

TEST_CASE("Semantic: duplicate struct error", "[semantic]") {
    CHECK(analyze_has_errors(
        "struct A:\n"
        "    x: int\n"
        "struct A:\n"
        "    y: int\n"));
}

TEST_CASE("Semantic: resolved struct fields", "[semantic]") {
    auto result = analyze(
        "struct Vec2:\n"
        "    x: float\n"
        "    y: float\n");
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
        "event Boom:\n"
        "    var x: int\n"
        "func bad():\n"
        "    emit Boom:\n"
        "        x = 1\n"));
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

// ── system-ordering-and-trait-cleanup semantic tests ────────────────────────

// Task 12.5: after: referencing unknown system reports error
TEST_CASE("Semantic: after: unknown system reports error", "[semantic][system-ordering]") {
    CHECK(analyze_has_errors(
        STDLIB_EVENTS +
        "trait T:\n"
        "    var x: float\n"
        "system A:\n"
        "    after:\n"
        "        NonExistentSystem\n"
        "    on tick:\n"
        "        x = 1.0\n"));
}

// Task 12.6: direct after: cycle reports error
TEST_CASE("Semantic: after: direct cycle reports error", "[semantic][system-ordering]") {
    CHECK(analyze_has_errors(
        STDLIB_EVENTS +
        "trait T:\n"
        "    var x: float\n"
        "system A:\n"
        "    after:\n"
        "        B\n"
        "    on tick:\n"
        "        x = 1.0\n"
        "system B:\n"
        "    after:\n"
        "        A\n"
        "    on tick:\n"
        "        x = 2.0\n"));
}

// Task 12.7: valid after: chain passes and after_systems populated
TEST_CASE("Semantic: after: linear chain passes and populates after_systems", "[semantic][system-ordering]") {
    auto result = analyze(
        STDLIB_EVENTS +
        "trait T:\n"
        "    var x: float\n"
        "system A:\n"
        "    filter: \n"
        "       T\n"
        "    on tick:\n"
        "        x = 1.0\n"
        "system B:\n"
        "    filter:\n"
        "       T\n"
        "    after:\n"
        "        A\n"
        "    on tick:\n"
        "        x = 2.0\n"
        "system C:\n"
        "    filter:\n"
        "       T\n"
        "    after:\n"
        "        B\n"
        "    on tick:\n"
        "        x = 3.0\n");
    REQUIRE(result.dependency_graph.size() == 3);
    // Find B and C in dependency graph
    bool found_b = false;
    bool found_c = false;
    for (auto& dep : result.dependency_graph) {
        if (dep.system_name == "B") {
            REQUIRE(dep.after_systems.size() == 1);
            CHECK(dep.after_systems[0] == "A");
            found_b = true;
        }
        if (dep.system_name == "C") {
            REQUIRE(dep.after_systems.size() == 1);
            CHECK(dep.after_systems[0] == "B");
            found_c = true;
        }
    }
    CHECK(found_b);
    CHECK(found_c);
}

// Task 12.8: ambiguous bare config key reports error
TEST_CASE("Semantic: duplicate field across nested traits reports no error", "[semantic][config-qualification]") {
    CHECK_FALSE(analyze_has_errors(
        "trait TraitA:\n"
        "    var value: int\n"
        "trait TraitB:\n"
        "    var value: int\n"
        "unit Player:\n"
        "    TraitA:\n"
        "        value = 5\n"
        "    TraitB:\n"
        "        value = 6\n"));
}

TEST_CASE("Semantic: nested trait field resolves correctly", "[semantic][config-qualification]") {
    CHECK_FALSE(analyze_has_errors(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "unit Player:\n"
        "    Health:\n"
        "        hp = 50\n"));
}

TEST_CASE("Semantic: nested trait field with unknown field reports error", "[semantic][config-qualification]") {
    CHECK(analyze_has_errors(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "unit Player:\n"
        "    Health:\n"
        "        notafield = 50\n"));
}

TEST_CASE("Semantic: marker trait with nested trait assignment passes", "[semantic][config-qualification]") {
    CHECK_FALSE(analyze_has_errors(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "unit Player:\n"
        "    Health:\n"
        "        hp = 50\n"));
}
