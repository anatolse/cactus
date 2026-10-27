// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace cactus;
namespace fs = std::filesystem;

static bool starts_with_module_declaration(const std::string& source) {
    const auto first = source.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || source.compare(first, 6, "module") != 0) {
        return false;
    }
    const auto after_keyword = first + 6;
    return after_keyword < source.size() && std::isspace(static_cast<unsigned char>(source[after_keyword])) != 0;
}

static ProgramNode parse(const std::string& source) {
    ErrorReporter errors;
    const bool synthetic_module    = !starts_with_module_declaration(source);
    const std::string parse_source = synthetic_module ? "module test\n" + source : source;
    Lexer lexer(parse_source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    if (synthetic_module) {
        REQUIRE_FALSE(program.declarations.empty());
        REQUIRE(std::holds_alternative<ModuleNode>(program.declarations.front()));
        program.declarations.erase(program.declarations.begin());
    }
    return program;
}

static std::string read_fixture(const std::string& name) {
    auto fixture_path = fs::path(CACTUS_TEST_FIXTURES_DIR) / name;
    std::ifstream ifs(fixture_path);
    REQUIRE(ifs.good());
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static ErrorReporter parse_expect_errors(const std::string& source,
                                         const std::string& filename = "test.cactus",
                                         bool add_synthetic_module   = true) {
    ErrorReporter errors;
    const bool synthetic_module    = add_synthetic_module && !starts_with_module_declaration(source);
    const std::string parse_source = synthetic_module ? "module test\n" + source : source;
    Lexer lexer(parse_source, filename, errors);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens), errors);
    parser.parse_program();
    REQUIRE(errors.has_errors());
    return errors;
}

static ErrorReporter parse_fixture_expect_errors_with_timeout(const std::string& fixture_name) {
    auto future = std::async(std::launch::async, [&fixture_name]() {
        return parse_expect_errors(read_fixture(fixture_name), fixture_name);
    });

    auto future_status = future.wait_for(std::chrono::seconds(5));
    REQUIRE(future_status == std::future_status::ready);
    return future.get();
}

TEST_CASE("AST: runtime phase and handler reference values support equality and debug output",
          "[ast][phase][handler-contract]") {
    const SourceLocation location{"runtime.cactus", 12, 9};
    const SourceLocation same_location{"runtime.cactus", 12, 9};
    const SourceLocation other_location{"runtime.cactus", 13, 9};
    CHECK(location == same_location);
    CHECK_FALSE(location == other_location);

    std::ostringstream location_debug;
    location_debug << location;
    CHECK(location_debug.str() == "runtime.cactus:12:9");

    const LocatedName phase_name{.spelling = "std.core.tick", .location = location};
    const LocatedName phase_name_copy{.spelling = "std.core.tick", .location = same_location};
    CHECK(phase_name == phase_name_copy);
    CHECK(phase_name.debug_string() == "std.core.tick@runtime.cactus:12:9");

    std::ostringstream phase_debug;
    phase_debug << phase_name;
    CHECK(phase_debug.str() == phase_name.debug_string());

    const HandlerReferenceNode reference{
        .rule     = LocatedName{.spelling = "game.Animation", .location = location},
        .trigger  = phase_name,
        .location = location,
    };
    const HandlerReferenceNode reference_copy{
        .rule     = LocatedName{.spelling = "game.Animation", .location = same_location},
        .trigger  = phase_name_copy,
        .location = same_location,
    };
    CHECK(reference == reference_copy);
    CHECK(reference.spelling() == "game.Animation/on std.core.tick");
    CHECK(reference.debug_string() == "game.Animation/on std.core.tick@runtime.cactus:12:9");

    const ResolvedHandlerTrigger trigger{
        .kind   = HandlerTriggerKind::Phase,
        .symbol = make_symbol_id(SymbolKind::Phase, "std.core", "tick"),
    };
    const ResolvedHandlerTrigger trigger_copy{
        .kind   = HandlerTriggerKind::Phase,
        .symbol = make_symbol_id(SymbolKind::Phase, "std.core", "tick"),
    };
    CHECK(trigger == trigger_copy);
    CHECK(trigger.debug_string() == "phase std.core.tick");

    std::ostringstream trigger_debug;
    trigger_debug << trigger;
    CHECK(trigger_debug.str() == trigger.debug_string());

    const HandlerCommandNode command{
        .kind     = HandlerCommandKind::Spawn,
        .target   = LocatedName{.spelling = "game.Projectile", .location = location},
        .location = location,
    };
    const HandlerCommandNode command_copy{
        .kind     = HandlerCommandKind::Spawn,
        .target   = LocatedName{.spelling = "game.Projectile", .location = same_location},
        .location = same_location,
    };
    CHECK(command == command_copy);
    CHECK(command.debug_string() == "spawn game.Projectile@runtime.cactus:12:9");

    std::ostringstream command_debug;
    command_debug << command;
    CHECK(command_debug.str() == command.debug_string());
}

