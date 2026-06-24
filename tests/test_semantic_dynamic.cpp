// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

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

// ── Test helpers ─────────────────────────────────────────────────────────────

static bool analyze_errors(const std::string& source) {
    // Prepend stdlib events so lifecycle handlers (on tick:, on spawn:, etc.) are declared
    const std::string FULL_SOURCE = STDLIB_EVENTS + source;
    ErrorReporter errors;
    Lexer lexer(FULL_SOURCE, "test.cactus", errors);
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

static std::string first_error(const std::string& source) {
    // Prepend stdlib events so lifecycle handlers (on tick:, on spawn:, etc.) are declared
    const std::string FULL_SOURCE = STDLIB_EVENTS + source;
    ErrorReporter errors;
    Lexer lexer(FULL_SOURCE, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    if (errors.has_errors()) {
        return errors.diagnostics()[0].message;
    }
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    if (errors.has_errors()) {
        return errors.diagnostics()[0].message;
    }
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program);
    return errors.has_errors() ? errors.diagnostics()[0].message : "";
}

static ProgramNode analyze_ast(const std::string& source) {
    const std::string FULL_SOURCE = STDLIB_EVENTS + source;
    ErrorReporter errors;
    Lexer lexer(FULL_SOURCE, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(program);
    REQUIRE_FALSE(errors.has_errors());
    return program;
}

static const TemplateNode& find_template(const ProgramNode& program, const std::string& name) {
    const TemplateNode* found = nullptr;
    for (const auto& decl : program.declarations) {
        const auto* tmpl = std::get_if<TemplateNode>(&decl);
        if (tmpl != nullptr && tmpl->name == name) {
            found = tmpl;
            break;
        }
    }
    REQUIRE(found != nullptr);
    return *found;
}

static const EntityNode& find_entity(const ProgramNode& program, const std::string& name) {
    const EntityNode* found = nullptr;
    for (const auto& decl : program.declarations) {
        const auto* entity = std::get_if<EntityNode>(&decl);
        if (entity != nullptr && entity->name == name) {
            found = entity;
            break;
        }
    }
    REQUIRE(found != nullptr);
    return *found;
}

static const ArchetypeTraitEntry& find_archetype_trait(const std::vector<ArchetypeTraitEntry>& traits,
                                                       const std::string& name) {
    const ArchetypeTraitEntry* found = nullptr;
    for (const auto& trait : traits) {
        if (trait.trait_name == name) {
            found = &trait;
            break;
        }
    }
    REQUIRE(found != nullptr);
    return *found;
}

static const FieldAssignment& find_assignment(const ArchetypeTraitEntry& trait, const std::string& name) {
    const FieldAssignment* found = nullptr;
    for (const auto& assignment : trait.assignments) {
        if (assignment.name == name) {
            found = &assignment;
            break;
        }
    }
    REQUIRE(found != nullptr);
    return *found;
}

static std::string literal_value(const FieldAssignment& assignment) {
    const auto* literal = std::get_if<LiteralExpr>(&assignment.value->expr);
    REQUIRE(literal != nullptr);
    return literal->value;
}

// ── Task 5.1: Template declaration validation ─────────────────────────────────

TEST_CASE("Semantic: template with declared traits — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template Enemy:\n"
                       "    Position\n"));
}

TEST_CASE("Semantic: template with undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("template Enemy:\n"
                       "    UnknownTrait\n"));
}

TEST_CASE("Semantic: template with invalid config field — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template Enemy:\n"
                       "    Position:\n"
                       "        badfield = 1.0\n"));
}

TEST_CASE("Semantic: template with valid config field — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template Enemy:\n"
                       "    Position:\n"
                       "        x = 5.0\n"));
}

// ── Task 5.2: Templates tracked separately from entities ──────────────────────

TEST_CASE("Semantic: entity with undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("entity Player:\n"
                       "    NonExistent\n"));
}

TEST_CASE("Semantic: entity with declared trait — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Health:\n"
                       "    var hp: int = 100\n"
                       "entity Player:\n"
                       "    Health\n"));
}

