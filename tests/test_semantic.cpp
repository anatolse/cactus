// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

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
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + R"(trait Pos:
    var x: float
trait Motion:
    var speed: float
template Projectile(speed: float):
    Motion:
        speed = speed
rule Fire:
    filter:
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
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + R"(trait Source:
    var value: int
trait Data:
    var value: int
template Made(value: int):
    Data:
        value = value
rule R:
    filter:
        Source as s
    on tick:
        spawn Made(value = s.value + 1)
)"));
}

// A trait read reaching the scheduler only through an argument still has to
// order this handler after the one that writes it.
TEST_CASE("Semantic: spawn argument trait reads reach the handler contract", "[semantic][template-parameters]") {
    auto program = analyze(STDLIB_EVENTS + R"(trait Source:
    var value: int
trait Data:
    var value: int
template Made(value: int):
    Data:
        value = value
rule Reader:
    filter:
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

TEST_CASE("Semantic: extern event cannot be emitted by authored code — error", "[semantic][persistence]") {
    // Mirrors std.persistence's SaveCompleted/SaveFailed: an extern event is
    // runtime-injected only, the same restriction std.core.frame already has.
    CHECK(analyze_has_errors(STDLIB_EVENTS + "extern event SaveCompleted:\n"
                                             "    slot: string\n"
                                             "rule Bad:\n"
                                             "    on tick:\n"
                                             "        emit SaveCompleted:\n"
                                             "            slot = \"x\"\n"));
}

TEST_CASE("Semantic: extern event can still be handled by authored code — ok", "[semantic][persistence]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "extern event SaveCompleted:\n"
                                                   "    slot: string\n"
                                                   "trait Pos:\n"
                                                   "    var x: float\n"
                                                   "rule Handle:\n"
                                                   "    filter:\n"
                                                   "        Pos\n"
                                                   "    on SaveCompleted as outcome:\n"
                                                   "        x = 0.0\n"));
}

TEST_CASE("Semantic: restore's extern event cannot be emitted by authored code — error",
          "[semantic][persistence]") {
    // Same rule as SaveCompleted/SaveFailed above, applied to RestoreCompleted:
    // the restriction is generic to extern events, not specific to save.
    CHECK(analyze_has_errors(STDLIB_EVENTS + "extern event RestoreCompleted:\n"
                                             "    slot: string\n"
                                             "rule Bad:\n"
                                             "    on tick:\n"
                                             "        emit RestoreCompleted:\n"
                                             "            slot = \"x\"\n"));
}

TEST_CASE("Semantic: restore's extern event can still be handled by authored code — ok",
          "[semantic][persistence]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "extern event RestoreCompleted:\n"
                                                   "    slot: string\n"
                                                   "trait Pos:\n"
                                                   "    var x: float\n"
                                                   "rule Handle:\n"
                                                   "    filter:\n"
                                                   "        Pos\n"
                                                   "    on RestoreCompleted as outcome:\n"
                                                   "        x = 0.0\n"));
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

TEST_CASE("Semantic: order by valid color channel", "[semantic][rule-order-by][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Tint:\n"
                                                   "    var tint: color\n"
                                                   "rule Render:\n"
                                                   "    filter:\n"
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
    auto message = analyze_first_error(STDLIB_EVENTS +
                                       "trait Tint:\n"
                                       "    var tint: color\n"
                                       "rule Render:\n"
                                       "    filter:\n"
                                       "        Tint as t\n"
                                       "    order by:\n"
                                       "        t.tint.w asc\n"
                                       "    on tick:\n"
                                       "        let x = 1\n");
    CHECK(message.find("t.tint.w") != std::string::npos);
}

TEST_CASE("Semantic: impure order by sort key is rejected", "[semantic][rule-order-by]") {
    auto err = analyze_first_error(STDLIB_EVENTS +
                                   "trait Health:\n"
                                   "    var value: int\n"
                                   "template Marker:\n"
                                   "    Health:\n"
                                   "        value = 1\n"
                                   "rule Render:\n"
                                   "    filter:\n"
                                   "        Health as h\n"
                                   "    order by:\n"
                                   "        spawn Marker:\n"
                                   "            Health:\n"
                                   "                value = 1\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("must be pure") != std::string::npos);
}

TEST_CASE("Semantic: order by accepts a computed sort key over a single filter alias", "[semantic][rule-order-by]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Health:\n"
                                                   "    var value: int\n"
                                                   "func doubled(v: int) int:\n"
                                                   "    return v * 2\n"
                                                   "rule Render:\n"
                                                   "    filter:\n"
                                                   "        Health as h\n"
                                                   "    order by:\n"
                                                   "        doubled(h.value) desc\n"
                                                   "    on tick:\n"
                                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: order by rejects a computed sort key with a non-scalar result type",
          "[semantic][rule-order-by]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                             "    var pos: vec2\n"
                                             "func identity(v: vec2) vec2:\n"
                                             "    return v\n"
                                             "rule Render:\n"
                                             "    filter:\n"
                                             "        Position as p\n"
                                             "    order by:\n"
                                             "        identity(p.pos) asc\n"
                                             "    on tick:\n"
                                             "        let x = 1\n"));
}

TEST_CASE("Semantic: order by without filter or pairs reports the specific diagnostic", "[semantic][rule-order-by]") {
    auto err = analyze_first_error(STDLIB_EVENTS +
                                   "rule NoDomain:\n"
                                   "    order by:\n"
                                   "        1\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("`order by:` requires a `filter:` or `pairs:` clause") != std::string::npos);
}

TEST_CASE("Semantic: order by accepts a cross-binding computed pair sort key", "[semantic][rule-order-by][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                                   "    var pos: vec2\n"
                                                   "rule Contacts:\n"
                                                   "    pairs:\n"
                                                   "        actor:\n"
                                                   "            Position\n"
                                                   "        victim:\n"
                                                   "            Position\n"
                                                   "    order by:\n"
                                                   "        actor.Position.pos.x - victim.Position.pos.x desc\n"
                                                   "    on fixed_tick:\n"
                                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: order by rejects a pair sort key naming an undeclared binding",
          "[semantic][rule-order-by][pair-relations]") {
    auto err = analyze_first_error(STDLIB_EVENTS +
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
    CHECK(err.find("pair binding") != std::string::npos);
}

// Regression guard (task 4.3): only order by: was carved out of pairs:'s
// unary-clause exclusion — filter:/exclude: combined with pairs: must still
// be rejected end to end.
TEST_CASE("Semantic: pairs combined with filter is still rejected", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                             "    var pos: vec2\n"
                                             "rule Bad:\n"
                                             "    pairs:\n"
                                             "        actor:\n"
                                             "            Position\n"
                                             "        victim:\n"
                                             "            Position\n"
                                             "    filter:\n"
                                             "        Position\n"
                                             "    on fixed_tick:\n"
                                             "        let x = 1\n"));
}

TEST_CASE("Semantic: pairs combined with exclude is still rejected", "[semantic][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "trait Position:\n"
                                             "    var pos: vec2\n"
                                             "rule Bad:\n"
                                             "    pairs:\n"
                                             "        actor:\n"
                                             "            Position\n"
                                             "        victim:\n"
                                             "            Position\n"
                                             "    exclude:\n"
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
    CHECK(message.find("vec2") != std::string::npos);
    CHECK(message.find('3') != std::string::npos);
}

TEST_CASE("Semantic: vec3 wrong constructor arity rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(
        "trait Position:\n"
        "    var pos: vec3 = vec3(1.0, 2.0)\n");
    CHECK(message.find("vec3") != std::string::npos);
    CHECK(message.find('2') != std::string::npos);
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
    CHECK(message.find("string") != std::string::npos);
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

    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "rule IntArgs:\n"
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
    CHECK(message.find("string") != std::string::npos);
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
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "rule Test:\n"
                                                   "    on tick:\n"
                                                   "        let c = color(1.0, 0.0, 0.0, 1.0)\n"));
}

TEST_CASE("Semantic: color wrong constructor arity rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(STDLIB_EVENTS +
                                       "rule Test:\n"
                                       "    on tick:\n"
                                       "        let c = color(1.0, 0.0, 0.0)\n");
    CHECK(message.find("color") != std::string::npos);
    CHECK(message.find('3') != std::string::npos);
}

