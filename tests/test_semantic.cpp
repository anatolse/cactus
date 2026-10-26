// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <set>

using namespace cactus;

// Standard lifecycle event declarations (normally from std.core imports)
static const std::string& stdlib_events() {
    static const std::string value =
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
    return value;
}

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

static DecoratedProgram analyze_with_imports(const std::string& source, const ModuleImports& imports) {
    const std::string src = starts_with_module_decl(source) ? source : "module test\n" + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(program, imports);
    REQUIRE_FALSE(errors.has_errors());
    return result;
}

static std::pair<DecoratedProgram, std::vector<Diagnostic>> analyze_with_diagnostics(const std::string& source) {
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
    return {std::move(result), errors.diagnostics()};
}

static bool analyze_has_errors(const std::string& source);

TEST_CASE("Semantic: template defaults reject later parameters inside constructors", "[semantic][template-parameters]") {
    CHECK(analyze_has_errors(R"(trait Data
template Item(a: vec2 = vec2(b), b: float = 1.0):
    Data
)"));
}

TEST_CASE("Semantic: template values cannot capture handler state at load time", "[semantic][template-parameters]") {
    CHECK(analyze_has_errors(R"(trait Data
template Item(value: int):
    Data
entity First from Item(value = handler_local)
)"));
}

TEST_CASE("Semantic: template parameters bind dependent defaults", "[semantic][template-parameters]") {
    auto program = analyze(R"(trait Motion:
    var velocity: vec2
template Projectile(speed: float, velocity: vec2 = vec2(speed, 0.0)):
    Motion:
        velocity = velocity
entity First from Projectile(speed = 12.0)
)");
    CHECK(program.module_name == "test");
}

TEST_CASE("Semantic: template argument diagnostics accumulate", "[semantic][template-parameters]") {
    const std::string source = R"(module test
trait Data:
    var value: int
template Item(value: int):
    Data:
        value = value
entity Missing from Item()
entity Duplicate from Item(value = 1, value = 2)
entity Wrong from Item(value = true)
entity Unknown from Item(extra = 1)
)";
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(ast);
    REQUIRE(errors.error_count() >= 4);
    for (const auto& diagnostic : errors.diagnostics()) {
        CHECK_FALSE(diagnostic.message.empty());
    }
}

TEST_CASE("Semantic: template binding plans preserve source order then dependent defaults", "[semantic][template-parameters]") {
    ErrorReporter errors;
    Lexer lexer(R"(module test
trait Data:
    var value: int
template Item(a: int, b: int, c: int = a + b):
    Data:
        value = c
entity First from Item(b = 2, a = 1)
)", "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(ast);
    REQUIRE_FALSE(errors.has_errors());
    const auto& entity = std::get<EntityNode>(ast.declarations.back());
    REQUIRE(entity.initializers.size() == 3);
    CHECK(entity.initializers[0].index == 1);
    CHECK(entity.initializers[1].index == 0);
    CHECK(entity.initializers[2].index == 2);
    const auto& sum = std::get<BinaryExpr>(entity.initializers[2].value->expr);
    CHECK(std::get<IdentExpr>(sum.left->expr).template_slot == 0);
    CHECK(std::get<IdentExpr>(sum.right->expr).template_slot == 1);
}

TEST_CASE("Semantic: spawn arguments read trait values and event data", "[semantic][template-parameters]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + R"(trait Pos:
    var x: float
trait Motion:
    var speed: float
template Projectile(speed: float):
    Motion:
        speed = speed
rule Fire:
    each:
        Pos
    on tick:
        spawn Projectile(speed = x + tick.dt)
)"));
}

// A child template referenced through this module's own name must splice the
// same traits the bare name does, not silently resolve to nothing.
TEST_CASE("Semantic: self-qualified child template reference keeps the template's traits",
          "[semantic][template-parameters][hierarchy]") {
    ErrorReporter errors;
    Lexer lexer(R"(module v
trait Parent:
    var parent: entity_id
trait Sink:
    var value: int
    var extra: int
template Leaf:
    Sink:
        value = 9
entity Root:
    Sink
    children:
        entity Kid from v.Leaf:
            Sink:
                extra = 1
)",
                "test.cactus",
                errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(ast);
    REQUIRE_FALSE(errors.has_errors());

    const auto& root = std::get<EntityNode>(ast.declarations.back());
    REQUIRE(root.children.size() == 1);
    const auto& kid = root.children[0];
    REQUIRE(kid.traits.size() == 1);
    const auto& assignments = kid.traits[0].assignments;
    CHECK(std::ranges::any_of(assignments, [](const auto& a) { return a.name == "value"; }));
    CHECK(std::ranges::any_of(assignments, [](const auto& a) { return a.name == "extra"; }));
}

TEST_CASE("Semantic: template arguments and defaults read module constants", "[semantic][template-parameters]") {
    CHECK_FALSE(analyze_has_errors(R"(const:
    BASE = 4
    DOUBLE = 8
trait Data:
    var value: int
template Item(value: int = BASE, other: int = DOUBLE):
    Data:
        value = value
entity First from Item(value = BASE)
)"));
}

TEST_CASE("Semantic: spawn arguments read aliased filter bindings", "[semantic][template-parameters]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + R"(trait Source:
    var value: int
trait Data:
    var value: int
template Made(value: int):
    Data:
        value = value
rule R:
    each:
        Source as s
    on tick:
        spawn Made(value = s.value + 1)
)"));
}

// A trait read reaching the scheduler only through an argument still has to
// order this handler after the one that writes it.
TEST_CASE("Semantic: spawn argument trait reads reach the handler contract", "[semantic][template-parameters]") {
    auto program        = analyze(stdlib_events() + R"(trait Source:
    var value: int
trait Data:
    var value: int
template Made(value: int):
    Data:
        value = value
rule Reader:
    each:
        Source
    on tick:
        spawn Made(value = value)
)");
    const auto contract = std::ranges::find_if(
        program.handler_contracts, [](const auto& candidate) { return candidate.rule.local_name == "Reader"; });
    REQUIRE(contract != program.handler_contracts.end());
    CHECK(std::ranges::any_of(contract->reads, [](const SymbolId& read) { return read.local_name == "Source"; }));
}

TEST_CASE("Semantic: template parameters reject duplicates and child-role collisions",
          "[semantic][template-parameters]") {
    CHECK(analyze_has_errors(R"(trait Data:
    var value: int
template Item(value: int, value: int):
    Data:
        value = value
)"));
    // The `children:` block needs `Parent`, which this module has no import for,
    // so pin the assertion to the collision diagnostic itself.
    ErrorReporter errors;
    Lexer lexer(R"(module test
trait Data:
    var value: int
template Item(Child: int):
    Data:
        value = Child
    children:
        entity Child:
            Data:
                value = 1
)",
                "test.cactus",
                errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(ast);
    CHECK(std::ranges::any_of(errors.diagnostics(), [](const Diagnostic& diagnostic) {
        return diagnostic.message == "child role conflicts with template parameter 'Child'";
    }));
}

TEST_CASE("Semantic: templates cannot be called as ordinary values", "[semantic][template-parameters]") {
    ErrorReporter errors;
    Lexer lexer(R"(module test
trait Data
template Item(value: int):
    Data
func invalid() int:
    return Item(1)
)", "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(ast);
    CHECK(errors.has_errors());
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

TEST_CASE("Semantic: rule filter — valid trait", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Pos:\n"
                                                     "    var x: float\n"
                                                     "rule Move:\n"
                                                     "    each: \n"
                                                     "        Pos\n"
                                                     "    on tick:\n"
                                                     "        x = x + tick.dt\n"));
}

TEST_CASE("Semantic: rule filter — unknown trait", "[semantic]") {
    CHECK(analyze_has_errors(stdlib_events() + "rule Bad:\n"
                                               "    each: \n"
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
                           "    each: \n"
                           "        Pos\n"
                           "    on Hit:\n"
                           "        x = x + 1.0\n"));
}

TEST_CASE("Semantic: event handler — unknown event", "[semantic]") {
    CHECK(
        analyze_has_errors("trait Pos:\n"
                           "    var x: float\n"
                           "rule Bad:\n"
                           "    each: \n"
                           "        Pos\n"
                           "    on FakeEvent:\n"
                           "        x = 0\n"));
}

TEST_CASE("Semantic: emit payload with unknown field — error", "[semantic]") {
    CHECK(analyze_has_errors(stdlib_events() + "event Damage:\n"
                                               "    amount: int\n"
                                               "rule Combat:\n"
                                               "    on tick:\n"
                                               "        emit Damage:\n"
                                               "            badfield = 1\n"));
}

TEST_CASE("Semantic: emit payload with valid field — ok", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "event Damage:\n"
                                                     "    amount: int\n"
                                                     "rule Combat:\n"
                                                     "    on tick:\n"
                                                     "        emit Damage:\n"
                                                     "            amount = 1\n"));
}

TEST_CASE("Semantic: extern event cannot be emitted by authored code — error", "[semantic][persistence]") {
    // Mirrors std.persistence's SaveCompleted/SaveFailed: an extern event is
    // runtime-injected only, the same restriction std.core.frame already has.
    CHECK(analyze_has_errors(stdlib_events() + "extern event SaveCompleted:\n"
                                               "    slot: string\n"
                                               "rule Bad:\n"
                                               "    on tick:\n"
                                               "        emit SaveCompleted:\n"
                                               "            slot = \"x\"\n"));
}

TEST_CASE("Semantic: extern event can still be handled by authored code — ok", "[semantic][persistence]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "extern event SaveCompleted:\n"
                                                     "    slot: string\n"
                                                     "trait Pos:\n"
                                                     "    var x: float\n"
                                                     "rule Handle:\n"
                                                     "    each:\n"
                                                     "        Pos\n"
                                                     "    on SaveCompleted as outcome:\n"
                                                     "        x = 0.0\n"));
}

TEST_CASE("Semantic: restore's extern event cannot be emitted by authored code — error",
          "[semantic][persistence]") {
    // Same rule as SaveCompleted/SaveFailed above, applied to RestoreCompleted:
    // the restriction is generic to extern events, not specific to save.
    CHECK(analyze_has_errors(stdlib_events() + "extern event RestoreCompleted:\n"
                                               "    slot: string\n"
                                               "rule Bad:\n"
                                               "    on tick:\n"
                                               "        emit RestoreCompleted:\n"
                                               "            slot = \"x\"\n"));
}

TEST_CASE("Semantic: restore's extern event can still be handled by authored code — ok",
          "[semantic][persistence]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "extern event RestoreCompleted:\n"
                                                     "    slot: string\n"
                                                     "trait Pos:\n"
                                                     "    var x: float\n"
                                                     "rule Handle:\n"
                                                     "    each:\n"
                                                     "        Pos\n"
                                                     "    on RestoreCompleted as outcome:\n"
                                                     "        x = 0.0\n"));
}

TEST_CASE("Semantic: tick handler — always valid", "[semantic]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Pos:\n"
                                                     "    var x: float\n"
                                                     "rule Move:\n"
                                                     "    each: \n"
                                                     "        Pos\n"
                                                     "    on tick:\n"
                                                     "        x = x + tick.dt\n"));
}

TEST_CASE("Semantic: dependency graph built", "[semantic]") {
    auto result = analyze(stdlib_events() +
                          "trait Pos:\n"
                          "    var x: float\n"
                          "rule Move:\n"
                          "    each: \n"
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

TEST_CASE("Semantic: cursor capture is a public bool-taking effect", "[semantic][extern-func][input]") {
    auto result = analyze("pub extern func set_cursor_captured(captured: bool)\n");
    REQUIRE(result.funcs.count("set_cursor_captured") == 1);
    const auto& function = result.funcs.at("set_cursor_captured");
    CHECK(function.is_pub);
    CHECK(function.is_extern);
    REQUIRE(function.params.size() == 1);
    CHECK(function.params.front().name == "captured");
    CHECK(function.params.front().type.kind == TypeKind::Bool);
    CHECK_FALSE(function.return_type.has_value());
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                                     "    var x: float\n"
                                                     "extern rule SpriteRenderer:\n"
                                                     "    each:\n"
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
    CHECK(analyze_has_errors(stdlib_events() + "trait T:\n"
                                               "    var x: float\n"
                                               "extern rule A:\n"
                                               "    each:\n"
                                               "        T\n"
                                               "    after:\n"
                                               "        B\n"
                                               "rule B:\n"
                                               "    each:\n"
                                               "        T\n"
                                               "    after:\n"
                                               "        A\n"
                                               "    on tick:\n"
                                               "        x = 1.0\n"));
}

// ── rule-ordering-and-trait-cleanup semantic tests ────────────────────────

// Task 12.5: after: referencing unknown rule reports error
TEST_CASE("Semantic: after: unknown rule reports error", "[semantic][rule-ordering]") {
    CHECK(analyze_has_errors(stdlib_events() + "trait T:\n"
                                               "    var x: float\n"
                                               "rule A:\n"
                                               "    after:\n"
                                               "        NonExistentSystem\n"
                                               "    on tick:\n"
                                               "        x = 1.0\n"));
}

// Task 12.6: direct after: cycle reports error
TEST_CASE("Semantic: after: direct cycle reports error", "[semantic][rule-ordering]") {
    CHECK(analyze_has_errors(stdlib_events() + "trait T:\n"
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
    auto result = analyze(stdlib_events() +
                          "trait T:\n"
                          "    var x: float\n"
                          "rule A:\n"
                          "    each: \n"
                          "       T\n"
                          "    on tick:\n"
                          "        x = 1.0\n"
                          "rule B:\n"
                          "    each:\n"
                          "       T\n"
                          "    after:\n"
                          "        A\n"
                          "    on tick:\n"
                          "        x = 2.0\n"
                          "rule C:\n"
                          "    each:\n"
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                                     "    var pos: vec2\n"
                                                     "trait Sprite:\n"
                                                     "    var layer: int\n"
                                                     "rule Render:\n"
                                                     "    each:\n"
                                                     "        Position as p\n"
                                                     "        Sprite as s\n"
                                                     "    order by:\n"
                                                     "        s.layer asc\n"
                                                     "        p.pos.y desc\n"
                                                     "    on tick:\n"
                                                     "        let x = 1\n"));
}

TEST_CASE("Semantic: order by alias not in filter errors", "[semantic][rule-order-by]") {
    CHECK(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                               "    var pos: vec2\n"
                                               "rule Render:\n"
                                               "    each:\n"
                                               "        Position as p\n"
                                               "    order by:\n"
                                               "        s.pos.y asc\n"
                                               "    on tick:\n"
                                               "        let x = 1\n"));
}

TEST_CASE("Semantic: order by non-orderable type errors", "[semantic][rule-order-by]") {
    CHECK(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                               "    var pos: vec2\n"
                                               "rule Render:\n"
                                               "    each:\n"
                                               "        Position as p\n"
                                               "    order by:\n"
                                               "        p.pos asc\n"
                                               "    on tick:\n"
                                               "        let x = 1\n"));
}

TEST_CASE("Semantic: order by invalid vec2 member errors", "[semantic][rule-order-by]") {
    CHECK(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                               "    var pos: vec2\n"
                                               "rule Render:\n"
                                               "    each:\n"
                                               "        Position as p\n"
                                               "    order by:\n"
                                               "        p.pos.z asc\n"
                                               "    on tick:\n"
                                               "        let x = 1\n"));
}

TEST_CASE("Semantic: order by valid color channel", "[semantic][rule-order-by][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Tint:\n"
                                                     "    var tint: color\n"
                                                     "rule Render:\n"
                                                     "    each:\n"
                                                     "        Tint as t\n"
                                                     "    order by:\n"
                                                     "        t.tint.a desc\n"
                                                     "    on tick:\n"
                                                     "        let x = 1\n"));
}

// Mirrors "order by invalid vec2 member errors" above: validate_order_by_key
// reports the same generic "order by field '...' is not valid" diagnostic for
// every unresolvable member-chain segment regardless of the preceding type,
// so this checks rejection (and that the invalid member spelling reaches the
// message via the field path) rather than a color-specific message shape.
TEST_CASE("Semantic: order by invalid color member errors", "[semantic][rule-order-by][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() +
                                       "trait Tint:\n"
                                       "    var tint: color\n"
                                       "rule Render:\n"
                                       "    each:\n"
                                       "        Tint as t\n"
                                       "    order by:\n"
                                       "        t.tint.w asc\n"
                                       "    on tick:\n"
                                       "        let x = 1\n");
    CHECK(message.contains("t.tint.w"));
}

TEST_CASE("Semantic: impure order by sort key is rejected", "[semantic][rule-order-by]") {
    auto err = analyze_first_error(stdlib_events() +
                                   "trait Health:\n"
                                   "    var value: int\n"
                                   "template Marker:\n"
                                   "    Health:\n"
                                   "        value = 1\n"
                                   "rule Render:\n"
                                   "    each:\n"
                                   "        Health as h\n"
                                   "    order by:\n"
                                   "        spawn Marker:\n"
                                   "            Health:\n"
                                   "                value = 1\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("must be pure"));
}

TEST_CASE("Semantic: order by accepts a computed sort key over a single filter alias", "[semantic][rule-order-by]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Health:\n"
                                                     "    var value: int\n"
                                                     "func doubled(v: int) int:\n"
                                                     "    return v * 2\n"
                                                     "rule Render:\n"
                                                     "    each:\n"
                                                     "        Health as h\n"
                                                     "    order by:\n"
                                                     "        doubled(h.value) desc\n"
                                                     "    on tick:\n"
                                                     "        let x = 1\n"));
}

TEST_CASE("Semantic: order by rejects a computed sort key with a non-scalar result type",
          "[semantic][rule-order-by]") {
    CHECK(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                               "    var pos: vec2\n"
                                               "func identity(v: vec2) vec2:\n"
                                               "    return v\n"
                                               "rule Render:\n"
                                               "    each:\n"
                                               "        Position as p\n"
                                               "    order by:\n"
                                               "        identity(p.pos) asc\n"
                                               "    on tick:\n"
                                               "        let x = 1\n"));
}

TEST_CASE("Semantic: order by without filter or pairs reports the specific diagnostic", "[semantic][rule-order-by]") {
    auto err = analyze_first_error(stdlib_events() +
                                   "rule NoDomain:\n"
                                   "    order by:\n"
                                   "        1\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("`order by:` requires an `each:` or `pairs:` clause"));
}

TEST_CASE("Semantic: order by accepts a cross-binding computed pair sort key", "[semantic][rule-order-by][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                                     "    var pos: vec2\n"
                                                     "rule Contacts:\n"
                                                     "    pairs:\n"
                                                     "        actor:\n"
                                                     "            Position\n"
                                                     "        victim:\n"
                                                     "            Position\n"
                                                     "    order by:\n"
                                                     "        actor[Position].pos.x - victim[Position].pos.x desc\n"
                                                     "    on fixed_tick:\n"
                                                     "        let x = 1\n"));
}

TEST_CASE("Semantic: order by rejects a pair sort key naming an undeclared binding",
          "[semantic][rule-order-by][pair-relations]") {
    auto err = analyze_first_error(stdlib_events() +
                                   "trait Position:\n"
                                   "    var pos: vec2\n"
                                   "rule Contacts:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Position\n"
                                   "        victim:\n"
                                   "            Position\n"
                                   "    order by:\n"
                                   "        other.Position.pos.x asc\n"
                                   "    on fixed_tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("pair binding"));
}

// Regression guard (task 4.3): only order by: was carved out of pairs:'s
// unary-clause exclusion — each:/without: combined with pairs: must still
// be rejected end to end.
TEST_CASE("Semantic: pairs combined with filter is still rejected", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                               "    var pos: vec2\n"
                                               "rule Bad:\n"
                                               "    pairs:\n"
                                               "        actor:\n"
                                               "            Position\n"
                                               "        victim:\n"
                                               "            Position\n"
                                               "    each:\n"
                                               "        Position\n"
                                               "    on fixed_tick:\n"
                                               "        let x = 1\n"));
}

TEST_CASE("Semantic: pairs combined with exclude is still rejected", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                               "    var pos: vec2\n"
                                               "rule Bad:\n"
                                               "    pairs:\n"
                                               "        actor:\n"
                                               "            Position\n"
                                               "        victim:\n"
                                               "            Position\n"
                                               "    without:\n"
                                               "        Position\n"
                                               "    on fixed_tick:\n"
                                               "        let x = 1\n"));
}

TEST_CASE("Semantic: vec2/vec3 splat constructors type-check as trait field defaults",
          "[semantic][vector-expressions]") {
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
    CHECK_FALSE(
        analyze_has_errors("trait Position:\n"
                           "    var pos2: vec2 = vec2(400.0, 40.0)\n"
                           "    var pos3: vec3 = vec3(1.0, 2.0, 3.0)\n"));
}

TEST_CASE("Semantic: vec2 wrong constructor arity rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(
        "trait Position:\n"
        "    var pos: vec2 = vec2(1.0, 2.0, 3.0)\n");
    CHECK(message.contains("vec2"));
    CHECK(message.contains('3'));
}

TEST_CASE("Semantic: vec3 wrong constructor arity rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(
        "trait Position:\n"
        "    var pos: vec3 = vec3(1.0, 2.0)\n");
    CHECK(message.contains("vec3"));
    CHECK(message.contains('2'));
}

TEST_CASE("Semantic: vec2 zero-argument constructor rejected", "[semantic][vector-expressions]") {
    CHECK(
        analyze_has_errors("trait Position:\n"
                           "    var pos: vec2 = vec2()\n"));
}

TEST_CASE("Semantic: vec2 non-float constructor argument rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(
        "trait Position:\n"
        "    var pos: vec2 = vec2(\"0\", \"0\")\n");
    CHECK(message.contains("string"));
}

TEST_CASE("Semantic: vec2/vec3 constructors accept int arguments, promoted to float",
          "[semantic][vector-expressions]") {
    auto result = analyze(
        "trait Position:\n"
        "    var pos2: vec2 = vec2(1, 0)\n"
        "    var pos3: vec3 = vec3(1, 0, 0)\n");
    REQUIRE(result.traits.count("Position"));
    auto& trait = result.traits["Position"];
    REQUIRE(trait.fields.size() == 2);
    CHECK(trait.fields[0].type.kind == TypeKind::Vec2);
    CHECK(trait.fields[1].type.kind == TypeKind::Vec3);

    CHECK_FALSE(analyze_has_errors(stdlib_events() + "rule IntArgs:\n"
                                                     "    on tick:\n"
                                                     "        for k in range(0, 1):\n"
                                                     "            let a = vec2(k, 0)\n"
                                                     "            let b = vec3(k, 0, 0)\n"));
}

TEST_CASE("Semantic: vec2 non-numeric constructor argument still rejected alongside int promotion",
          "[semantic][vector-expressions]") {
    auto message = analyze_first_error(
        "trait Position:\n"
        "    var pos: vec2 = vec2(\"0\", \"0\")\n");
    CHECK(message.contains("string"));
}

TEST_CASE("Semantic: vec2 splat constructor default mismatched against vec3 field rejected",
          "[semantic][vector-expressions]") {
    CHECK(
        analyze_has_errors("trait Position:\n"
                           "    var pos: vec3 = vec2(0.0)\n"));
}

// color(...) isn't accepted as a trait field default (unlike vec2(...)/
// vec3(...)) - it isn't in check_const's allowed-constructor list, so these
// exercise the constructor through a handler body instead, same as the
// "constructors resolve to real types outside trait defaults" case below.
TEST_CASE("Semantic: color constructor with four channel arguments is accepted", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "rule Test:\n"
                                                     "    on tick:\n"
                                                     "        let c = color(1.0, 0.0, 0.0, 1.0)\n"));
}

TEST_CASE("Semantic: color wrong constructor arity rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() +
                                       "rule Test:\n"
                                       "    on tick:\n"
                                       "        let c = color(1.0, 0.0, 0.0)\n");
    CHECK(message.contains("color"));
    CHECK(message.contains('3'));
}

TEST_CASE("Semantic: color non-numeric constructor argument rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() +
                                       "rule Test:\n"
                                       "    on tick:\n"
                                       "        let c = color(\"0\", 0.0, 0.0, 1.0)\n");
    CHECK(message.contains("string"));
}

TEST_CASE("Semantic: color constructor accepts int arguments, promoted to float", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "rule Test:\n"
                                                     "    on tick:\n"
                                                     "        for k in range(0, 1):\n"
                                                     "            let c = color(k, 0, 0, 1)\n"));
}

TEST_CASE("Semantic: vec2/vec3 constructors resolve to real types outside trait defaults",
          "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Position:\n"
                                                     "    var pos: vec2\n"
                                                     "rule Move:\n"
                                                     "    each:\n"
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
    CHECK_FALSE(
        analyze_has_errors("pub enum GizmoMode:\n"
                           "    Select\n"
                           "    Translate\n"
                           "trait EditorState:\n"
                           "    var mode: GizmoMode = GizmoMode.Select\n"));
}

TEST_CASE("Semantic: enum-qualified literal trait field default resolves to the enum type", "[semantic][enum]") {
    auto result = analyze(
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "trait EditorState:\n"
        "    var mode: GizmoMode = GizmoMode.Select\n");
    REQUIRE(result.traits.count("EditorState"));
    auto& trait = result.traits["EditorState"];
    REQUIRE(trait.fields.size() == 1);
    CHECK(trait.fields[0].type.kind == TypeKind::Enum);
}

TEST_CASE("Semantic: enum-qualified literal naming an unknown variant rejected as trait default", "[semantic][enum]") {
    CHECK(
        analyze_has_errors("pub enum GizmoMode:\n"
                           "    Select\n"
                           "trait EditorState:\n"
                           "    var mode: GizmoMode = GizmoMode.Translate\n"));
}

static const std::string& vector_matrix_stdlib() {
    static const std::string value =
        "trait Position:\n"
        "    var pos2: vec2\n"
        "    var pos3: vec3\n"
        "trait Motion:\n"
        "    var velocity2: vec2\n"
        "    var velocity3: vec3\n";
    return value;
}

TEST_CASE("Semantic: vec2/vec3 component addition and subtraction typecheck", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Position as p\n"
                                   "        Motion as m\n"
                                   "    on tick:\n"
                                   "        let sum2 = p.pos2 + m.velocity2\n"
                                   "        let diff2 = p.pos2 - m.velocity2\n"
                                   "        let sum3 = p.pos3 + m.velocity3\n"
                                   "        let diff3 = p.pos3 - m.velocity3\n"));
}

TEST_CASE("Semantic: vec2/vec3 scalar multiply is commutative in both orders", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        let a = p.pos2 * tick.dt\n"
                                   "        let b = tick.dt * p.pos2\n"
                                   "        let c = p.pos3 * tick.dt\n"
                                   "        let d = tick.dt * p.pos3\n"));
}

TEST_CASE("Semantic: vec2/vec3 divide by float typechecks", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Position as p\n"
                                   "        Motion as m\n"
                                   "    on tick:\n"
                                   "        let component_product = p.pos2 * m.velocity2 + p.pos2\n"));
}

TEST_CASE("Semantic: mismatched vec2/vec3 dimensions rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() + vector_matrix_stdlib() +
                                       "rule Test:\n"
                                       "    each:\n"
                                       "        Position as p\n"
                                       "    on tick:\n"
                                       "        let bad = p.pos2 + p.pos3\n");
    CHECK(message.contains("vec2"));
    CHECK(message.contains("vec3"));
}

TEST_CASE("Semantic: vector-by-vector division rejected", "[semantic][vector-expressions]") {
    CHECK(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                             "rule Test:\n"
                             "    each:\n"
                             "        Position as p\n"
                             "        Motion as m\n"
                             "    on tick:\n"
                             "        let bad = p.pos2 / m.velocity2\n"));
}

TEST_CASE("Semantic: vector plus bare scalar rejected", "[semantic][vector-expressions]") {
    CHECK(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                             "rule Test:\n"
                             "    each:\n"
                             "        Position as p\n"
                             "    on tick:\n"
                             "        let bad = p.pos2 + tick.dt\n"));
}

static const std::string& color_matrix_stdlib() {
    static const std::string value =
        "trait Tint:\n"
        "    var primary: color\n"
        "    var secondary: color\n";
    return value;
}

TEST_CASE("Semantic: color component addition and subtraction typecheck", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + color_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Tint as t\n"
                                   "    on tick:\n"
                                   "        let sum = t.primary + t.secondary\n"
                                   "        let diff = t.primary - t.secondary\n"));
}

TEST_CASE("Semantic: color scalar multiply is commutative in both orders", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + color_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Tint as t\n"
                                   "    on tick:\n"
                                   "        let a = t.primary * tick.dt\n"
                                   "        let b = tick.dt * t.primary\n"));
}

TEST_CASE("Semantic: color divide by float typechecks", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + color_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Tint as t\n"
                                   "    on tick:\n"
                                   "        let a = t.primary / tick.dt\n"));
}

TEST_CASE("Semantic: color component-wise multiply stays color-typed, not a dot product",
          "[semantic][vector-expressions]") {
    // Same reasoning as the vec2/vec3 case above: if `t.primary * t.secondary`
    // inferred as some scalar, adding it to another color below would be
    // rejected (no such row) - accepting it proves the result stayed `color`.
    CHECK_FALSE(analyze_has_errors(stdlib_events() + color_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Tint as t\n"
                                   "    on tick:\n"
                                   "        let component_product = t.primary * t.secondary + t.primary\n"));
}

TEST_CASE("Semantic: color plus bare scalar rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() + color_matrix_stdlib() +
                                       "rule Test:\n"
                                       "    each:\n"
                                       "        Tint as t\n"
                                       "    on tick:\n"
                                       "        let bad = t.primary + tick.dt\n");
    CHECK(message.contains("color"));
    CHECK(message.contains("float"));
    CHECK(message.contains('+'));
}

TEST_CASE("Semantic: color-by-color division rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() + color_matrix_stdlib() +
                                       "rule Test:\n"
                                       "    each:\n"
                                       "        Tint as t\n"
                                       "    on tick:\n"
                                       "        let bad = t.primary / t.secondary\n");
    CHECK(message.contains("color"));
    CHECK(message.contains('/'));
}

