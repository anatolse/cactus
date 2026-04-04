#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "frontend/semantic_analyzer.h"

using namespace cactus;

// Standard lifecycle event declarations (normally from std.core imports)
static const std::string STDLIB_EVENTS =
    "pub event tick:\n"
    "    let dt: float\n"
    "pub event fixed_tick:\n"
    "    let dt: float\n"
    "pub event late_tick:\n"
    "    let dt: float\n"
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

// ── Task 5.1: Template declaration validation ─────────────────────────────────

TEST_CASE("Semantic: template with declared traits — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n"));
}

TEST_CASE("Semantic: template with undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "template Enemy:\n"
        "    UnknownTrait\n"));
}

TEST_CASE("Semantic: template with invalid config field — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position:\n"
        "        badfield = 1.0\n"));
}

TEST_CASE("Semantic: template with valid config field — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position:\n"
        "        x = 5.0\n"));
}

// ── Task 5.2: Templates tracked separately from units ─────────────────────────

TEST_CASE("Semantic: unit with undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "unit Player:\n"
        "    NonExistent\n"));
}

TEST_CASE("Semantic: unit with declared trait — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "unit Player:\n"
        "    Health\n"));
}

TEST_CASE("Semantic: unit config with unknown field — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "unit Player:\n"
        "    Health:\n"
        "        badfield = 10\n"));
}

// ── Task 5.3: Spawn site validation ─────────────────────────────────────────

TEST_CASE("Semantic: spawn valid template — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Position:\n"
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
    CHECK(analyze_errors(
        "system Spawner:\n"
        "    on load:\n"
        "        spawn Ghost:\n"
        "            Position:\n"
        "                x = 0.0\n"));
}

TEST_CASE("Semantic: spawn with unknown override field — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Position:\n"
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
    CHECK_FALSE(analyze_errors(
        "trait Movement:\n"
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
    CHECK(analyze_errors(
        "trait Movement:\n"
        "    var speed: float\n"
        "template Bullet:\n"
        "    Movement\n"
        "system Spawner:\n"
        "    on load:\n"
        "        spawn Bullet:\n"
        "            Movement\n"));
}

TEST_CASE("Semantic: spawn with required field provided by template config — ok",
          "[semantic][dynamic-ecs]") {
    // speed provided in template config — not required at spawn
    CHECK_FALSE(analyze_errors(
        "trait Movement:\n"
        "    var speed: float\n"
        "template Bullet:\n"
        "    Movement:\n"
        "        speed = 10.0\n"
        "system Spawner:\n"
        "    on load:\n"
        "        spawn Bullet:\n"
        "            Movement\n"));
}

// ── Task 5.4: Spawn does not target a unit ──────────────────────────────────

TEST_CASE("Semantic: spawn of unit — error", "[semantic][dynamic-ecs]") {
    auto err = first_error(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "unit Player:\n"
        "    Position\n"
        "system Spawner:\n"
        "    on load:\n"
        "        spawn Player:\n"
        "            Position:\n"
        "                x = 0.0\n");
    CHECK(err.find("is a unit, not a template") != std::string::npos);
}

// ── Task 5.5: spawn/destroy/load/add/remove only in system handlers ──────

TEST_CASE("Semantic: spawn in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n"
        "func bad():\n"
        "    spawn Enemy:\n"
        "        Position:\n"
        "            x = 1.0\n"));
}

TEST_CASE("Semantic: destroy in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "func bad():\n"
        "    destroy\n"));
}

TEST_CASE("Semantic: load in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "use levels\n"
        "func bad():\n"
        "    load levels.main\n"));
}

TEST_CASE("Semantic: add in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Frozen\n"
        "func bad():\n"
        "    add Frozen\n"));
}

TEST_CASE("Semantic: remove in func body — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Frozen\n"
        "func bad():\n"
        "    remove Frozen\n"));
}

// ── Task 5.6: load module reachability ──────────────────────────────────────

TEST_CASE("Semantic: load reachable via use — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "use levels\n"
        "trait GameState:\n"
        "    var active: bool = true\n"
        "system GameMgr:\n"
        "    filter:\n"
        "        GameState\n"
        "    on tick:\n"
        "        load levels.main\n"));
}

TEST_CASE("Semantic: load unreachable module — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait GameState:\n"
        "    var active: bool = true\n"
        "system GameMgr:\n"
        "    filter:\n"
        "        GameState\n"
        "    on tick:\n"
        "        load unknown.scene\n"));
}

// ── Task 5.7: add/remove trait validation ────────────────────────────────

TEST_CASE("Semantic: add declared trait — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Frozen\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen\n"));
}

TEST_CASE("Semantic: add undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add NonExistentTrait\n"));
}

TEST_CASE("Semantic: remove declared trait — ok", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Frozen\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        remove Frozen\n"));
}

TEST_CASE("Semantic: remove undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        remove BadTrait\n"));
}

// ── Task 5.8: Lifecycle handler empty params ─────────────────────────────────

TEST_CASE("Semantic: on spawn no params — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Init:\n"
        "    filter:\n"
        "        Position\n"
        "    on spawn:\n"
        "        x = 0.0\n"));
}

TEST_CASE("Semantic: on spawn with params — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Init:\n"
        "    filter:\n"
        "        Position\n"
        "    on spawn(v: float):\n"
        "        x = v\n"));
}

TEST_CASE("Semantic: on destroy no params — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Cleanup:\n"
        "    filter:\n"
        "        Position\n"
        "    on destroy:\n"
        "        x = 0.0\n"));
}

