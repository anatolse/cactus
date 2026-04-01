#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "frontend/ast.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"

using namespace cactus;

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
    auto prog = parse("use world as w\n");
    auto& decl = std::get<UseNode>(prog.declarations[0]);
    CHECK(decl.module_name == "world");
    REQUIRE(decl.alias.has_value());
    CHECK(*decl.alias == "w");
}

TEST_CASE("Parser: const block", "[parser]") {
    auto prog = parse("const:\n    X = 42\n    Y = 3.14\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    REQUIRE(decl.assignments.size() == 2);
    CHECK(decl.assignments[0].name == "X");
    CHECK(decl.assignments[1].name == "Y");
}

TEST_CASE("Parser: struct declaration", "[parser]") {
    auto prog = parse("struct Item:\n    name: int\n    price: float\n");
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
    auto prog = parse("enum Color:\n    Red\n    Green\n    Blue\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EnumNode>(prog.declarations[0]);
    CHECK(decl.name == "Color");
    REQUIRE(decl.variants.size() == 3);
    CHECK(decl.variants[0].name == "Red");
    CHECK(decl.variants[1].name == "Green");
    CHECK(decl.variants[2].name == "Blue");
}

TEST_CASE("Parser: enum with explicit values", "[parser]") {
    auto prog = parse("enum Priority:\n    Low = 0\n    High = 10\n");
    auto& decl = std::get<EnumNode>(prog.declarations[0]);
    REQUIRE(decl.variants[0].value.has_value());
    CHECK(*decl.variants[0].value == 0);
    REQUIRE(decl.variants[1].value.has_value());
    CHECK(*decl.variants[1].value == 10);
}

TEST_CASE("Parser: trait with var fields", "[parser]") {
    auto prog = parse("trait Health:\n    var health: int = 100\n    let max_health: int = 100\n");
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
    auto prog = parse("trait Position:\n    persist sync var x: float\n    persist sync var y: float\n");
    auto& decl = std::get<TraitNode>(prog.declarations[0]);
    REQUIRE(decl.fields.size() == 2);
    CHECK(decl.fields[0].modifiers.is_persist);
    CHECK(decl.fields[0].modifiers.is_sync);
    CHECK(decl.fields[0].modifiers.is_var);
    CHECK(decl.fields[0].name == "x");
}

TEST_CASE("Parser: pub unit with apply and config", "[parser]") {
    auto prog = parse(
        "pub unit Player:\n"
        "    apply:\n"
        "        Health\n"
        "        Position\n"
        "    config:\n"
        "        health = 100\n"
        "        x = 0.0\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<UnitNode>(prog.declarations[0]);
    CHECK(decl.name == "Player");
    CHECK(decl.is_pub);
    REQUIRE(decl.apply.trait_names().size() == 2);
    CHECK(decl.apply.trait_names()[0] == "Health");
    CHECK(decl.apply.trait_names()[1] == "Position");
    REQUIRE(decl.config.has_value());
    CHECK(decl.config->assignments.size() == 2);
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

TEST_CASE("Parser: event declaration", "[parser]") {
    auto prog = parse("event Damage:\n    var amount: int\n    var source: int\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EventNode>(prog.declarations[0]);
    CHECK(decl.name == "Damage");
    REQUIRE(decl.fields.size() == 2);
}

TEST_CASE("Parser: func declaration", "[parser]") {
    auto prog = parse("func add(a: int, b: int) int:\n    return a + b\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    CHECK(decl.name == "add");
    REQUIRE(decl.params.size() == 2);
    REQUIRE(decl.return_type.has_value());
    CHECK(decl.return_type->name == "int");
    REQUIRE(decl.body.size() == 1);
}

TEST_CASE("Parser: expression — binary arithmetic", "[parser]") {
    auto prog = parse("const:\n    X = 1 + 2 * 3\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto& expr = decl.assignments[0].value;
    // Should be (1 + (2 * 3)) due to precedence
    auto* bin = std::get_if<BinaryExpr>(&expr->expr);
    REQUIRE(bin != nullptr);
    CHECK(bin->op == "+");
}

TEST_CASE("Parser: expression — function call", "[parser]") {
    auto prog = parse("func test():\n    foo(1, 2)\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* expr_stmt = std::get_if<ExprStmt>(&decl.body[0]->stmt);
    REQUIRE(expr_stmt != nullptr);
    auto* call = std::get_if<CallExpr>(&expr_stmt->expr->expr);
    REQUIRE(call != nullptr);
    CHECK(call->args.size() == 2);
}

TEST_CASE("Parser: expression — member access", "[parser]") {
    auto prog = parse("const:\n    X = a.b\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* mem = std::get_if<MemberExpr>(&decl.assignments[0].value->expr);
    REQUIRE(mem != nullptr);
    CHECK(mem->member == "b");
}

TEST_CASE("Parser: expression — unary not", "[parser]") {
    auto prog = parse("const:\n    X = not true\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* un = std::get_if<UnaryExpr>(&decl.assignments[0].value->expr);
    REQUIRE(un != nullptr);
    CHECK(un->op == "not");
}

TEST_CASE("Parser: expression — list literal", "[parser]") {
    auto prog = parse("const:\n    X = [1, 2, 3]\n");
    auto& decl = std::get<ConstBlockNode>(prog.declarations[0]);
    auto* list = std::get_if<ListExpr>(&decl.assignments[0].value->expr);
    REQUIRE(list != nullptr);
    CHECK(list->elements.size() == 3);
}

TEST_CASE("Parser: if statement", "[parser]") {
    auto prog = parse("func test():\n    if x > 0:\n        return x\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    REQUIRE(decl.body.size() == 1);
    auto* if_stmt = std::get_if<IfStmt>(&decl.body[0]->stmt);
    REQUIRE(if_stmt != nullptr);
    CHECK(if_stmt->then_body.size() == 1);
}

TEST_CASE("Parser: emit statement", "[parser]") {
    auto prog = parse("func test():\n    emit Damage(10, 0)\n");
    auto& decl = std::get<FuncNode>(prog.declarations[0]);
    auto* emit = std::get_if<EmitStmt>(&decl.body[0]->stmt);
    REQUIRE(emit != nullptr);
    CHECK(emit->event_name == "Damage");
    CHECK(emit->args.size() == 2);
}

TEST_CASE("Parser: assignment operators", "[parser]") {
    auto prog = parse("func test():\n    x = 1\n    y += 2\n    z -= 3\n");
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
    auto prog = parse("interface Renderable:\n    func draw(x: int, y: int)\n    func update(dt: float)\n");
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
    auto prog = parse("module lib.physics.rigid\n");
    auto& decl = std::get<ModuleNode>(prog.declarations[0]);
    CHECK(decl.name == "lib.physics.rigid");
}

TEST_CASE("Parser: use with dotted path", "[parser][modules]") {
    auto prog = parse("use enemies.walker\n");
    auto& decl = std::get<UseNode>(prog.declarations[0]);
    CHECK(decl.module_name == "enemies.walker");
    CHECK_FALSE(decl.alias.has_value());
}

TEST_CASE("Parser: use with dotted path and alias", "[parser][modules]") {
    auto prog = parse("use phys.body as b\n");
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
        "    apply:\n"
        "        Position\n"
        "        EnemyAI\n"
        "    config:\n"
        "        patrol_speed = 2.0\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    CHECK(tmpl.name == "EnemyWalker");
    CHECK_FALSE(tmpl.is_pub);
    REQUIRE(tmpl.apply.entries.size() == 2);
    CHECK(tmpl.apply.entries[0].trait_name == "Position");
    CHECK(tmpl.apply.entries[0].initially_active == true);
    CHECK(tmpl.apply.entries[1].trait_name == "EnemyAI");
    REQUIRE(tmpl.config.has_value());
}

// Task 4.1: pub template
TEST_CASE("Parser: pub template declaration", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "pub template Bullet:\n"
        "    apply:\n"
        "        Position\n"
        "    config:\n"
        "        speed = 10.0\n");
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    CHECK(tmpl.name == "Bullet");
    CHECK(tmpl.is_pub);
}

// Task 4.2: apply entry with ': disabled' annotation
TEST_CASE("Parser: apply_entry with disabled annotation", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "template Enemy:\n"
        "    apply:\n"
        "        Position\n"
        "        Frozen: disabled\n"
        "        EnemyAI\n");
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    REQUIRE(tmpl.apply.entries.size() == 3);
    CHECK(tmpl.apply.entries[0].initially_active == true);
    CHECK(tmpl.apply.entries[1].trait_name == "Frozen");
    CHECK(tmpl.apply.entries[1].initially_active == false);
    CHECK(tmpl.apply.entries[2].initially_active == true);
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
    CHECK(sys.filter.entries.empty());   // no filter
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
        "        spawn Enemy(pos = 0.0, patrol_speed = 2.0)\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    auto& handler = sys.handlers[0];
    REQUIRE(handler.body.size() == 1);
    auto* spawn = std::get_if<SpawnStmt>(&handler.body[0]->stmt);
    REQUIRE(spawn != nullptr);
    CHECK(spawn->template_name == "Enemy");
    CHECK(spawn->overrides.size() == 2);
    CHECK(spawn->overrides[0].name == "pos");
    CHECK(spawn->overrides[1].name == "patrol_speed");
}

// Task 4.7: destroy statement
TEST_CASE("Parser: destroy statement", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system DeathSys:\n"
        "    filter: \n"
        "        Health\n"
        "    on tick:\n"
        "        destroy\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    auto* destroy = std::get_if<DestroyStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(destroy != nullptr);
}

// Task 4.8: load statement
TEST_CASE("Parser: load statement", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system GameMgr:\n"
        "    filter: \n"
        "        GameState\n"
        "    on tick:\n"
        "        load levels.level1\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    auto* load = std::get_if<LoadStmt>(&sys.handlers[0].body[0]->stmt);
    REQUIRE(load != nullptr);
    CHECK(load->module_name == "levels.level1");
}

// Task 4.9: enable/disable statements
TEST_CASE("Parser: enable and disable statements", "[parser][dynamic-ecs]") {
    auto prog = parse(
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        enable Frozen\n"
        "        disable EnemyAI\n");
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers[0].body.size() == 2);
    auto* en = std::get_if<EnableStmt>(&sys.handlers[0].body[0]->stmt);
    auto* dis = std::get_if<DisableStmt>(&sys.handlers[0].body[1]->stmt);
    REQUIRE(en != nullptr);
    CHECK(en->trait_name == "Frozen");
    REQUIRE(dis != nullptr);
    CHECK(dis->trait_name == "EnemyAI");
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
    auto prog = parse("pub trait Frozen\n");
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
    auto prog = parse("asset ShotSfx: sound = \"audio/shot.wav\"\n");
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.asset_kind == AssetKind::Sound);
}

TEST_CASE("Parser: asset declaration music type", "[parser][dsl-spec-new-features]") {
    auto prog = parse("asset Theme: music = \"audio/theme.ogg\"\n");
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.asset_kind == AssetKind::Music);
}

TEST_CASE("Parser: asset declaration font type", "[parser][dsl-spec-new-features]") {
    auto prog = parse("asset HudFont: font = \"fonts/hud.ttf\"\n");
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.asset_kind == AssetKind::Font);
}

TEST_CASE("Parser: asset declaration material type", "[parser][dsl-spec-new-features]") {
    auto prog = parse("asset StoneMat: material = \"materials/stone.mat\"\n");
    auto& node = std::get<AssetDeclNode>(prog.declarations[0]);
    CHECK(node.asset_kind == AssetKind::Material);
}

// Task 4.9: input_decl
TEST_CASE("Parser: input declaration button with properties", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "input Jump: button\n"
        "    key     = Key.Space\n"
        "    gamepad = GamepadButton.South\n"
    );
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
        "    gamepad  = GamepadAxis.LeftX\n"
    );
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
        "    invert   = true\n"
    );
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
        "        x = 1\n"
    );
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "input");
}