TEST_CASE("Semantic: vec2 addition compound assignment on a trait field", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Position as p\n"
                                   "        Motion as m\n"
                                   "    on tick:\n"
                                   "        p.pos2 += m.velocity2\n"));
}

TEST_CASE("Semantic: vec3 scalar-multiply compound assignment on a trait field", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        p.pos3 *= tick.dt\n"));
}

TEST_CASE("Semantic: vec2 component-wise compound multiply on a trait field", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Position as p\n"
                                   "        Motion as m\n"
                                   "    on tick:\n"
                                   "        p.pos2 *= m.velocity2\n"));
}

TEST_CASE("Semantic: vec2 divide compound assignment on a trait field", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        p.pos2 /= tick.dt\n"));
}

TEST_CASE("Semantic: vec2 compound assignment on a handler-local var", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + vector_matrix_stdlib() +
                                   "rule Test:\n"
                                   "    each:\n"
                                   "        Position as p\n"
                                   "    on tick:\n"
                                   "        var accel = vec2(0.0)\n"
                                   "        accel += p.pos2\n"
                                   "        accel *= tick.dt\n"));
}

TEST_CASE("Semantic: incompatible vec2 compound-assignment operand rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() + vector_matrix_stdlib() +
                                       "rule Test:\n"
                                       "    each:\n"
                                       "        Position as p\n"
                                       "    on tick:\n"
                                       "        p.pos2 += 5\n");
    CHECK(message.contains("vec2"));
    CHECK(message.contains("+="));
}

TEST_CASE("Semantic: compound-assignment diagnostic names target and source types", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() + vector_matrix_stdlib() +
                                       "rule Test:\n"
                                       "    each:\n"
                                       "        Position as p\n"
                                       "    on tick:\n"
                                       "        p.pos2 -= \"east\"\n");
    CHECK(message.contains("vec2"));
    CHECK(message.contains("-="));
    CHECK(message.contains("string"));
}

TEST_CASE("Semantic: pair-bound compound assignment still rejected as read-only", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(stdlib_events() + vector_matrix_stdlib() +
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
                           "    each:\n"
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
              .contains("event fields use bare `name: type` syntax"));
}

TEST_CASE("Semantic: exists requires entity_id argument", "[semantic][entity-id]") {
    CHECK(analyze_first_error(stdlib_events() + "rule Combat:\n"
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Parent:\n"
                                                     "    var parent: entity_id\n"
                                                     "trait Link:\n"
                                                     "    var target: entity_id\n"
                                                     "rule Parenting:\n"
                                                     "    each:\n"
                                                     "        Link as link\n"
                                                     "    on tick:\n"
                                                     "        add Parent to link.target:\n"
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "struct Hit:\n"
                                                     "    victim: entity_id\n"
                                                     "trait Source:\n"
                                                     "    var hits: list[Hit]\n"
                                                     "event Damage:\n"
                                                     "    amount: int\n"
                                                     "rule ApplyHits:\n"
                                                     "    each:\n"
                                                     "        Source\n"
                                                     "    on tick:\n"
                                                     "        for hit in hits:\n"
                                                     "            emit Damage to hit.victim:\n"
                                                     "                amount = 1\n"));

    CHECK(analyze_first_error(stdlib_events() + "trait Source:\n"
                                                "    var count: int\n"
                                                "rule BadLoop:\n"
                                                "    each:\n"
                                                "        Source\n"
                                                "    on tick:\n"
                                                "        for item in count:\n"
                                                "            let x = item\n") ==
          "foreach requires a `list[T]` iterable");

    CHECK(analyze_first_error(stdlib_events() + "trait Source:\n"
                                                "    var values: list[int]\n"
                                                "rule BadAssign:\n"
                                                "    each:\n"
                                                "        Source\n"
                                                "    on tick:\n"
                                                "        for value in values:\n"
                                                "            value = 2\n") ==
          "foreach loop variable 'value' is read-only");
}

static std::vector<std::string> analyze_messages(const std::string& source) {
    const std::string src = "module test\n" + stdlib_events() + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program);
    std::vector<std::string> messages;
    for (const auto& d : errors.diagnostics()) {
        messages.push_back(d.message);
    }
    return messages;
}

static bool has_message(const std::vector<std::string>& messages, const std::string& text) {
    return std::ranges::any_of(messages, [&](const std::string& m) { return m.contains(text); });
}

TEST_CASE("Semantic: let locals are immutable", "[semantic][locals]") {
    CHECK(has_message(analyze_messages("rule R:\n"
                                       "    on tick:\n"
                                       "        let speed = 5.0\n"
                                       "        speed = 6.0\n"),
                      "cannot reassign immutable binding 'speed'"));
    CHECK(has_message(analyze_messages("rule R:\n"
                                       "    on tick:\n"
                                       "        let total = 0\n"
                                       "        total += 1\n"),
                      "cannot reassign immutable binding 'total'"));
    CHECK(has_message(analyze_messages("rule R:\n"
                                       "    on tick:\n"
                                       "        let v = vec2(0.0, 0.0)\n"
                                       "        v.x = 1.0\n"),
                      "cannot reassign immutable binding 'v'"));
    CHECK(has_message(analyze_messages("func f() int:\n"
                                       "    let x = 1\n"
                                       "    x = 2\n"
                                       "    return x\n"),
                      "cannot reassign immutable binding 'x'"));
}

TEST_CASE("Semantic: trait write through a let entity binding is an entity_id access error", "[semantic][locals]") {
    const auto messages = analyze_messages("trait Health:\n"
                                           "    var hp: int = 10\n"
                                           "trait Player\n"
                                           "rule R:\n"
                                           "    on tick:\n"
                                           "        let e = query.first[Player]()\n"
                                           "        e.Health.hp = 3\n");
    CHECK(has_message(messages, "can't write trait 'Health' through entity_id 'e'; use 'set Health on e:'"));
    CHECK_FALSE(has_message(messages, "immutable binding"));
    CHECK(messages.size() == 1);
}

static const std::string& entity_id_traits() {
    static const std::string value =
        "trait Health:\n"
        "    var hp: int = 10\n"
        "trait Player\n"
        "trait Solid\n";
    return value;
}

static std::string entity_id_read_error(const std::string& trait, const std::string& object) {
    return "can't read trait '" + trait + "' through entity_id '" + object +
           "'; read another entity's traits in a pairs: rule";
}

static std::size_t count_message(const std::vector<std::string>& messages, const std::string& text) {
    return static_cast<std::size_t>(
        std::ranges::count_if(messages, [&](const std::string& m) { return m.contains(text); }));
}

TEST_CASE("Semantic: trait read through an entity_id local is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule R:\n"
                                           "    on tick:\n"
                                           "        let e = query.first[Player]()\n"
                                           "        let hp = e.Health.hp\n");
    CHECK(count_message(messages, entity_id_read_error("Health", "e")) == 1);
}

TEST_CASE("Semantic: trait read through a loop variable names a module-qualified trait",
          "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule R:\n"
                                           "    on tick:\n"
                                           "        for wall in query.all[Solid]():\n"
                                           "            let hp = wall.test.Health.hp\n");
    CHECK(count_message(messages, entity_id_read_error("test.Health", "wall")) == 1);
}

TEST_CASE("Semantic: read of a missing field through entity_id is still an entity_id access error",
          "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule R:\n"
                                           "    on tick:\n"
                                           "        let e = query.first[Player]()\n"
                                           "        let hp = e.Health.nonexistent\n");
    CHECK(has_message(messages, entity_id_read_error("Health", "e")));
}

TEST_CASE("Semantic: unknown trait read through entity_id names the first member", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule R:\n"
                                           "    on tick:\n"
                                           "        let e = query.first[Player]()\n"
                                           "        let x = e.Bogus.x\n");
    CHECK(has_message(messages, entity_id_read_error("Bogus", "e")));
}

TEST_CASE("Semantic: trait read through an event field is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "event Contact:\n"
                                           "    other: entity_id\n"
                                           "rule R:\n"
                                           "    on Contact as contact:\n"
                                           "        let hp = contact.other.Health.hp\n");
    CHECK(count_message(messages, entity_id_read_error("Health", "contact.other")) == 1);
}

TEST_CASE("Semantic: trait read through self is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule R:\n"
                                           "    on tick:\n"
                                           "        let hp = self.Health.hp\n");
    CHECK(count_message(messages, entity_id_read_error("Health", "self")) == 1);
}

TEST_CASE("Semantic: trait read through self in a where clause is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule R:\n"
                                           "    each:\n"
                                           "        Player\n"
                                           "    where:\n"
                                           "        self.Health.hp > 0\n"
                                           "    on tick:\n"
                                           "        let x = 1\n");
    CHECK(has_message(messages, entity_id_read_error("Health", "self")));
}

TEST_CASE("Semantic: trait read through a func parameter is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "func f(e: entity_id) int:\n"
                                           "    return e.Health.hp\n");
    CHECK(has_message(messages, entity_id_read_error("Health", "e")));
}

TEST_CASE("Semantic: compound trait write through a loop variable is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule R:\n"
                                           "    on tick:\n"
                                           "        for e in query.all[Health]():\n"
                                           "            e.Health.hp += 1\n");
    CHECK(has_message(messages, "can't write trait 'Health' through entity_id 'e'; use 'set Health on e:'"));
    CHECK_FALSE(has_message(messages, "is read-only"));
    CHECK(messages.size() == 1);
}

TEST_CASE("Semantic: write to an undeclared trait through entity_id is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule R:\n"
                                           "    on tick:\n"
                                           "        var e = query.first[Player]()\n"
                                           "        e.Bogus.x = 1\n");
    CHECK(has_message(messages, "can't write trait 'Bogus' through entity_id 'e'; use 'set Bogus on e:'"));
    CHECK(messages.size() == 1);
}

TEST_CASE("Semantic: trait write through an event field is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "event Contact:\n"
                                           "    other: entity_id\n"
                                           "rule R:\n"
                                           "    on Contact as contact:\n"
                                           "        contact.other.Health.hp = 1\n");
    CHECK(has_message(
        messages, "can't write trait 'Health' through entity_id 'contact.other'; use 'set Health on contact.other:'"));
}

TEST_CASE("Semantic: pair binding, filter alias and trait match access stay legal", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(entity_id_traits() +
                                           "rule P:\n"
                                           "    pairs:\n"
                                           "        body:\n"
                                           "            Health\n"
                                           "        other:\n"
                                           "            Health\n"
                                           "    where:\n"
                                           "        body[Health].hp > other[Health].hp\n"
                                           "    on tick:\n"
                                           "        let hp = body[Health].hp\n"
                                           "rule F:\n"
                                           "    each:\n"
                                           "        Health as h\n"
                                           "    on tick:\n"
                                           "        h.hp += 1\n"
                                           "rule M:\n"
                                           "    on tick:\n"
                                           "        for e in query.all[Solid]():\n"
                                           "            match e:\n"
                                           "                Health as h =>\n"
                                           "                    let hp = h.hp\n");
    for (const auto& m : messages) {
        INFO(m);
    }
    CHECK_FALSE(has_message(messages, "through entity_id"));
}

TEST_CASE("Semantic: var locals are reassignable", "[semantic][locals]") {
    CHECK(analyze_messages("rule R:\n"
                           "    on tick:\n"
                           "        var count = 0\n"
                           "        count = count + 1\n"
                           "        count += 1\n"
                           "        var v: vec2 = vec2(0.0, 0.0)\n"
                           "        v.x = 1.0\n")
              .empty());
    CHECK(analyze_messages("func f() int:\n"
                           "    var x = 1\n"
                           "    x = 2\n"
                           "    return x\n")
              .empty());
}

TEST_CASE("Semantic: local redeclaration in the same scope is rejected", "[semantic][locals]") {
    CHECK(has_message(analyze_messages("rule R:\n"
                                       "    on tick:\n"
                                       "        let speed = 5.0\n"
                                       "        let speed = 6.0\n"),
                      "redeclaration of local 'speed' in the same scope"));
    CHECK(has_message(analyze_messages("func f() int:\n"
                                       "    var x = 1\n"
                                       "    let x = 2\n"
                                       "    return x\n"),
                      "redeclaration of local 'x' in the same scope"));
    CHECK(analyze_messages("rule R:\n"
                           "    on tick:\n"
                           "        if true:\n"
                           "            let t = 1\n"
                           "        else:\n"
                           "            let t = 2\n")
              .empty());
}

TEST_CASE("Semantic: assignment does not declare a local", "[semantic][locals]") {
    CHECK(has_message(analyze_messages("rule R:\n"
                                       "    on tick:\n"
                                       "        score = 3\n"),
                      "assignment to undeclared local 'score'; declare it with `var`"));
    CHECK(has_message(analyze_messages("func f() int:\n"
                                       "    score = 3\n"
                                       "    return 0\n"),
                      "assignment to undeclared local 'score'; declare it with `var`"));
}

TEST_CASE("Semantic: typed local initializer must match its annotation", "[semantic][locals]") {
    CHECK(analyze_messages("rule R:\n"
                           "    on tick:\n"
                           "        var t: float = 1.0\n")
              .empty());
    CHECK(has_message(analyze_messages("rule R:\n"
                                       "    on tick:\n"
                                       "        var t: float = 1\n"),
                      "local 't' is declared as 'float' but initialized with 'int'"));
}

TEST_CASE("Semantic: range() intrinsic types the loop variable as int", "[semantic][foreach][range]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "rule CountUp:\n"
                                                     "    on tick:\n"
                                                     "        for k in range(0, 5):\n"
                                                     "            let doubled = k * 2\n"));

    CHECK_FALSE(analyze_has_errors(stdlib_events() + "rule CountDown:\n"
                                                     "    on tick:\n"
                                                     "        for k in range(5, 0, -1):\n"
                                                     "            let doubled = k * 2\n"));

    // Omitted step type-checks identically to an explicit step of 1.
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "rule DefaultStep:\n"
                                                     "    on tick:\n"
                                                     "        for k in range(0, 3):\n"
                                                     "            let doubled = k * 2\n"));
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "rule ExplicitStep:\n"
                                                     "    on tick:\n"
                                                     "        for k in range(0, 3, 1):\n"
                                                     "            let doubled = k * 2\n"));
}

TEST_CASE("Semantic: range() rejected outside a for-loop iterable position", "[semantic][foreach][range]") {
    CHECK(analyze_first_error(stdlib_events() + "rule BadRangeLet:\n"
                                                "    on tick:\n"
                                                "        let r = range(0, 10)\n") ==
          "`range()` is only valid as the iterable of a `for` statement");

    CHECK(analyze_first_error(stdlib_events() + "rule BadRangeArg:\n"
                                                "    on tick:\n"
                                                "        let v = vec2(range(0, 3), 0)\n") ==
          "`range()` is only valid as the iterable of a `for` statement");
}

TEST_CASE("Semantic: range() validates argument count and int-typed arguments", "[semantic][foreach][range]") {
    CHECK(analyze_first_error(stdlib_events() + "rule TooFewArgs:\n"
                                                "    on tick:\n"
                                                "        for k in range(0):\n"
                                                "            let x = k\n")
              .contains("'range'"));

    CHECK(analyze_first_error(stdlib_events() + "rule TooManyArgs:\n"
                                                "    on tick:\n"
                                                "        for k in range(0, 1, 2, 3):\n"
                                                "            let x = k\n")
              .contains("'range'"));

    CHECK(analyze_first_error(stdlib_events() + "rule NonIntArg:\n"
                                                "    on tick:\n"
                                                "        for k in range(0, 3.0):\n"
                                                "            let x = k\n")
              .contains("'range'"));
}

TEST_CASE("Semantic: C-style numeric for, while, and break/continue remain unsupported", "[semantic][foreach]") {
    CHECK(analyze_has_errors(stdlib_events() + "rule NumericFor:\n"
                                               "    on tick:\n"
                                               "        for i = 0; i < 10; i += 1:\n"
                                               "            let x = i\n"));

    CHECK(analyze_has_errors(stdlib_events() + "rule WhileLoop:\n"
                                               "    on tick:\n"
                                               "        while true:\n"
                                               "            let x = 1\n"));

    CHECK(analyze_has_errors(stdlib_events() + "rule BreakStmt:\n"
                                               "    on tick:\n"
                                               "        for k in range(0, 3):\n"
                                               "            break\n"));

    CHECK(analyze_has_errors(stdlib_events() + "rule ContinueStmt:\n"
                                               "    on tick:\n"
                                               "        for k in range(0, 3):\n"
                                               "            continue\n"));
}

TEST_CASE("Semantic: project validates trait fields target and transient restrictions", "[semantic][project]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait DamageFlash:\n"
                                                     "    var color: color = #FFFFFF\n"
                                                     "    var intensity: float\n"
                                                     "trait Target:\n"
                                                     "    var victim: entity_id\n"
                                                     "rule Flash:\n"
                                                     "    each:\n"
                                                     "        Target\n"
                                                     "    on tick:\n"
                                                     "        project DamageFlash to victim:\n"
                                                     "            intensity = 1.0\n"));

    CHECK(analyze_first_error(stdlib_events() + "trait DamageFlash:\n"
                                                "    var intensity: float\n"
                                                "rule BadTarget:\n"
                                                "    on tick:\n"
                                                "        project DamageFlash to 123:\n"
                                                "            intensity = 1.0\n") ==
          "`project ... to` target must be of type `entity_id`");

    CHECK(analyze_first_error(stdlib_events() + "trait DamageFlash:\n"
                                                "    var intensity: float\n"
                                                "rule BadField:\n"
                                                "    on tick:\n"
                                                "        project DamageFlash:\n"
                                                "            missing = 1.0\n") ==
          "unknown field 'missing' in `project DamageFlash`");

    CHECK(analyze_first_error(stdlib_events() + "trait SavedFact:\n"
                                                "    persist var value: int\n"
                                                "rule BadPersist:\n"
                                                "    on tick:\n"
                                                "        project SavedFact:\n"
                                                "            value = 1\n") ==
          "trait 'SavedFact' has persistent fields and cannot be projected");
}

TEST_CASE("Semantic: project participates in dependency writes", "[semantic][project]") {
    auto result = analyze(stdlib_events() +
                          "trait DamageFlash\n"
                          "trait Health:\n"
                          "    var hp: int\n"
                          "rule Producer:\n"
                          "    each:\n"
                          "        Health\n"
                          "    on tick:\n"
                          "        let x = hp\n"
                          "        project DamageFlash\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    CHECK(contract.reads.contains(make_symbol_id(SymbolKind::Trait, "test", "Health")));
    CHECK(contract.writes.contains(make_symbol_id(SymbolKind::Trait, "test", "DamageFlash")));
    CHECK(contract.projects.contains(make_symbol_id(SymbolKind::Trait, "test", "DamageFlash")));
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
    CHECK(err.contains("must be a string literal"));
}

TEST_CASE("Semantic: std.text.format — too few args for auto placeholders", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"hello {} {}\")\n");
    CHECK(err.contains("placeholder"));
    CHECK(err.contains("argument"));
}

TEST_CASE("Semantic: std.text.format — manual index out of range", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"{1}\", 42)\n");
    CHECK(err.contains("out of range"));
}

TEST_CASE("Semantic: std.text.format — mixed automatic and manual placeholders rejected",
          "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"{} {0}\", 42, 43)\n");
    CHECK(err.contains("mixes"));
}

TEST_CASE("Semantic: std.text.format — malformed unclosed brace rejected", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"{\", 42)\n");
    CHECK(err.contains("malformed"));
}

TEST_CASE("Semantic: std.text.format — unsupported vec2 arg type rejected", "[semantic][std-text-format]") {
    auto err = analyze_first_error(stdlib_events() +
                                   "use std.text as text\n"
                                   "trait Pos:\n"
                                   "    var pos: vec2\n"
                                   "rule S:\n"
                                   "    each:\n"
                                   "        Pos\n"
                                   "    on tick:\n"
                                   "        let s = text.format(\"pos={}\", pos)\n");
    CHECK(err.contains("not supported"));
}

TEST_CASE("Semantic: std.text.format — extra args with no placeholders rejected", "[semantic][std-text-format]") {
    auto err = analyze_first_error(
        "use std.text as text\n"
        "func render():\n"
        "    let s = text.format(\"Ready\", 42)\n");
    CHECK(err.contains("no placeholders"));
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
    CHECK(err.contains("undefined template"));
    CHECK(err.contains("MissingTemplate"));
}

TEST_CASE("Semantic: template-backed entity from non-template rejected", "[semantic][entity]") {
    auto err = analyze_first_error(
        "trait Collectible:\n"
        "    var point_value: int = 10\n"
        "entity Gem1 from Collectible:\n"
        "    Collectible:\n"
        "        point_value = 5\n");
    CHECK(err.contains("not a template"));
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
    CHECK(err.contains("UnrelatedTrait"));
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
    CHECK(err.contains("notafield"));
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
    CHECK(err.contains("required field"));
    CHECK(err.contains('x'));
}

TEST_CASE("Semantic: entity name is an entity_id expression", "[semantic][entity]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + "trait Shape:\n"
                                                     "    var size: float = 16.0\n"
                                                     "template BlueGem:\n"
                                                     "    Shape\n"
                                                     "entity Gem1 from BlueGem:\n"
                                                     "    Shape\n"
                                                     "rule S:\n"
                                                     "    on tick:\n"
                                                     "        let x: entity_id = Gem1\n"));
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
    auto err = analyze_first_error(stdlib_events() +
                                   "trait Tag\n"
                                   "entity Player:\n"
                                   "    Tag\n"
                                   "rule S:\n"
                                   "    on tick:\n"
                                   "        spawn Player:\n"
                                   "            Tag\n");
    CHECK(err.contains("Player"));
    CHECK(err.contains("entity"));
}
// ── Query expression semantic tests ────────────────────────────────────────

TEST_CASE("Semantic: query exists in rule handler returns bool — no errors", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Boss:\n"
                           "    var hp: int\n" +
                           stdlib_events() +
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
                           stdlib_events() +
                           "rule S:\n"
                           "    on tick:\n"
                           "        let n = query.count[Enemy, not Dead]()\n"));
}

TEST_CASE("Semantic: query first in rule handler returns entity_id — no errors", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Boss:\n"
                           "    var hp: int\n" +
                           stdlib_events() +
                           "rule S:\n"
                           "    on tick:\n"
                           "        let t = query.first[Boss]()\n"));
}

TEST_CASE("Semantic: query all in rule handler returns list of entity_id — no errors", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Enemy:\n"
                           "    var hp: int\n" +
                           stdlib_events() +
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
              .contains("world access"));
}

TEST_CASE("Semantic: undeclared trait in query filter is rejected", "[semantic][query]") {
    CHECK(analyze_first_error("use std.query as query\n" + stdlib_events() +
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
                           stdlib_events() +
                           "rule S:\n"
                           "    on tick:\n"
                           "        let n = query.count[EnemyAI, not Dead]()\n"));
}

TEST_CASE("Semantic: query parent with entity_id of argument accepted", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query as query\n"
                           "trait Child:\n"
                           "    var child_id: entity_id\n" +
                           stdlib_events() +
                           "rule S:\n"
                           "    each:\n"
                           "        Child\n"
                           "    on tick:\n"
                           "        let p = query.parent(of = child_id)\n"));
}

TEST_CASE("Semantic: query parent of argument must be entity_id", "[semantic][query]") {
    CHECK(analyze_first_error("use std.query as query\n" + stdlib_events() +
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
                           stdlib_events() +
                           "rule S:\n"
                           "    each:\n"
                           "        Node\n"
                           "    on tick:\n"
                           "        let direct = query.children[Node](of = id)\n"
                           "        let pre = query.hierarchy_preorder[Node]()\n"
                           "        let post = query.hierarchy_postorder[Node]()\n"));
}

TEST_CASE("Semantic: query children of argument must be entity_id", "[semantic][query][hierarchy]") {
    CHECK(analyze_first_error("use std.query as query\n" + stdlib_events() +
                              "rule S:\n"
                              "    on tick:\n"
                              "        let children = query.children(of = 42)\n") ==
          "`children` `of` argument must be of type `entity_id`");
}

TEST_CASE("Semantic: query children requires of argument", "[semantic][query][hierarchy]") {
    CHECK(analyze_first_error("use std.query as query\n"
                              "trait Node\n" +
                              stdlib_events() +
                              "rule S:\n"
                              "    on tick:\n"
                              "        let children = query.children[Node]()\n") ==
          "`children` requires an `of` named argument");
}

TEST_CASE("Semantic: query nearest requires from argument", "[semantic][query]") {
    CHECK(analyze_first_error("use std.physics.flat.query as query\n" + stdlib_events() +
                              "trait Transform:\n"
                              "    var pos: vec2\n"
                              "rule S:\n"
                              "    on tick:\n"
                              "        let t = query.nearest[Transform]()\n")
              .contains("`nearest` requires"));
}

TEST_CASE("Semantic: module-qualified query path accepted", "[semantic][query]") {
    CHECK_FALSE(
        analyze_has_errors("use std.query\n"
                           "trait Boss:\n"
                           "    var hp: int\n" +
                           stdlib_events() +
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
                           stdlib_events() +
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
                           stdlib_events() +
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
                           stdlib_events() +
                           "rule S:\n"
                           "    on tick:\n"
                           "        add ModelRenderer:\n"
                           "            model = Robot\n"));
}

// ── Pair relations (dsl-pair-relations) ─────────────────────────────────────

static const std::string& pair_traits() {
    static const std::string value =
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
    return value;
}

TEST_CASE("Semantic: basic pair rule compiles with a tuple-rejecting condition", "[semantic][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "            Transform\n"
                                   "        wall:\n"
                                   "            Solid\n"
                                   "            Collider\n"
                                   "    on fixed_tick:\n"
                                   "        if body[Transform].x > 0.0 and wall[Collider].mask > 0:\n"
                                   "            emit Contact:\n"
                                   "                other = wall\n"));
}

TEST_CASE("Semantic: pair binding color channel read is accepted and typed",
          "[semantic][pair-relations][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
                                   "trait Tint:\n"
                                   "    var tint: color\n"
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "            Tint\n"
                                   "        wall:\n"
                                   "            Solid\n"
                                   "    on fixed_tick:\n"
                                   "        if body[Tint].tint.a > 0.5:\n"
                                   "            emit Contact:\n"
                                   "                other = wall\n"));
}

TEST_CASE("Semantic: cross-binding trait access is rejected", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "        wall:\n"
                                   "            Collider as c\n"
                                   "    on fixed_tick:\n"
                                   "        if c.mask > 0:\n"
                                   "            emit Contact:\n"
                                   "                other = wall\n"));
}

TEST_CASE("Semantic: self-qualified pair trait resolves via longest prefix", "[semantic][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
                                   "rule DetectContacts:\n"
                                   "    pairs:\n"
                                   "        body:\n"
                                   "            DynamicBody\n"
                                   "        wall:\n"
                                   "            test.Collider\n"
                                   "    on fixed_tick:\n"
                                   "        if wall[test.Collider].mask > 0:\n"
                                   "            emit Contact:\n"
                                   "                other = wall\n"));
}

TEST_CASE("Semantic: duplicate pair binding name is rejected", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
                             "rule DetectContacts:\n"
                             "    pairs:\n"
                             "        body:\n"
                             "            DynamicBody\n"
                             "            Transform\n"
                             "        wall:\n"
                             "            Solid\n"
                             "    on fixed_tick:\n"
                             "        body[Transform].x += 1.0\n"));
}

TEST_CASE("Semantic: compound assignment through a pair trait path is rejected as read-only",
          "[semantic][pair-relations]") {
    auto message = analyze_first_error(stdlib_events() + pair_traits() +
                                       "rule DetectContacts:\n"
                                       "    pairs:\n"
                                       "        body:\n"
                                       "            DynamicBody\n"
                                       "        wall:\n"
                                       "            Solid\n"
                                       "    on fixed_tick:\n"
                                       "        body[DynamicBody].vx = 1.0\n");
    CHECK(message.contains("read-only"));
}

