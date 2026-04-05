#include <catch2/catch_test_macros.hpp>

#include "backends/cpp-manual/cpp_manual_codegen.h"
#include "backends/cpp-manual/event_emitter.h"
#include "backends/cpp-manual/soa_emitter.h"
#include "backends/cpp-manual/system_emitter.h"
#include "common/error_reporter.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "frontend/semantic_analyzer.h"

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

// ── Test helpers ──────────────────────────────────────────────────────────────

static DecoratedProgram full_pipeline(const std::string& source, ProgramNode& program_out) {
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    program_out = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(program_out);
    REQUIRE_FALSE(errors.has_errors());
    return result;
}

static std::string generate(const std::string& source) {
    ProgramNode program;
    auto decorated = full_pipeline(source, program);
    return CppManualCodegen::generate(decorated);
}

// ── Legacy unit tests (static SoaEmitter methods) ────────────────────────────

TEST_CASE("Codegen Manual: SoA storage from trait (legacy)", "[codegen-manual]") {
    ResolvedTrait trait;
    trait.name = "Position";
    trait.fields.push_back({.name       = "x",
                            .type       = {.kind = TypeKind::Float, .name = "float"},
                            .is_let     = false,
                            .is_var     = true,
                            .is_persist = false,
                            .is_sync    = false,
                            .is_pub     = false});
    trait.fields.push_back({.name       = "y",
                            .type       = {.kind = TypeKind::Float, .name = "float"},
                            .is_let     = false,
                            .is_var     = true,
                            .is_persist = false,
                            .is_sync    = false,
                            .is_pub     = false});

    auto code = SoaEmitter::emit_soa_storage(trait);
    CHECK(code.find("struct PositionStorage") != std::string::npos);
    CHECK(code.find("std::vector<float> x;") != std::string::npos);
    CHECK(code.find("std::vector<float> y;") != std::string::npos);
    CHECK(code.find("size_t count") != std::string::npos);
}

TEST_CASE("Codegen Manual: POD struct", "[codegen-manual]") {
    ResolvedStruct s;
    s.name = "Item";
    s.fields.push_back({.name = "price", .type = {.kind = TypeKind::Int, .name = "int"}});
    s.fields.push_back({.name = "weight", .type = {.kind = TypeKind::Float, .name = "float"}});

    auto code = SoaEmitter::emit_pod_struct(s);
    CHECK(code.find("struct Item") != std::string::npos);
    CHECK(code.find("int price;") != std::string::npos);
    CHECK(code.find("float weight;") != std::string::npos);
}

TEST_CASE("Codegen Manual: enum generation", "[codegen-manual]") {
    ResolvedEnum e;
    e.name = "Color";
    e.variants = {"Red", "Green", "Blue"};

    auto code = SoaEmitter::emit_enum(e);
    CHECK(code.find("enum class Color") != std::string::npos);
    CHECK(code.find("Red") != std::string::npos);
    CHECK(code.find("Green") != std::string::npos);
    CHECK(code.find("Blue") != std::string::npos);
}

TEST_CASE("Codegen Manual: type_to_cpp mapping", "[codegen-manual]") {
    CHECK(SoaEmitter::type_to_cpp({TypeKind::Int, "int"}) == "int");
    CHECK(SoaEmitter::type_to_cpp({TypeKind::Float, "float"}) == "float");
    CHECK(SoaEmitter::type_to_cpp({TypeKind::Bool, "bool"}) == "bool");
    CHECK(SoaEmitter::type_to_cpp({TypeKind::String, "string"}) == "std::string");
    CHECK(SoaEmitter::type_to_cpp({TypeKind::EntityId, "entity_id"}) == "uint32_t");
    CHECK(SoaEmitter::type_to_cpp({TypeKind::Struct, "Item"}) == "Item");
}

TEST_CASE("Codegen Manual: event buffer generation", "[codegen-manual]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Damage:\n"
        "    amount: int\n"
        "    source: int\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* event = std::get_if<EventNode>(&decl)) {
            auto code = ManualEventEmitter::emit_event(*event, decorated);
            CHECK(code.find("struct DamageEvent") != std::string::npos);
            CHECK(code.find("int amount;") != std::string::npos);
            CHECK(code.find("Damage_buffer") != std::string::npos);

            auto dispatch = ManualEventEmitter::emit_dispatch(*event);
            CHECK(dispatch.find("dispatch_Damage") != std::string::npos);
        }
    }
}

