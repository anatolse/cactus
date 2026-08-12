// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace cactus;

// Standard lifecycle event declarations (normally from std.core imports)
static const std::string STDLIB_EVENTS =
    "pub event tick:\n"
    "    dt: float\n"
    "pub event fixed_tick:\n"
    "    dt: float\n"
    "pub event late_tick:\n"
    "    dt: float\n"
    "pub event spawn\n"
    "pub event destroy\n"
    "pub event input\n"
    "pub event load\n"
    "pub event unload\n";

static bool starts_with_module_decl(const std::string& src) {
    const auto first = src.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || src.compare(first, 6, "module") != 0) {
        return false;
    }
    const auto after = first + 6;
    return after < src.size() && std::isspace(static_cast<unsigned char>(src[after])) != 0;
}

static DecoratedProgram analyze(const std::string& source) {
    const std::string src = starts_with_module_decl(source) ? source : "module test\n" + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
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
    const std::string src = starts_with_module_decl(source) ? source : "module test\n" + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
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

static std::string analyze_first_error(const std::string& source) {
    const std::string src = starts_with_module_decl(source) ? source : "module test\n" + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    if (errors.has_errors()) {
        return errors.diagnostics().front().message;
    }
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    if (errors.has_errors()) {
        return errors.diagnostics().front().message;
    }
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program);
    REQUIRE(errors.has_errors());
    return errors.diagnostics().front().message;
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
    CHECK(
        analyze_has_errors("trait Bad:\n"
                           "    var x: UnknownType\n"));
}

TEST_CASE("Semantic: const string — allowed in const block", "[semantic]") {
    CHECK_FALSE(
        analyze_has_errors("const:\n"
                           "    NAME = \"hello\"\n"));
}

TEST_CASE("Semantic: const string — rejected in func", "[semantic]") {
    CHECK(
        analyze_has_errors("func test() int:\n"
                           "    x = \"bad\"\n"
                           "    return 0\n"));
}

TEST_CASE("Semantic: func purity — emit rejected", "[semantic]") {
    CHECK(
        analyze_has_errors("event Boom:\n"
                           "    x: int\n"
                           "func bad():\n"
                           "    emit Boom:\n"
                           "        x = 1\n"));
}

TEST_CASE("Semantic: func purity — pure func allowed", "[semantic]") {
    CHECK_FALSE(
        analyze_has_errors("func sum(a: int, b: int) int:\n"
                           "    return a + b\n"));
}

TEST_CASE("Semantic: no recursion — direct", "[semantic]") {
    CHECK(
        analyze_has_errors("func loop(x: int) int:\n"
                           "    return loop(x)\n"));
}

TEST_CASE("Semantic: persist on var — allowed", "[semantic]") {
    CHECK_FALSE(
        analyze_has_errors("trait Save:\n"
                           "    persist var data: int\n"));
}

TEST_CASE("Semantic: persist on let — rejected", "[semantic]") {
    CHECK(
        analyze_has_errors("trait Bad:\n"
                           "    persist let data: int = 0\n"));
}

TEST_CASE("Semantic: sync on let — rejected", "[semantic]") {
    CHECK(
        analyze_has_errors("trait Bad:\n"
                           "    sync let data: int = 0\n"));
}

TEST_CASE("Semantic: persist sync on var — allowed", "[semantic]") {
    CHECK_FALSE(
        analyze_has_errors("trait Net:\n"
                           "    persist sync var pos: float\n"));
}

TEST_CASE("Semantic: rule filter — valid trait", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Pos:\n"
                                                   "    var x: float\n"
                                                   "rule Move:\n"
                                                   "    filter: \n"
                                                   "        Pos\n"
                                                   "    on tick:\n"
                                                   "        x = x + tick.dt\n"));
}

TEST_CASE("Semantic: rule filter — unknown trait", "[semantic]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "rule Bad:\n"
                                             "    filter: \n"
                                             "        NonExistent\n"
                                             "    on tick:\n"
                                             "        x = 0\n"));
}

TEST_CASE("Semantic: event handler — valid event", "[semantic]") {
    CHECK_FALSE(
        analyze_has_errors("trait Pos:\n"
                           "    var x: float\n"
                           "event Hit:\n"
                           "    dmg: int\n"
                           "rule Combat:\n"
                           "    filter: \n"
                           "        Pos\n"
                           "    on Hit:\n"
                           "        x = x + 1.0\n"));
}

TEST_CASE("Semantic: event handler — unknown event", "[semantic]") {
    CHECK(
        analyze_has_errors("trait Pos:\n"
                           "    var x: float\n"
                           "rule Bad:\n"
                           "    filter: \n"
                           "        Pos\n"
                           "    on FakeEvent:\n"
                           "        x = 0\n"));
}

TEST_CASE("Semantic: emit payload with unknown field — error", "[semantic]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "event Damage:\n"
                                             "    amount: int\n"
                                             "rule Combat:\n"
                                             "    on tick:\n"
                                             "        emit Damage:\n"
                                             "            badfield = 1\n"));
}

TEST_CASE("Semantic: emit payload with valid field — ok", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "event Damage:\n"
                                                   "    amount: int\n"
                                                   "rule Combat:\n"
                                                   "    on tick:\n"
                                                   "        emit Damage:\n"
                                                   "            amount = 1\n"));
}

TEST_CASE("Semantic: tick handler — always valid", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Pos:\n"
                                                   "    var x: float\n"
                                                   "rule Move:\n"
                                                   "    filter: \n"
                                                   "        Pos\n"
                                                   "    on tick:\n"
                                                   "        x = x + tick.dt\n"));
}

TEST_CASE("Semantic: dependency graph built", "[semantic]") {
    auto result = analyze(STDLIB_EVENTS +
                          "trait Pos:\n"
                          "    var x: float\n"
                          "rule Move:\n"
                          "    filter: \n"
                          "        Pos\n"
                          "    on tick as t:\n"
                          "        x = x + t.dt\n");
    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    CHECK(contract.rule.local_name == "Move");
    const auto pos_symbol = make_symbol_id(SymbolKind::Trait, "test", "Pos");
    CHECK(contract.reads.contains(pos_symbol));
    CHECK(contract.writes.contains(pos_symbol));
}