TEST_CASE("Semantic: bare assignment has no implicit entity in a pair handler", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
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
    auto message = analyze_first_error(stdlib_events() + pair_traits() +
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
    CHECK(message.contains("trait-match directly on binding"));
}

TEST_CASE("Semantic: bare destroy has no implicit target in a pair handler", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
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
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
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
    auto result = analyze(stdlib_events() + pair_traits() +
                          "rule DetectContacts:\n"
                          "    pairs:\n"
                          "        body:\n"
                          "            DynamicBody\n"
                          "            Transform\n"
                          "        wall:\n"
                          "            Solid\n"
                          "            Collider\n"
                          "    on fixed_tick:\n"
                          "        if body != wall and body[Transform].x > 0.0 and wall[Collider].mask > 0:\n"
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

// handler-contracts: an order by: key's reads must reach the contract with the
// same precision as a body read, so scheduling sees identical dependency data
// whether a trait is touched in order by:, where:, or the handler body. The
// computed key here reads Collider through a nested call the body never
// mentions, so a flat root-alias lookup would miss it.
TEST_CASE("Semantic: unary handler contract folds reads from a computed order by key",
          "[semantic][rule-order-by][handler-contracts]") {
    auto result = analyze(stdlib_events() + pair_traits() +
                          "func doubled(v: int) int:\n"
                          "    return v * 2\n"
                          "rule Ranked:\n"
                          "    each:\n"
                          "        DynamicBody as body\n"
                          "        Collider as col\n"
                          "    order by:\n"
                          "        doubled(col.mask) desc\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    CHECK(contract.reads.contains(make_symbol_id(SymbolKind::Trait, "test", "Collider")));
}

TEST_CASE("Semantic: pair handler contract folds binding-qualified reads from an order by key",
          "[semantic][rule-order-by][pair-relations][handler-contracts]") {
    auto result = analyze(stdlib_events() + pair_traits() +
                          "rule Ranked:\n"
                          "    pairs:\n"
                          "        actor:\n"
                          "            Transform\n"
                          "        victim:\n"
                          "            Transform\n"
                          "    order by:\n"
                          "        actor[Transform].x - victim[Transform].x desc\n"
                          "    on fixed_tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract    = result.handler_contracts[0];
    const auto transform_id = make_symbol_id(SymbolKind::Trait, "test", "Transform");

    // Conservative `reads` is a set, so the same trait read through both
    // bindings collapses to one entry; `bound_reads` keeps the two roles apart.
    CHECK(contract.reads.contains(transform_id));
    const BoundTraitAccess actor_read{.binding_index = 0, .trait = transform_id};
    const BoundTraitAccess victim_read{.binding_index = 1, .trait = transform_id};
    CHECK(std::ranges::find(contract.bound_reads, actor_read) != contract.bound_reads.end());
    CHECK(std::ranges::find(contract.bound_reads, victim_read) != contract.bound_reads.end());
    CHECK(contract.bound_reads.size() == 2);
}

TEST_CASE("Semantic: unary rules keep their existing domain and are unaffected by pair support",
          "[semantic][pair-relations][handler-contracts]") {
    auto result = analyze(stdlib_events() +
                          "trait Pos:\n"
                          "    var x: float\n"
                          "rule Move:\n"
                          "    each:\n"
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

// ── Where clause (dsl-where-clause) ─────────────────────────────────────────

TEST_CASE("Semantic: bool-typed where predicate is accepted", "[semantic][where-clause]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
                                   "rule Moving:\n"
                                   "    each:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        body.vx > 0.0\n"
                                   "    on tick:\n"
                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: non-bool where predicate is rejected with a type error", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
                             "rule Moving:\n"
                             "    each:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        body.vx\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: pure user func call in where is accepted", "[semantic][where-clause]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + pair_traits() +
                                   "func is_positive(v: float) bool:\n"
                                   "    return v > 0.0\n"
                                   "rule Moving:\n"
                                   "    each:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        is_positive(body.vx)\n"
                                   "    on tick:\n"
                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: emit in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
                             "rule Moving:\n"
                             "    each:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        emit Contact\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: world query in where is rejected", "[semantic][where-clause]") {
    auto err = analyze_first_error("use std.query as query\n" + stdlib_events() + pair_traits() +
                                   "rule Moving:\n"
                                   "    each:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        query.exists[Solid]()\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("must be pure"));
}

TEST_CASE("Semantic: spawn in where is rejected", "[semantic][where-clause]") {
    auto err = analyze_first_error(stdlib_events() + pair_traits() +
                                   "template Marker:\n"
                                   "    Solid:\n"
                                   "        active = true\n"
                                   "rule Moving:\n"
                                   "    each:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        spawn Marker:\n"
                                   "            Solid:\n"
                                   "                active = true\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("must be pure"));
}

TEST_CASE("Semantic: destroy in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
                             "rule Moving:\n"
                             "    each:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        destroy\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: add in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
                             "rule Moving:\n"
                             "    each:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        add Solid\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: remove in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
                             "rule Moving:\n"
                             "    each:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        remove Solid\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: project in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(stdlib_events() + pair_traits() +
                             "rule Moving:\n"
                             "    each:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        project Solid\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: call to extern func without a proven-pure effect summary is rejected in where",
          "[semantic][where-clause]") {
    auto err = analyze_first_error(stdlib_events() + pair_traits() +
                                   "pub extern func mystery(v: float) float\n"
                                   "rule Moving:\n"
                                   "    each:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        mystery(body.vx) > 0.0\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("must be pure"));
}

TEST_CASE("Semantic: where on rule with neither each nor pairs is rejected", "[semantic][where-clause]") {
    auto err = analyze_first_error(stdlib_events() +
                                   "rule NoDomain:\n"
                                   "    where:\n"
                                   "        true\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("where:"));
    CHECK(err.contains("each:"));
}

TEST_CASE("Semantic: unary handler contract from where matches equivalent leading if guard",
          "[semantic][where-clause][handler-contracts]") {
    auto where_result = analyze(stdlib_events() + pair_traits() +
                                "rule MovingWhere:\n"
                                "    each:\n"
                                "        DynamicBody as body\n"
                                "    where:\n"
                                "        body.vx > 0.0\n"
                                "    on tick:\n"
                                "        let x = 1\n");
    auto if_result    = analyze(stdlib_events() + pair_traits() +
                                "rule MovingIf:\n"
                                "    each:\n"
                                "        DynamicBody as body\n"
                                "    on tick:\n"
                                "        if body.vx <= 0.0:\n"
                                "            return\n"
                                "        let x = 1\n");

    REQUIRE(where_result.handler_contracts.size() == 1);
    REQUIRE(if_result.handler_contracts.size() == 1);
    CHECK(where_result.handler_contracts[0].reads == if_result.handler_contracts[0].reads);
    CHECK(where_result.handler_contracts[0].bound_reads == if_result.handler_contracts[0].bound_reads);
}

TEST_CASE("Semantic: pair handler contract from where matches equivalent leading if guard",
          "[semantic][where-clause][handler-contracts]") {
    auto where_result = analyze(stdlib_events() + pair_traits() +
                                "rule ContactWhere:\n"
                                "    pairs:\n"
                                "        a:\n"
                                "            Transform\n"
                                "        b:\n"
                                "            Transform\n"
                                "    where:\n"
                                "        a[Transform].x > 0.0\n"
                                "    on tick:\n"
                                "        let x = 1\n");
    auto if_result    = analyze(stdlib_events() + pair_traits() +
                                "rule ContactIf:\n"
                                "    pairs:\n"
                                "        a:\n"
                                "            Transform\n"
                                "        b:\n"
                                "            Transform\n"
                                "    on tick:\n"
                                "        if a[Transform].x <= 0.0:\n"
                                "            return\n"
                                "        let x = 1\n");

    REQUIRE(where_result.handler_contracts.size() == 1);
    REQUIRE(if_result.handler_contracts.size() == 1);
    const auto& where_contract = where_result.handler_contracts[0];
    const auto& if_contract    = if_result.handler_contracts[0];
    CHECK(where_contract.reads == if_contract.reads);
    CHECK(std::ranges::is_permutation(where_contract.bound_reads, if_contract.bound_reads));
}

// ── Spatial join recognition (spatial-broadphase-runtime, dsl-where-clause) ──
//
// circles_overlap/spheres_overlap are declared locally within a module
// literally named "std.collision.flat"/"std.collision.volume" so their
// canonical id matches the recognized target exactly, without needing a real
// cross-module compile — recognition only ever inspects resolved_callee_id's
// canonical identity, never the declaring module's actual provenance.

static ImportedSymbols make_spheres_overlap_import() {
    ImportedSymbols syms;
    syms.module_name = "std.collision.volume";

    TypeInfo vec3_type;
    vec3_type.kind = TypeKind::Vec3;
    TypeInfo float_type;
    float_type.kind = TypeKind::Float;
    TypeInfo bool_type;
    bool_type.kind = TypeKind::Bool;

    ResolvedFunc func;
    func.name           = "spheres_overlap";
    func.module_name    = "std.collision.volume";
    func.is_pub         = true;
    func.effect_summary = std::unordered_set<std::string>{};  // proven pure: allowed in where:
    func.params         = {
        ResolvedParam{.name = "a_position", .type = vec3_type},
        ResolvedParam{.name = "a_radius", .type = float_type},
        ResolvedParam{.name = "b_position", .type = vec3_type},
        ResolvedParam{.name = "b_radius", .type = float_type},
    };
    func.return_type  = bool_type;
    const auto symbol = make_symbol_id(SymbolKind::Func, "std.collision.volume", "spheres_overlap");
    func.symbol_id    = symbol;
    func.canonical_id = make_canonical_id(symbol);

    syms.funcs["spheres_overlap"] = std::move(func);
    return syms;
}

TEST_CASE("Semantic: direct spatial call with same-domain bindings is recognized and eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.collision.flat\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, "
                          "b_radius: float) bool:\n"
                          "    return a_radius + b_radius >= 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        circles_overlap(a[Transform].position, a[Collider].radius, b[Transform].position, "
                          "b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    REQUIRE(contract.spatial_join.has_value());
    const auto& plan = *contract.spatial_join;
    CHECK(plan.dimension == SpatialJoinDimension::Flat2D);

    CHECK(plan.left.binding_index == 0);
    CHECK(plan.left.kind == SpatialShapeKind::Sphere);
    CHECK(plan.left.slots == std::vector<ExprPath>{{0}, {1}});
    CHECK(plan.right.binding_index == 1);
    CHECK(plan.right.kind == SpatialShapeKind::Sphere);
    CHECK(plan.right.slots == std::vector<ExprPath>{{2}, {3}});
    CHECK(plan.matched_predicate_index == 0);
}

TEST_CASE("Semantic: matched predicate index reflects its position among multiple where: predicates",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.collision.flat\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, "
                          "b_radius: float) bool:\n"
                          "    return a_radius + b_radius >= 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        a != b\n"
                          "        circles_overlap(a[Transform].position, a[Collider].radius, b[Transform].position, "
                          "b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    REQUIRE(contract.spatial_join.has_value());
    CHECK(contract.spatial_join->matched_predicate_index == 1);
}

TEST_CASE("Semantic: spatial call recognition succeeds through a renamed import alias",
          "[semantic][where-clause][spatial-join]") {
    ModuleImports imports;
    imports.modules["foo"] = make_spheres_overlap_import();

    auto result = analyze_with_imports(stdlib_events() +
                                           "trait Transform:\n"
                                           "    var position: vec3\n"
                                           "trait Collider:\n"
                                           "    var radius: float\n"
                                           "rule DetectContact:\n"
                                           "    pairs:\n"
                                           "        a:\n"
                                           "            Transform\n"
                                           "            Collider\n"
                                           "        b:\n"
                                           "            Transform\n"
                                           "            Collider\n"
                                           "    where:\n"
                                           "        foo.spheres_overlap(a[Transform].position, a[Collider].radius, "
                                           "b[Transform].position, b[Collider].radius)\n"
                                           "    on tick:\n"
                                           "        let x = 1\n",
                                       imports);

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    REQUIRE(contract.spatial_join.has_value());
    CHECK(contract.spatial_join->dimension == SpatialJoinDimension::Volume3D);
}

TEST_CASE("Semantic: negated spatial call remains an ordinary residual predicate",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.collision.flat\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, "
                          "b_radius: float) bool:\n"
                          "    return a_radius + b_radius >= 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        not circles_overlap(a[Transform].position, a[Collider].radius, "
                          "b[Transform].position, b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: computed single-binding radius argument is eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.collision.flat\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, "
                          "b_radius: float) bool:\n"
                          "    return a_radius + b_radius >= 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        circles_overlap(a[Transform].position, a[Collider].radius * 2.0, "
                          "b[Transform].position, b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: cross-domain pair rule with a recognized call is eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.collision.flat\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "trait Wall:\n"
                          "    var active: bool = true\n"
                          "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, "
                          "b_radius: float) bool:\n"
                          "    return a_radius + b_radius >= 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "            Wall\n"
                          "    where:\n"
                          "        circles_overlap(a[Transform].position, a[Collider].radius, b[Transform].position, "
                          "b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(result.handler_contracts[0].spatial_join.has_value());
}

// ── Shape-generic recognition (accelerate-cross-domain-pair-joins) ─────────

static const std::string& volume_overlap_header() {
    static const std::string value =
        "module std.collision.volume\n" + stdlib_events() +
        "const:\n"
        "    PROBE = 0.5\n"
        "trait Body:\n"
        "    var position: vec3\n"
        "    var radius: float\n"
        "trait Solid:\n"
        "    var position: vec3\n"
        "    var size: vec3\n"
        "    var rotation: quat\n"
        "trait Tuning:\n"
        "    var margin: float = 0.1\n"
        "entity Game:\n"
        "    Tuning\n"
        "func inflate(r: float) float:\n"
        "    return r * 2.0\n"
        "pub func spheres_overlap(a_position: vec3, a_radius: float, b_position: vec3, b_radius: float) bool:\n"
        "    return a_radius + b_radius >= 0.0\n"
        "pub func sphere_box_overlap(sphere_position: vec3, sphere_radius: float, box_position: vec3, box_size: vec3, "
        "box_rotation: quat) bool:\n"
        "    return sphere_radius >= 0.0\n"
        "pub func ray_hits_box(origin: vec3, direction: vec3, max_distance: float, box_center: vec3, box_size: vec3) "
        "bool:\n"
        "    return max_distance > 0.0\n"
        "pub func boxes_overlap(a_center: vec3, a_size: vec3, b_center: vec3, b_size: vec3) bool:\n"
        "    return a_size.x > 0.0\n";
    return value;
}

static std::optional<SpatialJoinPlan> volume_overlap_plan(const std::string& where_predicate) {
    auto result = analyze(volume_overlap_header() +
                          "rule Detect:\n"
                          "    pairs:\n"
                          "        actor:\n"
                          "            Body\n"
                          "        wall:\n"
                          "            Solid\n"
                          "    where:\n"
                          "        " +
                          where_predicate +
                          "\n"
                          "    on tick:\n"
                          "        let x = 1\n");
    REQUIRE(result.handler_contracts.size() == 1);
    return result.handler_contracts[0].spatial_join;
}

TEST_CASE("Semantic: sphere_box_overlap with the sphere on the left binding is eligible",
          "[semantic][where-clause][spatial-join]") {
    const auto plan = volume_overlap_plan(
        "sphere_box_overlap(actor[Body].position, actor[Body].radius, wall[Solid].position, wall[Solid].size, "
        "wall[Solid].rotation)");
    REQUIRE(plan.has_value());
    CHECK(plan->dimension == SpatialJoinDimension::Volume3D);
    CHECK(plan->left.binding_index == 0);
    CHECK(plan->left.kind == SpatialShapeKind::Sphere);
    CHECK(plan->left.slots == std::vector<ExprPath>{{0}, {1}});
    CHECK(plan->right.binding_index == 1);
    CHECK(plan->right.kind == SpatialShapeKind::Box);
    CHECK(plan->right.slots == std::vector<ExprPath>{{2}, {3}, {4}});
}

TEST_CASE("Semantic: ray_hits_box bounds a ray segment against an unrotated box",
          "[semantic][where-clause][spatial-join]") {
    const auto plan = volume_overlap_plan(
        "ray_hits_box(actor[Body].position, actor[Body].position * 2.0, actor[Body].radius, wall[Solid].position, "
        "wall[Solid].size)");
    REQUIRE(plan.has_value());
    CHECK(plan->dimension == SpatialJoinDimension::Volume3D);
    CHECK(plan->left.binding_index == 0);
    CHECK(plan->left.kind == SpatialShapeKind::RaySegment);
    CHECK(plan->left.slots == std::vector<ExprPath>{{0}, {1}, {2}});
    CHECK(plan->right.binding_index == 1);
    CHECK(plan->right.kind == SpatialShapeKind::AlignedBox);
    CHECK(plan->right.slots == std::vector<ExprPath>{{3}, {4}});
}

TEST_CASE("Semantic: boxes_overlap bounds two unrotated boxes", "[semantic][where-clause][spatial-join]") {
    const auto plan = volume_overlap_plan("boxes_overlap(wall[Solid].position, wall[Solid].size, actor[Body].position, "
                                          "vec3(actor[Body].radius, 1.0, 1.0))");
    REQUIRE(plan.has_value());
    CHECK(plan->left.binding_index == 1);
    CHECK(plan->left.kind == SpatialShapeKind::AlignedBox);
    CHECK(plan->right.binding_index == 0);
    CHECK(plan->right.kind == SpatialShapeKind::AlignedBox);
    CHECK(plan->right.slots == std::vector<ExprPath>{{2}, {3}});
}

TEST_CASE("Semantic: ray_hits_box mixing both bindings in one shape is not eligible",
          "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("ray_hits_box(actor[Body].position, wall[Solid].position, actor[Body].radius, "
                                    "wall[Solid].position, wall[Solid].size)")
                    .has_value());
}

TEST_CASE("Semantic: sphere_box_overlap with the box on the left binding is eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze(volume_overlap_header() +
                          "rule Detect:\n"
                          "    pairs:\n"
                          "        wall:\n"
                          "            Solid\n"
                          "        actor:\n"
                          "            Body\n"
                          "    where:\n"
                          "        sphere_box_overlap(actor[Body].position, actor[Body].radius, "
                          "wall[Solid].position, wall[Solid].size, wall[Solid].rotation)\n"
                          "    on tick:\n"
                          "        let x = 1\n");
    REQUIRE(result.handler_contracts.size() == 1);
    const auto& plan = result.handler_contracts[0].spatial_join;
    REQUIRE(plan.has_value());
    CHECK(plan->left.binding_index == 1);
    CHECK(plan->left.kind == SpatialShapeKind::Sphere);
    CHECK(plan->right.binding_index == 0);
    CHECK(plan->right.kind == SpatialShapeKind::Box);
}

TEST_CASE("Semantic: spheres_overlap across different trait sets is eligible", "[semantic][where-clause][spatial-join]") {
    const auto plan =
        volume_overlap_plan("spheres_overlap(actor[Body].position, actor[Body].radius, wall[Solid].position, 1.0 + "
                            "wall[Solid].size.x)");
    REQUIRE(plan.has_value());
    CHECK(plan->left.kind == SpatialShapeKind::Sphere);
    CHECK(plan->right.kind == SpatialShapeKind::Sphere);
}

TEST_CASE("Semantic: single-binding expression slots are eligible", "[semantic][where-clause][spatial-join]") {
    CHECK(volume_overlap_plan("sphere_box_overlap(actor[Body].position, actor[Body].radius + PROBE, wall[Solid].position, "
                              "wall[Solid].size, wall[Solid].rotation)")
              .has_value());
    CHECK(volume_overlap_plan("sphere_box_overlap(actor[Body].position, actor[Body].radius + Game[Tuning].margin, "
                              "wall[Solid].position, wall[Solid].size, wall[Solid].rotation)")
              .has_value());
    CHECK(volume_overlap_plan("sphere_box_overlap(actor[Body].position, inflate(actor[Body].radius), "
                              "wall[Solid].position, wall[Solid].size * 1.5, wall[Solid].rotation)")
              .has_value());
}

TEST_CASE("Semantic: an argument mixing both bindings is not eligible", "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("spheres_overlap(actor[Body].position, actor[Body].radius + wall[Solid].size.x, "
                                    "wall[Solid].position, wall[Solid].size.x)")
                    .has_value());
}

TEST_CASE("Semantic: a binding-free argument joins its shape's binding", "[semantic][where-clause][spatial-join]") {
    const auto plan = volume_overlap_plan(
        "sphere_box_overlap(actor[Body].position, PROBE, wall[Solid].position, wall[Solid].size, wall[Solid].rotation)");
    REQUIRE(plan.has_value());
    CHECK(plan->left.binding_index == 0);
    CHECK(plan->left.slots == std::vector<ExprPath>{{0}, {1}});
}

TEST_CASE("Semantic: a shape that reads no pair binding is not eligible", "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("sphere_box_overlap(vec3(0.0, 0.0, 0.0), PROBE, wall[Solid].position, "
                                    "wall[Solid].size, wall[Solid].rotation)")
                    .has_value());
}

TEST_CASE("Semantic: both shapes from one binding are not eligible", "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("spheres_overlap(actor[Body].position, actor[Body].radius, actor[Body].position, "
                                    "actor[Body].radius)")
                    .has_value());
}

TEST_CASE("Semantic: or-combined overlap predicate is not eligible", "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("spheres_overlap(actor[Body].position, actor[Body].radius, wall[Solid].position, "
                                    "wall[Solid].size.x) or actor[Body].radius > 1.0")
                    .has_value());
}

TEST_CASE("Semantic: resolve_expr_path walks call arguments and binary operands", "[semantic][spatial-join]") {
    auto leaf = [](std::string name) {
        return std::make_unique<ExprNode>(ExprNode::Variant{IdentExpr{.name = std::move(name)}}, SourceLocation{});
    };
    std::vector<std::unique_ptr<ExprNode>> args;
    args.push_back(leaf("x"));
    args.push_back(std::make_unique<ExprNode>(
        ExprNode::Variant{BinaryExpr{.op = "+", .left = leaf("y"), .right = leaf("z")}}, SourceLocation{}));
    const ExprNode call(ExprNode::Variant{CallExpr{.callee = leaf("f"), .args = std::move(args)}}, SourceLocation{});

    const auto name_at = [&call](const ExprPath& path) {
        const auto* node = resolve_expr_path(call, path);
        const auto* ident = node == nullptr ? nullptr : std::get_if<IdentExpr>(&node->expr);
        return ident == nullptr ? std::string("<none>") : ident->name;
    };
    CHECK(name_at({0}) == "x");
    CHECK(name_at({1, 0}) == "y");
    CHECK(name_at({1, 1}) == "z");
    CHECK(name_at({2}) == "<none>");
    CHECK(name_at({0, 0}) == "<none>");
    CHECK(resolve_expr_path(call, {}) == &call);
}

// ── Manual squared-distance expression recognition (add-sap-broadphase) ─────
//
// `dot` is declared locally within a module literally named "std.math.vec2"
// (or "std.math.vec3") so its canonical id matches the recognized target
// exactly, mirroring the circles_overlap/spheres_overlap tests above —
// recognition only ever inspects resolved_callee_id's canonical identity,
// never the declaring module's actual provenance.

static ImportedSymbols make_dot_import() {
    ImportedSymbols syms;
    syms.module_name = "std.math.vec2";

    TypeInfo vec2_type;
    vec2_type.kind = TypeKind::Vec2;
    TypeInfo float_type;
    float_type.kind = TypeKind::Float;

    ResolvedFunc func;
    func.name           = "dot";
    func.module_name    = "std.math.vec2";
    func.is_pub         = true;
    func.effect_summary = std::unordered_set<std::string>{};  // proven pure: allowed in where:
    func.params         = {
        ResolvedParam{.name = "a", .type = vec2_type},
        ResolvedParam{.name = "b", .type = vec2_type},
    };
    func.return_type  = float_type;
    const auto symbol = make_symbol_id(SymbolKind::Func, "std.math.vec2", "dot");
    func.symbol_id    = symbol;
    func.canonical_id = make_canonical_id(symbol);

    syms.funcs["dot"] = std::move(func);
    return syms;
}

TEST_CASE("Semantic: manual squared-distance-via-dot where: expression is recognized as broad-phase eligible (2D)",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func dot(a: vec2, b: vec2) float:\n"
                          "    return 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        dot(b[Transform].position - a[Transform].position, b[Transform].position - "
                          "a[Transform].position) < (a[Collider].radius + b[Collider].radius) * (a[Collider].radius + "
                          "b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    REQUIRE(contract.spatial_join.has_value());
    const auto& plan = *contract.spatial_join;
    CHECK(plan.dimension == SpatialJoinDimension::Flat2D);
    CHECK(plan.matched_predicate_index == 0);

    // The manual matcher pairs positions with radii by binding name, so don't
    // assume which of plan.left/plan.right is binding "a" vs "b".
    const auto& a_side = plan.left.binding_index == 0 ? plan.left : plan.right;
    const auto& b_side = plan.left.binding_index == 0 ? plan.right : plan.left;
    CHECK(a_side.binding_index == 0);
    CHECK(a_side.kind == SpatialShapeKind::Sphere);
    // dot(b.p - a.p, ...) < (a.r + b.r) * (...): a.p is the delta's right operand.
    CHECK(a_side.slots == std::vector<ExprPath>{{0, 0, 1}, {1, 0, 0}});
    CHECK(b_side.binding_index == 1);
    CHECK(b_side.slots == std::vector<ExprPath>{{0, 0, 0}, {1, 0, 1}});
}

TEST_CASE("Semantic: manual squared-distance-via-dot where: expression is recognized as broad-phase eligible (3D)",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec3\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec3\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func dot(a: vec3, b: vec3) float:\n"
                          "    return 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        dot(b[Transform].position - a[Transform].position, b[Transform].position - "
                          "a[Transform].position) < (a[Collider].radius + b[Collider].radius) * (a[Collider].radius + "
                          "b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(result.handler_contracts[0].spatial_join->dimension == SpatialJoinDimension::Volume3D);
}

TEST_CASE("Semantic: manual squared-distance-via-dot where: expression accepts <= as well as <",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func dot(a: vec2, b: vec2) float:\n"
                          "    return 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        dot(b[Transform].position - a[Transform].position, b[Transform].position - "
                          "a[Transform].position) <= (a[Collider].radius + b[Collider].radius) * (a[Collider].radius + "
                          "b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: manual dot-product recognition succeeds through a renamed import alias",
          "[semantic][where-clause][spatial-join]") {
    ModuleImports imports;
    imports.modules["v2m"] = make_dot_import();

    auto result = analyze_with_imports(stdlib_events() +
                                           "trait Transform:\n"
                                           "    var position: vec2\n"
                                           "trait Collider:\n"
                                           "    var radius: float\n"
                                           "rule DetectContact:\n"
                                           "    pairs:\n"
                                           "        a:\n"
                                           "            Transform\n"
                                           "            Collider\n"
                                           "        b:\n"
                                           "            Transform\n"
                                           "            Collider\n"
                                           "    where:\n"
                                           "        v2m.dot(b[Transform].position - a[Transform].position, "
                                           "b[Transform].position - a[Transform].position) < (a[Collider].radius + "
                                           "b[Collider].radius) * (a[Collider].radius + b[Collider].radius)\n"
                                           "    on tick:\n"
                                           "        let x = 1\n",
                                       imports);

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(result.handler_contracts[0].spatial_join->dimension == SpatialJoinDimension::Flat2D);
}

TEST_CASE("Semantic: cross-domain pair rule with manual dot-product shape is eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "trait Wall:\n"
                          "    var active: bool = true\n"
                          "pub func dot(a: vec2, b: vec2) float:\n"
                          "    return 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "            Wall\n"
                          "    where:\n"
                          "        dot(b[Transform].position - a[Transform].position, b[Transform].position - "
                          "a[Transform].position) < (a[Collider].radius + b[Collider].radius) * (a[Collider].radius + "
                          "b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: component-wise squared-distance arithmetic remains an ordinary residual predicate",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze(stdlib_events() +
                          "trait Transform:\n"
                          "    var x: float\n"
                          "    var y: float\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        (b[Transform].x - a[Transform].x) * (b[Transform].x - a[Transform].x) + "
                          "(b[Transform].y - a[Transform].y) * (b[Transform].y - a[Transform].y) < "
                          "(a[Collider].radius + b[Collider].radius) * (a[Collider].radius + b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE(
    "Semantic: distance check split across intermediate handler-body let bindings (no where: clause) "
    "remains unrecognized",
    "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func dot(a: vec2, b: vec2) float:\n"
                          "    return 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    on tick:\n"
                          "        let delta = b[Transform].position - a[Transform].position\n"
                          "        let dist_sq = dot(delta, delta)\n"
                          "        let radius_sum = a[Collider].radius + b[Collider].radius\n"
                          "        if dist_sq >= radius_sum * radius_sum:\n"
                          "            return\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: manual dot-product expression with a comparison operator outside {<, <=} is unrecognized",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + stdlib_events() +
                          "trait Transform:\n"
                          "    var position: vec2\n"
                          "trait Collider:\n"
                          "    var radius: float\n"
                          "pub func dot(a: vec2, b: vec2) float:\n"
                          "    return 0.0\n"
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        dot(b[Transform].position - a[Transform].position, b[Transform].position - "
                          "a[Transform].position) == (a[Collider].radius + b[Collider].radius) * (a[Collider].radius + "
                          "b[Collider].radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
}

// ── Unaccelerated linear-distance warning diagnostic (add-sap-broadphase) ───

static bool has_warning_containing(const std::vector<Diagnostic>& diagnostics, const std::string& needle) {
    return std::ranges::any_of(diagnostics, [&needle](const Diagnostic& diagnostic) {
        return diagnostic.level == DiagnosticLevel::Warning && diagnostic.message.contains(needle);
    });
}

static std::size_t count_warnings(const std::vector<Diagnostic>& diagnostics) {
    return static_cast<std::size_t>(std::ranges::count(diagnostics, DiagnosticLevel::Warning, &Diagnostic::level));
}

static std::string unaccelerated_pair_warning(const std::string& rule, const std::string& left, const std::string& right) {
    return "pair rule '" + rule + "' is not accelerated: no recognized overlap predicate in where:, so every (" + left +
           ", " + right + ") tuple is checked";
}

static int line_of(const std::string& source, const std::string& text) {
    const auto offset = source.find(text);
    REQUIRE(offset != std::string::npos);
    return static_cast<int>(std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(offset), '\n')) + 1;
}

static std::string equality_join_rule(const std::string& cell_x, const std::string& predicate) {
    return stdlib_events() +
           "trait Cell:\n"
           "    " + cell_x + " x: int\n"
           "    let owner: entity_id\n"
           "    var weight: float = 1.0\n"
           "trait Probe:\n"
           "    var x: int = 0\n"
           "    var owner: entity_id\n"
           "    var found: int = 0\n"
           "rule Detect:\n"
           "    pairs:\n"
           "        probe:\n"
           "            Probe\n"
           "        cell:\n"
           "            Cell as c\n"
           "    where:\n"
           "        " + predicate + "\n"
           "    reduce per probe:\n"
           "        matches = count()\n"
           "    on tick:\n"
           "        probe[Probe].found = matches\n";
}

TEST_CASE("Semantic: equalities on let fields and the member itself form an equality join",
          "[semantic][where-clause][equality-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(equality_join_rule(
        "let", "probe[Probe].found >= 0 and cell[Cell].x == probe[Probe].x + 1 and probe[Probe].owner == c.owner"));
    REQUIRE(result.handler_contracts.size() == 1);
    const auto& plan = result.handler_contracts[0].equality_join;
    REQUIRE(plan.has_value());
    CHECK(plan->indexed_binding == 1);
    REQUIRE(plan->keys.size() == 2);
    CHECK(plan->keys[0].member == ExprPath{0, 1, 0});
    CHECK(plan->keys[0].query == ExprPath{0, 1, 1});
    CHECK(plan->keys[1].member == ExprPath{1, 1});
    CHECK(plan->keys[1].query == ExprPath{1, 0});
    CHECK(count_warnings(diagnostics) == 0);

    auto [identity, identity_diagnostics] = analyze_with_diagnostics(equality_join_rule("let", "cell == probe[Probe].owner"));
    REQUIRE(identity.handler_contracts[0].equality_join.has_value());
    CHECK(identity.handler_contracts[0].equality_join->keys.front().member == ExprPath{0});
}

static std::string ranged_join_rule() {
    return stdlib_events() +
           "enum Kind:\n"
           "    Stone\n"
           "    Glass\n"
           "trait Volume:\n"
           "    let min_x: int = 0\n"
           "    let max_x: int = 3\n"
           "trait Cell:\n"
           "    let volume: entity_id\n"
           "    let x: int in range(volume[Volume].min_x, volume[Volume].max_x + 1)\n"
           "    let level: int in range(0, 4)\n"
           "    let lit: bool\n"
           "    let kind: Kind\n"
           "    let size: int = 3\n"
           "    let step: int in range(0, size)\n"
           "trait Probe:\n"
           "    var volume: entity_id\n"
           "    var x: int = 0\n"
           "    var level: int = 0\n"
           "    var lit: bool = false\n"
           "    var kind: Kind = Kind.Stone\n"
           "    var found: int = 0\n"
           "rule Detect:\n"
           "    pairs:\n"
           "        probe:\n"
           "            Probe\n"
           "        cell:\n"
           "            Cell\n"
           "    where:\n"
           "        cell[Cell].volume == probe[Probe].volume and cell[Cell].x == probe[Probe].x and "
           "cell[Cell].level == probe[Probe].level and cell[Cell].lit == probe[Probe].lit and "
           "cell[Cell].kind == probe[Probe].kind and cell[Cell].step == probe[Probe].x\n"
           "    reduce per probe:\n"
           "        matches = count()\n"
           "    on tick:\n"
           "        probe[Probe].found = matches\n";
}

static std::size_t count_infos_containing(const std::vector<Diagnostic>& diagnostics, const std::string& needle) {
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [&](const Diagnostic& diagnostic) {
        return diagnostic.level == DiagnosticLevel::Info && diagnostic.message.contains(needle);
    }));
}

TEST_CASE("Semantic: each equality key part records how it can be addressed",
          "[semantic][where-clause][equality-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(ranged_join_rule());
    const auto& plan = result.handler_contracts.at(0).equality_join;
    REQUIRE(plan.has_value());
    REQUIRE(plan->keys.size() == 6);
    CHECK(plan->keys[0].layout == EqualityKeyLayout::Entity);
    CHECK(plan->keys[0].field == "volume");
    CHECK(plan->keys[1].layout == EqualityKeyLayout::Ranged);
    CHECK(plan->keys[1].container == "volume");
    CHECK(plan->keys[1].field == "x");
    REQUIRE(plan->keys[1].trait.has_value());
    CHECK(plan->keys[1].trait->local_name == "Cell");
    CHECK(plan->keys[2].layout == EqualityKeyLayout::Direct);
    CHECK(plan->keys[3].layout == EqualityKeyLayout::Direct);
    CHECK(plan->keys[4].layout == EqualityKeyLayout::Direct);
    // A range read from the member's own fields gives no shared block.
    CHECK(plan->keys[5].layout == EqualityKeyLayout::Hashed);
    CHECK(count_infos_containing(diagnostics, "Cell.step") == 1);
    CHECK(count_infos_containing(diagnostics, "Cell.x") == 0);
    CHECK(count_infos_containing(diagnostics, "Cell.level") == 0);
    CHECK(count_warnings(diagnostics) == 0);
}

TEST_CASE("Semantic: an unranged int key part is hashed and reported once",
          "[semantic][where-clause][equality-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        equality_join_rule("let", "cell[Cell].x == probe[Probe].x and probe[Probe].owner == c.owner"));
    const auto& plan = result.handler_contracts.at(0).equality_join;
    REQUIRE(plan.has_value());
    CHECK(plan->keys[0].layout == EqualityKeyLayout::Hashed);
    CHECK(plan->keys[1].layout == EqualityKeyLayout::Entity);
    REQUIRE(count_infos_containing(diagnostics, "Cell.x") == 1);
    CHECK(count_infos_containing(diagnostics, "'Detect'") == 1);
    CHECK(count_infos_containing(diagnostics, "range") == 1);
    CHECK(count_warnings(diagnostics) == 0);

    // An identity key keeps no index, so nothing is hashed.
    auto [identity, identity_diagnostics] =
        analyze_with_diagnostics(equality_join_rule("let", "cell == probe[Probe].owner and cell[Cell].x == probe[Probe].x"));
    CHECK(count_infos_containing(identity_diagnostics, "Cell.x") == 0);
}

TEST_CASE("Semantic: var fields, cross reads and missing reductions form no equality join",
          "[semantic][where-clause][equality-join][diagnostics]") {
    auto [var_field, var_diagnostics] = analyze_with_diagnostics(equality_join_rule("var", "cell[Cell].x == probe[Probe].x"));
    CHECK_FALSE(var_field.handler_contracts[0].equality_join.has_value());
    CHECK(has_warning_containing(var_diagnostics, unaccelerated_pair_warning("Detect", "probe", "cell")));

    auto [both, both_diagnostics] =
        analyze_with_diagnostics(equality_join_rule("let", "cell[Cell].x == probe[Probe].x + c.x or c.weight > 2.0"));
    CHECK_FALSE(both.handler_contracts[0].equality_join.has_value());

    auto [self_read, self_diagnostics] = analyze_with_diagnostics(equality_join_rule("let", "cell[Cell].x == c.x"));
    CHECK_FALSE(self_read.handler_contracts[0].equality_join.has_value());

    auto [weight, weight_diagnostics] = analyze_with_diagnostics(equality_join_rule("let", "c.weight == 1.0"));
    CHECK_FALSE(weight.handler_contracts[0].equality_join.has_value());
}

// A pair rule over grid cells `a` and `b`; `head` precedes `where:`, `clauses` follow it.
static std::string grid_pair_rule(const std::string& predicates, const std::string& clauses, const std::string& head = "") {
    return stdlib_events() +
           "trait Cell:\n"
           "    let x: int in range(0, 8)\n"
           "    let y: int in range(0, 8)\n"
           "    let z: int in range(0, 8)\n"
           "    let volume: entity_id\n"
           "    let depth: int\n"
           "    var open: bool = false\n"
           "    var seen: int = 0\n"
           "trait Linked\n"
           "rule Join:\n"
           "    pairs:\n"
           "        a:\n"
           "            Cell\n"
           "        b:\n"
           "            Cell\n" +
           head + "    where:\n" + predicates + clauses;
}

static const std::string kGridTick = "    on tick:\n        let x = 1\n";

static std::string neighbor_predicate() {
    return "        (b[Cell].x == a[Cell].x + 1 and b[Cell].y == a[Cell].y) or "
           "(b[Cell].x == a[Cell].x - 1 and b[Cell].y == a[Cell].y) or "
           "(b[Cell].y == a[Cell].y + 1 and b[Cell].x == a[Cell].x) or "
           "(b[Cell].y == a[Cell].y - 1 and b[Cell].x == a[Cell].x)\n";
}

TEST_CASE("Semantic: every pair rule kind can be an equality join indexing the right binding",
          "[semantic][where-clause][equality-join][diagnostics]") {
    const std::string keyed = "        b[Cell].x == a[Cell].x + 1 and b[Cell].y == a[Cell].y\n";
    const std::vector<std::array<std::string, 3>> kinds{{
        {"plain handler", kGridTick, ""},
        {"unreduced keep", "    keep Linked on a\nextern event frame\nphase step:\n    from:\n        frame\ngroup facts:\n    phase: step\n",
         "    group: facts\n"},
        {"global reduce", "    reduce:\n        n = count()\n    on tick:\n        let x = n\n", ""},
    }};
    for (const auto& [kind, clauses, head] : kinds) {
        INFO(kind);
        auto [result, diagnostics] = analyze_with_diagnostics(grid_pair_rule(keyed, clauses, head));
        REQUIRE_FALSE(result.handler_contracts.empty());
        const auto& plan = result.handler_contracts[0].equality_join;
        REQUIRE(plan.has_value());
        CHECK(plan->indexed_binding == 1);
        CHECK(plan->branches.empty());
        REQUIRE(plan->keys.size() == 2);
        CHECK(plan->keys[0].field == "x");
        CHECK(plan->keys[1].field == "y");
        CHECK_FALSE(has_warning_containing(diagnostics, unaccelerated_pair_warning("Join", "a", "b")));
    }
}

TEST_CASE("Semantic: a shifted equality keys the member field with the shifted value",
          "[semantic][where-clause][equality-join]") {
    auto [result, diagnostics] =
        analyze_with_diagnostics(grid_pair_rule("        b[Cell].x == a[Cell].x - 1\n", kGridTick));
    const auto& plan = result.handler_contracts.at(0).equality_join;
    REQUIRE(plan.has_value());
    REQUIRE(plan->keys.size() == 1);
    CHECK(plan->keys[0].field == "x");
    REQUIRE(plan->keys[0].trait.has_value());
    CHECK(plan->keys[0].trait->local_name == "Cell");
    CHECK(plan->keys[0].member == ExprPath{0});
    CHECK(plan->keys[0].query == ExprPath{1});
}

TEST_CASE("Semantic: an or of equality chains is a disjunctive equality join",
          "[semantic][where-clause][equality-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(grid_pair_rule(neighbor_predicate(), kGridTick));
    const auto& plan = result.handler_contracts.at(0).equality_join;
    REQUIRE(plan.has_value());
    CHECK(plan->indexed_binding == 1);
    CHECK(plan->keys.empty());
    REQUIRE(plan->branches.size() == 4);
    for (const auto& branch : plan->branches) {
        REQUIRE(branch.size() == 2);
        // Every branch lists its keys in the first branch's field order.
        CHECK(branch[0].field == "x");
        CHECK(branch[1].field == "y");
    }
    // The third branch is `b.y == a.y + 1 and b.x == a.x`: its x key is the right conjunct.
    CHECK(plan->branches[2][0].member == ExprPath{0, 1, 1, 0});
    CHECK(plan->branches[2][1].member == ExprPath{0, 1, 0, 0});
    CHECK(plan->branches[3][0].member == ExprPath{1, 1, 0});
    CHECK_FALSE(has_warning_containing(diagnostics, unaccelerated_pair_warning("Join", "a", "b")));
}

TEST_CASE("Semantic: top-level keys are shared by every branch of a disjunctive join",
          "[semantic][where-clause][equality-join]") {
    auto [result, diagnostics] = analyze_with_diagnostics(grid_pair_rule(
        "        b[Cell].volume == a[Cell].volume\n"
        "        (b[Cell].x == a[Cell].x + 1) or (b[Cell].x == a[Cell].x - 1)\n",
        kGridTick));
    const auto& plan = result.handler_contracts.at(0).equality_join;
    REQUIRE(plan.has_value());
    REQUIRE(plan->keys.size() == 1);
    CHECK(plan->keys[0].field == "volume");
    REQUIRE(plan->branches.size() == 2);
    CHECK(plan->branches[0].size() == 1);
    CHECK(plan->branches[1].at(0).field == "x");
    CHECK(plan->branches[1].at(0).predicate_index == 1);

    // A branch key on a field the shared keys already fix adds nothing.
    auto [shared, shared_diagnostics] = analyze_with_diagnostics(grid_pair_rule(
        "        b[Cell].y == a[Cell].y\n"
        "        (b[Cell].x == a[Cell].x + 1 and b[Cell].y == a[Cell].y) or (b[Cell].x == a[Cell].x - 1)\n",
        kGridTick));
    const auto& shared_plan = shared.handler_contracts.at(0).equality_join;
    REQUIRE(shared_plan.has_value());
    REQUIRE(shared_plan->branches.size() == 2);
    CHECK(shared_plan->branches[0].size() == 1);
}

TEST_CASE("Semantic: an or whose branches do not all key the same fields is no join key",
          "[semantic][where-clause][equality-join][diagnostics]") {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"different fields",
         "        (b[Cell].x == a[Cell].x and b[Cell].y == a[Cell].y) or (b[Cell].z == a[Cell].z)\n"},
        {"branch without key", "        (b[Cell].x == a[Cell].x and b[Cell].y == a[Cell].y) or a[Cell].open\n"},
        {"nested or",
         "        (b[Cell].x == a[Cell].x and (b[Cell].y == a[Cell].y or b[Cell].y == a[Cell].y + 1)) or "
         "(b[Cell].x == a[Cell].x + 1 and b[Cell].y == a[Cell].y)\n"},
    };
    for (const auto& [name, predicate] : cases) {
        INFO(name);
        auto [result, diagnostics] = analyze_with_diagnostics(grid_pair_rule(predicate, kGridTick));
        CHECK_FALSE(result.handler_contracts.at(0).equality_join.has_value());
        CHECK(has_warning_containing(diagnostics, unaccelerated_pair_warning("Join", "a", "b")));
    }

    // Another predicate still makes the rule an equality join.
    auto [fallback, fallback_diagnostics] = analyze_with_diagnostics(grid_pair_rule(
        "        b[Cell].volume == a[Cell].volume\n"
        "        (b[Cell].x == a[Cell].x) or (b[Cell].z == a[Cell].z)\n",
        kGridTick));
    const auto& plan = fallback.handler_contracts.at(0).equality_join;
    REQUIRE(plan.has_value());
    CHECK(plan->keys.size() == 1);
    CHECK(plan->branches.empty());
}