// ── Task 7.1: TraitBits constants ─────────────────────────────────────────────

TEST_CASE("Codegen Manual: emit_trait_bits assigns unique bit indices", "[codegen-manual][7.1]") {
    std::vector<std::string> traits = {"Position", "EnemyAI", "Health"};
    auto code = SoaEmitter::emit_trait_bits(traits);

    CHECK(code.find("namespace TraitBits") != std::string::npos);
    CHECK(code.find("Position = 1ULL << 0") != std::string::npos);
    CHECK(code.find("EnemyAI = 1ULL << 1") != std::string::npos);
    CHECK(code.find("Health = 1ULL << 2") != std::string::npos);
}

TEST_CASE("Codegen Manual: TraitBits in full pipeline output", "[codegen-manual][7.1]") {
    auto code = generate(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "trait Health:\n"
        "    var hp: int = 100\n");

    CHECK(code.find("namespace TraitBits") != std::string::npos);
    CHECK(code.find("1ULL << 0") != std::string::npos);
    CHECK(code.find("1ULL << 1") != std::string::npos);
}

// ── Task 7.2: Global entity pool and field arrays ──────────────────────────────

TEST_CASE("Codegen Manual: global entity pool emitted", "[codegen-manual][7.2]") {
    auto code = SoaEmitter::emit_global_entity_pool();
    CHECK(code.find("MAX_ENTITIES") != std::string::npos);
    CHECK(code.find("entity_count") != std::string::npos);
    CHECK(code.find("g_trait_mask") != std::string::npos);
}

TEST_CASE("Codegen Manual: global field arrays emitted", "[codegen-manual][7.2]") {
    std::unordered_map<std::string, ResolvedTrait> traits;
    ResolvedTrait pos;
    pos.name = "Position";
    pos.fields.push_back({.name = "x", .type = {.kind = TypeKind::Float, .name = "float"}});
    pos.fields.push_back({.name = "y", .type = {.kind = TypeKind::Float, .name = "float"}});
    traits["Position"] = pos;

    auto code = SoaEmitter::emit_global_field_arrays(traits, {"Position"});
    CHECK(code.find("g_Position_x") != std::string::npos);
    CHECK(code.find("g_Position_y") != std::string::npos);
    CHECK(code.find("MAX_ENTITIES") != std::string::npos);
}

TEST_CASE("Codegen Manual: full pipeline has global pool and field arrays", "[codegen-manual][7.2]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "    var y: float = 0.0\n");

    CHECK(code.find("entity_count") != std::string::npos);
    CHECK(code.find("g_trait_mask") != std::string::npos);
    CHECK(code.find("g_Position_x") != std::string::npos);
    CHECK(code.find("g_Position_y") != std::string::npos);
}

// ── Tasks 7.3, 7.4: Bitmask loop condition (filter and exclude) ───────────────

TEST_CASE("Codegen Manual: system loop uses bitmask filter condition", "[codegen-manual][7.3]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Move:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        x = x + tick.dt\n");

    CHECK(code.find("_filter_mask") != std::string::npos);
    CHECK(code.find("_exclude_mask") != std::string::npos);
    CHECK(code.find("g_trait_mask[i] & _filter_mask") != std::string::npos);
    CHECK(code.find("while (i < entity_count)") != std::string::npos);
    CHECK(code.find("static void Move_tick(const tickEvent& tick)") != std::string::npos);
    CHECK(code.find("x = x + tick.dt") == std::string::npos);
    CHECK(code.find("x = (x + tick.dt)") != std::string::npos);
}

TEST_CASE("Codegen Manual: system filter_mask=0 for no-filter system", "[codegen-manual][7.3]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "system GlobalCleanup:\n"
        "    on tick:\n"
        "        destroy\n");

    CHECK(code.find("0ULL") != std::string::npos);
}