TEST_CASE("Semantic: duplicate struct error", "[semantic]") {
    CHECK(
        analyze_has_errors("struct A:\n"
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
    CHECK(
        analyze_has_errors("event Boom:\n"
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

TEST_CASE("Semantic: std.physics.flat query types and extern funcs resolve",
          "[semantic][extern-func][stdlib][physics]") {
    auto result = analyze(
        "pub enum QueryResultKind:\n"
        "    Empty\n"
        "    Hit\n"
        "pub struct QueryContact2D:\n"
        "    other: entity_id\n"
        "    normal: vec2\n"
        "    distance: float\n"
        "    overlap: vec2\n"
        "pub struct QueryResult2D:\n"
        "    kind: QueryResultKind\n"
        "    contact: QueryContact2D\n"
        "pub extern func query_cast_nearest(subject: entity_id, delta: vec2, mask: int, exclude: entity_id) "
        "QueryResult2D\n"
        "pub extern func query_overlap_deepest(subject: entity_id, mask: int, exclude: entity_id) QueryResult2D\n"
        "pub extern func query_overlap_all(subject: entity_id, mask: int, exclude: entity_id) list[QueryContact2D]\n");

    REQUIRE(result.enums.contains("QueryResultKind"));
    CHECK(result.enums.at("QueryResultKind").variants == std::vector<std::string>{"Empty", "Hit"});

    REQUIRE(result.structs.contains("QueryContact2D"));
    const auto& contact = result.structs.at("QueryContact2D");
    REQUIRE(contact.fields.size() == 4);
    CHECK(contact.fields[0].name == "other");
    CHECK(contact.fields[0].type.kind == TypeKind::EntityId);
    CHECK(contact.fields[1].type.kind == TypeKind::Vec2);
    CHECK(contact.fields[2].type.kind == TypeKind::Float);
    CHECK(contact.fields[3].type.kind == TypeKind::Vec2);

    REQUIRE(result.structs.contains("QueryResult2D"));
    const auto& query_result = result.structs.at("QueryResult2D");
    REQUIRE(query_result.fields.size() == 2);
    CHECK(query_result.fields[0].type.kind == TypeKind::Enum);
    CHECK(query_result.fields[0].type.name == "QueryResultKind");
    CHECK(query_result.fields[1].type.kind == TypeKind::Struct);
    CHECK(query_result.fields[1].type.name == "QueryContact2D");

    REQUIRE(result.funcs.contains("query_cast_nearest"));
    const auto& cast = result.funcs.at("query_cast_nearest");
    CHECK(cast.is_pub);
    CHECK(cast.is_extern);
    REQUIRE(cast.return_type.has_value());
    CHECK(cast.return_type->kind == TypeKind::Struct);
    CHECK(cast.return_type->name == "QueryResult2D");
    REQUIRE(cast.params.size() == 4);
    CHECK(cast.params[0].type.kind == TypeKind::EntityId);
    CHECK(cast.params[1].type.kind == TypeKind::Vec2);
    CHECK(cast.params[2].type.kind == TypeKind::Int);
    CHECK(cast.params[3].name == "exclude");
    CHECK(cast.params[3].type.kind == TypeKind::EntityId);

    REQUIRE(result.funcs.contains("query_overlap_all"));
    const auto& overlap_all = result.funcs.at("query_overlap_all");
    REQUIRE(overlap_all.return_type.has_value());
    CHECK(overlap_all.return_type->kind == TypeKind::List);
    REQUIRE(overlap_all.return_type->element != nullptr);
    CHECK(overlap_all.return_type->element->kind == TypeKind::Struct);
    CHECK(overlap_all.return_type->element->name == "QueryContact2D");
}

TEST_CASE("Semantic: extern rule with filter is valid", "[semantic][extern-rule]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                                   "    var x: float\n"
                                                   "extern rule SpriteRenderer:\n"
                                                   "    filter:\n"
                                                   "        Position\n"
                                                   "    on tick:\n"
                                                   "        reads:\n"
                                                   "            Position\n"
                                                   "        effects:\n"
                                                   "            graphics\n"));
}

TEST_CASE("Semantic: extern rule requires filter", "[semantic][extern-rule]") {
    CHECK(
        analyze_has_errors("extern rule SpriteRenderer:\n"
                           "    after:\n"
                           "        Move\n"));
}

TEST_CASE("Semantic: after cycle with extern rule reports error", "[semantic][extern-rule]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait T:\n"
                                             "    var x: float\n"
                                             "extern rule A:\n"
                                             "    filter:\n"
                                             "        T\n"
                                             "    after:\n"
                                             "        B\n"
                                             "rule B:\n"
                                             "    filter:\n"
                                             "        T\n"
                                             "    after:\n"
                                             "        A\n"
                                             "    on tick:\n"
                                             "        x = 1.0\n"));
}

// ── rule-ordering-and-trait-cleanup semantic tests ────────────────────────

// Task 12.5: after: referencing unknown rule reports error
TEST_CASE("Semantic: after: unknown rule reports error", "[semantic][rule-ordering]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait T:\n"
                                             "    var x: float\n"
                                             "rule A:\n"
                                             "    after:\n"
                                             "        NonExistentSystem\n"
                                             "    on tick:\n"
                                             "        x = 1.0\n"));
}

// Task 12.6: direct after: cycle reports error
TEST_CASE("Semantic: after: direct cycle reports error", "[semantic][rule-ordering]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait T:\n"
                                             "    var x: float\n"
                                             "rule A:\n"
                                             "    after:\n"
                                             "        B\n"
                                             "    on tick:\n"
                                             "        x = 1.0\n"
                                             "rule B:\n"
                                             "    after:\n"
                                             "        A\n"
                                             "    on tick:\n"
                                             "        x = 2.0\n"));
}

// Task 12.7: valid after: chain passes and after_rules populated
TEST_CASE("Semantic: after: linear chain passes and populates after_rules", "[semantic][rule-ordering]") {
    auto result = analyze(STDLIB_EVENTS +
                          "trait T:\n"
                          "    var x: float\n"
                          "rule A:\n"
                          "    filter: \n"
                          "       T\n"
                          "    on tick:\n"
                          "        x = 1.0\n"
                          "rule B:\n"
                          "    filter:\n"
                          "       T\n"
                          "    after:\n"
                          "        A\n"
                          "    on tick:\n"
                          "        x = 2.0\n"
                          "rule C:\n"
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
        if (dep.rule_name == "B") {
            REQUIRE(dep.after_rules.size() == 1);
            CHECK(dep.after_rules[0] == "test.A");
            found_b = true;
        }
        if (dep.rule_name == "C") {
            REQUIRE(dep.after_rules.size() == 1);
            CHECK(dep.after_rules[0] == "test.B");
            found_c = true;
        }
    }
    CHECK(found_b);
    CHECK(found_c);
}

TEST_CASE("Semantic: order by valid alias and scalar fields", "[semantic][rule-order-by]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                                   "    var pos: vec2\n"
                                                   "trait Sprite:\n"
                                                   "    var layer: int\n"
                                                   "rule Render:\n"
                                                   "    filter:\n"
                                                   "        Position as p\n"
                                                   "        Sprite as s\n"
                                                   "    order by:\n"
                                                   "        s.layer asc\n"
                                                   "        p.pos.y desc\n"
                                                   "    on tick:\n"
                                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: order by alias not in filter errors", "[semantic][rule-order-by]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                             "    var pos: vec2\n"
                                             "rule Render:\n"
                                             "    filter:\n"
                                             "        Position as p\n"
                                             "    order by:\n"
                                             "        s.pos.y asc\n"
                                             "    on tick:\n"
                                             "        let x = 1\n"));
}

TEST_CASE("Semantic: order by non-orderable type errors", "[semantic][rule-order-by]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                             "    var pos: vec2\n"
                                             "rule Render:\n"
                                             "    filter:\n"
                                             "        Position as p\n"
                                             "    order by:\n"
                                             "        p.pos asc\n"
                                             "    on tick:\n"
                                             "        let x = 1\n"));
}