TEST_CASE("Parser: module declaration", "[parser]") {
    auto prog = parse("module player\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ModuleNode>(prog.declarations[0]);
    CHECK(decl.name == "player");
}

TEST_CASE("Parser: missing module declaration rejected", "[parser][modules]") {
    auto errors = parse_expect_errors("trait Position\n", "missing_module.cactus", false);
    REQUIRE(errors.has_errors());

    bool found = false;
    for (const auto& diagnostic : errors.diagnostics()) {
        if (diagnostic.message.contains("source file must start with a module declaration")) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Parser: duplicate module declaration rejected", "[parser][modules]") {
    auto errors = parse_expect_errors(
        "module player\n"
        "module player.extra\n",
        "duplicate_module.cactus",
        false);
    REQUIRE(errors.has_errors());

    bool found = false;
    for (const auto& diagnostic : errors.diagnostics()) {
        if (diagnostic.message.contains("only one module declaration is allowed")) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Parser: use declaration", "[parser]") {
    auto prog = parse("use world\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<UseNode>(prog.declarations[0]);
    CHECK(decl.module_name == "world");
    CHECK_FALSE(decl.alias.has_value());
}

TEST_CASE("Parser: use with alias", "[parser]") {
    auto prog  = parse("use world as w\n");
    auto& decl = std::get<UseNode>(prog.declarations[0]);
    CHECK(decl.module_name == "world");
    REQUIRE(decl.alias.has_value());
    CHECK(*decl.alias == "w");
}

TEST_CASE("Parser: const block", "[parser]") {
    auto prog = parse(
        "const:\n"
        "    X = 42\n"
        "    Y = 3.14\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    REQUIRE(decl.assignments.size() == 2);
    CHECK(decl.assignments[0].name == "X");
    CHECK(decl.assignments[1].name == "Y");
}

TEST_CASE("Parser: struct declaration", "[parser]") {
    auto prog = parse(
        "struct Item:\n"
        "    name: int\n"
        "    price: float\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<StructNode>(prog.declarations[0]);
    CHECK(decl.name == "Item");
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].name == "name");
    CHECK(decl.fields[0].type.name == "int");
    CHECK(decl.fields[1].name == "price");
    CHECK(decl.fields[1].type.name == "float");
}

TEST_CASE("Parser: enum declaration", "[parser]") {
    auto prog = parse(
        "enum Color:\n"
        "    Red\n"
        "    Green\n"
        "    Blue\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EnumNode>(prog.declarations[0]);
    CHECK(decl.name == "Color");
    REQUIRE(decl.variants.size() == 3);
    CHECK(decl.variants[0].name == "Red");
    CHECK(decl.variants[1].name == "Green");
    CHECK(decl.variants[2].name == "Blue");
}

TEST_CASE("Parser: enum with explicit values", "[parser]") {
    auto prog = parse(
        "enum Priority:\n"
        "    Low = 0\n"
        "    High = 10\n");
    auto& decl = std::get<EnumNode>(prog.declarations[0]);
    REQUIRE(decl.variants[0].value.has_value());
    CHECK(*decl.variants[0].value == 0);
    REQUIRE(decl.variants[1].value.has_value());
    CHECK(*decl.variants[1].value == 10);
}

TEST_CASE("Parser: trait with var fields", "[parser]") {
    auto prog = parse(
        "trait Health:\n"
        "    var health: int = 100\n"
        "    let max_health: int = 100\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<TraitNode>(prog.declarations[0]);
    CHECK(decl.name == "Health");
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].modifiers.is_var);
    CHECK(decl.fields[0].name == "health");
    CHECK(decl.fields[1].modifiers.is_let);
    CHECK(decl.fields[1].name == "max_health");
}

TEST_CASE("Parser: a let int field declares a range", "[parser][ranged-fields]") {
    auto prog = parse(
        "trait Cell:\n"
        "    let volume: entity_id\n"
        "    let x: int in range(volume[Volume].min_x, volume[Volume].max_x + 1) = 0\n"
        "    let y: int\n");
    auto& decl = std::get<TraitNode>(prog.declarations[0]);
    REQUIRE(decl.fields.size() == 3);
    REQUIRE(decl.fields[1].range.has_value());
    CHECK(std::holds_alternative<MemberExpr>(decl.fields[1].range->lo->expr));
    CHECK(std::holds_alternative<BinaryExpr>(decl.fields[1].range->hi->expr));
    CHECK(decl.fields[1].default_value.has_value());
    CHECK_FALSE(decl.fields[2].range.has_value());
}

TEST_CASE("Parser: malformed field ranges are diagnosed", "[parser][ranged-fields]") {
    const auto [source, message] = GENERATE(table<std::string, std::string>({
        {"trait C:\n    let x: int in range(0, 16, 2)\n", "a field range takes no step"},
        {"trait C:\n    let x: int in range(16)\n", "a field range needs two bounds"},
        {"trait C:\n    let x: int in 16\n", "expected 'range' after 'in'"},
    }));
    auto errors = parse_expect_errors(source);
    CHECK(std::ranges::any_of(errors.diagnostics(),
                              [&](const Diagnostic& diagnostic) { return diagnostic.message.contains(message); }));
}

TEST_CASE("Parser: trait with persist modifier", "[parser]") {
    auto prog = parse(
        "trait Player:\n"
        "    persist var health: int = 100\n"
        "    var position: vec3\n");
    auto& decl = std::get<TraitNode>(prog.declarations[0]);
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].modifiers.is_persist);
    CHECK(decl.fields[0].modifiers.is_var);
    CHECK(decl.fields[0].name == "health");
    CHECK_FALSE(decl.fields[1].modifiers.is_persist);
}

TEST_CASE("Parser: lambda expression is a syntax error", "[parser]") {
    auto errors = parse_expect_errors(
        "func f() int:\n"
        "    let g = x => x * 2\n"
        "    return 0\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: match expression arms still use =>", "[parser]") {
    auto prog = parse(
        "func f(c: int) int:\n"
        "    let v = match c:\n"
        "        0 => 10\n"
        "        _ => 1\n");
    auto& func      = std::get<FuncNode>(prog.declarations[0]);
    const auto* let = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    const auto* match = std::get_if<MatchExpr>(&let->value->expr);
    REQUIRE(match != nullptr);
    CHECK(match->arms.size() == 2);
}

TEST_CASE("Parser: match expression may be followed by another statement", "[parser][conditional-expressions]") {
    auto prog = parse(
        "func f(c: int) float:\n"
        "    let a = match c:\n"
        "        0 => 1.0\n"
        "        _ => 2.0\n"
        "    var b = 0.0\n"
        "    b = match c:\n"
        "        1 => 3.0\n"
        "        _ => 4.0\n"
        "    return match c:\n"
        "        2 => a\n"
        "        _ => b\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(func.body.size() == 4);
    const auto* let = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    CHECK(std::holds_alternative<MatchExpr>(let->value->expr));
    CHECK(std::holds_alternative<LetStmt>(func.body[1]->stmt));
    const auto* assign = std::get_if<VarAssign>(&func.body[2]->stmt);
    REQUIRE(assign != nullptr);
    CHECK(std::holds_alternative<MatchExpr>(assign->value->expr));
    const auto* ret = std::get_if<ReturnStmt>(&func.body[3]->stmt);
    REQUIRE(ret != nullptr);
    REQUIRE(ret->value.has_value());
    CHECK(std::holds_alternative<MatchExpr>((*ret->value)->expr));
}

TEST_CASE("Parser: match expression literal, negative literal and wildcard patterns",
          "[parser][conditional-expressions]") {
    auto prog = parse(
        "func f(c: int) float:\n"
        "    let v = match c:\n"
        "        0 => 1.0\n"
        "        -1 => 2.0\n"
        "        _ => 0.0\n"
        "    return v\n");
    auto& func        = std::get<FuncNode>(prog.declarations[0]);
    const auto* let   = std::get_if<LetStmt>(&func.body[0]->stmt);
    const auto* match = std::get_if<MatchExpr>(&let->value->expr);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 3);
    const auto* zero = std::get_if<LiteralExpr>(&match->arms[0].pattern->expr);
    REQUIRE(zero != nullptr);
    CHECK(zero->value == "0");
    const auto* minus_one = std::get_if<LiteralExpr>(&match->arms[1].pattern->expr);
    REQUIRE(minus_one != nullptr);
    CHECK(minus_one->kind == LiteralExpr::Kind::Int);
    CHECK(minus_one->value == "-1");
    CHECK(is_wildcard_pattern(*match->arms[2].pattern));
}

TEST_CASE("Parser: if expression with else if parts nests in else_expr", "[parser][conditional-expressions]") {
    auto prog = parse(
        "func f(hp: int) int:\n"
        "    let tier = if hp <= 0: 0 else if hp < 50: 1 else: 2\n"
        "    return tier\n");
    auto& func        = std::get<FuncNode>(prog.declarations[0]);
    const auto* let   = std::get_if<LetStmt>(&func.body[0]->stmt);
    const auto* outer = std::get_if<IfExpr>(&let->value->expr);
    REQUIRE(outer != nullptr);
    const auto* inner = std::get_if<IfExpr>(&outer->else_expr->expr);
    REQUIRE(inner != nullptr);
    const auto* last = std::get_if<LiteralExpr>(&inner->else_expr->expr);
    REQUIRE(last != nullptr);
    CHECK(last->value == "2");
}

TEST_CASE("Parser: if expression may span lines inside parentheses", "[parser][conditional-expressions]") {
    auto prog = parse(
        "func f(hp: int) int:\n"
        "    return (\n"
        "        if hp <= 0: 0\n"
        "        else if hp < 50: 1\n"
        "        else: 2\n"
        "    )\n"
        "func g() int:\n"
        "    return 3\n");
    REQUIRE(prog.declarations.size() == 2);
    auto& func      = std::get<FuncNode>(prog.declarations[0]);
    const auto* ret = std::get_if<ReturnStmt>(&func.body[0]->stmt);
    REQUIRE(ret != nullptr);
    REQUIRE(ret->value.has_value());
    const auto* outer = std::get_if<IfExpr>(&(*ret->value)->expr);
    REQUIRE(outer != nullptr);
    CHECK(std::holds_alternative<IfExpr>(outer->else_expr->expr));
}

TEST_CASE("Parser: if expression without else is rejected", "[parser][conditional-expressions]") {
    auto errors = parse_expect_errors(
        "func f(ready: bool) int:\n"
        "    let x = if ready: 1\n"
        "    return x\n");
    REQUIRE(errors.has_errors());
    CHECK(errors.diagnostics().front().message == "an `if` expression requires an `else`");
}

TEST_CASE("Parser: statement-level match accepts value patterns", "[parser][conditional-expressions]") {
    auto prog = parse(
        "func f(c: int):\n"
        "    match c:\n"
        "        Mode.Idle =>\n"
        "            pass\n"
        "        3 =>\n"
        "            pass\n"
        "        true =>\n"
        "            pass\n"
        "        _ =>\n"
        "            pass\n");
    auto& func  = std::get<FuncNode>(prog.declarations[0]);
    auto* match = std::get_if<MatchStmt>(&func.body[0]->stmt);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 4);
    CHECK(match->arms[0].trait_name() == "Mode.Idle");
    const auto* three = std::get_if<LiteralExpr>(&match->arms[1].pattern->expr);
    REQUIRE(three != nullptr);
    CHECK(three->value == "3");
    const auto* yes = std::get_if<LiteralExpr>(&match->arms[2].pattern->expr);
    REQUIRE(yes != nullptr);
    CHECK(yes->kind == LiteralExpr::Kind::Bool);
    CHECK(match->arms[3].is_wildcard());
}

TEST_CASE("Parser: event handler in trait body is rejected", "[parser]") {
    auto errors = parse_expect_errors(
        "trait Damageable:\n"
        "    var hp: int = 1\n"
        "    on damage:\n"
        "        hp -= 1\n");
    REQUIRE_FALSE(errors.diagnostics().empty());
    CHECK(errors.diagnostics().front().message ==
          "event handlers are not allowed in trait bodies; declare a rule instead");
}

TEST_CASE("Parser: sync is not a field modifier", "[parser]") {
    auto errors = parse_expect_errors(
        "trait Player:\n"
        "    sync var position: vec3\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: pub entity with nested trait blocks", "[parser]") {
    auto prog = parse(
        "pub entity Player:\n"
        "    Health:\n"
        "        health = 100\n"
        "    Position:\n"
        "        x = 0.0\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EntityNode>(prog.declarations[0]);
    CHECK(decl.name == "Player");
    CHECK(decl.is_pub);
    CHECK_FALSE(decl.template_ref.has_value());
    REQUIRE(decl.traits.size() == 2);
    CHECK(decl.traits[0].trait_name == "Health");
    REQUIRE(decl.traits[0].assignments.size() == 1);
    CHECK(decl.traits[0].assignments[0].name == "health");
    CHECK(decl.traits[1].trait_name == "Position");
    REQUIRE(decl.traits[1].assignments.size() == 1);
    CHECK(decl.traits[1].assignments[0].name == "x");
}

TEST_CASE("Parser: rule with filter and handler", "[parser]") {
    auto prog = parse(
        "rule MoveSystem:\n"
        "    each:\n"
        "       Position\n"
        "       Velocity\n"
        "    on tick:\n"
        "        x = x + vx * tick.dt\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<RuleNode>(prog.declarations[0]);
    CHECK(decl.name == "MoveSystem");
    REQUIRE(decl.filter.trait_names.size() == 2);
    CHECK(decl.filter.trait_names[0] == "Position");
    CHECK(decl.filter.trait_names[1] == "Velocity");
    REQUIRE(decl.handlers.size() == 1);
    CHECK(decl.handlers[0].event_name == "tick");
    CHECK_FALSE(decl.handlers[0].alias.has_value());
}

TEST_CASE("Parser: bounded foreach statement", "[parser]") {
    auto prog = parse(
        "rule ContactSystem:\n"
        "    on tick:\n"
        "        for hit in hits:\n"
        "            emit Damage to hit.entity:\n"
        "                amount = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    REQUIRE(sys.handlers[0].body.size() == 1);
    auto* foreach_stmt = std::get_if<ForeachStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(foreach_stmt != nullptr);
    CHECK(foreach_stmt->var_name == "hit");
    REQUIRE(std::get_if<IdentExpr>(&foreach_stmt->iterable->expr) != nullptr);
    REQUIRE(foreach_stmt->body.size() == 1);
    CHECK(std::get_if<EmitStmt>(&foreach_stmt->body[0]->stmt) != nullptr);
}

TEST_CASE("Parser: project statements", "[parser]") {
    auto prog = parse(
        "rule ProjectionSystem:\n"
        "    on tick:\n"
        "        project Grounded\n"
        "        project GroundContact:\n"
        "            normal = n\n"
        "        project InExplosion to hit.entity:\n"
        "            damage = 10\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    REQUIRE(sys.handlers[0].body.size() == 3);

    auto* marker = std::get_if<ProjectTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(marker != nullptr);
    CHECK(marker->trait_name == "Grounded");
    CHECK(marker->args.empty());
    CHECK_FALSE(marker->target_expr.has_value());

    auto* self_payload = std::get_if<ProjectTraitStmt>(&sys.handlers[0].body[1]->stmt);
    REQUIRE(self_payload != nullptr);
    CHECK(self_payload->trait_name == "GroundContact");
    REQUIRE(self_payload->args.size() == 1);
    CHECK(self_payload->args[0].name == "normal");

    auto* targeted = std::get_if<ProjectTraitStmt>(&sys.handlers[0].body[2]->stmt);
    REQUIRE(targeted != nullptr);
    CHECK(targeted->trait_name == "InExplosion");
    REQUIRE(targeted->target_expr.has_value());
    REQUIRE(targeted->args.size() == 1);
    CHECK(targeted->args[0].name == "damage");
}

TEST_CASE("Parser: malformed foreach block reports errors", "[parser]") {
    auto errors = parse_expect_errors(
        "rule BadSystem:\n"
        "    on tick:\n"
        "        for hit in hits\n"
        "            project Grounded\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: a view element that starts at a synchronization point still terminates", "[parser]") {
    auto errors = parse_expect_errors(
        "view Panel():\n"
        "    rule Inner:\n"
        "        on tick:\n"
        "            emit Thing\n"
        "    view.target += 1\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: event declaration", "[parser]") {
    auto prog = parse(
        "event Damage:\n"
        "    amount: int\n"
        "    source: int\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EventNode>(prog.declarations[0]);
    CHECK(decl.name == "Damage");
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].name == "amount");
    CHECK(decl.fields[0].type.name == "int");
    CHECK_FALSE(decl.fields[0].modifiers.is_var);
}

TEST_CASE("Parser: event declaration rejects trait modifiers", "[parser]") {
    auto errors = parse_expect_errors(
        "event Tick:\n"
        "    let dt: float\n");
    CHECK(errors.diagnostics().front().message.contains("event fields use bare `name: type` syntax"));
}

TEST_CASE("Parser: func declaration", "[parser]") {
    auto prog = parse(
        "func sum(a: int, b: int) int:\n"
        "    return a + b\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    CHECK(decl.name == "sum");
    REQUIRE(decl.params.size() == 2);
    REQUIRE(decl.return_type.has_value());
    CHECK(decl.return_type->name == "int");
    REQUIRE(decl.body.size() == 1);
}

TEST_CASE("Parser: expression — binary arithmetic", "[parser]") {
    auto prog = parse(
        "const:\n"
        "    X = 1 + 2 * 3\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto& expr = decl.assignments[0].value;
    // Should be (1 + (2 * 3)) due to precedence
    auto* bin = std::get_if<BinaryExpr>(&expr->expr);
    REQUIRE(bin != nullptr);
    CHECK(bin->op == "+");
}

TEST_CASE("Parser: expression — function call", "[parser]") {
    auto prog = parse(
        "func test():\n"
        "    foo(1, 2)\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* expr_stmt = std::get_if<ExprStmt>(&decl.body[0]->stmt);
    REQUIRE(expr_stmt != nullptr);
    auto* call = std::get_if<CallExpr>(&expr_stmt->expr->expr);
    REQUIRE(call != nullptr);
    CHECK(call->args.size() == 2);
}

TEST_CASE("Parser: expression — member access", "[parser]") {
    auto prog = parse(
        "const:\n"
        "    X = a.b\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* mem  = std::get_if<MemberExpr>(&decl.assignments[0].value->expr);
    REQUIRE(mem != nullptr);
    CHECK(mem->member == "b");
}

TEST_CASE("Parser: expression — unary not", "[parser]") {
    auto prog = parse(
        "const:\n"
        "    X = not true\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* un   = std::get_if<UnaryExpr>(&decl.assignments[0].value->expr);
    REQUIRE(un != nullptr);
    CHECK(un->op == "not");
}

TEST_CASE("Parser: expression — list literal", "[parser]") {
    auto prog = parse(
        "const:\n"
        "    X = [1, 2, 3]\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* list = std::get_if<ListExpr>(&decl.assignments[0].value->expr);
    REQUIRE(list != nullptr);
    CHECK(list->elements.size() == 3);
}

TEST_CASE("Parser: const value is an expression", "[parser][const]") {
    auto prog = parse(
        "const:\n"
        "    HALF_PI = math.PI * 0.5\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    REQUIRE(decl.assignments.size() == 1);
    CHECK_FALSE(decl.assignments[0].type.has_value());
    CHECK(std::holds_alternative<BinaryExpr>(decl.assignments[0].value->expr));
}

TEST_CASE("Parser: typed constant", "[parser][const]") {
    auto prog = parse(
        "const:\n"
        "    WAVES: list[UnitDef] = []\n"
        "    SPEED: float = 2.0\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    REQUIRE(decl.assignments.size() == 2);
    REQUIRE(decl.assignments[0].type.has_value());
    CHECK(decl.assignments[0].type->name == "list");
    REQUIRE(decl.assignments[0].type->param.has_value());
    CHECK((*decl.assignments[0].type->param)->name == "UnitDef");
    auto* list = std::get_if<ListExpr>(&decl.assignments[0].value->expr);
    REQUIRE(list != nullptr);
    CHECK(list->elements.empty());
    REQUIRE(decl.assignments[1].type.has_value());
    CHECK(decl.assignments[1].type->name == "float");
}

TEST_CASE("Parser: multi-line constant value", "[parser][const]") {
    auto prog = parse(
        "const:\n"
        "    ROBOT = UnitDef(\n"
        "        speed = 4.0,\n"
        "        health = 3,\n"
        "    )\n"
        "    WAVES = [\n"
        "        ROBOT,\n"
        "        UnitDef(speed = 2.5, health = 6),\n"
        "    ]\n"
        "    AFTER = 1\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    REQUIRE(decl.assignments.size() == 3);
    auto* call = std::get_if<CallExpr>(&decl.assignments[0].value->expr);
    REQUIRE(call != nullptr);
    REQUIRE(call->args.size() == 2);
    REQUIRE(call->arg_names.size() == 2);
    CHECK(call->arg_names[0] == "speed");
    CHECK(call->arg_names[1] == "health");
    auto* list = std::get_if<ListExpr>(&decl.assignments[1].value->expr);
    REQUIRE(list != nullptr);
    CHECK(list->elements.size() == 2);
    CHECK(decl.assignments[2].name == "AFTER");
}

TEST_CASE("Parser: named call arguments", "[parser][const]") {
    auto prog = parse(
        "func test():\n"
        "    let s = Squad(lead = other, size = 3,)\n"
        "    let c = math.clamp(x, 0.0, 1.0)\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 2);
    auto* named = std::get_if<LetStmt>(&decl.body[0]->stmt);
    REQUIRE(named != nullptr);
    auto* call = std::get_if<CallExpr>(&named->value->expr);
    REQUIRE(call != nullptr);
    REQUIRE(call->args.size() == 2);
    REQUIRE(call->arg_names.size() == 2);
    CHECK(call->arg_names[0] == "lead");
    CHECK(call->arg_names[1] == "size");
    CHECK(call->has_named_args());

    auto* positional_let = std::get_if<LetStmt>(&decl.body[1]->stmt);
    REQUIRE(positional_let != nullptr);
    auto* positional = std::get_if<CallExpr>(&positional_let->value->expr);
    REQUIRE(positional != nullptr);
    CHECK(positional->args.size() == 3);
    CHECK_FALSE(positional->has_named_args());
}

TEST_CASE("Parser: named arguments on a qualified callee span lines", "[parser][const]") {
    auto prog = parse(
        "const:\n"
        "    ROBOT = units.UnitDef(\n"
        "        speed = 4.0,\n"
        "        health = 3,\n"
        "    )\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    REQUIRE(decl.assignments.size() == 1);
    auto* call = std::get_if<QueryCallExpr>(&decl.assignments[0].value->expr);
    REQUIRE(call != nullptr);
    REQUIRE(call->named_args.size() == 2);
    CHECK(call->named_args[0].name == "speed");
    CHECK(call->named_args[1].name == "health");
}

TEST_CASE("Parser: if statement", "[parser]") {
    auto prog = parse(
        "func test():\n"
        "    if x > 0:\n"
        "        return x\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* if_stmt = std::get_if<IfStmt>(&decl.body[0]->stmt);
    REQUIRE(if_stmt != nullptr);
    CHECK(if_stmt->then_body.size() == 1);
}

// Baseline (pre else-if): no test before this one exercised `else` in any
// form. This captures today's two-branch if/else shape so later `else if`
// work can be checked against it instead of assumed unaffected.
TEST_CASE("Parser: if/else statement", "[parser]") {
    auto prog = parse(
        "func test():\n"
        "    if x > 0:\n"
        "        return x\n"
        "    else:\n"
        "        return 0\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* if_stmt = std::get_if<IfStmt>(&decl.body[0]->stmt);
    REQUIRE(if_stmt != nullptr);
    REQUIRE(if_stmt->then_body.size() == 1);
    REQUIRE(if_stmt->else_body.size() == 1);

    auto* then_return = std::get_if<ReturnStmt>(&if_stmt->then_body[0]->stmt);
    REQUIRE(then_return != nullptr);
    REQUIRE(then_return->value.has_value());
    auto* then_ident = std::get_if<IdentExpr>(&(*then_return->value)->expr);
    REQUIRE(then_ident != nullptr);
    CHECK(then_ident->name == "x");

    auto* else_return = std::get_if<ReturnStmt>(&if_stmt->else_body[0]->stmt);
    REQUIRE(else_return != nullptr);
    REQUIRE(else_return->value.has_value());
    auto* else_lit = std::get_if<LiteralExpr>(&(*else_return->value)->expr);
    REQUIRE(else_lit != nullptr);
    CHECK(else_lit->value == "0");
}

// Baseline: the legacy spelling for chained conditions is `else:` whose body
// is a single nested `if`. This must keep parsing exactly as today (nested
// IfStmt inside else_body) — the else-if grammar work does not touch this
// path.
TEST_CASE("Parser: legacy nested else+if chain", "[parser]") {
    auto prog = parse(
        "func test():\n"
        "    if a:\n"
        "        return 1\n"
        "    else:\n"
        "        if b:\n"
        "            return 2\n"
        "        else:\n"
        "            return 3\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* outer_if = std::get_if<IfStmt>(&decl.body[0]->stmt);
    REQUIRE(outer_if != nullptr);
    REQUIRE(outer_if->else_body.size() == 1);
    auto* nested_if = std::get_if<IfStmt>(&outer_if->else_body[0]->stmt);
    REQUIRE(nested_if != nullptr);
    CHECK(nested_if->then_body.size() == 1);
    CHECK(nested_if->else_body.size() == 1);
}

// Baseline: today, a second `else:` with no owning chain slot to attach to
// (the "duplicate terminal else" shape from specs/dsl-parser/spec.md) is not
// a dedicated diagnostic — it falls through to the generic "expected
// expression" error from parse_primary_expr's unrecognised-token fallback.
// Captured here so the new-grammar diagnostic work (section 3) can compare
// against this instead of assuming what today's behavior is.
TEST_CASE("Parser: duplicate else with no owning if reports generic error", "[parser]") {
    auto errors = parse_expect_errors(
        "func test():\n"
        "    if a:\n"
        "        return 1\n"
        "    else:\n"
        "        return 2\n"
        "    else:\n"
        "        return 3\n");
    REQUIRE(errors.has_errors());

    bool found = false;
    for (const auto& diagnostic : errors.diagnostics()) {
        if (diagnostic.message == "expected expression") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Parser: else-if chain parses into branch-list shape", "[parser]") {
    auto prog = parse(
        "func test():\n"
        "    if a:\n"
        "        return 1\n"
        "    else if b:\n"
        "        return 2\n"
        "    else if c:\n"
        "        return 3\n"
        "    else:\n"
        "        return 4\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* if_stmt = std::get_if<IfStmt>(&decl.body[0]->stmt);
    REQUIRE(if_stmt != nullptr);
    REQUIRE(if_stmt->then_body.size() == 1);
    REQUIRE(if_stmt->else_if_branches.size() == 2);
    REQUIRE(if_stmt->else_body.size() == 1);

    auto* cond_a = std::get_if<IdentExpr>(&if_stmt->condition->expr);
    REQUIRE(cond_a != nullptr);
    CHECK(cond_a->name == "a");

    auto* cond_b = std::get_if<IdentExpr>(&if_stmt->else_if_branches[0].condition->expr);
    REQUIRE(cond_b != nullptr);
    CHECK(cond_b->name == "b");
    REQUIRE(if_stmt->else_if_branches[0].body.size() == 1);

    auto* cond_c = std::get_if<IdentExpr>(&if_stmt->else_if_branches[1].condition->expr);
    REQUIRE(cond_c != nullptr);
    CHECK(cond_c->name == "c");
    REQUIRE(if_stmt->else_if_branches[1].body.size() == 1);

    // Branch bodies preserve source order: 1 (then), 2/3 (else-if arms), 4 (terminal else).
    auto extract_return_literal = [](const std::unique_ptr<StmtNode>& stmt) -> const std::string& {
        auto* ret = std::get_if<ReturnStmt>(&stmt->stmt);
        REQUIRE(ret != nullptr);
        REQUIRE(ret->value.has_value());
        auto* lit = std::get_if<LiteralExpr>(&(*ret->value)->expr);
        REQUIRE(lit != nullptr);
        return lit->value;
    };
    CHECK(extract_return_literal(if_stmt->then_body[0]) == "1");
    CHECK(extract_return_literal(if_stmt->else_if_branches[0].body[0]) == "2");
    CHECK(extract_return_literal(if_stmt->else_if_branches[1].body[0]) == "3");
    CHECK(extract_return_literal(if_stmt->else_body[0]) == "4");
}

TEST_CASE("Parser: else-if chain without terminal else has empty else body", "[parser]") {
    auto prog = parse(
        "func test():\n"
        "    if a:\n"
        "        return 1\n"
        "    else if b:\n"
        "        return 2\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    auto* if_stmt = std::get_if<IfStmt>(&decl.body[0]->stmt);
    REQUIRE(if_stmt != nullptr);
    REQUIRE(if_stmt->else_if_branches.size() == 1);
    CHECK(if_stmt->else_body.empty());
}

// Note: "duplicate terminal else" (a second `else:` following an already-complete
// if/else) is already exercised by the section-1 baseline test above
// ("Parser: duplicate else with no owning if reports generic error") — the two
// scenarios are the same source shape, so it is not duplicated here.

TEST_CASE("Parser: else if after terminal else rejected", "[parser]") {
    auto errors = parse_expect_errors(
        "func test():\n"
        "    if a:\n"
        "        return 1\n"
        "    else:\n"
        "        return 2\n"
        "    else if c:\n"
        "        return 3\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: misindented else if rejected", "[parser]") {
    auto errors = parse_expect_errors(
        "func test():\n"
        "    if a:\n"
        "        return 1\n"
        "        else if b:\n"
        "            return 2\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: empty else if suite rejected", "[parser]") {
    auto errors = parse_expect_errors(
        "func test():\n"
        "    if a:\n"
        "        return 1\n"
        "    else if b:\n"
        "    return 2\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: emit statement", "[parser]") {
    auto prog = parse(
        "func test():\n"
        "    emit Damage:\n"
        "        amount = 10\n"
        "        source = 0\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    auto* emit = std::get_if<EmitStmt>(&decl.body[0]->stmt);
    REQUIRE(emit != nullptr);
    CHECK(emit->event_name == "Damage");
    CHECK(emit->payload.size() == 2);
    CHECK(emit->payload[0].name == "amount");
    CHECK(emit->payload[1].name == "source");
}

TEST_CASE("Parser: emit statement with no payload omits the field block", "[parser]") {
    // A zero-field event (e.g. std.ui.StartBump) has nothing to assign, and
    // the indentation-sensitive lexer only emits INDENT ahead of actual
    // indented content, so the trailing ':' + field block is optional —
    // `emit Event` / `emit Event to target` on one line is how a zero-payload
    // emit is spelled (see EmitStmt's the parser fix for the underlying gap:
    // this construct previously failed with "expected indented block").
    auto prog = parse(
        "func test():\n"
        "    emit Ping\n"
        "    emit Ping to self\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 2);

    auto* broadcast = std::get_if<EmitStmt>(&decl.body[0]->stmt);
    REQUIRE(broadcast != nullptr);
    CHECK(broadcast->event_name == "Ping");
    CHECK(broadcast->payload.empty());
    CHECK_FALSE(broadcast->target.has_value());

    auto* targeted = std::get_if<EmitStmt>(&decl.body[1]->stmt);
    REQUIRE(targeted != nullptr);
    CHECK(targeted->event_name == "Ping");
    CHECK(targeted->payload.empty());
    REQUIRE(targeted->target.has_value());
    CHECK(std::holds_alternative<SelfExpr>((*targeted->target)->expr));
}

TEST_CASE("Parser: assignment operators", "[parser]") {
    auto prog = parse(
        "func test():\n"
        "    x = 1\n"
        "    y += 2\n"
        "    z -= 3\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 3);
    auto* a1 = std::get_if<VarAssign>(&decl.body[0]->stmt);
    auto* a2 = std::get_if<VarAssign>(&decl.body[1]->stmt);
    auto* a3 = std::get_if<VarAssign>(&decl.body[2]->stmt);
    REQUIRE(a1 != nullptr);
    CHECK(a1->op == "=");
    REQUIRE(a2 != nullptr);
    CHECK(a2->op == "+=");
    REQUIRE(a3 != nullptr);
    CHECK(a3->op == "-=");
}

TEST_CASE("Parser: multiple declarations", "[parser]") {
    auto prog = parse(
        "module game\n"
        "use player\n"
        "const:\n"
        "    X = 10\n"
        "struct Item:\n"
        "    price: int\n");
    CHECK(prog.declarations.size() == 4);
}

// ── Module Rule Tests ─────────────────────────────────────────────────────

TEST_CASE("Parser: dotted module declaration", "[parser][modules]") {
    auto prog = parse("module enemies.walker\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ModuleNode>(prog.declarations[0]);
    CHECK(decl.name == "enemies.walker");
}

TEST_CASE("Parser: deeply dotted module declaration", "[parser][modules]") {
    auto prog  = parse("module lib.physics.rigid\n");
    auto& decl = std::get<ModuleNode>(prog.declarations[0]);
    CHECK(decl.name == "lib.physics.rigid");
}

TEST_CASE("Parser: use with dotted path", "[parser][modules]") {
    auto prog  = parse("use enemies.walker\n");
    auto& decl = std::get<UseNode>(prog.declarations[0]);
    CHECK(decl.module_name == "enemies.walker");
    CHECK_FALSE(decl.alias.has_value());
}

TEST_CASE("Parser: use with dotted path and alias", "[parser][modules]") {
    auto prog  = parse("use phys.body as b\n");
    auto& decl = std::get<UseNode>(prog.declarations[0]);
    CHECK(decl.module_name == "phys.body");
    REQUIRE(decl.alias.has_value());
    CHECK(*decl.alias == "b");
}

TEST_CASE("Parser: filter with qualified trait names", "[parser][modules]") {
    auto prog = parse(
        "rule Render:\n"
        "    each:  \n"
        "        phys.Body\n"
        "        render.Sprite\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.filter.entries.size() == 2);
    CHECK(sys.filter.entries[0].qualified_name == "phys.Body");
    CHECK_FALSE(sys.filter.entries[0].alias.has_value());
    CHECK(sys.filter.entries[1].qualified_name == "render.Sprite");
    // Backward compat: trait_names has simple names
    REQUIRE(sys.filter.trait_names.size() == 2);
    CHECK(sys.filter.trait_names[0] == "Body");
    CHECK(sys.filter.trait_names[1] == "Sprite");
}

TEST_CASE("Parser: filter with aliases", "[parser][modules]") {
    auto prog = parse(
        "rule Render:\n"
        "    each:\n"
        "        phys.Body as b\n"
        "        render.Sprite as s\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.filter.entries.size() == 2);
    CHECK(sys.filter.entries[0].qualified_name == "phys.Body");
    REQUIRE(sys.filter.entries[0].alias.has_value());
    CHECK(*sys.filter.entries[0].alias == "b");
    CHECK(sys.filter.entries[1].qualified_name == "render.Sprite");
    REQUIRE(sys.filter.entries[1].alias.has_value());
    CHECK(*sys.filter.entries[1].alias == "s");
}

TEST_CASE("Parser: filter mixed qualified and unqualified", "[parser][modules]") {
    auto prog = parse(
        "rule Mixed:\n"
        "    each:\n"
        "        Position\n"
        "        phys.Body as b\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.filter.entries.size() == 2);
    CHECK(sys.filter.entries[0].qualified_name == "Position");
    CHECK_FALSE(sys.filter.entries[0].alias.has_value());
    CHECK(sys.filter.entries[1].qualified_name == "phys.Body");
    REQUIRE(sys.filter.entries[1].alias.has_value());
    CHECK(*sys.filter.entries[1].alias == "b");
    // Backward compat
    CHECK(sys.filter.trait_names[0] == "Position");
    CHECK(sys.filter.trait_names[1] == "Body");
}

TEST_CASE("Parser: filter with unqualified aliases", "[parser][modules]") {
    auto prog = parse(
        "rule Simple:\n"
        "    each:\n"
        "        Position as pos\n"
        "        Velocity as vel\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.filter.entries.size() == 2);
    CHECK(sys.filter.entries[0].qualified_name == "Position");
    CHECK(*sys.filter.entries[0].alias == "pos");
    CHECK(sys.filter.entries[1].qualified_name == "Velocity");
    CHECK(*sys.filter.entries[1].alias == "vel");
}

// ── New parser tests (dynamic-ecs-language) ────────────────────────────────

// Task 4.1: template declaration
TEST_CASE("Parser: named template applications support dependent defaults", "[parser][template-parameters]") {
    auto program = parse(R"(template Projectile(speed: float, velocity: vec2 = vec2(speed, 0.0)):
    Motion:
        velocity = velocity
entity First from Projectile(speed = 12.0)
)");
    REQUIRE(program.declarations.size() == 2);
    CHECK(std::holds_alternative<TemplateNode>(program.declarations[0]));
    CHECK(std::holds_alternative<EntityNode>(program.declarations[1]));
}

TEST_CASE("Parser: parameterized templates apply at every creation site", "[parser][template-parameters]") {
    auto program = parse(R"(template Leaf(value: int):
    Data:
        value = value
template Tree(value: int = 3):
    use Leaf(value = value)
    children:
        entity Child from Leaf(value = value)
entity Root from Tree(value = 5):
    Data:
        value = 7
rule Create:
    on tick:
        spawn Tree(value = 8)
        let tree = spawn Tree(value = 9)
)");
    REQUIRE(program.declarations.size() == 4);
}

TEST_CASE("Parser: template arguments must be named", "[parser][template-parameters]") {
    auto errors = parse_expect_errors("entity First from Projectile(12.0)\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: template interfaces and applications support multiline lists", "[parser][template-parameters]") {
    auto program = parse(R"(template Item(
    a: int,
    b: int = 2,
):
    Data
entity First from Item(
    a = 1,
)
)");
    REQUIRE(program.declarations.size() == 2);
    CHECK(std::get<TemplateNode>(program.declarations[0]).parameters.size() == 2);
}

TEST_CASE("Parser: template declaration", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "template EnemyWalker:\n"
        "    Position:\n"
        "        x = 0.0\n"
        "    EnemyAI:\n"
        "        patrol_speed = 2.0\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    CHECK(tmpl.name == "EnemyWalker");
    CHECK_FALSE(tmpl.is_pub);
    REQUIRE(tmpl.traits.size() == 2);
    CHECK(tmpl.traits[0].trait_name == "Position");
    REQUIRE(tmpl.traits[0].assignments.size() == 1);
    CHECK(tmpl.traits[0].assignments[0].name == "x");
    CHECK(tmpl.traits[1].trait_name == "EnemyAI");
    REQUIRE(tmpl.traits[1].assignments.size() == 1);
    CHECK(tmpl.traits[1].assignments[0].name == "patrol_speed");
}

TEST_CASE("Parser: template body use entry", "[parser][template-composition]") {
    auto prog = parse(
        "template EnemyBase:\n"
        "    Health\n"
        "template WalkerEnemy:\n"
        "    use EnemyBase\n"
        "    PatrolMotion:\n"
        "        patrol_speed = 140.0\n");

    REQUIRE(prog.declarations.size() == 2);
    auto& tmpl = std::get<TemplateNode>(prog.declarations[1]);
    CHECK(tmpl.name == "WalkerEnemy");
    REQUIRE(tmpl.template_uses.size() == 1);
    CHECK(tmpl.template_uses[0].template_name == "EnemyBase");
    REQUIRE(tmpl.traits.size() == 1);
    CHECK(tmpl.traits[0].trait_name == "PatrolMotion");
    REQUIRE(tmpl.body_entries.size() == 2);
    CHECK(tmpl.body_entries[0].kind == ArchetypeBodyEntry::Kind::TemplateUse);
    CHECK(tmpl.body_entries[0].index == 0);
    CHECK(tmpl.body_entries[1].kind == ArchetypeBodyEntry::Kind::Trait);
    CHECK(tmpl.body_entries[1].index == 0);
}

TEST_CASE("Parser: entity body use entry", "[parser][template-composition]") {
    auto prog = parse(
        "entity Walker1:\n"
        "    use WalkerEnemy\n"
        "    WorldTransform:\n"
        "        position = vec2(400.0, 568.0)\n");

    REQUIRE(prog.declarations.size() == 1);
    auto& entity = std::get<EntityNode>(prog.declarations[0]);
    CHECK(entity.name == "Walker1");
    CHECK_FALSE(entity.template_ref.has_value());
    REQUIRE(entity.template_uses.size() == 1);
    CHECK(entity.template_uses[0].template_name == "WalkerEnemy");
    REQUIRE(entity.traits.size() == 1);
    CHECK(entity.traits[0].trait_name == "WorldTransform");
    REQUIRE(entity.body_entries.size() == 2);
    CHECK(entity.body_entries[0].kind == ArchetypeBodyEntry::Kind::TemplateUse);
    CHECK(entity.body_entries[1].kind == ArchetypeBodyEntry::Kind::Trait);
}

TEST_CASE("Parser: archetype body use names can be local qualified or aliased", "[parser][template-composition]") {
    auto prog = parse(
        "use enemies.walker as enemy\n"
        "template LocalVariant:\n"
        "    use EnemyBase\n"
        "template QualifiedVariant:\n"
        "    use enemies.WalkerEnemy\n"
        "entity AliasedWalker:\n"
        "    use enemy.WalkerEnemy\n");

    REQUIRE(prog.declarations.size() == 4);
    auto& top_level_use = std::get<UseNode>(prog.declarations[0]);
    CHECK(top_level_use.module_name == "enemies.walker");
    REQUIRE(top_level_use.alias.has_value());
    CHECK(*top_level_use.alias == "enemy");

    auto& local = std::get<TemplateNode>(prog.declarations[1]);
    REQUIRE(local.template_uses.size() == 1);
    CHECK(local.template_uses[0].template_name == "EnemyBase");

    auto& qualified = std::get<TemplateNode>(prog.declarations[2]);
    REQUIRE(qualified.template_uses.size() == 1);
    CHECK(qualified.template_uses[0].template_name == "enemies.WalkerEnemy");

    auto& aliased = std::get<EntityNode>(prog.declarations[3]);
    REQUIRE(aliased.template_uses.size() == 1);
    CHECK(aliased.template_uses[0].template_name == "enemy.WalkerEnemy");
}

// Task 4.1: pub template
TEST_CASE("Parser: pub template declaration", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "pub template Bullet:\n"
        "    Position\n"
        "    Motion:\n"
        "        speed = 10.0\n");
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    CHECK(tmpl.name == "Bullet");
    CHECK(tmpl.is_pub);
    REQUIRE(tmpl.traits.size() == 2);
}

// `spawn`/`destroy` still parse as event names so analysis can report the migration diagnostic.
TEST_CASE("Parser: on spawn lifecycle handler", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule Init:\n"
        "    each:\n"
        "        Position\n"
        "    on spawn:\n"
        "        x = 0.0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "spawn");
}

TEST_CASE("Parser: on destroy lifecycle handler", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule Cleanup:\n"
        "    each:\n"
        "        Position\n"
        "    on destroy:\n"
        "        x = 0.0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK(sys.handlers[0].event_name == "destroy");
}

TEST_CASE("Parser: on load and on unload lifecycle handlers", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule LevelMgr:\n"
        "    each: \n"
        "        GameState\n"
        "    on load:\n"
        "        x = 1\n"
        "    on unload:\n"
        "        x = 0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 2);
    CHECK(sys.handlers[0].event_name == "load");
    CHECK(sys.handlers[1].event_name == "unload");
}

// Task 4.4: rule with exclude block
TEST_CASE("Parser: rule with exclude clause", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule SceneCleanup:\n"
        "    without:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        x = 0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK(sys.filter.entries.empty());  // no filter
    REQUIRE(sys.exclude.trait_names.size() == 1);
    CHECK(sys.exclude.trait_names[0] == "Persistent");
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "unload");
}

// Task 4.5-4.6: spawn statement
TEST_CASE("Parser: spawn statement", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule LevelSetup:\n"
        "    each: \n"
        "        GameState\n"
        "    on load:\n"
        "        spawn Enemy:\n"
        "            Position:\n"
        "                pos = 0.0\n"
        "            EnemyAI:\n"
        "                patrol_speed = 2.0\n");
    auto& sys     = std::get<RuleNode>(prog.declarations[0]);
    auto& handler = sys.handlers[0];
    REQUIRE(handler.body.size() == 1);
    auto* spawn = std::get_if<SpawnStmt>(&handler.body[0]->stmt);
    REQUIRE(spawn != nullptr);
    CHECK(spawn->template_name == "Enemy");
    CHECK(spawn->overrides.size() == 2);
    CHECK(spawn->overrides[0].trait_name == "Position");
    REQUIRE(spawn->overrides[0].assignments.size() == 1);
    CHECK(spawn->overrides[0].assignments[0].name == "pos");
    CHECK(spawn->overrides[1].trait_name == "EnemyAI");
    REQUIRE(spawn->overrides[1].assignments.size() == 1);
    CHECK(spawn->overrides[1].assignments[0].name == "patrol_speed");
}

// Task 4.7: destroy statement
TEST_CASE("Parser: destroy statement", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule DeathSys:\n"
        "    each: \n"
        "        Health\n"
        "    on tick:\n"
        "        destroy\n");
    auto& sys     = std::get<RuleNode>(prog.declarations[0]);
    auto* destroy = std::get_if<DestroyStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(destroy != nullptr);
}

TEST_CASE("Parser: destroy targeted entity statement", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "event Collision:\n"
        "    other: entity_id\n"
        "rule Cleanup:\n"
        "    on Collision as c:\n"
        "        destroy c.other\n");
    auto& sys     = std::get<RuleNode>(prog.declarations[1]);
    auto* destroy = std::get_if<DestroyStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(destroy != nullptr);
    REQUIRE(destroy->target_expr.has_value());
}

TEST_CASE("Parser: self parsed as destroy target", "[parser][hierarchy]") {
    auto prog = parse(
        "rule Cleanup:\n"
        "    on tick:\n"
        "        destroy self\n");
    auto& sys     = std::get<RuleNode>(prog.declarations[0]);
    auto* destroy = std::get_if<DestroyStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(destroy != nullptr);
    REQUIRE(destroy->target_expr.has_value());
    CHECK(std::holds_alternative<SelfExpr>((*destroy->target_expr)->expr));
}

TEST_CASE("Parser: self parsed in assignment expression", "[parser][hierarchy]") {
    auto prog = parse(
        "rule Parenting:\n"
        "    on tick:\n"
        "        add Parent to self\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    auto* add = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(add != nullptr);
    REQUIRE(add->target_expr.has_value());
    CHECK(std::holds_alternative<SelfExpr>((*add->target_expr)->expr));
}

TEST_CASE("Parser: child is no longer reserved", "[parser][hierarchy]") {
    auto prog = parse(
        "func child() int:\n"
        "    return 1\n");
    auto& fn = std::get<FuncNode>(prog.declarations[0]);
    CHECK(fn.name == "child");
}

// Task 4.8: load statement
TEST_CASE("Parser: load statement", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule GameMgr:\n"
        "    each: \n"
        "        GameState\n"
        "    on tick:\n"
        "        load levels.level1\n");
    auto& sys  = std::get<RuleNode>(prog.declarations[0]);
    auto* load = std::get_if<LoadStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(load != nullptr);
    CHECK(load->module_name == "levels.level1");
}

TEST_CASE("Parser: add and remove statements", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule FreezeSystem:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen\n"
        "        remove EnemyAI\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 2);
    auto* add    = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    auto* remove = std::get_if<RemoveTraitStmt>(&sys.handlers[0].body[1]->stmt);
    REQUIRE(add != nullptr);
    CHECK(add->trait_name == "Frozen");
    REQUIRE(remove != nullptr);
    CHECK(remove->trait_name == "EnemyAI");
}

TEST_CASE("Parser: add statement with field block and target", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule FreezeSystem:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen to target_id:\n"
        "            duration = 2.0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    auto* add = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(add != nullptr);
    CHECK(add->trait_name == "Frozen");
    REQUIRE(add->args.size() == 1);
    CHECK(add->args[0].name == "duration");
    REQUIRE(add->target_expr.has_value());
}

TEST_CASE("Parser: remove statement with target", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule FreezeSystem:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        remove Frozen from target_id\n");
    auto& sys    = std::get<RuleNode>(prog.declarations[0]);
    auto* remove = std::get_if<RemoveTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(remove != nullptr);
    CHECK(remove->trait_name == "Frozen");
    REQUIRE(remove->target_expr.has_value());
}

static bool has_diagnostic(const ErrorReporter& errors, const std::string& text) {
    return std::ranges::any_of(errors.diagnostics(),
                               [&](const auto& diagnostic) { return diagnostic.message.contains(text); });
}

TEST_CASE("Parser: set statement with field block and target", "[parser][deferred-set]") {
    auto prog = parse(
        "rule Animate:\n"
        "    on tick:\n"
        "        set models.ModelAnimator on visual:\n"
        "            clip = 3\n"
        "            time = 0.0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    auto* set = std::get_if<SetTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(set != nullptr);
    CHECK(set->trait_name == "models.ModelAnimator");
    REQUIRE(set->target_expr != nullptr);
    auto* target = std::get_if<IdentExpr>(&set->target_expr->expr);
    REQUIRE(target != nullptr);
    CHECK(target->name == "visual");
    REQUIRE(set->args.size() == 2);
    CHECK(set->args[0].name == "clip");
    CHECK(set->args[1].name == "time");
}

TEST_CASE("Parser: set statement on self", "[parser][deferred-set]") {
    auto prog = parse(
        "rule Heal:\n"
        "    on tick:\n"
        "        set Health on self:\n"
        "            current = 10\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    auto* set = std::get_if<SetTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(set != nullptr);
    CHECK(set->trait_name == "Health");
    REQUIRE(set->target_expr != nullptr);
    CHECK(std::holds_alternative<SelfExpr>(set->target_expr->expr));
}

TEST_CASE("Parser: set statement without target is rejected", "[parser][deferred-set]") {
    auto errors = parse_expect_errors(
        "rule Heal:\n"
        "    on tick:\n"
        "        set Health:\n"
        "            current = 10\n");
    CHECK(has_diagnostic(errors, "'set' requires an 'on <entity>' target"));
}

TEST_CASE("Parser: set statement with empty field block is rejected", "[parser][deferred-set]") {
    auto errors = parse_expect_errors(
        "rule Heal:\n"
        "    on tick:\n"
        "        set Health on self:\n"
        "        destroy self\n");
    CHECK(has_diagnostic(errors, "'set' needs at least one field assignment"));
}

TEST_CASE("Parser: set remains an ordinary identifier", "[parser][deferred-set]") {
    auto prog = parse(
        "trait Settings:\n"
        "    var set: int = 0\n"
        "rule Count:\n"
        "    each:\n"
        "        Settings as settings\n"
        "    on tick:\n"
        "        let set = settings.set\n"
        "        settings.set = set + 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[1]);
    REQUIRE(sys.handlers[0].body.size() == 2);
    CHECK(std::holds_alternative<LetStmt>(sys.handlers[0].body[0]->stmt));
    CHECK(std::holds_alternative<VarAssign>(sys.handlers[0].body[1]->stmt));
}

TEST_CASE("Parser: let and var locals in handler bodies", "[parser][locals]") {
    auto prog = parse(
        "rule Count:\n"
        "    on tick:\n"
        "        let a = 1\n"
        "        var b = 2\n"
        "        var c: int = 3\n");
    auto& rule = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(rule.handlers[0].body.size() == 3);
    const auto* a = std::get_if<LetStmt>(&rule.handlers[0].body[0]->stmt);
    const auto* b = std::get_if<LetStmt>(&rule.handlers[0].body[1]->stmt);
    const auto* c = std::get_if<LetStmt>(&rule.handlers[0].body[2]->stmt);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK_FALSE(a->is_mutable);
    CHECK_FALSE(a->type.has_value());
    CHECK(b->is_mutable);
    CHECK(b->name == "b");
    CHECK(c->is_mutable);
    REQUIRE(c->type.has_value());
    CHECK(c->type->name == "int");
}

TEST_CASE("Parser: let and var locals in func bodies", "[parser][locals]") {
    auto prog = parse(
        "func f() int:\n"
        "    let x: float = 1.0\n"
        "    var y = 2\n"
        "    return y\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    const auto* x = std::get_if<LetStmt>(&func.body[0]->stmt);
    const auto* y = std::get_if<LetStmt>(&func.body[1]->stmt);
    REQUIRE(x != nullptr);
    REQUIRE(y != nullptr);
    CHECK_FALSE(x->is_mutable);
    REQUIRE(x->type.has_value());
    CHECK(x->type->name == "float");
    CHECK(y->is_mutable);
}

// Task 4.11: marker trait (no body)
TEST_CASE("Parser: marker trait without body", "[parser][dynamic-ecs]") {
    auto prog = parse("trait Persistent\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& trait = std::get<TraitNode>(prog.declarations[0]);
    CHECK(trait.name == "Persistent");
    CHECK(trait.fields.empty());
    // Note: TraitNode no longer has handlers — traits are data-only
}

TEST_CASE("Parser: pub marker trait", "[parser][dynamic-ecs]") {
    auto prog   = parse("pub trait Frozen\n");
    auto& trait = std::get<TraitNode>(prog.declarations[0]);
    CHECK(trait.name == "Frozen");
    CHECK(trait.is_pub);
    CHECK(trait.fields.empty());
}

// ── dsl-spec-new-features parser tests ─────────────────────────────────────

// Task 4.8: asset_decl - all seven asset types
TEST_CASE("Parser: asset declaration mesh type", "[parser][dsl-spec-new-features]") {
    auto prog = parse("asset PlayerMesh: mesh = \"models/player.glb\"\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.name == "PlayerMesh");
    CHECK(node.is_pub == false);
    CHECK(node.asset_kind == AssetKind::Mesh);
    CHECK(node.path == "models/player.glb");
}

TEST_CASE("Parser: asset declaration model type", "[parser][dsl-model-assets]") {
    auto prog = parse("asset Robot: model = \"art/robot.glb\"\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.name == "Robot");
    CHECK(node.is_pub == false);
    CHECK(node.asset_kind == AssetKind::Model);
    CHECK(node.path == "art/robot.glb");
}

TEST_CASE("Parser: pub asset declaration texture type", "[parser][dsl-spec-new-features]") {
    auto prog = parse("pub asset HeroTex: texture = \"sprites/hero.png\"\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.name == "HeroTex");
    CHECK(node.is_pub == true);
    CHECK(node.asset_kind == AssetKind::Texture);
    CHECK(node.path == "sprites/hero.png");
}

TEST_CASE("Parser: asset declaration sound type", "[parser][dsl-spec-new-features]") {
    auto prog  = parse("asset ShotSfx: sound = \"audio/shot.wav\"\n");
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.asset_kind == AssetKind::Sound);
}

TEST_CASE("Parser: asset declaration music type", "[parser][dsl-spec-new-features]") {
    auto prog  = parse("asset Theme: music = \"audio/theme.ogg\"\n");
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.asset_kind == AssetKind::Music);
}

TEST_CASE("Parser: asset declaration font type", "[parser][dsl-spec-new-features]") {
    auto prog  = parse("asset HudFont: font = \"fonts/hud.ttf\"\n");
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.asset_kind == AssetKind::Font);
}

TEST_CASE("Parser: asset declaration material type", "[parser][dsl-spec-new-features]") {
    auto prog  = parse("asset StoneMat: material = \"materials/stone.mat\"\n");
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.asset_kind == AssetKind::Material);
}

// Task 4.9: input_decl
TEST_CASE("Parser: input declaration button with properties", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "input Jump: button\n"
        "    key     = Key.Space\n"
        "    gamepad = GamepadButton.South\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& node = std::get<InputDeclNode>(prog.declarations[0]);
    CHECK(node.name == "Jump");
    CHECK(node.is_pub == false);
    CHECK(node.input_kind == InputKind::Button);
    REQUIRE(node.props.size() == 2);
    CHECK(node.props[0].key == "key");
    CHECK(node.props[1].key == "gamepad");
}

TEST_CASE("Parser: pub input declaration axis type", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "pub input MoveX: axis\n"
        "    negative = Key.A\n"
        "    positive = Key.D\n"
        "    gamepad  = GamepadAxis.LeftX\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& node = std::get<InputDeclNode>(prog.declarations[0]);
    CHECK(node.name == "MoveX");
    CHECK(node.is_pub == true);
    CHECK(node.input_kind == InputKind::Axis);
    REQUIRE(node.props.size() == 3);
    CHECK(node.props[0].key == "negative");
    CHECK(node.props[1].key == "positive");
    CHECK(node.props[2].key == "gamepad");
}

TEST_CASE("Parser: input declaration axis with invert property", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "input MoveY: axis\n"
        "    negative = Key.S\n"
        "    positive = Key.W\n"
        "    invert   = true\n");
    auto& node = std::get<InputDeclNode>(prog.declarations[0]);
    CHECK(node.input_kind == InputKind::Axis);
    CHECK(node.props.size() == 3);
    CHECK(node.props[2].key == "invert");
}

// Task 4.10: on fixed_tick, on late_tick, on input lifecycle handlers
TEST_CASE("Parser: on input handler with no parameters", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "rule InputSys:\n"
        "    on input:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "input");
}

TEST_CASE("Parser: on fixed_tick handler with dt parameter", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "rule PhysSys:\n"
        "    on fixed_tick:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "fixed_tick");
}

TEST_CASE("Parser: on late_tick handler with dt parameter", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "rule CamSys:\n"
        "    on late_tick:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "late_tick");
}

TEST_CASE("Parser: rule with multiple lifecycle handlers", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "rule MultiPhase:\n"
        "    on input:\n"
        "        x = 1\n"
        "    on fixed_tick:\n"
        "        y = 2\n"
        "    on late_tick:\n"
        "        z = 3\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 3);
    CHECK(sys.handlers[0].event_name == "input");
    CHECK(sys.handlers[1].event_name == "fixed_tick");
    CHECK(sys.handlers[2].event_name == "late_tick");
}

// ── extern-func parser tests (tasks 3.3, 8.4) ──────────────────────────────

TEST_CASE("Parser: pub extern func declaration with return type", "[parser][extern-func]") {
    auto prog = parse("pub extern func lerp(a: float, b: float, t: float) float\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    CHECK(decl.name == "lerp");
    CHECK(decl.is_pub);
    CHECK(decl.is_extern);
    REQUIRE(decl.params.size() == 3);
    CHECK(decl.params[0].name == "a");
    CHECK(decl.params[0].type.name == "float");
    CHECK(decl.params[1].name == "b");
    CHECK(decl.params[2].name == "t");
    REQUIRE(decl.return_type.has_value());
    CHECK(decl.return_type->name == "float");
    CHECK(decl.body.empty());  // no body for extern
}

TEST_CASE("Parser: extern func without pub", "[parser][extern-func]") {
    auto prog = parse("extern func sin(a: float) float\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    CHECK(decl.name == "sin");
    CHECK_FALSE(decl.is_pub);
    CHECK(decl.is_extern);
    REQUIRE(decl.params.size() == 1);
    REQUIRE(decl.return_type.has_value());
    CHECK(decl.return_type->name == "float");
    CHECK(decl.body.empty());
}

TEST_CASE("Parser: extern func without return type", "[parser][extern-func]") {
    auto prog = parse("pub extern func init()\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    CHECK(decl.name == "init");
    CHECK(decl.is_pub);
    CHECK(decl.is_extern);
    CHECK(decl.params.empty());
    CHECK_FALSE(decl.return_type.has_value());
    CHECK(decl.body.empty());
}

TEST_CASE("Parser: consecutive extern funcs parse cleanly", "[parser][extern-func]") {
    auto prog = parse(
        "pub extern func sin(a: float) float\n"
        "pub extern func cos(a: float) float\n"
        "pub extern func sqrt(v: float) float\n");
    REQUIRE(prog.declarations.size() == 3);
    CHECK(std::get<FuncNode>(prog.declarations[0]).name == "sin");
    CHECK(std::get<FuncNode>(prog.declarations[1]).name == "cos");
    CHECK(std::get<FuncNode>(prog.declarations[2]).name == "sqrt");
    for (auto& d : prog.declarations) {
        CHECK(std::get<FuncNode>(d).is_extern);
        CHECK(std::get<FuncNode>(d).body.empty());
    }
}

TEST_CASE("Parser: std.physics.flat query extern declarations parse", "[parser][extern-func][stdlib][physics]") {
    auto prog = parse(
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
        "pub extern func query_overlap_all(subject: entity_id, mask: int, exclude: entity_id) "
        "list[QueryContact2D]\n");

    REQUIRE(prog.declarations.size() == 6);
    auto& kind = std::get<EnumNode>(prog.declarations[0]);
    CHECK(kind.name == "QueryResultKind");
    REQUIRE(kind.variants.size() == 2);
    CHECK(kind.variants[0].name == "Empty");
    CHECK(kind.variants[1].name == "Hit");

    auto& contact = std::get<StructNode>(prog.declarations[1]);
    CHECK(contact.name == "QueryContact2D");
    REQUIRE(contact.fields.size() == 4);
    CHECK(contact.fields[0].name == "other");
    CHECK(contact.fields[0].type.name == "entity_id");
    CHECK(contact.fields[1].type.name == "vec2");

    auto& cast = std::get<FuncNode>(prog.declarations[3]);
    CHECK(cast.name == "query_cast_nearest");
    CHECK(cast.is_pub);
    CHECK(cast.is_extern);
    REQUIRE(cast.params.size() == 4);
    CHECK(cast.params[3].name == "exclude");
    REQUIRE(cast.return_type.has_value());
    CHECK(cast.return_type->name == "QueryResult2D");

    auto& overlap_all = std::get<FuncNode>(prog.declarations[5]);
    REQUIRE(overlap_all.return_type.has_value());
    CHECK(overlap_all.return_type->name == "list");
    REQUIRE(overlap_all.return_type->param.has_value());
    CHECK((*overlap_all.return_type->param)->name == "QueryContact2D");
}

TEST_CASE("Parser: non-extern func without body still errors", "[parser][extern-func]") {
    // A regular func must have a body (colon + indented block)
    // Without it, the parser will error on the missing ':'
    ErrorReporter errors;
    Lexer lexer("func missing_body(a: float) float\n", "test.cactus", errors);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens), errors);
    parser.parse_program();
    CHECK(errors.has_errors());  // should error: expected ':'
}

// Task 8.4: func return type without arrow, and arrow produces error

TEST_CASE("Parser: func with return type using no arrow", "[parser][extern-func]") {
    auto prog = parse(
        "func distance(a: float, b: float) float:\n"
        "    return a - b\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    CHECK(decl.name == "distance");
    CHECK_FALSE(decl.is_extern);
    REQUIRE(decl.return_type.has_value());
    CHECK(decl.return_type->name == "float");
    REQUIRE(decl.body.size() == 1);
}

TEST_CASE("Parser: func with arrow return type produces error", "[parser][extern-func]") {
    // Using old -> syntax should produce an error
    ErrorReporter errors;
    Lexer lexer(
        "func add(a: int, b: int) -> int:\n"
        "    return a + b\n",
        "test.cactus",
        errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());  // lexing should be fine
    Parser parser(std::move(tokens), errors);
    parser.parse_program();
    CHECK(errors.has_errors());  // parser should error: unexpected '->'
}

// ── rule-ordering-and-trait-cleanup tests ─────────────────────────────────

// Task 12.1: Trait body with 'on' produces error
TEST_CASE("Parser: trait body with on handler produces error", "[parser][trait-cleanup]") {
    ErrorReporter errors;
    Lexer lexer(
        "trait Bad:\n"
        "    var x: int\n"
        "    on tick(dt: float):\n"
        "        x = 1\n",
        "test.cactus",
        errors);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens), errors);
    parser.parse_program();
    CHECK(errors.has_errors());
}

// Task 12.2: after: block parses correctly into RuleNode.after_rules
TEST_CASE("Parser: rule after: block parsed correctly", "[parser][rule-ordering]") {
    auto prog = parse(
        "rule SceneRender:\n"
        "    on tick:\n"
        "        x = 1\n"
        "\n"
        "rule UIRender:\n"
        "    after:\n"
        "        SceneRender\n"
        "    on tick:\n"
        "        y = 2\n");
    REQUIRE(prog.declarations.size() == 2);
    auto& ui = std::get<RuleNode>(prog.declarations[1]);
    CHECK(ui.name == "UIRender");
    REQUIRE(ui.after_rules.size() == 1);
    CHECK(ui.after_rules[0] == "SceneRender");
    // SceneRender has empty after_rules
    auto& scene = std::get<RuleNode>(prog.declarations[0]);
    CHECK(scene.after_rules.empty());
}

TEST_CASE("Parser: marker trait entry in entity", "[parser][config-qualification]") {
    auto prog = parse(
        "entity Player:\n"
        "    Position\n");
    auto& entity = std::get<EntityNode>(prog.declarations[0]);
    REQUIRE(entity.traits.size() == 1);
    CHECK(entity.traits[0].trait_name == "Position");
    CHECK(entity.traits[0].assignments.empty());
}

TEST_CASE("Parser: nested trait field assignment parsed correctly", "[parser][config-qualification]") {
    auto prog = parse(
        "trait Health:\n"
        "    var health: int = 100\n"
        "entity Player:\n"
        "    Health:\n"
        "        health = 50\n");
    auto& entity = std::get<EntityNode>(prog.declarations[1]);
    REQUIRE(entity.traits.size() == 1);
    CHECK(entity.traits[0].trait_name == "Health");
    REQUIRE(entity.traits[0].assignments.size() == 1);
    CHECK(entity.traits[0].assignments[0].name == "health");
}

TEST_CASE("Parser: trait field default values are parsed", "[parser][dynamic-traits]") {
    auto prog = parse(
        "trait Frozen:\n"
        "    var duration: float = 3.0\n"
        "    let stacks: int = 1\n");
    auto& trait = std::get<TraitNode>(prog.declarations[0]);
    REQUIRE(trait.fields.size() == 2);
    CHECK(trait.fields[0].default_value.has_value());
    CHECK(trait.fields[1].default_value.has_value());
}

TEST_CASE("Parser: bare add statement parsed", "[parser][dynamic-traits]") {
    auto prog = parse(
        "rule FreezeSystem:\n"
        "    on tick:\n"
        "        add Frozen\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    REQUIRE(sys.handlers[0].body.size() == 1);
    auto* add = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(add != nullptr);
    CHECK(add->trait_name == "Frozen");
    CHECK(add->args.empty());
    CHECK_FALSE(add->target_expr.has_value());
}

// Flattens a MemberExpr chain rooted at an identifier into (alias, dotted
// field), mirroring the shape SortKey{alias, field} used to store directly
// before order-by's sort keys became general expressions.
static std::optional<std::pair<std::string, std::string>> sort_key_alias_field(const ExprNode& expr) {
    const auto* member = std::get_if<MemberExpr>(&expr.expr);
    if (member == nullptr) {
        return std::nullopt;
    }
    std::vector<std::string> segments{member->member};
    const ExprNode* cursor = member->object.get();
    while (cursor != nullptr) {
        if (const auto* inner = std::get_if<MemberExpr>(&cursor->expr)) {
            segments.push_back(inner->member);
            cursor = inner->object.get();
            continue;
        }
        if (const auto* ident = std::get_if<IdentExpr>(&cursor->expr)) {
            std::ranges::reverse(segments);
            std::string field;
            for (size_t i = 0; i < segments.size(); ++i) {
                if (i != 0) {
                    field += '.';
                }
                field += segments[i];
            }
            return std::make_pair(ident->name, field);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

TEST_CASE("Parser: extern rule declaration with filter and order by", "[parser][extern-rule]") {
    auto prog = parse(
        "extern rule SpriteRenderer:\n"
        "    each:\n"
        "        std.transform.flat.Position as pos\n"
        "        std.render.sprites.Renderer as r\n"
        "    without:\n"
        "        Hidden\n"
        "    order by:\n"
        "        r.layer asc\n"
        "        pos.pos.y desc\n"
        "    after:\n"
        "        TransformUpdate\n");

    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ExternRuleNode>(prog.declarations[0]);
    CHECK(decl.name == "SpriteRenderer");
    REQUIRE(decl.filter.entries.size() == 2);
    CHECK(decl.filter.entries[0].qualified_name == "std.transform.flat.Position");
    REQUIRE(decl.filter.entries[0].alias.has_value());
    CHECK(*decl.filter.entries[0].alias == "pos");
    REQUIRE(decl.exclude.entries.size() == 1);
    CHECK(decl.exclude.entries[0].qualified_name == "Hidden");
    REQUIRE(decl.order_by.size() == 2);
    auto key0 = sort_key_alias_field(*decl.order_by[0].expression);
    REQUIRE(key0.has_value());
    CHECK(key0->first == "r");
    CHECK(key0->second == "layer");
    CHECK_FALSE(decl.order_by[0].descending);
    auto key1 = sort_key_alias_field(*decl.order_by[1].expression);
    REQUIRE(key1.has_value());
    CHECK(key1->first == "pos");
    CHECK(key1->second == "pos.y");
    CHECK(decl.order_by[1].descending);
    REQUIRE(decl.after_rules.size() == 1);
    CHECK(decl.after_rules[0] == "TransformUpdate");
}

TEST_CASE("Parser: rule order by single and default asc", "[parser][rule-order-by]") {
    auto prog = parse(
        "rule Render:\n"
        "    each:\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.order_by.size() == 1);
    auto key0 = sort_key_alias_field(*sys.order_by[0].expression);
    REQUIRE(key0.has_value());
    CHECK(key0->first == "s");
    CHECK(key0->second == "layer");
    CHECK_FALSE(sys.order_by[0].descending);
}

TEST_CASE("Parser: rule order by multi key", "[parser][rule-order-by]") {
    auto prog = parse(
        "rule Render:\n"
        "    each:\n"
        "        Position as p\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer asc\n"
        "        p.pos.y desc\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.order_by.size() == 2);
    auto key0 = sort_key_alias_field(*sys.order_by[0].expression);
    REQUIRE(key0.has_value());
    CHECK(key0->first == "s");
    CHECK(key0->second == "layer");
    CHECK_FALSE(sys.order_by[0].descending);
    auto key1 = sort_key_alias_field(*sys.order_by[1].expression);
    REQUIRE(key1.has_value());
    CHECK(key1->first == "p");
    CHECK(key1->second == "pos.y");
    CHECK(sys.order_by[1].descending);
}

TEST_CASE("Parser: rule without order by leaves clause empty", "[parser][rule-order-by]") {
    auto prog = parse(
        "rule Render:\n"
        "    each:\n"
        "        Sprite\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK(sys.order_by.empty());
}

TEST_CASE("Parser: rule order by accepts a computed cross-expression sort key", "[parser][rule-order-by]") {
    auto prog = parse(
        "rule Render:\n"
        "    each:\n"
        "        Position as pos\n"
        "    order by:\n"
        "        math.length(pos.value - origin) desc\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.order_by.size() == 1);
    CHECK(sys.order_by[0].descending);
    const auto* call = std::get_if<CallExpr>(&sys.order_by[0].expression->expr);
    REQUIRE(call != nullptr);
    REQUIRE(call->args.size() == 1);
    const auto* subtract = std::get_if<BinaryExpr>(&call->args[0]->expr);
    REQUIRE(subtract != nullptr);
    CHECK(subtract->op == "-");
}

// ── Pair relations (dsl-pair-relations) ─────────────────────────────────────

TEST_CASE("Parser: pairs clause with two local-trait bindings", "[parser][pair-relations]") {
    auto prog = parse(
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "            Transform\n"
        "            Collider\n"
        "        wall:\n"
        "            Solid\n"
        "            Collider\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.pairs.has_value());
    REQUIRE(sys.pairs->bindings.size() == 2);

    auto& body = sys.pairs->bindings[0];
    CHECK(body.name == "body");
    REQUIRE(body.traits.size() == 3);
    CHECK(body.traits[0].qualified_name == "DynamicBody");
    CHECK(body.traits[1].qualified_name == "Transform");
    CHECK(body.traits[2].qualified_name == "Collider");

    auto& wall = sys.pairs->bindings[1];
    CHECK(wall.name == "wall");
    REQUIRE(wall.traits.size() == 2);
    CHECK(wall.traits[0].qualified_name == "Solid");
    CHECK(wall.traits[1].qualified_name == "Collider");

    CHECK(sys.filter.entries.empty());
    CHECK(sys.exclude.entries.empty());
}

TEST_CASE("Parser: pairs clause preserves imported trait qualification and binding-local alias",
          "[parser][pair-relations]") {
    auto prog = parse(
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "            tf.WorldTransform as transform\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys  = std::get<RuleNode>(prog.declarations[0]);
    auto& wall = sys.pairs->bindings[1];
    REQUIRE(wall.traits.size() == 2);
    CHECK(wall.traits[1].qualified_name == "tf.WorldTransform");
    REQUIRE(wall.traits[1].alias.has_value());
    CHECK(*wall.traits[1].alias == "transform");
}

TEST_CASE("Parser: pairs clause preserves source order and locations", "[parser][pair-relations]") {
    auto prog = parse(
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.pairs.has_value());
    CHECK(sys.pairs->bindings[0].name == "body");
    CHECK(sys.pairs->bindings[1].name == "wall");
    CHECK(sys.pairs->bindings[0].location.line < sys.pairs->bindings[1].location.line);
    CHECK(sys.pairs->bindings[0].traits[0].location.line > sys.pairs->bindings[0].location.line);
}

TEST_CASE("Parser: pairs remains a contextual identifier outside rule-clause position", "[parser][pair-relations]") {
    auto prog = parse(
        "rule UsesPairsLocal:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        let pairs = 1\n"
        "        x = pairs\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK_FALSE(sys.pairs.has_value());
    REQUIRE(sys.handlers.size() == 1);
    REQUIRE(sys.handlers[0].body.size() == 2);
}

TEST_CASE("Parser: pairs clause with one binding is rejected", "[parser][pair-relations]") {
    auto errors = parse_expect_errors(
        "rule Bad:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "    on fixed_tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: pairs clause with three bindings is rejected", "[parser][pair-relations]") {
    auto errors = parse_expect_errors(
        "rule Bad:\n"
        "    pairs:\n"
        "        a:\n"
        "            A\n"
        "        b:\n"
        "            B\n"
        "        c:\n"
        "            C\n"
        "    on fixed_tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: pairs clause with an empty binding is rejected", "[parser][pair-relations]") {
    auto errors = parse_expect_errors(
        "rule Bad:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "    on fixed_tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: pairs clause combined with filter is rejected", "[parser][pair-relations]") {
    auto errors = parse_expect_errors(
        "rule Bad:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    each:\n"
        "        Position\n"
        "    on fixed_tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: pairs clause combined with exclude is rejected", "[parser][pair-relations]") {
    auto errors = parse_expect_errors(
        "rule Bad:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    without:\n"
        "        Dead\n"
        "    on fixed_tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
}

// order by: is not mutually exclusive with pairs: at the parser layer (or, as
// of a later task, at the semantic layer either) — only each:/without: are.
TEST_CASE("Parser: pairs clause combined with order by parses successfully", "[parser][pair-relations]") {
    auto prog = parse(
        "rule Contacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    order by:\n"
        "        body[DynamicBody].x\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.pairs.has_value());
    REQUIRE(sys.order_by.size() == 1);
    const auto key0 = trait_field_path(std::as_const(*sys.order_by[0].expression));
    REQUIRE(key0.has_value());
    CHECK(dotted_name_of(*key0->first->object) == "body");
    CHECK(key0->first->trait_name == "DynamicBody");
    CHECK(key0->second == std::vector<std::string>{"x"});
}

TEST_CASE("Parser: bracketed assignment target parses name, trait and path", "[parser][pair-relations]") {
    auto prog = parse(
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    on fixed_tick:\n"
        "        body[Transform].x += 1.0\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 1);
    const auto* assign = std::get_if<VarAssign>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(assign != nullptr);
    CHECK(assign->name == "body");
    CHECK(assign->path == std::vector<std::string>{"x"});
    REQUIRE(assign->target != nullptr);
    CHECK(trait_field_path(std::as_const(*assign->target))->first->trait_name == "Transform");
    CHECK(assign->op == "+=");
}

TEST_CASE("Parser: star-assign and slash-assign parse into VarAssign with correct op", "[parser][vector-expressions]") {
    auto prog = parse(
        "rule Simple:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        motion.velocity *= 2.0\n"
        "        motion.velocity /= 2.0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 2);

    const auto* star_assign = std::get_if<VarAssign>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(star_assign != nullptr);
    CHECK(star_assign->name == "motion");
    REQUIRE(star_assign->path.size() == 1);
    CHECK(star_assign->path[0] == "velocity");
    CHECK(star_assign->op == "*=");

    const auto* slash_assign = std::get_if<VarAssign>(&sys.handlers[0].body[1]->stmt);
    REQUIRE(slash_assign != nullptr);
    CHECK(slash_assign->name == "motion");
    REQUIRE(slash_assign->path.size() == 1);
    CHECK(slash_assign->path[0] == "velocity");
    CHECK(slash_assign->op == "/=");
}

TEST_CASE("Parser: slash-assign parses a nested writable-component path", "[parser][vector-expressions]") {
    auto prog = parse(
        "rule Simple:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        motion.velocity.y /= 2.0\n");
    auto& sys           = std::get<RuleNode>(prog.declarations[0]);
    const auto* assign = std::get_if<VarAssign>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(assign != nullptr);
    CHECK(assign->name == "motion");
    REQUIRE(assign->path.size() == 2);
    CHECK(assign->path[0] == "velocity");
    CHECK(assign->path[1] == "y");
    CHECK(assign->op == "/=");
}

TEST_CASE("Parser: bare assignment target still has an empty path", "[parser][pair-relations]") {
    auto prog = parse(
        "rule Simple:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys          = std::get<RuleNode>(prog.declarations[0]);
    const auto* assign = std::get_if<VarAssign>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(assign != nullptr);
    CHECK(assign->name == "x");
    CHECK(assign->path.empty());
}

TEST_CASE("Parser: dotted member chain without assignment operator parses as expression statement",
          "[parser][pair-relations]") {
    auto prog = parse(
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    on fixed_tick:\n"
        "        body[DynamicBody].active\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 1);
    const auto* expr_stmt = std::get_if<ExprStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(expr_stmt != nullptr);
    const auto* outer = std::get_if<MemberExpr>(&expr_stmt->expr->expr);
    REQUIRE(outer != nullptr);
    CHECK(outer->member == "active");
}

TEST_CASE("Parser: pairs clause on extern rule is rejected", "[parser][pair-relations]") {
    auto errors = parse_expect_errors(
        "extern rule Bad:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n");
    REQUIRE(errors.has_errors());
}

// ── Where clause (dsl-where-clause) ─────────────────────────────────────────

TEST_CASE("Parser: where clause after filter is parsed", "[parser][where-clause]") {
    auto prog = parse(
        "rule Moving:\n"
        "    each:\n"
        "        Body as ball\n"
        "    where:\n"
        "        ball.velocity.x > 0.0\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.where_clause.has_value());
    REQUIRE(sys.where_clause->predicates.size() == 1);
    const auto* predicate = std::get_if<BinaryExpr>(&sys.where_clause->predicates[0]->expr);
    REQUIRE(predicate != nullptr);
    CHECK(predicate->op == ">");
}

TEST_CASE("Parser: where clause after pairs is parsed", "[parser][where-clause]") {
    auto prog = parse(
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        a:\n"
        "            Ball\n"
        "        b:\n"
        "            Ball\n"
        "    where:\n"
        "        a != b\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.where_clause.has_value());
    REQUIRE(sys.where_clause->predicates.size() == 1);
    const auto* predicate = std::get_if<BinaryExpr>(&sys.where_clause->predicates[0]->expr);
    REQUIRE(predicate != nullptr);
    CHECK(predicate->op == "!=");
}

TEST_CASE("Parser: multiple where clause lines are parsed as a list in source order", "[parser][where-clause]") {
    auto prog = parse(
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        a:\n"
        "            Ball\n"
        "        b:\n"
        "            Ball\n"
        "    where:\n"
        "        a != b\n"
        "        a.radius > 0.0\n"
        "        b.radius > 0.0\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.where_clause.has_value());
    REQUIRE(sys.where_clause->predicates.size() == 3);
    const auto* first = std::get_if<BinaryExpr>(&sys.where_clause->predicates[0]->expr);
    REQUIRE(first != nullptr);
    CHECK(first->op == "!=");
    const auto* second = std::get_if<BinaryExpr>(&sys.where_clause->predicates[1]->expr);
    REQUIRE(second != nullptr);
    const auto* second_left = std::get_if<MemberExpr>(&second->left->expr);
    REQUIRE(second_left != nullptr);
    CHECK(second_left->member == "radius");
    const auto* third = std::get_if<BinaryExpr>(&sys.where_clause->predicates[2]->expr);
    REQUIRE(third != nullptr);
    const auto* third_left = std::get_if<MemberExpr>(&third->left->expr);
    REQUIRE(third_left != nullptr);
    CHECK(third_left->member == "radius");
}

TEST_CASE("Parser: where remains a contextual identifier outside clause position", "[parser][where-clause]") {
    auto prog = parse(
        "rule UsesWhereLocal:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        let where = 1\n"
        "        x = where\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK_FALSE(sys.where_clause.has_value());
    REQUIRE(sys.handlers.size() == 1);
    REQUIRE(sys.handlers[0].body.size() == 2);
}

TEST_CASE("Parser: where clause on extern rule is rejected", "[parser][where-clause]") {
    auto errors = parse_expect_errors(
        "extern rule Bad:\n"
        "    each:\n"
        "        Position\n"
        "    where:\n"
        "        true\n");
    REQUIRE(errors.has_errors());
}

// ── When clause (dsl-when-clause) ───────────────────────────────────────────

TEST_CASE("Parser: when clause on a selectionless rule is parsed", "[parser][when-clause]") {
    auto prog = parse(
        "rule Spawn:\n"
        "    when:\n"
        "        not Game.Match.over\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.when_clause.has_value());
    REQUIRE(sys.when_clause->predicates.size() == 1);
    CHECK(std::holds_alternative<UnaryExpr>(sys.when_clause->predicates[0]->expr));
    CHECK_FALSE(sys.where_clause.has_value());
    CHECK(sys.handlers.size() == 1);
}

TEST_CASE("Parser: multiple when lines after filter, after and where are parsed in order", "[parser][when-clause]") {
    auto prog = parse(
        "rule Move:\n"
        "    each:\n"
        "        Enemy as enemy\n"
        "    after:\n"
        "        Spawn\n"
        "    where:\n"
        "        enemy.speed > 0.0\n"
        "    when:\n"
        "        not Game.Match.over\n"
        "        Game.Match.wave > 0\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.where_clause.has_value());
    REQUIRE(sys.when_clause.has_value());
    REQUIRE(sys.when_clause->predicates.size() == 2);
    CHECK(std::holds_alternative<UnaryExpr>(sys.when_clause->predicates[0]->expr));
    const auto* second = std::get_if<BinaryExpr>(&sys.when_clause->predicates[1]->expr);
    REQUIRE(second != nullptr);
    CHECK(second->op == ">");
    CHECK(sys.after_rules == std::vector<std::string>{"Spawn"});
}

TEST_CASE("Parser: when remains a contextual identifier outside clause position", "[parser][when-clause]") {
    auto prog = parse(
        "rule UsesWhenLocal:\n"
        "    on tick:\n"
        "        let when = 1\n"
        "        x = when\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK_FALSE(sys.when_clause.has_value());
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].body.size() == 2);
}

TEST_CASE("Parser: empty when clause is rejected", "[parser][when-clause]") {
    auto errors = parse_expect_errors(
        "rule Bad:\n"
        "    when:\n"
        "    on tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: when clause on extern rule is rejected", "[parser][when-clause]") {
    auto errors = parse_expect_errors(
        "extern rule Bad:\n"
        "    each:\n"
        "        Position\n"
        "    when:\n"
        "        true\n"
        "    on tick:\n"
        "        reads:\n"
        "            Position\n");
    REQUIRE(errors.has_errors());
    CHECK(std::ranges::any_of(errors.diagnostics(), [](const Diagnostic& diagnostic) {
        return diagnostic.message.contains("'when:' is only allowed on regular rules");
    }));
}

// ── Limit clause (dsl-rule-limit) ───────────────────────────────────────────

TEST_CASE("Parser: global limit on a pairs rule is parsed", "[parser][rule-limit]") {
    auto prog = parse(
        "rule Contacts:\n"
        "    pairs:\n"
        "        actor:\n"
        "            Body\n"
        "        other:\n"
        "            Body\n"
        "    limit: 10\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.limit.has_value());
    CHECK(sys.limit->parts.empty());
    const auto* count = std::get_if<LiteralExpr>(&sys.limit->count->expr);
    REQUIRE(count != nullptr);
    CHECK(count->value == "10");
}

TEST_CASE("Parser: per-binding limit on a pairs rule is parsed", "[parser][rule-limit]") {
    auto prog = parse(
        "rule Contacts:\n"
        "    pairs:\n"
        "        actor:\n"
        "            Body\n"
        "        other:\n"
        "            Body\n"
        "    limit: 1 per actor\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.limit.has_value());
    REQUIRE(sys.limit->parts.size() == 1);
    CHECK(sys.limit->parts[0].binding == "actor");
    const auto* count = std::get_if<LiteralExpr>(&sys.limit->parts[0].count->expr);
    REQUIRE(count != nullptr);
    CHECK(count->value == "1");
}

TEST_CASE("Parser: limit on a filter rule is parsed", "[parser][rule-limit]") {
    auto prog = parse(
        "rule Render:\n"
        "    each:\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer\n"
        "    limit: 10\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.limit.has_value());
    CHECK(sys.limit->parts.empty());
    CHECK(sys.order_by.size() == 1);
}

TEST_CASE("Parser: limit count accepts a computed expression", "[parser][rule-limit]") {
    auto prog = parse(
        "rule Contacts:\n"
        "    pairs:\n"
        "        tower:\n"
        "            Tower\n"
        "        other:\n"
        "            Body\n"
        "    limit: tower[Tower].target_count per tower\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.limit.has_value());
    REQUIRE(sys.limit->parts.size() == 1);
    CHECK(sys.limit->parts[0].binding == "tower");
    CHECK(std::holds_alternative<MemberExpr>(sys.limit->parts[0].count->expr));
}

TEST_CASE("Parser: rule without limit leaves clause unset", "[parser][rule-limit]") {
    auto prog = parse(
        "rule Render:\n"
        "    each:\n"
        "        Sprite\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK_FALSE(sys.limit.has_value());
}

// The domain requirement is a semantic check (see test_semantic.cpp's
// "limit: requires an each: or pairs: domain"), exactly as it is for where: —
// the parser accepts the clause and records it for that later pass.
TEST_CASE("Parser: limit on a selectionless rule still parses", "[parser][rule-limit]") {
    auto prog = parse(
        "rule Selectionless:\n"
        "    limit: 10\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.limit.has_value());
    CHECK(sys.filter.entries.empty());
    CHECK_FALSE(sys.pairs.has_value());
}

// ── Reduce clause (dsl-rule-reduce) ─────────────────────────────────────────

TEST_CASE("Parser: grouped reduce parses every reducer kind", "[parser][rule-reduce]") {
    auto prog = parse(
        "rule TeamScore:\n"
        "    pairs:\n"
        "        team:\n"
        "            Team\n"
        "        player:\n"
        "            Score\n"
        "    where:\n"
        "        player[Score].team == team\n"
        "    reduce per team:\n"
        "        rows = count()\n"
        "        players = count(player)\n"
        "        total = sum(player[Score].value)\n"
        "        low = min(player[Score].value, default = 0)\n"
        "        high = max(player[Score].value, default = 0)\n"
        "        any_high = any(player[Score].value > 10)\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.reduce.has_value());
    REQUIRE(sys.reduce->per_binding.has_value());
    CHECK(*sys.reduce->per_binding == "team");
    const auto& reducers = sys.reduce->reducers;
    REQUIRE(reducers.size() == 6);
    CHECK(reducers[0].name == "rows");
    CHECK(reducers[0].kind == ReducerKind::Count);
    CHECK(reducers[0].input == nullptr);
    CHECK(reducers[1].kind == ReducerKind::Count);
    REQUIRE(reducers[1].input != nullptr);
    CHECK(std::holds_alternative<IdentExpr>(reducers[1].input->expr));
    CHECK(reducers[2].kind == ReducerKind::Sum);
    CHECK(reducers[3].kind == ReducerKind::Min);
    CHECK(reducers[3].default_value != nullptr);
    CHECK(reducers[4].kind == ReducerKind::Max);
    CHECK(reducers[4].default_value != nullptr);
    CHECK(reducers[5].kind == ReducerKind::Any);
    CHECK(reducers[5].default_value == nullptr);
}

TEST_CASE("Parser: global reduce on a filter rule has no per binding", "[parser][rule-reduce]") {
    auto prog = parse(
        "rule CountEnemies:\n"
        "    each:\n"
        "        Enemy as e\n"
        "    reduce:\n"
        "        n = count()\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.reduce.has_value());
    CHECK_FALSE(sys.reduce->per_binding.has_value());
    REQUIRE(sys.reduce->reducers.size() == 1);
}

TEST_CASE("Parser: order by and limit after reduce rank aggregate rows", "[parser][rule-reduce]") {
    auto prog = parse(
        "rule RankTeams:\n"
        "    pairs:\n"
        "        team:\n"
        "            Team\n"
        "        player:\n"
        "            Score\n"
        "    reduce per team:\n"
        "        total = sum(player[Score].value)\n"
        "    order by:\n"
        "        total desc\n"
        "    limit: 2\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.reduce.has_value());
    REQUIRE(sys.order_by.size() == 1);
    CHECK(sys.order_by[0].descending);
    CHECK(sys.limit.has_value());
}

TEST_CASE("Parser: order by before reduce is rejected", "[parser][rule-reduce]") {
    auto errors = parse_expect_errors(
        "rule RankTeams:\n"
        "    pairs:\n"
        "        team:\n"
        "            Team\n"
        "        player:\n"
        "            Score\n"
        "    order by:\n"
        "        team[Team].id\n"
        "    reduce per team:\n"
        "        total = sum(player[Score].value)\n"
        "    on tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
    CHECK(std::ranges::any_of(errors.diagnostics(), [](const Diagnostic& diagnostic) {
        return diagnostic.message.contains("after 'reduce:'");
    }));
}

TEST_CASE("Parser: unknown reducers are rejected", "[parser][rule-reduce]") {
    for (const std::string reducer : {"collect(player[Score].value)", "avg(player[Score].value)"}) {
        auto errors = parse_expect_errors(
            "rule Bad:\n"
            "    pairs:\n"
            "        team:\n"
            "            Team\n"
            "        player:\n"
            "            Score\n"
            "    reduce:\n"
            "        all = " +
            reducer +
            "\n"
            "    on tick:\n"
            "        x = 1\n");
        REQUIRE(errors.has_errors());
        CHECK(std::ranges::any_of(errors.diagnostics(), [](const Diagnostic& diagnostic) {
            return diagnostic.message.contains("unknown reducer");
        }));
    }
}

TEST_CASE("Parser: reduce needs at least one reducer", "[parser][rule-reduce]") {
    auto errors = parse_expect_errors(
        "rule Bad:\n"
        "    each:\n"
        "        Enemy\n"
        "    reduce per e:\n"
        "    on tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: reduce clause on extern rule is rejected", "[parser][rule-reduce]") {
    auto errors = parse_expect_errors(
        "extern rule Bad:\n"
        "    each:\n"
        "        Position\n"
        "    reduce:\n"
        "        n = count()\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: limit clause on extern rule is rejected", "[parser][rule-limit]") {
    auto errors = parse_expect_errors(
        "extern rule Bad:\n"
        "    each:\n"
        "        Position\n"
        "    limit: 10\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: extern rule rejects executable handler statements", "[parser][extern-rule]") {
    auto errors = parse_expect_errors(
        "extern rule Bad:\n"
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        pass\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: external projects list preserves every entry and source location",
          "[parser][extern-rule][handler-contract]") {
    auto prog = parse(
        "extern rule ExternalLayout:\n"
        "    on input:\n"
        "        reads:\n"
        "            Node\n"
        "        projects:\n"
        "            DesiredSize\n"
        "            ui.ComputedLayout\n"
        "            interaction.PointerHit\n"
        "        writes:\n"
        "            PointerState\n"
        "        effects:\n"
        "            graphics\n");

    REQUIRE(prog.declarations.size() == 1);
    const auto& rule = std::get<ExternRuleNode>(prog.declarations[0]);
    REQUIRE(rule.handlers.size() == 1);
    const auto& handler = rule.handlers.front();
    REQUIRE(handler.projects.size() == 3);
    CHECK(handler.projects[0].spelling == "DesiredSize");
    CHECK(handler.projects[0].location == SourceLocation{"test.cactus", 7, 13});
    CHECK(handler.projects[1].spelling == "ui.ComputedLayout");
    CHECK(handler.projects[1].location == SourceLocation{"test.cactus", 8, 13});
    CHECK(handler.projects[2].spelling == "interaction.PointerHit");
    CHECK(handler.projects[2].location == SourceLocation{"test.cactus", 9, 13});
    REQUIRE(handler.reads.size() == 1);
    REQUIRE(handler.writes.size() == 1);
    REQUIRE(handler.effects.size() == 1);
}

TEST_CASE("Parser: canonical frame phase graph preserves dependencies and initializers",
          "[parser][phase][frame-graph]") {
    auto prog = parse(
        "module std.core\n"
        "pub extern event frame:\n"
        "    dt: float\n"
        "pub phase input:\n"
        "    from:\n"
        "        frame\n"
        "pub phase fixed_tick:\n"
        "    after:\n"
        "        input\n"
        "    every: 1.0 / 60.0\n"
        "    max: 8\n"
        "pub phase tick:\n"
        "    after:\n"
        "        fixed_tick\n"
        "    dt: float = frame.dt\n"
        "pub phase late_tick:\n"
        "    after:\n"
        "        tick\n"
        "    dt: float = frame.dt\n"
        "pub phase render:\n"
        "    after:\n"
        "        late_tick\n"
        "    alpha: float = fixed_tick.alpha\n");

    REQUIRE(prog.declarations.size() == 7);
    const auto& frame = std::get<EventNode>(prog.declarations[1]);
    CHECK(frame.is_pub);
    CHECK(frame.is_external);
    CHECK(frame.location.line == 2);
    REQUIRE(frame.fields.size() == 1);
    CHECK(frame.fields[0].location.line == 3);

    const auto& input = std::get<PhaseNode>(prog.declarations[2]);
    CHECK(input.name == "input");
    REQUIRE(input.from_sources.size() == 1);
    CHECK(input.from_sources[0].spelling == "frame");
    CHECK(input.from_sources[0].location.line == 6);
    CHECK(input.location.line == 4);

    const auto& fixed_tick = std::get<PhaseNode>(prog.declarations[3]);
    REQUIRE(fixed_tick.after_phases.size() == 1);
    CHECK(fixed_tick.after_phases[0].spelling == "input");
    REQUIRE(fixed_tick.every.has_value());
    REQUIRE(fixed_tick.max.has_value());

    const auto& tick = std::get<PhaseNode>(prog.declarations[4]);
    REQUIRE(tick.after_phases.size() == 1);
    CHECK(tick.after_phases[0].spelling == "fixed_tick");
    REQUIRE(tick.fields.size() == 1);
    CHECK(tick.fields[0].name == "dt");
    CHECK(tick.fields[0].location.line == 15);

    const auto& late_tick = std::get<PhaseNode>(prog.declarations[5]);
    REQUIRE(late_tick.after_phases.size() == 1);
    CHECK(late_tick.after_phases[0].spelling == "tick");
    REQUIRE(late_tick.fields.size() == 1);

    const auto& render = std::get<PhaseNode>(prog.declarations[6]);
    REQUIRE(render.after_phases.size() == 1);
    CHECK(render.after_phases[0].spelling == "late_tick");
    REQUIRE(render.fields.size() == 1);
    const auto* alpha_member = std::get_if<MemberExpr>(&render.fields[0].initializer->expr);
    REQUIRE(alpha_member != nullptr);
    CHECK(alpha_member->member == "alpha");
    const auto* fixed_tick_root = std::get_if<IdentExpr>(&alpha_member->object->expr);
    REQUIRE(fixed_tick_root != nullptr);
    CHECK(fixed_tick_root->name == "fixed_tick");
}

TEST_CASE("Parser: selectionless producer and selected renderer contracts preserve qualified entries",
          "[parser][extern-rule][handler-contract]") {
    auto prog = parse(
        "extern rule InputSource:\n"
        "    on std.core.input as input_phase:\n"
        "        emits:\n"
        "            game.input.InputSample\n"
        "extern rule SpriteRenderer:\n"
        "    each:\n"
        "        render.Sprite as sprite\n"
        "        transform.WorldTransform as world\n"
        "    on std.core.render:\n"
        "        after:\n"
        "            game.Animation/on std.core.render\n"
        "        reads:\n"
        "            render.Sprite\n"
        "            transform.WorldTransform\n"
        "        effects:\n"
        "            graphics\n");

    REQUIRE(prog.declarations.size() == 2);
    const auto& producer = std::get<ExternRuleNode>(prog.declarations[0]);
    CHECK(producer.filter.entries.empty());
    CHECK(producer.exclude.entries.empty());
    REQUIRE(producer.handlers.size() == 1);
    CHECK(producer.handlers[0].trigger_name == "std.core.input");
    CHECK(producer.handlers[0].trigger_location == SourceLocation{"test.cactus", 3, 8});
    REQUIRE(producer.handlers[0].alias.has_value());
    CHECK(*producer.handlers[0].alias == "input_phase");
    REQUIRE(producer.handlers[0].emits.size() == 1);
    CHECK(producer.handlers[0].emits[0].spelling == "game.input.InputSample");
    CHECK(producer.handlers[0].emits[0].location == SourceLocation{"test.cactus", 5, 13});

    const auto& renderer = std::get<ExternRuleNode>(prog.declarations[1]);
    REQUIRE(renderer.filter.entries.size() == 2);
    REQUIRE(renderer.handlers.size() == 1);
    const auto& render_handler = renderer.handlers[0];
    CHECK(render_handler.trigger_name == "std.core.render");
    CHECK(render_handler.trigger_location == SourceLocation{"test.cactus", 10, 8});
    REQUIRE(render_handler.after_handlers.size() == 1);
    CHECK(render_handler.after_handlers[0].spelling() == "game.Animation/on std.core.render");
    CHECK(render_handler.after_handlers[0].location == SourceLocation{"test.cactus", 12, 13});
    REQUIRE(render_handler.reads.size() == 2);
    CHECK(render_handler.reads[0].spelling == "render.Sprite");
    CHECK(render_handler.reads[1].spelling == "transform.WorldTransform");
    CHECK(render_handler.reads[0].location == SourceLocation{"test.cactus", 14, 13});
    CHECK(render_handler.reads[1].location == SourceLocation{"test.cactus", 15, 13});
    REQUIRE(render_handler.effects.size() == 1);
    CHECK(render_handler.effects[0].spelling == "graphics");
    CHECK(render_handler.effects[0].location == SourceLocation{"test.cactus", 17, 13});
    CHECK(render_handler.location.line == 10);
}

TEST_CASE("Parser: handler after clause must precede executable statements", "[parser][handler-contract][errors]") {
    auto errors = parse_expect_errors(
        "rule Move:\n"
        "    on tick:\n"
        "        let elapsed = 1\n"
        "        after:\n"
        "            game.Input/on tick\n");

    bool found = false;
    for (const auto& diagnostic : errors.diagnostics()) {
        if (diagnostic.message.contains("handler after: clause must be the first entry")) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Parser: phase and contract clauses require indented blocks", "[parser][phase][handler-contract][errors]") {
    auto phase_errors = parse_expect_errors(
        "phase tick:\n"
        "    from:\n"
        "    frame\n");
    CHECK(phase_errors.has_errors());

    auto contract_errors = parse_expect_errors(
        "extern rule InputSource:\n"
        "    on input:\n"
        "        emits:\n"
        "        InputSample\n");
    CHECK(contract_errors.has_errors());
}

TEST_CASE("Parser: malformed external handler aliases recover without looping",
          "[parser][handler-contract][errors][recovery]") {
    auto errors = parse_expect_errors(
        "extern rule InputSource:\n"
        "    on input as phase:\n"
        "        emits:\n"
        "            InputSample\n");

    CHECK(errors.has_errors());
}

TEST_CASE("Parser: phases, external events, handler order, and external contracts",
          "[parser][phase][extern-rule][handler-contract]") {
    auto prog = parse(
        "extern event HostTick:\n"
        "    dt: float\n"
        "pub phase Update:\n"
        "    from:\n"
        "        HostTick\n"
        "    every: 0.016\n"
        "    max: 4\n"
        "    dt: float = 0.0\n"
        "rule Move:\n"
        "    on Update:\n"
        "        after:\n"
        "            game.Input/on Update\n"
        "        let elapsed = 1\n"
        "extern rule Physics:\n"
        "    on Update:\n"
        "        reads:\n"
        "            Position\n"
        "        writes:\n"
        "            Velocity\n"
        "        emits:\n"
        "            Collision\n"
        "        commands:\n"
        "            spawn Projectile\n"
        "            destroy\n"
        "            add Active\n"
        "            remove Sleeping\n"
        "        effects:\n"
        "            physics.step\n");

    REQUIRE(prog.declarations.size() == 4);
    const auto& event = std::get<EventNode>(prog.declarations[0]);
    CHECK(event.is_external);
    CHECK(event.name == "HostTick");

    const auto& phase = std::get<PhaseNode>(prog.declarations[1]);
    CHECK(phase.is_pub);
    CHECK(phase.name == "Update");
    REQUIRE(phase.from_sources.size() == 1);
    CHECK(phase.from_sources[0].spelling == "HostTick");
    REQUIRE(phase.every.has_value());
    REQUIRE(phase.max.has_value());
    REQUIRE(phase.fields.size() == 1);
    CHECK(phase.fields[0].name == "dt");

    const auto& rule = std::get<RuleNode>(prog.declarations[2]);
    REQUIRE(rule.handlers.size() == 1);
    REQUIRE(rule.handlers[0].after_handlers.size() == 1);
    CHECK(rule.handlers[0].after_handlers[0].spelling() == "game.Input/on Update");
    REQUIRE(rule.handlers[0].body.size() == 1);

    const auto& external = std::get<ExternRuleNode>(prog.declarations[3]);
    REQUIRE(external.handlers.size() == 1);
    const auto& contract = external.handlers[0];
    CHECK(contract.trigger_name == "Update");
    REQUIRE(contract.reads.size() == 1);
    CHECK(contract.reads[0].spelling == "Position");
    REQUIRE(contract.writes.size() == 1);
    CHECK(contract.writes[0].spelling == "Velocity");
    REQUIRE(contract.emits.size() == 1);
    CHECK(contract.emits[0].spelling == "Collision");
    REQUIRE(contract.effects.size() == 1);
    CHECK(contract.effects[0].spelling == "physics.step");
    REQUIRE(contract.commands.size() == 4);
    CHECK(contract.commands[0].kind == HandlerCommandKind::Spawn);
    CHECK(contract.commands[1].kind == HandlerCommandKind::Destroy);
    CHECK(contract.commands[2].kind == HandlerCommandKind::Add);
    CHECK(contract.commands[3].kind == HandlerCommandKind::Remove);
}

TEST_CASE("Parser: extern handler set command", "[parser][deferred-set]") {
    auto prog = parse(
        "extern rule HostHeal:\n"
        "    on pulse:\n"
        "        commands:\n"
        "            set Health\n");
    const auto& external = std::get<ExternRuleNode>(prog.declarations[0]);
    REQUIRE(external.handlers.size() == 1);
    REQUIRE(external.handlers[0].commands.size() == 1);
    CHECK(external.handlers[0].commands[0].kind == HandlerCommandKind::Set);
    REQUIRE(external.handlers[0].commands[0].target.has_value());
    CHECK(external.handlers[0].commands[0].target->spelling == "Health");
}

TEST_CASE("Parser: trait match statement single arm with alias", "[parser][trait-match]") {
    auto prog = parse(
        "event Collision:\n"
        "    other: entity_id\n"
        "rule Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.phase\n");
    auto& sys        = std::get<RuleNode>(prog.declarations[1]);
    auto* match_stmt = std::get_if<MatchStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(match_stmt != nullptr);
    REQUIRE(match_stmt->arms.size() == 1);
    CHECK(match_stmt->arms[0].trait_name() == "Boss");
    REQUIRE(match_stmt->arms[0].alias.has_value());
    CHECK(*match_stmt->arms[0].alias == "b");
}

TEST_CASE("Parser: trait match statement multiple arms and wildcard", "[parser][trait-match]") {
    auto prog = parse(
        "event Collision:\n"
        "    other: entity_id\n"
        "rule Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                pass\n"
        "            EnemyAI =>\n"
        "                pass\n"
        "            _ =>\n"
        "                pass\n");
    auto& sys        = std::get<RuleNode>(prog.declarations[1]);
    auto* match_stmt = std::get_if<MatchStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(match_stmt != nullptr);
    REQUIRE(match_stmt->arms.size() == 3);
    CHECK(match_stmt->arms[0].trait_name() == "Boss");
    CHECK(match_stmt->arms[1].trait_name() == "EnemyAI");
    CHECK(match_stmt->arms[2].is_wildcard());
}

TEST_CASE("Parser: add statement with block parsed", "[parser][dynamic-traits]") {
    auto prog = parse(
        "rule FreezeSystem:\n"
        "    on tick:\n"
        "        add Health:\n"
        "            current = 100\n"
        "            max = 100\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    auto* add = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(add != nullptr);
    CHECK(add->trait_name == "Health");
    REQUIRE(add->args.size() == 2);
    CHECK(add->args[0].name == "current");
    CHECK(add->args[1].name == "max");
}

TEST_CASE("Parser: add and remove statements with cross-entity targets parsed", "[parser][dynamic-traits]") {
    auto prog = parse(
        "rule FreezeSystem:\n"
        "    on tick:\n"
        "        add Frozen to target_id:\n"
        "            duration = 2.0\n"
        "        remove Frozen from target_id\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 2);
    auto* add    = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    auto* remove = std::get_if<RemoveTraitStmt>(&sys.handlers[0].body[1]->stmt);
    REQUIRE(add != nullptr);
    REQUIRE(remove != nullptr);
    REQUIRE(add->target_expr.has_value());
    REQUIRE(remove->target_expr.has_value());
}

TEST_CASE("Parser: rule with multiple after: entries", "[parser][rule-ordering]") {
    auto prog = parse(
        "rule Debug:\n"
        "    after:\n"
        "        SceneRender\n"
        "        UIRender\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.after_rules.size() == 2);
    CHECK(sys.after_rules[0] == "SceneRender");
    CHECK(sys.after_rules[1] == "UIRender");
}

// ── dsl-event-handler-syntax parser tests ───────────────────────────────────

TEST_CASE("Parser: on tick with alias parsed", "[parser][dsl-event-handler-syntax]") {
    auto prog = parse(
        "rule Move:\n"
        "    each:\n"
        "        Position\n"
        "    on tick as t:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "tick");
    REQUIRE(sys.handlers[0].alias.has_value());
    CHECK(*sys.handlers[0].alias == "t");
}

TEST_CASE("Parser: on user event with alias parsed", "[parser][dsl-event-handler-syntax]") {
    auto prog = parse(
        "rule Combat:\n"
        "    on PlayerDamaged as dmg:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "PlayerDamaged");
    REQUIRE(sys.handlers[0].alias.has_value());
    CHECK(*sys.handlers[0].alias == "dmg");
}

TEST_CASE("Parser: on added trigger parsed", "[parser][trait-lifecycle]") {
    auto prog = parse(
        "rule StartDying:\n"
        "    on added Dying:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].trigger_form == HandlerTriggerForm::Added);
    CHECK(sys.handlers[0].event_name == "Dying");
    CHECK_FALSE(sys.handlers[0].alias.has_value());
}

TEST_CASE("Parser: on removed trigger with qualified trait and alias parsed", "[parser][trait-lifecycle]") {
    auto prog = parse(
        "rule StopBurning:\n"
        "    on removed fx.Burning as old:\n"
        "        x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].trigger_form == HandlerTriggerForm::Removed);
    CHECK(sys.handlers[0].event_name == "fx.Burning");
    REQUIRE(sys.handlers[0].alias.has_value());
    CHECK(*sys.handlers[0].alias == "old");
}

TEST_CASE("Parser: added without a trait name is an event name", "[parser][trait-lifecycle]") {
    auto prog = parse(
        "rule OnAdded:\n"
        "    on added:\n"
        "        x = 1\n"
        "    on removed as r:\n"
        "        x = 2\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 2);
    CHECK(sys.handlers[0].trigger_form == HandlerTriggerForm::Event);
    CHECK(sys.handlers[0].event_name == "added");
    CHECK(sys.handlers[1].trigger_form == HandlerTriggerForm::Event);
    CHECK(sys.handlers[1].event_name == "removed");
    REQUIRE(sys.handlers[1].alias.has_value());
    CHECK(*sys.handlers[1].alias == "r");
}

TEST_CASE("Parser: marker event declaration (no colon, no body)", "[parser][dsl-event-handler-syntax]") {
    auto prog = parse("pub event MarkerEvent\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EventNode>(prog.declarations[0]);
    CHECK(decl.name == "MarkerEvent");
    CHECK(decl.is_pub);
    CHECK(decl.fields.empty());
}

TEST_CASE("Parser: marker event without pub", "[parser][dsl-event-handler-syntax]") {
    auto prog = parse("event MyEvent\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EventNode>(prog.declarations[0]);
    CHECK(decl.name == "MyEvent");
    CHECK_FALSE(decl.is_pub);
    CHECK(decl.fields.empty());
}

TEST_CASE("Parser: malformed struct fixture completes without hanging", "[parser][recovery]") {
    auto errors = parse_fixture_expect_errors_with_timeout("malformed_struct.cactus");
    CHECK(errors.error_count() >= 2);
}

TEST_CASE("Parser: malformed trait fixture completes without hanging", "[parser][recovery]") {
    auto errors = parse_fixture_expect_errors_with_timeout("malformed_trait.cactus");
    CHECK(errors.error_count() >= 2);
}

TEST_CASE("Parser: malformed rule fixture completes without hanging", "[parser][recovery]") {
    auto errors = parse_fixture_expect_errors_with_timeout("malformed_system.cactus");
    CHECK(errors.error_count() >= 2);
}

TEST_CASE("Parser: malformed nested fixture completes without hanging", "[parser][recovery]") {
    auto errors = parse_fixture_expect_errors_with_timeout("malformed_nested.cactus");
    CHECK(errors.error_count() >= 1);
}

TEST_CASE("Parser: multiple independent errors are reported in one pass", "[parser][recovery]") {
    auto errors = parse_expect_errors(
        "struct Broken:\n"
        "    name int\n"
        "\n"
        "trait AlsoBroken:\n"
        "    var hp int\n",
        "multi_error.cactus");

    CHECK(errors.error_count() >= 2);
}

TEST_CASE("Parser: error messages retain source locations during recovery", "[parser][recovery]") {
    auto errors = parse_expect_errors(
        "struct Broken:\n"
        "    name int\n",
        "location_test.cactus");

    REQUIRE_FALSE(errors.diagnostics().empty());
    CHECK(errors.diagnostics().front().location.filename == "location_test.cactus");
    CHECK(errors.diagnostics().front().location.line == 3);
}

TEST_CASE("Parser: synchronization limits spurious cascade errors", "[parser][recovery]") {
    auto errors = parse_expect_errors(read_fixture("malformed_struct.cactus"), "malformed_struct.cactus");
    CHECK(errors.error_count() >= 2);
    CHECK(errors.error_count() <= 8);
}

// ── Task 1.5: entity / template-backed entity parser tests ──────────────────

TEST_CASE("Parser: inline entity parsed", "[parser][entity]") {
    auto prog = parse(
        "entity LevelDirector:\n"
        "    LevelState:\n"
        "        wave_index = 0\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& entity = std::get<EntityNode>(prog.declarations[0]);
    CHECK(entity.name == "LevelDirector");
    CHECK_FALSE(entity.template_ref.has_value());
    REQUIRE(entity.traits.size() == 1);
    CHECK(entity.traits[0].trait_name == "LevelState");
}

TEST_CASE("Parser: template-backed entity from local template parsed", "[parser][entity]") {
    auto prog = parse(
        "entity Gem1 from BlueGem:\n"
        "    WorldTransform:\n"
        "        position = vec2(250.0, 560.0)\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& entity = std::get<EntityNode>(prog.declarations[0]);
    CHECK(entity.name == "Gem1");
    REQUIRE(entity.template_ref.has_value());
    CHECK(*entity.template_ref == "BlueGem");
    REQUIRE(entity.traits.size() == 1);
    CHECK(entity.traits[0].trait_name == "WorldTransform");
}

TEST_CASE("Parser: template-backed entity from qualified template parsed", "[parser][entity]") {
    auto prog = parse(
        "entity Gem1 from items.BlueGem:\n"
        "    WorldTransform:\n"
        "        position = vec2(250.0, 560.0)\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& entity = std::get<EntityNode>(prog.declarations[0]);
    REQUIRE(entity.template_ref.has_value());
    CHECK(*entity.template_ref == "items.BlueGem");
}

TEST_CASE("Parser: template-backed entity from aliased template parsed", "[parser][entity]") {
    auto prog = parse(
        "use items.gems as gems\n"
        "entity Gem1 from gems.BlueGem:\n"
        "    WorldTransform:\n"
        "        position = vec2(250.0, 560.0)\n");
    REQUIRE(prog.declarations.size() == 2);
    auto& entity = std::get<EntityNode>(prog.declarations[1]);
    REQUIRE(entity.template_ref.has_value());
    CHECK(*entity.template_ref == "gems.BlueGem");
}

TEST_CASE("Parser: struct field named 'entity' produces reserved-keyword error and recovers", "[parser][reserved]") {
    auto errors = parse_expect_errors(
        "struct Data:\n"
        "    entity: entity_id\n"
        "    name: string\n");
    REQUIRE(errors.error_count() >= 1);
    bool has_kw_error = false;
    for (const auto& d : errors.diagnostics()) {
        if (d.message.contains("reserved keyword") && d.message.contains("entity")) {
            has_kw_error = true;
            break;
        }
    }
    CHECK(has_kw_error);
    // Verify recovery: subsequent field 'name' should parse successfully.
    // We expect fewer errors than before the fix (which would lose all remaining fields to sync).
    CHECK(errors.error_count() < 3);
}

TEST_CASE("Parser: trait field named 'entity' produces reserved-keyword error", "[parser][reserved]") {
    auto errors = parse_expect_errors(
        "trait Data:\n"
        "    let entity: entity_id\n");
    REQUIRE(errors.error_count() >= 1);
    bool has_kw_error = false;
    for (const auto& d : errors.diagnostics()) {
        if (d.message.contains("reserved keyword") && d.message.contains("entity")) {
            has_kw_error = true;
            break;
        }
    }
    CHECK(has_kw_error);
}

// ── std.editor module parser tests (add-std-editor) ─────────────────────────

TEST_CASE("Parser: std.editor module parses enum, traits, events, extern funcs, extern rules without errors",
          "[parser][stdlib][editor]") {
    auto prog = parse(
        "module std.editor\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "    Rotate\n"
        "    Scale\n"
        "    Place\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "    var selected: entity_id\n"
        "    var active_template: string = \"\"\n"
        "    var focused_trait: string = \"\"\n"
        "    var focused_field: string = \"\"\n"
        "pub entity Editor:\n"
        "    EditorState\n"
        "pub trait EditorSelected\n"
        "pub trait EditorLocked\n"
        "pub trait EditorHidden\n"
        "pub trait EditorSnap:\n"
        "    var position_snap: float = 0.0\n"
        "    var rotation_snap: float = 0.0\n"
        "    var scale_snap: float = 0.0\n"
        "pub trait EditorCategory:\n"
        "    let category: string\n"
        "    var visible: bool = true\n"
        "pub trait EditorGizmo2D:\n"
        "    var mode: GizmoMode = GizmoMode.Translate\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "pub trait EditorGizmo3D:\n"
        "    var mode: GizmoMode = GizmoMode.Translate\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "pub event EditorSelectionChanged:\n"
        "    previous: entity_id\n"
        "    current: entity_id\n"
        "pub event EditorModeChanged:\n"
        "    previous_mode: GizmoMode\n"
        "    current_mode: GizmoMode\n"
        "pub extern func spawn_template(template_name: string, position_2d: vec2, position_3d: vec3) entity_id\n"
        "pub extern func hit_test_2d(screen_pos: vec2, mask: int) entity_id\n"
        "pub extern func raycast_3d(screen_pos: vec2, mask: int) entity_id\n"
        "pub extern func camera_enter(use_3d: bool) entity_id\n"
        "pub extern func camera_exit()\n"
        "pub extern func apply_camera_2d(view_center: vec2, zoom: float)\n"
        "pub extern func apply_camera_3d(position: vec3, rotation: quat)\n"
        "pub extern rule EditorTemplatePalette:\n"
        "    each:\n"
        "        EditorState\n"
        "pub extern rule EditorPropertyPanel:\n"
        "    each:\n"
        "        EditorState\n"
        "pub extern rule GizmoRenderer2D:\n"
        "    each:\n"
        "        EditorGizmo2D\n"
        "pub extern rule GizmoRenderer3D:\n"
        "    each:\n"
        "        EditorGizmo3D\n");
    CHECK(prog.declarations.size() >= 20);
}

TEST_CASE("Parser: event field named 'entity' produces reserved-keyword error", "[parser][reserved]") {
    auto errors = parse_expect_errors(
        "event Data:\n"
        "    entity: entity_id\n");
    REQUIRE(errors.error_count() >= 1);
    bool has_kw_error = false;
    for (const auto& d : errors.diagnostics()) {
        if (d.message.contains("reserved keyword") && d.message.contains("entity")) {
            has_kw_error = true;
            break;
        }
    }
    CHECK(has_kw_error);
}
TEST_CASE("Parser: query call — one positive filter", "[parser][query]") {
    auto prog = parse(
        "func test():\n"
        "    let x = query.exists[Boss]()\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    auto* let  = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* qcall = std::get_if<QueryCallExpr>(&let->value->expr);
    REQUIRE(qcall != nullptr);
    REQUIRE(qcall->filters.size() == 1);
    CHECK(qcall->filters[0].trait_name == "Boss");
    CHECK_FALSE(qcall->filters[0].negated);
    CHECK(qcall->named_args.empty());
    auto* callee = std::get_if<MemberExpr>(&qcall->callee->expr);
    REQUIRE(callee != nullptr);
    CHECK(callee->member == "exists");
}

TEST_CASE("Parser: query call — negative trait filter", "[parser][query]") {
    auto prog = parse(
        "func test():\n"
        "    let x = query.count[EnemyAI, not Dead]()\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    auto* let  = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* qcall = std::get_if<QueryCallExpr>(&let->value->expr);
    REQUIRE(qcall != nullptr);
    REQUIRE(qcall->filters.size() == 2);
    CHECK(qcall->filters[0].trait_name == "EnemyAI");
    CHECK_FALSE(qcall->filters[0].negated);
    CHECK(qcall->filters[1].trait_name == "Dead");
    CHECK(qcall->filters[1].negated);
}

TEST_CASE("Parser: query call — named spatial argument", "[parser][query]") {
    auto prog = parse(
        "func test():\n"
        "    let x = query.nearest[Transform, Enemy](from = player_pos)\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    auto* let  = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* qcall = std::get_if<QueryCallExpr>(&let->value->expr);
    REQUIRE(qcall != nullptr);
    REQUIRE(qcall->filters.size() == 2);
    CHECK(qcall->filters[0].trait_name == "Transform");
    CHECK(qcall->filters[1].trait_name == "Enemy");
    REQUIRE(qcall->named_args.size() == 1);
    CHECK(qcall->named_args[0].name == "from");
}

TEST_CASE("Parser: query call — multiple named arguments", "[parser][query]") {
    auto prog = parse(
        "func test():\n"
        "    let x = query.overlap_box[Pickup](center = p, size = s)\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    auto* let  = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* qcall = std::get_if<QueryCallExpr>(&let->value->expr);
    REQUIRE(qcall != nullptr);
    REQUIRE(qcall->named_args.size() == 2);
    CHECK(qcall->named_args[0].name == "center");
    CHECK(qcall->named_args[1].name == "size");
}

TEST_CASE("Parser: query call — named arg without filter bracket", "[parser][query]") {
    auto prog = parse(
        "func test():\n"
        "    let x = query.parent(of = child_id)\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    auto* let  = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* qcall = std::get_if<QueryCallExpr>(&let->value->expr);
    REQUIRE(qcall != nullptr);
    CHECK(qcall->filters.empty());
    REQUIRE(qcall->named_args.size() == 1);
    CHECK(qcall->named_args[0].name == "of");
    auto* callee = std::get_if<MemberExpr>(&qcall->callee->expr);
    REQUIRE(callee != nullptr);
    CHECK(callee->member == "parent");
}

TEST_CASE("Parser: query call — module-qualified path", "[parser][query]") {
    auto prog = parse(
        "func test():\n"
        "    let x = std.query.first[Boss]()\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    auto* let  = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* qcall = std::get_if<QueryCallExpr>(&let->value->expr);
    REQUIRE(qcall != nullptr);
    REQUIRE(qcall->filters.size() == 1);
    CHECK(qcall->filters[0].trait_name == "Boss");
    auto* outer = std::get_if<MemberExpr>(&qcall->callee->expr);
    REQUIRE(outer != nullptr);
    CHECK(outer->member == "first");
    auto* inner = std::get_if<MemberExpr>(&outer->object->expr);
    REQUIRE(inner != nullptr);
    CHECK(inner->member == "query");
}

// ── Hierarchical entity templates (dsl-hierarchical-entity-templates) ───────

TEST_CASE("Parser: template with direct children", "[parser][hierarchy]") {
    auto prog = parse(
        "template TreeTemplate:\n"
        "    LocalTransform\n"
        "    children:\n"
        "        entity Trunk:\n"
        "            Renderer\n"
        "        entity Crown from CrownTemplate:\n"
        "            LocalTransform:\n"
        "                offset = 2.0\n");
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    REQUIRE(tmpl.traits.size() == 1);
    CHECK(tmpl.traits[0].trait_name == "LocalTransform");
    REQUIRE(tmpl.children.size() == 2);
    CHECK(tmpl.children[0].role == "Trunk");
    CHECK_FALSE(tmpl.children[0].template_ref.has_value());
    REQUIRE(tmpl.children[0].traits.size() == 1);
    CHECK(tmpl.children[0].traits[0].trait_name == "Renderer");
    CHECK(tmpl.children[1].role == "Crown");
    REQUIRE(tmpl.children[1].template_ref.has_value());
    CHECK(*tmpl.children[1].template_ref == "CrownTemplate");
    REQUIRE(tmpl.children[1].traits.size() == 1);
    REQUIRE(tmpl.children[1].traits[0].assignments.size() == 1);
    CHECK(tmpl.children[1].traits[0].assignments[0].name == "offset");
}

TEST_CASE("Parser: template with grandchildren", "[parser][hierarchy]") {
    auto prog = parse(
        "template PlayerRig:\n"
        "    LocalTransform\n"
        "    children:\n"
        "        entity WeaponSocket:\n"
        "            LocalTransform\n"
        "            children:\n"
        "                entity Sword from SwordTemplate:\n"
        "                    LocalTransform\n");
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    REQUIRE(tmpl.children.size() == 1);
    auto& socket = tmpl.children[0];
    CHECK(socket.role == "WeaponSocket");
    REQUIRE(socket.children.size() == 1);
    CHECK(socket.children[0].role == "Sword");
    REQUIRE(socket.children[0].template_ref.has_value());
    CHECK(*socket.children[0].template_ref == "SwordTemplate");
    // Grandchild is nested, not a sibling of the root's children.
    CHECK(socket.children[0].children.empty());
}

TEST_CASE("Parser: child body can mix use entries and traits", "[parser][hierarchy]") {
    auto prog = parse(
        "template Rig:\n"
        "    Tag\n"
        "    children:\n"
        "        entity Body:\n"
        "            use RenderableBase\n"
        "            Health:\n"
        "                hp = 10\n");
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    REQUIRE(tmpl.children.size() == 1);
    auto& body = tmpl.children[0];
    REQUIRE(body.template_uses.size() == 1);
    CHECK(body.template_uses[0].template_name == "RenderableBase");
    REQUIRE(body.traits.size() == 1);
    CHECK(body.traits[0].trait_name == "Health");
}

TEST_CASE("Parser: template-backed entity with nested child overrides", "[parser][hierarchy]") {
    auto prog = parse(
        "entity Player1 from PlayerRig:\n"
        "    LocalTransform:\n"
        "        position = vec3(0.0, 0.0, 0.0)\n"
        "    children:\n"
        "        WeaponSocket:\n"
        "            LocalTransform:\n"
        "                position = vec3(0.5, 1.1, 0.0)\n"
        "            children:\n"
        "                Sword:\n"
        "                    Renderer:\n"
        "                        material = BlueSwordMaterial\n");
    auto& entity = std::get<EntityNode>(prog.declarations[0]);
    REQUIRE(entity.template_ref.has_value());
    REQUIRE(entity.traits.size() == 1);
    CHECK(entity.traits[0].trait_name == "LocalTransform");
    CHECK(entity.children.empty());
    REQUIRE(entity.child_overrides.size() == 1);
    auto& socket = entity.child_overrides[0];
    CHECK(socket.role == "WeaponSocket");
    REQUIRE(socket.traits.size() == 1);
    CHECK(socket.traits[0].trait_name == "LocalTransform");
    REQUIRE(socket.children.size() == 1);
    CHECK(socket.children[0].role == "Sword");
    REQUIRE(socket.children[0].traits.size() == 1);
    CHECK(socket.children[0].traits[0].trait_name == "Renderer");
    REQUIRE(socket.children[0].traits[0].assignments.size() == 1);
    CHECK(socket.children[0].traits[0].assignments[0].name == "material");
}

TEST_CASE("Parser: spawn statement with child overrides", "[parser][hierarchy]") {
    auto prog = parse(
        "rule Spawner:\n"
        "    on tick:\n"
        "        spawn PlayerRig:\n"
        "            LocalTransform:\n"
        "                position = vec3(1.0, 0.0, 0.0)\n"
        "            children:\n"
        "                WeaponSocket:\n"
        "                    LocalTransform:\n"
        "                        position = vec3(0.5, 1.1, 0.0)\n");
    auto& sys   = std::get<RuleNode>(prog.declarations[0]);
    auto* spawn = std::get_if<SpawnStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(spawn != nullptr);
    REQUIRE(spawn->overrides.size() == 1);
    CHECK(spawn->overrides[0].trait_name == "LocalTransform");
    REQUIRE(spawn->child_overrides.size() == 1);
    CHECK(spawn->child_overrides[0].role == "WeaponSocket");
    REQUIRE(spawn->child_overrides[0].traits.size() == 1);
    CHECK(spawn->child_overrides[0].traits[0].trait_name == "LocalTransform");
}

TEST_CASE("Parser: spawn expression with child overrides", "[parser][hierarchy]") {
    auto prog = parse(
        "rule Spawner:\n"
        "    on tick:\n"
        "        let root = spawn PlayerRig:\n"
        "            Tag\n"
        "            children:\n"
        "                WeaponSocket:\n"
        "                    children:\n"
        "                        Sword:\n"
        "                            Renderer:\n"
        "                                tint = 1.0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    auto* let = std::get_if<LetStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* spawn = std::get_if<SpawnExpr>(&let->value->expr);
    REQUIRE(spawn != nullptr);
    REQUIRE(spawn->overrides.size() == 1);
    CHECK(spawn->overrides[0].trait_name == "Tag");
    REQUIRE(spawn->child_overrides.size() == 1);
    REQUIRE(spawn->child_overrides[0].children.size() == 1);
    CHECK(spawn->child_overrides[0].children[0].role == "Sword");
    REQUIRE(spawn->child_overrides[0].children[0].traits.size() == 1);
    CHECK(spawn->child_overrides[0].children[0].traits[0].trait_name == "Renderer");
}

TEST_CASE("Parser: flat archetype bodies remain valid with no children", "[parser][hierarchy]") {
    auto prog = parse(
        "template Flat:\n"
        "    Health:\n"
        "        hp = 5\n"
        "entity Static:\n"
        "    use Flat\n"
        "    Tag\n");
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    CHECK(tmpl.children.empty());
    auto& entity = std::get<EntityNode>(prog.declarations[1]);
    CHECK(entity.children.empty());
    CHECK(entity.child_overrides.empty());
}

TEST_CASE("Parser: children is not reserved outside archetype bodies", "[parser][hierarchy]") {
    auto prog = parse(
        "func children() int:\n"
        "    return 3\n"
        "func test():\n"
        "    let child = children()\n");
    auto& func = std::get<FuncNode>(prog.declarations[0]);
    CHECK(func.name == "children");
}

TEST_CASE("Parser: children declaration block requires entity keyword", "[parser][hierarchy]") {
    parse_expect_errors(
        "template Broken:\n"
        "    children:\n"
        "        Trunk:\n"
        "            Renderer\n");
}

// ── module-qualified-symbol-identity: parser coverage (tasks 2.1-2.6) ────────

TEST_CASE("Parser: qualified trait entry in entity body", "[parser][module-qualified]") {
    auto prog = parse(
        "entity Player:\n"
        "    flat.WorldTransform:\n"
        "        x = 0.0\n"
        "    health.Health\n");
    auto& entity = std::get<EntityNode>(prog.declarations[0]);
    REQUIRE(entity.traits.size() == 2);
    CHECK(entity.traits[0].trait_name == "flat.WorldTransform");
    REQUIRE(entity.traits[0].assignments.size() == 1);
    CHECK(entity.traits[0].assignments[0].name == "x");
    CHECK(entity.traits[1].trait_name == "health.Health");
}

TEST_CASE("Parser: qualified trait entry in template body", "[parser][module-qualified]") {
    auto prog = parse(
        "template Enemy:\n"
        "    physics.RigidBody:\n"
        "        mass = 1.0\n"
        "    ai.EnemyAI\n");
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    REQUIRE(tmpl.traits.size() == 2);
    CHECK(tmpl.traits[0].trait_name == "physics.RigidBody");
    REQUIRE(tmpl.traits[0].assignments.size() == 1);
    CHECK(tmpl.traits[0].assignments[0].name == "mass");
    CHECK(tmpl.traits[1].trait_name == "ai.EnemyAI");
}

TEST_CASE("Parser: qualified add remove project statements", "[parser][module-qualified]") {
    auto prog = parse(
        "rule TagSystem:\n"
        "    on tick:\n"
        "        add editor.EditorSelected to self\n"
        "        project debug.Highlight to self\n"
        "        remove editor.EditorSelected from self\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 3);
    auto* add     = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    auto* project = std::get_if<ProjectTraitStmt>(&sys.handlers[0].body[1]->stmt);
    auto* remove  = std::get_if<RemoveTraitStmt>(&sys.handlers[0].body[2]->stmt);
    REQUIRE(add != nullptr);
    CHECK(add->trait_name == "editor.EditorSelected");
    REQUIRE(project != nullptr);
    CHECK(project->trait_name == "debug.Highlight");
    REQUIRE(remove != nullptr);
    CHECK(remove->trait_name == "editor.EditorSelected");
}

TEST_CASE("Parser: qualified spawn template name and override traits", "[parser][module-qualified]") {
    auto prog = parse(
        "rule Spawner:\n"
        "    on tick:\n"
        "        spawn enemies.Walker:\n"
        "            flat.WorldTransform:\n"
        "                x = 1.0\n");
    auto& sys   = std::get<RuleNode>(prog.declarations[0]);
    auto* spawn = std::get_if<SpawnStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(spawn != nullptr);
    CHECK(spawn->template_name == "enemies.Walker");
    REQUIRE(spawn->overrides.size() == 1);
    CHECK(spawn->overrides[0].trait_name == "flat.WorldTransform");
}

TEST_CASE("Parser: qualified spawn expression template and override traits", "[parser][module-qualified]") {
    auto prog = parse(
        "rule Spawner:\n"
        "    on tick:\n"
        "        let e = spawn enemies.Walker:\n"
        "            flat.WorldTransform:\n"
        "                x = 0.0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    auto* let = std::get_if<LetStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* spawn = std::get_if<SpawnExpr>(&let->value->expr);
    REQUIRE(spawn != nullptr);
    CHECK(spawn->template_name == "enemies.Walker");
    REQUIRE(spawn->overrides.size() == 1);
    CHECK(spawn->overrides[0].trait_name == "flat.WorldTransform");
}

TEST_CASE("Parser: qualified trait match arm", "[parser][module-qualified]") {
    auto prog = parse(
        "rule MatchSys:\n"
        "    each:\n"
        "        Marker\n"
        "    on tick:\n"
        "        match self:\n"
        "            enemy.Boss as boss =>\n"
        "                add enemy.Activated\n"
        "            _ =>\n"
        "                add enemy.Idle\n");
    auto& sys   = std::get<RuleNode>(prog.declarations[0]);
    auto* match = std::get_if<MatchStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);
    CHECK(match->arms[0].trait_name() == "enemy.Boss");
    REQUIRE(match->arms[0].alias.has_value());
    CHECK(*match->arms[0].alias == "boss");
    CHECK(match->arms[1].is_wildcard());
}

TEST_CASE("Parser: qualified query filter predicate", "[parser][module-qualified]") {
    auto prog = parse(
        "rule QuerySys:\n"
        "    on tick:\n"
        "        let n = query.count[enemy.Boss, not enemy.Idle]()\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    auto* let = std::get_if<LetStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(let != nullptr);
    auto* qcall = std::get_if<QueryCallExpr>(&let->value->expr);
    REQUIRE(qcall != nullptr);
    REQUIRE(qcall->filters.size() == 2);
    CHECK(qcall->filters[0].trait_name == "enemy.Boss");
    CHECK_FALSE(qcall->filters[0].negated);
    CHECK(qcall->filters[1].trait_name == "enemy.Idle");
    CHECK(qcall->filters[1].negated);
}

TEST_CASE("Parser: qualified rule name in after clause", "[parser][module-qualified]") {
    auto prog = parse(
        "rule Follow2D:\n"
        "    after:\n"
        "        flat.TransformPropagation\n"
        "    on tick:\n"
        "        let x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.after_rules.size() == 1);
    CHECK(sys.after_rules[0] == "flat.TransformPropagation");
}

TEST_CASE("Parser: qualified after clause with multiple rules", "[parser][module-qualified]") {
    auto prog = parse(
        "rule Renderer:\n"
        "    after:\n"
        "        flat.TransformPropagation\n"
        "        volume.TransformPropagation\n"
        "    on tick:\n"
        "        let x = 1\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.after_rules.size() == 2);
    CHECK(sys.after_rules[0] == "flat.TransformPropagation");
    CHECK(sys.after_rules[1] == "volume.TransformPropagation");
}

static bool has_diagnostic_containing(const ErrorReporter& errors, const std::string& needle) {
    return std::ranges::any_of(errors.diagnostics(),
                               [&](const auto& diagnostic) { return diagnostic.message.contains(needle); });
}

TEST_CASE("Parser: pub group declaration with phase entry", "[parser][rule-groups]") {
    auto prog = parse(
        "pub group solve:\n"
        "    phase: fixed_tick\n"
        "group private_anchor:\n"
        "    phase: tick\n");
    REQUIRE(prog.declarations.size() == 2);
    const auto& solve = std::get<GroupNode>(prog.declarations[0]);
    CHECK(solve.name == "solve");
    CHECK(solve.is_pub);
    REQUIRE(solve.phase.has_value());
    CHECK(solve.phase->spelling == "fixed_tick");
    const auto& anchor = std::get<GroupNode>(prog.declarations[1]);
    CHECK(anchor.name == "private_anchor");
    CHECK_FALSE(anchor.is_pub);
    REQUIRE(anchor.phase.has_value());
    CHECK(anchor.phase->spelling == "tick");
}

TEST_CASE("Parser: group declaration without phase entry is rejected", "[parser][rule-groups]") {
    auto errors = parse_expect_errors(
        "group solve:\n"
        "rule A:\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(has_diagnostic_containing(errors, "group 'solve' is missing a 'phase:' entry"));
}

TEST_CASE("Parser: group declaration with duplicate phase entry is rejected", "[parser][rule-groups]") {
    auto errors = parse_expect_errors(
        "group solve:\n"
        "    phase: tick\n"
        "    phase: fixed_tick\n");
    CHECK(has_diagnostic_containing(errors, "duplicate phase: entry in group 'solve'"));
}

TEST_CASE("Parser: group declaration with unknown entry is rejected", "[parser][rule-groups]") {
    auto errors = parse_expect_errors(
        "group solve:\n"
        "    phase: tick\n"
        "    every: 1.0\n");
    CHECK(has_diagnostic_containing(errors, "group body only accepts 'phase:' and 'after:' entries"));
}

TEST_CASE("Parser: group declaration with an after: block", "[parser][rule-groups]") {
    auto prog = parse(
        "pub group contacts:\n"
        "    phase: fixed_tick\n"
        "    after:\n"
        "        solve\n"
        "        phys.move\n");
    const auto& group = std::get<GroupNode>(prog.declarations[0]);
    REQUIRE(group.phase.has_value());
    CHECK(group.phase->spelling == "fixed_tick");
    CHECK(group.after_groups == std::vector<std::string>{"solve", "phys.move"});
}

TEST_CASE("Parser: rule group: and before: clauses", "[parser][rule-groups]") {
    auto prog = parse(
        "rule MovePlayer:\n"
        "    group: solve\n"
        "    after:\n"
        "        Input\n"
        "    before:\n"
        "        Render\n"
        "        phys.solve\n"
        "    on fixed_tick:\n"
        "        let x = 1\n");
    const auto& rule = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(rule.group_ref.has_value());
    CHECK(*rule.group_ref == "solve");
    CHECK(rule.after_rules == std::vector<std::string>{"Input"});
    CHECK(rule.before_rules == std::vector<std::string>{"Render", "phys.solve"});
    CHECK(rule.handlers.size() == 1);
}

TEST_CASE("Parser: extern rule group: and before: clauses", "[parser][rule-groups]") {
    auto prog = parse(
        "extern rule MoveAndSlide:\n"
        "    group: phys.solve\n"
        "    before:\n"
        "        Render\n"
        "    on fixed_tick:\n"
        "        writes:\n"
        "            Position\n");
    const auto& rule = std::get<ExternRuleNode>(prog.declarations[0]);
    REQUIRE(rule.group_ref.has_value());
    CHECK(*rule.group_ref == "phys.solve");
    CHECK(rule.before_rules == std::vector<std::string>{"Render"});
}

TEST_CASE("Parser: rule without group: or before: leaves them empty", "[parser][rule-groups]") {
    auto prog = parse(
        "rule A:\n"
        "    on tick:\n"
        "        let x = 1\n");
    const auto& rule = std::get<RuleNode>(prog.declarations[0]);
    CHECK_FALSE(rule.group_ref.has_value());
    CHECK(rule.before_rules.empty());
}

TEST_CASE("Parser: group remains an ordinary identifier", "[parser][rule-groups]") {
    auto prog = parse(
        "trait Team:\n"
        "    var group: int = 0\n"
        "rule A:\n"
        "    on tick:\n"
        "        let group = 1\n"
        "        let before = group\n");
    REQUIRE(prog.declarations.size() == 2);
    const auto& team = std::get<TraitNode>(prog.declarations[0]);
    REQUIRE(team.fields.size() == 1);
    CHECK(team.fields[0].name == "group");
}

TEST_CASE("Parser: empty before: block is rejected", "[parser][rule-groups]") {
    auto errors = parse_expect_errors(
        "rule A:\n"
        "    before:\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(has_diagnostic_containing(errors, "before: block must contain at least one rule or group name"));
}

TEST_CASE("Parser: group: naming more than one group is rejected", "[parser][rule-groups]") {
    auto errors = parse_expect_errors(
        "rule A:\n"
        "    group: solve, render\n"
        "    on fixed_tick:\n"
        "        let x = 1\n");
    CHECK(has_diagnostic_containing(errors, "a rule joins at most one group"));
}

TEST_CASE("Parser: std.persistence save request is ordinary, outcomes are external",
          "[parser][persistence]") {
    auto prog = parse(
        "pub event SaveRequested:\n"
        "    slot: string\n"
        "    request_id: int\n"
        "pub extern event SaveCompleted:\n"
        "    slot: string\n"
        "    request_id: int\n"
        "pub extern event SaveFailed:\n"
        "    slot: string\n"
        "    request_id: int\n"
        "    code: string\n"
        "    message: string\n");

    REQUIRE(prog.declarations.size() == 3);

    const auto& requested = std::get<EventNode>(prog.declarations[0]);
    CHECK(requested.name == "SaveRequested");
    CHECK(requested.is_pub);
    CHECK_FALSE(requested.is_external);
    REQUIRE(requested.fields.size() == 2);
    CHECK(requested.fields[0].name == "slot");
    CHECK(requested.fields[1].name == "request_id");

    const auto& completed = std::get<EventNode>(prog.declarations[1]);
    CHECK(completed.name == "SaveCompleted");
    CHECK(completed.is_external);
    REQUIRE(completed.fields.size() == 2);

    const auto& failed = std::get<EventNode>(prog.declarations[2]);
    CHECK(failed.name == "SaveFailed");
    CHECK(failed.is_external);
    REQUIRE(failed.fields.size() == 4);
    CHECK(failed.fields[2].name == "code");
    CHECK(failed.fields[3].name == "message");
}

TEST_CASE("Parser: std.persistence restore request is ordinary, outcomes are external",
          "[parser][persistence]") {
    auto prog = parse(
        "pub event RestoreRequested:\n"
        "    slot: string\n"
        "    request_id: int\n"
        "pub extern event RestoreCompleted:\n"
        "    slot: string\n"
        "    request_id: int\n"
        "pub extern event RestoreFailed:\n"
        "    slot: string\n"
        "    request_id: int\n"
        "    code: string\n"
        "    message: string\n");

    REQUIRE(prog.declarations.size() == 3);

    const auto& requested = std::get<EventNode>(prog.declarations[0]);
    CHECK(requested.name == "RestoreRequested");
    CHECK(requested.is_pub);
    CHECK_FALSE(requested.is_external);
    REQUIRE(requested.fields.size() == 2);
    CHECK(requested.fields[0].name == "slot");
    CHECK(requested.fields[1].name == "request_id");

    const auto& completed = std::get<EventNode>(prog.declarations[1]);
    CHECK(completed.name == "RestoreCompleted");
    CHECK(completed.is_external);
    REQUIRE(completed.fields.size() == 2);

    const auto& failed = std::get<EventNode>(prog.declarations[2]);
    CHECK(failed.name == "RestoreFailed");
    CHECK(failed.is_external);
    REQUIRE(failed.fields.size() == 4);
    CHECK(failed.fields[2].name == "code");
    CHECK(failed.fields[3].name == "message");
}

TEST_CASE("Parser: state with marker, data and final variants", "[parser][exclusive-states]") {
    auto prog = parse(
        "pub state EnemyMode:\n"
        "    Idle\n"
        "    Chasing:\n"
        "        let target: entity_id\n"
        "    Dying final:\n"
        "        var elapsed: float = 0.0\n");
    REQUIRE(prog.declarations.size() == 1);
    const auto& state = std::get<StateNode>(prog.declarations[0]);
    CHECK(state.name == "EnemyMode");
    CHECK(state.is_pub);
    REQUIRE(state.variants.size() == 3);
    CHECK(state.variants[0].name == "Idle");
    CHECK(state.variants[1].name == "Chasing");
    CHECK(state.variants[2].name == "Dying");
    CHECK_FALSE(state.variants[0].is_final);
    CHECK_FALSE(state.variants[1].is_final);
    CHECK(state.variants[2].is_final);
    CHECK(state.variants[0].fields.empty());
    REQUIRE(state.variants[1].fields.size() == 1);
    CHECK(state.variants[1].fields[0].name == "target");
    CHECK(state.variants[1].fields[0].modifiers.is_let);
    REQUIRE(state.variants[2].fields.size() == 1);
    CHECK(state.variants[2].fields[0].name == "elapsed");
    CHECK(state.variants[2].fields[0].default_value.has_value());
}

TEST_CASE("Parser: a final marker variant needs no body", "[parser][exclusive-states]") {
    auto prog = parse(
        "state Phase:\n"
        "    Grounded\n"
        "    Gone final\n");
    const auto& state = std::get<StateNode>(prog.declarations[0]);
    CHECK_FALSE(state.is_pub);
    REQUIRE(state.variants.size() == 2);
    CHECK(state.variants[1].is_final);
    CHECK(state.variants[1].fields.empty());
}

TEST_CASE("Parser: an identifier named state is not a declaration", "[parser][exclusive-states]") {
    auto prog = parse(
        "trait Ctrl:\n"
        "    var state: int = 0\n"
        "rule R:\n"
        "    each:\n"
        "        Ctrl as ctrl\n"
        "    on tick:\n"
        "        ctrl.state = 1\n");
    REQUIRE(prog.declarations.size() == 2);
    const auto& trait = std::get<TraitNode>(prog.declarations[0]);
    REQUIRE(trait.fields.size() == 1);
    CHECK(trait.fields[0].name == "state");
    const auto& rule   = std::get<RuleNode>(prog.declarations[1]);
    const auto* assign = std::get_if<VarAssign>(&rule.handlers[0].body[0]->stmt);
    REQUIRE(assign != nullptr);
    CHECK(assign->name == "ctrl");
    CHECK(assign->path == std::vector<std::string>{"state"});
}

TEST_CASE("Parser: a state variant body holds only fields", "[parser][exclusive-states]") {
    auto errors = parse_expect_errors(
        "state Mode:\n"
        "    Idle:\n"
        "        x = 1\n");
    CHECK(has_diagnostic_containing(errors, "a state variant body can contain only field declarations"));
}

TEST_CASE("Parser: a state without a body parses with no variants", "[parser][exclusive-states]") {
    auto prog = parse(
        "state Mode:\n"
        "trait T\n");
    REQUIRE(prog.declarations.size() == 2);
    CHECK(std::get<StateNode>(prog.declarations[0]).variants.empty());
}

TEST_CASE("Parser: concise unary lists preserve aliases and entry locations", "[parser][concise]") {
    const auto program = parse(R"(
rule Move:
    each: phys.Position as pos, Motion, # selected state
    without: Frozen, Hidden
    order by: distance(pos.value, vec2(1.0, 2.0)) asc, pos.priority desc
    after: Input, phys.solve
    before: Draw, ui.paint
    where: touching(pos, pos), pos.priority > 0
    when: true, not false
    on tick:
        return
)");
    const auto& rule   = std::get<RuleNode>(program.declarations.front());
    REQUIRE(rule.filter.entries.size() == 2);
    CHECK(rule.filter.entries[0].qualified_name == "phys.Position");
    CHECK(rule.filter.entries[0].alias == "pos");
    CHECK(rule.filter.entries[0].location.column < rule.filter.entries[1].location.column);
    CHECK(rule.exclude.entries.size() == 2);
    REQUIRE(rule.order_by.size() == 2);
    CHECK_FALSE(rule.order_by[0].descending);
    CHECK(rule.order_by[1].descending);
    CHECK(rule.after_rules == std::vector<std::string>{"Input", "phys.solve"});
    CHECK(rule.before_rules == std::vector<std::string>{"Draw", "ui.paint"});
    REQUIRE(rule.where_clause.has_value());
    CHECK(rule.where_clause->predicates.size() == 2);
    REQUIRE(rule.when_clause.has_value());
    CHECK(rule.when_clause->predicates.size() == 2);
    CHECK(rule.handlers.size() == 1);
}

TEST_CASE("Parser: concise pair bindings and header grouping preserve contextual names", "[parser][concise]") {
    const auto program = parse(R"(
rule Count:
    pairs:
        each: Health as without, phys.Body
        without:
            Zone
    reduce per each:
        per = count()
    order by: per desc
    on tick:
        return
)");
    const auto& rule   = std::get<RuleNode>(program.declarations.front());
    REQUIRE(rule.pairs.has_value());
    REQUIRE(rule.pairs->bindings.size() == 2);
    CHECK(rule.pairs->bindings[0].name == "each");
    REQUIRE(rule.pairs->bindings[0].traits.size() == 2);
    CHECK(rule.pairs->bindings[0].traits[0].alias == "without");
    REQUIRE(rule.reduce.has_value());
    CHECK(rule.reduce->per_binding == "each");
    REQUIRE(rule.reduce->reducers.size() == 1);
    CHECK(rule.reduce->reducers[0].name == "per");
}

TEST_CASE("Parser: concise contracts retain every typed capability", "[parser][concise]") {
    const auto program = parse(R"(
extern rule Native:
    each: Position, Velocity
    without: Frozen
    on tick:
        after: Input/on tick, phys.Solve/on tick
        reads: Velocity, Config
        writes: Position
        projects: Contact, Focus
        emits: Hit, Sound
        commands: spawn Bullet, destroy, add Health, remove Frozen, set Health,
        effects: physics, audio
)");
    const auto& rule   = std::get<ExternRuleNode>(program.declarations.front());
    REQUIRE(rule.handlers.size() == 1);
    const auto& handler = rule.handlers.front();
    CHECK(handler.after_handlers.size() == 2);
    CHECK(handler.reads.size() == 2);
    CHECK(handler.writes.size() == 1);
    CHECK(handler.projects.size() == 2);
    CHECK(handler.emits.size() == 2);
    REQUIRE(handler.commands.size() == 5);
    CHECK(handler.commands.back().kind == HandlerCommandKind::Set);
    CHECK(handler.effects.size() == 2);
}

TEST_CASE("Parser: malformed concise lists recover to later declarations", "[parser][concise]") {
    const std::vector<std::string> clauses{
        "    each: , Position\n",
        "    each: Position,, Motion\n",
        "    each: Position\n        Motion\n",
        "    each: [Position, Motion]\n",
        "    each: Position\n    filter: Motion\n",
        "    without: Frozen\n    exclude: Hidden\n",
        "    pairs:\n        a: Position,, Motion\n        b: Zone\n",
        "    pairs:\n        a: Position\n        b: Zone\n    reduce per a:\n        per: a\n        n = count()\n",
        "    pairs:\n        a: Position\n        b: Zone\n    reduce per:\n        n = count()\n",
        "    pairs:\n        a: Position\n        b: Zone\n    reduce per a:\n"};
    for (const auto& clause : clauses) {
        CAPTURE(clause);
        ErrorReporter errors;
        Lexer lexer("module test\nrule Bad:\n" + clause +
                        "    on tick:\n        return\nrule Later:\n    on tick:\n        return\n",
                    "test.cactus",
                    errors);
        Parser parser(lexer.tokenize(), errors);
        const auto program = parser.parse_program();
        CHECK(errors.has_errors());
        REQUIRE(program.declarations.size() >= 3);
        const auto* bad = std::get_if<RuleNode>(&program.declarations[1]);
        REQUIRE(bad != nullptr);
        CHECK(bad->handlers.size() == 1);
        CHECK(std::ranges::any_of(program.declarations, [](const auto& declaration) {
            const auto* rule = std::get_if<RuleNode>(&declaration);
            return rule != nullptr && rule->name == "Later";
        }));
    }
}

struct ParsedWithErrors {
    ProgramNode program;
    std::vector<Diagnostic> diagnostics;
};

static ParsedWithErrors parse_collecting_errors(const std::string& source) {
    ErrorReporter errors;
    Lexer lexer("module test\n" + source, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto program = parser.parse_program();
    return {.program = std::move(program), .diagnostics = errors.diagnostics()};
}

static const RuleNode* find_rule(const ProgramNode& program, const std::string& name) {
    for (const auto& declaration : program.declarations) {
        if (const auto* rule = std::get_if<RuleNode>(&declaration); rule != nullptr && rule->name == name) {
            return rule;
        }
    }
    return nullptr;
}

TEST_CASE("Parser: removed filter and exclude clauses name their replacements", "[parser][concise]") {
    const auto [clause, replacement, positive] = GENERATE(
        std::tuple<std::string, std::string, bool>{"    filter: Position as pos\n", "each:", true},
        std::tuple<std::string, std::string, bool>{"    filter:\n        Position as pos\n", "each:", true},
        std::tuple<std::string, std::string, bool>{"    exclude: Position\n", "without:", false},
        std::tuple<std::string, std::string, bool>{"    exclude:\n        Position\n", "without:", false});
    CAPTURE(clause);
    const auto parsed = parse_collecting_errors("rule Bad:\n" + clause +
                                                "    on tick:\n        return\nrule Later:\n    on tick:\n        return\n");
    REQUIRE(parsed.diagnostics.size() == 1);
    CHECK(parsed.diagnostics[0].message.contains("renamed to `" + replacement + "`"));
    CHECK(parsed.diagnostics[0].location.line == 3);
    CHECK(parsed.diagnostics[0].location.column == 5);
    const auto* bad = find_rule(parsed.program, "Bad");
    REQUIRE(bad != nullptr);
    const auto& selection = positive ? bad->filter : bad->exclude;
    REQUIRE(selection.entries.size() == 1);
    CHECK(selection.entries[0].qualified_name == "Position");
    CHECK(selection.entries[0].alias == (positive ? std::optional<std::string>{"pos"} : std::nullopt));
    CHECK(bad->handlers.size() == 1);
    CHECK(find_rule(parsed.program, "Later") != nullptr);
}

TEST_CASE("Parser: removed spelling after its canonical clause is a duplicate", "[parser][concise]") {
    const auto [clauses, duplicate] = GENERATE(
        std::pair<std::string, std::string>{"    each: Position\n    filter: Motion\n", "duplicate each: clause"},
        std::pair<std::string, std::string>{"    without: Frozen\n    exclude: Hidden\n", "duplicate without: clause"});
    CAPTURE(clauses);
    const auto parsed = parse_collecting_errors("rule Bad:\n" + clauses + "    on tick:\n        return\n");
    REQUIRE(parsed.diagnostics.size() == 2);
    CHECK(parsed.diagnostics[0].message.contains("renamed to"));
    CHECK(parsed.diagnostics[1].message.contains(duplicate));
    CHECK_FALSE(parsed.diagnostics[1].message.contains("filter"));
    CHECK_FALSE(parsed.diagnostics[1].message.contains("exclude"));
}

TEST_CASE("Parser: a repeated canonical clause is a duplicate and is not merged", "[parser][concise]") {
    const auto [clauses, duplicate, positive] = GENERATE(
        std::tuple<std::string, std::string, bool>{"    each: Position\n    each: Motion\n", "duplicate each: clause", true},
        std::tuple<std::string, std::string, bool>{
            "    without: Frozen\n    without: Hidden\n", "duplicate without: clause", false});
    CAPTURE(clauses);
    const auto parsed = parse_collecting_errors("rule Bad:\n" + clauses + "    on tick:\n        return\n");
    REQUIRE(parsed.diagnostics.size() == 1);
    CHECK(parsed.diagnostics[0].message.contains(duplicate));
    const auto* bad = find_rule(parsed.program, "Bad");
    REQUIRE(bad != nullptr);
    const auto& selection = positive ? bad->filter : bad->exclude;
    REQUIRE(selection.entries.size() == 1);
    CHECK(selection.entries[0].qualified_name == (positive ? "Position" : "Frozen"));
}

TEST_CASE("Parser: filter and exclude are ordinary identifiers", "[parser][concise]") {
    const auto program = parse(R"(
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
    REQUIRE(program.declarations.size() == 4);
    const auto& trait = std::get<TraitNode>(program.declarations[0]);
    CHECK(trait.name == "filter");
    REQUIRE(trait.fields.size() == 2);
    CHECK(trait.fields[0].name == "exclude");
    CHECK(trait.fields[1].name == "filter");
    CHECK(std::get<TraitNode>(program.declarations[1]).name == "exclude");
    const auto& func = std::get<FuncNode>(program.declarations[2]);
    REQUIRE(func.params.size() == 2);
    CHECK(func.params[0].name == "filter");
    CHECK(func.params[1].name == "exclude");
    const auto& rule = std::get<RuleNode>(program.declarations[3]);
    REQUIRE(rule.filter.entries.size() == 1);
    CHECK(rule.filter.entries[0].qualified_name == "filter");
    CHECK(rule.filter.entries[0].alias == "exclude");
    REQUIRE(rule.exclude.entries.size() == 1);
    CHECK(rule.exclude.entries[0].qualified_name == "exclude");
    CHECK(rule.handlers.front().body.size() == 2);
}

TEST_CASE("Parser: body-level per names the grouped header", "[parser][rule-reduce][concise]") {
    const auto parsed = parse_collecting_errors(R"(rule Bad:
    pairs:
        team: Team
        player: Score
    reduce:
        per: team
        players = count(player)
        total = sum(player[Score].value)
    on tick:
        return
rule Later:
    on tick:
        return
)");
    REQUIRE(parsed.diagnostics.size() == 1);
    CHECK(parsed.diagnostics[0].message.contains("reduce per team:"));
    CHECK(parsed.diagnostics[0].location.line == 7);
    CHECK(parsed.diagnostics[0].location.column == 9);
    const auto* bad = find_rule(parsed.program, "Bad");
    REQUIRE(bad != nullptr);
    REQUIRE(bad->reduce.has_value());
    CHECK(bad->reduce->per_binding == "team");
    CHECK(bad->reduce->reducers.size() == 2);
    CHECK(find_rule(parsed.program, "Later") != nullptr);
}

TEST_CASE("Parser: body-level per under a grouped header keeps the header", "[parser][rule-reduce][concise]") {
    const auto parsed = parse_collecting_errors(R"(rule Bad:
    pairs:
        team: Team
        player: Score
    reduce per team:
        per: player
        players = count(player)
    on tick:
        return
)");
    REQUIRE(parsed.diagnostics.size() == 1);
    CHECK(parsed.diagnostics[0].message.contains("reduce per player:"));
    CHECK_FALSE(parsed.diagnostics[0].message.contains("duplicate"));
    const auto* bad = find_rule(parsed.program, "Bad");
    REQUIRE(bad != nullptr);
    REQUIRE(bad->reduce.has_value());
    CHECK(bad->reduce->per_binding == "team");
    CHECK(bad->reduce->reducers.size() == 1);
}

TEST_CASE("Parser: a reducer may be named per", "[parser][rule-reduce][concise]") {
    const auto program = parse(R"(rule R:
    pairs:
        team: Team
        player: Score
    reduce:
        per = count()
    on tick:
        return
)");
    const auto& rule = std::get<RuleNode>(program.declarations.front());
    REQUIRE(rule.reduce.has_value());
    CHECK_FALSE(rule.reduce->per_binding.has_value());
    REQUIRE(rule.reduce->reducers.size() == 1);
    CHECK(rule.reduce->reducers[0].name == "per");
}

TEST_CASE("Parser: compact list families preserve nesting and existing ordering positions", "[parser][concise]") {
    const auto program = parse(R"(
phase update:
    after: core.tick, core.late_tick,
group simulation:
    phase: update
    after: input, physics.solve
rule R:
    each: Position as each, Motion as without
    without: Frozen
    where: if each.x > 0: test([1, 2], vec2(1.0, 2.0)) else: false, true
    on tick:
        after: Input/on tick, Other/on tick,
        let without = 1
        let each = without
)");
    REQUIRE(program.declarations.size() == 3);
    CHECK(std::get<PhaseNode>(program.declarations[0]).after_phases.size() == 2);
    CHECK(std::get<GroupNode>(program.declarations[1]).after_groups.size() == 2);
    const auto& rule = std::get<RuleNode>(program.declarations[2]);
    REQUIRE(rule.where_clause.has_value());
    CHECK(rule.where_clause->predicates.size() == 2);
    CHECK(rule.handlers.front().after_handlers.size() == 2);
    CHECK(rule.handlers.front().body.size() == 2);
}

TEST_CASE("Parser: compact notation retains clause placement and context restrictions", "[parser][concise]") {
    const std::vector<std::string> sources{
        "phase update:\n    from: core.tick\n",
        "group simulation:\n    phase: tick\n    before: other\n",
        "rule R:\n    each: A\n    on tick:\n        writes: A\n",
        "rule R:\n    each: A\n    on tick:\n        return\n        after: Other/on tick\n",
        "rule R:\n    pairs:\n        a: A\n        b: B\n    each: A\n",
        "extern rule R:\n    pairs:\n        a: A\n        b: B\n    on tick:\n        effects: physics\n",
        R"(rule R:
    pairs:
        a: A
        b: B
    order by: a[A].x
    reduce per a:
        n = count()
)",
        "rule R:\n    pairs:\n        a: A\n    on tick:\n        return\n",
        "rule R:\n    pairs:\n        a: A\n        b: B\n        c: C\n    on tick:\n        return\n"};
    for (const auto& source : sources) {
        CAPTURE(source);
        CHECK(parse_expect_errors(source).has_errors());
    }
}

TEST_CASE("Parser: invalid compact external clauses preserve later contracts", "[parser][concise]") {
    const std::vector<std::string> clauses{"    pairs:\n        a: A\n        b: B\n",
                                           "    where: true\n",
                                           "    when: true\n",
                                           "    reduce per a:\n        n = count()\n"};
    for (const auto& clause : clauses) {
        CAPTURE(clause);
        ErrorReporter errors;
        Lexer lexer("module test\nextern rule Bad:\n" + clause +
                        "    on tick:\n        effects: physics\nrule Later:\n    on tick:\n        return\n",
                    "test.cactus",
                    errors);
        Parser parser(lexer.tokenize(), errors);
        const auto program = parser.parse_program();
        REQUIRE(errors.has_errors());
        REQUIRE(program.declarations.size() == 3);
        const auto* rule = std::get_if<ExternRuleNode>(&program.declarations[1]);
        REQUIRE(rule != nullptr);
        REQUIRE(rule->handlers.size() == 1);
        CHECK(rule->handlers.front().effects.size() == 1);
    }
}

TEST_CASE("Parser: independent inline errors report their own locations", "[parser][concise]") {
    ErrorReporter errors;
    Lexer lexer(
        "module test\nrule First:\n    each: A,, B\n    on tick:\n        return\n"
        "rule Second:\n    without: C,, D\n    on tick:\n        return\n",
        "test.cactus",
        errors);
    Parser parser(lexer.tokenize(), errors);
    const auto program = parser.parse_program();
    CHECK(program.declarations.size() == 3);
    CHECK(std::ranges::any_of(errors.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.location.line == 3 && diagnostic.location.column == 13;
    }));
    CHECK(std::ranges::any_of(errors.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.location.line == 7 && diagnostic.location.column == 16;
    }));
}
TEST_CASE("Parser: duplicate external list clauses are diagnosed without losing handlers", "[parser][concise]") {
    const bool first_block  = GENERATE(false, true);
    const bool second_block = GENERATE(false, true);
    const std::vector<std::string> clauses{
        "reads: A", "writes: A", "projects: A", "emits: E", "effects: physics", "commands: destroy"};
    for (const auto& clause : clauses) {
        CAPTURE(clause, first_block, second_block);
        auto first  = clause;
        auto second = clause;
        second.replace(second.find(": ") + 2, std::string::npos, "Other");
        if (clause.starts_with("commands:")) {
            second = "commands: add Other";
        }
        if (first_block) {
            first.replace(first.find(": "), 2, ":\n            ");
        }
        if (second_block) {
            second.replace(second.find(": "), 2, ":\n            ");
        }
        ErrorReporter errors;
        std::string source = "module test\nextern rule Native:\n    on tick:\n        ";
        source.append(first).append("\n        ").append(second);
        source.append("\n    on render:\n        effects: graphics\nrule Later:\n    on tick:\n        return\n");
        Lexer lexer(source, "test.cactus", errors);
        Parser parser(lexer.tokenize(), errors);
        const auto program = parser.parse_program();
        REQUIRE(errors.has_errors());
        CHECK(std::ranges::any_of(errors.diagnostics(), [first_block](const auto& diagnostic) {
            return diagnostic.location.line == (first_block ? 6 : 5) && diagnostic.message.starts_with("duplicate ");
        }));
        REQUIRE(program.declarations.size() == 3);
        const auto& rule = std::get<ExternRuleNode>(program.declarations[1]);
        REQUIRE(rule.handlers.size() == 2);
        CHECK(rule.handlers.back().effects.size() == 1);
    }
}
TEST_CASE("Parser: external contract uniqueness is local to each handler", "[parser][concise]") {
    const auto program = parse(R"(
extern rule Native:
    on tick:
        effects: physics
    on render:
        effects: graphics
)");
    const auto& rule   = std::get<ExternRuleNode>(program.declarations.front());
    REQUIRE(rule.handlers.size() == 2);
    CHECK(rule.handlers.front().effects.size() == 1);
    CHECK(rule.handlers.back().effects.size() == 1);
}

// ── Bracketed trait access (dsl-parser) ─────────────────────────────────────

static const TraitAccessExpr& trait_access_under(const ExprNode& expr, std::size_t fields) {
    const ExprNode* cursor = &expr;
    for (std::size_t index = 0; index < fields; ++index) {
        const auto* member = std::get_if<MemberExpr>(&cursor->expr);
        REQUIRE(member != nullptr);
        cursor = member->object.get();
    }
    const auto* access = std::get_if<TraitAccessExpr>(&cursor->expr);
    REQUIRE(access != nullptr);
    return *access;
}

static const ExprNode& first_let_value(const ProgramNode& program) {
    const auto& func = std::get<FuncNode>(program.declarations[0]);
    const auto* let  = std::get_if<LetStmt>(&func.body[0]->stmt);
    REQUIRE(let != nullptr);
    return *let->value;
}

TEST_CASE("Parser: bracketed trait access on an entity name", "[parser][trait-access]") {
    const auto program  = parse("func test():\n"
                                "    let x = Game[Match].over\n");
    const auto& value   = first_let_value(program);
    const auto& field   = std::get<MemberExpr>(value.expr);
    CHECK(field.member == "over");
    const auto& access = trait_access_under(value, 1);
    CHECK(access.trait_name == "Match");
    CHECK(dotted_name_of(*access.object) == "Game");
}

TEST_CASE("Parser: bracketed trait access with qualified base and trait", "[parser][trait-access]") {
    const auto program = parse("func test():\n"
                               "    let x = hud.Hud[text.ScreenLabel].visible\n");
    const auto& access = trait_access_under(first_let_value(program), 1);
    CHECK(access.trait_name == "text.ScreenLabel");
    CHECK(dotted_name_of(*access.object) == "hud.Hud");
}

TEST_CASE("Parser: bracketed trait access keeps a deeper field path", "[parser][trait-access]") {
    const auto program = parse("func test():\n"
                               "    let x = Game[Match].pos.x\n");
    const auto& value  = first_let_value(program);
    CHECK(std::get<MemberExpr>(value.expr).member == "x");
    CHECK(std::get<MemberExpr>(std::get<MemberExpr>(value.expr).object->expr).member == "pos");
    CHECK(trait_access_under(value, 2).trait_name == "Match");
}

TEST_CASE("Parser: a bracket list followed by a call stays a query call", "[parser][trait-access]") {
    const auto program = parse("func test():\n"
                               "    let a = query.all[Enemy]()\n"
                               "    let b = query.count[EnemyAI, not Dead]()\n");
    const auto& func   = std::get<FuncNode>(program.declarations[0]);
    for (const auto& stmt : func.body) {
        const auto& let = std::get<LetStmt>(stmt->stmt);
        CHECK(std::holds_alternative<QueryCallExpr>(let.value->expr));
    }
}

TEST_CASE("Parser: assignment through a bracketed trait path", "[parser][trait-access]") {
    const auto program = parse("rule R:\n"
                               "    on tick:\n"
                               "        Game[Match].score += 1\n"
                               "        hud.Hud[text.ScreenLabel].visible = false\n");
    const auto& body   = std::get<RuleNode>(program.declarations[0]).handlers[0].body;
    const auto& first  = std::get<VarAssign>(body[0]->stmt);
    CHECK(first.op == "+=");
    REQUIRE(first.target != nullptr);
    CHECK(std::get<MemberExpr>(first.target->expr).member == "score");
    const auto& access = trait_access_under(*first.target, 1);
    CHECK(access.trait_name == "Match");
    CHECK(dotted_name_of(*access.object) == "Game");

    const auto& second = std::get<VarAssign>(body[1]->stmt);
    CHECK(second.op == "=");
    REQUIRE(second.target != nullptr);
    CHECK(trait_access_under(*second.target, 1).trait_name == "text.ScreenLabel");
    CHECK(dotted_name_of(*trait_access_under(*second.target, 1).object) == "hud.Hud");
}

TEST_CASE("Parser: bracketed trait access takes exactly one trait name", "[parser][trait-access]") {
    for (const std::string access : {"Game[Match, Score].over", "Game[not Match].over"}) {
        CAPTURE(access);
        const auto errors = parse_expect_errors("func test():\n"
                                                "    let x = " +
                                                access + "\n");
        CHECK(has_diagnostic(errors, "a trait access takes exactly one trait name"));
    }
}

// ── Relations (dsl-relations) ───────────────────────────────────────────────

TEST_CASE("Parser: relation declaration with ends and a field", "[parser][relations]") {
    auto prog = parse(
        "pub relation Walk:\n"
        "    from: Tile\n"
        "    to: tf.Tile\n"
        "    let cost: float = 1.0\n");
    const auto& relation = std::get<RelationNode>(prog.declarations[0]);
    CHECK(relation.name == "Walk");
    CHECK(relation.is_pub);
    CHECK(relation.from.trait_name == "Tile");
    CHECK_FALSE(relation.from.one);
    CHECK(relation.to.trait_name == "tf.Tile");
    CHECK_FALSE(relation.to.one);
    REQUIRE(relation.fields.size() == 1);
    CHECK(relation.fields[0].name == "cost");
    CHECK(relation.fields[0].modifiers.is_let);
}

TEST_CASE("Parser: relation ends marked one", "[parser][relations]") {
    auto prog = parse(
        "relation Claim:\n"
        "    from: Robot one\n"
        "    to: DigTask one\n");
    const auto& relation = std::get<RelationNode>(prog.declarations[0]);
    CHECK_FALSE(relation.is_pub);
    CHECK(relation.from.one);
    CHECK(relation.to.one);
    CHECK(relation.fields.empty());
}

TEST_CASE("Parser: relation is a keyword, one stays an identifier", "[parser][relations]") {
    auto prog = parse(
        "func one() int:\n"
        "    return 1\n");
    CHECK(std::get<FuncNode>(prog.declarations[0]).name == "one");
    auto errors = parse_expect_errors(
        "func relation() int:\n"
        "    return 1\n");
    CHECK(errors.has_errors());
}

TEST_CASE("Parser: relation declaration errors", "[parser][relations]") {
    SECTION("missing to end") {
        auto errors = parse_expect_errors(
            "relation Walk:\n"
            "    from: Tile\n");
        CHECK(has_diagnostic(errors, "relation 'Walk' needs a `to:` end"));
    }
    SECTION("missing from end") {
        auto errors = parse_expect_errors(
            "relation Walk:\n"
            "    to: Tile\n");
        CHECK(has_diagnostic(errors, "relation 'Walk' needs a `from:` end"));
    }
    SECTION("duplicate end") {
        auto errors = parse_expect_errors(
            "relation Walk:\n"
            "    from: Tile\n"
            "    from: Floor\n"
            "    to: Tile\n");
        CHECK(has_diagnostic(errors, "relation 'Walk' declares `from:` twice"));
    }
    SECTION("var field") {
        auto errors = parse_expect_errors(
            "relation Walk:\n"
            "    from: Tile\n"
            "    to: Tile\n"
            "    var cost: float = 1.0\n");
        CHECK(has_diagnostic(errors, "relation fields are `let`; to change a row, replace it with `add Walk(a, b):`"));
    }
    SECTION("persist field") {
        auto errors = parse_expect_errors(
            "relation Walk:\n"
            "    from: Tile\n"
            "    to: Tile\n"
            "    persist let cost: float = 1.0\n");
        CHECK(has_diagnostic(errors, "relation fields can't be `persist`"));
    }
    SECTION("more than one trait on an end") {
        auto errors = parse_expect_errors(
            "relation Walk:\n"
            "    from: Tile, Floor\n"
            "    to: Tile\n");
        CHECK(has_diagnostic(errors, "an end names exactly one trait"));
    }
}

TEST_CASE("Parser: relation recovers after a bad end", "[parser][relations]") {
    auto errors = parse_expect_errors(
        "relation Walk:\n"
        "    from: 3\n"
        "    to: Tile\n"
        "    let cost: float = 1.0\n"
        "trait After\n");
    CHECK(has_diagnostic(errors, "expected a trait name after `from:`"));
    CHECK_FALSE(has_diagnostic(errors, "needs a `to:` end"));
    CHECK(errors.error_count() == 1);
}

TEST_CASE("Parser: derived relation forms", "[parser][relations][derived]") {
    auto prog = parse(
        "relation Reach = closure(Walk)\n"
        "pub relation Route = shortest(nav.Walk, by = cost)\n"
        "relation Room = components(SameRoom)\n");
    REQUIRE(prog.declarations.size() == 3);
    const auto& reach = std::get<RelationNode>(prog.declarations[0]);
    CHECK(reach.name == "Reach");
    CHECK_FALSE(reach.is_pub);
    REQUIRE(reach.derivation.has_value());
    CHECK(reach.derivation->kind == DerivationKind::Closure);
    CHECK(reach.derivation->source_name == "Walk");
    CHECK_FALSE(reach.derivation->cost_field.has_value());
    const auto& route = std::get<RelationNode>(prog.declarations[1]);
    CHECK(route.is_pub);
    REQUIRE(route.derivation.has_value());
    CHECK(route.derivation->kind == DerivationKind::Shortest);
    CHECK(route.derivation->source_name == "nav.Walk");
    CHECK(route.derivation->cost_field == "cost");
    const auto& room = std::get<RelationNode>(prog.declarations[2]);
    REQUIRE(room.derivation.has_value());
    CHECK(room.derivation->kind == DerivationKind::Components);
    CHECK(room.derivation->source_name == "SameRoom");
}

TEST_CASE("Parser: derived relation errors", "[parser][relations][derived]") {
    SECTION("unknown form") {
        auto errors = parse_expect_errors("relation Reach = union(Walk)\n");
        CHECK(has_diagnostic(errors, "expected `closure`, `shortest` or `components` after `relation Reach =`"));
    }
    SECTION("shortest without by") {
        auto errors = parse_expect_errors("relation Route = shortest(Walk)\n");
        CHECK(has_diagnostic(errors, "`shortest` needs a cost field: `shortest(Walk, by = <field>)`"));
    }
    SECTION("by on closure") {
        auto errors = parse_expect_errors("relation Reach = closure(Walk, by = cost)\n");
        CHECK(has_diagnostic(errors, "`closure` takes one relation"));
    }
    SECTION("recovers at the next declaration") {
        auto errors = parse_expect_errors(
            "relation Reach = closure(3)\n"
            "trait After\n"
            "relation Room = components(Walk)\n");
        CHECK(has_diagnostic(errors, "expected a relation name in `closure(...)`"));
        CHECK(errors.error_count() == 1);
    }
}

TEST_CASE("Parser: keep of a relation", "[parser][relations]") {
    auto prog = parse(
        "rule WalkLinks:\n"
        "    pairs:\n"
        "        a: Tile\n"
        "        b: Tile\n"
        "    group: Links\n"
        "    keep Walk(a, b):\n"
        "        cost = 2.0\n");
    const auto& rule = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(rule.keep.has_value());
    CHECK(rule.keep->target_name == "Walk");
    REQUIRE(rule.keep->arguments.size() == 2);
    CHECK(rule.keep->arguments[0].spelling == "a");
    CHECK(rule.keep->arguments[1].spelling == "b");
    CHECK(rule.keep->fields.size() == 1);
    CHECK_FALSE(rule.keep->binding.has_value());
}

TEST_CASE("Parser: add and remove of relation rows", "[parser][relations]") {
    auto prog = parse(
        "rule Claims:\n"
        "    pairs:\n"
        "        robot: Robot\n"
        "        task: Task\n"
        "    on tick:\n"
        "        add Claim(robot, task)\n"
        "        add game.Claim(robot, task):\n"
        "            score = 2\n"
        "        remove Claim(robot, _)\n");
    const auto& body = std::get<RuleNode>(prog.declarations[0]).handlers[0].body;
    REQUIRE(body.size() == 3);
    const auto* plain = std::get_if<AddRelationStmt>(&body[0]->stmt);
    REQUIRE(plain != nullptr);
    CHECK(plain->relation_name == "Claim");
    CHECK(plain->args.empty());
    CHECK(dotted_name_of(*plain->from) == "robot");
    CHECK(dotted_name_of(*plain->to) == "task");
    const auto* fielded = std::get_if<AddRelationStmt>(&body[1]->stmt);
    REQUIRE(fielded != nullptr);
    CHECK(fielded->relation_name == "game.Claim");
    REQUIRE(fielded->args.size() == 1);
    CHECK(fielded->args[0].name == "score");
    const auto* removed = std::get_if<RemoveRelationStmt>(&body[2]->stmt);
    REQUIRE(removed != nullptr);
    CHECK(removed->relation_name == "Claim");
    CHECK(dotted_name_of(*removed->from) == "robot");
    CHECK(is_wildcard_pattern(*removed->to));
}

TEST_CASE("Parser: relation row command needs two arguments", "[parser][relations]") {
    auto errors = parse_expect_errors(
        "rule Claims:\n"
        "    each: Robot\n"
        "    on tick:\n"
        "        remove Claim(self)\n");
    CHECK(has_diagnostic(errors, "a relation row takes two entities: `Claim(from, to)`"));
}

TEST_CASE("Parser: two-sided limit", "[parser][relations][rule-limit]") {
    auto prog = parse(
        "rule Claims:\n"
        "    pairs:\n"
        "        robot: Robot\n"
        "        task: Task\n"
        "    limit: 1 per robot, 2 per task\n"
        "    on tick:\n"
        "        emit Done\n");
    const auto& limit = *std::get<RuleNode>(prog.declarations[0]).limit;
    CHECK(limit.count == nullptr);
    REQUIRE(limit.parts.size() == 2);
    CHECK(limit.parts[0].binding == "robot");
    CHECK(limit.parts[1].binding == "task");
    CHECK(std::get<LiteralExpr>(limit.parts[1].count->expr).value == "2");
}

TEST_CASE("Parser: a second limit part needs per", "[parser][relations][rule-limit]") {
    auto errors = parse_expect_errors(
        "rule Claims:\n"
        "    pairs:\n"
        "        robot: Robot\n"
        "        task: Task\n"
        "    limit: 1 per robot, 2\n"
        "    on tick:\n"
        "        emit Done\n");
    CHECK(has_diagnostic(errors, "each part of a two-sided `limit:` needs `per <binding>`"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