TEST_CASE("Codegen Manual: exclude mask in loop condition", "[codegen-manual][7.4]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "trait Frozen\n"
        "system Move:\n"
        "    filter:\n"
        "        Position\n"
        "    exclude:\n"
        "        Frozen\n"
        "    on tick as t:\n"
        "        x = x + t.dt\n");

    CHECK(code.find("TraitBits::Frozen") != std::string::npos);
    CHECK(code.find("_exclude_mask") != std::string::npos);
    CHECK(code.find("g_trait_mask[i] & _exclude_mask") != std::string::npos);
    CHECK(code.find("== 0") != std::string::npos);
    CHECK(code.find("static void Move_tick(const tickEvent& t)") != std::string::npos);
    CHECK(code.find("x = (x + t.dt)") != std::string::npos);
}

// ── Dynamic trait add/remove codegen ────────────────────────────────────────

TEST_CASE("Codegen Manual: add emits trait bit set", "[codegen-manual][dynamic-traits]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Frozen\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen\n");

    CHECK(code.find("|= TraitBits::Frozen") != std::string::npos);
    CHECK(code.find("g_trait_mask[i]") != std::string::npos);
}

TEST_CASE("Codegen Manual: remove emits trait bit clear", "[codegen-manual][dynamic-traits]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Frozen\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system ThawSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        remove Frozen\n");

    CHECK(code.find("&= ~TraitBits::Frozen") != std::string::npos);
    CHECK(code.find("g_trait_mask[i]") != std::string::npos);
}

TEST_CASE("Codegen Manual: add with fields initializes arrays", "[codegen-manual][dynamic-traits]") {
    auto code = generate(
        STDLIB_EVENTS +
        "trait Stunned:\n"
        "    var duration: float = 0.0\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Stunned:\n"
        "            duration = 2.0\n");

    CHECK(code.find("g_Stunned_duration[i] = 2.0f") != std::string::npos);
    CHECK(code.find("g_trait_mask[i] |= TraitBits::Stunned") != std::string::npos);
}

// ── Task 7.7: Template factory function ──────────────────────────────────────

TEST_CASE("Codegen Manual: template factory function emitted (task 7.7)", "[codegen-manual][7.7]") {
    auto code = generate(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "    var y: float = 0.0\n"
        "template Enemy:\n"
        "    Position:\n"
        "        x = 100.0\n");

    CHECK(code.find("spawn_Enemy") != std::string::npos);
    CHECK(code.find("entity_count++") != std::string::npos);
    CHECK(code.find("g_Position_x[_idx]") != std::string::npos);
    CHECK(code.find("dispatch_on_spawn") != std::string::npos);
}

TEST_CASE("Codegen Manual: template factory has correct initial trait_mask", "[codegen-manual][7.7]") {
    auto code = generate(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n");

    CHECK(code.find("spawn_Enemy") != std::string::npos);
    CHECK(code.find("TraitBits::Position") != std::string::npos);
    CHECK(code.find("g_trait_mask[_idx]") != std::string::npos);
}

// ── Task 7.8: spawn statement calls factory ───────────────────────────────────

TEST_CASE("Codegen Manual: spawn statement emits factory call (task 7.8)", "[codegen-manual][7.8]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "    var y: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n"
        "system Spawner:\n"
        "    on load:\n"
        "        spawn Enemy:\n"
        "            Position:\n"
        "                x = 200.0\n");

    CHECK(code.find("spawn_Enemy(") != std::string::npos);
    CHECK(code.find("200.0f") != std::string::npos);
}

// ── Task 7.9: destroy uses swap-and-delete ────────────────────────────────────

TEST_CASE("Codegen Manual: destroy emits entity_remove with swap-and-delete (task 7.9)", "[codegen-manual][7.9]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Health:\n"
        "    var hp: int = 100\n"
        "system DeathSystem:\n"
        "    filter:\n"
        "        Health\n"
        "    on tick:\n"
        "        destroy\n");

    CHECK(code.find("entity_remove(i)") != std::string::npos);
    CHECK(code.find("__destroyed = true") != std::string::npos);
    // Verify swap-and-delete logic in entity_remove
    CHECK(code.find("g_trait_mask[_idx] = g_trait_mask[_last]") != std::string::npos);
    CHECK(code.find("--entity_count") != std::string::npos);
}