TEST_CASE("Semantic: order by invalid vec2 member errors", "[semantic][rule-order-by]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                             "    var pos: vec2\n"
                                             "rule Render:\n"
                                             "    filter:\n"
                                             "        Position as p\n"
                                             "    order by:\n"
                                             "        p.pos.z asc\n"
                                             "    on tick:\n"
                                             "        let x = 1\n"));
}

TEST_CASE("Semantic: vec2/vec3 splat constructors type-check as trait field defaults", "[semantic][vector-expressions]") {
    auto result = analyze(
        "trait Position:\n"
        "    var pos2: vec2 = vec2(0.0)\n"
        "    var pos3: vec3 = vec3(0.0)\n");
    REQUIRE(result.traits.count("Position"));
    auto& trait = result.traits["Position"];
    REQUIRE(trait.fields.size() == 2);
    CHECK(trait.fields[0].type.kind == TypeKind::Vec2);
    CHECK(trait.fields[1].type.kind == TypeKind::Vec3);
}

TEST_CASE("Semantic: vec2/vec3 component constructors remain valid", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors("trait Position:\n"
                                   "    var pos2: vec2 = vec2(400.0, 40.0)\n"
                                   "    var pos3: vec3 = vec3(1.0, 2.0, 3.0)\n"));
}

TEST_CASE("Semantic: vec2 wrong constructor arity rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error("trait Position:\n"
                                       "    var pos: vec2 = vec2(1.0, 2.0, 3.0)\n");
    CHECK(message.find("vec2") != std::string::npos);
    CHECK(message.find('3') != std::string::npos);
}

TEST_CASE("Semantic: vec3 wrong constructor arity rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error("trait Position:\n"
                                       "    var pos: vec3 = vec3(1.0, 2.0)\n");
    CHECK(message.find("vec3") != std::string::npos);
    CHECK(message.find('2') != std::string::npos);
}

TEST_CASE("Semantic: vec2 zero-argument constructor rejected", "[semantic][vector-expressions]") {
    CHECK(analyze_has_errors("trait Position:\n"
                             "    var pos: vec2 = vec2()\n"));
}

TEST_CASE("Semantic: vec2 non-float constructor argument rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error("trait Position:\n"
                                       "    var pos: vec2 = vec2(\"0\", \"0\")\n");
    CHECK(message.find("string") != std::string::npos);
}

TEST_CASE("Semantic: vec2 splat constructor default mismatched against vec3 field rejected",
          "[semantic][vector-expressions]") {
    CHECK(analyze_has_errors("trait Position:\n"
                             "    var pos: vec3 = vec2(0.0)\n"));
}

TEST_CASE("Semantic: vec2/vec3 constructors resolve to real types outside trait defaults",
          "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                                    "    var pos: vec2\n"
                                                    "rule Move:\n"
                                                    "    filter:\n"
                                                    "        Position as p\n"
                                                    "    on tick:\n"
                                                    "        let origin = vec2(0.0)\n"
                                                    "        p.Position.pos = origin\n"));
}

// An enum-qualified literal (`GizmoMode.Select`) is resolved against its enum declaration
// by an earlier pass (resolve_enum_member_expr, populating MemberExpr::resolved_enum_member)
// before trait-default validation runs, so it's already known to be a legitimate variant
// reference here — not an arbitrary runtime member access. check_const's exhaustive
// ExprNode dispatch previously had no MemberExpr case at all, so it fell to the catch-all
// "not constant" branch regardless.
TEST_CASE("Semantic: enum-qualified literal is a valid trait field default", "[semantic][enum]") {
    CHECK_FALSE(analyze_has_errors("pub enum GizmoMode:\n"
                                   "    Select\n"
                                   "    Translate\n"
                                   "trait EditorState:\n"
                                   "    var mode: GizmoMode = GizmoMode.Select\n"));
}

TEST_CASE("Semantic: enum-qualified literal trait field default resolves to the enum type",
          "[semantic][enum]") {
    auto result = analyze("pub enum GizmoMode:\n"
                          "    Select\n"
                          "    Translate\n"
                          "trait EditorState:\n"
                          "    var mode: GizmoMode = GizmoMode.Select\n");
    REQUIRE(result.traits.count("EditorState"));
    auto& trait = result.traits["EditorState"];
    REQUIRE(trait.fields.size() == 1);
    CHECK(trait.fields[0].type.kind == TypeKind::Enum);
}

TEST_CASE("Semantic: enum-qualified literal naming an unknown variant rejected as trait default",
          "[semantic][enum]") {
    CHECK(analyze_has_errors("pub enum GizmoMode:\n"
                             "    Select\n"
                             "trait EditorState:\n"
                             "    var mode: GizmoMode = GizmoMode.Translate\n"));
}

static const std::string VECTOR_MATRIX_STDLIB =
    "trait Position:\n"
    "    var pos2: vec2\n"
    "    var pos3: vec3\n"
    "trait Motion:\n"
    "    var velocity2: vec2\n"
    "    var velocity3: vec3\n";

TEST_CASE("Semantic: vec2/vec3 component addition and subtraction typecheck", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "        Motion as m\n"
                                   "    on tick:\n"
                                   "        let sum2 = p.pos2 + m.velocity2\n"
                                   "        let diff2 = p.pos2 - m.velocity2\n"
                                   "        let sum3 = p.pos3 + m.velocity3\n"
                                   "        let diff3 = p.pos3 - m.velocity3\n"));
}

TEST_CASE("Semantic: vec2/vec3 scalar multiply is commutative in both orders", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        let a = p.pos2 * tick.dt\n"
                                   "        let b = tick.dt * p.pos2\n"
                                   "        let c = p.pos3 * tick.dt\n"
                                   "        let d = tick.dt * p.pos3\n"));
}

TEST_CASE("Semantic: vec2/vec3 divide by float typechecks", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        let a = p.pos2 / tick.dt\n"
                                   "        let b = p.pos3 / tick.dt\n"));
}

TEST_CASE("Semantic: vec2/vec3 component-wise multiply stays vector-typed, not a dot product",
          "[semantic][vector-expressions]") {
    // If `p.pos2 * m.velocity2` inferred as a scalar dot product (float), adding
    // it to another vec2 below would be rejected by the operator matrix (no
    // `float + vec2` row); accepting it proves the result stayed `vec2`.
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "        Motion as m\n"
                                   "    on tick:\n"
                                   "        let component_product = p.pos2 * m.velocity2 + p.pos2\n"));
}

TEST_CASE("Semantic: mismatched vec2/vec3 dimensions rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                       "rule Test:\n"
                                       "    filter:\n"
                                       "        Position as p\n"
                                       "    on tick:\n"
                                       "        let bad = p.pos2 + p.pos3\n");
    CHECK(message.find("vec2") != std::string::npos);
    CHECK(message.find("vec3") != std::string::npos);
}

TEST_CASE("Semantic: vector-by-vector division rejected", "[semantic][vector-expressions]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                             "rule Test:\n"
                             "    filter:\n"
                             "        Position as p\n"
                             "        Motion as m\n"
                             "    on tick:\n"
                             "        let bad = p.pos2 / m.velocity2\n"));
}

TEST_CASE("Semantic: vector plus bare scalar rejected", "[semantic][vector-expressions]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                             "rule Test:\n"
                             "    filter:\n"
                             "        Position as p\n"
                             "    on tick:\n"
                             "        let bad = p.pos2 + tick.dt\n"));
}