TEST_CASE("Semantic: entity config with unknown field — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Health:\n"
                       "    var hp: int = 100\n"
                       "entity Player:\n"
                       "    Health:\n"
                       "        badfield = 10\n"));
}

TEST_CASE("Semantic: archetype body use resolves local template", "[semantic][dynamic-ecs][template-composition]") {
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template EnemyBase:\n"
                       "    Position\n"
                       "template WalkerEnemy:\n"
                       "    use EnemyBase\n"));
}

TEST_CASE("Semantic: unit body use resolves local template", "[semantic][dynamic-ecs][template-composition]") {
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template EnemyBase:\n"
                       "    Position\n"
                       "entity Walker1:\n"
                       "    use EnemyBase\n"));
}

TEST_CASE("Semantic: archetype body use rejects local trait", "[semantic][dynamic-ecs][template-composition]") {
    auto err = first_error(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "template Bad:\n"
        "    use Health\n");
    CHECK(err.find("must reference a template") != std::string::npos);
    CHECK(err.find("not a template") != std::string::npos);
}

TEST_CASE("Semantic: archetype body use rejects local unit", "[semantic][dynamic-ecs][template-composition]") {
    auto err = first_error(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "entity Player:\n"
        "    Position\n"
        "template Bad:\n"
        "    use Player\n");
    CHECK(err.find("must reference a template") != std::string::npos);
    CHECK(err.find("not a template") != std::string::npos);
}

TEST_CASE("Semantic: archetype body use reports undefined template", "[semantic][dynamic-ecs][template-composition]") {
    auto err = first_error(
        "template Bad:\n"
        "    use MissingTemplate\n");
    CHECK(err.find("undefined template 'MissingTemplate'") != std::string::npos);
}

TEST_CASE("Semantic: archetype body use rejects self cycle", "[semantic][dynamic-ecs][template-composition]") {
    auto err = first_error(
        "template EnemyBase:\n"
        "    use EnemyBase\n");
    CHECK(err.find("cyclic template-use graph") != std::string::npos);
    CHECK(err.find("EnemyBase -> EnemyBase") != std::string::npos);
}

TEST_CASE("Semantic: archetype body use rejects indirect cycle", "[semantic][dynamic-ecs][template-composition]") {
    auto err = first_error(
        "template A:\n"
        "    use B\n"
        "template B:\n"
        "    use C\n"
        "template C:\n"
        "    use A\n");
    CHECK(err.find("cyclic template-use graph") != std::string::npos);
    CHECK(err.find("A -> B -> C -> A") != std::string::npos);
}

TEST_CASE("Semantic: template composition flattens merge order and field overrides",
          "[semantic][dynamic-ecs][template-composition]") {
    auto program = analyze_ast(
        "trait Stats:\n"
        "    var hp: int = 0\n"
        "    var damage: int = 0\n"
        "    var armor: int = 0\n"
        "template EnemyBase:\n"
        "    Stats:\n"
        "        hp = 1\n"
        "        damage = 10\n"
        "        armor = 5\n"
        "template EnemyElite:\n"
        "    Stats:\n"
        "        hp = 2\n"
        "template BossEnemy:\n"
        "    use EnemyBase\n"
        "    use EnemyElite\n"
        "    Stats:\n"
        "        damage = 99\n");

    const auto& boss  = find_template(program, "BossEnemy");
    const auto& stats = find_archetype_trait(boss.traits, "Stats");

    REQUIRE(boss.traits.size() == 1);
    CHECK(literal_value(find_assignment(stats, "hp")) == "2");
    CHECK(literal_value(find_assignment(stats, "damage")) == "99");
    CHECK(literal_value(find_assignment(stats, "armor")) == "5");
}