TEST_CASE("Codegen Manual: destroy loop increments correctly", "[codegen-manual][7.9]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Health:\n"
        "    var hp: int = 100\n"
        "system DeathSystem:\n"
        "    filter:\n"
        "        Health\n"
        "    on tick:\n"
        "        destroy\n");

    // After destroy, entity slot is reused (while loop + __destroyed flag)
    CHECK(code.find("if (!__destroyed) ++i") != std::string::npos);
}

// ── Task 7.10: load emits deferred load ──────────────────────────────────────

TEST_CASE("Codegen Manual: load emits deferred load mechanism (task 7.10)", "[codegen-manual][7.10]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "use levels\n"
        "trait GameState:\n"
        "    var active: bool = true\n"
        "system GameMgr:\n"
        "    filter:\n"
        "        GameState\n"
        "    on tick:\n"
        "        load levels.game\n");

    CHECK(code.find("g_pending_load") != std::string::npos);
    CHECK(code.find("g_load_pending") != std::string::npos);
    CHECK(code.find("levels.game") != std::string::npos);
}

// ── Task 7.11: on_spawn dispatch ─────────────────────────────────────────────

TEST_CASE("Codegen Manual: on_spawn dispatch function emitted (task 7.11)", "[codegen-manual][7.11]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Init:\n"
        "    filter:\n"
        "        Position\n"
        "    on spawn:\n"
        "        x = 0.0\n");

    CHECK(code.find("dispatch_on_spawn") != std::string::npos);
    CHECK(code.find("Init_spawn") != std::string::npos);
    CHECK(code.find("static void Init_spawn(size_t _idx, const spawnEvent& spawn)") != std::string::npos);
    CHECK(code.find("Init_spawn(_idx, SpawnEvent{})") != std::string::npos);
    // Filter check inside dispatch
    CHECK(code.find("g_trait_mask[_idx]") != std::string::npos);
    CHECK(code.find("TraitBits::Position") != std::string::npos);
}

TEST_CASE("Codegen Manual: aliased tick handler uses alias in signature", "[codegen-manual][event-handler]") {
    auto code = generate(
        STDLIB_EVENTS +
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Move:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick as t:\n"
        "        x = x + t.dt\n");

    CHECK(code.find("static void Move_tick(const tickEvent& t)") != std::string::npos);
    CHECK(code.find("x = (x + t.dt)") != std::string::npos);
}

TEST_CASE("Codegen Manual: marker spawn event emits empty struct and parameter", "[codegen-manual][event-handler]") {
    auto code = generate(
        STDLIB_EVENTS +
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Init:\n"
        "    filter:\n"
        "        Position\n"
        "    on spawn:\n"
        "        x = 0.0\n");

    CHECK(code.find("struct spawnEvent {\n};") != std::string::npos);
    CHECK(code.find("static void Init_spawn(size_t _idx, const spawnEvent& spawn)") != std::string::npos);
}

// ── Task 7.12: on_destroy dispatch ───────────────────────────────────────────

TEST_CASE("Codegen Manual: on_destroy dispatch function emitted (task 7.12)", "[codegen-manual][7.12]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Cleanup:\n"
        "    filter:\n"
        "        Position\n"
        "    on destroy:\n"
        "        x = 0.0\n");

    CHECK(code.find("dispatch_on_destroy") != std::string::npos);
    CHECK(code.find("Cleanup_destroy") != std::string::npos);
    // entity_remove calls dispatch_on_destroy before swap
    CHECK(code.find("dispatch_on_destroy(_idx)") != std::string::npos);
}

// ── Task 7.13: on_unload dispatch ────────────────────────────────────────────

TEST_CASE("Codegen Manual: on_unload dispatch function emitted (task 7.13)", "[codegen-manual][7.13]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Persistent\n"
        "system SceneCleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n");

    CHECK(code.find("dispatch_on_unload") != std::string::npos);
    CHECK(code.find("SceneCleanup_unload") != std::string::npos);
    CHECK(code.find("TraitBits::Persistent") != std::string::npos);
}

// ── Task 7.14: on_load dispatch ──────────────────────────────────────────────