TEST_CASE("Semantic: color non-numeric constructor argument rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(STDLIB_EVENTS +
                                       "rule Test:\n"
                                       "    on tick:\n"
                                       "        let c = color(\"0\", 0.0, 0.0, 1.0)\n");
    CHECK(message.find("string") != std::string::npos);
}

TEST_CASE("Semantic: color constructor accepts int arguments, promoted to float", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "rule Test:\n"
                                                   "    on tick:\n"
                                                   "        for k in range(0, 1):\n"
                                                   "            let c = color(k, 0, 0, 1)\n"));
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

static const std::string COLOR_MATRIX_STDLIB =
    "trait Tint:\n"
    "    var primary: color\n"
    "    var secondary: color\n";

TEST_CASE("Semantic: color component addition and subtraction typecheck", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + COLOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Tint as t\n"
                                   "    on tick:\n"
                                   "        let sum = t.primary + t.secondary\n"
                                   "        let diff = t.primary - t.secondary\n"));
}

TEST_CASE("Semantic: color scalar multiply is commutative in both orders", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + COLOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Tint as t\n"
                                   "    on tick:\n"
                                   "        let a = t.primary * tick.dt\n"
                                   "        let b = tick.dt * t.primary\n"));
}

TEST_CASE("Semantic: color divide by float typechecks", "[semantic][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + COLOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Tint as t\n"
                                   "    on tick:\n"
                                   "        let a = t.primary / tick.dt\n"));
}

TEST_CASE("Semantic: color component-wise multiply stays color-typed, not a dot product",
          "[semantic][vector-expressions]") {
    // Same reasoning as the vec2/vec3 case above: if `t.primary * t.secondary`
    // inferred as some scalar, adding it to another color below would be
    // rejected (no such row) - accepting it proves the result stayed `color`.
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + COLOR_MATRIX_STDLIB +
                                   "rule Test:\n"
                                   "    filter:\n"
                                   "        Tint as t\n"
                                   "    on tick:\n"
                                   "        let component_product = t.primary * t.secondary + t.primary\n"));
}

TEST_CASE("Semantic: color plus bare scalar rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(STDLIB_EVENTS + COLOR_MATRIX_STDLIB +
                                       "rule Test:\n"
                                       "    filter:\n"
                                       "        Tint as t\n"
                                       "    on tick:\n"
                                       "        let bad = t.primary + tick.dt\n");
    CHECK(message.find("color") != std::string::npos);
    CHECK(message.find("float") != std::string::npos);
    CHECK(message.find('+') != std::string::npos);
}

TEST_CASE("Semantic: color-by-color division rejected", "[semantic][vector-expressions]") {
    auto message = analyze_first_error(STDLIB_EVENTS + COLOR_MATRIX_STDLIB +
                                       "rule Test:\n"
                                       "    filter:\n"
                                       "        Tint as t\n"
                                       "    on tick:\n"
                                       "        let bad = t.primary / t.secondary\n");
    CHECK(message.find("color") != std::string::npos);
    CHECK(message.find('/') != std::string::npos);
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
                                   "        var accel = vec2(0.0)\n"
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

TEST_CASE("Semantic: compound-assignment diagnostic names target and source types", "[semantic][vector-expressions]") {
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

static std::vector<std::string> analyze_messages(const std::string& source) {
    const std::string src = "module test\n" + STDLIB_EVENTS + source;
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
    return std::ranges::any_of(messages, [&](const std::string& m) { return m.find(text) != std::string::npos; });
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

static const std::string ENTITY_ID_TRAITS = "trait Health:\n"
                                            "    var hp: int = 10\n"
                                            "trait Player\n"
                                            "trait Solid\n";

static std::string entity_id_read_error(const std::string& trait, const std::string& object) {
    return "can't read trait '" + trait + "' through entity_id '" + object +
           "'; read another entity's traits in a pairs: rule";
}

static std::size_t count_message(const std::vector<std::string>& messages, const std::string& text) {
    return static_cast<std::size_t>(
        std::ranges::count_if(messages, [&](const std::string& m) { return m.find(text) != std::string::npos; }));
}

TEST_CASE("Semantic: trait read through an entity_id local is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule R:\n"
                                                              "    on tick:\n"
                                                              "        let e = query.first[Player]()\n"
                                                              "        let hp = e.Health.hp\n");
    CHECK(count_message(messages, entity_id_read_error("Health", "e")) == 1);
}

TEST_CASE("Semantic: trait read through a loop variable names a module-qualified trait",
          "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule R:\n"
                                                              "    on tick:\n"
                                                              "        for wall in query.all[Solid]():\n"
                                                              "            let hp = wall.test.Health.hp\n");
    CHECK(count_message(messages, entity_id_read_error("test.Health", "wall")) == 1);
}

TEST_CASE("Semantic: read of a missing field through entity_id is still an entity_id access error",
          "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule R:\n"
                                                              "    on tick:\n"
                                                              "        let e = query.first[Player]()\n"
                                                              "        let hp = e.Health.nonexistent\n");
    CHECK(has_message(messages, entity_id_read_error("Health", "e")));
}

TEST_CASE("Semantic: unknown trait read through entity_id names the first member", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule R:\n"
                                                              "    on tick:\n"
                                                              "        let e = query.first[Player]()\n"
                                                              "        let x = e.Bogus.x\n");
    CHECK(has_message(messages, entity_id_read_error("Bogus", "e")));
}

TEST_CASE("Semantic: trait read through an event field is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "event Contact:\n"
                                                              "    other: entity_id\n"
                                                              "rule R:\n"
                                                              "    on Contact as contact:\n"
                                                              "        let hp = contact.other.Health.hp\n");
    CHECK(count_message(messages, entity_id_read_error("Health", "contact.other")) == 1);
}

TEST_CASE("Semantic: trait read through self is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule R:\n"
                                                              "    on tick:\n"
                                                              "        let hp = self.Health.hp\n");
    CHECK(count_message(messages, entity_id_read_error("Health", "self")) == 1);
}

TEST_CASE("Semantic: trait read through self in a where clause is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule R:\n"
                                                              "    filter:\n"
                                                              "        Player\n"
                                                              "    where:\n"
                                                              "        self.Health.hp > 0\n"
                                                              "    on tick:\n"
                                                              "        let x = 1\n");
    CHECK(has_message(messages, entity_id_read_error("Health", "self")));
}

TEST_CASE("Semantic: trait read through a func parameter is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "func f(e: entity_id) int:\n"
                                                              "    return e.Health.hp\n");
    CHECK(has_message(messages, entity_id_read_error("Health", "e")));
}

TEST_CASE("Semantic: compound trait write through a loop variable is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule R:\n"
                                                              "    on tick:\n"
                                                              "        for e in query.all[Health]():\n"
                                                              "            e.Health.hp += 1\n");
    CHECK(has_message(messages, "can't write trait 'Health' through entity_id 'e'; use 'set Health on e:'"));
    CHECK_FALSE(has_message(messages, "is read-only"));
    CHECK(messages.size() == 1);
}

TEST_CASE("Semantic: write to an undeclared trait through entity_id is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule R:\n"
                                                              "    on tick:\n"
                                                              "        var e = query.first[Player]()\n"
                                                              "        e.Bogus.x = 1\n");
    CHECK(has_message(messages, "can't write trait 'Bogus' through entity_id 'e'; use 'set Bogus on e:'"));
    CHECK(messages.size() == 1);
}

TEST_CASE("Semantic: trait write through an event field is rejected", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "event Contact:\n"
                                                              "    other: entity_id\n"
                                                              "rule R:\n"
                                                              "    on Contact as contact:\n"
                                                              "        contact.other.Health.hp = 1\n");
    CHECK(has_message(
        messages, "can't write trait 'Health' through entity_id 'contact.other'; use 'set Health on contact.other:'"));
}

