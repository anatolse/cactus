// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "common/types.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace cactus;

// ── Helpers ──────────────────────────────────────────────────────────────────

static constexpr const char* TEST_MODULE_NAME = "test.module";

static ProgramNode make_program_with_module(const std::string& module_name = TEST_MODULE_NAME) {
    ProgramNode prog;
    prog.declarations.emplace_back(ModuleNode{.name = module_name, .location = {}});
    return prog;
}

static ExprNode make_int_literal_expr(const std::string& value = "1") {
    return ExprNode{ExprNode::Variant{LiteralExpr{.kind = LiteralExpr::Kind::Int, .value = value, .location = {}}}, {}};
}

static ConstBlockNode make_const_block_named(const std::string& name) {
    ConstBlockNode block;
    block.assignments.push_back(
        ConstAssignment{.name = name, .value = std::make_unique<ExprNode>(make_int_literal_expr()), .location = {}});
    return block;
}

static std::string analyze_first_module_error(ProgramNode& prog) {
    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog);
    REQUIRE(errors.has_errors());
    return errors.diagnostics().front().message;
}

template <typename Decl>
static std::string duplicate_against_trait_error(Decl second_decl) {
    ProgramNode prog = make_program_with_module("game.namespace");
    TraitNode first;
    first.name = "Shared";
    prog.declarations.emplace_back(std::move(first));
    prog.declarations.emplace_back(std::move(second_decl));
    return analyze_first_module_error(prog);
}

/// Build a minimal ProgramNode containing a single trait with a field
/// whose type is the given TypeRef name.
static ProgramNode make_program_with_trait_field(const std::string& trait_name,
                                                 const std::string& field_name,
                                                 const std::string& field_type_name) {
    ProgramNode prog = make_program_with_module();
    TraitNode trait;
    trait.name   = trait_name;
    trait.is_pub = false;
    FieldNode field;
    field.name             = field_name;
    field.type.name        = field_type_name;
    field.modifiers.is_var = true;
    trait.fields.push_back(std::move(field));
    prog.declarations.emplace_back(std::move(trait));
    return prog;
}

// ── One module-scope namespace ────────────────────────────────────────────────

TEST_CASE("semantic_modules: one namespace rejects event/struct duplicate", "[semantic][modules][namespace]") {
    ProgramNode prog = make_program_with_module("game.combat");

    EventNode event;
    event.name = "Hit";
    prog.declarations.emplace_back(std::move(event));

    StructNode strct;
    strct.name = "Hit";
    prog.declarations.emplace_back(std::move(strct));

    const auto msg = analyze_first_module_error(prog);
    CHECK(msg.find("duplicate module-scope declaration 'Hit'") != std::string::npos);
    CHECK(msg.find("struct conflicts with existing event") != std::string::npos);
}

TEST_CASE("semantic_modules: one namespace includes all top-level declaration names",
          "[semantic][modules][namespace]") {
    auto expect_duplicate = [](const std::string& msg, const std::string& kind) {
        CHECK(msg.find("duplicate module-scope declaration 'Shared'") != std::string::npos);
        CHECK(msg.find(kind + " conflicts with existing trait") != std::string::npos);
    };

    SECTION("trait") {
        TraitNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "trait");
    }
    SECTION("struct") {
        StructNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "struct");
    }
    SECTION("enum") {
        EnumNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "enum");
    }
    SECTION("event") {
        EventNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "event");
    }
    SECTION("func") {
        FuncNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "func");
    }
    SECTION("system") {
        SystemNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "system");
    }
    SECTION("template") {
        TemplateNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "template");
    }
    SECTION("entity") {
        EntityNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "entity");
    }
    SECTION("asset") {
        AssetDeclNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "asset");
    }
    SECTION("input") {
        InputDeclNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "input");
    }
    SECTION("const") {
        expect_duplicate(duplicate_against_trait_error(make_const_block_named("Shared")), "const");
    }
}