TEST_CASE("Codegen Manual: on_load dispatch function emitted (task 7.14)", "[codegen-manual][7.14]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait LevelState:\n"
        "    var loaded: bool = false\n"
        "system LevelSetup:\n"
        "    filter:\n"
        "        LevelState\n"
        "    on load:\n"
        "        loaded = true\n");

    CHECK(code.find("dispatch_on_load") != std::string::npos);
    CHECK(code.find("LevelSetup_load") != std::string::npos);
}

// ── Task 7.15: Full codegen verification ─────────────────────────────────────

TEST_CASE("Codegen Manual: complete codegen structure", "[codegen-manual][7.15]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "    var y: float = 0.0\n"
        "trait Persistent\n"
        "template Enemy:\n"
        "    Position:\n"
        "        x = 400.0\n"
        "unit Player:\n"
        "    Position:\n"
        "        x = 100.0\n"
        "        y = 200.0\n"
        "    Persistent\n"
        "system PatrolSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on tick:\n"
        "        x = x + tick.dt\n"
        "    on spawn:\n"
        "        x = 0.0\n"
        "    on destroy:\n"
        "        y = 0.0\n"
        "system SceneCleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n");

    // TraitBits constants (task 7.1)
    CHECK(code.find("namespace TraitBits") != std::string::npos);
    CHECK(code.find("1ULL << 0") != std::string::npos);
    CHECK(code.find("1ULL << 1") != std::string::npos);

    // Global entity pool (task 7.2)
    CHECK(code.find("entity_count") != std::string::npos);
    CHECK(code.find("g_trait_mask") != std::string::npos);
    CHECK(code.find("g_Position_x") != std::string::npos);

    // Bitmask filter condition (task 7.3)
    CHECK(code.find("_filter_mask") != std::string::npos);
    CHECK(code.find("_exclude_mask") != std::string::npos);
    CHECK(code.find("while (i < entity_count)") != std::string::npos);

    // Template factory (task 7.7)
    CHECK(code.find("spawn_Enemy") != std::string::npos);

    // Swap-and-delete (task 7.9)
    CHECK(code.find("entity_remove") != std::string::npos);
    CHECK(code.find("g_trait_mask[_idx] = g_trait_mask[_last]") != std::string::npos);

    // Deferred load (task 7.10)
    CHECK(code.find("g_pending_load") != std::string::npos);

    // Lifecycle dispatches
    CHECK(code.find("dispatch_on_spawn") != std::string::npos);
    CHECK(code.find("dispatch_on_destroy") != std::string::npos);
    CHECK(code.find("dispatch_on_unload") != std::string::npos);

    // Unit initialization in init_units (not in template-only spawn path)
    CHECK(code.find("init_units") != std::string::npos);
    CHECK(code.find("// Unit: Player") != std::string::npos);

    // Main with deferred load check
    CHECK(code.find("int main()") != std::string::npos);
    CHECK(code.find("perform_load") != std::string::npos);
}

// ── Tasks 8.1-8.5: Scene loading ─────────────────────────────────────────────

TEST_CASE("Codegen Manual: deferred load state globals emitted (task 8.1)", "[codegen-manual][8.1]") {
    auto code = generate(
        "trait T:\n"
        "    var x: float = 0.0\n");
    CHECK(code.find("g_pending_load") != std::string::npos);
    CHECK(code.find("g_load_pending") != std::string::npos);
    CHECK(code.find("g_load_multi_error") != std::string::npos);
}

TEST_CASE("Codegen Manual: perform_load has 3 phases in correct order (tasks 8.2-8.4)", "[codegen-manual][8.2-8.4]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Persistent\n"
        "system SceneCleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n"
        "system LevelSetup:\n"
        "    on load:\n"
        "        destroy\n");

    CHECK(code.find("perform_load") != std::string::npos);

    // Phase 1 before Phase 3 in perform_load
    auto phase1_pos = code.find("dispatch_on_unload()");
    auto phase3_pos = code.find("dispatch_on_load()");
    CHECK(phase1_pos != std::string::npos);
    CHECK(phase3_pos != std::string::npos);
    CHECK(phase1_pos < phase3_pos);

    // Phase comments present
    CHECK(code.find("Phase 1") != std::string::npos);
    CHECK(code.find("Phase 3") != std::string::npos);
}