TEST_CASE("Semantic: a hashed key part of a disjunctive join is reported once",
          "[semantic][where-clause][equality-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(grid_pair_rule(
        "        (b[Cell].depth == a[Cell].depth + 1) or (b[Cell].depth == a[Cell].depth - 1)\n", kGridTick));
    const auto& plan = result.handler_contracts.at(0).equality_join;
    REQUIRE(plan.has_value());
    CHECK(plan->branches.at(0).at(0).layout == EqualityKeyLayout::Hashed);
    CHECK(count_infos_containing(diagnostics, "Cell.depth") == 1);
}

TEST_CASE("Semantic: linear 2D distance-vs-radius-sum where: predicate is flagged with a warning",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        "module std.math.vec2\n" + stdlib_events() +
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func distance(a: vec2, b: vec2) float:\n"
        "    return 0.0\n"
        "rule DetectContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Transform\n"
        "            Collider\n"
        "        b:\n"
        "            Transform\n"
        "            Collider\n"
        "    where:\n"
        "        distance(a[Transform].position, b[Transform].position) < a[Collider].radius + b[Collider].radius\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(has_warning_containing(diagnostics, "circles_overlap"));
    CHECK(count_warnings(diagnostics) == 1);
}

TEST_CASE("Semantic: linear 3D distance-vs-radius-sum where: predicate is flagged with a warning",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        "module std.math.vec3\n" + stdlib_events() +
        "trait Transform:\n"
        "    var position: vec3\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func distance(a: vec3, b: vec3) float:\n"
        "    return 0.0\n"
        "rule DetectContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Transform\n"
        "            Collider\n"
        "        b:\n"
        "            Transform\n"
        "            Collider\n"
        "    where:\n"
        "        distance(a[Transform].position, b[Transform].position) >= a[Collider].radius + b[Collider].radius\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(has_warning_containing(diagnostics, "spheres_overlap"));
    CHECK(count_warnings(diagnostics) == 1);
}

TEST_CASE("Semantic: linear-distance where: predicate is flagged with > as well as >=/</<=",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        "module std.math.vec2\n" + stdlib_events() +
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func distance(a: vec2, b: vec2) float:\n"
        "    return 0.0\n"
        "rule DetectContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Transform\n"
        "            Collider\n"
        "        b:\n"
        "            Transform\n"
        "            Collider\n"
        "    where:\n"
        "        distance(a[Transform].position, b[Transform].position) > a[Collider].radius + b[Collider].radius\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(has_warning_containing(diagnostics, "circles_overlap"));
    CHECK(count_warnings(diagnostics) == 1);
}

TEST_CASE("Semantic: pair rule with only unrecognized predicates is flagged as unaccelerated",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(stdlib_events() +
                                                          "trait Transform:\n"
                                                          "    var position: vec2\n"
                                                          "trait Collider:\n"
                                                          "    var radius: float\n"
                                                          "rule DetectContact:\n"
                                                          "    pairs:\n"
                                                          "        a:\n"
                                                          "            Transform\n"
                                                          "            Collider\n"
                                                          "        b:\n"
                                                          "            Transform\n"
                                                          "            Collider\n"
                                                          "    where:\n"
                                                          "        a != b\n"
                                                          "    on tick:\n"
                                                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].level == DiagnosticLevel::Warning);
    CHECK(diagnostics[0].message == unaccelerated_pair_warning("DetectContact", "a", "b"));
}

TEST_CASE("Semantic: direct-call recognized predicate produces no unaccelerated-distance warning",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        "module std.collision.flat\n" + stdlib_events() +
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, b_radius: float) bool:\n"
        "    return a_radius + b_radius >= 0.0\n"
        "rule DetectContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Transform\n"
        "            Collider\n"
        "        b:\n"
        "            Transform\n"
        "            Collider\n"
        "    where:\n"
        "        circles_overlap(a[Transform].position, a[Collider].radius, b[Transform].position, "
        "b[Collider].radius)\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(diagnostics.empty());
}

TEST_CASE("Semantic: manual dot-product recognized predicate produces no unaccelerated-distance warning",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        "module std.math.vec2\n" + stdlib_events() +
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func dot(a: vec2, b: vec2) float:\n"
        "    return 0.0\n"
        "rule DetectContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Transform\n"
        "            Collider\n"
        "        b:\n"
        "            Transform\n"
        "            Collider\n"
        "    where:\n"
        "        dot(b[Transform].position - a[Transform].position, b[Transform].position - a[Transform].position) < "
        "(a[Collider].radius + b[Collider].radius) * (a[Collider].radius + b[Collider].radius)\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(diagnostics.empty());
}
TEST_CASE("Semantic: pair rule without where: is flagged at its pairs: clause",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    const std::string source   = "module test\n" + stdlib_events() +
                                 "trait Rig:\n"
                                 "    var height: float\n"
                                 "trait Body:\n"
                                 "    var radius: float\n"
                                 "rule ComposePose:\n"
                                 "    pairs:\n"
                                 "        rig:\n"
                                 "            Rig\n"
                                 "        body:\n"
                                 "            Body\n"
                                 "    on tick:\n"
                                 "        let x = 1\n";
    auto [result, diagnostics] = analyze_with_diagnostics(source);

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].level == DiagnosticLevel::Warning);
    CHECK(diagnostics[0].message == unaccelerated_pair_warning("ComposePose", "rig", "body"));
    CHECK(diagnostics[0].location.line == line_of(source, "    pairs:"));
}

TEST_CASE("Semantic: recognized predicate next to a residual one is not flagged",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        "module std.collision.flat\n" + stdlib_events() +
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, b_radius: float) bool:\n"
        "    return a_radius + b_radius >= 0.0\n"
        "rule DetectContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Transform\n"
        "            Collider\n"
        "        b:\n"
        "            Transform\n"
        "            Collider\n"
        "    where:\n"
        "        a != b\n"
        "        circles_overlap(a[Transform].position, a[Collider].radius, b[Transform].position, "
        "b[Collider].radius)\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(diagnostics.empty());
}

TEST_CASE("Semantic: unary rule with where: is never flagged as unaccelerated",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(stdlib_events() +
                                                          "trait Collider:\n"
                                                          "    var radius: float\n"
                                                          "rule Shrink:\n"
                                                          "    each:\n"
                                                          "        Collider\n"
                                                          "    where:\n"
                                                          "        Collider.radius > 1.0\n"
                                                          "    on tick:\n"
                                                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(diagnostics.empty());
}

// ── Limit clause (dsl-rule-limit) ───────────────────────────────────────────

static const std::string& limit_traits() {
    static const std::string value =
        "trait Actor:\n"
        "    var grounded: bool = false\n"
        "    var height: float = 0.0\n"
        "trait Surface:\n"
        "    var top: float = 0.0\n"
        "trait Tower:\n"
        "    var target_count: int = 1\n";
    return value;
}

// The `provably_one` proof is recorded on the AST for codegen to consume, so
// asserting on it needs the analyzed ProgramNode rather than DecoratedProgram.
static bool analyze_limit_provably_one(const std::string& source) {
    const std::string src = "module test\n" + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program);
    for (const auto& decl : program.declarations) {
        const auto* rule = std::get_if<RuleNode>(&decl);
        if (rule != nullptr && rule->limit.has_value()) {
            return !rule->limit->parts.empty() && rule->limit->parts.front().provably_one;
        }
    }
    FAIL("no rule with a limit: clause in the analyzed program");
    return false;
}

TEST_CASE("Semantic: limit without filter or pairs reports the specific diagnostic", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() +
                                   "rule NoDomain:\n"
                                   "    limit: 10\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("`limit:` requires an `each:` or `pairs:` clause"));
}

TEST_CASE("Semantic: global limit is accepted on both a unary and a pair domain", "[semantic][rule-limit]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + limit_traits() +
                                   "rule Unary:\n"
                                   "    each:\n"
                                   "        Actor as a\n"
                                   "    limit: 10\n"
                                   "    on tick:\n"
                                   "        let x = 1\n"
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 10\n"
                                   "    on tick:\n"
                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: per-binding limit on a unary rule is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Unary:\n"
                                   "    each:\n"
                                   "        Actor as a\n"
                                   "    limit: 1 per a\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("`per`"));
}

TEST_CASE("Semantic: per-binding limit naming an undeclared binding is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per enemy\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("pair binding"));
}

TEST_CASE("Semantic: per-binding limit computed from the per binding's own trait is accepted",
          "[semantic][rule-limit]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + limit_traits() +
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        tower:\n"
                                   "            Tower\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: tower[Tower].target_count per tower\n"
                                   "    on tick:\n"
                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: per-binding limit referencing the other binding is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        tower:\n"
                                   "            Tower\n"
                                   "        other:\n"
                                   "            Tower\n"
                                   "    limit: other[Tower].target_count per tower\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("limit"));
}

TEST_CASE("Semantic: non-int limit count is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Unary:\n"
                                   "    each:\n"
                                   "        Actor as a\n"
                                   "    limit: 1.5\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("'int'"));
}

TEST_CASE("Semantic: a global limit count referencing a domain binding is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        tower:\n"
                                   "            Tower\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: tower[Tower].target_count\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("limit"));
}

TEST_CASE("Semantic: a global limit count referencing a unary filter alias is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Unary:\n"
                                   "    each:\n"
                                   "        Actor as a\n"
                                   "    limit: a.height\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("filter binding"));
}

// Bare unqualified field access is ordinarily legal DSL (dsl-where-clause),
// so the scope check has to deny it here too, not just the alias-qualified
// spelling `a.height` covered above.
TEST_CASE("Semantic: a global limit count referencing a bare filter field is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Unary:\n"
                                   "    each:\n"
                                   "        Actor as a\n"
                                   "    limit: height\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("filter binding"));
}

TEST_CASE("Semantic: impure limit count is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Unary:\n"
                                   "    each:\n"
                                   "        Actor as a\n"
                                   "    limit: query.count[Surface]()\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.contains("must be pure"));
}

TEST_CASE("Semantic: literal one is provably one", "[semantic][rule-limit]") {
    CHECK(analyze_limit_provably_one(stdlib_events() + limit_traits() +
                                     "rule Paired:\n"
                                     "    pairs:\n"
                                     "        actor:\n"
                                     "            Actor\n"
                                     "        surface:\n"
                                     "            Surface\n"
                                     "    limit: 1 per actor\n"
                                     "    on tick:\n"
                                     "        let x = 1\n"));
}

TEST_CASE("Semantic: a named constant equal to one is provably one", "[semantic][rule-limit]") {
    CHECK(analyze_limit_provably_one(stdlib_events() + limit_traits() +
                                     "const:\n"
                                     "    MAX_ONE = 1\n"
                                     "rule Paired:\n"
                                     "    pairs:\n"
                                     "        actor:\n"
                                     "            Actor\n"
                                     "        surface:\n"
                                     "            Surface\n"
                                     "    limit: MAX_ONE per actor\n"
                                     "    on tick:\n"
                                     "        let x = 1\n"));
}

TEST_CASE("Semantic: a constant other than one is not provably one", "[semantic][rule-limit]") {
    CHECK_FALSE(analyze_limit_provably_one(stdlib_events() + limit_traits() +
                                           "rule Paired:\n"
                                           "    pairs:\n"
                                           "        actor:\n"
                                           "            Actor\n"
                                           "        surface:\n"
                                           "            Surface\n"
                                           "    limit: 2 per actor\n"
                                           "    on tick:\n"
                                           "        let x = 1\n"));
}

TEST_CASE("Semantic: a non-constant count is not provably one", "[semantic][rule-limit]") {
    CHECK_FALSE(analyze_limit_provably_one(stdlib_events() + limit_traits() +
                                           "rule Paired:\n"
                                           "    pairs:\n"
                                           "        tower:\n"
                                           "            Tower\n"
                                           "        surface:\n"
                                           "            Surface\n"
                                           "    limit: tower[Tower].target_count per tower\n"
                                           "    on tick:\n"
                                           "        let x = 1\n"));
}

// ── Pair read-only carve-out (dsl-pair-relations) ───────────────────────────

TEST_CASE("Semantic: mutation through a provably-one per binding is accepted", "[semantic][rule-limit][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(stdlib_events() + limit_traits() +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per actor\n"
                                   "    on tick:\n"
                                   "        actor[Actor].grounded = true\n"));
}

TEST_CASE("Semantic: the non-limited binding remains read-only under a per-binding limit",
          "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per actor\n"
                                   "    on tick:\n"
                                   "        surface[Surface].top = 1.0\n");
    CHECK(err.contains("read-only"));
}

TEST_CASE("Semantic: mutation is rejected when the limit is not provably one",
          "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 2 per actor\n"
                                   "    on tick:\n"
                                   "        actor[Actor].grounded = true\n");
    CHECK(err.contains("read-only"));
}

TEST_CASE("Semantic: a global limit grants no pair-binding write access", "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 10\n"
                                   "    on tick:\n"
                                   "        actor[Actor].grounded = true\n");
    CHECK(err.contains("read-only"));
}

TEST_CASE("Semantic: a rule with no limit still rejects mutation on either pair binding",
          "[semantic][rule-limit][pair-relations]") {
    CHECK(analyze_has_errors(stdlib_events() + limit_traits() +
                             "rule Ground:\n"
                             "    pairs:\n"
                             "        actor:\n"
                             "            Actor\n"
                             "        surface:\n"
                             "            Surface\n"
                             "    on tick:\n"
                             "        actor[Actor].grounded = true\n"));
    CHECK(analyze_has_errors(stdlib_events() + limit_traits() +
                             "rule Ground:\n"
                             "    pairs:\n"
                             "        actor:\n"
                             "            Actor\n"
                             "        surface:\n"
                             "            Surface\n"
                             "    on tick:\n"
                             "        surface[Surface].top = 1.0\n"));
}

// The trait-match check stays unconditional: only ordinary dotted-path
// assignment is carved out, never a mutable match alias.
TEST_CASE("Semantic: trait match on a provably-limited binding is still rejected",
          "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per actor\n"
                                   "    on tick:\n"
                                   "        match actor:\n"
                                   "            Actor as a =>\n"
                                   "                let x = 1\n");
    CHECK(err.contains("cannot trait-match directly on binding"));
}

// A bare `actor = ...` has no field to write; only a dotted trait path
// through the provably-one binding is admitted (spec: "only ordinary
// dotted-path assignment is admitted").
TEST_CASE("Semantic: a bare reassignment of a provably-limited binding is still rejected",
          "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(stdlib_events() + limit_traits() +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per actor\n"
                                   "    on tick:\n"
                                   "        actor = surface\n");
    CHECK(err.contains("read-only"));
}

// ── Pair write contract inference (handler-contracts) ───────────────────────

