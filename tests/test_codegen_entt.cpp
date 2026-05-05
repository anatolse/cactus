// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/cpp_entt_codegen.hpp"
#include "backends/cpp-entt/event_emitter.hpp"
#include "backends/cpp-entt/system_emitter.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cactus;

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

TEST_CASE("Codegen EnTT: component struct from trait", "[codegen-entt]") {
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

    auto code = EnttComponentEmitter::emit_component(trait);
    CHECK(code.find("struct Position") != std::string::npos);
    CHECK(code.find("float x{}") != std::string::npos);
    CHECK(code.find("float y{}") != std::string::npos);
    // Should NOT have std::vector (that's SoA)
    CHECK(code.find("std::vector") == std::string::npos);
}

TEST_CASE("Codegen EnTT: stdlib collider components keep authored defaults", "[codegen-entt][stdlib][physics]") {
    ResolvedTrait collider;
    collider.name      = "Collider";
    collider.is_pub    = true;
    collider.is_stdlib = true;
    collider.fields.push_back({.name = "layer", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});
    collider.fields.push_back({.name = "mask", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});

    const auto collider_code = EnttComponentEmitter::emit_component(collider);
    CHECK(collider_code.find("int layer{1};") != std::string::npos);
    CHECK(collider_code.find("int mask{1};") != std::string::npos);

    ResolvedTrait box;
    box.name      = "BoxCollider";
    box.is_pub    = true;
    box.is_stdlib = true;
    box.fields.push_back({.name = "size", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});

    const auto box_code = EnttComponentEmitter::emit_component(box);
    CHECK(box_code.find("Vector2 size{.x = 1.0F, .y = 1.0F};") != std::string::npos);
}