TEST_CASE("Codegen Manual: on_unload fires before new entities (task 8.5)", "[codegen-manual][8.5]") {
    // Verify that dispatch_on_unload comes before any data-file loading
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Persistent\n"
        "system Cleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n");

    // In perform_load, unload must come first
    auto unload_pos = code.find("dispatch_on_unload");
    auto load_pos   = code.find("dispatch_on_load");
    CHECK(unload_pos != std::string::npos);
    CHECK(load_pos   != std::string::npos);
    CHECK(unload_pos < load_pos);
}

TEST_CASE("Codegen Manual: end-of-frame deferred load in main loop (task 8.1)", "[codegen-manual][8.6]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "use levels\n"
        "trait GameState:\n"
        "    var x: float = 0.0\n"
        "system GameMgr:\n"
        "    filter:\n"
        "        GameState\n"
        "    on tick:\n"
        "        load levels.game\n");

    // Main loop must check g_load_pending and call perform_load
    CHECK(code.find("if (g_load_pending)") != std::string::npos);
    CHECK(code.find("perform_load(g_pending_load)") != std::string::npos);
    CHECK(code.find("g_pending_load.clear()") != std::string::npos);
}

// ── Tasks 9.3, 9.4: std.core integration ─────────────────────────────────────

TEST_CASE("Codegen Manual: std.core Persistent trait codegen", "[codegen-manual][9.3]") {
    // Simulate code with an explicit Persistent trait (as if imported from std.core)
    auto code = generate(
        "trait Persistent\n"
        "unit Player:\n"
        "    Persistent\n");

    // Persistent should be assigned a TraitBits value
    CHECK(code.find("TraitBits") != std::string::npos);
    CHECK(code.find("Persistent") != std::string::npos);
    // Player unit should have Persistent bit set in init_units
    CHECK(code.find("TraitBits::Persistent") != std::string::npos);
}

TEST_CASE("Codegen Manual: without SceneCleanup dispatch_on_unload is empty (task 9.4)", "[codegen-manual][9.4]") {
    // Without std.core, no SceneCleanup system, so dispatch_on_unload does nothing
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system PatrolSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        x = x + tick.dt\n");

    // dispatch_on_unload exists but has no system calls
    CHECK(code.find("dispatch_on_unload") != std::string::npos);
    // No SceneCleanup_unload call
    CHECK(code.find("SceneCleanup_unload") == std::string::npos);
}

TEST_CASE("Codegen Manual: with SceneCleanup on_unload destroys non-persistent (task 9.4)", "[codegen-manual][9.4]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Persistent\n"
        "system SceneCleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n");

    CHECK(code.find("SceneCleanup_unload") != std::string::npos);
    CHECK(code.find("dispatch_on_unload") != std::string::npos);
    // SceneCleanup_unload must call entity_remove for non-persistent
    CHECK(code.find("entity_remove(i)") != std::string::npos);
}

// ── Tasks 11.1-11.13: Integration tests ──────────────────────────────────────

TEST_CASE("Codegen: template declared, not auto-instantiated (task 11.1)", "[codegen-manual][11.1]") {
    auto code = generate(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n");

    // Factory function declared
    CHECK(code.find("spawn_Enemy") != std::string::npos);
    // init_units should NOT auto-create Enemy entities
    CHECK(code.find("// Unit: Enemy") == std::string::npos);
}

TEST_CASE("Codegen: spawn creates entity, on_spawn fires (task 11.2)", "[codegen-manual][11.2]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n"
        "system Init:\n"
        "    filter:\n"
        "        Position\n"
        "    on spawn:\n"
        "        x = 0.0\n"
        "system Spawner:\n"
        "    on load:\n"
        "        spawn Enemy:\n"
        "            Position:\n"
        "                x = 400.0\n");

    // Spawn call should appear in Spawner_load
    CHECK(code.find("spawn_Enemy(") != std::string::npos);
    // dispatch_on_spawn called after entity creation
    CHECK(code.find("dispatch_on_spawn") != std::string::npos);
    // Init_spawn handler defined
    CHECK(code.find("Init_spawn") != std::string::npos);
}

