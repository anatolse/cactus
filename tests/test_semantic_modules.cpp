// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "common/types.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>

using namespace cactus;

// ── Helpers ──────────────────────────────────────────────────────────────────

static constexpr const char* TEST_MODULE_NAME = "test.module";

static ProgramNode make_program_with_module(const std::string& module_name = TEST_MODULE_NAME) {
    ProgramNode prog;
    prog.declarations.emplace_back(ModuleNode{.name = module_name, .location = {}});
    return prog;
}

static std::pair<DecoratedProgram, std::vector<Diagnostic>> analyze_source(const std::string& source,
                                                                           const ModuleImports& imports = {}) {
    ErrorReporter errors;
    Lexer lexer(source, "semantic_modules_test.cactus", errors);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens), errors);
    auto ast = parser.parse_program();
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(ast, imports);
    return {std::move(result), errors.diagnostics()};
}

static ExprNode make_int_literal_expr(const std::string& value = "1") {
    return ExprNode{ExprNode::Variant{LiteralExpr{.kind = LiteralExpr::Kind::Int, .value = value, .location = {}}}, {}};
}

static std::unique_ptr<ExprNode> make_literal_expr(LiteralExpr::Kind kind, const std::string& value) {
    return std::make_unique<ExprNode>(ExprNode::Variant{LiteralExpr{.kind = kind, .value = value, .location = {}}},
                                      SourceLocation{});
}

static std::unique_ptr<ExprNode> make_ident_expr(const std::string& name) {
    return std::make_unique<ExprNode>(ExprNode::Variant{IdentExpr{.name = name, .location = {}}}, SourceLocation{});
}

static std::unique_ptr<ExprNode> make_member_expr(const std::string& owner, const std::string& member) {
    MemberExpr expression{
        .object = make_ident_expr(owner), .member = member, .resolved_enum_member = {}, .location = {}};
    return std::make_unique<ExprNode>(ExprNode::Variant{std::move(expression)}, SourceLocation{});
}

static EventNode make_external_event(const std::string& name, bool with_dt = false) {
    EventNode event;
    event.name        = name;
    event.is_external = true;
    if (with_dt) {
        FieldNode dt;
        dt.name      = "dt";
        dt.type.name = "float";
        event.fields.push_back(std::move(dt));
    }
    return event;
}

static bool has_diagnostic(const ErrorReporter& errors, const std::string& text) {
    return std::ranges::any_of(errors.diagnostics(), [&text](const auto& diagnostic) {
        return diagnostic.message.find(text) != std::string::npos;
    });
}

static bool has_diagnostic(const std::vector<Diagnostic>& diagnostics, const std::string& text) {
    return std::ranges::any_of(
        diagnostics, [&text](const auto& diagnostic) { return diagnostic.message.find(text) != std::string::npos; });
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
    SECTION("phase") {
        PhaseNode node;
        node.name = "Shared";
        expect_duplicate(duplicate_against_trait_error(std::move(node)), "phase");
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

TEST_CASE("semantic_modules 3.7: filter canonical trait ID resolved for aliased import", "[semantic][modules][3.7]") {
    FilterEntry entry;
    entry.qualified_name = "flat.Position";
    auto prog            = make_program_with_system("ReadSystem", {entry});

    ModuleImports imports;
    imports.add("flat", make_module_with_trait("std.transform.flat", "Position"));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE_FALSE(errors.has_errors());
    const auto* sys = std::get_if<SystemNode>(&prog.declarations.back());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->filter.resolved_trait_ids.size() == 1);
    CHECK(make_canonical_id(sys->filter.resolved_trait_ids[0]) == "std.transform.flat.Position");
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
    analyzer.analyze(prog, imports);

    REQUIRE_FALSE(errors.has_errors());
    const auto* sys = std::get_if<SystemNode>(&prog.declarations.back());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->filter.resolved_trait_ids.size() == 2);
    CHECK(make_canonical_id(sys->filter.resolved_trait_ids[0]) == "std.transform.flat.WorldTransform");
    CHECK(make_canonical_id(sys->filter.resolved_trait_ids[1]) == "std.transform.volume.WorldTransform");
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
    tick_ev.name                    = "tick";
    tick_ev.module_name             = "std.core";
    tick_ev.symbol_id               = make_symbol_id(SymbolKind::Event, "std.core", "tick");
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
    REQUIRE(h.resolved_trigger.has_value());
    CHECK(h.resolved_trigger->kind == HandlerTriggerKind::Event);
    CHECK(h.resolved_trigger->symbol == make_symbol_id(SymbolKind::Event, "std.core", "tick"));
}

TEST_CASE("semantic_modules: local external event and phase retain canonical identities and tagged dependencies",
          "[semantic][modules][phase][external-event]") {
    ProgramNode prog = make_program_with_module("game.runtime");

    EventNode frame;
    frame.name        = "frame";
    frame.is_pub      = true;
    frame.is_external = true;
    prog.declarations.emplace_back(std::move(frame));

    PhaseNode tick;
    tick.name   = "tick";
    tick.is_pub = true;
    tick.from_sources.push_back(LocatedName{.spelling = "frame", .location = {"runtime.cactus", 4, 9}});
    prog.declarations.emplace_back(std::move(tick));

    TraitNode selected;
    selected.name = "Selected";
    prog.declarations.emplace_back(std::move(selected));

    SystemNode regular;
    regular.name = "Move";
    EventHandlerNode regular_handler;
    regular_handler.event_name = "tick";
    regular.handlers.push_back(std::move(regular_handler));
    prog.declarations.emplace_back(std::move(regular));

    ExternSystemNode external;
    external.name               = "NativeMove";
    external.filter.trait_names = {"Selected"};
    ExternHandlerNode external_handler;
    external_handler.trigger_name = "tick";
    external.handlers.push_back(std::move(external_handler));
    prog.declarations.emplace_back(std::move(external));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(prog);

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(decorated.events.contains("frame"));
    const auto& resolved_frame = decorated.events.at("frame");
    REQUIRE(resolved_frame.symbol_id.has_value());
    CHECK(*resolved_frame.symbol_id == make_symbol_id(SymbolKind::Event, "game.runtime", "frame"));
    CHECK(resolved_frame.canonical_id == "game.runtime.frame");
    CHECK(resolved_frame.is_external);

    REQUIRE(decorated.phases.contains("tick"));
    const auto& resolved_tick = decorated.phases.at("tick");
    REQUIRE(resolved_tick.symbol_id.has_value());
    CHECK(*resolved_tick.symbol_id == make_symbol_id(SymbolKind::Phase, "game.runtime", "tick"));
    CHECK(resolved_tick.canonical_id == "game.runtime.tick");
    REQUIRE(resolved_tick.from_sources.size() == 1);
    CHECK(resolved_tick.from_sources[0].kind == HandlerTriggerKind::Event);
    CHECK(resolved_tick.from_sources[0].symbol == make_symbol_id(SymbolKind::Event, "game.runtime", "frame"));

    const auto& regular_decl = std::get<SystemNode>(prog.declarations[4]);
    REQUIRE(regular_decl.handlers[0].resolved_trigger.has_value());
    CHECK(regular_decl.handlers[0].resolved_trigger->kind == HandlerTriggerKind::Phase);
    CHECK(regular_decl.handlers[0].resolved_trigger->symbol ==
          make_symbol_id(SymbolKind::Phase, "game.runtime", "tick"));

    const auto& external_decl = std::get<ExternSystemNode>(prog.declarations[5]);
    REQUIRE(external_decl.handlers[0].resolved_trigger.has_value());
    CHECK(external_decl.handlers[0].resolved_trigger->kind == HandlerTriggerKind::Phase);
    CHECK(external_decl.handlers[0].resolved_trigger->symbol ==
          make_symbol_id(SymbolKind::Phase, "game.runtime", "tick"));
}