TEST_CASE("Codegen EnTT: registry view system", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "    var y: float\n"
        "system Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("void move_tick") != std::string::npos);
            CHECK(code.find("entt::registry& registry") != std::string::npos);
            CHECK(code.find("const TickEvent& tick") != std::string::npos);
            CHECK(code.find("Pos_comp.x = (Pos_comp.x + tick.dt)") != std::string::npos);
            CHECK(code.find("registry.view<Pos>()") != std::string::npos);
            CHECK(code.find("view.each") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: event struct", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Damage:\n"
        "   amount: int\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* event = std::get_if<EventNode>(&decl)) {
            auto code = EnttEventEmitter::emit_event(*event, decorated);
            CHECK(code.find("struct DamageEvent") != std::string::npos);
            CHECK(code.find("int amount;") != std::string::npos);

            auto sink = EnttEventEmitter::emit_sink_connection(*event);
            CHECK(sink.find("dispatcher.sink<DamageEvent>") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: CollisionEnter event supports entity and vector payload fields",
          "[codegen-entt][stdlib][physics]") {
    EventNode event;
    event.name = "CollisionEnter";
    event.fields.push_back({.name = "other", .type = {.name = "entity_id"}});
    event.fields.push_back({.name = "overlap", .type = {.name = "vec2"}});

    DecoratedProgram program;
    const auto code = EnttEventEmitter::emit_event(event, program);
    CHECK(code.find("entt::entity other;") != std::string::npos);
    CHECK(code.find("Vector2 overlap;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: full pipeline", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick: \n"
        "    dt: float\n"
        "trait Pos:\n    persist sync var x: float\n    persist sync var y: float\n"
        "system Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n ",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.find("Generated by Cactus DSL Compiler (cpp-entt backend)") != std::string::npos);
    CHECK(code.find("#include <entt/entt.hpp>") != std::string::npos);
    CHECK(code.find("#include <raylib.h>") != std::string::npos);
    CHECK(code.find("struct Pos") != std::string::npos);
    CHECK(code.find("backends/cpp-entt/runtime.hpp") != std::string::npos);
    CHECK(code.find("generated_project_config() noexcept") != std::string::npos);
    CHECK(code.find("generated_init_project(entt::registry& registry)") != std::string::npos);
    CHECK(code.find("generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt)") !=
          std::string::npos);
    CHECK(code.find("move_tick(registry, TickEvent{dt});") != std::string::npos);
    CHECK(code.find("int main()") != std::string::npos);
    CHECK(code.find("InitWindow(config.window_width, config.window_height, config.window_title)") != std::string::npos);
    CHECK(code.find("entt::registry registry;") != std::string::npos);
    CHECK(code.find("entt::dispatcher dispatcher;") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::generated_setup_dispatcher(dispatcher);") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::generated_init_project(registry);") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::generated_update_project(registry, dispatcher, dt);") !=
          std::string::npos);
    CHECK(code.find("BeginDrawing();") != std::string::npos);
    CHECK(code.find("ClearBackground(RAYWHITE);") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::generated_render_project(registry, dispatcher);") !=
          std::string::npos);
    CHECK(code.find("EndDrawing();") != std::string::npos);

    // Persist hooks
    CHECK(code.find("save_Pos") != std::string::npos);
    CHECK(code.find("load_Pos") != std::string::npos);

    // Sync hooks
    CHECK(code.find("replicate_Pos") != std::string::npos);
}

TEST_CASE("Codegen EnTT: extern func generates runtime header include", "[codegen-entt][extern-func]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern func lerp(a: float, b: float, t: float) float\n"
        "trait Pos:\n    var x: float\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("#include \"cactus_runtime.hpp\"") != std::string::npos);
}

TEST_CASE("Codegen EnTT: no extern func means no runtime header", "[codegen-entt][extern-func]") {
    ProgramNode program;
    auto decorated = full_pipeline("trait Pos:\n    var x: float\n", program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("#include \"cactus_runtime.hpp\"") == std::string::npos);
}

TEST_CASE("Codegen EnTT: extern func body is not emitted", "[codegen-entt][extern-func]") {
    ProgramNode program;
    auto decorated = full_pipeline("pub extern func sin(a: float) float\n", program);

    auto code = CppEnttCodegen::generate(decorated);
    // No function definition should be emitted for the extern func
    // (the include is present but no function body)
    CHECK(code.find("float sin(") == std::string::npos);
}

TEST_CASE("Codegen EnTT: imported stdlib math alias lowers to runtime namespace",
          "[codegen-entt][extern-func][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.math as math\n"
        "event tick:\n"
        "    dt: float\n"
        "trait Value:\n"
        "    var x: float\n"
        "system Demo:\n"
        "    filter:\n"
        "        Value\n"
        "    on tick:\n"
        "        x = math.lerp(0.0, 10.0, 0.5)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::stdlib::math::lerp(0.0F, 10.0F, 0.5F)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: unqualified imported stdlib func lowers to runtime namespace",
          "[codegen-entt][extern-func][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.math\n"
        "event tick:\n"
        "    dt: float\n"
        "trait Value:\n"
        "    var x: float\n"
        "system Demo:\n"
        "    filter:\n"
        "        Value\n"
        "    on tick:\n"
        "        x = lerp(0.0, 10.0, 0.5)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::stdlib::math::lerp(0.0F, 10.0F, 0.5F)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: std.input extern calls lower to backend runtime namespace",
          "[codegen-entt][extern-func][stdlib][input]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "use std.input\n"
        "input Jump: button\n"
        "    key = Key.Space\n"
        "trait Controller:\n"
        "    var active: bool = false\n"
        "system Demo:\n"
        "    filter:\n"
        "        Controller\n"
        "    on tick:\n"
        "        active = down(Jump)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::down(K_JUMP)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: std.physics.flat query calls lower with registry access",
          "[codegen-entt][extern-func][stdlib][physics]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.physics.flat as phys\n"
        "pub event tick:\n"
        "    dt: float\n"
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
        "pub extern func query_overlap_all(subject: entity_id, mask: int, exclude: entity_id) list[QueryContact2D]\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "trait BoxCollider:\n"
        "    var size: vec2\n"
        "system QueryProbe:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        BoxCollider\n"
        "    on tick:\n"
        "        let hit = phys.query_cast_nearest(self, vec2(1.0, 0.0), 1, self)\n"
        "        if hit.kind == phys.QueryResultKind.Empty:\n"
        "            let contacts = phys.query_overlap_all(self, 1, self)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("auto hit = cactus_query_cast_nearest(registry, entity, vec2(1.0F, 0.0F), 1, entity)") !=
          std::string::npos);
    CHECK(code.find("if (hit.kind == QueryResultKind::Empty)") != std::string::npos);
    CHECK(code.find("auto contacts = cactus_query_overlap_all(registry, entity, 1, entity)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: hierarchy destroy helper delegates to runtime library", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Parent:\n"
        "    var parent: entity_id\n"
        "pub event tick:\n"
        "    dt: float\n"
        "system Cleanup:\n"
        "    on tick:\n"
        "        destroy self\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::destroy_entity_recursive(") != std::string::npos);
}

TEST_CASE("Codegen EnTT: sprite and animation extern systems bind to asset runtime adapters",
          "[codegen-entt][assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "trait Renderer:\n"
        "    let texture: texture_id\n"
        "    var size: vec2\n"
        "    var color: color\n"
        "    var visible: bool\n"
        "    var layer: int\n"
        "trait AnimatedSprite:\n"
        "    let texture: texture_id\n"
        "    var frame: int\n"
        "    var frame_count: int\n"
        "    var fps: float\n"
        "    var playing: bool\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "extern system AnimatedSpriteSystem:\n"
        "    filter:\n"
        "        AnimatedSprite\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::submit_sprite(") != std::string::npos);
    CHECK(code.find("Renderer_comp.layer") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::advance_animated_sprite(") != std::string::npos);
}

TEST_CASE("Codegen EnTT: render glue brackets render extern systems with frame calls", "[codegen-entt][assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "trait Renderer:\n"
        "    let texture: texture_id\n"
        "    var size: vec2\n"
        "    var color: color\n"
        "    var visible: bool\n"
        "    var layer: int\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::begin_render_frame();") != std::string::npos);
    CHECK(code.find("sprite_renderer_tick(registry);") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::end_render_frame();") != std::string::npos);
}

