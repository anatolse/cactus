// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <string>
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
    auto future = std::async(
        std::launch::async, [fixture_name]() { return parse_expect_errors(read_fixture(fixture_name), fixture_name); });

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
        if (diagnostic.message.find("source file must start with a module declaration") != std::string::npos) {
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
        if (diagnostic.message.find("only one module declaration is allowed") != std::string::npos) {
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

TEST_CASE("Parser: trait with persist sync modifiers", "[parser]") {
    auto prog = parse(
        "trait Position:\n"
        "    persist sync var x: float\n"
        "    persist sync var y: float\n");
    auto& decl = std::get<TraitNode>(prog.declarations[0]);
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].modifiers.is_persist);
    CHECK(decl.fields[0].modifiers.is_sync);
    CHECK(decl.fields[0].modifiers.is_var);
    CHECK(decl.fields[0].name == "x");
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
        "    filter:\n"
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
    CHECK(errors.diagnostics().front().message.find("event fields use bare `name: type` syntax") != std::string::npos);
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
        "    filter:  \n"
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
        "    filter:\n"
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
        "    filter:\n"
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
        "    filter:\n"
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

// Task 4.10: on spawn/destroy/load/unload lifecycle handlers
TEST_CASE("Parser: on spawn lifecycle handler", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule Init:\n"
        "    filter:\n"
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
        "    filter:\n"
        "        Position\n"
        "    on destroy:\n"
        "        x = 0.0\n");
    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK(sys.handlers[0].event_name == "destroy");
}

TEST_CASE("Parser: on load and on unload lifecycle handlers", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "rule LevelMgr:\n"
        "    filter: \n"
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
        "    exclude:\n"
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
        "    filter: \n"
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
        "    filter: \n"
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
        "    filter: \n"
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
        "    filter:\n"
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
        "    filter:\n"
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
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        remove Frozen from target_id\n");
    auto& sys    = std::get<RuleNode>(prog.declarations[0]);
    auto* remove = std::get_if<RemoveTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(remove != nullptr);
    CHECK(remove->trait_name == "Frozen");
    REQUIRE(remove->target_expr.has_value());
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
        "    filter:\n"
        "        std.transform.flat.Position as pos\n"
        "        std.render.sprites.Renderer as r\n"
        "    exclude:\n"
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
        "    filter:\n"
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
        "    filter:\n"
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
        "    filter:\n"
        "        Sprite\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    CHECK(sys.order_by.empty());
}

TEST_CASE("Parser: rule order by accepts a computed cross-expression sort key", "[parser][rule-order-by]") {
    auto prog = parse(
        "rule Render:\n"
        "    filter:\n"
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
        "    filter:\n"
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
        "    filter:\n"
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
        "    exclude:\n"
        "        Dead\n"
        "    on fixed_tick:\n"
        "        x = 1\n");
    REQUIRE(errors.has_errors());
}

// order by: is not mutually exclusive with pairs: at the parser layer (or, as
// of a later task, at the semantic layer either) — only filter:/exclude: are.
TEST_CASE("Parser: pairs clause combined with order by parses successfully", "[parser][pair-relations]") {
    auto prog = parse(
        "rule Contacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    order by:\n"
        "        body.DynamicBody.x\n"
        "    on fixed_tick:\n"
        "        x = 1\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.pairs.has_value());
    REQUIRE(sys.order_by.size() == 1);
    auto key0 = sort_key_alias_field(*sys.order_by[0].expression);
    REQUIRE(key0.has_value());
    CHECK(key0->first == "body");
    CHECK(key0->second == "DynamicBody.x");
}

TEST_CASE("Parser: dotted assignment target parses name and path", "[parser][pair-relations]") {
    auto prog = parse(
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    on fixed_tick:\n"
        "        body.Transform.x += 1.0\n");

    auto& sys = std::get<RuleNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 1);
    const auto* assign = std::get_if<VarAssign>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(assign != nullptr);
    CHECK(assign->name == "body");
    REQUIRE(assign->path.size() == 2);
    CHECK(assign->path[0] == "Transform");
    CHECK(assign->path[1] == "x");
    CHECK(assign->op == "+=");
}

TEST_CASE("Parser: star-assign and slash-assign parse into VarAssign with correct op", "[parser][vector-expressions]") {
    auto prog = parse(
        "rule Simple:\n"
        "    filter:\n"
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
        "    filter:\n"
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
        "    filter:\n"
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
        "        body.DynamicBody.active\n");

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
        "    filter:\n"
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
        "    filter:\n"
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
        "    filter:\n"
        "        Position\n"
        "    where:\n"
        "        true\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: extern rule rejects executable handler statements", "[parser][extern-rule]") {
    auto errors = parse_expect_errors(
        "extern rule Bad:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        pass\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: external projects clauses preserve every entry and source location",
          "[parser][extern-rule][handler-contract]") {
    auto prog = parse(
        "extern rule ExternalLayout:\n"
        "    on input:\n"
        "        reads:\n"
        "            Node\n"
        "        projects:\n"
        "            DesiredSize\n"
        "            ui.ComputedLayout\n"
        "        writes:\n"
        "            PointerState\n"
        "        projects:\n"
        "            interaction.PointerHit\n"
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
    CHECK(handler.projects[2].location == SourceLocation{"test.cactus", 12, 13});
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
        "    filter:\n"
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
        if (diagnostic.message.find("handler after: clause must be the first entry") != std::string::npos) {
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
    auto* match_stmt = std::get_if<TraitMatchStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(match_stmt != nullptr);
    REQUIRE(match_stmt->arms.size() == 1);
    CHECK(match_stmt->arms[0].trait_name == "Boss");
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
    auto* match_stmt = std::get_if<TraitMatchStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(match_stmt != nullptr);
    REQUIRE(match_stmt->arms.size() == 2);
    CHECK(match_stmt->arms[0].trait_name == "Boss");
    CHECK(match_stmt->arms[1].trait_name == "EnemyAI");
    REQUIRE(match_stmt->wildcard.has_value());
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
        "    filter:\n"
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
        if (d.message.find("reserved keyword") != std::string::npos && d.message.find("entity") != std::string::npos) {
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
        if (d.message.find("reserved keyword") != std::string::npos && d.message.find("entity") != std::string::npos) {
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
        "    filter:\n"
        "        EditorState\n"
        "pub extern rule EditorPropertyPanel:\n"
        "    filter:\n"
        "        EditorState\n"
        "pub extern rule GizmoRenderer2D:\n"
        "    filter:\n"
        "        EditorGizmo2D\n"
        "pub extern rule GizmoRenderer3D:\n"
        "    filter:\n"
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
        if (d.message.find("reserved keyword") != std::string::npos && d.message.find("entity") != std::string::npos) {
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
        "    filter:\n"
        "        Marker\n"
        "    on tick:\n"
        "        match self:\n"
        "            enemy.Boss as boss =>\n"
        "                add enemy.Activated\n"
        "            _ =>\n"
        "                add enemy.Idle\n");
    auto& sys   = std::get<RuleNode>(prog.declarations[0]);
    auto* match = std::get_if<TraitMatchStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 1);
    CHECK(match->arms[0].trait_name == "enemy.Boss");
    REQUIRE(match->arms[0].alias.has_value());
    CHECK(*match->arms[0].alias == "boss");
    REQUIRE(match->wildcard.has_value());
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
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
