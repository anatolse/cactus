// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>

using namespace cactus;
namespace fs = std::filesystem;

static ProgramNode parse(const std::string& source) {
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
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

static ErrorReporter parse_expect_errors(const std::string& source, const std::string& filename = "test.cactus") {
    ErrorReporter errors;
    Lexer lexer(source, filename, errors);
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

TEST_CASE("Parser: module declaration", "[parser]") {
    auto prog = parse("module player\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ModuleNode>(prog.declarations[0]);
    CHECK(decl.name == "player");
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

TEST_CASE("Parser: system with filter and handler", "[parser]") {
    auto prog = parse(
        "system MoveSystem:\n"
        "    filter:\n"
        "       Position\n"
        "       Velocity\n"
        "    on tick:\n"
        "        x = x + vx * tick.dt\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<SystemNode>(prog.declarations[0]);
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
        "system ContactSystem:\n"
        "    on tick:\n"
        "        for hit in hits:\n"
        "            emit Damage to hit.entity:\n"
        "                amount = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "system ProjectionSystem:\n"
        "    on tick:\n"
        "        project Grounded\n"
        "        project GroundContact:\n"
        "            normal = n\n"
        "        project InExplosion to hit.entity:\n"
        "            damage = 10\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "system BadSystem:\n"
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

TEST_CASE("Parser: interface declaration", "[parser]") {
    auto prog = parse(
        "interface Renderable:\n"
        "    func draw(x: int, y: int)\n"
        "    func update(dt: float)\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<InterfaceNode>(prog.declarations[0]);
    CHECK(decl.name == "Renderable");
    REQUIRE(decl.methods.size() == 2);
    CHECK(decl.methods[0].name == "draw");
    CHECK(decl.methods[1].name == "update");
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

// ── Module System Tests ─────────────────────────────────────────────────────

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
        "system Render:\n"
        "    filter:  \n"
        "        phys.Body\n"
        "        render.Sprite\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "system Render:\n"
        "    filter:\n"
        "        phys.Body as b\n"
        "        render.Sprite as s\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "system Mixed:\n"
        "    filter:\n"
        "        Position\n"
        "        phys.Body as b\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "system Simple:\n"
        "    filter:\n"
        "        Position as pos\n"
        "        Velocity as vel\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "system Init:\n"
        "    filter:\n"
        "        Position\n"
        "    on spawn:\n"
        "        x = 0.0\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "spawn");
}

TEST_CASE("Parser: on destroy lifecycle handler", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system Cleanup:\n"
        "    filter:\n"
        "        Position\n"
        "    on destroy:\n"
        "        x = 0.0\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    CHECK(sys.handlers[0].event_name == "destroy");
}

TEST_CASE("Parser: on load and on unload lifecycle handlers", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system LevelMgr:\n"
        "    filter: \n"
        "        GameState\n"
        "    on load:\n"
        "        x = 1\n"
        "    on unload:\n"
        "        x = 0\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 2);
    CHECK(sys.handlers[0].event_name == "load");
    CHECK(sys.handlers[1].event_name == "unload");
}

// Task 4.4: system with exclude block
TEST_CASE("Parser: system with exclude clause", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system SceneCleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        x = 0\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    CHECK(sys.filter.entries.empty());  // no filter
    REQUIRE(sys.exclude.trait_names.size() == 1);
    CHECK(sys.exclude.trait_names[0] == "Persistent");
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "unload");
}

// Task 4.5-4.6: spawn statement
TEST_CASE("Parser: spawn statement", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system LevelSetup:\n"
        "    filter: \n"
        "        GameState\n"
        "    on load:\n"
        "        spawn Enemy:\n"
        "            Position:\n"
        "                pos = 0.0\n"
        "            EnemyAI:\n"
        "                patrol_speed = 2.0\n");
    auto& sys     = std::get<SystemNode>(prog.declarations[0]);
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
        "system DeathSys:\n"
        "    filter: \n"
        "        Health\n"
        "    on tick:\n"
        "        destroy\n");
    auto& sys     = std::get<SystemNode>(prog.declarations[0]);
    auto* destroy = std::get_if<DestroyStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(destroy != nullptr);
}

TEST_CASE("Parser: destroy targeted entity statement", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "event Collision:\n"
        "    other: entity_id\n"
        "system Cleanup:\n"
        "    on Collision as c:\n"
        "        destroy c.other\n");
    auto& sys     = std::get<SystemNode>(prog.declarations[1]);
    auto* destroy = std::get_if<DestroyStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(destroy != nullptr);
    REQUIRE(destroy->target_expr.has_value());
}

TEST_CASE("Parser: self parsed as destroy target", "[parser][hierarchy]") {
    auto prog = parse(
        "system Cleanup:\n"
        "    on tick:\n"
        "        destroy self\n");
    auto& sys     = std::get<SystemNode>(prog.declarations[0]);
    auto* destroy = std::get_if<DestroyStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(destroy != nullptr);
    REQUIRE(destroy->target_expr.has_value());
    CHECK(std::holds_alternative<SelfExpr>((*destroy->target_expr)->expr));
}

TEST_CASE("Parser: self parsed in assignment expression", "[parser][hierarchy]") {
    auto prog = parse(
        "system Parenting:\n"
        "    on tick:\n"
        "        add Parent to self\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "system GameMgr:\n"
        "    filter: \n"
        "        GameState\n"
        "    on tick:\n"
        "        load levels.level1\n");
    auto& sys  = std::get<SystemNode>(prog.declarations[0]);
    auto* load = std::get_if<LoadStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(load != nullptr);
    CHECK(load->module_name == "levels.level1");
}

TEST_CASE("Parser: add and remove statements", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen\n"
        "        remove EnemyAI\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen to target_id:\n"
        "            duration = 2.0\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    auto* add = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(add != nullptr);
    CHECK(add->trait_name == "Frozen");
    REQUIRE(add->args.size() == 1);
    CHECK(add->args[0].name == "duration");
    REQUIRE(add->target_expr.has_value());
}

TEST_CASE("Parser: remove statement with target", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        remove Frozen from target_id\n");
    auto& sys    = std::get<SystemNode>(prog.declarations[0]);
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

// Task 4.8: asset_decl - all six asset types
TEST_CASE("Parser: asset declaration mesh type", "[parser][dsl-spec-new-features]") {
    auto prog = parse("asset PlayerMesh: mesh = \"models/player.glb\"\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.name == "PlayerMesh");
    CHECK(node.is_pub == false);
    CHECK(node.asset_kind == AssetKind::Mesh);
    CHECK(node.path == "models/player.glb");
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
        "system InputSys:\n"
        "    on input:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "input");
}

TEST_CASE("Parser: on fixed_tick handler with dt parameter", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "system PhysSys:\n"
        "    on fixed_tick:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "fixed_tick");
}

TEST_CASE("Parser: on late_tick handler with dt parameter", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "system CamSys:\n"
        "    on late_tick:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "late_tick");
}

TEST_CASE("Parser: system with multiple lifecycle handlers", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "system MultiPhase:\n"
        "    on input:\n"
        "        x = 1\n"
        "    on fixed_tick:\n"
        "        y = 2\n"
        "    on late_tick:\n"
        "        z = 3\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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
        "    entity: entity_id\n"
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

// ── system-ordering-and-trait-cleanup tests ─────────────────────────────────

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

// Task 12.2: after: block parses correctly into SystemNode.after_systems
TEST_CASE("Parser: system after: block parsed correctly", "[parser][system-ordering]") {
    auto prog = parse(
        "system SceneRender:\n"
        "    on tick:\n"
        "        x = 1\n"
        "\n"
        "system UIRender:\n"
        "    after:\n"
        "        SceneRender\n"
        "    on tick:\n"
        "        y = 2\n");
    REQUIRE(prog.declarations.size() == 2);
    auto& ui = std::get<SystemNode>(prog.declarations[1]);
    CHECK(ui.name == "UIRender");
    REQUIRE(ui.after_systems.size() == 1);
    CHECK(ui.after_systems[0] == "SceneRender");
    // SceneRender has empty after_systems
    auto& scene = std::get<SystemNode>(prog.declarations[0]);
    CHECK(scene.after_systems.empty());
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
        "system FreezeSystem:\n"
        "    on tick:\n"
        "        add Frozen\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    REQUIRE(sys.handlers[0].body.size() == 1);
    auto* add = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(add != nullptr);
    CHECK(add->trait_name == "Frozen");
    CHECK(add->args.empty());
    CHECK_FALSE(add->target_expr.has_value());
}

TEST_CASE("Parser: extern system declaration with filter and order by", "[parser][extern-system]") {
    auto prog = parse(
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        std.transform.flat.Position as pos\n"
        "        std.render.sprites.Renderer as r\n"
        "    exclude:\n"
        "        Hidden\n"
        "    order by:\n"
        "        r.layer asc\n"
        "        pos.pos.y desc\n"
        "    after:\n"
        "        TransformUpdate\n"
        "    target: cpu\n");

    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ExternSystemNode>(prog.declarations[0]);
    CHECK(decl.name == "SpriteRenderer");
    REQUIRE(decl.filter.entries.size() == 2);
    CHECK(decl.filter.entries[0].qualified_name == "std.transform.flat.Position");
    REQUIRE(decl.filter.entries[0].alias.has_value());
    CHECK(*decl.filter.entries[0].alias == "pos");
    REQUIRE(decl.exclude.entries.size() == 1);
    CHECK(decl.exclude.entries[0].qualified_name == "Hidden");
    REQUIRE(decl.order_by.size() == 2);
    CHECK(decl.order_by[0].alias == "r");
    CHECK(decl.order_by[0].field == "layer");
    CHECK_FALSE(decl.order_by[0].descending);
    CHECK(decl.order_by[1].alias == "pos");
    CHECK(decl.order_by[1].field == "pos.y");
    CHECK(decl.order_by[1].descending);
    REQUIRE(decl.after_systems.size() == 1);
    CHECK(decl.after_systems[0] == "TransformUpdate");
    REQUIRE(decl.target.has_value());
    CHECK(*decl.target == "cpu");
}

TEST_CASE("Parser: system order by single and default asc", "[parser][system-order-by]") {
    auto prog = parse(
        "system Render:\n"
        "    filter:\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.order_by.size() == 1);
    CHECK(sys.order_by[0].alias == "s");
    CHECK(sys.order_by[0].field == "layer");
    CHECK_FALSE(sys.order_by[0].descending);
}

TEST_CASE("Parser: system order by multi key", "[parser][system-order-by]") {
    auto prog = parse(
        "system Render:\n"
        "    filter:\n"
        "        Position as p\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer asc\n"
        "        p.pos.y desc\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.order_by.size() == 2);
    CHECK(sys.order_by[0].alias == "s");
    CHECK(sys.order_by[0].field == "layer");
    CHECK_FALSE(sys.order_by[0].descending);
    CHECK(sys.order_by[1].alias == "p");
    CHECK(sys.order_by[1].field == "pos.y");
    CHECK(sys.order_by[1].descending);
}

TEST_CASE("Parser: system without order by leaves clause empty", "[parser][system-order-by]") {
    auto prog = parse(
        "system Render:\n"
        "    filter:\n"
        "        Sprite\n"
        "    on tick:\n"
        "        x = 1\n");

    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    CHECK(sys.order_by.empty());
}

TEST_CASE("Parser: extern system with handler produces error", "[parser][extern-system]") {
    auto errors = parse_expect_errors(
        "extern system Bad:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        pass\n");
    REQUIRE(errors.has_errors());
}

TEST_CASE("Parser: trait match statement single arm with alias", "[parser][trait-match]") {
    auto prog = parse(
        "event Collision:\n"
        "    other: entity_id\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.phase\n");
    auto& sys        = std::get<SystemNode>(prog.declarations[1]);
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
        "system Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                pass\n"
        "            EnemyAI =>\n"
        "                pass\n"
        "            _ =>\n"
        "                pass\n");
    auto& sys        = std::get<SystemNode>(prog.declarations[1]);
    auto* match_stmt = std::get_if<TraitMatchStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(match_stmt != nullptr);
    REQUIRE(match_stmt->arms.size() == 2);
    CHECK(match_stmt->arms[0].trait_name == "Boss");
    CHECK(match_stmt->arms[1].trait_name == "EnemyAI");
    REQUIRE(match_stmt->wildcard.has_value());
}

TEST_CASE("Parser: add statement with block parsed", "[parser][dynamic-traits]") {
    auto prog = parse(
        "system FreezeSystem:\n"
        "    on tick:\n"
        "        add Health:\n"
        "            current = 100\n"
        "            max = 100\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    auto* add = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(add != nullptr);
    CHECK(add->trait_name == "Health");
    REQUIRE(add->args.size() == 2);
    CHECK(add->args[0].name == "current");
    CHECK(add->args[1].name == "max");
}

TEST_CASE("Parser: add and remove statements with cross-entity targets parsed", "[parser][dynamic-traits]") {
    auto prog = parse(
        "system FreezeSystem:\n"
        "    on tick:\n"
        "        add Frozen to target_id:\n"
        "            duration = 2.0\n"
        "        remove Frozen from target_id\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 2);
    auto* add    = std::get_if<AddTraitStmt>(&sys.handlers[0].body[0]->stmt);
    auto* remove = std::get_if<RemoveTraitStmt>(&sys.handlers[0].body[1]->stmt);
    REQUIRE(add != nullptr);
    REQUIRE(remove != nullptr);
    REQUIRE(add->target_expr.has_value());
    REQUIRE(remove->target_expr.has_value());
}

TEST_CASE("Parser: system with multiple after: entries", "[parser][system-ordering]") {
    auto prog = parse(
        "system Debug:\n"
        "    after:\n"
        "        SceneRender\n"
        "        UIRender\n"
        "    on tick:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.after_systems.size() == 2);
    CHECK(sys.after_systems[0] == "SceneRender");
    CHECK(sys.after_systems[1] == "UIRender");
}

// ── dsl-event-handler-syntax parser tests ───────────────────────────────────

TEST_CASE("Parser: on tick with alias parsed", "[parser][dsl-event-handler-syntax]") {
    auto prog = parse(
        "system Move:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick as t:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "tick");
    REQUIRE(sys.handlers[0].alias.has_value());
    CHECK(*sys.handlers[0].alias == "t");
}

TEST_CASE("Parser: on user event with alias parsed", "[parser][dsl-event-handler-syntax]") {
    auto prog = parse(
        "system Combat:\n"
        "    on PlayerDamaged as dmg:\n"
        "        x = 1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
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

TEST_CASE("Parser: malformed system fixture completes without hanging", "[parser][recovery]") {
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
    CHECK(errors.diagnostics().front().location.line == 2);
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

TEST_CASE("Parser: legacy unit declaration rejected", "[parser][entity]") {
    auto errors = parse_expect_errors(
        "unit Player:\n"
        "    Position\n");
    REQUIRE_FALSE(errors.diagnostics().empty());
    CHECK(errors.diagnostics().front().message.find("renamed") != std::string::npos);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
