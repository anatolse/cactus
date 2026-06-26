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

static std::size_t count_occurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos   = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

static std::string generated_function(const std::string& code, const std::string& signature_fragment) {
    const auto start = code.find(signature_fragment);
    REQUIRE(start != std::string::npos);
    const auto end = code.find("\n}\n\n", start);
    REQUIRE(end != std::string::npos);
    return code.substr(start, end - start + 3);
}

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
            CHECK(code.find("view.each([&](entt::entity entity, [[maybe_unused]] Pos& Pos_comp)") != std::string::npos);
            CHECK(code.find("registry.storage<entt::entity>()") == std::string::npos);
            CHECK(code.find("cactus_try_get_projected_or_durable_Pos(registry, entity)") == std::string::npos);
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
    CHECK(code.find("move_tick(registry, dispatcher, TickEvent{dt});") != std::string::npos);
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

TEST_CASE("Codegen EnTT: std.input mouse button actions and mouse_position lower to runtime helpers",
          "[codegen-entt][input][mouse]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.input\n"
        "pub event input\n"
        "input Select: button\n"
        "    mouse = MouseButton.Left\n"
        "trait MouseState:\n"
        "    var pos: vec2\n"
        "system ReadMouse:\n"
        "    filter:\n"
        "        MouseState\n"
        "    on input:\n"
        "        if input.pressed(Select):\n"
        "            pos = input.mouse_position()\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.find("int cactus_input_button_mouse(std::uint8_t button) noexcept") != std::string::npos);
    CHECK(code.find("return MOUSE_BUTTON_LEFT;") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::pressed(action)") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::mouse_position()") != std::string::npos);
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
        "entity Cube:\n"
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
        "entity Player:\n"
        "    Pos\n"
        "    Health\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("create_player") != std::string::npos);
    CHECK(code.find("registry.create()") != std::string::npos);
    CHECK(code.find("registry.emplace<Pos>") != std::string::npos);
    CHECK(code.find("registry.emplace<Health>") != std::string::npos);
}