TEST_CASE("Semantic: a provably-limited pair write is recorded in the contract",
          "[semantic][rule-limit][handler-contracts]") {
    auto result = analyze(stdlib_events() + limit_traits() +
                          "rule Ground:\n"
                          "    pairs:\n"
                          "        actor:\n"
                          "            Actor\n"
                          "        surface:\n"
                          "            Surface\n"
                          "    limit: 1 per actor\n"
                          "    on tick:\n"
                          "        actor[Actor].grounded = true\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    const auto actor_trait = make_symbol_id(SymbolKind::Trait, "test", "Actor");
    CHECK(contract.writes.contains(actor_trait));
    CHECK(contract.reads.contains(actor_trait));
}

TEST_CASE("Semantic: a provably-limited pair write conflicts with a unary read of the same trait",
          "[semantic][rule-limit][handler-contracts]") {
    auto result = analyze(stdlib_events() + limit_traits() +
                          "rule Ground:\n"
                          "    pairs:\n"
                          "        actor:\n"
                          "            Actor\n"
                          "        surface:\n"
                          "            Surface\n"
                          "    limit: 1 per actor\n"
                          "    on tick:\n"
                          "        actor[Actor].grounded = true\n"
                          "rule ReadGround:\n"
                          "    each:\n"
                          "        Actor as a\n"
                          "    on tick:\n"
                          "        let x = a.grounded\n");

    REQUIRE(result.handler_contracts.size() == 2);
    const auto actor_trait = make_symbol_id(SymbolKind::Trait, "test", "Actor");
    const auto writer = std::ranges::find_if(result.handler_contracts,
                                             [&](const auto& c) { return c.writes.contains(actor_trait); });
    REQUIRE(writer != result.handler_contracts.end());
    CHECK(writer->domain_kind == HandlerDomainKind::Pair);
    const auto reader = std::ranges::find_if(result.handler_contracts, [&](const auto& c) {
        return c.domain_kind != HandlerDomainKind::Pair && c.reads.contains(actor_trait);
    });
    REQUIRE(reader != result.handler_contracts.end());
}

// ── Field-level contract access (handler-contracts) ─────────────────────────

static ResolvedTrait match_trait_declaration() {
    ResolvedTrait trait;
    trait.name   = "Match";
    trait.fields = {ResolvedField{.name = "over"}, ResolvedField{.name = "score"}};
    return trait;
}

static FieldAccess fields_of(std::initializer_list<std::string> names) {
    return FieldAccess{.all = false, .fields = std::set<std::string>(names)};
}

TEST_CASE("Contract field access: a named declared field is recorded alone", "[semantic][handler-contracts]") {
    const auto match       = make_symbol_id(SymbolKind::Trait, "test", "Match");
    const auto declaration = match_trait_declaration();
    HandlerContract contract;
    record_read(contract, match, "over", &declaration);

    CHECK(contract.reads.contains(match));
    CHECK(contract.read_fields.at(match) == fields_of({"over"}));
    CHECK(contract.writes.empty());
    CHECK(contract.write_fields.empty());
}

TEST_CASE("Contract field access: nullopt covers all fields and absorbs named fields",
          "[semantic][handler-contracts]") {
    const auto match       = make_symbol_id(SymbolKind::Trait, "test", "Match");
    const auto declaration = match_trait_declaration();
    HandlerContract contract;
    record_read(contract, match, "over", &declaration);
    record_read(contract, match, std::nullopt, &declaration);
    record_read(contract, match, "score", &declaration);

    CHECK(contract.read_fields.at(match) == all_fields());
}

TEST_CASE("Contract field access: an undeclared field name falls back to all fields", "[semantic][handler-contracts]") {
    const auto match       = make_symbol_id(SymbolKind::Trait, "test", "Match");
    const auto declaration = match_trait_declaration();
    HandlerContract contract;
    record_read(contract, match, "missing", &declaration);
    CHECK(contract.read_fields.at(match) == all_fields());

    HandlerContract undeclared_trait;
    record_read(undeclared_trait, match, "over", nullptr);
    CHECK(undeclared_trait.read_fields.at(match) == all_fields());
}

TEST_CASE("Contract field access: a write also records the read of the same field", "[semantic][handler-contracts]") {
    const auto match       = make_symbol_id(SymbolKind::Trait, "test", "Match");
    const auto declaration = match_trait_declaration();
    HandlerContract contract;
    record_read(contract, match, "score", &declaration);
    record_write(contract, match, "over", &declaration, FieldWriteKind{});

    CHECK(contract.writes.contains(match));
    CHECK(contract.reads.contains(match));
    CHECK(contract.write_fields.at(match) == fields_of({"over"}));
    CHECK(contract.read_fields.at(match) == fields_of({"over", "score"}));
}

TEST_CASE("Contract field access: trait sets and field tables keep the same keys", "[semantic][handler-contracts]") {
    const auto match       = make_symbol_id(SymbolKind::Trait, "test", "Match");
    const auto health      = make_symbol_id(SymbolKind::Trait, "test", "Health");
    const auto declaration = match_trait_declaration();
    HandlerContract contract;
    record_read(contract, health, std::nullopt, nullptr);
    record_write(contract, match, "score", &declaration, FieldWriteKind{});

    CHECK(contract.read_fields.size() == contract.reads.size());
    CHECK(contract.write_fields.size() == contract.writes.size());
    for (const auto& trait : contract.reads) {
        CHECK(contract.read_fields.contains(trait));
    }
    for (const auto& trait : contract.writes) {
        CHECK(contract.write_fields.contains(trait));
    }
}

static const std::string& field_traits() {
    static const std::string value =
        "struct Point:\n"
        "    x: float\n"
        "    y: float\n"
        "trait Match:\n"
        "    var over: bool = false\n"
        "    var score: int = 0\n"
        "trait Body:\n"
        "    var position: Point\n"
        "    var speed: float = 0.0\n"
        "trait Player:\n"
        "    var game_over: bool = false\n"
        "    var lives: int = 3\n"
        "trait Health:\n"
        "    var hp: int = 10\n"
        "    var armor: int = 0\n"
        "trait Target:\n"
        "    var other: entity_id\n"
        "event Hit:\n"
        "    other: entity_id\n";
    return value;
}

static InferredHandlerContract only_contract(const std::string& rules) {
    auto result = analyze(stdlib_events() + field_traits() + rules);
    REQUIRE(result.handler_contracts.size() == 1);
    return result.handler_contracts[0];
}

static SymbolId test_trait(const std::string& name) {
    return make_symbol_id(SymbolKind::Trait, "test", name);
}

TEST_CASE("Contract fields: an alias member read records that field", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule ReadOver:\n"
        "    each:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        let x = m.over\n");
    CHECK(contract.read_fields.at(test_trait("Match")) == fields_of({"over"}));
    CHECK(contract.write_fields.empty());
}

TEST_CASE("Contract fields: a nested alias member write stops at the trait field", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule Nudge:\n"
        "    each:\n"
        "        Body as b\n"
        "    on tick:\n"
        "        b.position.x = 1.0\n");
    CHECK(contract.write_fields.at(test_trait("Body")) == fields_of({"position"}));
    CHECK(contract.read_fields.at(test_trait("Body")) == fields_of({"position"}));
}

TEST_CASE("Contract fields: a bare field identifier records its field", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule Check:\n"
        "    each:\n"
        "        Player\n"
        "    on tick:\n"
        "        let x = game_over\n");
    CHECK(contract.read_fields.at(test_trait("Player")) == fields_of({"game_over"}));
}

TEST_CASE("Contract fields: a bare field assignment records its field", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule End:\n"
        "    each:\n"
        "        Player\n"
        "    on tick:\n"
        "        game_over = true\n");
    CHECK(contract.write_fields.at(test_trait("Player")) == fields_of({"game_over"}));
}

TEST_CASE("Contract fields: a trait-rooted assignment records the field after the trait",
          "[semantic][handler-contracts]") {
    const auto via_alias = only_contract(
        "rule End:\n"
        "    each:\n"
        "        Match\n"
        "    on tick:\n"
        "        Match.over = true\n");
    CHECK(via_alias.write_fields.at(test_trait("Match")) == fields_of({"over"}));

    const auto via_leading_trait = only_contract(
        "rule End:\n"
        "    each:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        foo.Match.over = true\n");
    CHECK(via_leading_trait.write_fields.at(test_trait("Match")) == fields_of({"over"}));
}

TEST_CASE("Contract fields: a trait_for_field fallback assignment covers all fields", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule End:\n"
        "    each:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        foo.over = true\n");
    CHECK(contract.write_fields.at(test_trait("Match")) == all_fields());
    CHECK(contract.read_fields.at(test_trait("Match")) == all_fields());
}

TEST_CASE("Contract fields: a bare alias used as a value reads all fields", "[semantic][handler-contracts]") {
    const auto as_argument = only_contract(
        "func pick(e: entity_id) int:\n"
        "    return 1\n"
        "rule Pass:\n"
        "    each:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        let y = pick(m)\n");
    CHECK(as_argument.read_fields.at(test_trait("Match")) == all_fields());
    CHECK(as_argument.write_fields.empty());

    const auto as_local = only_contract(
        "rule Keep:\n"
        "    each:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        let y = m\n");
    CHECK(as_local.read_fields.at(test_trait("Match")) == all_fields());
}

TEST_CASE("Contract fields: a trait-match arm reads all fields of its trait", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule Strike:\n"
        "    on Hit as h:\n"
        "        match h.other:\n"
        "            Health as victim =>\n"
        "                let x = victim.hp\n");
    CHECK(contract.read_fields.at(test_trait("Health")) == all_fields());
}

TEST_CASE("Contract fields: set stays a command with no field access", "[semantic][handler-contracts][deferred-set]") {
    const auto contract = only_contract(
        "rule Heal:\n"
        "    each:\n"
        "        Target as aim\n"
        "    on tick:\n"
        "        set Health on aim.other:\n"
        "            hp = 5\n");
    const auto health = test_trait("Health");
    const InferredHandlerCommand set_health{.kind     = HandlerCommandKind::Set,
                                            .target   = health,
                                            .patched  = fields_of({"hp"}),
                                            .entities = {EntityTarget{.kind = EntityTargetKind::Unknown}}};
    CHECK(std::ranges::find(contract.commands, set_health) != contract.commands.end());
    CHECK_FALSE(contract.reads.contains(health));
    CHECK_FALSE(contract.writes.contains(health));
    CHECK_FALSE(contract.read_fields.contains(health));
    CHECK_FALSE(contract.write_fields.contains(health));
    CHECK(contract.read_fields.at(test_trait("Target")) == fields_of({"other"}));
}

TEST_CASE("Contract fields: set records its patched fields", "[semantic][handler-contracts][deferred-set]") {
    const auto contract = only_contract(
        "rule Heal:\n"
        "    each:\n"
        "        Target as aim\n"
        "    on tick:\n"
        "        set Health on aim.other:\n"
        "            hp = 5\n");
    REQUIRE(contract.commands.size() == 1);
    CHECK(contract.commands[0].kind == HandlerCommandKind::Set);
    CHECK(contract.commands[0].patched == fields_of({"hp"}));
    CHECK_FALSE(contract.write_fields.contains(test_trait("Health")));

    const auto both = only_contract(
        "rule Heal:\n"
        "    each:\n"
        "        Target as aim\n"
        "    on tick:\n"
        "        set Health on aim.other:\n"
        "            hp = 5\n"
        "        set Health on aim.other:\n"
        "            armor = 1\n");
    REQUIRE(both.commands.size() == 1);
    CHECK(both.commands[0].patched == fields_of({"hp", "armor"}));
}

TEST_CASE("Contract fields: an extern set covers every field", "[semantic][handler-contracts][deferred-set]") {
    const auto program = analyze(stdlib_events() + field_traits() +
                                 "extern rule HostHeal:\n"
                                 "    on tick:\n"
                                 "        commands:\n"
                                 "            set Health\n"
                                 "            add Health\n");
    REQUIRE(program.execution_graph.handlers.size() == 1);
    const auto& commands = program.execution_graph.handlers[0].contract.commands;
    REQUIRE(commands.size() == 2);
    CHECK(commands[0].patched == all_fields());
    CHECK(commands[1].patched == fields_of({"hp", "armor"}));
}

static ImportedSymbols make_input_import() {
    ImportedSymbols syms;
    syms.module_name = "std.input";
    TypeInfo bool_type;
    bool_type.kind = TypeKind::Bool;
    for (const std::string name : {"pressed", "consume"}) {
        ResolvedFunc func;
        func.name           = name;
        func.module_name    = "std.input";
        func.is_pub         = true;
        func.is_extern      = true;
        func.effect_summary = std::unordered_set<std::string>{"input"};
        if (name == "pressed") {
            func.return_type = bool_type;
        }
        const auto symbol = make_symbol_id(SymbolKind::Func, "std.input", name);
        func.symbol_id    = symbol;
        func.canonical_id = make_canonical_id(symbol);
        syms.funcs[name]  = std::move(func);
    }
    return syms;
}

static InferredHandlerContract only_effect_contract(const std::string& source) {
    ModuleImports imports;
    imports.modules["input"] = make_input_import();
    auto result              = analyze_with_imports(stdlib_events() + source, imports);
    REQUIRE(result.handler_contracts.size() == 1);
    return result.handler_contracts[0];
}

TEST_CASE("Effect access: an input read observes", "[semantic][handler-contracts][effects]") {
    const auto contract = only_effect_contract(
        "rule Look:\n"
        "    on tick:\n"
        "        if input.pressed():\n"
        "            let x = 1\n");
    CHECK(contract.effects.contains("input"));
    CHECK(contract.effect_acts.empty());
}

TEST_CASE("Effect access: consume acts", "[semantic][handler-contracts][effects]") {
    const auto contract = only_effect_contract(
        "rule Take:\n"
        "    on tick:\n"
        "        if input.pressed():\n"
        "            input.consume()\n");
    CHECK(contract.effects.contains("input"));
    CHECK(contract.effect_acts.contains("input"));
}

TEST_CASE("Effect access: an unknown extern function acts on external", "[semantic][handler-contracts][effects]") {
    const auto contract = only_effect_contract(
        "extern func poke()\n"
        "rule Poke:\n"
        "    on tick:\n"
        "        poke()\n");
    CHECK(contract.effects.contains("external"));
    CHECK(contract.effect_acts.contains("external"));
}

TEST_CASE("Effect access: extern rule effects act", "[semantic][handler-contracts][effects]") {
    const auto program = analyze(stdlib_events() +
                                 "extern rule Draw:\n"
                                 "    on tick:\n"
                                 "        effects:\n"
                                 "            graphics\n");
    REQUIRE(program.execution_graph.handlers.size() == 1);
    const auto& contract = program.execution_graph.handlers[0].contract;
    CHECK(contract.effects.contains("graphics"));
    CHECK(contract.effect_acts.contains("graphics"));
}

TEST_CASE("Effect access: acting stdlib functions are listed by identity", "[semantic][handler-contracts][effects]") {
    CHECK(stdlib_function_acts(make_symbol_id(SymbolKind::Func, "std.input", "consume")));
    CHECK(stdlib_function_acts(make_symbol_id(SymbolKind::Func, "std.input", "set_cursor_captured")));
    CHECK(stdlib_function_acts(make_symbol_id(SymbolKind::Func, "std.editor", "apply_camera_3d")));
    CHECK_FALSE(stdlib_function_acts(make_symbol_id(SymbolKind::Func, "std.input", "pressed")));
    CHECK_FALSE(stdlib_function_acts(make_symbol_id(SymbolKind::Func, "std.query", "all")));
    CHECK_FALSE(stdlib_function_acts(make_symbol_id(SymbolKind::Func, "game", "consume")));
}

// ── Write kinds (handler-contracts) ─────────────────────────────────────────

static ImportedSymbols make_math_import() {
    ImportedSymbols syms;
    syms.module_name = "std.math";
    TypeInfo float_type;
    float_type.kind = TypeKind::Float;
    for (const std::string name : {"max", "min"}) {
        ResolvedFunc func;
        func.name           = name;
        func.module_name    = "std.math";
        func.is_pub         = true;
        func.effect_summary = std::unordered_set<std::string>{};
        func.params         = {ResolvedParam{.name = "a", .type = float_type},
                               ResolvedParam{.name = "b", .type = float_type}};
        func.return_type    = float_type;
        const auto symbol   = make_symbol_id(SymbolKind::Func, "std.math", name);
        func.symbol_id      = symbol;
        func.canonical_id   = make_canonical_id(symbol);
        syms.funcs[name]    = std::move(func);
    }
    return syms;
}

static const std::string& write_kind_traits() {
    static const std::string value =
        "trait Tally:\n"
        "    var total: float = 0.0\n"
        "    var count: int = 0\n"
        "    var best: float = 0.0\n"
        "    var flag: bool = false\n"
        "    var label: string = \"\"\n"
        "trait Mover:\n"
        "    var pos: vec2\n"
        "    var speed: float = 1.0\n"
        "trait Actor:\n"
        "    var grounded: bool = false\n"
        "trait Surface:\n"
        "    var top: float = 0.0\n"
        "entity Game:\n"
        "    Tally\n";
    return value;
}

static InferredHandlerContract only_write_kind_contract(const std::string& rules) {
    ModuleImports imports;
    imports.modules["math"] = make_math_import();
    auto result             = analyze_with_imports(stdlib_events() + write_kind_traits() + rules, imports);
    REQUIRE(result.handler_contracts.size() == 1);
    return result.handler_contracts[0];
}

static FieldWriteKind kind_of(const std::string& rule_body, const std::string& trait, const std::string& field) {
    return write_kind(only_write_kind_contract(rule_body), test_trait(trait), field);
}

static std::string tally_rule(const std::string& body) {
    return "rule Update:\n"
           "    each:\n"
           "        Tally as t\n"
           "    on tick:\n" +
           body;
}

static const FieldWriteKind kAccum{.kind = WriteKind::Accum};
static const FieldWriteKind kAssign{.kind = WriteKind::Assign};
static const FieldWriteKind kGeneral{.kind = WriteKind::General};

static FieldWriteKind join(JoinOp op) {
    return FieldWriteKind{.kind = WriteKind::Join, .op = op};
}

TEST_CASE("Write kinds: compound addition and subtraction are Accum", "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of(tally_rule("        t.total += 1.0\n"), "Tally", "total") == kAccum);
    CHECK(kind_of(tally_rule("        t.count -= 5\n"), "Tally", "count") == kAccum);
    CHECK(kind_of(tally_rule("        t.total += 1.0\n        t.total -= 0.5\n"), "Tally", "total") == kAccum);
}

TEST_CASE("Write kinds: a vector component accumulation is Accum on the field", "[semantic][handler-contracts][write-kinds]") {
    const std::string rule = "rule Push:\n"
                      "    each:\n"
                      "        Mover as m\n"
                      "    on tick:\n"
                      "        m.pos.x += 1.0\n";
    CHECK(kind_of(rule, "Mover", "pos") == kAccum);
}

TEST_CASE("Write kinds: accumulating into a string is General", "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of(tally_rule("        t.label += \"x\"\n"), "Tally", "label") == kGeneral);
}

TEST_CASE("Write kinds: every Join form and its commuted form", "[semantic][handler-contracts][write-kinds]") {
    const std::string local = "        let v = 2.0\n        let hit = true\n";
    CHECK(kind_of(tally_rule(local + "        t.best = math.max(t.best, v)\n"), "Tally", "best") == join(JoinOp::Max));
    CHECK(kind_of(tally_rule(local + "        t.best = math.max(v, t.best)\n"), "Tally", "best") == join(JoinOp::Max));
    CHECK(kind_of(tally_rule(local + "        t.best = math.min(t.best, v)\n"), "Tally", "best") == join(JoinOp::Min));
    CHECK(kind_of(tally_rule(local + "        t.best = math.min(v, t.best)\n"), "Tally", "best") == join(JoinOp::Min));
    CHECK(kind_of(tally_rule(local + "        t.flag = t.flag or hit\n"), "Tally", "flag") == join(JoinOp::Or));
    CHECK(kind_of(tally_rule(local + "        t.flag = hit or t.flag\n"), "Tally", "flag") == join(JoinOp::Or));
    CHECK(kind_of(tally_rule(local + "        t.flag = t.flag and hit\n"), "Tally", "flag") == join(JoinOp::And));
    CHECK(kind_of(tally_rule(local + "        t.flag = hit and t.flag\n"), "Tally", "flag") == join(JoinOp::And));
}

TEST_CASE("Write kinds: constant boolean assignments are Joins", "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of(tally_rule("        t.flag = true\n"), "Tally", "flag") == join(JoinOp::Or));
    CHECK(kind_of(tally_rule("        t.flag = false\n"), "Tally", "flag") == join(JoinOp::And));
    const std::string bare = "rule Land:\n"
                      "    each:\n"
                      "        Actor\n"
                      "    on tick:\n"
                      "        grounded = true\n";
    CHECK(kind_of(bare, "Actor", "grounded") == join(JoinOp::Or));
}

TEST_CASE("Write kinds: a plain assignment is Assign", "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of(tally_rule("        t.total = 3.0\n"), "Tally", "total") == kAssign);
    CHECK(kind_of(tally_rule("        t.total = 3.0\n        t.total = 4.0\n"), "Tally", "total") == kAssign);
}

TEST_CASE("Write kinds: scaling and division are General", "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of(tally_rule("        t.total *= 0.5\n"), "Tally", "total") == kGeneral);
    CHECK(kind_of(tally_rule("        t.total /= 2.0\n"), "Tally", "total") == kGeneral);
}

TEST_CASE("Write kinds: reading the field anywhere else makes the write General",
          "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of(tally_rule("        t.count -= 5\n        if t.count < 0:\n            let x = 1\n"), "Tally", "count") ==
          kGeneral);
    CHECK(kind_of(tally_rule("        let before = t.total\n        t.total = 1.0\n"), "Tally", "total") == kGeneral);
    CHECK(kind_of("rule Update:\n"
                  "    each:\n"
                  "        Tally as t\n"
                  "    where:\n"
                  "        t.count > 0\n"
                  "    on tick:\n"
                  "        t.count += 1\n",
                  "Tally",
                  "count") == kGeneral);
    CHECK(kind_of("rule Update:\n"
                  "    each:\n"
                  "        Tally as t\n"
                  "    order by:\n"
                  "        t.total desc\n"
                  "    on tick:\n"
                  "        t.total += 1.0\n",
                  "Tally",
                  "total") == kGeneral);
    CHECK(kind_of("rule Update:\n"
                  "    each:\n"
                  "        Mover\n"
                  "    limit: Game[Tally].count\n"
                  "    on tick:\n"
                  "        Game[Tally].count += 1\n",
                  "Tally",
                  "count") == kGeneral);
    CHECK(kind_of("rule Update:\n"
                  "    when:\n"
                  "        Game[Tally].total > 0.0\n"
                  "    on tick:\n"
                  "        Game[Tally].total += 1.0\n",
                  "Tally",
                  "total") == kGeneral);
}

TEST_CASE("Write kinds: mixed kinds in one handler are General", "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of(tally_rule("        let reset = true\n"
                             "        if reset:\n"
                             "            t.total = 0.0\n"
                             "        else:\n"
                             "            t.total += 1.0\n"),
                  "Tally",
                  "total") == kGeneral);
    CHECK(kind_of(tally_rule("        t.flag = true\n        t.flag = false\n"), "Tally", "flag") == kGeneral);
}

TEST_CASE("Write kinds: a delta that reads its own field is General", "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of(tally_rule("        t.total += t.total\n"), "Tally", "total") == kGeneral);
    CHECK(kind_of(tally_rule("        t.best = math.max(t.best, t.best + 1.0)\n"), "Tally", "best") == kGeneral);
    CHECK(kind_of(tally_rule("        t.flag = t.flag or (t.flag and true)\n"), "Tally", "flag") == kGeneral);
}

TEST_CASE("Write kinds: another field in the delta keeps the kind", "[semantic][handler-contracts][write-kinds]") {
    const auto contract = only_write_kind_contract(tally_rule("        t.total += t.best\n"));
    CHECK(write_kind(contract, test_trait("Tally"), "total") == kAccum);
    CHECK(contract.read_fields.at(test_trait("Tally")).fields.contains("best"));
}

TEST_CASE("Write kinds: a write covering every field is General", "[semantic][handler-contracts][write-kinds]") {
    const auto contract = only_write_kind_contract("rule End:\n"
                                                   "    each:\n"
                                                   "        Tally as t\n"
                                                   "    on tick:\n"
                                                   "        foo.flag = true\n");
    CHECK(contract.write_fields.at(test_trait("Tally")) == all_fields());
    CHECK(write_kind(contract, test_trait("Tally"), "flag") == kGeneral);
    CHECK(write_kind(contract, test_trait("Tally"), "total") == kGeneral);
}

TEST_CASE("Write kinds: extern rule writes are General", "[semantic][handler-contracts][write-kinds]") {
    const auto program = analyze(stdlib_events() + write_kind_traits() +
                                 "extern rule Physics:\n"
                                 "    on tick:\n"
                                 "        writes:\n"
                                 "            Tally\n");
    REQUIRE(program.execution_graph.handlers.size() == 1);
    const auto& contract = program.execution_graph.handlers[0].contract;
    CHECK(write_kind(contract, test_trait("Tally"), "total") == kGeneral);
    CHECK(write_kind(contract, test_trait("Tally"), "flag") == kGeneral);
}

TEST_CASE("Write kinds: a user function named max is not a Join", "[semantic][handler-contracts][write-kinds]") {
    CHECK(kind_of("func max(a: float, b: float) float:\n"
                  "    return a\n" +
                      tally_rule("        let v = 2.0\n        t.best = max(t.best, v)\n"),
                  "Tally",
                  "best") == kGeneral);
}

TEST_CASE("Write kinds: named-entity writes are classified", "[semantic][handler-contracts][write-kinds]") {
    const std::string rule = "rule Count:\n"
                      "    each:\n"
                      "        Mover\n"
                      "    on tick:\n"
                      "        Game[Tally].total += 0.1\n"
                      "        Game[Tally].flag = true\n"
                      "        Game[Tally].best = math.max(Game[Tally].best, 1.0)\n";
    const auto contract = only_write_kind_contract(rule);
    CHECK(write_kind(contract, test_trait("Tally"), "total") == kAccum);
    CHECK(write_kind(contract, test_trait("Tally"), "flag") == join(JoinOp::Or));
    CHECK(write_kind(contract, test_trait("Tally"), "best") == join(JoinOp::Max));
}

TEST_CASE("Write kinds: pair-binding writes are classified", "[semantic][handler-contracts][write-kinds]") {
    const std::string rule = "rule Ground:\n"
                      "    pairs:\n"
                      "        actor:\n"
                      "            Actor\n"
                      "        surface:\n"
                      "            Surface\n"
                      "    limit: 1 per actor\n"
                      "    on tick:\n"
                      "        actor[Actor].grounded = true\n";
    CHECK(kind_of(rule, "Actor", "grounded") == join(JoinOp::Or));
}

static std::vector<std::optional<AccumulationTarget>> accumulation_targets(const std::string& rule) {
    ModuleImports imports;
    imports.modules["math"] = make_math_import();
    ErrorReporter errors;
    Lexer lexer("module test\n" + stdlib_events() + write_kind_traits() + rule, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto program = parser.parse_program();
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program, imports);
    REQUIRE_FALSE(errors.has_errors());
    std::vector<std::optional<AccumulationTarget>> targets;
    for (const auto& declaration : program.declarations) {
        const auto* node = std::get_if<RuleNode>(&declaration);
        if (node == nullptr) {
            continue;
        }
        for (const auto& handler : node->handlers) {
            for (const auto& statement : handler.body) {
                if (const auto* assign = std::get_if<VarAssign>(&statement->stmt)) {
                    targets.push_back(assign->accumulation);
                }
            }
        }
    }
    return targets;
}

TEST_CASE("Write kinds: an Accum statement names its target for the backend",
          "[semantic][handler-contracts][write-kinds]") {
    const auto own = accumulation_targets(tally_rule("        t.total += 1.0\n"));
    REQUIRE(own.size() == 1);
    REQUIRE(own[0].has_value());
    CHECK(own[0]->trait == test_trait("Tally"));
    CHECK(own[0]->field == "total");
    CHECK(own[0]->own_row);

    const auto named = accumulation_targets("rule Count:\n    on tick:\n        Game[Tally].total -= 1.0\n");
    REQUIRE(named.size() == 1);
    REQUIRE(named[0].has_value());
    CHECK_FALSE(named[0]->own_row);

    const auto observed = accumulation_targets(tally_rule("        t.total += 1.0\n        t.best = t.total\n"));
    REQUIRE(observed.size() == 2);
    CHECK_FALSE(observed[0].has_value());
    CHECK_FALSE(observed[1].has_value());
}

TEST_CASE("Write kinds: the read-tracking table is cleared after inference", "[semantic][handler-contracts][write-kinds]") {
    CHECK(only_write_kind_contract(tally_rule("        let x = t.total\n")).observed_reads.empty());
}

static const std::string& pair_field_traits() {
    static const std::string value =
        "trait Collider:\n"
        "    var radius: float = 1.0\n"
        "    var mask: int = 0\n"
        "trait Actor:\n"
        "    var grounded: bool = false\n"
        "    var height: float = 0.0\n"
        "    var slots: int = 1\n"
        "trait Surface:\n"
        "    var top: float = 0.0\n";
    return value;
}

static InferredHandlerContract only_pair_contract(const std::string& rules) {
    auto result = analyze(stdlib_events() + pair_field_traits() + rules);
    REQUIRE(result.handler_contracts.size() == 1);
    return result.handler_contracts[0];
}

TEST_CASE("Contract fields: a pair bound read records its field and keeps the bound read",
          "[semantic][handler-contracts][pair-relations]") {
    const auto contract = only_pair_contract(
        "rule Touch:\n"
        "    pairs:\n"
        "        body:\n"
        "            Collider\n"
        "        wall:\n"
        "            Surface\n"
        "    on tick:\n"
        "        if body[Collider].radius > 0.0:\n"
        "            let x = 1\n");
    const auto collider = test_trait("Collider");
    CHECK(contract.read_fields.at(collider) == fields_of({"radius"}));
    const BoundTraitAccess body_collider{.binding_index = 0, .trait = collider};
    CHECK(std::ranges::find(contract.bound_reads, body_collider) != contract.bound_reads.end());
}

TEST_CASE("Contract fields: a provably-limited pair write records its field",
          "[semantic][handler-contracts][pair-relations][rule-limit]") {
    const auto contract = only_pair_contract(
        "rule Ground:\n"
        "    pairs:\n"
        "        actor:\n"
        "            Actor\n"
        "        surface:\n"
        "            Surface\n"
        "    limit: 1 per actor\n"
        "    on tick:\n"
        "        actor[Actor].grounded = true\n");
    const auto actor = test_trait("Actor");
    CHECK(contract.write_fields.at(actor) == fields_of({"grounded"}));
    CHECK(contract.read_fields.at(actor) == fields_of({"grounded"}));
}