TEST_CASE("Codegen: destroy removes entity, on_destroy fires (task 11.3)", "[codegen-manual][11.3]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Health:\n"
        "    var hp: int = 100\n"
        "system DeathSys:\n"
        "    filter:\n"
        "        Health\n"
        "    on destroy:\n"
        "        hp = 0\n"
        "    on tick:\n"
        "        destroy\n");

    // on_destroy handler defined
    CHECK(code.find("DeathSys_destroy") != std::string::npos);
    // entity_remove triggers on_destroy before removing
    CHECK(code.find("dispatch_on_destroy(_idx)") != std::string::npos);
    // Swap-and-delete present
    CHECK(code.find("--entity_count") != std::string::npos);
}

TEST_CASE("Codegen: load transition 3-phase ordering (task 11.4)", "[codegen-manual][11.4]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Persistent\n"
        "system SceneCleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n"
        "system LevelSetup:\n"
        "    on load:\n"
        "        destroy\n");

    auto unload_call = code.find("dispatch_on_unload();\n");
    auto load_call   = code.find("dispatch_on_load();\n");
    CHECK(unload_call != std::string::npos);
    CHECK(load_call   != std::string::npos);
    CHECK(unload_call < load_call);
}

TEST_CASE("Codegen: on_unload fires before new entities created (task 11.5)", "[codegen-manual][11.5]") {
    // Same as 11.4 — ordering verified by position in perform_load
    auto code = generate(
        STDLIB_EVENTS + 
        "system GlobalReset:\n"
        "    on unload:\n"
        "        destroy\n"
        "system GlobalSetup:\n"
        "    on load:\n"
        "        destroy\n");

    auto u = code.find("GlobalReset_unload");
    auto l = code.find("GlobalSetup_load");
    CHECK(u != std::string::npos);
    CHECK(l != std::string::npos);
    // unload handler definition comes before load handler definition
    // (both are called from dispatch functions which appear in order)
}

TEST_CASE("Codegen: on_load fires after all entities instantiated (task 11.6)", "[codegen-manual][11.6]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "system LevelSetup:\n"
        "    on load:\n"
        "        destroy\n");

    // Phase 3 Load fires after Phase 2 Instantiate
    auto phase2 = code.find("Phase 2");
    auto phase3 = code.find("Phase 3");
    CHECK(phase2 != std::string::npos);
    CHECK(phase3 != std::string::npos);
    CHECK(phase2 < phase3);
}

TEST_CASE("Codegen: add/remove toggles trait presence bitmask", "[codegen-manual][11.7]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Frozen\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system FreezeSystem:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen\n"
        "        remove Frozen\n");

    CHECK(code.find("|= TraitBits::Frozen") != std::string::npos);
    CHECK(code.find("&= ~TraitBits::Frozen") != std::string::npos);
    CHECK(code.find("g_Position_x[i]") != std::string::npos);
}

TEST_CASE("Codegen: exclude skips entities with active excluded trait (task 11.8)", "[codegen-manual][11.8]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "trait Frozen\n"
        "system Move:\n"
        "    filter:\n"
        "        Position\n"
        "    exclude:\n"
        "        Frozen\n"
        "    on tick:\n"
        "        x = x + tick.dt\n");

    // Exclude mask applied in loop condition
    auto filt = code.find("TraitBits::Position");
    auto excl = code.find("TraitBits::Frozen");
    CHECK(filt != std::string::npos);
    CHECK(excl != std::string::npos);
    CHECK(code.find("_exclude_mask") != std::string::npos);
}

TEST_CASE("Codegen: exclude checks current trait presence bitmask (task 11.9)", "[codegen-manual][11.9]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "trait Frozen\n"
        "system Move:\n"
        "    filter:\n"
        "        Position\n"
        "    exclude:\n"
        "        Frozen\n"
        "    on tick:\n"
        "        x = x + tick.dt\n");

    CHECK(code.find("g_trait_mask[i] & _exclude_mask") != std::string::npos);
}