TEST_CASE("Parser: on fixed_tick handler with dt parameter", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "system PhysSys:\n"
        "    on fixed_tick:\n"
        "        x = 1\n"
    );
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "fixed_tick");
}

TEST_CASE("Parser: on late_tick handler with dt parameter", "[parser][dsl-spec-new-features]") {
    auto prog = parse(
        "system CamSys:\n"
        "    on late_tick:\n"
        "        x = 1\n"
    );
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
        "        z = 3\n"
    );
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
        "pub extern func sqrt(v: float) float\n"
    );
    REQUIRE(prog.declarations.size() == 3);
    CHECK(std::get<FuncNode>(prog.declarations[0]).name == "sin");
    CHECK(std::get<FuncNode>(prog.declarations[1]).name == "cos");
    CHECK(std::get<FuncNode>(prog.declarations[2]).name == "sqrt");
    for (auto& d : prog.declarations) {
        CHECK(std::get<FuncNode>(d).is_extern);
        CHECK(std::get<FuncNode>(d).body.empty());
    }
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
    auto prog = parse("func distance(a: float, b: float) float:\n    return a - b\n");
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
    Lexer lexer("func add(a: int, b: int) -> int:\n    return a + b\n", "test.cactus", errors);
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
    Lexer lexer("trait Bad:\n    var x: int\n    on tick(dt: float):\n        x = 1\n", "test.cactus", errors);
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
        "        y = 2\n"
    );
    REQUIRE(prog.declarations.size() == 2);
    auto& ui = std::get<SystemNode>(prog.declarations[1]);
    CHECK(ui.name == "UIRender");
    REQUIRE(ui.after_systems.size() == 1);
    CHECK(ui.after_systems[0] == "SceneRender");
    // SceneRender has empty after_systems
    auto& scene = std::get<SystemNode>(prog.declarations[0]);
    CHECK(scene.after_systems.empty());
}