TEST_CASE("Semantic: template composition collapses duplicate marker traits",
          "[semantic][dynamic-ecs][template-composition]") {
    auto program = analyze_ast(
        "trait Persistent\n"
        "trait Renderable:\n"
        "    var layer: int = 0\n"
        "template PersistentBase:\n"
        "    Persistent\n"
        "template VisiblePersistent:\n"
        "    Persistent\n"
        "    Renderable:\n"
        "        layer = 1\n"
        "entity Crate:\n"
        "    use PersistentBase\n"
        "    use VisiblePersistent\n"
        "    Persistent\n");

    const auto& crate = find_entity(program, "Crate");

    std::size_t persistent_count = 0;
    for (const auto& trait : crate.traits) {
        if (trait.trait_name == "Persistent") {
            ++persistent_count;
            CHECK(trait.assignments.empty());
        }
    }

    CHECK(persistent_count == 1);
    const auto& renderable = find_archetype_trait(crate.traits, "Renderable");
    CHECK(literal_value(find_assignment(renderable, "layer")) == "1");
}

// ── Task 5.3: Spawn site validation ─────────────────────────────────────────

TEST_CASE("Semantic: spawn valid template — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template Enemy:\n"
                       "    Position\n"
                       "system Spawner:\n"
                       "    on load:\n"
                       "        spawn Enemy:\n"
                       "            Position:\n"
                       "                x = 1.0\n"));
}

TEST_CASE("Semantic: spawn undefined template — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("system Spawner:\n"
                       "    on load:\n"
                       "        spawn Ghost:\n"
                       "            Position:\n"
                       "                x = 0.0\n"));
}

TEST_CASE("Semantic: spawn with unknown override field — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template Enemy:\n"
                       "    Position\n"
                       "system Spawner:\n"
                       "    on load:\n"
                       "        spawn Enemy:\n"
                       "            Position:\n"
                       "                badfield = 1.0\n"));
}

// ── Task 5.3: Spawn required field check ────────────────────────────────────

TEST_CASE("Semantic: spawn with required field provided — ok", "[semantic][dynamic-ecs]") {
    // 'speed' is var with no default → required at spawn
    CHECK_FALSE(
        analyze_errors("trait Movement:\n"
                       "    var speed: float\n"
                       "template Bullet:\n"
                       "    Movement\n"
                       "system Spawner:\n"
                       "    on load:\n"
                       "        spawn Bullet:\n"
                       "            Movement:\n"
                       "                speed = 5.0\n"));
}

TEST_CASE("Semantic: spawn with required field missing — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Movement:\n"
                       "    var speed: float\n"
                       "template Bullet:\n"
                       "    Movement\n"
                       "system Spawner:\n"
                       "    on load:\n"
                       "        spawn Bullet:\n"
                       "            Movement\n"));
}

TEST_CASE("Semantic: spawn with required field provided by template config — ok", "[semantic][dynamic-ecs]") {
    // speed provided in template config — not required at spawn
    CHECK_FALSE(
        analyze_errors("trait Movement:\n"
                       "    var speed: float\n"
                       "template Bullet:\n"
                       "    Movement:\n"
                       "        speed = 10.0\n"
                       "system Spawner:\n"
                       "    on load:\n"
                       "        spawn Bullet:\n"
                       "            Movement\n"));
}

// ── Task 5.4: Spawn does not target an entity ───────────────────────────────

TEST_CASE("Semantic: spawn of entity — error", "[semantic][dynamic-ecs]") {
    auto err = first_error(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "entity Player:\n"
        "    Position\n"
        "system Spawner:\n"
        "    on load:\n"
        "        spawn Player:\n"
        "            Position:\n"
        "                x = 0.0\n");
    CHECK(err.find("is an entity, not a template") != std::string::npos);
}

// ── Task 5.5: spawn/destroy/load/add/remove only in system handlers ──────

TEST_CASE("Semantic: spawn in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template Enemy:\n"
                       "    Position\n"
                       "func bad():\n"
                       "    spawn Enemy:\n"
                       "        Position:\n"
                       "            x = 1.0\n"));
}

TEST_CASE("Semantic: destroy in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("func bad():\n"
                       "    destroy\n"));
}

TEST_CASE("Semantic: load in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("use levels\n"
                       "func bad():\n"
                       "    load levels.main\n"));
}

TEST_CASE("Semantic: add in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Frozen\n"
                       "func bad():\n"
                       "    add Frozen\n"));
}

TEST_CASE("Semantic: remove in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Frozen\n"
                       "func bad():\n"
                       "    remove Frozen\n"));
}