TEST_CASE("Semantic: vec2 addition compound assignment on a trait field", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "        Motion as m\n"
                                   "    on tick:\n"
                                   "        p.pos2 += m.velocity2\n"));
}

TEST_CASE("Semantic: vec3 scalar-multiply compound assignment on a trait field", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        p.pos3 *= tick.dt\n"));
}

TEST_CASE("Semantic: vec2 component-wise compound multiply on a trait field", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "        Motion as m\n"
                                   "    on tick:\n"
                                   "        p.pos2 *= m.velocity2\n"));
}

TEST_CASE("Semantic: vec2 divide compound assignment on a trait field", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        p.pos2 /= tick.dt\n"));
}

TEST_CASE("Semantic: vec2 compound assignment on a handler-local var", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        let accel = vec2(0.0)\n"
                                   "        accel += p.pos2\n"
                                   "        accel *= tick.dt\n"));
}

TEST_CASE("Semantic: incompatible vec2 compound-assignment operand rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                       "rule Test:\n"
                                       "    filter:\n"
                                       "        Position as p\n"
                                       "    on tick:\n"
                                       "        p.pos2 += 5\n");
    CHECK(message.find("vec2") != std::string::npos);
    CHECK(message.find("+=") != std::string::npos);
}

TEST_CASE("Semantic: compound-assignment diagnostic names target and source types",
          "[semantic][vector-expressions]") {
    auto message = analyze_first_error(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                       "rule Test:\n"
                                       "    filter:\n"
                                       "        Position as p\n"
                                       "    on tick:\n"
                                       "        p.pos2 -= \"east\"\n");
    CHECK(message.find("vec2") != std::string::npos);
    CHECK(message.find("-=") != std::string::npos);
    CHECK(message.find("string") != std::string::npos);
}

TEST_CASE("Semantic: pair-bound compound assignment still rejected as read-only", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(STDLIB_EVENTS + VECTOR_MATRIX_STDLIB +
                                       "rule Test:\n"
                                       "    pairs:\n"
                                       "        body:\n"
                                       "            Position\n"
                                       "        other:\n"
                                       "            Motion\n"
                                       "    on fixed_tick:\n"
                                       "        body.pos2 *= 2.0\n");
    CHECK(message == "pair-bound durable traits are read-only");
}

// Task 12.8: ambiguous bare config key reports error
TEST_CASE("Semantic: duplicate field across nested traits reports no error", "[semantic][config-qualification]") {
    CHECK_FALSE(
        analyze_has_errors("trait TraitA:\n"
                           "    var value: int\n"
                           "trait TraitB:\n"
                           "    var value: int\n"
                           "entity Player:\n"
                           "    TraitA:\n"
                           "        value = 5\n"
                           "    TraitB:\n"
                           "        value = 6\n"));
}

TEST_CASE("Semantic: nested trait field resolves correctly", "[semantic][config-qualification]") {
    CHECK_FALSE(
        analyze_has_errors("trait Health:\n"
                           "    var hp: int = 100\n"
                           "entity Player:\n"
                           "    Health:\n"
                           "        hp = 50\n"));
}

TEST_CASE("Semantic: nested trait field with unknown field reports error", "[semantic][config-qualification]") {
    CHECK(
        analyze_has_errors("trait Health:\n"
                           "    var hp: int = 100\n"
                           "entity Player:\n"
                           "    Health:\n"
                           "        notafield = 50\n"));
}

TEST_CASE("Semantic: marker trait with nested trait assignment passes", "[semantic][config-qualification]") {
    CHECK_FALSE(
        analyze_has_errors("trait Health:\n"
                           "    var hp: int = 100\n"
                           "entity Player:\n"
                           "    Health:\n"
                           "        hp = 50\n"));
}

TEST_CASE("Semantic: trait match valid", "[semantic][trait-match]") {
    CHECK_FALSE(
        analyze_has_errors("event Collision:\n"
                           "    other: entity_id\n"
                           "trait Boss:\n"
                           "    var stage: int\n"
                           "trait Spike\n"
                           "rule Combat:\n"
                           "    on Collision as c:\n"
                           "        match c.other:\n"
                           "            Boss as b =>\n"
                           "                let x = b.stage\n"
                           "            Spike =>\n"
                           "                let y = 1\n"));
}

TEST_CASE("Semantic: trait match non-entity subject error", "[semantic][trait-match]") {
    CHECK(
        analyze_has_errors("event Collision:\n"
                           "    other: int\n"
                           "trait Boss:\n"
                           "    var phase: int\n"
                           "rule Combat:\n"
                           "    on Collision as c:\n"
                           "        match c.other:\n"
                           "            Boss as b =>\n"
                           "                let x = b.phase\n"));
}

TEST_CASE("Semantic: trait match unknown trait error", "[semantic][trait-match]") {
    CHECK(
        analyze_has_errors("event Collision:\n"
                           "    other: entity_id\n"
                           "rule Combat:\n"
                           "    on Collision as c:\n"
                           "        match c.other:\n"
                           "            Phantom as p =>\n"
                           "                let x = 1\n"));
}

TEST_CASE("Semantic: trait match alias conflict error", "[semantic][trait-match]") {
    CHECK(
        analyze_has_errors("event Collision:\n"
                           "    other: entity_id\n"
                           "trait Position:\n"
                           "    var x: float\n"
                           "trait Boss:\n"
                           "    var phase: int\n"
                           "rule Combat:\n"
                           "    filter:\n"
                           "        Position as p\n"
                           "    on Collision as c:\n"
                           "        match c.other:\n"
                           "            Boss as p =>\n"
                           "                let x = 1\n"));
}

TEST_CASE("Semantic: marker trait alias error", "[semantic][trait-match]") {
    CHECK(
        analyze_has_errors("event Collision:\n"
                           "    other: entity_id\n"
                           "trait Spike\n"
                           "rule Combat:\n"
                           "    on Collision as c:\n"
                           "        match c.other:\n"
                           "            Spike as s =>\n"
                           "                let x = 1\n"));
}

TEST_CASE("Semantic: wildcard not last error", "[semantic][trait-match]") {
    CHECK(
        analyze_has_errors("event Collision:\n"
                           "    other: entity_id\n"
                           "trait Boss:\n"
                           "    var phase: int\n"
                           "rule Combat:\n"
                           "    on Collision as c:\n"
                           "        match c.other:\n"
                           "            _ =>\n"
                           "                let x = 0\n"
                           "            Boss as b =>\n"
                           "                let y = b.phase\n"));
}

TEST_CASE("Semantic: trait match outside handler error", "[semantic][trait-match]") {
    CHECK(
        analyze_has_errors("trait Boss:\n"
                           "    var phase: int\n"
                           "func test(subject_id: entity_id):\n"
                           "    match subject_id:\n"
                           "        Boss as b =>\n"
                           "            let x = b.phase\n"));
}

TEST_CASE("Semantic: entity_id compared to zero uses total-semantics error", "[semantic][entity-id]") {
    CHECK(analyze_first_error("event Collision:\n"
                              "    other: entity_id\n"
                              "rule Combat:\n"
                              "    on Collision as c:\n"
                              "        let dead = c.other == 0\n") ==
          "entity_id has no null literal; use `exists(id)` to test handle validity or `add`/`remove` to model absent "
          "relationships via trait presence");
}