TEST_CASE("Semantic: pair binding, filter alias and trait match access stay legal", "[semantic][entity-id-access]") {
    const auto messages = analyze_messages(ENTITY_ID_TRAITS + "rule P:\n"
                                                              "    pairs:\n"
                                                              "        body:\n"
                                                              "            Health\n"
                                                              "        other:\n"
                                                              "            Health\n"
                                                              "    where:\n"
                                                              "        body.Health.hp > other.Health.hp\n"
                                                              "    on tick:\n"
                                                              "        let hp = body.Health.hp\n"
                                                              "rule F:\n"
                                                              "    filter:\n"
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
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "rule CountUp:\n"
                                                   "    on tick:\n"
                                                   "        for k in range(0, 5):\n"
                                                   "            let doubled = k * 2\n"));

    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "rule CountDown:\n"
                                                   "    on tick:\n"
                                                   "        for k in range(5, 0, -1):\n"
                                                   "            let doubled = k * 2\n"));

    // Omitted step type-checks identically to an explicit step of 1.
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "rule DefaultStep:\n"
                                                   "    on tick:\n"
                                                   "        for k in range(0, 3):\n"
                                                   "            let doubled = k * 2\n"));
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + "rule ExplicitStep:\n"
                                                   "    on tick:\n"
                                                   "        for k in range(0, 3, 1):\n"
                                                   "            let doubled = k * 2\n"));
}

TEST_CASE("Semantic: range() rejected outside a for-loop iterable position", "[semantic][foreach][range]") {
    CHECK(analyze_first_error(STDLIB_EVENTS + "rule BadRangeLet:\n"
                                              "    on tick:\n"
                                              "        let r = range(0, 10)\n") ==
          "`range()` is only valid as the iterable of a `for` statement");

    CHECK(analyze_first_error(STDLIB_EVENTS + "rule BadRangeArg:\n"
                                              "    on tick:\n"
                                              "        let v = vec2(range(0, 3), 0)\n") ==
          "`range()` is only valid as the iterable of a `for` statement");
}

TEST_CASE("Semantic: range() validates argument count and int-typed arguments", "[semantic][foreach][range]") {
    CHECK(analyze_first_error(STDLIB_EVENTS + "rule TooFewArgs:\n"
                                              "    on tick:\n"
                                              "        for k in range(0):\n"
                                              "            let x = k\n")
              .find("'range'") != std::string::npos);

    CHECK(analyze_first_error(STDLIB_EVENTS + "rule TooManyArgs:\n"
                                              "    on tick:\n"
                                              "        for k in range(0, 1, 2, 3):\n"
                                              "            let x = k\n")
              .find("'range'") != std::string::npos);

    CHECK(analyze_first_error(STDLIB_EVENTS + "rule NonIntArg:\n"
                                              "    on tick:\n"
                                              "        for k in range(0, 3.0):\n"
                                              "            let x = k\n")
              .find("'range'") != std::string::npos);
}

TEST_CASE("Semantic: C-style numeric for, while, and break/continue remain unsupported", "[semantic][foreach]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + "rule NumericFor:\n"
                                             "    on tick:\n"
                                             "        for i = 0; i < 10; i += 1:\n"
                                             "            let x = i\n"));

    CHECK(analyze_has_errors(STDLIB_EVENTS + "rule WhileLoop:\n"
                                             "    on tick:\n"
                                             "        while true:\n"
                                             "            let x = 1\n"));

    CHECK(analyze_has_errors(STDLIB_EVENTS + "rule BreakStmt:\n"
                                             "    on tick:\n"
                                             "        for k in range(0, 3):\n"
                                             "            break\n"));

    CHECK(analyze_has_errors(STDLIB_EVENTS + "rule ContinueStmt:\n"
                                             "    on tick:\n"
                                             "        for k in range(0, 3):\n"
                                             "            continue\n"));
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