// ── Task 5.6: load module reachability ──────────────────────────────────────

TEST_CASE("Semantic: load reachable via use — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("use levels\n"
                       "trait GameState:\n"
                       "    var active: bool = true\n"
                       "system GameMgr:\n"
                       "    filter:\n"
                       "        GameState\n"
                       "    on tick:\n"
                       "        load levels.main\n"));
}

TEST_CASE("Semantic: load unreachable module — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait GameState:\n"
                       "    var active: bool = true\n"
                       "system GameMgr:\n"
                       "    filter:\n"
                       "        GameState\n"
                       "    on tick:\n"
                       "        load unknown.scene\n"));
}

// ── Task 5.7: add/remove trait validation ────────────────────────────────

TEST_CASE("Semantic: add declared trait — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Frozen\n"
                       "trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system FreezeSystem:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on tick:\n"
                       "        add Frozen\n"));
}

TEST_CASE("Semantic: add undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system FreezeSystem:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on tick:\n"
                       "        add NonExistentTrait\n"));
}

TEST_CASE("Semantic: remove declared trait — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Frozen\n"
                       "trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system FreezeSystem:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on tick:\n"
                       "        remove Frozen\n"));
}

TEST_CASE("Semantic: remove undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system FreezeSystem:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on tick:\n"
                       "        remove BadTrait\n"));
}

// ── Task 5.8: Lifecycle handler empty params ─────────────────────────────────

TEST_CASE("Semantic: on spawn no params — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system Init:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on spawn:\n"
                       "        x = 0.0\n"));
}

TEST_CASE("Semantic: on spawn with params — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system Init:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on spawn(v: float):\n"
                       "        x = v\n"));
}

TEST_CASE("Semantic: on destroy no params — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system Cleanup:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on destroy:\n"
                       "        x = 0.0\n"));
}

TEST_CASE("Semantic: on load with params — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait GameState:\n"
                       "    var active: bool = true\n"
                       "system GameMgr:\n"
                       "    filter:\n"
                       "        GameState\n"
                       "    on load(name: float):\n"
                       "        active = true\n"));
}

TEST_CASE("Semantic: on unload with params — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait GameState:\n"
                       "    var active: bool = true\n"
                       "system GameMgr:\n"
                       "    filter:\n"
                       "        GameState\n"
                       "    on unload(dt: float):\n"
                       "        active = false\n"));
}

// ── Task 5.9: Exclude clause trait validation ────────────────────────────────

TEST_CASE("Semantic: exclude declared trait — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Persistent\n"
                       "system Cleanup:\n"
                       "    exclude:\n"
                       "        Persistent\n"
                       "    on unload:\n"
                       "        destroy\n"));
}

TEST_CASE("Semantic: exclude undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("system Cleanup:\n"
                       "    exclude:\n"
                       "        NonExistentTrait\n"
                       "    on unload:\n"
                       "        destroy\n"));
}

// ── Task 5.10: Optional filter — no filter matches all ──────────────────────

TEST_CASE("Semantic: system with no filter — valid match-all", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("system Cleanup:\n"
                       "    on tick:\n"
                       "        destroy\n"));
}

TEST_CASE("Semantic: system with exclude only (no filter) — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Persistent\n"
                       "system Cleanup:\n"
                       "    exclude:\n"
                       "        Persistent\n"
                       "    on unload:\n"
                       "        destroy\n"));
}

TEST_CASE("Semantic: add trait with required fields — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Health:\n"
                       "    var current: int\n"
                       "    var max: int\n"
                       "system FreezeSystem:\n"
                       "    on tick:\n"
                       "        add Health:\n"
                       "            current = 10\n"
                       "            max = 20\n"));
}

TEST_CASE("Semantic: add trait with missing required field — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Health:\n"
                       "    var current: int\n"
                       "    var max: int\n"
                       "system FreezeSystem:\n"
                       "    on tick:\n"
                       "        add Health:\n"
                       "            current = 10\n"));
}