TEST_CASE("Semantic: exists(entity_id) valid in rule handler", "[semantic][entity-id]") {
    CHECK_FALSE(
        analyze_has_errors("event Collision:\n"
                           "    other: entity_id\n"
                           "rule Combat:\n"
                           "    on Collision as c:\n"
                           "        if exists(c.other):\n"
                           "            let x = 1\n"));
}

TEST_CASE("Semantic: event field modifiers rejected", "[semantic]") {
    CHECK(
        analyze_has_errors("event Tick:\n"
                           "    let dt: float\n"));
    CHECK(analyze_first_error("event Tick:\n"
                              "    let dt: float\n")
              .find("event fields use bare `name: type` syntax") != std::string::npos);
}

TEST_CASE("Semantic: exists requires entity_id argument", "[semantic][entity-id]") {
    CHECK(analyze_first_error(STDLIB_EVENTS + "rule Combat:\n"
                                              "    on tick:\n"
                                              "        if exists(42):\n"
                                              "            let x = 1\n") ==
          "`exists()` argument must be of type `entity_id`");
}

TEST_CASE("Semantic: exists forbidden in func body", "[semantic][entity-id]") {
    CHECK(analyze_first_error("func test(id: entity_id) bool:\n"
                              "    return exists(id)\n") ==
          "`exists()` requires world access; only allowed inside rule event handlers");
}

TEST_CASE("Semantic: self is entity_id in rule handler", "[semantic][hierarchy]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Parent:\n"
                                                   "    var parent: entity_id\n"
                                                   "rule Parenting:\n"
                                                   "    on tick:\n"
                                                   "        add Parent:\n"
                                                   "            parent = self\n"
                                                   "        destroy self\n"));
}

TEST_CASE("Semantic: self rejected in func body", "[semantic][hierarchy]") {
    CHECK(analyze_first_error("func current() entity_id:\n"
                              "    return self\n") == "`self` only allowed inside rule event handlers");
}

TEST_CASE("Semantic: self rejected in trait default", "[semantic][hierarchy]") {
    CHECK(analyze_first_error("trait Parent:\n"
                              "    var parent: entity_id = self\n") ==
          "`self` only allowed inside rule event handlers");
}

TEST_CASE("Semantic: self rejected in entity initializer", "[semantic][hierarchy]") {
    CHECK(analyze_first_error("trait Parent:\n"
                              "    var parent: entity_id\n"
                              "entity Child:\n"
                              "    Parent:\n"
                              "        parent = self\n") == "`self` only allowed inside rule event handlers");
}

TEST_CASE("Semantic: bounded foreach over list binds read-only element", "[semantic][foreach][project]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "struct Hit:\n"
                                                   "    victim: entity_id\n"
                                                   "trait Source:\n"
                                                   "    var hits: list[Hit]\n"
                                                   "event Damage:\n"
                                                   "    amount: int\n"
                                                   "rule ApplyHits:\n"
                                                   "    filter:\n"
                                                   "        Source\n"
                                                   "    on tick:\n"
                                                   "        for hit in hits:\n"
                                                   "            emit Damage to hit.victim:\n"
                                                   "                amount = 1\n"));

    CHECK(analyze_first_error(STDLIB_EVENTS + "trait Source:\n"
                                              "    var count: int\n"
                                              "rule BadLoop:\n"
                                              "    filter:\n"
                                              "        Source\n"
                                              "    on tick:\n"
                                              "        for item in count:\n"
                                              "            let x = item\n") == "foreach requires a `list[T]` iterable");

    CHECK(analyze_first_error(STDLIB_EVENTS + "trait Source:\n"
                                              "    var values: list[int]\n"
                                              "rule BadAssign:\n"
                                              "    filter:\n"
                                              "        Source\n"
                                              "    on tick:\n"
                                              "        for value in values:\n"
                                              "            value = 2\n") ==
          "foreach loop variable 'value' is read-only");
}

TEST_CASE("Semantic: project validates trait fields target and transient restrictions", "[semantic][project]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait DamageFlash:\n"
                                                   "    var color: color = #FFFFFF\n"
                                                   "    var intensity: float\n"
                                                   "trait Target:\n"
                                                   "    var victim: entity_id\n"
                                                   "rule Flash:\n"
                                                   "    filter:\n"
                                                   "        Target\n"
                                                   "    on tick:\n"
                                                   "        project DamageFlash to victim:\n"
                                                   "            intensity = 1.0\n"));

    CHECK(analyze_first_error(STDLIB_EVENTS + "trait DamageFlash:\n"
                                              "    var intensity: float\n"
                                              "rule BadTarget:\n"
                                              "    on tick:\n"
                                              "        project DamageFlash to 123:\n"
                                              "            intensity = 1.0\n") ==
          "`project ... to` target must be of type `entity_id`");

    CHECK(analyze_first_error(STDLIB_EVENTS + "trait DamageFlash:\n"
                                              "    var intensity: float\n"
                                              "rule BadField:\n"
                                              "    on tick:\n"
                                              "        project DamageFlash:\n"
                                              "            missing = 1.0\n") ==
          "unknown field 'missing' in `project DamageFlash`");

    CHECK(analyze_first_error(STDLIB_EVENTS + "trait SavedFact:\n"
                                              "    persist var value: int\n"
                                              "rule BadPersist:\n"
                                              "    on tick:\n"
                                              "        project SavedFact:\n"
                                              "            value = 1\n") ==
          "trait 'SavedFact' has persistent fields and cannot be projected");
}

TEST_CASE("Semantic: project participates in dependency writes", "[semantic][project]") {
    auto result = analyze(STDLIB_EVENTS +
                          "trait DamageFlash\n"
                          "trait Health:\n"
                          "    var hp: int\n"
                          "rule Producer:\n"
                          "    filter:\n"
                          "        Health\n"
                          "    on tick:\n"
                          "        let x = hp\n"
                          "        project DamageFlash\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    CHECK(contract.reads.contains(make_symbol_id(SymbolKind::Trait, "test", "Health")));
    CHECK(contract.writes.contains(make_symbol_id(SymbolKind::Trait, "test", "DamageFlash")));
}

// ── std.text.format semantic tests (add-stdlib-text-format) ───────────────────

TEST_CASE("Semantic: std.text.format aliased import — valid single auto placeholder", "[semantic][std-text-format]") {
    CHECK_FALSE(
        analyze_has_errors("use std.text as text\n"
                           "func render():\n"
                           "    let s = text.format(\"Score: {}\", 42)\n"));
}

TEST_CASE("Semantic: std.text.format aliased import — valid multiple auto placeholders",
          "[semantic][std-text-format]") {
    CHECK_FALSE(
        analyze_has_errors("use std.text as text\n"
                           "func render():\n"
                           "    let s = text.format(\"HP: {}/{}\", 10, 100)\n"));
}

TEST_CASE("Semantic: std.text.format — valid no placeholders no args", "[semantic][std-text-format]") {
    CHECK_FALSE(
        analyze_has_errors("use std.text as text\n"
                           "func render():\n"
                           "    let s = text.format(\"Ready\")\n"));
}

TEST_CASE("Semantic: std.text.format — valid manual placeholder", "[semantic][std-text-format]") {
    CHECK_FALSE(
        analyze_has_errors("use std.text as text\n"
                           "func render():\n"
                           "    let s = text.format(\"{0} and {0} again\", 42)\n"));
}

TEST_CASE("Semantic: std.text.format — non-literal first arg is rejected", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let x = 42\n"
        "    let s = text.format(x)\n");
    CHECK(err.find("must be a string literal") != std::string::npos);
}