TEST_CASE("Codegen EnTT: mesh renderer extern system binds to backend runtime without user callback",
          "[codegen-entt][assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait Renderer:\n"
        "    let mesh: mesh_id\n"
        "    let material: material_id\n"
        "    var visible: bool\n"
        "    var cast_shadow: bool\n"
        "extern system MeshRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::submit_mesh(") != std::string::npos);
    CHECK(code.find("void mesh_renderer_update(") == std::string::npos);
}

TEST_CASE("Codegen EnTT: generated init registers declared mesh and material assets", "[codegen-entt][assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "asset BlueCubeMesh: mesh = \"models/blue_cube.mesh\"\n"
        "asset BlueCubeMaterial: material = \"materials/blue_cube.mat\"\n"
        "trait Marker\n"
        "unit Cube:\n"
        "    Marker\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("shared_asset_registry().register_mesh(BlueCubeMesh, \"models/blue_cube.mesh\", "
                    "static_cast<int>(BlueCubeMesh));") != std::string::npos);
    CHECK(code.find("shared_asset_registry().register_material(BlueCubeMaterial, \"materials/blue_cube.mat\", "
                    "static_cast<int>(BlueCubeMaterial));") != std::string::npos);
}

TEST_CASE("Codegen EnTT: entity creation from unit", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Pos:\n"
        "    var x: float\n"
        "    var y: float\n"
        "trait Health:\n"
        "    var hp: int\n"
        "unit Player:\n"
        "    Pos\n"
        "    Health\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("create_player") != std::string::npos);
    CHECK(code.find("registry.create()") != std::string::npos);
    CHECK(code.find("registry.emplace<Pos>") != std::string::npos);
    CHECK(code.find("registry.emplace<Health>") != std::string::npos);
}