// Task 12.3: apply: with 'as alias' records alias in ApplyEntry
TEST_CASE("Parser: apply entry with as alias records alias", "[parser][config-qualification]") {
    auto prog = parse(
        "trait Position:\n"
        "    var x: float\n"
        "unit Player:\n"
        "    apply:\n"
        "        Position as pos\n"
    );
    auto& unit = std::get<UnitNode>(prog.declarations[1]);
    REQUIRE(unit.apply.entries.size() == 1);
    CHECK(unit.apply.entries[0].trait_name == "Position");
    REQUIRE(unit.apply.entries[0].alias.has_value());
    CHECK(*unit.apply.entries[0].alias == "pos");
}

// Task 12.4: config: dotted key parsed as (prefix, field)
TEST_CASE("Parser: config dotted key parsed correctly", "[parser][config-qualification]") {
    auto prog = parse(
        "trait Health:\n"
        "    var health: int = 100\n"
        "unit Player:\n"
        "    apply:\n"
        "        Health\n"
        "    config:\n"
        "        Health.health = 50\n"
    );
    auto& unit = std::get<UnitNode>(prog.declarations[1]);
    REQUIRE(unit.config.has_value());
    REQUIRE(unit.config->assignments.size() == 1);
    CHECK(unit.config->assignments[0].name == "health");
    CHECK(unit.config->assignments[0].key_prefix == "Health");
}