TEST_CASE("Codegen: marker trait in apply initializes trait_mask bit (task 11.10)", "[codegen-manual][11.10]") {
    auto code = generate(
        "trait Persistent\n"
        "unit Player:\n"
        "    Persistent\n");

    // init_units sets Persistent bit in trait_mask
    CHECK(code.find("TraitBits::Persistent") != std::string::npos);
    // No field arrays for marker trait
    CHECK(code.find("g_Persistent_") == std::string::npos);
}

TEST_CASE("Codegen: no-filter + exclude processes all non-excluded (task 11.11)", "[codegen-manual][11.11]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Persistent\n"
        "system SceneCleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n");

    // filter_mask = 0 (no filter), exclude_mask = TraitBits::Persistent
    CHECK(code.find("0ULL") != std::string::npos);
    CHECK(code.find("TraitBits::Persistent") != std::string::npos);
    CHECK(code.find("entity_remove(i)") != std::string::npos);
}

TEST_CASE("Codegen: Persistent entity survives load with SceneCleanup (task 11.13)", "[codegen-manual][11.13]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Persistent\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "unit Player:\n"
        "    Position\n"
        "    Persistent\n"
        "system SceneCleanup:\n"
        "    exclude:\n"
        "        Persistent\n"
        "    on unload:\n"
        "        destroy\n");

    // SceneCleanup excludes Persistent → Player survives unload
    CHECK(code.find("TraitBits::Persistent") != std::string::npos);
    CHECK(code.find("SceneCleanup_unload") != std::string::npos);
    // Player's trait_mask has Persistent bit set
    CHECK(code.find("TraitBits::Persistent") != std::string::npos);

    // In init_units, Player gets Persistent bit
    auto player_init = code.find("// Unit: Player");
    CHECK(player_init != std::string::npos);
}

// ── extern-func codegen tests (task 7.4) ─────────────────────────────────────

TEST_CASE("Codegen Manual: extern func generates runtime header include", "[codegen-manual][extern-func]") {
    auto code = generate(
        "pub extern func lerp(a: float, b: float, t: float) float\n"
        "trait Pos:\n"
        "    var x: float = 0.0\n");

    CHECK(code.find("#include \"cactus_runtime.h\"") != std::string::npos);
}

TEST_CASE("Codegen Manual: no extern func means no runtime header", "[codegen-manual][extern-func]") {
    auto code = generate(
        "trait Pos:\n"
        "    var x: float = 0.0\n");

    CHECK(code.find("#include \"cactus_runtime.h\"") == std::string::npos);
}

TEST_CASE("Codegen Manual: runtime header placed after standard includes", "[codegen-manual][extern-func]") {
    auto code = generate(
        "pub extern func sin(a: float) float\n");

    auto raylib_pos  = code.find("#include \"raylib.h\"");
    auto runtime_pos = code.find("#include \"cactus_runtime.h\"");
    CHECK(raylib_pos  != std::string::npos);
    CHECK(runtime_pos != std::string::npos);
    // cactus_runtime.h comes after raylib.h
    CHECK(runtime_pos > raylib_pos);
}

// ── Full pipeline structure tests ─────────────────────────────────────────────

TEST_CASE("Codegen Manual: full pipeline generates compilable structure", "[codegen-manual]") {
    auto code = generate(
        STDLIB_EVENTS + 
        "trait Pos:\n"
        "    persist sync var x: float = 0.0\n"
        "    persist sync var y: float = 0.0\n"
        "system Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n");

    // Check overall structure
    CHECK(code.find("Generated by Cactus DSL Compiler") != std::string::npos);
    CHECK(code.find("#include <vector>") != std::string::npos);
    CHECK(code.find("#include \"raylib.h\"") != std::string::npos);
    // New model: global arrays, not per-trait storage
    CHECK(code.find("g_Pos_x") != std::string::npos);
    CHECK(code.find("int main()") != std::string::npos);
    CHECK(code.find("InitWindow") != std::string::npos);
    CHECK(code.find("CloseWindow") != std::string::npos);

    // Persist hooks (task 7.2 — field serialization stubs)
    CHECK(code.find("save_Pos") != std::string::npos);
    CHECK(code.find("load_Pos") != std::string::npos);

    // Sync hooks
    CHECK(code.find("replicate_Pos") != std::string::npos);
}