TEST_CASE("Codegen EnTT: composed unit creation uses flattened template traits",
          "[codegen-entt][template-composition]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Health:\n"
        "    var hp: int = 1\n"
        "    var armor: int = 0\n"
        "trait Patrol:\n"
        "    var speed: float = 0.0\n"
        "trait Persistent\n"
        "template EnemyBase:\n"
        "    Health:\n"
        "        hp = 3\n"
        "    Persistent\n"
        "template WalkerEnemy:\n"
        "    use EnemyBase\n"
        "    Health:\n"
        "        armor = 5\n"
        "    Patrol:\n"
        "        speed = 2.0\n"
        "entity Walker1:\n"
        "    use WalkerEnemy\n"
        "    Health:\n"
        "        hp = 4\n",
        program);

    const auto code    = CppEnttCodegen::generate(decorated);
    const auto unit_fn = generated_function(code, "entt::entity create_walker1");

    CHECK(count_occurrences(unit_fn, "registry.emplace<Health>(entity, component);") == 1);
    CHECK(count_occurrences(unit_fn, "registry.emplace<Patrol>(entity, component);") == 1);
    CHECK(count_occurrences(unit_fn, "registry.emplace<Persistent>(entity);") == 1);
    CHECK(unit_fn.find("component.hp") != std::string::npos);
    CHECK(unit_fn.find("= 4;") != std::string::npos);
    CHECK(unit_fn.find("component.armor") != std::string::npos);
    CHECK(unit_fn.find("= 5;") != std::string::npos);
    CHECK(unit_fn.find("component.speed") != std::string::npos);
    CHECK(unit_fn.find("= 2.0F;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: spawn of composed template constructs flattened traits once",
          "[codegen-entt][template-composition]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Health:\n"
        "    var hp: int = 1\n"
        "    var armor: int = 0\n"
        "trait Patrol:\n"
        "    var speed: float = 0.0\n"
        "template EnemyBase:\n"
        "    Health:\n"
        "        hp = 3\n"
        "template WalkerEnemy:\n"
        "    use EnemyBase\n"
        "    Patrol:\n"
        "        speed = 2.0\n"
        "system Spawner:\n"
        "    on tick:\n"
        "        spawn WalkerEnemy:\n"
        "            Health:\n"
        "                armor = 7\n",
        program);

    const auto code        = CppEnttCodegen::generate(decorated);
    const auto template_fn = generated_function(code, "entt::entity create_walker_enemy");
    const auto system_fn   = generated_function(code, "void spawner_tick");

    CHECK(count_occurrences(template_fn, "registry.emplace<Health>(entity, component);") == 1);
    CHECK(count_occurrences(template_fn, "registry.emplace<Patrol>(entity, component);") == 1);
    CHECK(template_fn.find("component.hp") != std::string::npos);
    CHECK(template_fn.find("= 3;") != std::string::npos);
    CHECK(template_fn.find("component.speed") != std::string::npos);
    CHECK(template_fn.find("= 2.0F;") != std::string::npos);

    CHECK(system_fn.find("auto __spawned = create_walker_enemy(registry);") != std::string::npos);
    CHECK(system_fn.find("auto __existing = registry.try_get<Health>(__spawned);") != std::string::npos);
    CHECK(system_fn.find("auto __value = __existing ? *__existing : Health{};") != std::string::npos);
    CHECK(system_fn.find("__value.armor = 7;") != std::string::npos);
    CHECK(system_fn.find("registry.emplace_or_replace<Health>(__spawned, __value);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: spawn expression of composed template returns created entity",
          "[codegen-entt][template-composition]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Health:\n"
        "    var hp: int = 1\n"
        "trait Patrol:\n"
        "    var speed: float = 0.0\n"
        "template EnemyBase:\n"
        "    Health:\n"
        "        hp = 3\n"
        "template WalkerEnemy:\n"
        "    use EnemyBase\n"
        "    Patrol:\n"
        "        speed = 2.0\n"
        "system Spawner:\n"
        "    on tick:\n"
        "        let spawned = spawn WalkerEnemy:\n"
        "            Patrol:\n"
        "                speed = 3.0\n",
        program);

    const auto code      = CppEnttCodegen::generate(decorated);
    const auto system_fn = generated_function(code, "void spawner_tick");

    CHECK(system_fn.find("auto spawned = ([&]()") != std::string::npos);
    CHECK(system_fn.find("auto __spawned = create_walker_enemy(registry);") != std::string::npos);
    CHECK(system_fn.find("auto __existing = registry.try_get<Patrol>(__spawned);") != std::string::npos);
    CHECK(system_fn.find("__value.speed = 3.0F;") != std::string::npos);
    CHECK(system_fn.find("return __spawned;") != std::string::npos);
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
            CHECK(code.find("void init_spawn(entt::registry& registry, const spawnEvent& spawn)") != std::string::npos);  // spawn handlers don't get dispatcher (lifecycle event)
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
            CHECK(code.find("registry.storage<entt::entity>()") == std::string::npos);
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
    contact.fields                = {{.name = "other", .type = make_entity_id_type()},
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

TEST_CASE("Codegen EnTT: bounded foreach evaluates iterable once", "[codegen-entt][foreach]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "struct Hit:\n"
        "    victim: entity_id\n"
        "event Damage:\n"
        "    amount: int\n"
        "trait Detector:\n"
        "    var hits: list[Hit]\n"
        "system Detect:\n"
        "    filter:\n"
        "        Detector\n"
        "    on tick:\n"
        "        for hit in hits:\n"
        "            emit Damage to hit.victim:\n"
        "                amount = 1\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("auto __foreach_snapshot_") != std::string::npos);
    CHECK(code.find("= Detector_comp.hits;") != std::string::npos);
    CHECK(code.find("for (const auto& hit : __foreach_snapshot_") != std::string::npos);
    CHECK(code.find("if (registry.valid(hit.victim))") != std::string::npos);
}

TEST_CASE("Codegen EnTT: projected traits use registry components in filters and clear after render",
          "[codegen-entt][project]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "trait Health:\n"
        "    var hp: int\n"
        "trait DamageFlash:\n"
        "    var intensity: float = 0.0\n"
        "system Producer:\n"
        "    filter:\n"
        "        Health\n"
        "    on tick:\n"
        "        project DamageFlash:\n"
        "            intensity = 1.0\n"
        "system Consumer:\n"
        "    filter:\n"
        "        Health\n"
        "        DamageFlash as flash\n"
        "    exclude:\n"
        "        Suppressed\n"
        "    after:\n"
        "        Producer\n"
        "    on tick:\n"
        "        hp = hp - 1\n"
        "trait Suppressed\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("std::vector<entt::entity> projected_DamageFlash_entities") != std::string::npos);
    CHECK(code.find("std::unordered_map<entt::entity, std::optional<DamageFlash>> projected_DamageFlash_previous") !=
          std::string::npos);
    CHECK(code.find("auto& __projected = project_DamageFlash(registry, entity)") != std::string::npos);
    CHECK(code.find("__projected.intensity = 1.0F") != std::string::npos);
    CHECK(code.find("registry.view<Health, DamageFlash>(entt::exclude<Suppressed>)") != std::string::npos);
    CHECK(code.find("auto& flash = DamageFlash_comp") != std::string::npos);
    CHECK(code.find("cactus_projected_DamageFlash") == std::string::npos);
    CHECK(code.find("cactus_try_get_projected_or_durable_DamageFlash") == std::string::npos);
    CHECK(code.find("cactus_has_projected_or_durable_Suppressed") == std::string::npos);
    CHECK(code.find("clear_projected_traits(registry);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: filtered systems use native views and no early-return guards", "[codegen-entt][project]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "trait Wanted\n"
        "trait Blocked\n"
        "system Consumer:\n"
        "    filter:\n"
        "        Wanted\n"
        "    exclude:\n"
        "        Blocked\n"
        "    on tick:\n"
        "        let seen = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.view<Wanted>(entt::exclude<Blocked>)") != std::string::npos);
            CHECK(code.find("registry.storage<entt::entity>()") == std::string::npos);
            CHECK(code.find("return;") == std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: projected trait cleanup restores durable or removes projected-only components",
          "[codegen-entt][project]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "trait Health:\n"
        "    var hp: int\n"
        "trait Flash:\n"
        "    var amount: int = 0\n"
        "system Producer:\n"
        "    filter:\n"
        "        Health\n"
        "    on tick:\n"
        "        project Flash:\n"
        "            amount = 2\n"
        "        project Flash:\n"
        "            amount = 3\n"
        "        remove Flash\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("if (projected_Flash_previous.contains(entity))") != std::string::npos);
    CHECK(code.find("projected_Flash_previous.emplace(entity, *previous)") != std::string::npos);
    CHECK(code.find("projected_Flash_previous.emplace(entity, std::nullopt)") != std::string::npos);
    CHECK(code.find("registry.emplace_or_replace<Flash>(entity, *previous_it->second)") != std::string::npos);
    CHECK(code.find("registry.remove<Flash>(entity)") != std::string::npos);
    CHECK(code.find("cancel_projected_Flash(entity)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: projected marker traits avoid value snapshots", "[codegen-entt][project]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "trait Actor\n"
        "trait Grounded\n"
        "system Producer:\n"
        "    filter:\n"
        "        Actor\n"
        "    on tick:\n"
        "        project Grounded\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("std::unordered_map<entt::entity, bool> projected_Grounded_previous") != std::string::npos);
    CHECK(code.find("projected_Grounded_previous.emplace(entity, registry.all_of<Grounded>(entity))") !=
          std::string::npos);
    CHECK(code.find("void project_Grounded(entt::registry& registry, entt::entity entity)") != std::string::npos);
    CHECK(code.find("project_Grounded(registry, entity);") != std::string::npos);
    CHECK(code.find("registry.try_get<Grounded>") == std::string::npos);
    CHECK(code.find("std::optional<Grounded>") == std::string::npos);
}

// ── std.text.format codegen tests (add-stdlib-text-format) ───────────────────

TEST_CASE("Codegen EnTT: aliased std.text.format lowers to std::format in system handler",
          "[codegen-entt][std-text-format]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.text as text\n"
        "event tick:\n"
        "    dt: float\n"
        "trait Score:\n"
        "    var value: int\n"
        "system Display:\n"
        "    filter:\n"
        "        Score\n"
        "    on tick:\n"
        "        let s = text.format(\"Score: {}\", value)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("std::format(") != std::string::npos);
    CHECK(code.find("\"Score: {}\"") != std::string::npos);
}

TEST_CASE("Codegen EnTT: std.text import causes format header to be emitted", "[codegen-entt][std-text-format]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.text as text\n"
        "event tick:\n"
        "    dt: float\n"
        "trait Score:\n"
        "    var value: int\n"
        "system Display:\n"
        "    filter:\n"
        "        Score\n"
        "    on tick:\n"
        "        let s = text.format(\"Score: {}\", value)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("#include <format>") != std::string::npos);
}

TEST_CASE("Codegen EnTT: no std.text import means no format header", "[codegen-entt][std-text-format]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Score:\n"
        "    var value: int\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("#include <format>") == std::string::npos);
}

TEST_CASE("Codegen EnTT: unaliased std.text format lowers to std::format in system handler",
          "[codegen-entt][std-text-format]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.text\n"
        "event tick:\n"
        "    dt: float\n"
        "trait Score:\n"
        "    var value: int\n"
        "system Display:\n"
        "    filter:\n"
        "        Score\n"
        "    on tick:\n"
        "        let s = format(\"Score: {}\", value)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("std::format(") != std::string::npos);
}

// ── Task 3.5: Template-backed entity backend tests ────────────────────────────

TEST_CASE("Codegen EnTT: template-backed entity emits template components plus overrides", "[codegen-entt][entity]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Shape:\n"
        "    var size: float = 16.0\n"
        "trait Collectible:\n"
        "    var point_value: int = 10\n"
        "trait WorldTransform:\n"
        "    var x: float = 0.0\n"
        "template BlueGem:\n"
        "    Shape:\n"
        "        size = 16.0\n"
        "    Collectible:\n"
        "        point_value = 10\n"
        "    WorldTransform\n"
        "entity Gem1 from BlueGem:\n"
        "    WorldTransform:\n"
        "        x = 250.0\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    // Should have a create function for Gem1 that emplaces Shape, Collectible, and WorldTransform
    CHECK(code.find("create_gem1") != std::string::npos);
    CHECK(code.find("emplace<Shape>") != std::string::npos);
    CHECK(code.find("emplace<Collectible>") != std::string::npos);
    CHECK(code.find("emplace<WorldTransform>") != std::string::npos);
}

TEST_CASE("Codegen EnTT: mixed entity creation order preserved", "[codegen-entt][entity]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Tag:\n"
        "    var v: int = 0\n"
        "template T:\n"
        "    Tag:\n"
        "        v = 1\n"
        "entity A:\n"
        "    Tag\n"
        "entity B from T:\n"
        "    Tag:\n"
        "        v = 2\n"
        "entity C:\n"
        "    Tag\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    // A, B, C should appear in that order in the init function
    const auto pos_a = code.find("create_a(registry)");
    const auto pos_b = code.find("create_b(registry)");
    const auto pos_c = code.find("create_c(registry)");
    REQUIRE(pos_a != std::string::npos);
    REQUIRE(pos_b != std::string::npos);
    REQUIRE(pos_c != std::string::npos);
    CHECK(pos_a < pos_b);
    CHECK(pos_b < pos_c);
}
// ── std.editor codegen tests (add-std-editor) ──────────────────────────────

TEST_CASE("Codegen EnTT: editor.cactus module generates EditorState component, Editor entity, extern system stubs",
          "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
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
        "    var focused_field: string = \"\"\n"
        "pub entity Editor:\n"
        "    EditorState\n"
        "pub trait EditorSelected\n"
        "pub trait EditorSnap:\n"
        "    var position_snap: float = 0.0\n"
        "    var rotation_snap: float = 0.0\n"
        "    var scale_snap: float = 0.0\n"
        "pub event EditorSelectionChanged:\n"
        "    previous: entity_id\n"
        "    current: entity_id\n"
        "pub extern func editor_hit_test_2d(screen_pos: vec2, mask: int) entity_id\n"
        "pub extern system EditorTemplatePalette:\n"
        "    filter:\n"
        "        EditorState\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    // EditorState should have a component struct
    CHECK(code.find("struct EditorState") != std::string::npos);
    // Editor entity should be created
    CHECK(code.find("create_editor") != std::string::npos);
    // EditorSelected marker trait should be registered
    CHECK(code.find("struct EditorSelected") != std::string::npos);
    // EditorSnap should have fields
    CHECK(code.find("struct EditorSnap") != std::string::npos);
    // EditorSelectionChanged event should have a struct
    CHECK(code.find("struct EditorSelectionChanged") != std::string::npos);
    // EditorTemplatePalette extern system stub (snake_case name)
    CHECK(code.find("editor_template_palette_tick") != std::string::npos);
    // HexColor for #00FF00FF is not in this test, but HexColor conversion path should exist
}

TEST_CASE("Codegen EnTT: editor projected traits generate registry helpers", "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
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
        "        project EditorGizmo2D:\n"
        "            mode = 1\n"
        "            color = #00FF00FF\n"
        "            size = 1.0\n"
        "        project EditorGizmo3D:\n"
        "            mode = 1\n"
        "            color = #00FF00FF\n"
        "            size = 1.0\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    // Should generate projected trait helper for EditorGizmo2D
    CHECK(code.find("project_EditorGizmo2D") != std::string::npos);
    CHECK(code.find("registry.emplace<EditorGizmo2D>") != std::string::npos);
    // Should generate projected trait helper for EditorGizmo3D
    CHECK(code.find("project_EditorGizmo3D") != std::string::npos);
    CHECK(code.find("registry.emplace<EditorGizmo3D>") != std::string::npos);
    // Should have the general projected trait cleanup call
    CHECK(code.find("clear_projected_traits(registry)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: editor extern systems generate correct dispatch calls",
          "[codegen-entt][stdlib][editor][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: int = 0\n"
        "pub trait EditorGizmo2D:\n"
        "    var mode: int = 1\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "pub extern system EditorTemplatePalette:\n"
        "    filter:\n"
        "        EditorState\n"
        "pub extern system EditorPropertyPanel:\n"
        "    filter:\n"
        "        EditorState\n"
        "pub extern system GizmoRenderer2D:\n"
        "    filter:\n"
        "        EditorGizmo2D\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    // EditorTemplatePalette should have a tick call (snake_case names)
    CHECK(code.find("editor_template_palette_tick") != std::string::npos);
    // EditorPropertyPanel should have a tick call
    CHECK(code.find("editor_property_panel_tick") != std::string::npos);
    // EditorTemplatePalette and EditorPropertyPanel extern system stubs are generated
    // (GizmoRenderer2D dispatch requires WorldTransform in filter + stdlib contract flag,
    //  which are tested in the actual editor-test.cactus example via test_example_cpp_compilation)
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