TEST_CASE("Semantic: add/remove cross-entity target must be entity_id", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Frozen\n"
                       "system FreezeSystem:\n"
                       "    on tick:\n"
                       "        add Frozen to 42\n"));
    CHECK(
        analyze_errors("trait Frozen\n"
                       "system FreezeSystem:\n"
                       "    on tick:\n"
                       "        remove Frozen from 42\n"));
}

TEST_CASE("Semantic: trait default value type mismatch — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Frozen:\n"
                       "    var duration: int = 3.14\n"));
}

TEST_CASE("Semantic: trait default value must be constant — error", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Frozen:\n"
                       "    var duration: int = other\n"));
}

// ── Lifecycle events accepted in event handler validation ───────────────────

TEST_CASE("Semantic: lifecycle events not treated as unknown events", "[semantic][dynamic-ecs]") {
    // spawn, destroy, load, unload handlers should not trigger 'unknown event' error
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system Sys:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on spawn:\n"
                       "        x = 0.0\n"
                       "    on destroy:\n"
                       "        x = 0.0\n"
                       "    on load:\n"
                       "        x = 1.0\n"
                       "    on unload:\n"
                       "        x = 0.0\n"));
}

// ── Marker trait accepted in apply/filter/exclude ───────────────────────────

TEST_CASE("Semantic: marker trait in apply is valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Persistent\n"
                       "entity Player:\n"
                       "    Persistent\n"));
}

TEST_CASE("Semantic: pub marker trait", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors("pub trait Persistent\n"));
}

TEST_CASE("Semantic: marker trait in exclude is valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Persistent\n"
                       "system Cleanup:\n"
                       "    exclude:\n"
                       "        Persistent\n"
                       "    on tick:\n"
                       "        destroy\n"));
}

// ── destroy in system handler is valid ─────────────────────────────────────

TEST_CASE("Semantic: destroy in system handler — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Health:\n"
                       "    var hp: int = 100\n"
                       "system DeathSystem:\n"
                       "    filter:\n"
                       "        Health\n"
                       "    on tick:\n"
                       "        destroy\n"));
}

// ── spawn in system handler is valid ────────────────────────────────────────

TEST_CASE("Semantic: spawn in on tick handler — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "template Enemy:\n"
                       "    Position\n"
                       "system Spawner:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on tick:\n"
                       "        spawn Enemy:\n"
                       "            Position:\n"
                       "                x = 0.0\n"));
}

// ── Task 11.10: Marker trait in apply/filter/exclude ────────────────────────

TEST_CASE("Semantic: marker trait in filter is valid (task 11.10)", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Persistent\n"
                       "system Sys:\n"
                       "    filter:\n"
                       "        Persistent\n"
                       "    on tick:\n"
                       "        destroy\n"));
}

TEST_CASE("Semantic: marker trait filter + exclude combination (task 11.10)", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(
        analyze_errors("trait Persistent\n"
                       "trait Active\n"
                       "system Sys:\n"
                       "    filter:\n"
                       "        Active\n"
                       "    exclude:\n"
                       "        Persistent\n"
                       "    on tick:\n"
                       "        destroy\n"));
}

// ── Task 11.12: Field access in no-filter system body is an error ────────────

TEST_CASE("Semantic: field access in no-filter system body — error (task 11.12)", "[semantic][dynamic-ecs]") {
    auto err = first_error(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system GlobalSystem:\n"
        "    on tick:\n"
        "        x = x + tick.dt\n");  // 'x' is a trait field, system has no filter
    CHECK(err.find("not accessible") != std::string::npos);
}

TEST_CASE("Semantic: non-field stmts allowed in no-filter system — ok (task 11.12)", "[semantic][dynamic-ecs]") {
    // destroy, emit, spawn etc. are allowed even without filter
    CHECK_FALSE(
        analyze_errors("system GlobalCleanup:\n"
                       "    on unload:\n"
                       "        destroy\n"));
}

TEST_CASE("Semantic: field access in filter system body — ok (task 11.12)", "[semantic][dynamic-ecs]") {
    // With filter, field access is fine
    CHECK_FALSE(
        analyze_errors("trait Position:\n"
                       "    var x: float = 0.0\n"
                       "system Move:\n"
                       "    filter:\n"
                       "        Position\n"
                       "    on tick:\n"
                       "        x = x + tick.dt\n"));
}