TEST_CASE("Codegen EnTT: add/remove trait statements", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Frozen\n"
        "trait Stunned:\n"
        "    var duration: float = 0.0\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "system Freeze:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen\n"
        "        add Stunned:\n"
        "            duration = 2.0\n"
        "        remove Frozen\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.emplace_or_replace<Frozen>(entity)") != std::string::npos);
            CHECK(code.find("registry.emplace_or_replace<Stunned>(entity, __value)") != std::string::npos);
            CHECK(code.find("registry.remove<Frozen>(entity)") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: cross-entity add/remove/destroy use validity guards", "[codegen-entt][entity-id]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Collision:\n"
        "    other: entity_id\n"
        "trait Frozen\n"
        "system Cleanup:\n"
        "    on Collision as c:\n"
        "        add Frozen to c.other\n"
        "        remove Frozen from c.other\n"
        "        destroy c.other\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (registry.valid(c.other))") != std::string::npos);
            CHECK(code.find("registry.emplace_or_replace<Frozen>(c.other)") != std::string::npos);
            CHECK(code.find("registry.remove<Frozen>(c.other)") != std::string::npos);
            CHECK(code.find("cactus_destroy_entity_recursive(registry, c.other)") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: targeted emit uses validity guard", "[codegen-entt][entity-id]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Hit:\n"
        "    amount: int\n"
        "event Collision:\n"
        "    other: entity_id\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        emit Hit to c.other:\n"
        "            amount = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (registry.valid(c.other))") != std::string::npos);
            CHECK(code.find("Hit_buffer.push_back({.amount = 1})") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: exists compiles to registry.valid", "[codegen-entt][entity-id]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Collision:\n"
        "    other: entity_id\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        if exists(c.other):\n"
        "            let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (registry.valid(c.other))") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: trait match is guarded by entity validity", "[codegen-entt][entity-id]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Collision:\n"
        "    other: entity_id\n"
        "trait Boss:\n"
        "    var phase: int\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.phase\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("auto __match_entity = c.other") != std::string::npos);
            CHECK(code.find("if (registry.valid(__match_entity))") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: trait match emits try_get, all_of, and else", "[codegen-entt][trait-match]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Collision:\n"
        "    other: entity_id\n"
        "trait Boss:\n"
        "    var phase: int\n"
        "trait Spike\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.phase\n"
        "            Spike =>\n"
        "                let y = 1\n"
        "            _ =>\n"
        "                let z = 2\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("auto __match_entity = c.other") != std::string::npos);
            CHECK(code.find("auto* b = registry.try_get<Boss>(__match_entity)") != std::string::npos);
            CHECK(code.find("registry.all_of<Spike>(__match_entity)") != std::string::npos);
            CHECK(code.find("else {") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: aliased tick handler uses alias in signature and body", "[codegen-entt][event-handler]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "system Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick as t:\n"
        "        x = x + t.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("const TickEvent& t") != std::string::npos);
            CHECK(code.find("Pos_comp.x = (Pos_comp.x + t.dt)") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: spawn handler uses marker event parameter", "[codegen-entt][event-handler]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event spawn\n"
        "trait Pos:\n"
        "    var x: float\n"
        "system Init:\n"
        "    filter:\n"
        "        Pos\n"
        "    on spawn:\n"
        "        x = 0.0\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("void init_spawn(entt::registry& registry, const spawnEvent& spawn)") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: trait match without wildcard emits no else", "[codegen-entt][trait-match]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Collision:\n"
        "    other: entity_id\n"
        "trait Boss:\n"
        "    var phase: int\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.phase\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("auto* b = registry.try_get<Boss>(__match_entity)") != std::string::npos);
            CHECK(code.find("else {") == std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: extern system emits callback scaffold", "[codegen-entt][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var x: float\n"
        "trait Velocity:\n"
        "    var dx: float\n"
        "extern system ParticleSystem:\n"
        "    filter:\n"
        "        Position\n"
        "        Velocity\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_extern_system(*sys, decorated);
            CHECK(code.find("void particle_system_tick(entt::registry& registry)") != std::string::npos);
            CHECK(code.find("registry.view<Position, Velocity>()") != std::string::npos);
            CHECK(code.find("particle_system_update(registry, entity, Position_comp, Velocity_comp)") !=
                  std::string::npos);
            CHECK(code.find("void particle_system_update(entt::registry& registry, entt::entity entity, Position& "
                            "Position_comp, Velocity& Velocity_comp);") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: extern system order by is reflected in scaffold comments", "[codegen-entt][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var y: float\n"
        "extern system SortedRenderer:\n"
        "    filter:\n"
        "        Position\n"
        "    order by:\n"
        "        Position.y desc\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_extern_system(*sys, decorated);
            CHECK(code.find("// order by:") != std::string::npos);
            CHECK(code.find("//   Position.y desc") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: system order by emits registry sort for single key", "[codegen-entt][system-order-by]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Sprite:\n"
        "    var layer: int\n"
        "system Render:\n"
        "    filter:\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer asc\n"
        "    on tick:\n"
        "        let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.sort<Sprite>([&](entt::entity a, entt::entity b)") != std::string::npos);
            CHECK(code.find("registry.get<Sprite>(a).layer < registry.get<Sprite>(b).layer") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: system order by emits multi-key comparator", "[codegen-entt][system-order-by]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Position:\n"
        "    var pos: vec2\n"
        "trait Sprite:\n"
        "    var layer: int\n"
        "system Render:\n"
        "    filter:\n"
        "        Position as p\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer asc\n"
        "        p.pos.y desc\n"
        "    on tick:\n"
        "        let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (registry.get<Sprite>(a).layer != registry.get<Sprite>(b).layer)") !=
                  std::string::npos);
            CHECK(code.find("return registry.get<Position>(a).pos.y > registry.get<Position>(b).pos.y;") !=
                  std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: system without order by emits no sort call", "[codegen-entt][system-order-by]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Sprite:\n"
        "    var layer: int\n"
        "system Render:\n"
        "    filter:\n"
        "        Sprite\n"
        "    on tick:\n"
        "        let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.sort<") == std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: full pipeline includes extern system tick call", "[codegen-entt][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var x: float\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        Position\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("void sprite_renderer_tick(entt::registry& registry)") != std::string::npos);
    CHECK(code.find("sprite_renderer_update(registry, entity, Position_comp)") != std::string::npos);
    CHECK(code.find("namespace cactus::runtime::entt_backend") != std::string::npos);
    CHECK(code.find("void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher)") !=
          std::string::npos);
    CHECK(code.find("sprite_renderer_tick(registry);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: stdlib-style extern sprite renderer emits scaffold", "[codegen-entt][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Renderer:\n"
        "    var layer: int\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        Transform\n"
        "        Renderer\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("void sprite_renderer_tick(entt::registry& registry)") != std::string::npos);
    CHECK(code.find("void sprite_renderer_update(entt::registry& registry, entt::entity entity, Transform& "
                    "Transform_comp, Renderer& Renderer_comp);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: self lowers to current entity and destroy self uses recursive helper",
          "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Parent:\n"
        "    var parent: entity_id\n"
        "system Hierarchy:\n"
        "    on tick:\n"
        "        add Parent:\n"
        "            parent = self\n"
        "        destroy self\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("__value.parent = entity") != std::string::npos);
    CHECK(code.find("cactus_destroy_entity_recursive(registry, entity)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: flat transform propagation extern system is recognized", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Parent:\n"
        "    var parent: entity_id\n"
        "trait LocalTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "extern system TransformPropagation:\n"
        "    filter:\n"
        "        std.core.Parent\n"
        "        std.transform.flat.LocalTransform\n"
        "        std.transform.flat.WorldTransform\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::propagate_hierarchy(") != std::string::npos);
    CHECK(code.find("parent_world.position.x + local.position.x") != std::string::npos);
    CHECK(code.find("if (auto* parent = registry.try_get<Parent>(entity)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: hierarchy propagation handles stale parents and cycles safely", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Parent:\n"
        "    var parent: entity_id\n"
        "trait LocalTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "extern system TransformPropagation:\n"
        "    filter:\n"
        "        std.core.Parent\n"
        "        std.transform.flat.LocalTransform\n"
        "        std.transform.flat.WorldTransform\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::propagate_hierarchy(") != std::string::npos);
    CHECK(code.find("return entt::entity{entt::null};") != std::string::npos);
    CHECK(code.find("registry.all_of<LocalTransform, WorldTransform>(entity)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: volume transform propagation extern system is recognized", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Parent:\n"
        "    var parent: entity_id\n"
        "trait LocalTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "extern system TransformPropagation:\n"
        "    filter:\n"
        "        std.core.Parent\n"
        "        std.transform.volume.LocalTransform\n"
        "        std.transform.volume.WorldTransform\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("world.position = Vector3{") != std::string::npos);
    CHECK(code.find("parent_world.position.x + local.position.x") != std::string::npos);
    CHECK(code.find("cactus::runtime::stdlib::math::quat::multiply(parent_world.rotation, local.rotation)") !=
          std::string::npos);
    CHECK(code.find("parent_world.scale.z * local.scale.z") != std::string::npos);
}

TEST_CASE("Codegen EnTT: stdlib flat box colliders emit overlap runtime pass", "[codegen-entt][stdlib][physics]") {
    ProgramNode ast;
    DecoratedProgram program;
    program.ast = &ast;

    ResolvedTrait world;
    world.name = "WorldTransform";
    world.fields.push_back({.name = "position", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});
    world.fields.push_back({.name = "rotation", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    world.fields.push_back({.name = "scale", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});
    program.traits[world.name] = world;

    ResolvedTrait collider;
    collider.name      = "Collider";
    collider.is_pub    = true;
    collider.is_stdlib = true;
    collider.fields.push_back({.name = "layer", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});
    collider.fields.push_back({.name = "mask", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});
    program.traits[collider.name] = collider;

    ResolvedTrait box;
    box.name      = "BoxCollider";
    box.is_pub    = true;
    box.is_stdlib = true;
    box.fields.push_back({.name = "size", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});
    program.traits[box.name] = box;

    ResolvedTrait circle;
    circle.name      = "CircleCollider";
    circle.is_pub    = true;
    circle.is_stdlib = true;
    circle.fields.push_back({.name = "radius", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    program.traits[circle.name] = circle;

    ResolvedTrait capsule;
    capsule.name      = "CapsuleCollider";
    capsule.is_pub    = true;
    capsule.is_stdlib = true;
    capsule.fields.push_back({.name = "radius", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    capsule.fields.push_back({.name = "height", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    program.traits[capsule.name] = capsule;

    const auto code = CppEnttCodegen::generate(program);
    CHECK(code.find("cactus_dispatch_stdlib_flat_collisions") != std::string::npos);
    CHECK(code.find("registry.view<WorldTransform, Collider, BoxCollider>()") != std::string::npos);
    CHECK(code.find("cactus_collision_masks_allow") != std::string::npos);
    CHECK(code.find("dispatcher.trigger(CollisionEnterEvent") != std::string::npos);
}

TEST_CASE("Codegen EnTT: stdlib flat collider queries emit cast and overlap helpers",
          "[codegen-entt][stdlib][physics]") {
    ProgramNode ast;
    DecoratedProgram program;
    program.ast = &ast;

    ResolvedEnum kind;
    kind.name                = "QueryResultKind";
    kind.variants            = {"Empty", "Hit"};
    program.enums[kind.name] = kind;

    ResolvedStruct contact;
    contact.name                  = "QueryContact2D";
    contact.fields                = {{.name = "entity", .type = make_entity_id_type()},
                                     {.name = "normal", .type = make_vec2_type()},
                                     {.name = "distance", .type = make_float_type()},
                                     {.name = "overlap", .type = make_vec2_type()}};
    program.structs[contact.name] = contact;

    ResolvedStruct query_result;
    query_result.name   = "QueryResult2D";
    query_result.fields = {{.name = "kind", .type = {.kind = TypeKind::Enum, .name = "QueryResultKind"}},
                           {.name = "contact", .type = {.kind = TypeKind::Struct, .name = "QueryContact2D"}}};
    program.structs[query_result.name] = query_result;

    ResolvedTrait world;
    world.name = "WorldTransform";
    world.fields.push_back({.name = "position", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});
    world.fields.push_back({.name = "rotation", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    world.fields.push_back({.name = "scale", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});
    program.traits[world.name] = world;

    ResolvedTrait collider;
    collider.name      = "Collider";
    collider.is_pub    = true;
    collider.is_stdlib = true;
    collider.fields.push_back({.name = "layer", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});
    collider.fields.push_back({.name = "mask", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});
    program.traits[collider.name] = collider;

    ResolvedTrait box;
    box.name      = "BoxCollider";
    box.is_pub    = true;
    box.is_stdlib = true;
    box.fields.push_back({.name = "size", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});
    program.traits[box.name] = box;

    ResolvedTrait circle;
    circle.name      = "CircleCollider";
    circle.is_pub    = true;
    circle.is_stdlib = true;
    circle.fields.push_back({.name = "radius", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    program.traits[circle.name] = circle;

    ResolvedTrait capsule;
    capsule.name      = "CapsuleCollider";
    capsule.is_pub    = true;
    capsule.is_stdlib = true;
    capsule.fields.push_back({.name = "radius", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    capsule.fields.push_back({.name = "height", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    program.traits[capsule.name] = capsule;

    const auto code = CppEnttCodegen::generate(program);
    CHECK(code.find("QueryResult2D cactus_query_cast_nearest(entt::registry& registry") != std::string::npos);
    CHECK(code.find("if (delta.x == 0.0F && delta.y == 0.0F)") != std::string::npos);
    CHECK(code.find("if (!cactus_find_flat_collider(registry, subject_entity, subject))") != std::string::npos);
    CHECK(code.find("return cactus_empty_query_result();") != std::string::npos);
    CHECK(code.find("if (!nearest.has_value() || contact->distance < nearest->distance)") != std::string::npos);
    CHECK(code.find("entry > exit || entry < 0.0F || entry > 1.0F") != std::string::npos);
    CHECK(code.find("const float distance = cactus_flat_length(delta) * std::max(0.0F, entry)") != std::string::npos);
    CHECK(code.find("normal.y = delta.y > 0.0F ? -1.0F : 1.0F") != std::string::npos);
    CHECK(code.find("QueryResult2D cactus_query_overlap_deepest(entt::registry& registry") != std::string::npos);
    CHECK(code.find("const float amount = std::abs(contact->overlap.x) + std::abs(contact->overlap.y)") !=
          std::string::npos);
    CHECK(code.find("if (!deepest.has_value() || amount > deepest_amount)") != std::string::npos);
    CHECK(code.find("std::vector<QueryContact2D> cactus_query_overlap_all(entt::registry& registry") !=
          std::string::npos);
    CHECK(code.find("candidate.entity == exclude || !cactus_query_mask_allows(candidate, mask)") != std::string::npos);
    CHECK(code.find("contacts.push_back(*contact)") != std::string::npos);
    CHECK(code.find("return contacts;") != std::string::npos);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