/// Build a ProgramNode with a system that has filter entries.
static ProgramNode make_program_with_system(const std::string& sys_name, const std::vector<FilterEntry>& entries) {
    ProgramNode prog = make_program_with_module();
    SystemNode sys;
    sys.name           = sys_name;
    sys.filter.entries = entries;
    for (const auto& e : entries) {
        // Also populate backward-compat trait_names with the last component
        auto dot = e.qualified_name.find('.');
        sys.filter.trait_names.push_back(dot != std::string::npos ? e.qualified_name.substr(dot + 1)
                                                                  : e.qualified_name);
    }
    prog.declarations.emplace_back(std::move(sys));
    return prog;
}

/// Build an ImportedSymbols with a single pub struct.
static ImportedSymbols make_module_with_struct(const std::string& module_name, const std::string& struct_name) {
    ImportedSymbols syms;
    syms.module_name = module_name;
    ResolvedStruct rs;
    rs.name                   = struct_name;
    syms.structs[struct_name] = rs;
    return syms;
}

/// Build an ImportedSymbols with a single pub trait.
static ImportedSymbols make_module_with_trait(const std::string& module_name, const std::string& trait_name) {
    ImportedSymbols syms;
    syms.module_name = module_name;
    ResolvedTrait rt;
    rt.name                 = trait_name;
    rt.is_pub               = true;
    rt.module_name          = module_name;
    rt.canonical_id         = make_canonical_id(module_name, trait_name);
    syms.traits[trait_name] = rt;
    return syms;
}

/// Build an ImportedSymbols with a single pub template.
static ImportedSymbols make_module_with_template(const std::string& module_name, const std::string& template_name) {
    ImportedSymbols syms;
    syms.module_name = module_name;
    ImportedTemplate tmpl;
    tmpl.name                     = template_name;
    tmpl.canonical_id             = make_canonical_id(module_name, template_name);
    syms.templates[template_name] = tmpl;
    return syms;
}

/// Build a ProgramNode with one template that uses another template by name.
static ProgramNode make_program_with_template_use(const std::string& template_use_name) {
    ProgramNode prog = make_program_with_module();
    TemplateNode tmpl;
    tmpl.name = "Composed";
    tmpl.template_uses.push_back({.template_name = template_use_name, .location = {}});
    prog.declarations.emplace_back(std::move(tmpl));
    return prog;
}

// ── Template composition import resolution ───────────────────────────────────