TEST_CASE("Semantic: field access in no-filter if-branch — error (task 11.12)", "[semantic][dynamic-ecs]") {
    CHECK(
        analyze_errors("trait Health:\n"
                       "       var hp: int = 100\n"
                       "system GlobalSys:\n"
                       "    on tick:\n"
                       "        if hp <= 0:\n"
                       "            hp = 100\n"));
}
// ── std.editor semantic tests (add-std-editor) ─────────────────────────────

TEST_CASE("Semantic: EditorState trait fields have correct types and defaults", "[semantic][stdlib][editor]") {
    auto prog = analyze_ast(
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "    Rotate\n"
        "    Scale\n"
        "    Place\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: int = 0\n"
        "    var selected: entity_id\n"
        "    var active_template: string = \"\"\n"
        "    var focused_trait: string = \"\"\n"
        "    var focused_field: string = \"\"\n");
    // Find the EditorState trait
    const TraitNode* editor_state = nullptr;
    for (const auto& decl : prog.declarations) {
        if (const auto* trait = std::get_if<TraitNode>(&decl)) {
            if (trait->name == "EditorState") {
                editor_state = trait;
                break;
            }
        }
    }
    REQUIRE(editor_state != nullptr);
    REQUIRE(editor_state->fields.size() == 6);
    CHECK(editor_state->fields[0].name == "active");
    CHECK(editor_state->fields[0].type.name == "bool");
    CHECK(editor_state->fields[1].name == "mode");
    CHECK(editor_state->fields[1].type.name == "int");
    CHECK(editor_state->fields[2].name == "selected");
    CHECK(editor_state->fields[2].type.name == "entity_id");
    CHECK(editor_state->fields[3].name == "active_template");
    CHECK(editor_state->fields[3].type.name == "string");
}

TEST_CASE("Semantic: Editor entity with EditorState trait is valid", "[semantic][stdlib][editor]") {
    CHECK_FALSE(
        analyze_errors("pub trait EditorState:\n"
                       "    var active: bool = true\n"
                       "    var mode: int = 0\n"
                       "    var selected: entity_id\n"
                       "pub entity Editor:\n"
                       "    EditorState\n"));
}

TEST_CASE("Semantic: EditorGizmo2D and EditorGizmo3D traits are valid for projection", "[semantic][stdlib][editor]") {
    // Projection traits must be valid (have fields, can be projected)
    auto prog = analyze_ast(
        "pub trait EditorGizmo2D:\n"
        "    var mode: int = 1\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "pub trait EditorGizmo3D:\n"
        "    var mode: int = 1\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "system Gizmo2D:\n"
        "    filter:\n"
        "        EditorGizmo2D\n"
        "    on tick:\n"
        "        let m = mode\n");
    // Find EditorGizmo2D trait
    const TraitNode* gizmo2d = nullptr;
    const TraitNode* gizmo3d = nullptr;
    for (const auto& decl : prog.declarations) {
        if (const auto* trait = std::get_if<TraitNode>(&decl)) {
            if (trait->name == "EditorGizmo2D")
                gizmo2d = trait;
            if (trait->name == "EditorGizmo3D")
                gizmo3d = trait;
        }
    }
    REQUIRE(gizmo2d != nullptr);
    REQUIRE(gizmo3d != nullptr);
    CHECK(gizmo2d->fields.size() == 3);
    CHECK(gizmo3d->fields.size() == 3);
}