TEST_CASE("semantic_modules: imported phase and external-event provenance normalize and resolve canonically",
          "[semantic][modules][phase][external-event][imports]") {
    ImportedSymbols core;
    core.module_name            = "std.core";
    core.event_symbols["frame"] = ImportedEvent{
        .name        = "frame",
        .is_external = true,
    };
    core.phase_symbols["tick"] = ImportedPhase{.name = "tick"};

    ModuleImports imports;
    imports.add("core", std::move(core));

    REQUIRE(imports.modules.at("core").event_symbols.at("frame").symbol_id.has_value());
    CHECK(*imports.modules.at("core").event_symbols.at("frame").symbol_id ==
          make_symbol_id(SymbolKind::Event, "std.core", "frame"));
    CHECK(imports.modules.at("core").event_symbols.at("frame").canonical_id == "std.core.frame");
    CHECK(imports.modules.at("core").event_symbols.at("frame").is_external);
    REQUIRE(imports.modules.at("core").phase_symbols.at("tick").symbol_id.has_value());
    CHECK(*imports.modules.at("core").phase_symbols.at("tick").symbol_id ==
          make_symbol_id(SymbolKind::Phase, "std.core", "tick"));
    CHECK(imports.modules.at("core").phase_symbols.at("tick").canonical_id == "std.core.tick");
    REQUIRE(imports.phase_providers.contains("tick"));
    CHECK(imports.phase_providers.at("tick") == std::vector<std::string>{"core"});

    ProgramNode prog = make_program_with_module("game.client");
    PhaseNode render;
    render.name = "render";
    render.from_sources.push_back(LocatedName{.spelling = "core.frame", .location = {}});
    prog.declarations.emplace_back(std::move(render));

    SystemNode system;
    system.name = "Animate";
    EventHandlerNode tick_handler;
    tick_handler.event_name = "core.tick";
    system.handlers.push_back(std::move(tick_handler));
    EventHandlerNode frame_handler;
    frame_handler.event_name = "core.frame";
    system.handlers.push_back(std::move(frame_handler));
    prog.declarations.emplace_back(std::move(system));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(prog, imports);

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(decorated.phases.at("render").from_sources.size() == 1);
    CHECK(decorated.phases.at("render").from_sources[0].symbol ==
          make_symbol_id(SymbolKind::Event, "std.core", "frame"));

    const auto& resolved_system = std::get<SystemNode>(prog.declarations.back());
    REQUIRE(resolved_system.handlers[0].resolved_trigger.has_value());
    CHECK(resolved_system.handlers[0].resolved_trigger->kind == HandlerTriggerKind::Phase);
    CHECK(resolved_system.handlers[0].resolved_trigger->symbol ==
          make_symbol_id(SymbolKind::Phase, "std.core", "tick"));
    REQUIRE(resolved_system.handlers[1].resolved_trigger.has_value());
    CHECK(resolved_system.handlers[1].resolved_trigger->kind == HandlerTriggerKind::Event);
    CHECK(resolved_system.handlers[1].resolved_trigger->symbol ==
          make_symbol_id(SymbolKind::Event, "std.core", "frame"));
}