TEST_CASE("semantic_modules: archetype template use resolves qualified imported template",
          "[semantic][modules][template-composition]") {
    auto prog = make_program_with_template_use("enemies.EnemyBase");

    ModuleImports imports;
    imports.add("enemies", make_module_with_template("enemies", "EnemyBase"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: archetype template use resolves aliased imported template",
          "[semantic][modules][template-composition]") {
    auto prog = make_program_with_template_use("enemy.EnemyBase");

    ModuleImports imports;
    imports.add("enemy", make_module_with_template("enemies", "EnemyBase"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: archetype template use resolves unique unqualified imported template",
          "[semantic][modules][template-composition]") {
    auto prog = make_program_with_template_use("EnemyBase");

    ModuleImports imports;
    imports.add("enemies", make_module_with_template("enemies", "EnemyBase"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("template 'EnemyBase' is imported from module 'enemies'") != std::string::npos);
    CHECK(msg.find("must be referenced as 'enemies.EnemyBase'") != std::string::npos);
}

TEST_CASE("semantic_modules: archetype template use rejects imported non-template symbol",
          "[semantic][modules][template-composition]") {
    auto prog = make_program_with_template_use("player.Health");

    ModuleImports imports;
    imports.add("player", make_module_with_trait("player", "Health"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("must reference a template") != std::string::npos);
    CHECK(msg.find("not a template") != std::string::npos);
}

TEST_CASE("semantic_modules: archetype template use rejects imported private template",
          "[semantic][modules][template-composition]") {
    auto prog = make_program_with_template_use("enemies.SecretBase");

    ImportedSymbols syms;
    syms.module_name = "enemies";
    ModuleImports imports;
    imports.add("enemies", std::move(syms), {}, {"SecretBase"});

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("not public") != std::string::npos);
    CHECK(msg.find("SecretBase") != std::string::npos);
}

TEST_CASE("semantic_modules: ambiguous unqualified archetype template use reports error",
          "[semantic][modules][template-composition]") {
    auto prog = make_program_with_template_use("EnemyBase");

    ModuleImports imports;
    imports.add("enemies.ground", make_module_with_template("enemies.ground", "EnemyBase"));
    imports.add("enemies.flying", make_module_with_template("enemies.flying", "EnemyBase"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("ambiguous reference 'EnemyBase'") != std::string::npos);
    CHECK(msg.find("use qualified access") != std::string::npos);
}

// ── Task 4.2: Qualified symbol resolution ────────────────────────────────────

TEST_CASE("semantic_modules: local struct and enum type refs carry symbol identity", "[semantic][modules][3.1]") {
    ProgramNode prog = make_program_with_module("game.inventory");

    StructNode item;
    item.name = "Item";
    prog.declarations.emplace_back(std::move(item));

    EnumNode rarity;
    rarity.name = "Rarity";
    rarity.variants.push_back({.name = "Common", .location = {}});
    rarity.variants.push_back({.name = "Rare", .location = {}});
    prog.declarations.emplace_back(std::move(rarity));

    TraitNode inventory;
    inventory.name = "Inventory";
    FieldNode item_field;
    item_field.name             = "held";
    item_field.type.name        = "Item";
    item_field.modifiers.is_var = true;
    inventory.fields.push_back(std::move(item_field));
    FieldNode rarity_field;
    rarity_field.name             = "rarity";
    rarity_field.type.name        = "Rarity";
    rarity_field.modifiers.is_var = true;
    inventory.fields.push_back(std::move(rarity_field));
    prog.declarations.emplace_back(std::move(inventory));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog);

    CHECK_FALSE(errors.has_errors());
    const auto& fields = result.traits.at("Inventory").fields;
    REQUIRE(fields.size() == 2);
    CHECK(fields[0].type.kind == TypeKind::Struct);
    REQUIRE(fields[0].type.symbol_id.has_value());
    CHECK(*fields[0].type.symbol_id == make_symbol_id(SymbolKind::Struct, "game.inventory", "Item"));
    CHECK(fields[1].type.kind == TypeKind::Enum);
    REQUIRE(fields[1].type.symbol_id.has_value());
    CHECK(*fields[1].type.symbol_id == make_symbol_id(SymbolKind::Enum, "game.inventory", "Rarity"));
}

TEST_CASE("semantic_modules: qualified struct type resolved", "[semantic][modules][4.2]") {
    // Trait field uses "player.Vec2Pos" (qualified reference to an imported struct)
    auto prog = make_program_with_trait_field("EnemyAI", "pos", "player.Vec2Pos");

    ImportedSymbols player_syms;
    player_syms.module_name = "player";
    ResolvedStruct rs;
    rs.name                        = "Vec2Pos";
    player_syms.structs["Vec2Pos"] = rs;

    ModuleImports imports;
    imports.add("player", std::move(player_syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
    REQUIRE(result.traits.count("EnemyAI") == 1);
    auto& t = result.traits.at("EnemyAI");
    REQUIRE(t.fields.size() == 1);
    CHECK(t.fields[0].type.kind == TypeKind::Struct);
    CHECK(t.fields[0].type.name == "player.Vec2Pos");
    REQUIRE(t.fields[0].type.symbol_id.has_value());
    CHECK(*t.fields[0].type.symbol_id == make_symbol_id(SymbolKind::Struct, "player", "Vec2Pos"));
}

TEST_CASE("semantic_modules: qualified enum type resolved", "[semantic][modules][4.2]") {
    auto prog = make_program_with_trait_field("State", "dir", "physics.Direction");

    ImportedSymbols phys_syms;
    phys_syms.module_name = "physics";
    ResolvedEnum re;
    re.name                      = "Direction";
    re.variants                  = {"Up", "Down"};
    phys_syms.enums["Direction"] = re;

    ModuleImports imports;
    imports.add("physics", std::move(phys_syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
    auto& t = result.traits.at("State");
    CHECK(t.fields[0].type.kind == TypeKind::Enum);
    CHECK(t.fields[0].type.name == "physics.Direction");
    REQUIRE(t.fields[0].type.symbol_id.has_value());
    CHECK(*t.fields[0].type.symbol_id == make_symbol_id(SymbolKind::Enum, "physics", "Direction"));
}

TEST_CASE("semantic_modules: alias resolution (use player as p)", "[semantic][modules][4.2]") {
    // Module is registered under alias "p", field uses "p.Vec2Pos"
    auto prog = make_program_with_trait_field("EnemyAI", "pos", "p.Vec2Pos");

    ImportedSymbols player_syms;
    player_syms.module_name = "player";
    ResolvedStruct rs;
    rs.name                        = "Vec2Pos";
    player_syms.structs["Vec2Pos"] = rs;

    ModuleImports imports;
    imports.add("p", std::move(player_syms));  // alias "p"

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
    auto& t = result.traits.at("EnemyAI");
    CHECK(t.fields[0].type.kind == TypeKind::Struct);
    CHECK(t.fields[0].type.name == "player.Vec2Pos");
    REQUIRE(t.fields[0].type.symbol_id.has_value());
    CHECK(*t.fields[0].type.symbol_id == make_symbol_id(SymbolKind::Struct, "player", "Vec2Pos"));
}

TEST_CASE("semantic_modules: alias-qualified list element type stores canonical symbol identity",
          "[semantic][modules][3.1]") {
    ProgramNode prog = make_program_with_module();
    TraitNode trait;
    trait.name = "Inventory";
    FieldNode field;
    field.name             = "items";
    field.type.name        = "list";
    field.type.param       = std::make_unique<TypeRef>(TypeRef{.name = "items.Item", .location = {}});
    field.modifiers.is_var = true;
    trait.fields.push_back(std::move(field));
    prog.declarations.emplace_back(std::move(trait));

    ModuleImports imports;
    imports.add("items", make_module_with_struct("game.items", "Item"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
    const auto& type = result.traits.at("Inventory").fields.at(0).type;
    CHECK(type.kind == TypeKind::List);
    REQUIRE(type.element != nullptr);
    CHECK(type.element->kind == TypeKind::Struct);
    CHECK(type.element->name == "game.items.Item");
    REQUIRE(type.element->symbol_id.has_value());
    CHECK(*type.element->symbol_id == make_symbol_id(SymbolKind::Struct, "game.items", "Item"));
}

TEST_CASE("semantic_modules: unknown module qualifier error", "[semantic][modules][4.2]") {
    auto prog = make_program_with_trait_field("T", "x", "unknown_mod.Foo");

    ModuleImports imports;  // empty

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("unknown module qualifier") != std::string::npos);
}

// ── Task 4.3: Unqualified unique import lookup ───────────────────────────────

TEST_CASE("semantic_modules: unqualified unique struct resolved from import", "[semantic][modules][4.3]") {
    // Struct "Velocity" is only in one ordinary imported module, but imports are namespace bindings.
    auto prog = make_program_with_trait_field("Mover", "vel", "Velocity");

    auto syms = make_module_with_struct("physics", "Velocity");

    ModuleImports imports;
    imports.add("physics", std::move(syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("type 'Velocity' is imported from module 'physics'") != std::string::npos);
    CHECK(msg.find("must be referenced as 'physics.Velocity'") != std::string::npos);
}

TEST_CASE("semantic_modules: unqualified ambiguous type reports error", "[semantic][modules][4.3]") {
    // "Config" is in both modA and modB → ambiguous
    auto prog = make_program_with_trait_field("Service", "cfg", "Config");

    ImportedSymbols syms_a;
    syms_a.module_name       = "modA";
    syms_a.structs["Config"] = ResolvedStruct{.name = "Config", .fields = {}};

    ImportedSymbols syms_b;
    syms_b.module_name       = "modB";
    syms_b.structs["Config"] = ResolvedStruct{.name = "Config", .fields = {}};

    ModuleImports imports;
    imports.add("modA", std::move(syms_a));
    imports.add("modB", std::move(syms_b));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("ambiguous") != std::string::npos);
    CHECK(msg.find("Config") != std::string::npos);
}

// ── Task 4.4: Filter clause alias resolution ─────────────────────────────────

TEST_CASE("semantic_modules: filter entry with qualified trait resolved", "[semantic][modules][4.4]") {
    FilterEntry entry;
    entry.qualified_name = "player.Position";
    auto prog            = make_program_with_system("MoveSystem", {entry});

    auto syms = make_module_with_trait("player", "Position");
    ModuleImports imports;
    imports.add("player", std::move(syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: filter entry with alias resolves trait", "[semantic][modules][4.4]") {
    FilterEntry entry;
    entry.qualified_name = "p.Position";  // module registered as "p"
    entry.alias          = "pos";
    auto prog            = make_program_with_system("MoveSystem", {entry});

    auto syms = make_module_with_trait("player", "Position");
    ModuleImports imports;
    imports.add("p", std::move(syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: std.physics.flat collider filters resolve direct and alias imports",
          "[semantic][modules][stdlib][physics]") {
    auto direct = make_module_with_trait("std.physics.flat", "Collider");
    auto alias  = make_module_with_trait("std.physics.flat", "BoxCollider");

    ModuleImports imports;
    imports.add("std.physics.flat", std::move(direct));
    imports.add("phys", std::move(alias));

    FilterEntry collider;
    collider.qualified_name = "std.physics.flat.Collider";
    FilterEntry box;
    box.qualified_name = "phys.BoxCollider";
    auto prog          = make_program_with_system("Collide", {collider, box});

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: std.physics.volume collider filters resolve direct and alias imports",
          "[semantic][modules][stdlib][physics]") {
    auto direct = make_module_with_trait("std.physics.volume", "Collider");
    auto alias  = make_module_with_trait("std.physics.volume", "BoxCollider");

    ModuleImports imports;
    imports.add("std.physics.volume", std::move(direct));
    imports.add("phys3", std::move(alias));

    FilterEntry collider;
    collider.qualified_name = "std.physics.volume.Collider";
    FilterEntry box;
    box.qualified_name = "phys3.BoxCollider";
    auto prog          = make_program_with_system("Collide3D", {collider, box});

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: filter entry with unqualified trait from import", "[semantic][modules][4.4]") {
    FilterEntry entry;
    entry.qualified_name = "Position";  // unqualified ordinary import is rejected
    auto prog            = make_program_with_system("MoveSystem", {entry});

    auto syms = make_module_with_trait("player", "Position");
    ModuleImports imports;
    imports.add("player", std::move(syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("trait 'Position' is imported from module 'player'") != std::string::npos);
    CHECK(msg.find("must be referenced as 'player.Position'") != std::string::npos);
}

TEST_CASE("semantic_modules: ambiguous unqualified trait in filter", "[semantic][modules][4.4]") {
    FilterEntry entry;
    entry.qualified_name = "Config";
    auto prog            = make_program_with_system("Worker", {entry});

    auto syms_a = make_module_with_trait("modA", "Config");
    auto syms_b = make_module_with_trait("modB", "Config");

    ModuleImports imports;
    imports.add("modA", std::move(syms_a));
    imports.add("modB", std::move(syms_b));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("ambiguous") != std::string::npos);
}

// ── Task 4.6: Non-pub helpful error ──────────────────────────────────────────

TEST_CASE("semantic_modules: non-pub type reference suggests adding pub", "[semantic][modules][4.6]") {
    auto prog = make_program_with_trait_field("Enemy", "health", "player.PlayerPhysics");

    // player module exports no pub traits, but PlayerPhysics is listed as non-pub
    ImportedSymbols player_syms;
    player_syms.module_name = "player";
    // No pub traits/structs/enums

    ModuleImports imports;
    imports.add("player", std::move(player_syms), {"PlayerPhysics"});  // non-pub set

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("not public") != std::string::npos);
    CHECK(msg.find("PlayerPhysics") != std::string::npos);
    CHECK(msg.find("pub") != std::string::npos);
}

TEST_CASE("semantic_modules: non-pub filter trait suggests adding pub", "[semantic][modules][4.6]") {
    FilterEntry entry;
    entry.qualified_name = "player.Secret";
    auto prog            = make_program_with_system("Worker", {entry});

    ImportedSymbols player_syms;
    player_syms.module_name = "player";

    ModuleImports imports;
    imports.add("player", std::move(player_syms), {"Secret"});  // non-pub

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("not public") != std::string::npos);
}

// ── Task 4.7: Local module analysis ──────────────────────────────────────────

TEST_CASE("semantic_modules: explicit module with no imports works", "[semantic][modules][4.7]") {
    // Local trait, no imports
    ProgramNode prog = make_program_with_module();
    TraitNode trait;
    trait.name = "Position";
    FieldNode f;
    f.name             = "x";
    f.type.name        = "float";
    f.modifiers.is_var = true;
    trait.fields.push_back(std::move(f));
    prog.declarations.emplace_back(std::move(trait));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog);  // no imports — default empty

    CHECK_FALSE(errors.has_errors());
    REQUIRE(result.traits.count("Position") == 1);
    CHECK(result.traits.at("Position").fields[0].type.kind == TypeKind::Float);
}

// ── Task 3.8: Semantic resolution tests ─────────────────────────────────────

/// Make a program with an entity whose only trait is trait_name.
static ProgramNode make_program_with_entity_trait(const std::string& entity_name, const std::string& trait_name) {
    ProgramNode prog = make_program_with_module();
    EntityNode entity;
    entity.name = entity_name;
    ArchetypeTraitEntry entry;
    entry.trait_name = trait_name;
    entity.traits.push_back(std::move(entry));
    prog.declarations.emplace_back(std::move(entity));
    return prog;
}

/// Make ImportedSymbols whose only pub export is a named system.
static ImportedSymbols make_module_with_system(const std::string& module_name, const std::string& sys_name) {
    ImportedSymbols syms;
    syms.module_name = module_name;
    ImportedSystem sys;
    sys.name               = sys_name;
    sys.canonical_id       = make_canonical_id(module_name, sys_name);
    syms.systems[sys_name] = sys;
    return syms;
}

/// Make a program containing sys_name (with after: after_refs) plus any extra local systems.
static ProgramNode make_program_with_system_after(const std::string& sys_name,
                                                  const std::vector<std::string>& after_refs,
                                                  const std::vector<std::string>& extra_locals = {}) {
    ProgramNode prog = make_program_with_module();
    for (const auto& name : extra_locals) {
        SystemNode s;
        s.name = name;
        prog.declarations.emplace_back(std::move(s));
    }
    SystemNode sys;
    sys.name          = sys_name;
    sys.after_systems = after_refs;
    prog.declarations.emplace_back(std::move(sys));
    return prog;
}

// ── 3.1: Trait application in entity bodies ──────────────────────────────────

TEST_CASE("semantic_modules 3.1: qualified trait in entity resolves from import", "[semantic][modules][3.1]") {
    auto prog = make_program_with_entity_trait("Player", "flat.Position");
    ModuleImports imports;
    imports.add("flat", make_module_with_trait("std.transform.flat", "Position"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules 3.1: unknown qualifier in entity trait reports error", "[semantic][modules][3.1]") {
    auto prog = make_program_with_entity_trait("Player", "unknown.Position");
    ModuleImports imports;

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("unknown") != std::string::npos);
}

TEST_CASE("semantic_modules 3.1: ambiguous unqualified trait in entity reports error", "[semantic][modules][3.1]") {
    auto prog = make_program_with_entity_trait("Player", "Position");
    ModuleImports imports;
    imports.add("flat", make_module_with_trait("std.transform.flat", "Position"));
    imports.add("volume", make_module_with_trait("std.transform.volume", "Position"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("ambiguous") != std::string::npos);
    CHECK(errors.diagnostics()[0].message.find("Position") != std::string::npos);
}

TEST_CASE("semantic_modules 3.1: unique unqualified trait in entity from import succeeds", "[semantic][modules][3.1]") {
    auto prog = make_program_with_entity_trait("Player", "Position");
    ModuleImports imports;
    imports.add("flat", make_module_with_trait("std.transform.flat", "Position"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("trait 'Position' is imported from module 'std.transform.flat'") != std::string::npos);
    CHECK(msg.find("must be referenced as 'flat.Position'") != std::string::npos);
}

TEST_CASE("semantic_modules 3.1: non-pub trait in entity reports error", "[semantic][modules][3.1]") {
    auto prog = make_program_with_entity_trait("Player", "flat.Secret");

    ImportedSymbols flat_syms;
    flat_syms.module_name = "std.transform.flat";

    ModuleImports imports;
    imports.add("flat", std::move(flat_syms), {"Secret"});

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("not public") != std::string::npos);
}

// ── 3.4: after: clause resolution ───────────────────────────────────────────

TEST_CASE("semantic_modules 3.4: qualified after resolves imported system", "[semantic][modules][3.4]") {
    auto prog = make_program_with_system_after("B", {"flat.Transform"});
    ModuleImports imports;
    imports.add("flat", make_module_with_system("std.transform.flat", "Transform"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(result.dependency_graph.size() == 1);
    const auto& dep = result.dependency_graph[0];
    REQUIRE(dep.after_systems.size() == 1);
    CHECK(dep.after_systems[0] == "std.transform.flat.Transform");
}

TEST_CASE("semantic_modules 3.4: unique unqualified after from import succeeds", "[semantic][modules][3.4]") {
    auto prog = make_program_with_system_after("B", {"Transform"});
    ModuleImports imports;
    imports.add("flat", make_module_with_system("std.transform.flat", "Transform"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("system 'Transform' is imported from module 'std.transform.flat'") != std::string::npos);
    CHECK(msg.find("must be referenced as 'flat.Transform'") != std::string::npos);
}

TEST_CASE("semantic_modules 3.4: ambiguous unqualified after reports error", "[semantic][modules][3.4]") {
    auto prog = make_program_with_system_after("B", {"Transform"});
    ModuleImports imports;
    imports.add("flat", make_module_with_system("std.transform.flat", "Transform"));
    imports.add("volume", make_module_with_system("std.transform.volume", "Transform"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("ambiguous") != std::string::npos);
    CHECK(errors.diagnostics()[0].message.find("Transform") != std::string::npos);
}

TEST_CASE("semantic_modules 3.4: unknown qualifier in after reports error", "[semantic][modules][3.4]") {
    auto prog = make_program_with_system_after("B", {"bad.Transform"});
    ModuleImports imports;

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("unknown") != std::string::npos);
}

TEST_CASE("semantic_modules 3.4: local system in after resolves without imports", "[semantic][modules][3.4]") {
    auto prog = make_program_with_system_after("B", {"A"}, {"A"});

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog);

    REQUIRE_FALSE(errors.has_errors());
    // Find system B
    const SystemDependency* b_dep = nullptr;
    for (const auto& d : result.dependency_graph) {
        if (d.system_name == "B") {
            b_dep = &d;
            break;
        }
    }
    REQUIRE(b_dep != nullptr);
    REQUIRE(b_dep->after_systems.size() == 1);
    CHECK(b_dep->after_systems[0] == "test.module.A");
}

// ── 3.7: Dependency graph canonical trait IDs ────────────────────────────────

TEST_CASE("semantic_modules 3.7: filter canonical trait ID stored in dep.reads", "[semantic][modules][3.7]") {
    FilterEntry entry;
    entry.qualified_name = "flat.Position";
    auto prog            = make_program_with_system("ReadSystem", {entry});

    ModuleImports imports;
    imports.add("flat", make_module_with_trait("std.transform.flat", "Position"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(result.dependency_graph.size() == 1);
    const auto& dep = result.dependency_graph[0];
    CHECK(dep.reads.count("std.transform.flat.Position") == 1);
}

// ── Task 6.3: Reconcile with refactor-editor-dsl-boundaries ─────────────────
// When std.editor eventually imports both transform modules (for world_position),
// it will use alias-qualified names: `transform2d.WorldTransform` and
// `transform3d.WorldTransform`. The tests below verify that the alias-qualified
// filter resolution and ambiguity detection work correctly for this scenario.

TEST_CASE("semantic_modules 6.3: system filter resolves flat and volume traits via distinct aliases",
          "[semantic][modules][6.3]") {
    // Simulates `use std.transform.flat as transform2d` + `use std.transform.volume as transform3d`
    // A system filters on both: flat.WorldTransform and volume.WorldTransform are distinct.
    FilterEntry entry_flat;
    entry_flat.qualified_name = "transform2d.WorldTransform";
    FilterEntry entry_vol;
    entry_vol.qualified_name = "transform3d.WorldTransform";
    auto prog                = make_program_with_system("RenderSystem", {entry_flat, entry_vol});

    ModuleImports imports;
    imports.add("transform2d", make_module_with_trait("std.transform.flat", "WorldTransform"));
    imports.add("transform3d", make_module_with_trait("std.transform.volume", "WorldTransform"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(result.dependency_graph.size() == 1);
    const auto& dep = result.dependency_graph[0];
    CHECK(dep.reads.count("std.transform.flat.WorldTransform") == 1);
    CHECK(dep.reads.count("std.transform.volume.WorldTransform") == 1);
}

TEST_CASE("semantic_modules 6.3: unqualified WorldTransform is ambiguous when both transforms imported",
          "[semantic][modules][6.3]") {
    // Using unqualified WorldTransform when both flat and volume are in scope must fail.
    FilterEntry entry;
    entry.qualified_name = "WorldTransform";
    auto prog            = make_program_with_system("S", {entry});

    ModuleImports imports;
    imports.add("transform2d", make_module_with_trait("std.transform.flat", "WorldTransform"));
    imports.add("transform3d", make_module_with_trait("std.transform.volume", "WorldTransform"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("ambiguous") != std::string::npos);
    CHECK(errors.diagnostics()[0].message.find("WorldTransform") != std::string::npos);
}

TEST_CASE("semantic_modules: local filter trait still works", "[semantic][modules][4.7]") {
    // Define a local trait, then a system filtering on it (trait_names, no entries)
    ProgramNode prog = make_program_with_module();
    TraitNode trait;
    trait.name = "Health";
    prog.declarations.emplace_back(std::move(trait));

    SystemNode sys;
    sys.name               = "HealSystem";
    sys.filter.trait_names = {"Health"};
    // entries is empty — backward-compat path
    prog.declarations.emplace_back(std::move(sys));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog);

    CHECK_FALSE(errors.has_errors());
}

// ── Task 6.3: std.core prelude canonicalization ───────────────────────────────

TEST_CASE("semantic_modules 6.3: on-tick handler resolved_event_id points to std.core.tick",
          "[semantic][modules][6.3]") {
    // When std.core is in imports, an `on tick:` handler must resolve to the
    // canonical SymbolId {Event, "std.core", "tick"} — not left nullopt.
    ProgramNode prog = make_program_with_module("game.counter");

    SystemNode sys;
    sys.name = "CountTicks";

    EventHandlerNode handler;
    handler.event_name = "tick";
    sys.handlers.push_back(std::move(handler));
    prog.declarations.emplace_back(std::move(sys));

    ImportedSymbols core_syms;
    core_syms.module_name = "std.core";
    core_syms.events.insert("tick");
    ImportedEvent tick_ev;
    tick_ev.name        = "tick";
    tick_ev.module_name = "std.core";
    tick_ev.symbol_id   = make_symbol_id(SymbolKind::Event, "std.core", "tick");
    core_syms.event_symbols["tick"] = std::move(tick_ev);

    ModuleImports imports;
    imports.add("core", std::move(core_syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE_FALSE(errors.has_errors());

    const auto* sys_decl = std::get_if<SystemNode>(&prog.declarations.back());
    REQUIRE(sys_decl != nullptr);
    REQUIRE_FALSE(sys_decl->handlers.empty());
    const auto& h = sys_decl->handlers[0];
    REQUIRE(h.resolved_event_id.has_value());
    CHECK(*h.resolved_event_id == make_symbol_id(SymbolKind::Event, "std.core", "tick"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