TEST_CASE("Semantic: std.text.format — too few args for auto placeholders", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"hello {} {}\")\n");
    CHECK(err.find("placeholder") != std::string::npos);
    CHECK(err.find("argument") != std::string::npos);
}

TEST_CASE("Semantic: std.text.format — manual index out of range", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"{1}\", 42)\n");
    CHECK(err.find("out of range") != std::string::npos);
}

TEST_CASE("Semantic: std.text.format — mixed automatic and manual placeholders rejected",
          "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"{} {0}\", 42, 43)\n");
    CHECK(err.find("mixes") != std::string::npos);
}

TEST_CASE("Semantic: std.text.format — malformed unclosed brace rejected", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"{\", 42)\n");
    CHECK(err.find("malformed") != std::string::npos);
}

TEST_CASE("Semantic: std.text.format — unsupported vec2 arg type rejected", "[semantic][std-text-format]") {
    auto err = analyze_first_error(STDLIB_EVENTS +
                                   "use std.text as text\n"
                                   "trait Pos:\n"
                                   "    var pos: vec2\n"
                                   "rule S:\n"
                                   "    filter:\n"
                                   "        Pos\n"
                                   "    on tick:\n"
                                   "        let s = text.format(\"pos={}\", pos)\n");
    CHECK(err.find("not supported") != std::string::npos);
}

TEST_CASE("Semantic: std.text.format — extra args with no placeholders rejected", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"Ready\", 42)\n");
    CHECK(err.find("no placeholders") != std::string::npos);
}

// ── Task 2.7: Template-backed entity semantic tests ──────────────────────────

TEST_CASE("Semantic: template-backed entity from local template accepted", "[semantic][entity]") {
    CHECK_FALSE(
        analyze_has_errors("trait Shape:\n"
                           "    var size: float = 16.0\n"
                           "trait Collectible:\n"
                           "    var point_value: int = 10\n"
                           "trait WorldTransform:\n"
                           "    var x: float = 0.0\n"
                           "template BlueGem:\n"
                           "    Shape:\n"
                           "        size = 16.0\n"
                           "    Collectible:\n"
                           "        point_value = 10\n"
                           "    WorldTransform\n"
                           "entity Gem1 from BlueGem:\n"
                           "    WorldTransform:\n"
                           "        x = 250.0\n"));
}

TEST_CASE("Semantic: template-backed entity from undefined template rejected", "[semantic][entity]") {
    auto err = analyze_first_error(
        "trait Shape:\n"
        "    var size: float = 16.0\n"
        "entity Gem1 from MissingTemplate:\n"
        "    Shape:\n"
        "        size = 8.0\n");
    CHECK(err.find("undefined template") != std::string::npos);
    CHECK(err.find("MissingTemplate") != std::string::npos);
}

TEST_CASE("Semantic: template-backed entity from non-template rejected", "[semantic][entity]") {
    auto err = analyze_first_error(
        "trait Collectible:\n"
        "    var point_value: int = 10\n"
        "entity Gem1 from Collectible:\n"
        "    Collectible:\n"
        "        point_value = 5\n");
    CHECK(err.find("not a template") != std::string::npos);
}

TEST_CASE("Semantic: template-backed entity override with unknown trait rejected", "[semantic][entity]") {
    auto err = analyze_first_error(
        "trait Shape:\n"
        "    var size: float = 16.0\n"
        "trait UnrelatedTrait:\n"
        "    var val: int = 0\n"
        "template BlueGem:\n"
        "    Shape:\n"
        "        size = 16.0\n"
        "entity Gem1 from BlueGem:\n"
        "    UnrelatedTrait:\n"
        "        val = 5\n");
    CHECK(err.find("UnrelatedTrait") != std::string::npos);
}

TEST_CASE("Semantic: template-backed entity override with unknown field rejected", "[semantic][entity]") {
    auto err = analyze_first_error(
        "trait Shape:\n"
        "    var size: float = 16.0\n"
        "template BlueGem:\n"
        "    Shape:\n"
        "        size = 16.0\n"
        "entity Gem1 from BlueGem:\n"
        "    Shape:\n"
        "        notafield = 8.0\n");
    CHECK(err.find("notafield") != std::string::npos);
}

TEST_CASE("Semantic: template-backed entity satisfies required field via override", "[semantic][entity]") {
    CHECK_FALSE(
        analyze_has_errors("trait WorldTransform:\n"
                           "    var x: float\n"
                           "template GemTemplate:\n"
                           "    WorldTransform\n"
                           "entity Gem1 from GemTemplate:\n"
                           "    WorldTransform:\n"
                           "        x = 250.0\n"));
}

TEST_CASE("Semantic: template-backed entity missing required field rejected", "[semantic][entity]") {
    auto err = analyze_first_error(
        "trait WorldTransform:\n"
        "    var x: float\n"
        "template GemTemplate:\n"
        "    WorldTransform\n"
        "entity Gem1 from GemTemplate:\n"
        "    WorldTransform\n");
    CHECK(err.find("required field") != std::string::npos);
    CHECK(err.find("x") != std::string::npos);
}

TEST_CASE("Semantic: entity name is not an entity_id expression", "[semantic][entity]") {
    auto err = analyze_first_error(STDLIB_EVENTS +
                                   "trait Shape:\n"
                                   "    var size: float = 16.0\n"
                                   "template BlueGem:\n"
                                   "    Shape\n"
                                   "entity Gem1 from BlueGem:\n"
                                   "    Shape\n"
                                   "rule S:\n"
                                   "    on tick:\n"
                                   "        let x = Gem1\n");
    CHECK(err.find("Gem1") != std::string::npos);
}

TEST_CASE("Semantic: mixed inline and template-backed entity order preserved", "[semantic][entity]") {
    // All three should be accepted without errors in source order
    CHECK_FALSE(
        analyze_has_errors("trait Tag:\n"
                           "    var v: int = 0\n"
                           "template T:\n"
                           "    Tag:\n"
                           "        v = 1\n"
                           "entity A:\n"
                           "    Tag\n"
                           "entity B from T:\n"
                           "    Tag:\n"
                           "        v = 2\n"
                           "entity C:\n"
                           "    Tag\n"));
}

TEST_CASE("Semantic: spawn of entity rejected", "[semantic][entity]") {
    auto err = analyze_first_error(STDLIB_EVENTS +
                                   "trait Tag\n"
                                   "entity Player:\n"
                                   "    Tag\n"
                                   "rule S:\n"
                                   "    on tick:\n"
                                   "        spawn Player:\n"
                                   "            Tag\n");
    CHECK(err.find("Player") != std::string::npos);
    CHECK(err.find("entity") != std::string::npos);
}
// ── Query expression semantic tests ────────────────────────────────────────