TEST_CASE("Semantic: EditorSelectionChanged and EditorModeChanged events are valid", "[semantic][stdlib][editor]") {
    // Both events should pass semantic validation
    auto prog = analyze_ast(
        "pub event EditorSelectionChanged:\n"
        "    previous: entity_id\n"
        "    current: entity_id\n"
        "pub event EditorModeChanged:\n"
        "    previous_mode: int\n"
        "    current_mode: int\n"
        "system EventHandler:\n"
        "    on EditorSelectionChanged as e:\n"
        "        pass\n"
        "    on EditorModeChanged as e:\n"
        "        pass\n");
    const EventNode* sel_event  = nullptr;
    const EventNode* mode_event = nullptr;
    for (const auto& decl : prog.declarations) {
        if (const auto* event = std::get_if<EventNode>(&decl)) {
            if (event->name == "EditorSelectionChanged")
                sel_event = event;
            if (event->name == "EditorModeChanged")
                mode_event = event;
        }
    }
    REQUIRE(sel_event != nullptr);
    REQUIRE(mode_event != nullptr);
    CHECK(sel_event->fields.size() == 2);
    CHECK(sel_event->fields[0].name == "previous");
    CHECK(sel_event->fields[1].name == "current");
    CHECK(mode_event->fields.size() == 2);
    CHECK(mode_event->fields[0].name == "previous_mode");
    CHECK(mode_event->fields[1].name == "current_mode");
}

TEST_CASE("Semantic: editor_spawn_template extern func with string param passes validation",
          "[semantic][stdlib][editor][extern-func]") {
    // string param in extern func should NOT produce const-binding errors
    CHECK_FALSE(
        analyze_errors("pub extern func editor_spawn_template(template_name: string, position_2d: vec2, position_3d: "
                       "vec3) entity_id\n"));
}

TEST_CASE("Semantic: all seven editor extern funcs with string/int/vec params pass validation",
          "[semantic][stdlib][editor][extern-func]") {
    CHECK_FALSE(
        analyze_errors("pub extern func editor_spawn_template(template_name: string, position_2d: vec2, position_3d: "
                       "vec3) entity_id\n"
                       "pub extern func editor_hit_test_2d(screen_pos: vec2, mask: int) entity_id\n"
                       "pub extern func editor_raycast_3d(screen_pos: vec2, mask: int) entity_id\n"
                       "pub extern func editor_screen_to_world_2d(screen: vec2) vec2\n"
                       "pub extern func editor_mouse_delta_2d() vec2\n"
                       "pub extern func editor_plane_project_3d(screen: vec2, plane_origin: vec3, plane_normal: vec3) "
                       "vec3\n"
                       "pub extern func editor_mouse_delta_3d() vec3\n"));
}

TEST_CASE("Semantic: extern system EditorTemplatePalette filter on EditorState is valid",
          "[semantic][stdlib][editor][extern-system]") {
    CHECK_FALSE(
        analyze_errors("pub trait EditorState:\n"
                       "    var active: bool = true\n"
                       "    var mode: int = 0\n"
                       "    var selected: entity_id\n"
                       "pub extern system EditorTemplatePalette:\n"
                       "    filter:\n"
                       "        EditorState\n"));
}

TEST_CASE("Semantic: extern system EditorPropertyPanel filter on EditorState is valid",
          "[semantic][stdlib][editor][extern-system]") {
    CHECK_FALSE(
        analyze_errors("pub trait EditorState:\n"
                       "    var active: bool = true\n"
                       "    var mode: int = 0\n"
                       "pub extern system EditorPropertyPanel:\n"
                       "    filter:\n"
                       "        EditorState\n"));
}

TEST_CASE("Semantic: extern system GizmoRenderer2D filter on EditorGizmo2D is valid",
          "[semantic][stdlib][editor][extern-system]") {
    CHECK_FALSE(
        analyze_errors("pub trait EditorGizmo2D:\n"
                       "    var mode: int = 1\n"
                       "    var color: color = #00FF00FF\n"
                       "    var size: float = 1.0\n"
                       "pub extern system GizmoRenderer2D:\n"
                       "    filter:\n"
                       "        EditorGizmo2D\n"));
}

TEST_CASE("Semantic: extern system GizmoRenderer3D filter on EditorGizmo3D is valid",
          "[semantic][stdlib][editor][extern-system]") {
    CHECK_FALSE(
        analyze_errors("pub trait EditorGizmo3D:\n"
                       "    var mode: int = 1\n"
                       "    var color: color = #00FF00FF\n"
                       "    var size: float = 1.0\n"
                       "pub extern system GizmoRenderer3D:\n"
                       "    filter:\n"
                       "        EditorGizmo3D\n"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