TEST_CASE("Parser: apply alias with disabled annotation", "[parser][config-qualification]") {
    auto prog = parse(
        "template Enemy:\n"
        "    apply:\n"
        "        Position as pos\n"
        "        Frozen as ice: disabled\n"
    );
    auto& tmpl = std::get<TemplateNode>(prog.declarations[0]);
    REQUIRE(tmpl.apply.entries.size() == 2);
    CHECK(*tmpl.apply.entries[0].alias == "pos");
    CHECK(*tmpl.apply.entries[1].alias == "ice");
    CHECK(tmpl.apply.entries[1].initially_active == false);
}

TEST_CASE("Parser: system with multiple after: entries", "[parser][system-ordering]") {
    auto prog = parse(
        "system Debug:\n"
        "    after:\n"
        "        SceneRender\n"
        "        UIRender\n"
        "    on tick:\n"
        "        x = 1\n"
    );
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
        "        x = 1\n"
    );
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
        "        x = 1\n"
    );
    auto& sys = std::get<SystemNode>(prog.declarations[0]);
    REQUIRE(sys.handlers.size() == 1);
    CHECK(sys.handlers[0].event_name == "PlayerDamaged");
    REQUIRE(sys.handlers[0].alias.has_value());
    CHECK(*sys.handlers[0].alias == "dmg");
}

TEST_CASE("Parser: marker event declaration (no colon, no body)", "[parser][dsl-event-handler-syntax]") {
    auto prog = parse("pub event spawn\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EventNode>(prog.declarations[0]);
    CHECK(decl.name == "spawn");
    CHECK(decl.is_pub);
    CHECK(decl.fields.empty());
}

TEST_CASE("Parser: marker event without pub", "[parser][dsl-event-handler-syntax]") {
    auto prog = parse("event destroy\n");
    REQUIRE(prog.declarations.size() == 1);
    auto& decl = std::get<EventNode>(prog.declarations[0]);
    CHECK(decl.name == "destroy");
    CHECK_FALSE(decl.is_pub);
    CHECK(decl.fields.empty());
}