TEST_CASE("Contract fields: where and order by reads record fields", "[semantic][handler-contracts][where-clause]") {
    const auto pair = only_pair_contract(
        "rule Rank:\n"
        "    pairs:\n"
        "        a:\n"
        "            Collider\n"
        "        b:\n"
        "            Collider\n"
        "    order by:\n"
        "        b[Collider].mask desc\n"
        "    where:\n"
        "        a[Collider].radius > 0.0\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(pair.read_fields.at(test_trait("Collider")) == fields_of({"mask", "radius"}));

    const auto unary = only_pair_contract(
        "rule Rank:\n"
        "    each:\n"
        "        Collider as c\n"
        "    order by:\n"
        "        c.mask desc\n"
        "    where:\n"
        "        c.radius > 0.0\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(unary.read_fields.at(test_trait("Collider")) == fields_of({"mask", "radius"}));
}

TEST_CASE("Contract fields: where on a limited unary rule records its reads",
          "[semantic][handler-contracts][where-clause][rule-limit]") {
    const auto contract = only_contract(
        "rule Pick:\n"
        "    each:\n"
        "        Health as h\n"
        "    where:\n"
        "        h.hp < 20\n"
        "    limit: 3\n"
        "    on tick:\n"
        "        let x = 1\n");
    const auto health = test_trait("Health");
    CHECK(contract.reads.contains(health));
    CHECK(contract.read_fields.at(health) == fields_of({"hp"}));
}

TEST_CASE("Contract fields: where on a limited pair rule records its bound reads",
          "[semantic][handler-contracts][where-clause][rule-limit][pair-relations]") {
    const auto contract = only_pair_contract(
        "rule Land:\n"
        "    pairs:\n"
        "        actor:\n"
        "            Actor\n"
        "        surface:\n"
        "            Surface\n"
        "    where:\n"
        "        surface[Surface].top <= actor[Actor].height\n"
        "    limit: 1 per actor\n"
        "    on tick:\n"
        "        let x = 1\n");
    const auto actor   = test_trait("Actor");
    const auto surface = test_trait("Surface");
    const BoundTraitAccess actor_read{.binding_index = 0, .trait = actor};
    const BoundTraitAccess surface_read{.binding_index = 1, .trait = surface};
    CHECK(std::ranges::find(contract.bound_reads, actor_read) != contract.bound_reads.end());
    CHECK(std::ranges::find(contract.bound_reads, surface_read) != contract.bound_reads.end());
    CHECK(contract.read_fields.at(actor) == fields_of({"height"}));
    CHECK(contract.read_fields.at(surface) == fields_of({"top"}));
}

TEST_CASE("Contract fields: a per-binding limit count records its bound read",
          "[semantic][handler-contracts][rule-limit][pair-relations]") {
    const auto contract = only_pair_contract(
        "rule Land:\n"
        "    pairs:\n"
        "        actor:\n"
        "            Actor\n"
        "        surface:\n"
        "            Surface\n"
        "    limit: actor[Actor].slots per actor\n"
        "    on tick:\n"
        "        let x = 1\n");
    const auto actor = test_trait("Actor");
    const BoundTraitAccess actor_read{.binding_index = 0, .trait = actor};
    CHECK(std::ranges::find(contract.bound_reads, actor_read) != contract.bound_reads.end());
    CHECK(contract.read_fields.at(actor) == fields_of({"slots"}));
}

TEST_CASE("Contract fields: a constant limit count adds no read", "[semantic][handler-contracts][rule-limit]") {
    const auto contract = only_pair_contract(
        "rule Pick:\n"
        "    each:\n"
        "        Surface as s\n"
        "    limit: 3\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(contract.reads.empty());
    CHECK(contract.read_fields.empty());
}

TEST_CASE("Contract fields: extern rule declarations cover all fields", "[semantic][handler-contracts][extern-rule]") {
    auto result        = analyze(stdlib_events() + field_traits() +
                                 "extern rule Scoreboard:\n"
                                 "    each:\n"
                                 "        Match as m\n"
                                 "    order by:\n"
                                 "        m.score desc\n"
                                 "    on tick:\n"
                                 "        reads:\n"
                                 "            Player\n"
                                 "        writes:\n"
                                 "            Health\n");
    const auto handler = std::ranges::find_if(result.execution_graph.handlers, [](const HandlerNode& node) {
        return node.identity.rule.local_name == "Scoreboard";
    });
    REQUIRE(handler != result.execution_graph.handlers.end());
    const auto& contract = handler->contract;
    CHECK(contract.read_fields.at(test_trait("Player")) == all_fields());
    CHECK(contract.write_fields.at(test_trait("Health")) == all_fields());
    CHECK(contract.read_fields.at(test_trait("Health")) == all_fields());
    CHECK(contract.read_fields.at(test_trait("Match")) == fields_of({"score"}));
}
// ── Named entity access (dsl-named-entity-access, dsl-when-clause) ─────────

namespace {

const std::string& named_header() {
    static const std::string value =
        "const:\n"
        "    LIMIT = 3\n"
        "trait Match:\n"
        "    var over: bool = false\n"
        "    var score: int = 0\n"
        "trait Pos:\n"
        "    var value: vec2 = vec2(0.0, 0.0)\n"
        "trait Enemy:\n"
        "    var dying: bool = false\n"
        "    var aim: entity_id\n"
        "trait Rival:\n"
        "    var rival: entity_id\n"
        "trait Tag:\n"
        "    var id: int = 0\n"
        "trait Label:\n"
        "    var visible: bool = true\n"
        "trait Player\n"
        "event Hit:\n"
        "    source: entity_id\n"
        "func half(value: int) int:\n"
        "    return value / 2\n"
        "template Grunt:\n"
        "    Enemy\n"
        "template Base:\n"
        "    Match\n"
        "    Pos\n"
        "entity Game from Base:\n"
        "    Match\n"
        "entity Hud:\n"
        "    Label\n"
        "entity Boss:\n"
        "    Rival:\n"
        "        rival = Nemesis\n"
        "entity Nemesis:\n"
        "    Rival:\n"
        "        rival = Boss\n";
    return value;
}

std::vector<std::string> named_messages(const std::string& source) {
    return analyze_messages(named_header() + source);
}

// Parses and analyzes, keeping the AST so tests can inspect annotations.
struct AnalyzedProgram {
    ProgramNode ast;
    std::vector<std::string> messages;
};

AnalyzedProgram analyze_named_program(const std::string& source) {
    const std::string src = "module test\n" + stdlib_events() + named_header() + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    AnalyzedProgram result{.ast = parser.parse_program(), .messages = {}};
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(result.ast);
    for (const auto& diagnostic : errors.diagnostics()) {
        result.messages.push_back(diagnostic.message);
    }
    return result;
}

const RuleNode& rule_named(const ProgramNode& program, const std::string& name) {
    for (const auto& decl : program.declarations) {
        if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name == name) {
            return *rule;
        }
    }
    FAIL("no rule " << name);
    std::unreachable();
}

SymbolId test_entity(const std::string& name) {
    return make_symbol_id(SymbolKind::Entity, "test", name);
}

}  // namespace

TEST_CASE("Semantic: an entity name is an entity_id value in handler bodies", "[semantic][named-entity]") {
    const auto messages = named_messages("rule R:\n"
                                         "    each:\n"
                                         "        Enemy as enemy\n"
                                         "    on tick:\n"
                                         "        let aim_at: entity_id = Game\n"
                                         "        let same = self == Boss\n"
                                         "        set Label on Hud:\n"
                                         "            visible = false\n"
                                         "        emit Hit to Boss:\n"
                                         "            source = Game\n"
                                         "        enemy.aim = Nemesis\n");
    INFO((messages.empty() ? "" : messages.front()));
    CHECK(messages.empty());
}

TEST_CASE("Semantic: an entity name is typed entity_id", "[semantic][named-entity]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let wrong: int = Game\n"),
                      "is declared as 'int' but initialized with 'entity_id'"));
}

TEST_CASE("Semantic: an entity name value is annotated with its canonical identity", "[semantic][named-entity]") {
    const auto analyzed = analyze_named_program("rule R:\n"
                                                "    on tick:\n"
                                                "        let aim_at = Game\n");
    REQUIRE(analyzed.messages.empty());
    const auto& body  = rule_named(analyzed.ast, "R").handlers[0].body;
    const auto& let   = std::get<LetStmt>(body[0]->stmt);
    const auto& ident = std::get<IdentExpr>(let.value->expr);
    REQUIRE(ident.resolved_entity_id.has_value());
    CHECK(*ident.resolved_entity_id == test_entity("Game"));
}

TEST_CASE("Semantic: an entity name is a value in rule clauses", "[semantic][named-entity]") {
    const auto messages = named_messages("rule R:\n"
                                         "    each:\n"
                                         "        Enemy as enemy\n"
                                         "    where:\n"
                                         "        enemy.aim == Boss\n"
                                         "    when:\n"
                                         "        Game != Boss\n"
                                         "    on tick:\n"
                                         "        enemy.dying = true\n");
    INFO((messages.empty() ? "" : messages.front()));
    CHECK(messages.empty());
}

TEST_CASE("Semantic: an entity name is an archetype override value", "[semantic][named-entity]") {
    CHECK(named_messages("").empty());
    CHECK(has_message(named_messages("entity Wrong:\n"
                                     "    Tag:\n"
                                     "        id = Boss\n"),
                      "type mismatch for field 'id'"));
}

TEST_CASE("Semantic: a template name is not an entity_id value", "[semantic][named-entity]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let aim_at = Grunt\n"),
                      "template 'Grunt' is not an entity_id value"));
}

TEST_CASE("Semantic: a local shadows an entity name", "[semantic][named-entity]") {
    const auto messages = named_messages("rule R:\n"
                                         "    on tick:\n"
                                         "        let Game = 5\n"
                                         "        let count: int = Game\n");
    INFO((messages.empty() ? "" : messages.front()));
    CHECK(messages.empty());
}

TEST_CASE("Semantic: named field reads are typed by the field", "[semantic][named-entity]") {
    const auto messages = named_messages("rule R:\n"
                                         "    on tick:\n"
                                         "        let over: bool = Game[Match].over\n"
                                         "        let x: float = Game[Pos].value.x\n"
                                         "        let v: vec2 = Game[Pos].value\n"
                                         "        if Game[Match].score > half(LIMIT):\n"
                                         "            let y = 1\n");
    INFO((messages.empty() ? "" : messages.front()));
    CHECK(messages.empty());
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let wrong: int = Game[Match].over\n"),
                      "is declared as 'int' but initialized with 'bool'"));
}

TEST_CASE("Semantic: named writes and compound writes are accepted in unary and pair handlers",
          "[semantic][named-entity]") {
    const auto messages = named_messages("rule R:\n"
                                         "    each:\n"
                                         "        Enemy as enemy\n"
                                         "    on tick:\n"
                                         "        Game[Match].over = true\n"
                                         "        Game[Match].score += 1\n"
                                         "        Game[Pos].value.x -= 1.0\n"
                                         "rule P:\n"
                                         "    pairs:\n"
                                         "        a:\n"
                                         "            Enemy\n"
                                         "        b:\n"
                                         "            Enemy\n"
                                         "    on tick:\n"
                                         "        Game[Match].score += 1\n");
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].starts_with("pair rule 'P' is not accelerated"));
}

TEST_CASE("Semantic: named access to a trait the archetype does not declare is rejected",
          "[semantic][named-entity]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let id = Game[Tag].id\n"),
                      "entity 'Game' does not declare trait 'Tag'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        Hud[Match].over = true\n"),
                      "entity 'Hud' does not declare trait 'Match'"));
}

TEST_CASE("Semantic: named access to an unknown field is rejected", "[semantic][named-entity]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let nope = Game[Match].nope\n"),
                      "trait 'Match' has no field 'nope'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let whole = Game[Match]\n"),
                      "named field access needs a field after 'Game[Match]'"));
}

TEST_CASE("Semantic: named field access is rejected outside rules", "[semantic][named-entity]") {
    CHECK(has_message(named_messages("func peek() bool:\n"
                                     "    return Game[Match].over\n"),
                      "named field access is only allowed in rules"));
    CHECK(has_message(named_messages("const:\n"
                                     "    START = Game[Match].score\n"),
                      "named field access is only allowed in rules"));
    CHECK(has_message(named_messages("entity Copy:\n"
                                     "    Tag:\n"
                                     "        id = Game[Match].score\n"),
                      "named field access is only allowed in rules"));
}

TEST_CASE("Semantic: when: accepts named reads, constants, and pure calls", "[semantic][when-clause]") {
    const auto messages = named_messages("rule Idle:\n"
                                         "    when:\n"
                                         "        not Game[Match].over\n"
                                         "        Game[Match].score > half(LIMIT)\n"
                                         "    on tick:\n"
                                         "        Game[Match].score += 1\n"
                                         "rule Hits:\n"
                                         "    each:\n"
                                         "        Enemy as enemy\n"
                                         "    when:\n"
                                         "        not Game[Match].over\n"
                                         "    on Hit:\n"
                                         "        enemy.dying = true\n");
    INFO((messages.empty() ? "" : messages.front()));
    CHECK(messages.empty());
}

TEST_CASE("Semantic: a non-bool when: predicate is rejected", "[semantic][when-clause]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    when:\n"
                                     "        Game[Match].score\n"
                                     "    on tick:\n"
                                     "        let y = 1\n"),
                      "when: predicate must be of type 'bool'"));
}

TEST_CASE("Semantic: when: cannot read bindings, self, or event payloads", "[semantic][when-clause]") {
    const auto binding = named_messages("rule R:\n"
                                        "    each:\n"
                                        "        Enemy as enemy\n"
                                        "    when:\n"
                                        "        enemy.dying\n"
                                        "    on tick:\n"
                                        "        enemy.dying = false\n");
    CHECK(has_message(binding, "when: cannot read 'enemy'"));
    CHECK(has_message(binding, "use where:"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    each:\n"
                                     "        Enemy\n"
                                     "    when:\n"
                                     "        dying\n"
                                     "    on tick:\n"
                                     "        let y = 1\n"),
                      "when: cannot read 'dying'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    each:\n"
                                     "        Enemy\n"
                                     "    when:\n"
                                     "        self != Game\n"
                                     "    on tick:\n"
                                     "        let y = 1\n"),
                      "when: cannot read 'self'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    when:\n"
                                     "        tick.dt > 0.0\n"
                                     "    on tick:\n"
                                     "        let y = 1\n"),
                      "when: cannot read 'tick'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    each:\n"
                                     "        Enemy\n"
                                     "    when:\n"
                                     "        hit.source == Game\n"
                                     "    on Hit as hit:\n"
                                     "        let y = 1\n"),
                      "when: cannot read 'hit'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    pairs:\n"
                                     "        a:\n"
                                     "            Enemy\n"
                                     "        b:\n"
                                     "            Enemy\n"
                                     "    when:\n"
                                     "        a != b\n"
                                     "    on tick:\n"
                                     "        let y = 1\n"),
                      "when: cannot read 'a'"));
}

TEST_CASE("Semantic: when: predicates must be pure", "[semantic][when-clause]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    when:\n"
                                     "        query.first[Player]() == Game\n"
                                     "    on tick:\n"
                                     "        let y = 1\n"),
                      "when: predicates must be pure"));
}
// ── Named entity access in handler contracts (handler-contracts) ──────────

namespace {

const HandlerContract& named_contract(const DecoratedProgram& program, const std::string& rule) {
    const auto handler = std::ranges::find_if(program.execution_graph.handlers, [&](const HandlerNode& node) {
        return node.identity.rule.local_name == rule;
    });
    REQUIRE(handler != program.execution_graph.handlers.end());
    return handler->contract;
}

DecoratedProgram analyze_named(const std::string& rules) {
    return analyze(stdlib_events() + named_header() + rules);
}

std::vector<const ScheduleEdge*> data_edges_between(const DecoratedProgram& program,
                                                    const std::string& first,
                                                    const std::string& second) {
    std::vector<const ScheduleEdge*> edges;
    for (const auto& edge : program.execution_graph.schedule_edges) {
        const auto& before = edge.before.rule.local_name;
        const auto& after  = edge.after.rule.local_name;
        if (edge.kind == ScheduleEdgeKind::DataConflict &&
            ((before == first && after == second) || (before == second && after == first))) {
            edges.push_back(&edge);
        }
    }
    return edges;
}

const NamedRequirement& game_match() {
    static const NamedRequirement value{.entity = make_symbol_id(SymbolKind::Entity, "test", "Game"),
                                        .trait  = make_symbol_id(SymbolKind::Trait, "test", "Match")};
    return value;
}

}  // namespace

TEST_CASE("Contract: a named read in when: is recorded with entity, field, and requirement",
          "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule R:\n"
                                         "    each:\n"
                                         "        Enemy as enemy\n"
                                         "    when:\n"
                                         "        not Game[Match].over\n"
                                         "    on tick:\n"
                                         "        enemy.dying = true\n");
    const auto& contract = named_contract(program, "R");
    const auto game      = test_entity("Game");
    const auto match     = test_trait("Match");
    REQUIRE(contract.named_reads.contains(game));
    CHECK(contract.named_reads.at(game).at(match) == fields_of({"over"}));
    CHECK(contract.read_fields.at(match) == fields_of({"over"}));
    CHECK(contract.named_requirements == std::vector<NamedRequirement>{game_match()});
    CHECK(contract.named_writes.empty());
    CHECK(contract.splittable_per_entity());
}

TEST_CASE("Contract: named reads and writes in a body record fields and one requirement",
          "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule R:\n"
                                         "    on tick:\n"
                                         "        if Game[Match].over:\n"
                                         "            Game[Match].score += 1\n"
                                         "        let x = Game[Pos].value.x\n");
    const auto& contract = named_contract(program, "R");
    const auto game      = test_entity("Game");
    CHECK(contract.named_reads.at(game).at(test_trait("Match")) == fields_of({"over", "score"}));
    CHECK(contract.named_writes.at(game).at(test_trait("Match")) == fields_of({"score"}));
    CHECK(contract.named_reads.at(game).at(test_trait("Pos")) == fields_of({"value"}));
    CHECK(contract.write_fields.at(test_trait("Match")) == fields_of({"score"}));
    const std::vector<NamedRequirement> expected{game_match(),
                                                 NamedRequirement{.entity = game, .trait = test_trait("Pos")}};
    CHECK(contract.named_requirements == expected);
}

TEST_CASE("Contract: a named write orders against a filter reader of the same field",
          "[semantic][handler-contracts][named-entity]") {
    const auto program = analyze_named("rule End:\n"
                                       "    on tick:\n"
                                       "        Game[Match].over = true\n"
                                       "rule Watch:\n"
                                       "    each:\n"
                                       "        Match as m\n"
                                       "    on tick:\n"
                                       "        if m.over:\n"
                                       "            let y = 1\n");
    const auto edges = data_edges_between(program, "End", "Watch");
    REQUIRE(edges.size() == 1);
    REQUIRE(edges[0]->field_provenance.size() == 1);
    CHECK(edges[0]->field_provenance[0].trait == test_trait("Match"));
    CHECK(edges[0]->field_provenance[0].access == fields_of({"over"}));
}

TEST_CASE("Contract: disjoint named fields do not conflict", "[semantic][handler-contracts][named-entity]") {
    const auto program = analyze_named("rule Score:\n"
                                       "    on tick:\n"
                                       "        Game[Match].score += 1\n"
                                       "rule Watch:\n"
                                       "    on tick:\n"
                                       "        if Game[Match].over:\n"
                                       "            let y = 1\n");
    CHECK(data_edges_between(program, "Score", "Watch").empty());
}

TEST_CASE("Contract: a named write marks a filter handler unsplittable",
          "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule Count:\n"
                                         "    each:\n"
                                         "        Enemy\n"
                                         "    on tick:\n"
                                         "        Game[Match].score += 1\n");
    const auto& contract = named_contract(program, "Count");
    CHECK_FALSE(contract.splittable_per_entity());
    CHECK(contract.named_requirements == std::vector<NamedRequirement>{game_match()});
}

TEST_CASE("Contract: an entity name used as a value adds no named access",
          "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule Hide:\n"
                                         "    on tick:\n"
                                         "        set Label on Hud:\n"
                                         "            visible = false\n"
                                         "        emit Hit to Boss:\n"
                                         "            source = Game\n");
    const auto& contract = named_contract(program, "Hide");
    CHECK(contract.named_reads.empty());
    CHECK(contract.named_writes.empty());
    CHECK(contract.named_requirements.empty());
    CHECK(std::ranges::any_of(contract.commands, [](const InferredHandlerCommand& command) {
        return command.kind == HandlerCommandKind::Set;
    }));
}

TEST_CASE("Contract: a named read in where: on a limited rule records its requirement",
          "[semantic][handler-contracts][named-entity][rule-limit]") {
    const auto unary = analyze_named("rule Pick:\n"
                                     "    each:\n"
                                     "        Enemy as enemy\n"
                                     "    where:\n"
                                     "        enemy.dying == Game[Match].over\n"
                                     "    limit: 3\n"
                                     "    on tick:\n"
                                     "        let y = 1\n");
    const auto& unary_contract = named_contract(unary, "Pick");
    CHECK(unary_contract.named_requirements == std::vector<NamedRequirement>{game_match()});
    CHECK(unary_contract.named_reads.at(test_entity("Game")).at(test_trait("Match")) == fields_of({"over"}));

    const auto pair = analyze_named("rule Touch:\n"
                                    "    pairs:\n"
                                    "        a:\n"
                                    "            Enemy\n"
                                    "        b:\n"
                                    "            Enemy\n"
                                    "    where:\n"
                                    "        Game[Match].score > 0\n"
                                    "    limit: 1 per a\n"
                                    "    on tick:\n"
                                    "        let y = 1\n");
    CHECK(named_contract(pair, "Touch").named_requirements == std::vector<NamedRequirement>{game_match()});
}

TEST_CASE("Contract: named reads in a pair handler record a requirement", "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule Touch:\n"
                                         "    pairs:\n"
                                         "        a:\n"
                                         "            Enemy\n"
                                         "        b:\n"
                                         "            Enemy\n"
                                         "    when:\n"
                                         "        not Game[Match].over\n"
                                         "    on tick:\n"
                                         "        Game[Match].score += 1\n");
    const auto& contract = named_contract(program, "Touch");
    CHECK(contract.named_requirements == std::vector<NamedRequirement>{game_match()});
    CHECK(contract.named_writes.at(test_entity("Game")).at(test_trait("Match")) == fields_of({"score"}));
    CHECK_FALSE(contract.splittable_per_entity());
}

// ── Bracketed trait access (dsl-named-entity-access, dsl-pair-relations) ────

namespace {

std::vector<std::string> error_messages_of(const std::string& source) {
    const std::string src = "module test\n" + stdlib_events() + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program);
    std::vector<std::string> messages;
    for (const auto& diagnostic : errors.diagnostics()) {
        if (diagnostic.level == DiagnosticLevel::Error) {
            messages.push_back(diagnostic.message);
        }
    }
    return messages;
}

std::string describe_messages(const std::vector<std::string>& messages) {
    std::string text;
    for (const auto& message : messages) {
        text += message + "\n";
    }
    return text;
}

std::string named_errors_source(const std::string& rules) {
    return named_header() + rules;
}

std::string pair_rule(const std::string& body_traits,
                      const std::string& wall_traits,
                      const std::string& handler,
                      const std::string& clauses = "") {
    return pair_traits() +
           "rule R:\n"
           "    pairs:\n"
           "        body:\n" +
           body_traits + "        wall:\n" + wall_traits + clauses + "    on fixed_tick:\n" + handler;
}

const TraitAccessExpr* find_trait_access(const ExprNode& expr) {
    const ExprNode* cursor = &expr;
    while (const auto* member = std::get_if<MemberExpr>(&cursor->expr)) {
        cursor = member->object.get();
    }
    return std::get_if<TraitAccessExpr>(&cursor->expr);
}

}  // namespace

TEST_CASE("Semantic: bracketed named reads and writes type-check", "[semantic][named-entity][trait-access]") {
    const auto messages = error_messages_of(named_errors_source("rule R:\n"
                                                                "    on tick:\n"
                                                                "        let over: bool = Game[Match].over\n"
                                                                "        let x: float = Game[Pos].value.x\n"
                                                                "        Game[Match].score += 1\n"
                                                                "        Hud[test.Label].visible = false\n"));
    INFO(describe_messages(messages));
    CHECK(messages.empty());
    CHECK(has_message(error_messages_of(named_errors_source("rule R:\n"
                                                            "    on tick:\n"
                                                            "        let wrong: int = Game[Match].over\n")),
                      "is declared as 'int' but initialized with 'bool'"));
}

TEST_CASE("Semantic: bracketed named access is annotated with entity and trait",
          "[semantic][named-entity][trait-access]") {
    const auto analyzed = analyze_named_program("rule R:\n"
                                                "    on tick:\n"
                                                "        let over = Game[Match].over\n"
                                                "        Game[Match].score += 1\n");
    REQUIRE(analyzed.messages.empty());
    const auto& body   = rule_named(analyzed.ast, "R").handlers[0].body;
    const auto* access = find_trait_access(*std::get<LetStmt>(body[0]->stmt).value);
    REQUIRE(access != nullptr);
    CHECK(access->resolved_entity_id == test_entity("Game"));
    CHECK(access->resolved_trait_id == make_symbol_id(SymbolKind::Trait, "test", "Match"));

    const auto& assign = std::get<VarAssign>(body[1]->stmt);
    REQUIRE(assign.target != nullptr);
    const auto* target = find_trait_access(*assign.target);
    REQUIRE(target != nullptr);
    CHECK(target->resolved_entity_id == test_entity("Game"));
    CHECK(target->resolved_trait_id == make_symbol_id(SymbolKind::Trait, "test", "Match"));
}

TEST_CASE("Semantic: bracketed named access to an undeclared trait is rejected",
          "[semantic][named-entity][trait-access]") {
    CHECK(has_message(error_messages_of(named_errors_source("rule R:\n"
                                                            "    on tick:\n"
                                                            "        let id = Game[Tag].id\n")),
                      "entity 'Game' does not declare trait 'Tag'"));
    CHECK(has_message(error_messages_of(named_errors_source("rule R:\n"
                                                            "    on tick:\n"
                                                            "        Hud[Match].over = true\n")),
                      "entity 'Hud' does not declare trait 'Match'"));
    CHECK(has_message(error_messages_of(named_errors_source("rule R:\n"
                                                            "    on tick:\n"
                                                            "        let nope = Game[Match].nope\n")),
                      "trait 'Match' has no field 'nope'"));
    CHECK(has_message(error_messages_of(named_errors_source("rule R:\n"
                                                            "    on tick:\n"
                                                            "        let whole = Game[Match]\n")),
                      "named field access needs a field after 'Game[Match]'"));
}

TEST_CASE("Semantic: bracketed named access in a func body is rejected", "[semantic][named-entity][trait-access]") {
    CHECK(has_message(error_messages_of(named_errors_source("func peek() bool:\n"
                                                            "    return Game[Match].over\n")),
                      "named field access is only allowed in rules"));
}

TEST_CASE("Semantic: dotted named access reports one rename error and keeps analyzing",
          "[semantic][named-entity][trait-access]") {
    const auto read = error_messages_of(named_errors_source("rule R:\n"
                                                            "    on tick:\n"
                                                            "        let over: bool = Game.Match.over\n"
                                                            "        let wrong: int = true\n"));
    INFO(describe_messages(read));
    CHECK(count_message(read, "use 'Game[Match].over'") == 1);
    CHECK(has_message(read, "is declared as 'int' but initialized with 'bool'"));
    CHECK(read.size() == 2);

    const auto write = error_messages_of(named_errors_source("rule R:\n"
                                                             "    on tick:\n"
                                                             "        Hud.test.Label.visible = false\n"));
    INFO(describe_messages(write));
    CHECK(count_message(write, "use 'Hud[test.Label].visible'") == 1);
    CHECK(write.size() == 1);
}

TEST_CASE("Semantic: bracketed pair access resolves and types", "[semantic][pair-relations][trait-access]") {
    const auto messages = error_messages_of(
        pair_rule("            DynamicBody\n"
                  "            Transform\n",
                  "            test.Collider\n"
                  "            Solid as solid\n",
                  "        let x: float = body[Transform].x\n"
                  "        let mask: int = wall[test.Collider].mask\n"
                  "        let active: bool = solid.active\n"));
    INFO(describe_messages(messages));
    CHECK(messages.empty());
    CHECK(has_message(error_messages_of(pair_rule("            DynamicBody\n",
                                                  "            Collider\n",
                                                  "        let wrong: float = wall[Collider].mask\n")),
                      "is declared as 'float' but initialized with 'int'"));
}

TEST_CASE("Semantic: bracketed pair access to an unselected trait is rejected",
          "[semantic][pair-relations][trait-access]") {
    CHECK(has_message(error_messages_of(pair_rule("            DynamicBody\n",
                                                  "            Solid\n",
                                                  "        let x = body[Solid].active\n")),
                      "pair binding 'body' does not select trait 'Solid'"));
}

TEST_CASE("Semantic: pair aliases share one scope per rule", "[semantic][pair-relations][trait-access]") {
    CHECK(has_message(error_messages_of(pair_rule("            Transform as pose\n",
                                                  "            Transform as pose\n",
                                                  "        let x = pose.x\n")),
                      "alias 'pose' is declared twice in rule 'R'"));
    CHECK(has_message(error_messages_of(pair_rule("            Transform\n",
                                                  "            Collider as body\n",
                                                  "        let x = 1\n")),
                      "alias 'body' clashes with a pair binding name"));
}

TEST_CASE("Semantic: an eliminated binding's alias is not visible after reduce per",
          "[semantic][pair-relations][trait-access][rule-reduce]") {
    const auto messages = error_messages_of(limit_traits() +
                                            "rule R:\n"
                                            "    pairs:\n"
                                            "        actor:\n"
                                            "            Actor as kinematic\n"
                                            "        surface:\n"
                                            "            Surface as ground\n"
                                            "    reduce per actor:\n"
                                            "        rows = count()\n"
                                            "    on tick:\n"
                                            "        kinematic.grounded = rows > 0\n"
                                            "        let top = ground.top\n");
    INFO(describe_messages(messages));
    CHECK(has_message(messages, "'ground' is not available after `reduce:`"));
    CHECK_FALSE(has_message(messages, "'kinematic'"));
}