TEST_CASE("Semantic: query exists in rule handler returns bool — no errors", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Boss:\n"
                           "    var hp: int\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        if query.exists[Boss]():\n"
                           "            let x = 1\n"));
}

TEST_CASE("Semantic: query count in rule handler returns int — no errors", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Enemy:\n"
                           "    var hp: int\n"
                           "trait Dead\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        let n = query.count[Enemy, not Dead]()\n"));
}

TEST_CASE("Semantic: query first in rule handler returns entity_id — no errors", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Boss:\n"
                           "    var hp: int\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        let t = query.first[Boss]()\n"));
}

TEST_CASE("Semantic: query all in rule handler returns list of entity_id — no errors", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Enemy:\n"
                           "    var hp: int\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        let all = query.all[Enemy]()\n"));
}

TEST_CASE("Semantic: query expression inside pure func is rejected", "[semantic][query]") {
    CHECK(
        analyze_has_errors("use std.query as query\n"
                           "trait Boss:\n"
                           "    var hp: int\n"
                           "func count_bosses() int:\n"
                           "    let n = query.count[Boss]()\n"
                           "    return n\n"));
    CHECK(analyze_first_error("use std.query as query\n"
                              "trait Boss:\n"
                              "    var hp: int\n"
                              "func count_bosses() int:\n"
                              "    let n = query.count[Boss]()\n"
                              "    return n\n")
              .find("world access") != std::string::npos);
}

TEST_CASE("Semantic: undeclared trait in query filter is rejected", "[semantic][query]") {
    CHECK(analyze_first_error("use std.query as query\n" + STDLIB_EVENTS +
                              "rule S:\n"
                              "    on tick:\n"
                              "        let x = query.first[GhostBoss]()\n") ==
          "undeclared trait 'GhostBoss' in query filter");
}

TEST_CASE("Semantic: valid traits in query filter are accepted", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait EnemyAI:\n"
                           "    var active: bool\n"
                           "trait Dead\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        let n = query.count[EnemyAI, not Dead]()\n"));
}

TEST_CASE("Semantic: query parent with entity_id of argument accepted", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Child:\n"
                           "    var child_id: entity_id\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    filter:\n"
                           "        Child\n"
                           "    on tick:\n"
                           "        let p = query.parent(of = child_id)\n"));
}

TEST_CASE("Semantic: query parent of argument must be entity_id", "[semantic][query]") {
    CHECK(analyze_first_error("use std.query as query\n" + STDLIB_EVENTS +
                              "rule S:\n"
                              "    on tick:\n"
                              "        let p = query.parent(of = 42)\n") ==
          "`parent` `of` argument must be of type `entity_id`");
}

TEST_CASE("Semantic: hierarchy snapshot query forms are accepted", "[semantic][query][hierarchy]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Node:\n"
                           "    var id: entity_id\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    filter:\n"
                           "        Node\n"
                           "    on tick:\n"
                           "        let direct = query.children[Node](of = id)\n"
                           "        let pre = query.hierarchy_preorder[Node]()\n"
                           "        let post = query.hierarchy_postorder[Node]()\n"));
}

TEST_CASE("Semantic: query children of argument must be entity_id", "[semantic][query][hierarchy]") {
    CHECK(analyze_first_error("use std.query as query\n" + STDLIB_EVENTS +
                              "rule S:\n"
                              "    on tick:\n"
                              "        let children = query.children(of = 42)\n") ==
          "`children` `of` argument must be of type `entity_id`");
}

TEST_CASE("Semantic: query children requires of argument", "[semantic][query][hierarchy]") {
    CHECK(analyze_first_error("use std.query as query\n"
                              "trait Node\n" +
                              STDLIB_EVENTS +
                              "rule S:\n"
                              "    on tick:\n"
                              "        let children = query.children[Node]()\n") ==
          "`children` requires an `of` named argument");
}

TEST_CASE("Semantic: query nearest requires from argument", "[semantic][query]") {
    CHECK(analyze_first_error("use std.physics.flat.query as query\n" + STDLIB_EVENTS +
                              "trait Transform:\n"
                              "    var pos: vec2\n"
                              "rule S:\n"
                              "    on tick:\n"
                              "        let t = query.nearest[Transform]()\n")
              .find("`nearest` requires") != std::string::npos);
}

TEST_CASE("Semantic: module-qualified query path accepted", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query\n"
                           "trait Boss:\n"
                           "    var hp: int\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        let t = std.query.first[Boss]()\n"));
}

// dsl-model-assets: model asset declarations resolve to model_id
TEST_CASE("Semantic: model asset name resolves to model_id", "[semantic][dsl-model-assets]") {
    CHECK_FALSE(
        analyze_has_errors("asset Robot: model = \"art/robot.glb\"\n"
                           "trait ModelRenderer:\n"
                           "    let model: model_id\n"
                           "entity Bot:\n"
                           "    ModelRenderer:\n"
                           "        model = Robot\n"));
}

TEST_CASE("Semantic: mesh asset rejected where model_id expected", "[semantic][dsl-model-assets]") {
    CHECK(
        analyze_has_errors("asset Rock: mesh = \"rock.glb\"\n"
                           "trait ModelRenderer:\n"
                           "    var model: model_id\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        add ModelRenderer:\n"
                           "            model = Rock\n"));
}

TEST_CASE("Semantic: model asset rejected where mesh_id expected", "[semantic][dsl-model-assets]") {
    CHECK(
        analyze_has_errors("asset Robot: model = \"art/robot.glb\"\n"
                           "trait Renderer:\n"
                           "    var mesh: mesh_id\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        add Renderer:\n"
                           "            mesh = Robot\n"));
}

TEST_CASE("Semantic: model asset accepted where model_id expected in add", "[semantic][dsl-model-assets]") {
    CHECK_FALSE(
        analyze_has_errors("asset Robot: model = \"art/robot.glb\"\n"
                           "trait ModelRenderer:\n"
                           "    var model: model_id\n" +
                           STDLIB_EVENTS +
                           "rule S:\n"
                           "    on tick:\n"
                           "        add ModelRenderer:\n"
                           "            model = Robot\n"));
}

// ── Pair relations (dsl-pair-relations) ─────────────────────────────────────

static const std::string PAIR_TRAITS =
    "trait DynamicBody:\n"
    "    var vx: float\n"
    "trait Transform:\n"
    "    var x: float\n"
    "trait Collider:\n"
    "    var mask: int\n"
    "trait Solid:\n"
    "    var active: bool = true\n"
    "trait GroundContact:\n"
    "    var active: bool = true\n"
    "event Contact:\n"
    "    other: entity_id\n";

TEST_CASE("Semantic: basic pair rule compiles with a tuple-rejecting condition", "[semantic][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "            Transform\n"
                                   "        wall:\n"
                                   "            Solid\n"
                                   "            Collider\n"
                                   "    on fixed_tick:\n"
                                   "        if body != wall:\n"
                                   "            emit Contact to body:\n"
                                   "                other = wall\n"));
}