TEST_CASE("semantic_modules: authored emit cannot produce a local external event",
          "[semantic][modules][external-event][errors]") {
    ProgramNode prog = make_program_with_module("game.runtime");

    EventNode frame;
    frame.name        = "frame";
    frame.is_external = true;
    prog.declarations.emplace_back(std::move(frame));

    EventNode request;
    request.name = "Request";
    prog.declarations.emplace_back(std::move(request));

    SystemNode system;
    system.name = "BadProducer";
    EventHandlerNode handler;
    handler.event_name = "Request";
    EmitStmt emit;
    emit.event_name          = "frame";
    emit.location            = {"runtime.cactus", 8, 9};
    const auto emit_location = emit.location;
    handler.body.push_back(std::make_unique<StmtNode>(StmtNode::Variant{std::move(emit)}, emit_location));
    system.handlers.push_back(std::move(handler));
    prog.declarations.emplace_back(std::move(system));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog);

    REQUIRE(errors.has_errors());
    bool found = false;
    for (const auto& diagnostic : errors.diagnostics()) {
        if (diagnostic.message.find("external event 'game.runtime.frame' can only be emitted by the runtime") !=
            std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("semantic_modules: authored contracts cannot emit imported external events",
          "[semantic][modules][external-event][imports][errors]") {
    ImportedSymbols core;
    core.module_name            = "std.core";
    core.event_symbols["frame"] = ImportedEvent{.name = "frame", .is_external = true};
    core.phase_symbols["tick"]  = ImportedPhase{.name = "tick"};
    ModuleImports imports;
    imports.add("core", std::move(core));

    ProgramNode prog = make_program_with_module("game.host");
    TraitNode selected;
    selected.name = "Selected";
    prog.declarations.emplace_back(std::move(selected));

    ExternSystemNode system;
    system.name               = "BadHost";
    system.filter.trait_names = {"Selected"};
    ExternHandlerNode handler;
    handler.trigger_name = "core.tick";
    handler.emits.push_back(LocatedName{.spelling = "core.frame", .location = {"host.cactus", 7, 13}});
    system.handlers.push_back(std::move(handler));
    prog.declarations.emplace_back(std::move(system));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE(errors.has_errors());
    bool found = false;
    for (const auto& diagnostic : errors.diagnostics()) {
        if (diagnostic.message.find("external event 'std.core.frame' can only be emitted by the runtime") !=
            std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("semantic_modules: phase analysis resolves lineage cadence and synthesized activation data",
          "[semantic][modules][phase][2.2]") {
    ProgramNode prog = make_program_with_module("game.loop");
    prog.declarations.emplace_back(make_external_event("frame", true));

    ConstBlockNode constants;
    constants.assignments.push_back(ConstAssignment{
        .name = "fixed_step", .value = make_literal_expr(LiteralExpr::Kind::Float, "0.25"), .location = {}});
    constants.assignments.push_back(ConstAssignment{
        .name = "catch_up_limit", .value = make_literal_expr(LiteralExpr::Kind::Int, "4"), .location = {}});
    prog.declarations.emplace_back(std::move(constants));

    PhaseNode fixed_tick;
    fixed_tick.name = "fixed_tick";
    fixed_tick.from_sources.push_back(LocatedName{.spelling = "frame", .location = {}});
    fixed_tick.every.emplace(make_ident_expr("fixed_step"));
    fixed_tick.max.emplace(make_ident_expr("catch_up_limit"));
    prog.declarations.emplace_back(std::move(fixed_tick));

    PhaseNode render;
    render.name = "render";
    render.after_phases.push_back(LocatedName{.spelling = "fixed_tick", .location = {}});
    PhaseFieldNode alpha;
    alpha.name        = "interpolation";
    alpha.type.name   = "float";
    alpha.initializer = make_member_expr("fixed_tick", "alpha");
    render.fields.push_back(std::move(alpha));
    PhaseFieldNode frame_dt;
    frame_dt.name        = "frame_dt";
    frame_dt.type.name   = "float";
    frame_dt.initializer = make_member_expr("frame", "dt");
    render.fields.push_back(std::move(frame_dt));
    prog.declarations.emplace_back(std::move(render));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog);

    REQUIRE_FALSE(errors.has_errors());
    const auto frame_id = make_symbol_id(SymbolKind::Event, "game.loop", "frame");
    const auto tick_id  = make_symbol_id(SymbolKind::Phase, "game.loop", "fixed_tick");
    const auto& tick    = result.phases.at("fixed_tick");
    REQUIRE(tick.runtime_root.has_value());
    CHECK(*tick.runtime_root == frame_id);
    REQUIRE(tick.every_seconds.has_value());
    CHECK(*tick.every_seconds == 0.25);
    REQUIRE(tick.max_repetitions.has_value());
    CHECK(*tick.max_repetitions == 4);
    REQUIRE(tick.fields.size() == 2);
    CHECK(tick.fields[0].name == "dt");
    CHECK(tick.fields[0].is_synthesized);
    CHECK_FALSE(tick.fields[0].is_completion_only);
    CHECK(tick.fields[1].name == "alpha");
    CHECK(tick.fields[1].is_synthesized);
    CHECK(tick.fields[1].is_completion_only);

    const auto& resolved_render = result.phases.at("render");
    REQUIRE(resolved_render.runtime_root.has_value());
    CHECK(*resolved_render.runtime_root == frame_id);
    CHECK(resolved_render.upstream_phases == std::vector<SymbolId>{tick_id});
    REQUIRE(resolved_render.fields.size() == 2);
    CHECK(resolved_render.fields[0].type.kind == TypeKind::Float);
    CHECK(resolved_render.fields[1].type.kind == TypeKind::Float);

    REQUIRE(result.execution_graph.phases.size() == 2);
    const auto& tick_plan = result.execution_graph.phases[0];
    CHECK(tick_plan.phase == tick_id);
    CHECK(tick_plan.source_dependencies == std::vector<ResolvedHandlerTrigger>{ResolvedHandlerTrigger{
                                               .kind = HandlerTriggerKind::Event, .symbol = frame_id}});
    CHECK(tick_plan.completion_dependencies.empty());
    REQUIRE(tick_plan.fields.size() == tick.fields.size());
    CHECK(tick_plan.fields[0].name == "dt");
    CHECK(tick_plan.fields[0].type.kind == TypeKind::Float);
    CHECK(tick_plan.fields[0].is_synthesized);
    CHECK_FALSE(tick_plan.fields[0].is_completion_only);
    CHECK(tick_plan.fields[1].name == "alpha");
    CHECK(tick_plan.fields[1].type.kind == TypeKind::Float);
    CHECK(tick_plan.fields[1].is_synthesized);
    CHECK(tick_plan.fields[1].is_completion_only);
    CHECK(tick_plan.runtime_root == frame_id);
    CHECK(tick_plan.every_seconds == 0.25);
    CHECK(tick_plan.max_repetitions == 4);
    CHECK(tick_plan.declaration_order ==
          DeclarationOrder{.module_index = 0, .declaration_index = 3, .handler_index = 0});

    const auto& render_plan = result.execution_graph.phases[1];
    CHECK(render_plan.phase == make_symbol_id(SymbolKind::Phase, "game.loop", "render"));
    CHECK(render_plan.source_dependencies.empty());
    CHECK(render_plan.completion_dependencies == std::vector<SymbolId>{tick_id});
    CHECK(render_plan.runtime_root == frame_id);
    CHECK(render_plan.declaration_order ==
          DeclarationOrder{.module_index = 0, .declaration_index = 4, .handler_index = 0});
    CHECK(result.execution_graph.handlers.empty());
    CHECK(result.execution_graph.schedule_edges.empty());
    CHECK(result.execution_graph.phase_barriers.empty());
    CHECK(result.execution_graph.event_flows.empty());
    CHECK(result.execution_graph.stable_topological_order.empty());
    CHECK(result.execution_graph.dependency_levels.empty());
}

TEST_CASE("semantic_modules: phase analysis diagnoses ambiguous roots and cycles",
          "[semantic][modules][phase][2.2][errors]") {
    SECTION("ambiguous runtime roots") {
        ProgramNode prog = make_program_with_module("game.loop");
        prog.declarations.emplace_back(make_external_event("frame"));
        prog.declarations.emplace_back(make_external_event("network"));
        PhaseNode phase;
        phase.name = "mixed";
        phase.from_sources.push_back(LocatedName{.spelling = "frame", .location = {}});
        phase.from_sources.push_back(LocatedName{.spelling = "network", .location = {}});
        prog.declarations.emplace_back(std::move(phase));

        ErrorReporter errors;
        SemanticAnalyzer analyzer(errors);
        analyzer.analyze(prog);
        CHECK(has_diagnostic(errors, "phase 'game.loop.mixed' has ambiguous runtime-root lineage"));
        CHECK(has_diagnostic(errors, "'game.loop.frame'"));
        CHECK(has_diagnostic(errors, "'game.loop.network'"));
    }

    SECTION("phase cycle uses canonical path") {
        ProgramNode prog = make_program_with_module("game.loop");
        PhaseNode first;
        first.name = "first";
        first.after_phases.push_back(LocatedName{.spelling = "second", .location = {}});
        prog.declarations.emplace_back(std::move(first));
        PhaseNode second;
        second.name = "second";
        second.after_phases.push_back(LocatedName{.spelling = "first", .location = {}});
        prog.declarations.emplace_back(std::move(second));

        ErrorReporter errors;
        SemanticAnalyzer analyzer(errors);
        analyzer.analyze(prog);
        CHECK(has_diagnostic(errors, "phase cycle: game.loop.first -> game.loop.second -> game.loop.first"));
    }
}

TEST_CASE("semantic_modules: phase cadence and initializers are statically validated",
          "[semantic][modules][phase][2.2][errors]") {
    ProgramNode prog = make_program_with_module("game.loop");
    prog.declarations.emplace_back(make_external_event("frame", true));

    PhaseNode unrelated;
    unrelated.name = "unrelated";
    unrelated.from_sources.push_back(LocatedName{.spelling = "frame", .location = {}});
    unrelated.every.emplace(make_literal_expr(LiteralExpr::Kind::Float, "0.5"));
    prog.declarations.emplace_back(std::move(unrelated));

    PhaseNode invalid;
    invalid.name = "invalid";
    invalid.from_sources.push_back(LocatedName{.spelling = "frame", .location = {}});
    invalid.every.emplace(make_literal_expr(LiteralExpr::Kind::Int, "1"));
    invalid.max.emplace(make_literal_expr(LiteralExpr::Kind::Float, "2.0"));
    PhaseFieldNode mismatch;
    mismatch.name        = "count";
    mismatch.type.name   = "int";
    mismatch.initializer = make_member_expr("frame", "dt");
    invalid.fields.push_back(std::move(mismatch));
    PhaseFieldNode sibling_read;
    sibling_read.name        = "forbidden";
    sibling_read.type.name   = "float";
    sibling_read.initializer = make_member_expr("unrelated", "dt");
    invalid.fields.push_back(std::move(sibling_read));
    prog.declarations.emplace_back(std::move(invalid));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog);

    CHECK(has_diagnostic(errors, "phase 'invalid' every value must be a positive compile-time float"));
    CHECK(has_diagnostic(errors, "phase 'invalid' max value must be a positive compile-time integer"));
    CHECK(has_diagnostic(errors, "phase field 'invalid.count' has type 'int' but initializer has type 'float'"));
    CHECK(has_diagnostic(errors, "initializer cannot read non-upstream value 'game.loop.unrelated'"));
}

TEST_CASE("semantic_modules: periodic completion alpha is downstream-only", "[semantic][modules][phase][2.2][errors]") {
    ProgramNode prog = make_program_with_module("game.loop");
    prog.declarations.emplace_back(make_external_event("frame", true));

    PhaseNode fixed_tick;
    fixed_tick.name = "fixed_tick";
    fixed_tick.from_sources.push_back(LocatedName{.spelling = "frame", .location = {}});
    fixed_tick.every.emplace(make_literal_expr(LiteralExpr::Kind::Float, "0.25"));
    prog.declarations.emplace_back(std::move(fixed_tick));

    SystemNode system;
    system.name = "Simulation";
    EventHandlerNode handler;
    handler.event_name = "fixed_tick";
    handler.alias      = "step";
    LetStmt read_dt{.name = "dt", .value = make_member_expr("step", "dt"), .location = {}};
    handler.body.push_back(std::make_unique<StmtNode>(StmtNode::Variant{std::move(read_dt)}, SourceLocation{}));
    LetStmt read_alpha{.name = "alpha", .value = make_member_expr("step", "alpha"), .location = {}};
    handler.body.push_back(std::make_unique<StmtNode>(StmtNode::Variant{std::move(read_alpha)}, SourceLocation{}));
    system.handlers.push_back(std::move(handler));
    prog.declarations.emplace_back(std::move(system));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog);

    CHECK(has_diagnostic(errors,
                         "phase completion field 'game.loop.fixed_tick.alpha' is available only to downstream phases"));
    CHECK_FALSE(has_diagnostic(errors, "game.loop.fixed_tick.dt' is available only"));
}

TEST_CASE("semantic_modules: external handler contracts resolve canonical capabilities",
          "[semantic][modules][extern-system][2.3]") {
    ProgramNode prog = make_program_with_module("game.render");
    EventNode pulse;
    pulse.name = "pulse";
    prog.declarations.emplace_back(std::move(pulse));
    EventNode spawned;
    spawned.name = "spawned";
    prog.declarations.emplace_back(std::move(spawned));

    ImportedSymbols physics;
    physics.module_name = "engine.physics";
    ResolvedTrait position;
    position.name   = "Position";
    position.is_pub = true;
    physics.traits.emplace("Position", std::move(position));
    ImportedTemplate bullet;
    bullet.name = "Bullet";
    physics.templates.emplace("Bullet", std::move(bullet));
    ModuleImports imports;
    imports.add("phys", std::move(physics));

    ExternSystemNode system;
    system.name = "Renderer";
    FilterEntry filtered;
    filtered.qualified_name = "phys.Position";
    filtered.alias          = "pos";
    system.filter.entries.push_back(std::move(filtered));
    ExternHandlerNode handler;
    handler.trigger_name = "pulse";
    handler.reads.push_back(LocatedName{.spelling = "pos", .location = {}});
    handler.writes.push_back(LocatedName{.spelling = "phys.Position", .location = {}});
    handler.emits.push_back(LocatedName{.spelling = "spawned", .location = {}});
    HandlerCommandNode spawn_command;
    spawn_command.kind   = HandlerCommandKind::Spawn;
    spawn_command.target = LocatedName{.spelling = "phys.Bullet", .location = {}};
    handler.commands.push_back(std::move(spawn_command));
    handler.commands.push_back(HandlerCommandNode{.kind = HandlerCommandKind::Destroy, .location = {}});
    handler.effects.push_back(LocatedName{.spelling = "graphics.draw", .location = {}});
    system.handlers.push_back(std::move(handler));
    prog.declarations.emplace_back(std::move(system));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    REQUIRE_FALSE(errors.has_errors());
    const auto& resolved_system  = std::get<ExternSystemNode>(prog.declarations.back());
    const auto& resolved_handler = resolved_system.handlers.front();
    CHECK(resolved_handler.resolved_reads ==
          std::vector<SymbolId>{make_symbol_id(SymbolKind::Trait, "engine.physics", "Position")});
    CHECK(resolved_handler.resolved_writes == resolved_handler.resolved_reads);
    CHECK(resolved_handler.resolved_emits ==
          std::vector<SymbolId>{make_symbol_id(SymbolKind::Event, "game.render", "spawned")});
    REQUIRE(resolved_handler.commands.front().resolved_target_id.has_value());
    CHECK(*resolved_handler.commands.front().resolved_target_id ==
          make_symbol_id(SymbolKind::Template, "engine.physics", "Bullet"));
    CHECK(resolved_handler.resolved_effects == std::vector<std::string>{"graphics.draw"});
}

TEST_CASE("semantic_modules: selectionless external systems are valid but contracts are mandatory",
          "[semantic][modules][extern-system][2.3]") {
    SECTION("selectionless handler contract") {
        ProgramNode prog = make_program_with_module("game.host");
        EventNode pulse;
        pulse.name = "pulse";
        prog.declarations.emplace_back(std::move(pulse));
        ExternSystemNode system;
        system.name = "HostPump";
        ExternHandlerNode handler;
        handler.trigger_name = "pulse";
        handler.effects.push_back(LocatedName{.spelling = "host.poll", .location = {}});
        system.handlers.push_back(std::move(handler));
        prog.declarations.emplace_back(std::move(system));

        ErrorReporter errors;
        SemanticAnalyzer analyzer(errors);
        analyzer.analyze(prog);
        CHECK_FALSE(errors.has_errors());
    }

    SECTION("handlerless declaration") {
        ProgramNode prog = make_program_with_module("game.host");
        ExternSystemNode system;
        system.name = "HostPump";
        prog.declarations.emplace_back(std::move(system));

        ErrorReporter errors;
        SemanticAnalyzer analyzer(errors);
        analyzer.analyze(prog);
        CHECK(has_diagnostic(errors, "extern system 'HostPump' requires at least one external handler contract"));
    }
}

TEST_CASE("semantic_modules: malformed external capabilities are rejected",
          "[semantic][modules][extern-system][2.3][errors]") {
    ProgramNode prog = make_program_with_module("game.host");
    EventNode pulse;
    pulse.name = "pulse";
    prog.declarations.emplace_back(std::move(pulse));
    TraitNode position;
    position.name = "Position";
    prog.declarations.emplace_back(std::move(position));
    EntityNode not_a_template;
    not_a_template.name = "Actor";
    prog.declarations.emplace_back(std::move(not_a_template));

    ExternSystemNode system;
    system.name = "BrokenHost";
    ExternHandlerNode handler;
    handler.trigger_name = "pulse";
    handler.reads.push_back(LocatedName{.spelling = "Position", .location = {}});
    handler.reads.push_back(LocatedName{.spelling = "Position", .location = {}});
    handler.writes.push_back(LocatedName{.spelling = "MissingTrait", .location = {}});
    handler.emits.push_back(LocatedName{.spelling = "missing_event", .location = {}});
    HandlerCommandNode bad_spawn;
    bad_spawn.kind   = HandlerCommandKind::Spawn;
    bad_spawn.target = LocatedName{.spelling = "Actor", .location = {}};
    handler.commands.push_back(std::move(bad_spawn));
    handler.commands.push_back(HandlerCommandNode{.kind = HandlerCommandKind::Add, .location = {}});
    handler.commands.push_back(HandlerCommandNode{.kind = HandlerCommandKind::Destroy, .location = {}});
    handler.commands.push_back(HandlerCommandNode{.kind = HandlerCommandKind::Destroy, .location = {}});
    handler.effects.push_back(LocatedName{.spelling = "graphics..draw", .location = {}});
    handler.effects.push_back(LocatedName{.spelling = "audio", .location = {}});
    handler.effects.push_back(LocatedName{.spelling = "audio", .location = {}});
    system.handlers.push_back(std::move(handler));
    prog.declarations.emplace_back(std::move(system));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog);

    CHECK(has_diagnostic(errors, "duplicate reads contract entry 'game.host.Position'"));
    CHECK(has_diagnostic(errors, "unknown writes contract entry 'MissingTrait'"));
    CHECK(has_diagnostic(errors, "unknown emits contract event 'missing_event'"));
    CHECK(has_diagnostic(errors, "spawn command target 'Actor' is not a template"));
    CHECK(has_diagnostic(errors, "add command requires a target"));
    CHECK(has_diagnostic(errors, "duplicate commands contract entry 'destroy'"));
    CHECK(has_diagnostic(errors, "invalid effect domain 'graphics..draw'"));
    CHECK(has_diagnostic(errors, "duplicate effects contract entry 'audio'"));
}

TEST_CASE("regular handler contracts infer canonical capabilities independently", "[semantic][handler-contract]") {
    const auto [decorated, diagnostics] = analyze_source(
        "module game.contracts\n"
        "extern event frame:\n"
        "    dt: float\n"
        "event Hit:\n"
        "    amount: int\n"
        "trait Position:\n"
        "    var x: float\n"
        "trait Velocity:\n"
        "    var speed: float\n"
        "trait Health:\n"
        "    var hp: int\n"
        "trait Flash:\n"
        "    var amount: int = 0\n"
        "trait Tag\n"
        "template Actor:\n"
        "    Position\n"
        "func twice(value: float) float:\n"
        "    return value + value\n"
        "system Movement:\n"
        "    filter:\n"
        "        Position as p\n"
        "        Velocity as v\n"
        "        Health as h\n"
        "    order by:\n"
        "        p.x asc\n"
        "    on frame as f:\n"
        "        if twice(v.speed) > f.dt:\n"
        "            hp -= 1\n"
        "        for item in [v.speed]:\n"
        "            let speed = item\n"
        "        emit Hit:\n"
        "            amount = hp\n"
        "        project Flash:\n"
        "            amount = hp\n"
        "        spawn Actor:\n"
        "            Position:\n"
        "                x = v.speed\n"
        "        spawn Actor:\n"
        "            Position:\n"
        "                x = v.speed\n"
        "        add Tag\n"
        "        remove Tag\n"
        "        destroy\n"
        "    on Hit:\n"
        "        let amount = Hit.amount\n"
        "system FilterOnly:\n"
        "    filter:\n"
        "        Position\n"
        "    on frame:\n"
        "        let local = frame.dt\n"
        "system ExcludeOnly:\n"
        "    exclude:\n"
        "        Tag\n"
        "    on frame:\n"
        "        let local = frame.dt\n"
        "system Once:\n"
        "    on frame:\n"
        "        let local = frame.dt\n");

    INFO((diagnostics.empty() ? "" : diagnostics.front().message));
    REQUIRE(diagnostics.empty());
    REQUIRE(decorated.handler_contracts.size() == 5);

    const auto contract_for = [&](const std::string& system,
                                  const std::string& trigger) -> const InferredHandlerContract& {
        const auto found = std::ranges::find_if(decorated.handler_contracts, [&](const auto& contract) {
            return contract.system.local_name == system && contract.trigger.symbol.local_name == trigger;
        });
        REQUIRE(found != decorated.handler_contracts.end());
        return *found;
    };
    const auto symbol = [](SymbolKind kind, const std::string& name) {
        return make_symbol_id(kind, "game.contracts", name);
    };

    const auto& movement = contract_for("Movement", "frame");
    CHECK(movement.reads.contains(symbol(SymbolKind::Trait, "Position")));
    CHECK(movement.reads.contains(symbol(SymbolKind::Trait, "Velocity")));
    CHECK(movement.reads.contains(symbol(SymbolKind::Trait, "Health")));
    CHECK(movement.writes.contains(symbol(SymbolKind::Trait, "Health")));
    CHECK(movement.reads.contains(symbol(SymbolKind::Trait, "Flash")));
    CHECK(movement.writes.contains(symbol(SymbolKind::Trait, "Flash")));
    CHECK(movement.emits.contains(symbol(SymbolKind::Event, "Hit")));
    CHECK(std::ranges::count(movement.commands,
                             InferredHandlerCommand{.kind   = HandlerCommandKind::Spawn,
                                                    .target = symbol(SymbolKind::Template, "Actor")}) == 1);
    CHECK(std::ranges::find(
              movement.commands,
              InferredHandlerCommand{.kind = HandlerCommandKind::Add, .target = symbol(SymbolKind::Trait, "Tag")}) !=
          movement.commands.end());
    CHECK(std::ranges::find(
              movement.commands,
              InferredHandlerCommand{.kind = HandlerCommandKind::Remove, .target = symbol(SymbolKind::Trait, "Tag")}) !=
          movement.commands.end());
    CHECK(std::ranges::find(movement.commands,
                            InferredHandlerCommand{.kind = HandlerCommandKind::Destroy, .target = std::nullopt}) !=
          movement.commands.end());

    const auto& hit = contract_for("Movement", "Hit");
    CHECK(hit.reads == std::unordered_set<SymbolId>{symbol(SymbolKind::Trait, "Position")});
    CHECK_FALSE(hit.reads.contains(symbol(SymbolKind::Trait, "Velocity")));
    CHECK_FALSE(hit.reads.contains(symbol(SymbolKind::Trait, "Health")));
    CHECK(hit.writes.empty());
    CHECK(hit.emits.empty());
    CHECK(hit.commands.empty());

    const auto& filter_only = contract_for("FilterOnly", "frame");
    CHECK_FALSE(filter_only.reads.contains(symbol(SymbolKind::Trait, "Position")));
    CHECK_FALSE(filter_only.is_selectionless);
    CHECK_FALSE(contract_for("ExcludeOnly", "frame").is_selectionless);
    CHECK(contract_for("Once", "frame").is_selectionless);
}

TEST_CASE("regular handler contracts infer extern function effect summaries", "[semantic][handler-contract][effects]") {
    auto imported_func = [](const std::string& name,
                            std::optional<std::unordered_set<std::string>> effects = std::nullopt) {
        ResolvedFunc function;
        function.name           = name;
        function.is_pub         = true;
        function.is_extern      = true;
        function.effect_summary = std::move(effects);
        return function;
    };

    ImportedSymbols math;
    math.module_name = "std.math";
    math.funcs.emplace("abs", imported_func("abs"));
    ImportedSymbols input;
    input.module_name = "std.input";
    input.funcs.emplace("mouse_position", imported_func("mouse_position"));
    ImportedSymbols audio;
    audio.module_name = "engine.audio";
    audio.funcs.emplace("play", imported_func("play", std::unordered_set<std::string>{"audio"}));
    ImportedSymbols unknown;
    unknown.module_name = "vendor.host";
    unknown.funcs.emplace("poll", imported_func("poll"));

    ModuleImports imports;
    imports.add("math", std::move(math));
    imports.add("input", std::move(input));
    imports.add("audio", std::move(audio));
    imports.add("host", std::move(unknown));

    const auto [decorated, diagnostics] = analyze_source(
        "module game.effects\n"
        "event pulse\n"
        "event pure_pulse\n"
        "extern func local_host()\n"
        "func identity(value: float) float:\n"
        "    return value\n"
        "system Effectful:\n"
        "    on pulse:\n"
        "        let cursor = input.mouse_position()\n"
        "        audio.play()\n"
        "        host.poll()\n"
        "        local_host()\n"
        "system Pure:\n"
        "    on pure_pulse:\n"
        "        let value = identity(math.abs(1.0))\n",
        imports);

    INFO((diagnostics.empty() ? "" : diagnostics.front().message));
    REQUIRE(diagnostics.empty());
    REQUIRE(decorated.handler_contracts.size() == 2);
    const auto contract_for = [&](const std::string& system) -> const InferredHandlerContract& {
        const auto found = std::ranges::find_if(
            decorated.handler_contracts, [&](const auto& contract) { return contract.system.local_name == system; });
        REQUIRE(found != decorated.handler_contracts.end());
        return *found;
    };

    CHECK(contract_for("Effectful").effects == std::unordered_set<std::string>{"input", "audio", "external"});
    CHECK(contract_for("Pure").effects.empty());
    REQUIRE(decorated.funcs.at("identity").effect_summary.has_value());
    CHECK(decorated.funcs.at("identity").effect_summary->empty());
    CHECK_FALSE(decorated.funcs.at("local_host").effect_summary.has_value());
}

TEST_CASE("handler graph expands system shorthand only across matching canonical triggers",
          "[semantic][handler-graph][3.2]") {
    const auto [decorated, diagnostics] = analyze_source(
        "module game.graph\n"
        "event tick\n"
        "event Damaged\n"
        "system Producer:\n"
        "    on tick:\n"
        "        let value = 1\n"
        "    on Damaged:\n"
        "        let value = 2\n"
        "system Consumer:\n"
        "    after:\n"
        "        Producer\n"
        "    on tick:\n"
        "        let value = 3\n"
        "    on Damaged:\n"
        "        let value = 4\n"
        "system Finalizer:\n"
        "    on tick:\n"
        "        after:\n"
        "            Consumer/on tick\n"
        "        let value = 5\n"
        "extern system HostObserver:\n"
        "    on tick:\n"
        "        effects:\n"
        "            host.observe\n");

    INFO((diagnostics.empty() ? "" : diagnostics.front().message));
    REQUIRE(diagnostics.empty());
    REQUIRE(decorated.execution_graph.handlers.size() == 6);
    REQUIRE(decorated.execution_graph.schedule_edges.size() == 3);

    const auto handler = [](const std::string& system, const std::string& trigger) {
        return HandlerIdentity{
            .system  = make_symbol_id(SymbolKind::System, "game.graph", system),
            .trigger = ResolvedHandlerTrigger{.kind   = HandlerTriggerKind::Event,
                                              .symbol = make_symbol_id(SymbolKind::Event, "game.graph", trigger)}};
    };
    const auto producer_tick      = handler("Producer", "tick");
    const auto producer_damaged   = handler("Producer", "Damaged");
    const auto consumer_tick      = handler("Consumer", "tick");
    const auto consumer_damaged   = handler("Consumer", "Damaged");
    const auto finalizer_tick     = handler("Finalizer", "tick");
    const auto host_observer_tick = handler("HostObserver", "tick");

    const auto has_edge = [&](const HandlerIdentity& before, const HandlerIdentity& after, ScheduleEdgeKind kind) {
        return std::ranges::any_of(decorated.execution_graph.schedule_edges, [&](const auto& edge) {
            return edge.before == before && edge.after == after && edge.kind == kind &&
                   edge.orientation == ScheduleEdgeOrientation::Explicit;
        });
    };
    CHECK(has_edge(producer_tick, consumer_tick, ScheduleEdgeKind::ExplicitSystem));
    CHECK(has_edge(producer_damaged, consumer_damaged, ScheduleEdgeKind::ExplicitSystem));
    CHECK(has_edge(consumer_tick, finalizer_tick, ScheduleEdgeKind::ExplicitHandler));
    CHECK_FALSE(has_edge(producer_tick, consumer_damaged, ScheduleEdgeKind::ExplicitSystem));
    CHECK_FALSE(has_edge(producer_damaged, consumer_tick, ScheduleEdgeKind::ExplicitSystem));

    const auto node_for = [&](const HandlerIdentity& identity) -> const HandlerNode& {
        const auto found = std::ranges::find_if(decorated.execution_graph.handlers,
                                                [&](const auto& node) { return node.identity == identity; });
        REQUIRE(found != decorated.execution_graph.handlers.end());
        return *found;
    };
    CHECK(node_for(consumer_tick).explicit_after == std::vector<HandlerIdentity>{producer_tick});
    CHECK(node_for(consumer_damaged).explicit_after == std::vector<HandlerIdentity>{producer_damaged});
    CHECK(node_for(finalizer_tick).explicit_after == std::vector<HandlerIdentity>{consumer_tick});
    CHECK(node_for(host_observer_tick).implementation == HandlerImplementationKind::External);
    CHECK(node_for(producer_tick).implementation == HandlerImplementationKind::Cactus);

    CHECK(decorated.execution_graph.stable_topological_order ==
          std::vector<HandlerIdentity>{
              producer_tick, host_observer_tick, consumer_tick, finalizer_tick, producer_damaged, consumer_damaged});
    REQUIRE(decorated.execution_graph.dependency_levels.size() == 5);
    CHECK(decorated.execution_graph.dependency_levels[0].activation == producer_tick.trigger);
    CHECK(decorated.execution_graph.dependency_levels[0].index == 0);
    CHECK(decorated.execution_graph.dependency_levels[0].handlers ==
          std::vector<HandlerIdentity>{producer_tick, host_observer_tick});
    CHECK(decorated.execution_graph.dependency_levels[1].activation == producer_tick.trigger);
    CHECK(decorated.execution_graph.dependency_levels[1].index == 1);
    CHECK(decorated.execution_graph.dependency_levels[1].handlers == std::vector<HandlerIdentity>{consumer_tick});
    CHECK(decorated.execution_graph.dependency_levels[2].activation == producer_tick.trigger);
    CHECK(decorated.execution_graph.dependency_levels[2].index == 2);
    CHECK(decorated.execution_graph.dependency_levels[2].handlers == std::vector<HandlerIdentity>{finalizer_tick});
    CHECK(decorated.execution_graph.dependency_levels[3].activation == producer_damaged.trigger);
    CHECK(decorated.execution_graph.dependency_levels[3].index == 0);
    CHECK(decorated.execution_graph.dependency_levels[3].handlers == std::vector<HandlerIdentity>{producer_damaged});
    CHECK(decorated.execution_graph.dependency_levels[4].activation == producer_damaged.trigger);
    CHECK(decorated.execution_graph.dependency_levels[4].index == 1);
    CHECK(decorated.execution_graph.dependency_levels[4].handlers == std::vector<HandlerIdentity>{consumer_damaged});
}

TEST_CASE("handler graph rejects ineligible exact handler ordering", "[semantic][handler-graph][3.2][errors]") {
    const auto [decorated, diagnostics] = analyze_source(
        "module game.graph\n"
        "event tick\n"
        "event Damaged\n"
        "system Producer:\n"
        "    on Damaged:\n"
        "        let value = 1\n"
        "system Consumer:\n"
        "    on tick:\n"
        "        after:\n"
        "            Producer/on Damaged\n"
        "        let value = 2\n");

    CHECK(has_diagnostic(diagnostics, "resolved triggers differ"));
    CHECK(decorated.execution_graph.schedule_edges.empty());
}

TEST_CASE("handler graph diagnoses cycles formed by explicit and contract edges",
          "[semantic][handler-graph][3.5][errors]") {
    const auto [decorated, diagnostics] = analyze_source(
        "module game.cycle\n"
        "event tick\n"
        "trait Forward\n"
        "trait LoopBack\n"
        "extern system A:\n"
        "    on tick:\n"
        "        reads:\n"
        "            LoopBack\n"
        "extern system B:\n"
        "    on tick:\n"
        "        after:\n"
        "            A/on tick\n"
        "        writes:\n"
        "            Forward\n"
        "extern system C:\n"
        "    on tick:\n"
        "        reads:\n"
        "            Forward\n"
        "        writes:\n"
        "            LoopBack\n");

    CHECK(has_diagnostic(diagnostics,
                         "handler cycle: game.cycle.A/on game.cycle.tick -> game.cycle.B/on game.cycle.tick -> "
                         "game.cycle.C/on game.cycle.tick -> game.cycle.A/on game.cycle.tick"));
    CHECK(decorated.execution_graph.schedule_edges.size() == 3);
    CHECK(decorated.execution_graph.stable_topological_order.empty());
    CHECK(decorated.execution_graph.dependency_levels.empty());
}

TEST_CASE("handler graph orients data and effect conflicts with deterministic provenance",
          "[semantic][handler-graph][3.3]") {
    const auto [decorated, diagnostics] = analyze_source(
        "module game.conflicts\n"
        "event tick\n"
        "event other\n"
        "trait OneWay\n"
        "trait ReadOnly\n"
        "trait WriteWrite\n"
        "trait ExplicitTrait\n"
        "trait Mixed\n"
        "trait FilterOnly\n"
        "extern system ReaderOne:\n"
        "    on tick:\n"
        "        reads:\n"
        "            OneWay\n"
        "extern system WriterOne:\n"
        "    on tick:\n"
        "        writes:\n"
        "            OneWay\n"
        "extern system ReadFirst:\n"
        "    on tick:\n"
        "        reads:\n"
        "            ReadOnly\n"
        "extern system ReadSecond:\n"
        "    on tick:\n"
        "        reads:\n"
        "            ReadOnly\n"
        "extern system WriteFirst:\n"
        "    on tick:\n"
        "        writes:\n"
        "            WriteWrite\n"
        "extern system WriteSecond:\n"
        "    on tick:\n"
        "        writes:\n"
        "            WriteWrite\n"
        "extern system EffectFirst:\n"
        "    on tick:\n"
        "        effects:\n"
        "            graphics\n"
        "extern system EffectSecond:\n"
        "    on tick:\n"
        "        effects:\n"
        "            graphics\n"
        "extern system ExplicitReader:\n"
        "    on tick:\n"
        "        reads:\n"
        "            ExplicitTrait\n"
        "extern system ExplicitWriter:\n"
        "    on tick:\n"
        "        after:\n"
        "            ExplicitReader/on tick\n"
        "        writes:\n"
        "            ExplicitTrait\n"
        "extern system MixedReader:\n"
        "    on tick:\n"
        "        reads:\n"
        "            Mixed\n"
        "        effects:\n"
        "            network\n"
        "extern system MixedWriter:\n"
        "    on tick:\n"
        "        writes:\n"
        "            Mixed\n"
        "        effects:\n"
        "            network\n"
        "extern system FilterPass:\n"
        "    filter:\n"
        "        FilterOnly\n"
        "    on tick:\n"
        "        effects:\n"
        "            unique.filter\n"
        "extern system FilterWriter:\n"
        "    on tick:\n"
        "        writes:\n"
        "            FilterOnly\n"
        "extern system OtherWriter:\n"
        "    on other:\n"
        "        writes:\n"
        "            OneWay\n"
        "        effects:\n"
        "            graphics\n");

    INFO((diagnostics.empty() ? "" : diagnostics.front().message));
    REQUIRE(diagnostics.empty());

    const auto handler = [](const std::string& system, const std::string& trigger = "tick") {
        return HandlerIdentity{
            .system  = make_symbol_id(SymbolKind::System, "game.conflicts", system),
            .trigger = ResolvedHandlerTrigger{.kind   = HandlerTriggerKind::Event,
                                              .symbol = make_symbol_id(SymbolKind::Event, "game.conflicts", trigger)}};
    };
    const auto edges_between = [&](const std::string& first, const std::string& second) {
        const auto first_id  = handler(first);
        const auto second_id = handler(second);
        std::vector<const ScheduleEdge*> edges;
        for (const auto& edge : decorated.execution_graph.schedule_edges) {
            if ((edge.before == first_id && edge.after == second_id) ||
                (edge.before == second_id && edge.after == first_id)) {
                edges.push_back(&edge);
            }
        }
        return edges;
    };
    const auto conflict_edge =
        [&](const std::string& first, const std::string& second, ScheduleEdgeKind kind) -> const ScheduleEdge& {
        const auto edges = edges_between(first, second);
        const auto found = std::ranges::find_if(edges, [&](const auto* edge) { return edge->kind == kind; });
        REQUIRE(found != edges.end());
        return **found;
    };

    const auto& one_way = conflict_edge("ReaderOne", "WriterOne", ScheduleEdgeKind::DataConflict);
    CHECK(one_way.before == handler("WriterOne"));
    CHECK(one_way.after == handler("ReaderOne"));
    CHECK(one_way.orientation == ScheduleEdgeOrientation::WriterBeforeReader);
    CHECK(one_way.trait_provenance ==
          std::vector<SymbolId>{make_symbol_id(SymbolKind::Trait, "game.conflicts", "OneWay")});
    CHECK(edges_between("ReadFirst", "ReadSecond").empty());

    const auto& write_write = conflict_edge("WriteFirst", "WriteSecond", ScheduleEdgeKind::DataConflict);
    CHECK(write_write.before == handler("WriteFirst"));
    CHECK(write_write.after == handler("WriteSecond"));
    CHECK(write_write.orientation == ScheduleEdgeOrientation::DeclarationOrder);

    const auto& effect = conflict_edge("EffectFirst", "EffectSecond", ScheduleEdgeKind::EffectConflict);
    CHECK(effect.before == handler("EffectFirst"));
    CHECK(effect.after == handler("EffectSecond"));
    CHECK(effect.orientation == ScheduleEdgeOrientation::DeclarationOrder);
    CHECK(effect.effect_provenance == std::vector<std::string>{"graphics"});

    const auto& explicit_data = conflict_edge("ExplicitReader", "ExplicitWriter", ScheduleEdgeKind::DataConflict);
    CHECK(explicit_data.before == handler("ExplicitReader"));
    CHECK(explicit_data.after == handler("ExplicitWriter"));
    CHECK(explicit_data.orientation == ScheduleEdgeOrientation::Explicit);

    const auto& mixed_data   = conflict_edge("MixedReader", "MixedWriter", ScheduleEdgeKind::DataConflict);
    const auto& mixed_effect = conflict_edge("MixedReader", "MixedWriter", ScheduleEdgeKind::EffectConflict);
    CHECK(mixed_data.before == handler("MixedWriter"));
    CHECK(mixed_effect.before == handler("MixedWriter"));
    CHECK(mixed_data.orientation == ScheduleEdgeOrientation::WriterBeforeReader);
    CHECK(mixed_effect.orientation == ScheduleEdgeOrientation::WriterBeforeReader);
    CHECK(mixed_effect.effect_provenance == std::vector<std::string>{"network"});

    CHECK(edges_between("FilterPass", "FilterWriter").empty());
    const auto other_writer = handler("OtherWriter", "other");
    CHECK(std::ranges::none_of(decorated.execution_graph.schedule_edges, [&](const auto& edge) {
        return edge.before == other_writer || edge.after == other_writer;
    }));
}

TEST_CASE("handler graph separates phase barriers from cyclic event flow", "[semantic][handler-graph][3.4]") {
    const auto [decorated, diagnostics] = analyze_source(
        "module game.activation\n"
        "extern event frame:\n"
        "    dt: float\n"
        "event A:\n"
        "    value: int\n"
        "event B:\n"
        "    value: int\n"
        "phase input:\n"
        "    from:\n"
        "        frame\n"
        "phase fixed_tick:\n"
        "    after:\n"
        "        input\n"
        "    every: 0.5\n"
        "phase tick:\n"
        "    after:\n"
        "        fixed_tick\n"
        "system FixedFirst:\n"
        "    on fixed_tick:\n"
        "        let value = fixed_tick.dt\n"
        "system FixedSecond:\n"
        "    on fixed_tick:\n"
        "        let value = fixed_tick.dt\n"
        "system TickConsumer:\n"
        "    on tick:\n"
        "        let value = 1\n"
        "system FeedbackA:\n"
        "    on A:\n"
        "        emit B:\n"
        "            value = A.value\n"
        "system FeedbackB:\n"
        "    on B:\n"
        "        emit A:\n"
        "            value = B.value\n");

    INFO((diagnostics.empty() ? "" : diagnostics.front().message));
    REQUIRE(diagnostics.empty());

    const auto handler = [](const std::string& system, HandlerTriggerKind kind, const std::string& trigger) {
        return HandlerIdentity{
            .system  = make_symbol_id(SymbolKind::System, "game.activation", system),
            .trigger = ResolvedHandlerTrigger{
                .kind   = kind,
                .symbol = make_symbol_id(kind == HandlerTriggerKind::Phase ? SymbolKind::Phase : SymbolKind::Event,
                                         "game.activation",
                                         trigger)}};
    };
    const auto fixed_first  = handler("FixedFirst", HandlerTriggerKind::Phase, "fixed_tick");
    const auto fixed_second = handler("FixedSecond", HandlerTriggerKind::Phase, "fixed_tick");
    const auto tick         = handler("TickConsumer", HandlerTriggerKind::Phase, "tick");
    const auto feedback_a   = handler("FeedbackA", HandlerTriggerKind::Event, "A");
    const auto feedback_b   = handler("FeedbackB", HandlerTriggerKind::Event, "B");
    const auto input_phase  = make_symbol_id(SymbolKind::Phase, "game.activation", "input");
    const auto fixed_phase  = make_symbol_id(SymbolKind::Phase, "game.activation", "fixed_tick");

    REQUIRE(decorated.execution_graph.phase_barriers.size() == 3);
    const auto has_barrier = [&](const SymbolId& upstream, const HandlerIdentity& downstream) {
        return std::ranges::any_of(decorated.execution_graph.phase_barriers, [&](const auto& edge) {
            return edge.upstream_phase == upstream && edge.downstream_handler == downstream;
        });
    };
    CHECK(has_barrier(input_phase, fixed_first));
    CHECK(has_barrier(input_phase, fixed_second));
    CHECK(has_barrier(fixed_phase, tick));

    REQUIRE(decorated.execution_graph.event_flows.size() == 2);
    const auto has_flow =
        [&](const HandlerIdentity& producer, const std::string& event, const HandlerIdentity& consumer) {
            return std::ranges::any_of(decorated.execution_graph.event_flows, [&](const auto& edge) {
                return edge.producer == producer &&
                       edge.event == make_symbol_id(SymbolKind::Event, "game.activation", event) &&
                       edge.consumer == consumer;
            });
        };
    CHECK(has_flow(feedback_a, "B", feedback_b));
    CHECK(has_flow(feedback_b, "A", feedback_a));
    CHECK(decorated.execution_graph.schedule_edges.empty());
    CHECK(decorated.execution_graph.stable_topological_order ==
          std::vector<HandlerIdentity>{fixed_first, fixed_second, tick, feedback_a, feedback_b});
    REQUIRE(decorated.execution_graph.dependency_levels.size() == 4);
    CHECK(decorated.execution_graph.dependency_levels[0].handlers ==
          std::vector<HandlerIdentity>{fixed_first, fixed_second});
    CHECK(decorated.execution_graph.dependency_levels[1].handlers == std::vector<HandlerIdentity>{tick});
    CHECK(decorated.execution_graph.dependency_levels[2].handlers == std::vector<HandlerIdentity>{feedback_a});
    CHECK(decorated.execution_graph.dependency_levels[3].handlers == std::vector<HandlerIdentity>{feedback_b});
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