TEST_CASE("Semantic: pair writes keep read-only and limit rules for brackets and aliases",
          "[semantic][pair-relations][trait-access][rule-limit]") {
    const auto rule = [](const std::string& limit, const std::string& body) {
        return limit_traits() +
               "rule Ground:\n"
               "    pairs:\n"
               "        actor:\n"
               "            Actor as kinematic\n"
               "        surface:\n"
               "            Surface as ground\n" +
               limit + "    on tick:\n" + body;
    };
    CHECK(error_messages_of(rule("    limit: 1 per actor\n", "        actor[Actor].grounded = true\n")).empty());
    CHECK(error_messages_of(rule("    limit: 1 per actor\n", "        kinematic.grounded = true\n")).empty());
    CHECK(has_message(error_messages_of(rule("    limit: 1 per actor\n", "        surface[Surface].top = 1.0\n")),
                      "read-only"));
    CHECK(has_message(error_messages_of(rule("    limit: 1 per actor\n", "        ground.top = 1.0\n")), "read-only"));
    CHECK(has_message(error_messages_of(rule("", "        actor[Actor].grounded = true\n")), "read-only"));
    CHECK(has_message(error_messages_of(rule("", "        kinematic.grounded = true\n")), "read-only"));
    CHECK(has_message(error_messages_of(rule("    limit: 2 per actor\n", "        kinematic.grounded = true\n")),
                      "read-only"));
}

TEST_CASE("Semantic: dotted pair access reports one rename error", "[semantic][pair-relations][trait-access]") {
    const auto unaliased = error_messages_of(
        pair_rule("            DynamicBody\n", "            Collider\n", "        let mask = wall.Collider.mask\n"));
    INFO(describe_messages(unaliased));
    CHECK(count_message(unaliased, "use 'wall[Collider].mask'") == 1);
    CHECK(unaliased.size() == 1);

    const auto qualified = error_messages_of(pair_rule("            DynamicBody\n",
                                                       "            test.Collider\n",
                                                       "        let x = 1\n",
                                                       "    where:\n"
                                                       "        wall.test.Collider.mask > 0\n"));
    INFO(describe_messages(qualified));
    CHECK(count_message(qualified, "use 'wall[test.Collider].mask'") == 1);
    CHECK(qualified.size() == 1);

    const auto prefixed = error_messages_of(pair_rule(
        "            DynamicBody\n", "            Transform as pose\n", "        let x = wall.pose.x\n"));
    INFO(describe_messages(prefixed));
    CHECK(count_message(prefixed, "aliases are written bare: use 'pose.x'") == 1);
    CHECK(prefixed.size() == 1);

    const auto bracketed_alias = error_messages_of(pair_rule(
        "            DynamicBody\n", "            Transform as pose\n", "        let x = wall[pose].x\n"));
    INFO(describe_messages(bracketed_alias));
    CHECK(has_message(bracketed_alias, "'pose' is an alias; use 'pose.x'"));
}

TEST_CASE("Semantic: bracketed access through entity_id values is rejected",
          "[semantic][entity-id-access][trait-access]") {
    const auto local = error_messages_of(entity_id_traits() +
                                         "rule R:\n"
                                         "    on tick:\n"
                                         "        let e = query.first[Player]()\n"
                                         "        let hp = e[Health].hp\n");
    CHECK(count_message(local, entity_id_read_error("Health", "e")) == 1);

    const auto self = error_messages_of(entity_id_traits() +
                                        "rule R:\n"
                                        "    on tick:\n"
                                        "        let hp = self[Health].hp\n");
    CHECK(count_message(self, entity_id_read_error("Health", "self")) == 1);

    const auto event = error_messages_of(entity_id_traits() +
                                         "event Contact:\n"
                                         "    other: entity_id\n"
                                         "rule R:\n"
                                         "    on Contact as contact:\n"
                                         "        let hp = contact.other[Health].hp\n");
    CHECK(count_message(event, entity_id_read_error("Health", "contact.other")) == 1);

    const auto loop = error_messages_of(entity_id_traits() +
                                        "rule R:\n"
                                        "    on tick:\n"
                                        "        for wall in query.all[Solid]():\n"
                                        "            let hp = wall[test.Health].hp\n");
    CHECK(count_message(loop, entity_id_read_error("test.Health", "wall")) == 1);

    const auto write = error_messages_of(entity_id_traits() +
                                         "rule R:\n"
                                         "    on tick:\n"
                                         "        let e = query.first[Player]()\n"
                                         "        e[Health].hp = 3\n");
    INFO(describe_messages(write));
    CHECK(has_message(write, "can't write trait 'Health' through entity_id 'e'; use 'set Health on e:'"));
    CHECK(write.size() == 1);
}

TEST_CASE("Semantic: bracketed entity_id access is rejected in every position",
          "[semantic][entity-id-access][trait-access]") {
    CHECK(has_message(error_messages_of(entity_id_traits() +
                                        "rule R:\n"
                                        "    on tick:\n"
                                        "        let e = query.first[Player]()\n"
                                        "        let hp = e[Health].nonexistent\n"),
                      entity_id_read_error("Health", "e")));
    CHECK(has_message(error_messages_of(entity_id_traits() +
                                        "rule R:\n"
                                        "    each:\n"
                                        "        Player\n"
                                        "    where:\n"
                                        "        self[Health].hp > 0\n"
                                        "    on tick:\n"
                                        "        let x = 1\n"),
                      entity_id_read_error("Health", "self")));
    CHECK(has_message(error_messages_of(entity_id_traits() +
                                        "func f(e: entity_id) int:\n"
                                        "    return e[Health].hp\n"),
                      entity_id_read_error("Health", "e")));

    const auto compound = error_messages_of(entity_id_traits() +
                                            "rule R:\n"
                                            "    on tick:\n"
                                            "        for e in query.all[Health]():\n"
                                            "            e[Health].hp += 1\n");
    INFO(describe_messages(compound));
    CHECK(has_message(compound, "can't write trait 'Health' through entity_id 'e'; use 'set Health on e:'"));
    CHECK_FALSE(has_message(compound, "is read-only"));
    CHECK(compound.size() == 1);

    const auto undeclared = error_messages_of(entity_id_traits() +
                                              "rule R:\n"
                                              "    on tick:\n"
                                              "        var e = query.first[Player]()\n"
                                              "        e[Bogus].x = 1\n");
    INFO(describe_messages(undeclared));
    CHECK(has_message(undeclared, "can't write trait 'Bogus' through entity_id 'e'; use 'set Bogus on e:'"));
    CHECK(undeclared.size() == 1);
}

TEST_CASE("Semantic: bracketed access on a non-entity value is rejected", "[semantic][trait-access]") {
    CHECK(has_message(error_messages_of(entity_id_traits() +
                                        "rule R:\n"
                                        "    on tick:\n"
                                        "        let n = 3\n"
                                        "        let hp = n[Health].hp\n"),
                      "only entities carry traits"));
}

TEST_CASE("Contract: bracketed named access records reads, writes, and one requirement",
          "[semantic][handler-contracts][named-entity][trait-access]") {
    const auto program   = analyze_named("rule R:\n"
                                         "    each:\n"
                                         "        Enemy as enemy\n"
                                         "    when:\n"
                                         "        not Game[Match].over\n"
                                         "    on tick:\n"
                                         "        Game[Match].score += 1\n");
    const auto& contract = named_contract(program, "R");
    const auto game      = test_entity("Game");
    const auto match     = test_trait("Match");
    CHECK(contract.named_reads.at(game).at(match) == fields_of({"over", "score"}));
    CHECK(contract.named_writes.at(game).at(match) == fields_of({"score"}));
    CHECK(contract.write_fields.at(match) == fields_of({"score"}));
    CHECK(contract.named_requirements == std::vector<NamedRequirement>{game_match()});
    CHECK_FALSE(contract.splittable_per_entity());
}

TEST_CASE("Contract: a bare pair alias records a bound read on its binding",
          "[semantic][handler-contracts][pair-relations][trait-access]") {
    auto result = analyze(stdlib_events() + pair_traits() +
                          "rule R:\n"
                          "    pairs:\n"
                          "        body:\n"
                          "            DynamicBody\n"
                          "            Transform as pose\n"
                          "        wall:\n"
                          "            Collider\n"
                          "    on fixed_tick:\n"
                          "        if pose.x > 0.0 and wall[Collider].mask > 0:\n"
                          "            emit Contact to body:\n"
                          "                other = wall\n");
    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract    = result.handler_contracts[0];
    const auto transform_id = make_symbol_id(SymbolKind::Trait, "test", "Transform");
    const auto collider_id  = make_symbol_id(SymbolKind::Trait, "test", "Collider");
    CHECK(contract.reads.contains(transform_id));
    CHECK(contract.reads.contains(collider_id));
    const BoundTraitAccess body_transform{.binding_index = 0, .trait = transform_id};
    const BoundTraitAccess wall_collider{.binding_index = 1, .trait = collider_id};
    CHECK(std::ranges::find(contract.bound_reads, body_transform) != contract.bound_reads.end());
    CHECK(std::ranges::find(contract.bound_reads, wall_collider) != contract.bound_reads.end());
}

// ── Conditional expressions and value match ────────────────────────────────

static std::vector<std::string> analyze_error_messages(const std::string& source) {
    const std::string src = starts_with_module_decl(source) ? source : "module test\n" + source;
    ErrorReporter errors;
    Lexer lexer(src, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto program = parser.parse_program();
    if (!errors.has_errors()) {
        SemanticAnalyzer analyzer(errors);
        analyzer.analyze(program);
    }
    std::vector<std::string> messages;
    for (const auto& diagnostic : errors.diagnostics()) {
        if (diagnostic.level == DiagnosticLevel::Error) {
            messages.push_back(diagnostic.message);
        }
    }
    return messages;
}

static bool has_error(const std::vector<std::string>& messages, const std::string& expected) {
    return std::ranges::contains(messages, expected);
}

static const std::string& conditional_prelude() {
    static const std::string value =
        "enum Mode:\n"
        "    Idle\n"
        "    Chase\n"
        "    Flee\n"
        "trait Actor:\n"
        "    var mode: Mode = Mode.Idle\n"
        "    var hp: int = 10\n"
        "    var speed: float = 0.0\n"
        "    var ready: bool = false\n"
        "event Ping\n";
    return value;
}

static std::vector<std::string> conditional_handler_errors(const std::string& body) {
    return analyze_error_messages(conditional_prelude() +
                                  "rule R:\n"
                                  "    each:\n"
                                  "        Actor as a\n"
                                  "    on Ping:\n" +
                                  body);
}

TEST_CASE("Semantic: non-bool conditions are rejected", "[semantic][conditional-expressions]") {
    CHECK(has_error(conditional_handler_errors("        if a.hp:\n"
                                               "            a.speed = 1.0\n"),
                    "`if` condition must be `bool`, got `int`"));
    CHECK(has_error(conditional_handler_errors("        if a.ready:\n"
                                               "            a.speed = 1.0\n"
                                               "        else if a.speed:\n"
                                               "            a.speed = 2.0\n"),
                    "`else if` condition must be `bool`, got `float`"));
    CHECK(has_error(conditional_handler_errors("        a.speed = if a.hp: 1.0 else: 2.0\n"),
                    "`if` condition must be `bool`, got `int`"));
    CHECK(has_error(conditional_handler_errors("        a.speed = if a.ready: 1.0 else if a.hp: 2.0 else: 3.0\n"),
                    "`else if` condition must be `bool`, got `int`"));
    CHECK(has_error(conditional_handler_errors("        if a.ready and a.hp:\n"
                                               "            a.speed = 1.0\n"),
                    "right operand of `and` must be `bool`, got `int`"));
    CHECK(has_error(conditional_handler_errors("        if a.hp or a.ready:\n"
                                               "            a.speed = 1.0\n"),
                    "left operand of `or` must be `bool`, got `int`"));
    CHECK(has_error(conditional_handler_errors("        if not a.speed:\n"
                                               "            a.speed = 1.0\n"),
                    "operand of `not` must be `bool`, got `float`"));
    CHECK(conditional_handler_errors("        if a.hp > 0 and not a.ready:\n"
                                     "            a.speed = 1.0\n")
              .empty());
}

TEST_CASE("Semantic: expression error in a handler let is reported once", "[semantic][diagnostics]") {
    CHECK(conditional_handler_errors("        let z = if a.hp: 1 else: 2\n") ==
          std::vector<std::string>{"`if` condition must be `bool`, got `int`"});
}

TEST_CASE("Semantic: expression error in a func let is reported once", "[semantic][diagnostics]") {
    CHECK(analyze_error_messages(conditional_prelude() + "func f() int:\n"
                                                         "    let x: int = if true: 1 else: 2.5\n"
                                                         "    return x\n") ==
          std::vector<std::string>{"`if` expression branches have different types: `int` and `float`"});
}

TEST_CASE("Semantic: the same expression error on two lines is reported twice", "[semantic][diagnostics]") {
    CHECK(conditional_handler_errors("        let z = if a.hp: 1 else: 2\n"
                                     "        let w = if a.hp: 1 else: 2\n") ==
          std::vector<std::string>{"`if` condition must be `bool`, got `int`",
                                   "`if` condition must be `bool`, got `int`"});
}

TEST_CASE("Semantic: arithmetic with a bool operand is rejected", "[semantic][type-system]") {
    CHECK(conditional_handler_errors("        let y = 1 + true\n") ==
          std::vector<std::string>{"no operator '+' for operand types 'int' and 'bool'"});
    CHECK(conditional_handler_errors("        let s = a.ready * 2.0\n") ==
          std::vector<std::string>{"no operator '*' for operand types 'bool' and 'float'"});
    CHECK(conditional_handler_errors("        let m = a.hp % a.ready\n") ==
          std::vector<std::string>{"no operator '%' for operand types 'int' and 'bool'"});
    CHECK(conditional_handler_errors("        let n = -a.ready\n") ==
          std::vector<std::string>{"no operator '-' for operand type 'bool'"});
}

TEST_CASE("Semantic: bool equality is still accepted", "[semantic][type-system]") {
    CHECK(conditional_handler_errors("        if a.ready == true:\n"
                                     "            a.speed = 1.0\n")
              .empty());
}

TEST_CASE("Semantic: an unknown arithmetic operand reports no operator error", "[semantic][type-system]") {
    const auto errors = conditional_handler_errors("        let y = a.missing + true\n"
                                                   "        let n = -a.missing\n");
    CHECK(std::ranges::none_of(errors, [](const std::string& message) { return message.contains("no operator"); }));
}

TEST_CASE("Semantic: conditional expression branch types must match", "[semantic][conditional-expressions]") {
    CHECK(has_error(conditional_handler_errors("        let x = if a.ready: 1 else: 2.0\n"),
                    "`if` expression branches have different types: `int` and `float`"));
    CHECK(has_error(conditional_handler_errors("        let x = match a.mode:\n"
                                               "            Mode.Idle => 0.0\n"
                                               "            _ => 1\n"),
                    "`match` arms have different types: `float` and `int`"));
    CHECK(conditional_handler_errors("        let x = if a.ready: 4.0 else: 1.5\n"
                                     "        a.speed = x\n"
                                     "        let s = match a.mode:\n"
                                     "            Mode.Idle => 0.0\n"
                                     "            Mode.Chase => 2.0\n"
                                     "            Mode.Flee => 3.0\n"
                                     "        a.speed = s\n")
              .empty());
}

TEST_CASE("Semantic: value match patterns are typed and checked", "[semantic][conditional-expressions]") {
    CHECK(has_error(conditional_handler_errors("        match a.mode:\n"
                                               "            3 =>\n"
                                               "                a.hp = 1\n"
                                               "            _ =>\n"
                                               "                a.hp = 2\n"),
                    "pattern `3` is an `int` but the subject is `Mode`"));
    CHECK(has_error(conditional_handler_errors("        match a.mode:\n"
                                               "            Mode.Idle =>\n"
                                               "                a.hp = 1\n"
                                               "            Mode.Idle =>\n"
                                               "                a.hp = 2\n"
                                               "            _ =>\n"
                                               "                a.hp = 3\n"),
                    "duplicate pattern `Mode.Idle`"));
    CHECK(has_error(conditional_handler_errors("        let x = match a.hp:\n"
                                               "            _ => 0.0\n"
                                               "            1 => 1.0\n"
                                               "        a.speed = x\n"),
                    "`_` must be the last arm of a `match`"));
    CHECK(has_error(conditional_handler_errors("        match a.mode:\n"
                                               "            Mode.Idle =>\n"
                                               "                a.hp = 1\n"
                                               "            Mode.Chase =>\n"
                                               "                a.hp = 2\n"),
                    "`match` on `Mode` is missing `Flee`"));
    CHECK(has_error(conditional_handler_errors("        match a.hp:\n"
                                               "            0 =>\n"
                                               "                a.speed = 1.0\n"
                                               "            1 =>\n"
                                               "                a.speed = 2.0\n"),
                    "a `match` on `int` needs a `_` arm"));
    CHECK(has_error(conditional_handler_errors("        match a.ready:\n"
                                               "            true =>\n"
                                               "                a.hp = 1\n"),
                    "`match` on `bool` is missing `false`"));
    CHECK(conditional_handler_errors("        match a.ready:\n"
                                     "            true =>\n"
                                     "                a.hp = 1\n"
                                     "            false =>\n"
                                     "                a.hp = 2\n")
              .empty());
    CHECK(has_error(conditional_handler_errors("        match a.mode:\n"
                                               "            Mode.Idle as m =>\n"
                                               "                a.hp = 1\n"
                                               "            _ =>\n"
                                               "                a.hp = 2\n"),
                    "value match arms cannot declare an alias"));
}

TEST_CASE("Semantic: value match in a func body is accepted", "[semantic][conditional-expressions]") {
    CHECK(analyze_error_messages(conditional_prelude() + "func cost(mode: Mode) int:\n"
                                                         "    var c = 0\n"
                                                         "    match mode:\n"
                                                         "        Mode.Idle =>\n"
                                                         "            c = 0\n"
                                                         "        _ =>\n"
                                                         "            c = 1\n"
                                                         "    return c\n")
              .empty());
}

TEST_CASE("Semantic: trait match in a func body is rejected with exactly one diagnostic",
          "[semantic][conditional-expressions]") {
    CHECK(analyze_error_messages(conditional_prelude() + "func f(subject: entity_id) int:\n"
                                                         "    match subject:\n"
                                                         "        Actor as b =>\n"
                                                         "            return b.hp\n"
                                                         "    return 0\n") ==
          std::vector<std::string>{"statement-level `match entity_id` only allowed inside rule event handlers"});
}

TEST_CASE("Semantic: destroy inside a value match arm in a func body is still rejected",
          "[semantic][conditional-expressions]") {
    CHECK(has_error(analyze_error_messages(conditional_prelude() + "func f(mode: Mode, target: entity_id) int:\n"
                                                                   "    match mode:\n"
                                                                   "        Mode.Idle =>\n"
                                                                   "            destroy target\n"
                                                                   "        _ =>\n"
                                                                   "            return 1\n"
                                                                   "    return 0\n"),
                    "`destroy` only allowed inside rule event handlers"));
}

TEST_CASE("Semantic: float match subject is rejected with the subject-type message",
          "[semantic][conditional-expressions]") {
    CHECK(has_error(conditional_handler_errors("        match a.speed:\n"
                                               "            _ =>\n"
                                               "                a.hp = 1\n"),
                    "statement-level `match` subject must be `entity_id`, an enum, `int`, `bool` or a state slot, got `float`"));
}

TEST_CASE("Semantic: conditional expression in a constant is a const expression",
          "[semantic][conditional-expressions]") {
    CHECK(analyze_error_messages("const:\n"
                                 "    FAST = true\n"
                                 "    STEP = if FAST: 2.0 else: 1.0\n")
              .empty());
}

TEST_CASE("Semantic: compact contracts preserve explicit capabilities and exclusions", "[semantic][concise]") {
    const std::string declarations = "pub event tick\ntrait Health:\n    var value: int = 0\ntrait Mark\n";
    const auto program             = analyze(declarations + R"(extern rule Native:
    each: Health
    on tick:
        commands: set Health
)");
    REQUIRE(program.execution_graph.handlers.size() == 1);
    const auto& contract = program.execution_graph.handlers.front().contract;
    CHECK(contract.reads.empty());
    CHECK(contract.writes.empty());
    CHECK(contract.selection.size() == 1);
    REQUIRE(contract.commands.size() == 1);
    CHECK(contract.commands.front().kind == HandlerCommandKind::Set);
    CHECK(analyze_has_errors(declarations + R"(extern rule Native:
    each: Health
    on tick:
        writes: Health
        projects: Health
)"));
    CHECK(analyze_has_errors(declarations + R"(extern rule Native:
    each: Health
    on tick:
        projects: Health, Health
)"));
}

TEST_CASE("Semantic: concise clauses preserve domain and predicate restrictions", "[semantic][concise]") {
    const std::string declarations = "pub event tick\ntrait Body:\n    var x: int = 0\ntrait Zone\n";
    const std::vector<std::string> rules{
        "rule R:\n    where: true\n    on tick:\n        return\n",
        "rule R:\n    each: Body as b\n    when: b.x > 0\n    on tick:\n        return\n",
        "rule R:\n    each: Body as b\n    where: true, b.x\n    on tick:\n        return\n",
        "rule R:\n    each: Body\n    reduce per body:\n        n = count()\n    on tick:\n        return\n",
        R"(rule R:
    pairs:
        body: Body
        zone: Zone
    reduce per unknown:
        n = count()
    on tick:
        return
)",
        "rule R:\n    pairs:\n        body: Body\n        body: Zone\n    on tick:\n        return\n"};
    for (const auto& rule : rules) {
        CAPTURE(rule);
        CHECK(analyze_has_errors(declarations + rule));
    }
}

TEST_CASE("Semantic: filter and exclude resolve as ordinary names", "[semantic][concise]") {
    const auto program = analyze(R"(pub event tick
trait filter:
    var exclude: int = 0
    var filter: float = 0.0
trait exclude
func pick(filter: int, exclude: int) int:
    return filter + exclude
rule R:
    each: filter as exclude
    without: exclude
    on tick:
        let filter = exclude.filter
        exclude.exclude = pick(1, 2)
)");
    CHECK(program.execution_graph.handlers.size() == 1);
}

TEST_CASE("Semantic: compact dependencies retain cycle diagnostics", "[semantic][concise]") {
    const auto errors = analyze_error_messages(R"(pub event tick
rule A:
    after: B
    on tick:
        return
rule B:
    after: A
    on tick:
        return
)");
    CHECK(std::ranges::any_of(errors, [](const auto& message) { return message.starts_with("handler cycle: "); }));
}

TEST_CASE("Semantic: a second inline predicate reports its own source location", "[semantic][concise]") {
    const std::string source =
        "module test\npub event tick\ntrait Body\nrule R:\n    each: Body\n"
        "    where: true, 42\n    on tick:\n        return\n";
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(ast);
    REQUIRE(errors.has_errors());
    CHECK(std::ranges::any_of(errors.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.location.line == 6 && diagnostic.location.column == 18;
    }));
}

// ── Iteration order (dsl-rule-order-by) ─────────────────────────────────────

static const std::string& iteration_order_prelude() {
    static const std::string value = stdlib_events() +
                                     "trait Pos:\n"
                                     "    var x: float\n"
                                     "    var y: float\n"
                                     "trait Score:\n"
                                     "    var value: float\n"
                                     "trait Mover:\n"
                                     "    var speed: float\n"
                                     "entity Game:\n"
                                     "    Score\n"
                                     "event Ping:\n"
                                     "    v: float\n"
                                     "extern func draw(x: float)\n";
    return value;
}

static IterationOrder iteration_order_of(const std::string& rule_source) {
    const auto program = analyze(iteration_order_prelude() + rule_source);
    const auto node    = std::ranges::find_if(program.execution_graph.handlers, [](const HandlerNode& candidate) {
        return candidate.identity.rule.local_name == "R";
    });
    REQUIRE(node != program.execution_graph.handlers.end());
    return node->contract.iteration_order;
}

TEST_CASE("Semantic: a unary pass that can observe its row order iterates in creation order",
          "[semantic][rule-order-by][handler-contracts]") {
    const auto unary = [](const std::string& extra, const std::string& body) {
        return "rule R:\n    each:\n        Pos as p\n" + extra + "    on tick:\n" + body;
    };
    SECTION("limit without order by") {
        CHECK(iteration_order_of(unary("    limit: 2\n", "        p.x = 1.0\n")) == IterationOrder::Creation);
    }
    SECTION("emit") {
        CHECK(iteration_order_of(unary("", "        emit Ping:\n            v = p.x\n")) == IterationOrder::Creation);
    }
    SECTION("structural command") {
        CHECK(iteration_order_of(unary("", "        destroy\n")) == IterationOrder::Creation);
    }
    SECTION("effect") {
        CHECK(iteration_order_of(unary("", "        draw(p.x)\n")) == IterationOrder::Creation);
    }
    SECTION("named write") {
        CHECK(iteration_order_of(unary("", "        Game[Score].value = p.x\n")) == IterationOrder::Creation);
    }
}

TEST_CASE("Semantic: a named read of a field the pass writes observes row order",
          "[semantic][rule-order-by][handler-contracts]") {
    CHECK(iteration_order_of("rule R:\n"
                             "    each:\n"
                             "        Pos as p\n"
                             "        Score as s\n"
                             "    on tick:\n"
                             "        s.value = Game[Score].value + p.x\n") == IterationOrder::Creation);
    CHECK(iteration_order_of("rule R:\n"
                             "    each:\n"
                             "        Pos as p\n"
                             "    on tick:\n"
                             "        p.x = Game[Score].value\n") == IterationOrder::Free);
}

TEST_CASE("Semantic: passes that cannot observe their row order stay free",
          "[semantic][rule-order-by][handler-contracts]") {
    SECTION("self-only writes") {
        CHECK(iteration_order_of("rule R:\n"
                                 "    each:\n"
                                 "        Pos as p\n"
                                 "        Mover as m\n"
                                 "    on tick:\n"
                                 "        p.x = p.x + (m.speed * tick.dt)\n") == IterationOrder::Free);
    }
    SECTION("limit with order by") {
        CHECK(iteration_order_of("rule R:\n"
                                 "    each:\n"
                                 "        Pos as p\n"
                                 "    order by:\n"
                                 "        p.x asc\n"
                                 "    limit: 2\n"
                                 "    on tick:\n"
                                 "        p.y = 1.0\n") == IterationOrder::Free);
    }
    SECTION("selectionless") {
        CHECK(iteration_order_of("rule R:\n"
                                 "    on tick:\n"
                                 "        Game[Score].value = 1.0\n"
                                 "        emit Ping:\n"
                                 "            v = 1.0\n") == IterationOrder::Free);
    }
    SECTION("pair") {
        CHECK(iteration_order_of("rule R:\n"
                                 "    pairs:\n"
                                 "        a: Pos\n"
                                 "        b: Pos\n"
                                 "    on tick:\n"
                                 "        emit Ping:\n"
                                 "            v = a[Pos].x\n") == IterationOrder::Free);
    }
}

// ── Schedule edge reasons and levels (handler-execution-graph) ─────────────

static const std::string& edge_reason_prelude() {
    static const std::string value = stdlib_events() +
                                     "trait Score:\n"
                                     "    var value: int\n"
                                     "trait Transform:\n"
                                     "    var position: float\n"
                                     "trait Seen:\n"
                                     "    var value: float\n"
                                     "entity Game:\n"
                                     "    Score\n";
    return value;
}

static std::vector<const ScheduleEdge*> edges_from(const DecoratedProgram& program,
                                                   const std::string& before,
                                                   const std::string& after) {
    std::vector<const ScheduleEdge*> edges;
    for (const auto& edge : program.execution_graph.schedule_edges) {
        if (edge.before.rule.local_name == before && edge.after.rule.local_name == after) {
            edges.push_back(&edge);
        }
    }
    return edges;
}

static bool has_reason(const std::vector<const ScheduleEdge*>& edges, ScheduleEdgeOrientation reason) {
    return std::ranges::any_of(edges, [&](const ScheduleEdge* edge) { return edge->orientation == reason; });
}

TEST_CASE("Semantic: explicit order is recorded as the edge reason", "[semantic][handler-execution-graph]") {
    const auto program = analyze(edge_reason_prelude() +
                                 "rule A:\n"
                                 "    on tick:\n"
                                 "        Game[Score].value = 1\n"
                                 "rule B:\n"
                                 "    after:\n"
                                 "        A\n"
                                 "    on tick:\n"
                                 "        Game[Score].value = 2\n");
    const auto edges = edges_from(program, "A", "B");
    REQUIRE_FALSE(edges.empty());
    CHECK(std::ranges::all_of(
        edges, [](const ScheduleEdge* edge) { return edge->orientation == ScheduleEdgeOrientation::Explicit; }));
}

TEST_CASE("Semantic: one-way data flow is recorded as writer before reader", "[semantic][handler-execution-graph]") {
    const auto program = analyze(edge_reason_prelude() +
                                 "rule Reader:\n"
                                 "    each:\n"
                                 "        Transform as t\n"
                                 "        Seen as s\n"
                                 "    on tick:\n"
                                 "        s.value = t.position\n"
                                 "rule Writer:\n"
                                 "    each:\n"
                                 "        Transform as t\n"
                                 "    on tick:\n"
                                 "        t.position += 1.0\n");
    CHECK(edges_from(program, "Reader", "Writer").empty());
    CHECK(has_reason(edges_from(program, "Writer", "Reader"), ScheduleEdgeOrientation::WriterBeforeReader));
}

TEST_CASE("Semantic: two assignments that nothing orders are rejected", "[semantic][handler-execution-graph]") {
    const auto messages = analyze_error_messages(edge_reason_prelude() +
                                                 "rule First:\n"
                                                 "    on tick:\n"
                                                 "        Game[Score].value = 1\n"
                                                 "rule Second:\n"
                                                 "    on tick:\n"
                                                 "        Game[Score].value = 2\n");
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].starts_with("unorientable conflict between 'test.First/on test.tick' and 'test.Second/on test.tick'"));
}