TEST_CASE("Semantic: pair binding trait field read is accepted and typed", "[semantic][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "            Transform\n"
                                   "        wall:\n"
                                   "            Solid\n"
                                   "            Collider\n"
                                   "    on fixed_tick:\n"
                                   "        if body.Transform.x > 0.0 and wall.Collider.mask > 0:\n"
                                   "            emit Contact:\n"
                                   "                other = wall\n"));
}

TEST_CASE("Semantic: cross-binding trait access is rejected", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule DetectContacts:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        if wall.DynamicBody.vx > 0.0:\n"
                             "            emit Contact:\n"
                             "                other = wall\n"));
}

TEST_CASE("Semantic: binding-local trait alias resolves shortened access", "[semantic][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "        wall:\n"
                                   "            Collider as c\n"
                                   "    on fixed_tick:\n"
                                   "        if wall.c.mask > 0:\n"
                                   "            emit Contact:\n"
                                   "                other = wall\n"));
}

TEST_CASE("Semantic: self-qualified pair trait resolves via longest prefix", "[semantic][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "        wall:\n"
                                   "            test.Collider\n"
                                   "    on fixed_tick:\n"
                                   "        if wall.test.Collider.mask > 0:\n"
                                   "            emit Contact:\n"
                                   "                other = wall\n"));
}

TEST_CASE("Semantic: duplicate pair binding name is rejected", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Bad:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "        body:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        emit Contact:\n"
                             "            other = body\n"));
}

TEST_CASE("Semantic: duplicate trait entry in one binding is ambiguous", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Bad:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "            DynamicBody\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        emit Contact:\n"
                             "            other = body\n"));
}

TEST_CASE("Semantic: unknown trait in pair binding is reported", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Bad:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            Nonexistent\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        emit Contact:\n"
                             "            other = body\n"));
}

TEST_CASE("Semantic: self is rejected in a pair handler", "[semantic][pair-relations][self]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule DetectContacts:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        if self == body:\n"
                             "            emit Contact:\n"
                             "                other = wall\n"));
}

TEST_CASE("Semantic: assignment through a pair trait path is rejected as read-only", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule DetectContacts:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "            Transform\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        body.Transform.x += 1.0\n"));
}

TEST_CASE("Semantic: compound assignment through a pair trait path is rejected as read-only",
          "[semantic][pair-relations]") {
    auto message = analyze_first_error(STDLIB_EVENTS + PAIR_TRAITS +
                                       "rule DetectContacts:\n"
                                       "    pairs:\n"
                                       "        body:\n"
                                       "            DynamicBody\n"
                                       "        wall:\n"
                                       "            Solid\n"
                                       "    on fixed_tick:\n"
                                       "        body.DynamicBody.vx = 1.0\n");
    CHECK(message.find("read-only") != std::string::npos);
}

TEST_CASE("Semantic: bare assignment has no implicit entity in a pair handler", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule DetectContacts:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        vx = 1.0\n"));
}

TEST_CASE("Semantic: trait match directly on a pair binding is rejected", "[semantic][pair-relations]") {
    auto message = analyze_first_error(STDLIB_EVENTS + PAIR_TRAITS +
                                       "rule DetectContacts:\n"
                                       "    pairs:\n"
                                       "        body:\n"
                                       "            DynamicBody\n"
                                       "        wall:\n"
                                       "            Solid\n"
                                       "    on fixed_tick:\n"
                                       "        match body:\n"
                                       "            DynamicBody as db =>\n"
                                       "                emit Contact:\n"
                                       "                    other = wall\n");
    CHECK(message.find("trait-match directly on binding") != std::string::npos);
}

TEST_CASE("Semantic: bare destroy has no implicit target in a pair handler", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule DetectContacts:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        destroy\n"));
}

TEST_CASE("Semantic: bare project has no implicit target in a pair handler", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule DetectContacts:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        project GroundContact\n"));
}

TEST_CASE("Semantic: explicit-target destroy, add, remove, project, and spawn are accepted in a pair handler",
          "[semantic][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                                   "template Debris:\n"
                                   "    Solid\n"
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "        wall:\n"
                                   "            Solid\n"
                                   "    on fixed_tick:\n"
                                   "        project GroundContact to body\n"
                                   "        add Solid to body\n"
                                   "        remove Solid from wall\n"
                                   "        destroy wall\n"
                                   "        spawn Debris:\n"
                                   "            Solid\n"));
}

TEST_CASE("Semantic: untargeted emit remains a valid broadcast in a pair handler", "[semantic][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "        wall:\n"
                                   "            Solid\n"
                                   "    on fixed_tick:\n"
                                   "        emit Contact:\n"
                                   "            other = wall\n"));
}

TEST_CASE("Semantic: pair handler contract records domain, bindings, bound reads, and projects",
          "[semantic][pair-relations][handler-contracts]") {
    auto result = analyze(STDLIB_EVENTS + PAIR_TRAITS +
                          "rule DetectContacts:\n"
                          "    pairs:\n"
                          "        body:\n"
                          "            DynamicBody\n"
                          "            Transform\n"
                          "        wall:\n"
                          "            Solid\n"
                          "            Collider\n"
                          "    on fixed_tick:\n"
                          "        if body != wall and body.Transform.x > 0.0 and wall.Collider.mask > 0:\n"
                          "            emit Contact to body:\n"
                          "                other = wall\n"
                          "            project GroundContact to body\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    CHECK(contract.domain_kind == HandlerDomainKind::Pair);
    CHECK_FALSE(contract.is_selectionless());
    REQUIRE(contract.pair_bindings.size() == 2);
    CHECK(contract.pair_bindings[0].name == "body");
    CHECK(contract.pair_bindings[1].name == "wall");

    const auto transform_id = make_symbol_id(SymbolKind::Trait, "test", "Transform");
    const auto collider_id  = make_symbol_id(SymbolKind::Trait, "test", "Collider");
    const auto ground_id    = make_symbol_id(SymbolKind::Trait, "test", "GroundContact");

    CHECK(contract.reads.contains(transform_id));
    CHECK(contract.reads.contains(collider_id));
    CHECK_FALSE(contract.writes.contains(transform_id));
    CHECK_FALSE(contract.writes.contains(collider_id));
    CHECK(contract.projects.contains(ground_id));
    CHECK_FALSE(contract.writes.contains(ground_id));

    const BoundTraitAccess body_transform_read{.binding_index = 0, .trait = transform_id};
    const BoundTraitAccess wall_collider_read{.binding_index = 1, .trait = collider_id};
    CHECK(std::ranges::find(contract.bound_reads, body_transform_read) != contract.bound_reads.end());
    CHECK(std::ranges::find(contract.bound_reads, wall_collider_read) != contract.bound_reads.end());
}

TEST_CASE("Semantic: unary rules keep their existing domain and are unaffected by pair support",
          "[semantic][pair-relations][handler-contracts]") {
    auto result = analyze(STDLIB_EVENTS +
                          "trait Pos:\n"
                          "    var x: float\n"
                          "rule Move:\n"
                          "    filter:\n"
                          "        Pos\n"
                          "    on tick:\n"
                          "        x = x + tick.dt\n");
    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    CHECK(contract.domain_kind == HandlerDomainKind::Unary);
    CHECK_FALSE(contract.is_selectionless());
    CHECK(contract.pair_bindings.empty());
    CHECK(contract.bound_reads.empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