TEST_CASE("Semantic: entity name is an entity_id expression", "[semantic][entity]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS +
                                   "trait Shape:\n"
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

TEST_CASE("Semantic: pair binding color channel read is accepted and typed",
          "[semantic][pair-relations][vector-expressions]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
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
                                   "        if body.Tint.tint.a > 0.5:\n"
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

// handler-contracts: an order by: key's reads must reach the contract with the
// same precision as a body read, so scheduling sees identical dependency data
// whether a trait is touched in order by:, where:, or the handler body. The
// computed key here reads Collider through a nested call the body never
// mentions, so a flat root-alias lookup would miss it.
TEST_CASE("Semantic: unary handler contract folds reads from a computed order by key",
          "[semantic][rule-order-by][handler-contracts]") {
    auto result = analyze(STDLIB_EVENTS + PAIR_TRAITS +
                          "func doubled(v: int) int:\n"
                          "    return v * 2\n"
                          "rule Ranked:\n"
                          "    filter:\n"
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
    auto result = analyze(STDLIB_EVENTS + PAIR_TRAITS +
                          "rule Ranked:\n"
                          "    pairs:\n"
                          "        actor:\n"
                          "            Transform\n"
                          "        victim:\n"
                          "            Transform\n"
                          "    order by:\n"
                          "        actor.Transform.x - victim.Transform.x desc\n"
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

// ── Where clause (dsl-where-clause) ─────────────────────────────────────────

TEST_CASE("Semantic: bool-typed where predicate is accepted", "[semantic][where-clause]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                                   "rule Moving:\n"
                                   "    filter:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        body.vx > 0.0\n"
                                   "    on tick:\n"
                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: non-bool where predicate is rejected with a type error", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Moving:\n"
                             "    filter:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        body.vx\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: pure user func call in where is accepted", "[semantic][where-clause]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                                   "func is_positive(v: float) bool:\n"
                                   "    return v > 0.0\n"
                                   "rule Moving:\n"
                                   "    filter:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        is_positive(body.vx)\n"
                                   "    on tick:\n"
                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: emit in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Moving:\n"
                             "    filter:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        emit Contact\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: world query in where is rejected", "[semantic][where-clause]") {
    auto err = analyze_first_error("use std.query as query\n" + STDLIB_EVENTS + PAIR_TRAITS +
                                   "rule Moving:\n"
                                   "    filter:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        query.exists[Solid]()\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("must be pure") != std::string::npos);
}

TEST_CASE("Semantic: spawn in where is rejected", "[semantic][where-clause]") {
    auto err = analyze_first_error(STDLIB_EVENTS + PAIR_TRAITS +
                                   "template Marker:\n"
                                   "    Solid:\n"
                                   "        active = true\n"
                                   "rule Moving:\n"
                                   "    filter:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        spawn Marker:\n"
                                   "            Solid:\n"
                                   "                active = true\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("must be pure") != std::string::npos);
}

TEST_CASE("Semantic: destroy in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Moving:\n"
                             "    filter:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        destroy\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: add in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Moving:\n"
                             "    filter:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        add Solid\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: remove in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Moving:\n"
                             "    filter:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        remove Solid\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: project in where is rejected", "[semantic][where-clause]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + PAIR_TRAITS +
                             "rule Moving:\n"
                             "    filter:\n"
                             "        DynamicBody as body\n"
                             "    where:\n"
                             "        project Solid\n"
                             "    on tick:\n"
                             "        let x = 1\n"));
}

TEST_CASE("Semantic: call to extern func without a proven-pure effect summary is rejected in where",
          "[semantic][where-clause]") {
    auto err = analyze_first_error(STDLIB_EVENTS + PAIR_TRAITS +
                                   "pub extern func mystery(v: float) float\n"
                                   "rule Moving:\n"
                                   "    filter:\n"
                                   "        DynamicBody as body\n"
                                   "    where:\n"
                                   "        mystery(body.vx) > 0.0\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("must be pure") != std::string::npos);
}

TEST_CASE("Semantic: where on rule with neither filter nor pairs is rejected", "[semantic][where-clause]") {
    auto err = analyze_first_error(STDLIB_EVENTS +
                                   "rule NoDomain:\n"
                                   "    where:\n"
                                   "        true\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("where:") != std::string::npos);
    CHECK(err.find("filter") != std::string::npos);
}

TEST_CASE("Semantic: unary handler contract from where matches equivalent leading if guard",
          "[semantic][where-clause][handler-contracts]") {
    auto where_result = analyze(STDLIB_EVENTS + PAIR_TRAITS +
                                "rule MovingWhere:\n"
                                "    filter:\n"
                                "        DynamicBody as body\n"
                                "    where:\n"
                                "        body.vx > 0.0\n"
                                "    on tick:\n"
                                "        let x = 1\n");
    auto if_result    = analyze(STDLIB_EVENTS + PAIR_TRAITS +
                                "rule MovingIf:\n"
                                "    filter:\n"
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
    auto where_result = analyze(STDLIB_EVENTS + PAIR_TRAITS +
                                "rule ContactWhere:\n"
                                "    pairs:\n"
                                "        a:\n"
                                "            Transform\n"
                                "        b:\n"
                                "            Transform\n"
                                "    where:\n"
                                "        a.Transform.x > 0.0\n"
                                "    on tick:\n"
                                "        let x = 1\n");
    auto if_result    = analyze(STDLIB_EVENTS + PAIR_TRAITS +
                                "rule ContactIf:\n"
                                "    pairs:\n"
                                "        a:\n"
                                "            Transform\n"
                                "        b:\n"
                                "            Transform\n"
                                "    on tick:\n"
                                "        if a.Transform.x <= 0.0:\n"
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
    auto result = analyze("module std.collision.flat\n" + STDLIB_EVENTS +
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
                          "        circles_overlap(a.Transform.position, a.Collider.radius, b.Transform.position, "
                          "b.Collider.radius)\n"
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
    auto result = analyze("module std.collision.flat\n" + STDLIB_EVENTS +
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
                          "        circles_overlap(a.Transform.position, a.Collider.radius, b.Transform.position, "
                          "b.Collider.radius)\n"
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

    auto result = analyze_with_imports(STDLIB_EVENTS +
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
                                           "        foo.spheres_overlap(a.Transform.position, a.Collider.radius, "
                                           "b.Transform.position, b.Collider.radius)\n"
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
    auto result = analyze("module std.collision.flat\n" + STDLIB_EVENTS +
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
                          "        not circles_overlap(a.Transform.position, a.Collider.radius, "
                          "b.Transform.position, b.Collider.radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: computed single-binding radius argument is eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.collision.flat\n" + STDLIB_EVENTS +
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
                          "        circles_overlap(a.Transform.position, a.Collider.radius * 2.0, "
                          "b.Transform.position, b.Collider.radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: cross-domain pair rule with a recognized call is eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.collision.flat\n" + STDLIB_EVENTS +
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
                          "        circles_overlap(a.Transform.position, a.Collider.radius, b.Transform.position, "
                          "b.Collider.radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(result.handler_contracts[0].spatial_join.has_value());
}

// ── Shape-generic recognition (accelerate-cross-domain-pair-joins) ─────────

static const std::string VOLUME_OVERLAP_HEADER =
    "module std.collision.volume\n" + STDLIB_EVENTS +
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
    "    return sphere_radius >= 0.0\n";

static std::optional<SpatialJoinPlan> volume_overlap_plan(const std::string& where_predicate) {
    auto result = analyze(VOLUME_OVERLAP_HEADER + "rule Detect:\n"
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
        "sphere_box_overlap(actor.Body.position, actor.Body.radius, wall.Solid.position, wall.Solid.size, "
        "wall.Solid.rotation)");
    REQUIRE(plan.has_value());
    CHECK(plan->dimension == SpatialJoinDimension::Volume3D);
    CHECK(plan->left.binding_index == 0);
    CHECK(plan->left.kind == SpatialShapeKind::Sphere);
    CHECK(plan->left.slots == std::vector<ExprPath>{{0}, {1}});
    CHECK(plan->right.binding_index == 1);
    CHECK(plan->right.kind == SpatialShapeKind::Box);
    CHECK(plan->right.slots == std::vector<ExprPath>{{2}, {3}, {4}});
}

TEST_CASE("Semantic: sphere_box_overlap with the box on the left binding is eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze(VOLUME_OVERLAP_HEADER + "rule Detect:\n"
                                                  "    pairs:\n"
                                                  "        wall:\n"
                                                  "            Solid\n"
                                                  "        actor:\n"
                                                  "            Body\n"
                                                  "    where:\n"
                                                  "        sphere_box_overlap(actor.Body.position, actor.Body.radius, "
                                                  "wall.Solid.position, wall.Solid.size, wall.Solid.rotation)\n"
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
        volume_overlap_plan("spheres_overlap(actor.Body.position, actor.Body.radius, wall.Solid.position, 1.0 + "
                            "wall.Solid.size.x)");
    REQUIRE(plan.has_value());
    CHECK(plan->left.kind == SpatialShapeKind::Sphere);
    CHECK(plan->right.kind == SpatialShapeKind::Sphere);
}

TEST_CASE("Semantic: single-binding expression slots are eligible", "[semantic][where-clause][spatial-join]") {
    CHECK(volume_overlap_plan("sphere_box_overlap(actor.Body.position, actor.Body.radius + PROBE, wall.Solid.position, "
                              "wall.Solid.size, wall.Solid.rotation)")
              .has_value());
    CHECK(volume_overlap_plan("sphere_box_overlap(actor.Body.position, actor.Body.radius + Game.Tuning.margin, "
                              "wall.Solid.position, wall.Solid.size, wall.Solid.rotation)")
              .has_value());
    CHECK(volume_overlap_plan("sphere_box_overlap(actor.Body.position, inflate(actor.Body.radius), "
                              "wall.Solid.position, wall.Solid.size * 1.5, wall.Solid.rotation)")
              .has_value());
}

TEST_CASE("Semantic: an argument mixing both bindings is not eligible", "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("spheres_overlap(actor.Body.position, actor.Body.radius + wall.Solid.size.x, "
                                    "wall.Solid.position, wall.Solid.size.x)")
                    .has_value());
}

TEST_CASE("Semantic: a binding-free argument joins its shape's binding", "[semantic][where-clause][spatial-join]") {
    const auto plan = volume_overlap_plan(
        "sphere_box_overlap(actor.Body.position, PROBE, wall.Solid.position, wall.Solid.size, wall.Solid.rotation)");
    REQUIRE(plan.has_value());
    CHECK(plan->left.binding_index == 0);
    CHECK(plan->left.slots == std::vector<ExprPath>{{0}, {1}});
}

TEST_CASE("Semantic: a shape that reads no pair binding is not eligible", "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("sphere_box_overlap(vec3(0.0, 0.0, 0.0), PROBE, wall.Solid.position, "
                                    "wall.Solid.size, wall.Solid.rotation)")
                    .has_value());
}

TEST_CASE("Semantic: both shapes from one binding are not eligible", "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("spheres_overlap(actor.Body.position, actor.Body.radius, actor.Body.position, "
                                    "actor.Body.radius)")
                    .has_value());
}

TEST_CASE("Semantic: or-combined overlap predicate is not eligible", "[semantic][where-clause][spatial-join]") {
    CHECK_FALSE(volume_overlap_plan("spheres_overlap(actor.Body.position, actor.Body.radius, wall.Solid.position, "
                                    "wall.Solid.size.x) or actor.Body.radius > 1.0")
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
    auto result = analyze("module std.math.vec2\n" + STDLIB_EVENTS +
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
                          "        dot(b.Transform.position - a.Transform.position, b.Transform.position - "
                          "a.Transform.position) < (a.Collider.radius + b.Collider.radius) * (a.Collider.radius + "
                          "b.Collider.radius)\n"
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
    auto result = analyze("module std.math.vec3\n" + STDLIB_EVENTS +
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
                          "        dot(b.Transform.position - a.Transform.position, b.Transform.position - "
                          "a.Transform.position) < (a.Collider.radius + b.Collider.radius) * (a.Collider.radius + "
                          "b.Collider.radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(result.handler_contracts[0].spatial_join->dimension == SpatialJoinDimension::Volume3D);
}

TEST_CASE("Semantic: manual squared-distance-via-dot where: expression accepts <= as well as <",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + STDLIB_EVENTS +
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
                          "        dot(b.Transform.position - a.Transform.position, b.Transform.position - "
                          "a.Transform.position) <= (a.Collider.radius + b.Collider.radius) * (a.Collider.radius + "
                          "b.Collider.radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: manual dot-product recognition succeeds through a renamed import alias",
          "[semantic][where-clause][spatial-join]") {
    ModuleImports imports;
    imports.modules["v2m"] = make_dot_import();

    auto result = analyze_with_imports(STDLIB_EVENTS +
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
                                           "        v2m.dot(b.Transform.position - a.Transform.position, "
                                           "b.Transform.position - a.Transform.position) < (a.Collider.radius + "
                                           "b.Collider.radius) * (a.Collider.radius + b.Collider.radius)\n"
                                           "    on tick:\n"
                                           "        let x = 1\n",
                                       imports);

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(result.handler_contracts[0].spatial_join->dimension == SpatialJoinDimension::Flat2D);
}

TEST_CASE("Semantic: cross-domain pair rule with manual dot-product shape is eligible",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + STDLIB_EVENTS +
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
                          "        dot(b.Transform.position - a.Transform.position, b.Transform.position - "
                          "a.Transform.position) < (a.Collider.radius + b.Collider.radius) * (a.Collider.radius + "
                          "b.Collider.radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: component-wise squared-distance arithmetic remains an ordinary residual predicate",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze(STDLIB_EVENTS +
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
                          "        (b.Transform.x - a.Transform.x) * (b.Transform.x - a.Transform.x) + "
                          "(b.Transform.y - a.Transform.y) * (b.Transform.y - a.Transform.y) < "
                          "(a.Collider.radius + b.Collider.radius) * (a.Collider.radius + b.Collider.radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE(
    "Semantic: distance check split across intermediate handler-body let bindings (no where: clause) "
    "remains unrecognized",
    "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + STDLIB_EVENTS +
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
                          "        let delta = b.Transform.position - a.Transform.position\n"
                          "        let dist_sq = dot(delta, delta)\n"
                          "        let radius_sum = a.Collider.radius + b.Collider.radius\n"
                          "        if dist_sq >= radius_sum * radius_sum:\n"
                          "            return\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
}

TEST_CASE("Semantic: manual dot-product expression with a comparison operator outside {<, <=} is unrecognized",
          "[semantic][where-clause][spatial-join]") {
    auto result = analyze("module std.math.vec2\n" + STDLIB_EVENTS +
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
                          "        dot(b.Transform.position - a.Transform.position, b.Transform.position - "
                          "a.Transform.position) == (a.Collider.radius + b.Collider.radius) * (a.Collider.radius + "
                          "b.Collider.radius)\n"
                          "    on tick:\n"
                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK_FALSE(result.handler_contracts[0].spatial_join.has_value());
}

// ── Unaccelerated linear-distance warning diagnostic (add-sap-broadphase) ───

static bool has_warning_containing(const std::vector<Diagnostic>& diagnostics, const std::string& needle) {
    return std::ranges::any_of(diagnostics, [&needle](const Diagnostic& diagnostic) {
        return diagnostic.level == DiagnosticLevel::Warning && diagnostic.message.find(needle) != std::string::npos;
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

TEST_CASE("Semantic: linear 2D distance-vs-radius-sum where: predicate is flagged with a warning",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        "module std.math.vec2\n" + STDLIB_EVENTS +
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
        "        distance(a.Transform.position, b.Transform.position) < a.Collider.radius + b.Collider.radius\n"
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
        "module std.math.vec3\n" + STDLIB_EVENTS +
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
        "        distance(a.Transform.position, b.Transform.position) >= a.Collider.radius + b.Collider.radius\n"
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
        "module std.math.vec2\n" + STDLIB_EVENTS +
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
        "        distance(a.Transform.position, b.Transform.position) > a.Collider.radius + b.Collider.radius\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(has_warning_containing(diagnostics, "circles_overlap"));
    CHECK(count_warnings(diagnostics) == 1);
}

TEST_CASE("Semantic: pair rule with only unrecognized predicates is flagged as unaccelerated",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(STDLIB_EVENTS +
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
        "module std.collision.flat\n" + STDLIB_EVENTS +
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
        "        circles_overlap(a.Transform.position, a.Collider.radius, b.Transform.position, "
        "b.Collider.radius)\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(diagnostics.empty());
}

TEST_CASE("Semantic: manual dot-product recognized predicate produces no unaccelerated-distance warning",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(
        "module std.math.vec2\n" + STDLIB_EVENTS +
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
        "        dot(b.Transform.position - a.Transform.position, b.Transform.position - a.Transform.position) < "
        "(a.Collider.radius + b.Collider.radius) * (a.Collider.radius + b.Collider.radius)\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(diagnostics.empty());
}
TEST_CASE("Semantic: pair rule without where: is flagged at its pairs: clause",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    const std::string source = "module test\n" + STDLIB_EVENTS +
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
        "module std.collision.flat\n" + STDLIB_EVENTS +
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
        "        circles_overlap(a.Transform.position, a.Collider.radius, b.Transform.position, "
        "b.Collider.radius)\n"
        "    on tick:\n"
        "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    REQUIRE(result.handler_contracts[0].spatial_join.has_value());
    CHECK(diagnostics.empty());
}

TEST_CASE("Semantic: unary rule with where: is never flagged as unaccelerated",
          "[semantic][where-clause][spatial-join][diagnostics]") {
    auto [result, diagnostics] = analyze_with_diagnostics(STDLIB_EVENTS +
                                                          "trait Collider:\n"
                                                          "    var radius: float\n"
                                                          "rule Shrink:\n"
                                                          "    filter:\n"
                                                          "        Collider\n"
                                                          "    where:\n"
                                                          "        Collider.radius > 1.0\n"
                                                          "    on tick:\n"
                                                          "        let x = 1\n");

    REQUIRE(result.handler_contracts.size() == 1);
    CHECK(diagnostics.empty());
}

// ── Limit clause (dsl-rule-limit) ───────────────────────────────────────────

static const std::string LIMIT_TRAITS =
    "trait Actor:\n"
    "    var grounded: bool = false\n"
    "    var height: float = 0.0\n"
    "trait Surface:\n"
    "    var top: float = 0.0\n"
    "trait Tower:\n"
    "    var target_count: int = 1\n";

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
            return rule->limit->provably_one;
        }
    }
    FAIL("no rule with a limit: clause in the analyzed program");
    return false;
}

TEST_CASE("Semantic: limit without filter or pairs reports the specific diagnostic", "[semantic][rule-limit]") {
    auto err = analyze_first_error(STDLIB_EVENTS +
                                   "rule NoDomain:\n"
                                   "    limit: 10\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("`limit:` requires a `filter:` or `pairs:` clause") != std::string::npos);
}

TEST_CASE("Semantic: global limit is accepted on both a unary and a pair domain", "[semantic][rule-limit]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Unary:\n"
                                   "    filter:\n"
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
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Unary:\n"
                                   "    filter:\n"
                                   "        Actor as a\n"
                                   "    limit: 1 per a\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("`per`") != std::string::npos);
}

TEST_CASE("Semantic: per-binding limit naming an undeclared binding is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per enemy\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("pair binding") != std::string::npos);
}

TEST_CASE("Semantic: per-binding limit computed from the per binding's own trait is accepted",
          "[semantic][rule-limit]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        tower:\n"
                                   "            Tower\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: tower.Tower.target_count per tower\n"
                                   "    on tick:\n"
                                   "        let x = 1\n"));
}

TEST_CASE("Semantic: per-binding limit referencing the other binding is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        tower:\n"
                                   "            Tower\n"
                                   "        other:\n"
                                   "            Tower\n"
                                   "    limit: other.Tower.target_count per tower\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("limit") != std::string::npos);
}

TEST_CASE("Semantic: non-int limit count is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Unary:\n"
                                   "    filter:\n"
                                   "        Actor as a\n"
                                   "    limit: 1.5\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("'int'") != std::string::npos);
}

TEST_CASE("Semantic: a global limit count referencing a domain binding is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Paired:\n"
                                   "    pairs:\n"
                                   "        tower:\n"
                                   "            Tower\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: tower.Tower.target_count\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("limit") != std::string::npos);
}

TEST_CASE("Semantic: a global limit count referencing a unary filter alias is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Unary:\n"
                                   "    filter:\n"
                                   "        Actor as a\n"
                                   "    limit: a.height\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("filter binding") != std::string::npos);
}

// Bare unqualified field access is ordinarily legal DSL (dsl-where-clause),
// so the scope check has to deny it here too, not just the alias-qualified
// spelling `a.height` covered above.
TEST_CASE("Semantic: a global limit count referencing a bare filter field is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Unary:\n"
                                   "    filter:\n"
                                   "        Actor as a\n"
                                   "    limit: height\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("filter binding") != std::string::npos);
}

TEST_CASE("Semantic: impure limit count is rejected", "[semantic][rule-limit]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Unary:\n"
                                   "    filter:\n"
                                   "        Actor as a\n"
                                   "    limit: query.count[Surface]()\n"
                                   "    on tick:\n"
                                   "        let x = 1\n");
    CHECK(err.find("must be pure") != std::string::npos);
}

TEST_CASE("Semantic: literal one is provably one", "[semantic][rule-limit]") {
    CHECK(analyze_limit_provably_one(STDLIB_EVENTS + LIMIT_TRAITS +
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
    CHECK(analyze_limit_provably_one(STDLIB_EVENTS + LIMIT_TRAITS +
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
    CHECK_FALSE(analyze_limit_provably_one(STDLIB_EVENTS + LIMIT_TRAITS +
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
    CHECK_FALSE(analyze_limit_provably_one(STDLIB_EVENTS + LIMIT_TRAITS +
                                           "rule Paired:\n"
                                           "    pairs:\n"
                                           "        tower:\n"
                                           "            Tower\n"
                                           "        surface:\n"
                                           "            Surface\n"
                                           "    limit: tower.Tower.target_count per tower\n"
                                           "    on tick:\n"
                                           "        let x = 1\n"));
}

// ── Pair read-only carve-out (dsl-pair-relations) ───────────────────────────

TEST_CASE("Semantic: mutation through a provably-one per binding is accepted", "[semantic][rule-limit][pair-relations]") {
    CHECK_FALSE(analyze_has_errors(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per actor\n"
                                   "    on tick:\n"
                                   "        actor.Actor.grounded = true\n"));
}

TEST_CASE("Semantic: the non-limited binding remains read-only under a per-binding limit",
          "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per actor\n"
                                   "    on tick:\n"
                                   "        surface.Surface.top = 1.0\n");
    CHECK(err.find("read-only") != std::string::npos);
}

TEST_CASE("Semantic: mutation is rejected when the limit is not provably one",
          "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 2 per actor\n"
                                   "    on tick:\n"
                                   "        actor.Actor.grounded = true\n");
    CHECK(err.find("read-only") != std::string::npos);
}

TEST_CASE("Semantic: a global limit grants no pair-binding write access", "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 10\n"
                                   "    on tick:\n"
                                   "        actor.Actor.grounded = true\n");
    CHECK(err.find("read-only") != std::string::npos);
}

TEST_CASE("Semantic: a rule with no limit still rejects mutation on either pair binding",
          "[semantic][rule-limit][pair-relations]") {
    CHECK(analyze_has_errors(STDLIB_EVENTS + LIMIT_TRAITS +
                             "rule Ground:\n"
                             "    pairs:\n"
                             "        actor:\n"
                             "            Actor\n"
                             "        surface:\n"
                             "            Surface\n"
                             "    on tick:\n"
                             "        actor.Actor.grounded = true\n"));
    CHECK(analyze_has_errors(STDLIB_EVENTS + LIMIT_TRAITS +
                             "rule Ground:\n"
                             "    pairs:\n"
                             "        actor:\n"
                             "            Actor\n"
                             "        surface:\n"
                             "            Surface\n"
                             "    on tick:\n"
                             "        surface.Surface.top = 1.0\n"));
}

// The trait-match check stays unconditional: only ordinary dotted-path
// assignment is carved out, never a mutable match alias.
TEST_CASE("Semantic: trait match on a provably-limited binding is still rejected",
          "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
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
    CHECK(err.find("cannot trait-match directly on binding") != std::string::npos);
}

// A bare `actor = ...` has no field to write; only a dotted trait path
// through the provably-one binding is admitted (spec: "only ordinary
// dotted-path assignment is admitted").
TEST_CASE("Semantic: a bare reassignment of a provably-limited binding is still rejected",
          "[semantic][rule-limit][pair-relations]") {
    auto err = analyze_first_error(STDLIB_EVENTS + LIMIT_TRAITS +
                                   "rule Ground:\n"
                                   "    pairs:\n"
                                   "        actor:\n"
                                   "            Actor\n"
                                   "        surface:\n"
                                   "            Surface\n"
                                   "    limit: 1 per actor\n"
                                   "    on tick:\n"
                                   "        actor = surface\n");
    CHECK(err.find("read-only") != std::string::npos);
}

// ── Pair write contract inference (handler-contracts) ───────────────────────

TEST_CASE("Semantic: a provably-limited pair write is recorded in the contract",
          "[semantic][rule-limit][handler-contracts]") {
    auto result = analyze(STDLIB_EVENTS + LIMIT_TRAITS +
                          "rule Ground:\n"
                          "    pairs:\n"
                          "        actor:\n"
                          "            Actor\n"
                          "        surface:\n"
                          "            Surface\n"
                          "    limit: 1 per actor\n"
                          "    on tick:\n"
                          "        actor.Actor.grounded = true\n");

    REQUIRE(result.handler_contracts.size() == 1);
    const auto& contract = result.handler_contracts[0];
    const auto actor_trait = make_symbol_id(SymbolKind::Trait, "test", "Actor");
    CHECK(contract.writes.contains(actor_trait));
    CHECK(contract.reads.contains(actor_trait));
}

TEST_CASE("Semantic: a provably-limited pair write conflicts with a unary read of the same trait",
          "[semantic][rule-limit][handler-contracts]") {
    auto result = analyze(STDLIB_EVENTS + LIMIT_TRAITS +
                          "rule Ground:\n"
                          "    pairs:\n"
                          "        actor:\n"
                          "            Actor\n"
                          "        surface:\n"
                          "            Surface\n"
                          "    limit: 1 per actor\n"
                          "    on tick:\n"
                          "        actor.Actor.grounded = true\n"
                          "rule ReadGround:\n"
                          "    filter:\n"
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

static const FieldAccess ALL_FIELDS{.all = true, .fields = {}};

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

    CHECK(contract.read_fields.at(match) == ALL_FIELDS);
}

TEST_CASE("Contract field access: an undeclared field name falls back to all fields", "[semantic][handler-contracts]") {
    const auto match       = make_symbol_id(SymbolKind::Trait, "test", "Match");
    const auto declaration = match_trait_declaration();
    HandlerContract contract;
    record_read(contract, match, "missing", &declaration);
    CHECK(contract.read_fields.at(match) == ALL_FIELDS);

    HandlerContract undeclared_trait;
    record_read(undeclared_trait, match, "over", nullptr);
    CHECK(undeclared_trait.read_fields.at(match) == ALL_FIELDS);
}

TEST_CASE("Contract field access: a write also records the read of the same field", "[semantic][handler-contracts]") {
    const auto match       = make_symbol_id(SymbolKind::Trait, "test", "Match");
    const auto declaration = match_trait_declaration();
    HandlerContract contract;
    record_read(contract, match, "score", &declaration);
    record_write(contract, match, "over", &declaration);

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
    record_write(contract, match, "score", &declaration);

    CHECK(contract.read_fields.size() == contract.reads.size());
    CHECK(contract.write_fields.size() == contract.writes.size());
    for (const auto& trait : contract.reads) {
        CHECK(contract.read_fields.contains(trait));
    }
    for (const auto& trait : contract.writes) {
        CHECK(contract.write_fields.contains(trait));
    }
}

static const std::string FIELD_TRAITS =
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

static InferredHandlerContract only_contract(const std::string& rules) {
    auto result = analyze(STDLIB_EVENTS + FIELD_TRAITS + rules);
    REQUIRE(result.handler_contracts.size() == 1);
    return result.handler_contracts[0];
}

static SymbolId test_trait(const std::string& name) {
    return make_symbol_id(SymbolKind::Trait, "test", name);
}

TEST_CASE("Contract fields: an alias member read records that field", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule ReadOver:\n"
        "    filter:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        let x = m.over\n");
    CHECK(contract.read_fields.at(test_trait("Match")) == fields_of({"over"}));
    CHECK(contract.write_fields.empty());
}

TEST_CASE("Contract fields: a nested alias member write stops at the trait field", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule Nudge:\n"
        "    filter:\n"
        "        Body as b\n"
        "    on tick:\n"
        "        b.position.x = 1.0\n");
    CHECK(contract.write_fields.at(test_trait("Body")) == fields_of({"position"}));
    CHECK(contract.read_fields.at(test_trait("Body")) == fields_of({"position"}));
}

TEST_CASE("Contract fields: a bare field identifier records its field", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule Check:\n"
        "    filter:\n"
        "        Player\n"
        "    on tick:\n"
        "        let x = game_over\n");
    CHECK(contract.read_fields.at(test_trait("Player")) == fields_of({"game_over"}));
}

TEST_CASE("Contract fields: a bare field assignment records its field", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule End:\n"
        "    filter:\n"
        "        Player\n"
        "    on tick:\n"
        "        game_over = true\n");
    CHECK(contract.write_fields.at(test_trait("Player")) == fields_of({"game_over"}));
}

TEST_CASE("Contract fields: a trait-rooted assignment records the field after the trait",
          "[semantic][handler-contracts]") {
    const auto via_alias = only_contract(
        "rule End:\n"
        "    filter:\n"
        "        Match\n"
        "    on tick:\n"
        "        Match.over = true\n");
    CHECK(via_alias.write_fields.at(test_trait("Match")) == fields_of({"over"}));

    const auto via_leading_trait = only_contract(
        "rule End:\n"
        "    filter:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        foo.Match.over = true\n");
    CHECK(via_leading_trait.write_fields.at(test_trait("Match")) == fields_of({"over"}));
}

TEST_CASE("Contract fields: a trait_for_field fallback assignment covers all fields", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule End:\n"
        "    filter:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        foo.over = true\n");
    CHECK(contract.write_fields.at(test_trait("Match")) == ALL_FIELDS);
    CHECK(contract.read_fields.at(test_trait("Match")) == ALL_FIELDS);
}

TEST_CASE("Contract fields: a bare alias used as a value reads all fields", "[semantic][handler-contracts]") {
    const auto as_argument = only_contract(
        "func pick(e: entity_id) int:\n"
        "    return 1\n"
        "rule Pass:\n"
        "    filter:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        let y = pick(m)\n");
    CHECK(as_argument.read_fields.at(test_trait("Match")) == ALL_FIELDS);
    CHECK(as_argument.write_fields.empty());

    const auto as_local = only_contract(
        "rule Keep:\n"
        "    filter:\n"
        "        Match as m\n"
        "    on tick:\n"
        "        let y = m\n");
    CHECK(as_local.read_fields.at(test_trait("Match")) == ALL_FIELDS);
}

TEST_CASE("Contract fields: a trait-match arm reads all fields of its trait", "[semantic][handler-contracts]") {
    const auto contract = only_contract(
        "rule Strike:\n"
        "    on Hit as h:\n"
        "        match h.other:\n"
        "            Health as victim =>\n"
        "                let x = victim.hp\n");
    CHECK(contract.read_fields.at(test_trait("Health")) == ALL_FIELDS);
}

TEST_CASE("Contract fields: set stays a command with no field access", "[semantic][handler-contracts][deferred-set]") {
    const auto contract = only_contract(
        "rule Heal:\n"
        "    filter:\n"
        "        Target as aim\n"
        "    on tick:\n"
        "        set Health on aim.other:\n"
        "            hp = 5\n");
    const auto health = test_trait("Health");
    const InferredHandlerCommand set_health{.kind = HandlerCommandKind::Set, .target = health};
    CHECK(std::ranges::find(contract.commands, set_health) != contract.commands.end());
    CHECK_FALSE(contract.reads.contains(health));
    CHECK_FALSE(contract.writes.contains(health));
    CHECK_FALSE(contract.read_fields.contains(health));
    CHECK_FALSE(contract.write_fields.contains(health));
    CHECK(contract.read_fields.at(test_trait("Target")) == fields_of({"other"}));
}

static const std::string PAIR_FIELD_TRAITS =
    "trait Collider:\n"
    "    var radius: float = 1.0\n"
    "    var mask: int = 0\n"
    "trait Actor:\n"
    "    var grounded: bool = false\n"
    "    var height: float = 0.0\n"
    "    var slots: int = 1\n"
    "trait Surface:\n"
    "    var top: float = 0.0\n";

static InferredHandlerContract only_pair_contract(const std::string& rules) {
    auto result = analyze(STDLIB_EVENTS + PAIR_FIELD_TRAITS + rules);
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
        "        if body.Collider.radius > 0.0:\n"
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
        "        actor.Actor.grounded = true\n");
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
        "        b.Collider.mask desc\n"
        "    where:\n"
        "        a.Collider.radius > 0.0\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(pair.read_fields.at(test_trait("Collider")) == fields_of({"mask", "radius"}));

    const auto unary = only_pair_contract(
        "rule Rank:\n"
        "    filter:\n"
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
        "    filter:\n"
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
        "        surface.Surface.top <= actor.Actor.height\n"
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
        "    limit: actor.Actor.slots per actor\n"
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
        "    filter:\n"
        "        Surface as s\n"
        "    limit: 3\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(contract.reads.empty());
    CHECK(contract.read_fields.empty());
}

TEST_CASE("Contract fields: extern rule declarations cover all fields", "[semantic][handler-contracts][extern-rule]") {
    auto result        = analyze(STDLIB_EVENTS + FIELD_TRAITS +
                                 "extern rule Scoreboard:\n"
                                 "    filter:\n"
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
    CHECK(contract.read_fields.at(test_trait("Player")) == ALL_FIELDS);
    CHECK(contract.write_fields.at(test_trait("Health")) == ALL_FIELDS);
    CHECK(contract.read_fields.at(test_trait("Health")) == ALL_FIELDS);
    CHECK(contract.read_fields.at(test_trait("Match")) == fields_of({"score"}));
}
// ── Named entity access (dsl-named-entity-access, dsl-when-clause) ─────────

namespace {

const std::string NAMED_HEADER =
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

std::vector<std::string> named_messages(const std::string& source) {
    return analyze_messages(NAMED_HEADER + source);
}

// Parses and analyzes, keeping the AST so tests can inspect annotations.
struct AnalyzedProgram {
    ProgramNode ast;
    std::vector<std::string> messages;
};

AnalyzedProgram analyze_named_program(const std::string& source) {
    const std::string src = "module test\n" + STDLIB_EVENTS + NAMED_HEADER + source;
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
                                         "    filter:\n"
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
                                         "    filter:\n"
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
                                         "        let over: bool = Game.Match.over\n"
                                         "        let x: float = Game.Pos.value.x\n"
                                         "        let v: vec2 = Game.Pos.value\n"
                                         "        if Game.Match.score > half(LIMIT):\n"
                                         "            let y = 1\n");
    INFO((messages.empty() ? "" : messages.front()));
    CHECK(messages.empty());
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let wrong: int = Game.Match.over\n"),
                      "is declared as 'int' but initialized with 'bool'"));
}

TEST_CASE("Semantic: named field reads and writes are annotated with entity and trait", "[semantic][named-entity]") {
    const auto analyzed = analyze_named_program("rule R:\n"
                                                "    on tick:\n"
                                                "        let over = Game.Match.over\n"
                                                "        Game.Match.score += 1\n");
    REQUIRE(analyzed.messages.empty());
    const auto& body       = rule_named(analyzed.ast, "R").handlers[0].body;
    const auto& field_expr = std::get<MemberExpr>(std::get<LetStmt>(body[0]->stmt).value->expr);
    const auto& trait_expr = std::get<MemberExpr>(field_expr.object->expr);
    REQUIRE(trait_expr.resolved_named_trait.has_value());
    CHECK(trait_expr.resolved_named_trait->entity == test_entity("Game"));
    CHECK(trait_expr.resolved_named_trait->trait == make_symbol_id(SymbolKind::Trait, "test", "Match"));

    const auto& assign = std::get<VarAssign>(body[1]->stmt);
    REQUIRE(assign.named_target.has_value());
    CHECK(assign.named_target->entity == test_entity("Game"));
    CHECK(assign.named_target->trait == make_symbol_id(SymbolKind::Trait, "test", "Match"));
    CHECK(assign.named_target->trait_segments == 1);
}

TEST_CASE("Semantic: named writes and compound writes are accepted in unary and pair handlers",
          "[semantic][named-entity]") {
    const auto messages = named_messages("rule R:\n"
                                         "    filter:\n"
                                         "        Enemy as enemy\n"
                                         "    on tick:\n"
                                         "        Game.Match.over = true\n"
                                         "        Game.Match.score += 1\n"
                                         "        Game.Pos.value.x -= 1.0\n"
                                         "rule P:\n"
                                         "    pairs:\n"
                                         "        a:\n"
                                         "            Enemy\n"
                                         "        b:\n"
                                         "            Enemy\n"
                                         "    on tick:\n"
                                         "        Game.Match.score += 1\n");
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].starts_with("pair rule 'P' is not accelerated"));
}

TEST_CASE("Semantic: named access to a trait the archetype does not declare is rejected",
          "[semantic][named-entity]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let id = Game.Tag.id\n"),
                      "entity 'Game' does not declare trait 'Tag'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        Hud.Match.over = true\n"),
                      "entity 'Hud' does not declare trait 'Match'"));
}

TEST_CASE("Semantic: named access to an unknown field is rejected", "[semantic][named-entity]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let nope = Game.Match.nope\n"),
                      "trait 'Match' has no field 'nope'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    on tick:\n"
                                     "        let whole = Game.Match\n"),
                      "named field access needs a field after 'Game.Match'"));
}

TEST_CASE("Semantic: named field access is rejected outside rules", "[semantic][named-entity]") {
    CHECK(has_message(named_messages("func peek() bool:\n"
                                     "    return Game.Match.over\n"),
                      "named field access is only allowed in rules"));
    CHECK(has_message(named_messages("const:\n"
                                     "    START = Game.Match.score\n"),
                      "named field access is only allowed in rules"));
    CHECK(has_message(named_messages("entity Copy:\n"
                                     "    Tag:\n"
                                     "        id = Game.Match.score\n"),
                      "named field access is only allowed in rules"));
}

TEST_CASE("Semantic: when: accepts named reads, constants, and pure calls", "[semantic][when-clause]") {
    const auto messages = named_messages("rule Idle:\n"
                                         "    when:\n"
                                         "        not Game.Match.over\n"
                                         "        Game.Match.score > half(LIMIT)\n"
                                         "    on tick:\n"
                                         "        Game.Match.score += 1\n"
                                         "rule Hits:\n"
                                         "    filter:\n"
                                         "        Enemy as enemy\n"
                                         "    when:\n"
                                         "        not Game.Match.over\n"
                                         "    on Hit:\n"
                                         "        enemy.dying = true\n");
    INFO((messages.empty() ? "" : messages.front()));
    CHECK(messages.empty());
}

TEST_CASE("Semantic: a non-bool when: predicate is rejected", "[semantic][when-clause]") {
    CHECK(has_message(named_messages("rule R:\n"
                                     "    when:\n"
                                     "        Game.Match.score\n"
                                     "    on tick:\n"
                                     "        let y = 1\n"),
                      "when: predicate must be of type 'bool'"));
}

TEST_CASE("Semantic: when: cannot read bindings, self, or event payloads", "[semantic][when-clause]") {
    const auto binding = named_messages("rule R:\n"
                                        "    filter:\n"
                                        "        Enemy as enemy\n"
                                        "    when:\n"
                                        "        enemy.dying\n"
                                        "    on tick:\n"
                                        "        enemy.dying = false\n");
    CHECK(has_message(binding, "when: cannot read 'enemy'"));
    CHECK(has_message(binding, "use where:"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    filter:\n"
                                     "        Enemy\n"
                                     "    when:\n"
                                     "        dying\n"
                                     "    on tick:\n"
                                     "        let y = 1\n"),
                      "when: cannot read 'dying'"));
    CHECK(has_message(named_messages("rule R:\n"
                                     "    filter:\n"
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
                                     "    filter:\n"
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
    return analyze(STDLIB_EVENTS + NAMED_HEADER + rules);
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

const NamedRequirement GAME_MATCH{.entity = make_symbol_id(SymbolKind::Entity, "test", "Game"),
                                  .trait  = make_symbol_id(SymbolKind::Trait, "test", "Match")};

}  // namespace

TEST_CASE("Contract: a named read in when: is recorded with entity, field, and requirement",
          "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule R:\n"
                                         "    filter:\n"
                                         "        Enemy as enemy\n"
                                         "    when:\n"
                                         "        not Game.Match.over\n"
                                         "    on tick:\n"
                                         "        enemy.dying = true\n");
    const auto& contract = named_contract(program, "R");
    const auto game      = test_entity("Game");
    const auto match     = test_trait("Match");
    REQUIRE(contract.named_reads.contains(game));
    CHECK(contract.named_reads.at(game).at(match) == fields_of({"over"}));
    CHECK(contract.read_fields.at(match) == fields_of({"over"}));
    CHECK(contract.named_requirements == std::vector<NamedRequirement>{GAME_MATCH});
    CHECK(contract.named_writes.empty());
    CHECK(contract.splittable_per_entity());
}

TEST_CASE("Contract: named reads and writes in a body record fields and one requirement",
          "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule R:\n"
                                         "    on tick:\n"
                                         "        if Game.Match.over:\n"
                                         "            Game.Match.score += 1\n"
                                         "        let x = Game.Pos.value.x\n");
    const auto& contract = named_contract(program, "R");
    const auto game      = test_entity("Game");
    CHECK(contract.named_reads.at(game).at(test_trait("Match")) == fields_of({"over", "score"}));
    CHECK(contract.named_writes.at(game).at(test_trait("Match")) == fields_of({"score"}));
    CHECK(contract.named_reads.at(game).at(test_trait("Pos")) == fields_of({"value"}));
    CHECK(contract.write_fields.at(test_trait("Match")) == fields_of({"score"}));
    const std::vector<NamedRequirement> expected{
        GAME_MATCH, NamedRequirement{.entity = game, .trait = test_trait("Pos")}};
    CHECK(contract.named_requirements == expected);
}

TEST_CASE("Contract: a named write orders against a filter reader of the same field",
          "[semantic][handler-contracts][named-entity]") {
    const auto program = analyze_named("rule End:\n"
                                       "    on tick:\n"
                                       "        Game.Match.over = true\n"
                                       "rule Watch:\n"
                                       "    filter:\n"
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
                                       "        Game.Match.score += 1\n"
                                       "rule Watch:\n"
                                       "    on tick:\n"
                                       "        if Game.Match.over:\n"
                                       "            let y = 1\n");
    CHECK(data_edges_between(program, "Score", "Watch").empty());
}

TEST_CASE("Contract: a named write marks a filter handler unsplittable",
          "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule Count:\n"
                                         "    filter:\n"
                                         "        Enemy\n"
                                         "    on tick:\n"
                                         "        Game.Match.score += 1\n");
    const auto& contract = named_contract(program, "Count");
    CHECK_FALSE(contract.splittable_per_entity());
    CHECK(contract.named_requirements == std::vector<NamedRequirement>{GAME_MATCH});
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
                                     "    filter:\n"
                                     "        Enemy as enemy\n"
                                     "    where:\n"
                                     "        enemy.dying == Game.Match.over\n"
                                     "    limit: 3\n"
                                     "    on tick:\n"
                                     "        let y = 1\n");
    const auto& unary_contract = named_contract(unary, "Pick");
    CHECK(unary_contract.named_requirements == std::vector<NamedRequirement>{GAME_MATCH});
    CHECK(unary_contract.named_reads.at(test_entity("Game")).at(test_trait("Match")) == fields_of({"over"}));

    const auto pair = analyze_named("rule Touch:\n"
                                    "    pairs:\n"
                                    "        a:\n"
                                    "            Enemy\n"
                                    "        b:\n"
                                    "            Enemy\n"
                                    "    where:\n"
                                    "        Game.Match.score > 0\n"
                                    "    limit: 1 per a\n"
                                    "    on tick:\n"
                                    "        let y = 1\n");
    CHECK(named_contract(pair, "Touch").named_requirements == std::vector<NamedRequirement>{GAME_MATCH});
}

TEST_CASE("Contract: named reads in a pair handler record a requirement", "[semantic][handler-contracts][named-entity]") {
    const auto program   = analyze_named("rule Touch:\n"
                                         "    pairs:\n"
                                         "        a:\n"
                                         "            Enemy\n"
                                         "        b:\n"
                                         "            Enemy\n"
                                         "    when:\n"
                                         "        not Game.Match.over\n"
                                         "    on tick:\n"
                                         "        Game.Match.score += 1\n");
    const auto& contract = named_contract(program, "Touch");
    CHECK(contract.named_requirements == std::vector<NamedRequirement>{GAME_MATCH});
    CHECK(contract.named_writes.at(test_entity("Game")).at(test_trait("Match")) == fields_of({"score"}));
    CHECK_FALSE(contract.splittable_per_entity());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