static std::map<std::string, std::uint64_t> levels_of(const std::vector<std::string>& rules) {
    std::string source = edge_reason_prelude() + "trait Clock:\n    var t: float\n";
    for (const auto& rule : rules) {
        source += rule;
    }
    const auto program = analyze(source);
    std::map<std::string, std::uint64_t> levels;
    for (const auto& level : program.execution_graph.dependency_levels) {
        for (const auto& handler : level.handlers) {
            levels[handler.rule.local_name] = level.index;
        }
    }
    return levels;
}

TEST_CASE("Semantic: dependency levels follow the longest path, not declaration order",
          "[semantic][handler-execution-graph]") {
    // A -> B -> C by data flow; D touches nothing the others do.
    const std::string a = "rule A:\n    each:\n        Transform as t\n    on tick:\n        t.position = 1.0\n";
    const std::string b = "rule B:\n    each:\n        Transform as t\n        Seen as s\n    on tick:\n"
                          "        s.value = t.position\n";
    const std::string c = "rule C:\n    each:\n        Seen as s\n        Score as score\n    on tick:\n"
                          "        if s.value > 0.0:\n            score.value = 2\n";
    const std::string d = "rule D:\n    each:\n        Clock as c\n    on tick:\n        c.t += 1.0\n";
    const std::map<std::string, std::uint64_t> expected{{"A", 0}, {"B", 1}, {"C", 2}, {"D", 0}};
    CHECK(levels_of({a, b, c, d}) == expected);
    CHECK(levels_of({d, c, b, a}) == expected);
    CHECK(levels_of({b, a, d, c}) == expected);
}

// ── Commuting writes and channel conflicts (handler-execution-graph) ───────

static const std::string& commuting_prelude() {
    static const std::string value = stdlib_events() +
                                     "trait Health:\n"
                                     "    var hp: int = 10\n"
                                     "    var max_hp: int = 10\n"
                                     "trait Actor:\n"
                                     "    var grounded: bool = false\n"
                                     "trait T:\n"
                                     "    var x: float = 0.0\n"
                                     "trait P:\n"
                                     "    var x: float = 0.0\n"
                                     "    var v: float = 0.0\n"
                                     "trait Log:\n"
                                     "    var last: int = 0\n"
                                     "    var total: int = 0\n"
                                     "trait Stunned\n"
                                     "event Damaged:\n"
                                     "    amount: int\n"
                                     "event Spoke:\n"
                                     "    line: int\n"
                                     "event Relay:\n"
                                     "    line: int\n";
    return value;
}

static DecoratedProgram analyze_commuting(const std::string& rules) {
    ModuleImports imports;
    imports.modules["input"] = make_input_import();
    return analyze_with_imports(commuting_prelude() + rules, imports);
}

static std::string tick_rule(const std::string& name,
                             const std::string& filter,
                             const std::string& body,
                             const std::string& after = "") {
    return "rule " + name + ":\n" + (filter.empty() ? "" : "    each:\n        " + filter + "\n") +
           (after.empty() ? "" : "    after:\n        " + after + "\n") + "    on tick:\n" + body;
}

static std::vector<const ScheduleEdge*> edges_between(const DecoratedProgram& program,
                                                      const std::string& left,
                                                      const std::string& right) {
    auto edges = edges_from(program, left, right);
    const auto reverse = edges_from(program, right, left);
    edges.insert(edges.end(), reverse.begin(), reverse.end());
    return edges;
}

static const ScheduleEdge* edge_of_kind(const std::vector<const ScheduleEdge*>& edges, ScheduleEdgeKind kind) {
    const auto found = std::ranges::find(edges, kind, &ScheduleEdge::kind);
    return found == edges.end() ? nullptr : *found;
}

TEST_CASE("Commuting writes: two damage sources add no edge", "[semantic][handler-execution-graph][write-kinds]") {
    const auto program = analyze_commuting(tick_rule("Burn", "Health as h", "        h.hp -= 1\n") +
                                           tick_rule("Poison", "Health as h", "        h.hp -= 2\n"));
    CHECK(edges_between(program, "Burn", "Poison").empty());
}

TEST_CASE("Commuting writes: the same Join operator adds no edge", "[semantic][handler-execution-graph][write-kinds]") {
    const auto program = analyze_commuting(
        tick_rule("Land", "Actor as a", "        a.grounded = true\n") +
        tick_rule("Touch", "Actor as a", "        let touching = true\n        a.grounded = a.grounded or touching\n"));
    CHECK(edges_between(program, "Land", "Touch").empty());
}

static std::vector<std::string> commuting_errors(const std::string& rules) {
    return analyze_error_messages(commuting_prelude() + rules);
}

TEST_CASE("Commuting writes: different Join operators conflict", "[semantic][handler-execution-graph][write-kinds]") {
    const auto messages = commuting_errors(tick_rule("Lift", "Actor as a", "        a.grounded = false\n") +
                                           tick_rule("Land", "Actor as a", "        a.grounded = true\n"));
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].contains("test.Actor.grounded (Land: Join or, Lift: Join and)"));
}

TEST_CASE("Commuting writes: Accum against Assign conflicts", "[semantic][handler-execution-graph][write-kinds]") {
    const auto messages = commuting_errors(tick_rule("Add", "T as t", "        t.x += 1.0\n") +
                                           tick_rule("Reset", "T as t", "        t.x = 0.0\n"));
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].contains("test.T.x (Add: Accum, Reset: Assign)"));
}

TEST_CASE("Commuting writes: an Accum writer precedes a reader", "[semantic][handler-execution-graph][write-kinds]") {
    const auto program = analyze_commuting(tick_rule("Read", "Health as h", "        let x = h.hp\n") +
                                           tick_rule("Hurt", "Health as h", "        h.hp -= 5\n"));
    CHECK(has_reason(edges_from(program, "Hurt", "Read"), ScheduleEdgeOrientation::WriterBeforeReader));
    CHECK(edges_from(program, "Read", "Hurt").empty());
}

TEST_CASE("Commuting writes: a delta that reads the other's write still conflicts",
          "[semantic][handler-execution-graph][write-kinds]") {
    const auto program = analyze_commuting(tick_rule("Move", "P as p", "        p.x += p.v\n") +
                                           tick_rule("Stop", "P as p", "        p.v = 0.0\n        p.x += 1.0\n"));
    const auto edges = edges_from(program, "Stop", "Move");
    CHECK(has_reason(edges, ScheduleEdgeOrientation::WriterBeforeReader));
    const auto* data = edge_of_kind(edges, ScheduleEdgeKind::DataConflict);
    REQUIRE(data != nullptr);
    REQUIRE(data->field_provenance.size() == 1);
    CHECK(data->field_provenance[0].access == fields_of({"v"}));
    CHECK(edges_from(program, "Move", "Stop").empty());
}

TEST_CASE("Effect access: observing calls add no edge", "[semantic][handler-execution-graph][effects]") {
    const auto program = analyze_commuting(
        tick_rule("LookA", "", "        if input.pressed():\n            let x = 1\n") +
        tick_rule("LookB", "", "        if input.pressed():\n            let y = 2\n"));
    CHECK(edges_between(program, "LookA", "LookB").empty());
}

TEST_CASE("Effect access: observing and acting conflict", "[semantic][handler-execution-graph][effects]") {
    const auto look = tick_rule("Look", "", "        if input.pressed():\n            let x = 1\n");
    const auto program = analyze_commuting(look + tick_rule("Take", "", "        input.consume()\n", "Look"));
    const auto* effect = edge_of_kind(edges_between(program, "Look", "Take"), ScheduleEdgeKind::EffectConflict);
    REQUIRE(effect != nullptr);
    CHECK(effect->effect_provenance == std::vector<std::string>{"input"});

    ModuleImports imports;
    imports.modules["input"] = make_input_import();
    const auto source = "module test\n" + commuting_prelude() + look + tick_rule("Take", "", "        input.consume()\n");
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(ast, imports);
    REQUIRE(errors.diagnostics().size() == 1);
    CHECK(errors.diagnostics().front().message.contains("on effect input;"));
}

static std::optional<ScheduleEdge> channel_edge(const std::string& rules, const std::string& left, const std::string& right) {
    const auto program = analyze_commuting(rules);
    const auto* edge   = edge_of_kind(edges_between(program, left, right), ScheduleEdgeKind::ChannelConflict);
    return edge == nullptr ? std::nullopt : std::optional(*edge);
}

TEST_CASE("Channel conflicts: the command table", "[semantic][handler-execution-graph][channels]") {
    const auto pair = [](const std::string& left, const std::string& right) {
        return tick_rule("A", "Health as h", left) + tick_rule("B", "Health as h", right, "A");
    };
    SECTION("marker adds commute") {
        CHECK(channel_edge(pair("        add Stunned\n", "        add Stunned\n"), "A", "B") == std::nullopt);
    }
    SECTION("removes commute") {
        CHECK(channel_edge(pair("        remove Stunned\n", "        remove Stunned\n"), "A", "B") == std::nullopt);
    }
    SECTION("add against remove conflicts") {
        const auto edge = channel_edge(pair("        add Stunned\n", "        remove Stunned\n"), "A", "B");
        REQUIRE(edge != std::nullopt);
        CHECK(edge->channel_provenance == std::vector<std::string>{"commands test.Stunned: add / remove"});
    }
    SECTION("destroy commutes with set") {
        CHECK(channel_edge(pair("        destroy\n", "        set Log on self:\n            last = 1\n"), "A", "B") ==
              std::nullopt);
    }
    SECTION("disjoint set patches commute") {
        CHECK(channel_edge(pair("        set Log on self:\n            last = 1\n",
                                "        set Log on self:\n            total = 1\n"),
                           "A",
                           "B") == std::nullopt);
    }
    SECTION("overlapping set patches conflict") {
        CHECK(channel_edge(pair("        set Log on self:\n            last = 1\n",
                                "        set Log on self:\n            last = 2\n"),
                           "A",
                           "B") != std::nullopt);
    }
    SECTION("valued adds conflict") {
        CHECK(channel_edge(pair("        add Log:\n            last = 1\n", "        add Log:\n            last = 2\n"),
                           "A",
                           "B") != std::nullopt);
    }
    SECTION("set against add conflicts") {
        CHECK(channel_edge(pair("        set Log on self:\n            last = 1\n", "        add Log\n"), "A", "B") !=
              std::nullopt);
    }
}

static std::string log_consumer(const std::string& trigger, const std::string& body) {
    return "rule Consume:\n"
           "    each:\n"
           "        Log as log\n"
           "    on " +
           trigger + ":\n" + body;
}

TEST_CASE("Channel conflicts: lifecycle triggers from unordered commands", "[semantic][handler-execution-graph][channels]") {
    const auto adders = tick_rule("A", "Health as h", "        add Stunned\n") +
                        tick_rule("B", "Health as h", "        add Stunned\n", "A");
    const auto ordered = channel_edge(adders + log_consumer("added Stunned", "        log.last = 1\n"), "A", "B");
    REQUIRE(ordered != std::nullopt);
    CHECK(ordered->channel_provenance ==
          std::vector<std::string>{"test.A/on test.tick via added test.Stunned reaches test.Consume/on added "
                                   "test.Stunned; test.B/on test.tick via added test.Stunned reaches test.Consume/on "
                                   "added test.Stunned: test.Log.last (Consume: Assign, Consume: Assign)"});
    CHECK(channel_edge(adders + log_consumer("added Stunned", "        log.total += 1\n"), "A", "B") == std::nullopt);
}

TEST_CASE("Channel conflicts: event producers and observing consumers", "[semantic][handler-execution-graph][channels]") {
    const auto emitters = [](const std::string& event, const std::string& field = "line") {
        return tick_rule("A", "", "        emit " + event + ":\n            " + field + " = 1\n") +
               tick_rule("B", "", "        emit " + event + ":\n            " + field + " = 2\n", "A");
    };
    SECTION("an Accum consumer allows unordered producers") {
        CHECK(channel_edge(emitters("Damaged", "amount") +
                               "rule Hurt:\n"
                               "    each:\n"
                               "        Health as h\n"
                               "    on Damaged as d:\n"
                               "        h.hp -= d.amount\n",
                           "A",
                           "B") == std::nullopt);
    }
    SECTION("an Assign consumer needs ordered producers") {
        const auto edge = channel_edge(emitters("Spoke") + log_consumer("Spoke as s", "        log.last = s.line\n"), "A", "B");
        REQUIRE(edge != std::nullopt);
        CHECK(edge->channel_provenance ==
              std::vector<std::string>{"test.A/on test.tick via event test.Spoke reaches test.Consume/on test.Spoke; "
                                       "test.B/on test.tick via event test.Spoke reaches test.Consume/on test.Spoke: "
                                       "test.Log.last (Consume: Assign, Consume: Assign)"});
    }
    SECTION("an emit chain ending in an observing consumer") {
        CHECK(channel_edge(emitters("Relay") +
                               "rule Forward:\n"
                               "    on Relay as r:\n"
                               "        emit Spoke:\n"
                               "            line = r.line\n" +
                               log_consumer("Spoke as s", "        log.last = s.line\n"),
                           "A",
                           "B") != std::nullopt);
    }
    SECTION("an event cycle of commuting consumers") {
        CHECK(channel_edge(emitters("Relay") +
                               "rule Echo:\n"
                               "    each:\n"
                               "        Log as log\n"
                               "    on Relay as r:\n"
                               "        log.total += 1\n"
                               "        emit Relay:\n"
                               "            line = r.line\n",
                           "A",
                           "B") == std::nullopt);
    }
}

static std::string emit_rule(const std::string& name, const std::string& trigger, const std::vector<std::string>& events) {
    std::string body;
    for (const auto& event : events) {
        body += "        emit " + event + ":\n            line = 1\n";
    }
    return "rule " + name + ":\n    on " + trigger + ":\n" + body;
}

static std::string hp_rule(const std::string& name, const std::string& trigger, const std::string& value) {
    return "rule " + name + ":\n    each:\n        Health as h\n    on " + trigger + ":\n        h.hp = " + value + "\n";
}

static const std::string& occurrence_events() {
    static const std::string value = "event Hit:\n    line: int\nevent Heal:\n    line: int\nevent Ping:\n    line: int\n"
                                     "event Pong:\n    line: int\nevent E3:\n    line: int\n";
    return value;
}

TEST_CASE("Channel conflicts: occurrences race per generation", "[semantic][handler-execution-graph][channels]") {
    SECTION("different events with conflicting consumers") {
        const auto messages = commuting_errors(occurrence_events() + emit_rule("A", "tick", {"Hit"}) +
                                               emit_rule("B", "tick", {"Heal"}) + hp_rule("OnHit", "Hit", "0") +
                                               hp_rule("OnHeal", "Heal", "10"));
        REQUIRE(messages.size() == 1);
        CHECK(messages[0] ==
              "unorientable conflict between 'test.A/on test.tick' and 'test.B/on test.tick' on test.A/on test.tick via "
              "event test.Hit reaches test.OnHit/on test.Hit; test.B/on test.tick via event test.Heal reaches "
              "test.OnHeal/on test.Heal: test.Health.hp (OnHit: Assign, OnHeal: Assign); nothing orders them: add "
              "'after: B/on tick' to the A/on tick handler, or 'after: A/on tick' to the B/on tick handler");
    }
    SECTION("an after: between the producers orders them") {
        CHECK(commuting_errors(occurrence_events() + emit_rule("A", "tick", {"Hit"}) +
                               "rule B:\n    after:\n        A\n    on tick:\n        emit Heal:\n            line = 1\n" +
                               hp_rule("OnHit", "Hit", "0") + hp_rule("OnHeal", "Heal", "10"))
                  .empty());
    }
    SECTION("different lifecycle triggers with conflicting consumers") {
        const std::string traits = "trait Burning\ntrait Wet\n";
        const auto consumer = [](const std::string& name, const std::string& trait, const std::string& value) {
            return "rule " + name + ":\n    each:\n        T as t\n    on added " + trait + ":\n        t.x = " + value +
                   "\n";
        };
        const auto messages = commuting_errors(traits + tick_rule("A", "Health as h", "        add Burning\n") +
                                               tick_rule("B", "Health as h", "        add Wet\n") +
                                               consumer("OnBurn", "Burning", "100.0") + consumer("OnWet", "Wet", "0.0"));
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("test.A/on test.tick via added test.Burning reaches test.OnBurn/on added test.Burning; "
                                   "test.B/on test.tick via added test.Wet reaches test.OnWet/on added test.Wet: test.T.x"));
    }
    SECTION("consumers at different generations do not race") {
        CHECK(commuting_errors(occurrence_events() + emit_rule("A", "tick", {"Hit"}) + emit_rule("B", "tick", {"Heal"}) +
                               emit_rule("Mid", "Hit", {"E3"}) + hp_rule("OnHeal", "Heal", "10") +
                               hp_rule("OnE3", "E3", "0"))
                  .empty());
    }
    SECTION("one producer's emissions never race with each other") {
        CHECK(commuting_errors(occurrence_events() + emit_rule("A", "tick", {"Hit", "Heal"}) +
                               hp_rule("OnHit", "Hit", "0") + hp_rule("OnHeal", "Heal", "10"))
                  .empty());
    }
    SECTION("a lifecycle delivery runs in the next round, not at the next generation") {
        CHECK(commuting_errors(occurrence_events() + "trait Burning\n" +
                               tick_rule("A", "Health as h", "        add Burning\n") + emit_rule("B", "tick", {"Hit"}) +
                               "rule OnBurn:\n    each:\n        T as t\n    on added Burning:\n        t.x = 1.0\n"
                               "rule OnHit:\n    each:\n        T as t\n    on Hit:\n        t.x = 2.0\n")
                  .empty());
    }
    SECTION("lifecycle deliveries share a round whatever depth issued them") {
        const auto messages = commuting_errors(
            occurrence_events() + "trait Burning\ntrait Wet\n" + tick_rule("A", "Health as h", "        add Burning\n") +
            emit_rule("B", "tick", {"Hit"}) + "rule Soak:\n    each:\n        Health as h\n    on Hit:\n        add Wet\n" +
            "rule OnBurn:\n    each:\n        T as t\n    on added Burning:\n        t.x = 1.0\n"
            "rule OnWet:\n    each:\n        T as t\n    on added Wet:\n        t.x = 2.0\n");
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("test.B/on test.tick via event test.Hit -> added test.Wet reaches "
                                   "test.OnWet/on added test.Wet"));
    }
    SECTION("runs after a delayed edge share one bucket") {
        const auto messages = commuting_errors(
            occurrence_events() + "event Knock:\n    line: int\n" + emit_rule("A", "tick", {"Knock"}) +
            emit_rule("B", "tick", {"Hit"}) + emit_rule("Tap", "Hit", {"Knock"}) +
            "rule Echo:\n    each:\n        Log as log\n    on Knock as k:\n        log.last = k.line\n"
            "        emit Knock:\n            line = k.line\n");
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("test.A/on test.tick via event test.Knock -> event test.Knock (delayed) reaches "
                                   "test.Echo/on test.Knock; test.B/on test.tick via event test.Hit -> event test.Knock "
                                   "-> event test.Knock (delayed) reaches test.Echo/on test.Knock: test.Log.last"));
    }
    SECTION("a same-event chain conflicts at its deeper generation") {
        const auto messages = commuting_errors(occurrence_events() + emit_rule("A", "tick", {"Ping"}) +
                                               emit_rule("B", "tick", {"Ping"}) + emit_rule("Forward", "Ping", {"Pong"}) +
                                               log_consumer("Pong as s", "        log.last = s.line\n"));
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("test.A/on test.tick via event test.Ping -> event test.Pong reaches "
                                   "test.Consume/on test.Pong; test.B/on test.tick via event test.Ping -> event test.Pong "
                                   "reaches test.Consume/on test.Pong: test.Log.last"));
    }
}

TEST_CASE("Channel conflicts: unordered save and restore requests race on the persistence boundary",
          "[semantic][handler-execution-graph][channels][native-result-ordering]") {
    // The requesting rules live in std.persistence itself, so its events need no qualifier.
    const std::string persistence = "module std.persistence\n"
                                    "pub extern event go\n"
                                    "pub event SaveRequested:\n"
                                    "    slot: string\n"
                                    "    request_id: int\n"
                                    "pub event RestoreRequested:\n"
                                    "    slot: string\n"
                                    "    request_id: int\n";
    const auto request = [](const std::string& event) {
        return "        emit " + event + ":\n            slot = \"s\"\n            request_id = 1\n";
    };
    const auto rule = [](const std::string& name, const std::string& body, const std::string& after = "") {
        return "rule " + name + ":\n" + (after.empty() ? "" : "    after:\n        " + after + "\n") + "    on go:\n" +
               body;
    };

    SECTION("two unordered save requests") {
        const auto messages = analyze_error_messages(persistence + rule("A", request("SaveRequested")) +
                                                     rule("B", request("SaveRequested")));
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("'std.persistence.A/on std.persistence.go' and 'std.persistence.B/on std.persistence.go'"));
        CHECK(messages[0].contains("reaches std.persistence.Runtime/on std.persistence.SaveRequested"));
        CHECK(messages[0].contains("effect persistence"));
    }
    SECTION("an unordered save and restore") {
        const auto messages = analyze_error_messages(persistence + rule("A", request("SaveRequested")) +
                                                     rule("B", request("RestoreRequested")));
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("reaches std.persistence.Runtime/on std.persistence.RestoreRequested"));
    }
    SECTION("ordered requests") {
        CHECK(analyze_error_messages(persistence + rule("A", request("SaveRequested")) +
                                     rule("B", request("RestoreRequested"), "A"))
                  .empty());
    }
    SECTION("one rule's requests") {
        CHECK(analyze_error_messages(persistence + rule("A", request("SaveRequested") + request("RestoreRequested")))
                  .empty());
    }
}

// ── Unorientable conflicts (handler-execution-graph) ───────────────────────

TEST_CASE("Unorientable conflicts: the diagnostic names both sides and both fixes",
          "[semantic][handler-execution-graph][unorientable]") {
    const auto messages = commuting_errors(tick_rule("Regen", "Health as h", "        h.hp = h.max_hp\n") +
                                           tick_rule("Poison", "Health as h", "        h.hp -= 1\n"));
    REQUIRE(messages.size() == 1);
    CHECK(messages[0] ==
          "unorientable conflict between 'test.Poison/on test.tick' and 'test.Regen/on test.tick' on "
          "test.Health.hp (Poison: Accum, Regen: Assign); nothing orders them: add 'after: Regen/on tick' to the "
          "Poison/on tick handler, or 'after: Poison/on tick' to the Regen/on tick handler");
}

TEST_CASE("Unorientable conflicts: a read is named as a read", "[semantic][handler-execution-graph][unorientable]") {
    const auto messages = commuting_errors(
        tick_rule("Copy", "P as p", "        p.x = p.v\n") + tick_rule("Swap", "P as p", "        p.v = p.x\n"));
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].contains("test.P.v (Copy: read, Swap: Assign), test.P.x (Copy: Assign, Swap: read)"));
}

TEST_CASE("Unorientable conflicts: channels and effects are named", "[semantic][handler-execution-graph][unorientable]") {
    SECTION("commands") {
        const auto messages = commuting_errors(tick_rule("A", "Health as h", "        add Stunned\n") +
                                               tick_rule("B", "Health as h", "        remove Stunned\n"));
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("on commands test.Stunned: add / remove;"));
    }
    SECTION("events") {
        const auto messages = commuting_errors(tick_rule("A", "", "        emit Spoke:\n            line = 1\n") +
                                               tick_rule("B", "", "        emit Spoke:\n            line = 2\n") +
                                               log_consumer("Spoke as s", "        log.last = s.line\n"));
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("'test.A/on test.tick' and 'test.B/on test.tick' on test.A/on test.tick via event "
                                   "test.Spoke reaches test.Consume/on test.Spoke;"));
    }
    SECTION("effects") {
        const auto messages = analyze_error_messages(stdlib_events() +
                                                     "extern rule DrawA:\n"
                                                     "    on tick:\n"
                                                     "        effects:\n"
                                                     "            graphics\n"
                                                     "extern rule DrawB:\n"
                                                     "    on tick:\n"
                                                     "        effects:\n"
                                                     "            graphics\n");
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].contains("on effect graphics;"));
    }
}

TEST_CASE("Unorientable conflicts: shared effects follow an explicit order",
          "[semantic][handler-execution-graph][unorientable]") {
    const auto program = analyze(stdlib_events() +
                                 "extern rule DrawA:\n"
                                 "    on tick:\n"
                                 "        effects:\n"
                                 "            graphics\n"
                                 "extern rule DrawB:\n"
                                 "    after:\n"
                                 "        DrawA\n"
                                 "    on tick:\n"
                                 "        effects:\n"
                                 "            graphics\n");
    const auto* effect = edge_of_kind(edges_from(program, "DrawA", "DrawB"), ScheduleEdgeKind::EffectConflict);
    REQUIRE(effect != nullptr);
    CHECK(effect->orientation == ScheduleEdgeOrientation::Explicit);
}

TEST_CASE("Unorientable conflicts: every conflict is reported", "[semantic][handler-execution-graph][unorientable]") {
    const auto messages = commuting_errors(tick_rule("A1", "T as t", "        t.x = 1.0\n") +
                                           tick_rule("A2", "T as t", "        t.x = 2.0\n") +
                                           tick_rule("B1", "Log as l", "        l.last = 1\n") +
                                           tick_rule("B2", "Log as l", "        l.last = 2\n") +
                                           tick_rule("C1", "Health as h", "        add Stunned\n") +
                                           tick_rule("C2", "Health as h", "        remove Stunned\n"));
    REQUIRE(messages.size() == 3);
    CHECK(messages[0].contains("'test.A1/on test.tick' and 'test.A2/on test.tick'"));
    CHECK(messages[1].contains("'test.B1/on test.tick' and 'test.B2/on test.tick'"));
    CHECK(messages[2].contains("'test.C1/on test.tick' and 'test.C2/on test.tick'"));
}
TEST_CASE("Unorientable conflicts: a trigger from another module is suggested in full",
          "[semantic][handler-execution-graph][unorientable]") {
    ImportedSymbols other;
    other.module_name = "other";
    ResolvedTrait stunned;
    stunned.name         = "Stunned";
    stunned.module_name  = "other";
    stunned.is_pub       = true;
    const auto symbol    = make_symbol_id(SymbolKind::Trait, "other", "Stunned");
    stunned.symbol_id    = symbol;
    stunned.canonical_id = make_canonical_id(symbol);
    other.traits["Stunned"] = std::move(stunned);
    ModuleImports imports;
    imports.modules["hp"] = std::move(other);

    const auto consumer = [](const std::string& name, int value) {
        return "rule " + name + ":\n    each:\n        Log as log\n    on added hp.Stunned:\n        log.last = " +
               std::to_string(value) + "\n";
    };
    const auto source = "module test\n" + commuting_prelude() + consumer("A", 1) + consumer("B", 2);
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(ast, imports);
    REQUIRE(errors.diagnostics().size() == 1);
    CHECK(errors.diagnostics().front().message.contains("'after: B/on added other.Stunned'"));
}

// ── Native field reads (handler-contracts) ──────────────────────────────────

static std::string native_read_source(const std::string& rules) {
    return "module std.transform.flat\n" + stdlib_events() +
           "pub trait WorldTransform:\n"
           "    var position: vec2 = vec2(0.0, 0.0)\n"
           "    var rotation: float = 0.0\n"
           "pub trait Seen:\n"
           "    var x: float = 0.0\n"
           "pub extern func world_position(of: entity_id) vec2\n"
           "entity Mover:\n"
           "    WorldTransform\n" +
           rules;
}

static const std::string& native_move_rule() {
    static const std::string value = "rule Move:\n"
                                     "    each:\n"
                                     "        WorldTransform as t\n"
                                     "    on tick:\n"
                                     "        t.position.x = t.position.x + 1.0\n";
    return value;
}

static const std::string& native_look_rule() {
    static const std::string value = "rule Look:\n"
                                     "    each:\n"
                                     "        Seen as seen\n"
                                     "    on tick:\n"
                                     "        seen.x = world_position(Mover).x\n";
    return value;
}

TEST_CASE("Native field reads: a world_position call reads WorldTransform.position",
          "[semantic][handler-contracts][native-field-reads]") {
    const auto program = analyze(native_read_source(native_look_rule()));
    const auto world   = make_symbol_id(SymbolKind::Trait, "std.transform.flat", "WorldTransform");
    REQUIRE(program.handler_contracts.size() == 1);
    const auto& contract = program.handler_contracts[0];
    CHECK(contract.reads.contains(world));
    REQUIRE(contract.read_fields.contains(world));
    CHECK_FALSE(contract.read_fields.at(world).all);
    CHECK(contract.read_fields.at(world).fields == std::set<std::string>{"position"});
}

TEST_CASE("Native field reads: the writer runs before a native reader in either declaration order",
          "[semantic][handler-execution-graph][native-field-reads]") {
    for (const auto& rules : {native_move_rule() + native_look_rule(), native_look_rule() + native_move_rule()}) {
        const auto program = analyze(native_read_source(rules));
        CHECK(has_reason(edges_from(program, "Move", "Look"), ScheduleEdgeOrientation::WriterBeforeReader));
        CHECK(edges_from(program, "Look", "Move").empty());
    }
}

TEST_CASE("Native field reads: a two-way native conflict needs an explicit order",
          "[semantic][handler-execution-graph][unorientable][native-field-reads]") {
    const auto messages = analyze_error_messages(native_read_source("rule A:\n"
                                                                    "    each:\n"
                                                                    "        WorldTransform as t\n"
                                                                    "        Seen as seen\n"
                                                                    "    on tick:\n"
                                                                    "        t.position.x = seen.x\n" +
                                                                    native_look_rule()));
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].starts_with("unorientable conflict between 'std.transform.flat.A/on std.transform.flat.tick' "
                                  "and 'std.transform.flat.Look/on std.transform.flat.tick'"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