TEST_CASE("Semantic: on load with params — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait GameState:\n"
        "    var active: bool = true\n"
        "system GameMgr:\n"
        "    filter:\n"
        "        GameState\n"
        "    on load(name: float):\n"
        "        active = true\n"));
}

TEST_CASE("Semantic: on unload with params — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait GameState:\n"
        "    var active: bool = true\n"
        "system GameMgr:\n"
        "    filter:\n"
        "        GameState\n"
        "    on unload(dt: float):\n"
        "        active = false\n"));
}

// ── Task 5.9: Exclude clause trait validation ────────────────────────────────

TEST_CASE("Semantic: exclude declared trait — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Persistent\n"
        "system Cleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n"));
}

TEST_CASE("Semantic: exclude undeclared trait — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "system Cleanup:\n"
        "    exclude:\n"
        "        NonExistentTrait\n"
        "    on unload:\n"
        "        destroy\n"));
}

// ── Task 5.10: Optional filter — no filter matches all ──────────────────────

TEST_CASE("Semantic: system with no filter — valid match-all", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "system Cleanup:\n"
        "    on tick:\n"
        "        destroy\n"));
}

TEST_CASE("Semantic: system with exclude only (no filter) — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Persistent\n"
        "system Cleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n"));
}

TEST_CASE("Semantic: add trait with required fields — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Health:\n"
        "    var current: int\n"
        "    var max: int\n"
        "system FreezeSystem:\n"
        "    on tick:\n"
        "        add Health:\n"
        "            current = 10\n"
        "            max = 20\n"));
}

TEST_CASE("Semantic: add trait with missing required field — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Health:\n"
        "    var current: int\n"
        "    var max: int\n"
        "system FreezeSystem:\n"
        "    on tick:\n"
        "        add Health:\n"
        "            current = 10\n"));
}

TEST_CASE("Semantic: add/remove cross-entity target must be entity_id", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Frozen\n"
        "system FreezeSystem:\n"
        "    on tick:\n"
        "        add Frozen to 42\n"));
    CHECK(analyze_errors(
        "trait Frozen\n"
        "system FreezeSystem:\n"
        "    on tick:\n"
        "        remove Frozen from 42\n"));
}

TEST_CASE("Semantic: trait default value type mismatch — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Frozen:\n"
        "    var duration: int = 3.14\n"));
}

TEST_CASE("Semantic: trait default value must be constant — error", "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Frozen:\n"
        "    var duration: int = other\n"));
}

// ── Lifecycle events accepted in event handler validation ───────────────────

TEST_CASE("Semantic: lifecycle events not treated as unknown events", "[semantic][dynamic-ecs]") {
    // spawn, destroy, load, unload handlers should not trigger 'unknown event' error
    CHECK_FALSE(analyze_errors(
        "trait Position:\n"
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
    CHECK_FALSE(analyze_errors(
        "trait Persistent\n"
        "unit Player:\n"
        "    Persistent\n"));
}

TEST_CASE("Semantic: pub marker trait", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors("pub trait Persistent\n"));
}

TEST_CASE("Semantic: marker trait in exclude is valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Persistent\n"
        "system Cleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on tick:\n"
        "        destroy\n"));
}

// ── destroy in system handler is valid ─────────────────────────────────────

TEST_CASE("Semantic: destroy in system handler — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "system DeathSystem:\n"
        "    filter:\n"
        "        Health\n"
        "    on tick:\n"
        "        destroy\n"));
}

// ── spawn in system handler is valid ────────────────────────────────────────

TEST_CASE("Semantic: spawn in on tick handler — valid", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Position:\n"
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
    CHECK_FALSE(analyze_errors(
        "trait Persistent\n"
        "system Sys:\n"
        "    filter:\n"
        "        Persistent\n"
        "    on tick:\n"
        "        destroy\n"));
}

TEST_CASE("Semantic: marker trait filter + exclude combination (task 11.10)", "[semantic][dynamic-ecs]") {
    CHECK_FALSE(analyze_errors(
        "trait Persistent\n"
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

TEST_CASE("Semantic: field access in no-filter system body — error (task 11.12)",
          "[semantic][dynamic-ecs]") {
    auto err = first_error(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system GlobalSystem:\n"
        "    on tick:\n"
        "        x = x + tick.dt\n");  // 'x' is a trait field, system has no filter
    CHECK(err.find("not accessible") != std::string::npos);
}

TEST_CASE("Semantic: non-field stmts allowed in no-filter system — ok (task 11.12)",
          "[semantic][dynamic-ecs]") {
    // destroy, emit, spawn etc. are allowed even without filter
    CHECK_FALSE(analyze_errors(
        "system GlobalCleanup:\n"
        "    on unload:\n"
        "        destroy\n"));
}

TEST_CASE("Semantic: field access in filter system body — ok (task 11.12)",
          "[semantic][dynamic-ecs]") {
    // With filter, field access is fine
    CHECK_FALSE(analyze_errors(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Move:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        x = x + tick.dt\n"));
}

TEST_CASE("Semantic: field access in no-filter if-branch — error (task 11.12)",
          "[semantic][dynamic-ecs]") {
    CHECK(analyze_errors(
        "trait Health:\n"
        "       var hp: int = 100\n"
        "system GlobalSys:\n"
        "    on tick:\n"
        "        if hp <= 0:\n"
        "            hp = 100\n"));
}
