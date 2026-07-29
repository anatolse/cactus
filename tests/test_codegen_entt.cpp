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
#include "backends/cpp-entt/type_utils.hpp"

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

// Returns true when source already starts with a module declaration.
static bool starts_with_module_decl(const std::string& src) {
    const auto first = src.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || src.compare(first, 6, "module") != 0) {
        return false;
    }
    const auto after = first + 6;
    return after < src.size() && std::isspace(static_cast<unsigned char>(src[after])) != 0;
}

// Erase canonical identity from a resolved declaration map so the codegen uses
// the (now-empty) module_name and produces unqualified C++ names. Tests that
// want a specific module prefix assign trait.module_name themselves after calling
// full_pipeline.  canonical_id is kept so find_decl_by_symbol can still locate
// entries when looking up by resolved SymbolId from AST nodes.
template <typename Map>
static void clear_module_identity(Map& map) {
    for (auto& [key, decl] : map) {
        decl.module_name = "";
        if (decl.symbol_id.has_value()) {
            decl.symbol_id->module.name = "";
        }
    }
}

// Minimal std.input pub-symbol surface for tests whose sources declare input
// bindings. Registered under alias "inp"; the canonical path "std.input" also
// resolves through the unified resolver's canonical-qualifier support.
static ModuleImports std_input_imports() {
    ImportedSymbols syms;
    syms.module_name    = "std.input";
    const auto add_enum = [&syms](const std::string& name, std::vector<std::string> variants) {
        ResolvedEnum enm;
        enm.name         = name;
        enm.module_name  = "std.input";
        enm.symbol_id    = make_symbol_id(SymbolKind::Enum, "std.input", name);
        enm.canonical_id = make_canonical_id(*enm.symbol_id);
        enm.variants     = std::move(variants);
        syms.enums[name] = std::move(enm);
    };
    add_enum("Key",
             {"A",     "B",     "C",    "D",    "E",      "F",        "G",     "H",     "I",      "J",     "K",
              "L",     "M",     "N",    "O",    "P",      "Q",        "R",     "S",     "T",      "U",     "V",
              "W",     "X",     "Y",    "Z",    "Zero",   "One",      "Two",   "Three", "Four",   "Five",  "Six",
              "Seven", "Eight", "Nine", "Left", "Right",  "Up",       "Down",  "Space", "Escape", "Enter", "Backspace",
              "Tab",   "Shift", "Ctrl", "Alt",  "PageUp", "PageDown", "Minus", "Equal", "F1",     "F2",    "F3",
              "F4",    "F5",    "F6",   "F7",   "F8",     "F9",       "F10",   "F11",   "F12"});
    add_enum("MouseButton", {"Left", "Right", "Middle", "Side", "Extra"});
    add_enum("GamepadButton",
             {"South",
              "North",
              "East",
              "West",
              "L1",
              "L2",
              "R1",
              "R2",
              "Start",
              "Select",
              "L3",
              "R3",
              "DPadUp",
              "DPadDown",
              "DPadLeft",
              "DPadRight"});
    add_enum("GamepadAxis", {"LeftX", "LeftY", "RightX", "RightY", "L2Axis", "R2Axis"});
    ModuleImports imports;
    imports.add("inp", std::move(syms));
    return imports;
}

static DecoratedProgram full_pipeline_from_file(const std::string& source,
                                                const std::string& filename,
                                                ProgramNode& program_out,
                                                const ModuleImports& imports = ModuleImports{}) {
    const std::string src = starts_with_module_decl(source) ? source : "module test\n" + source;
    ErrorReporter errors;
    Lexer lexer(src, filename, errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    program_out = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(program_out, imports);
    REQUIRE_FALSE(errors.has_errors());
    // Clear module identity so codegen produces unqualified C++ names.
    // Tests that need qualified names assign module_name explicitly after this call.
    result.module_name = "";
    clear_module_identity(result.traits);
    clear_module_identity(result.structs);
    clear_module_identity(result.enums);
    clear_module_identity(result.funcs);
    return result;
}

static DecoratedProgram full_pipeline(const std::string& source,
                                      ProgramNode& program_out,
                                      const ModuleImports& imports = ModuleImports{}) {
    return full_pipeline_from_file(source, "test.cactus", program_out, imports);
}

TEST_CASE("Codegen EnTT: component struct from trait", "[codegen-entt]") {
    ResolvedTrait trait;
    trait.name        = "Position";
    trait.module_name = "test.codegen";
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
    CHECK(code.find("struct test_codegen__Position") != std::string::npos);
    CHECK(code.find("float x{}") != std::string::npos);
    CHECK(code.find("float y{}") != std::string::npos);
    // Should NOT have std::vector (that's SoA)
    CHECK(code.find("std::vector") == std::string::npos);
}

TEST_CASE("cpp-entt lowers external handlers to canonical contract-shaped callbacks") {
    const auto source = R"(
module game

pub extern event frame:
    dt: float

event Contact:
    amount: int

trait Position:
    var value: float

trait Velocity:
    var value: float

trait Disabled

template Particle:
    Position:
        value = 0.0

phase simulate:
    from:
        frame

extern system Integrate:
    filter:
        Position
        Velocity
    on simulate:
        reads:
            Position
        writes:
            Velocity
        emits:
            Contact
        commands:
            spawn Particle
            destroy
            add Disabled
            remove Disabled
        effects:
            physics

extern system Monitor:
    on simulate:
        effects:
            host.observe
)";
    ProgramNode ast;
    auto program = full_pipeline(source, ast);

    const auto generated = CppEnttCodegen::generate(program);

    CHECK(generated.find("struct CactusCapabilities__game__Integrate__on__game__simulate") != std::string::npos);
    CHECK(generated.find("void emit_game__Contact(ContactEvent occurrence) const") != std::string::npos);
    CHECK(generated.find("[[nodiscard]] entt::entity command_spawn_game__Particle() const") != std::string::npos);
    CHECK(generated.find("void command_destroy(entt::entity target) const") != std::string::npos);
    CHECK(generated.find("void command_add_game__Disabled(entt::entity target, game__Disabled value = {}) const") !=
          std::string::npos);
    CHECK(generated.find("void command_remove_game__Disabled(entt::entity target) const") != std::string::npos);
    CHECK(generated.find("CactusEffectService effect_physics() const noexcept") != std::string::npos);
    CHECK(generated.find("void cactus_external__game__Integrate__on__game__simulate(") != std::string::npos);
    CHECK(generated.find("entt::entity entity") != std::string::npos);
    CHECK(generated.find("const game__Position& read_game__Position") != std::string::npos);
    CHECK(generated.find("game__Velocity& write_game__Velocity") != std::string::npos);
    CHECK(generated.find("for (const auto entity : registry.view<game__Position, game__Velocity>())") !=
          std::string::npos);
    CHECK(generated.find("::cactus_external__game__Integrate__on__game__simulate(") != std::string::npos);
    CHECK(generated.find("CactusCapabilities__game__Integrate__on__game__simulate{registry}") != std::string::npos);

    const auto integrate_capabilities =
        generated.find("struct CactusCapabilities__game__Integrate__on__game__simulate");
    REQUIRE(integrate_capabilities != std::string::npos);
    const auto integrate_capabilities_end = generated.find("\n};\n\n", integrate_capabilities);
    REQUIRE(integrate_capabilities_end != std::string::npos);
    const auto integrate_surface =
        generated.substr(integrate_capabilities, integrate_capabilities_end - integrate_capabilities);
    CHECK(integrate_surface.find("std::function") == std::string::npos);
    CHECK(integrate_surface.find("emit_game__Contact") != std::string::npos);
    CHECK(integrate_surface.find("effect_physics") != std::string::npos);
    CHECK(integrate_surface.find("effect_host__observe") == std::string::npos);

    CHECK(generated.find("void cactus_external__game__Monitor__on__game__simulate(") != std::string::npos);
    CHECK(generated.find("::cactus_external__game__Monitor__on__game__simulate(") != std::string::npos);
    const auto monitor_capabilities = generated.find("struct CactusCapabilities__game__Monitor__on__game__simulate");
    REQUIRE(monitor_capabilities != std::string::npos);
    const auto monitor_capabilities_end = generated.find("\n};\n\n", monitor_capabilities);
    REQUIRE(monitor_capabilities_end != std::string::npos);
    const auto monitor_surface =
        generated.substr(monitor_capabilities, monitor_capabilities_end - monitor_capabilities);
    CHECK(monitor_surface.find("emit_") == std::string::npos);
    CHECK(monitor_surface.find("command_") == std::string::npos);
    CHECK(monitor_surface.find("effect_host__observe") != std::string::npos);
    CHECK(monitor_surface.find("effect_physics") == std::string::npos);
    CHECK(generated.find("void game__Integrate__tick(entt::registry& registry)") == std::string::npos);
    CHECK(generated.find("void game__Monitor__tick(entt::registry& registry)") == std::string::npos);
}

TEST_CASE("Codegen EnTT: stdlib collider components keep authored defaults", "[codegen-entt][stdlib][physics]") {
    ResolvedTrait collider;
    collider.name        = "Collider";
    collider.module_name = "std.physics.flat";
    collider.is_pub      = true;
    collider.is_stdlib   = true;
    collider.fields.push_back({.name = "layer", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});
    collider.fields.push_back({.name = "mask", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});

    const auto collider_code = EnttComponentEmitter::emit_component(collider);
    CHECK(collider_code.find("int layer{1};") != std::string::npos);
    CHECK(collider_code.find("int mask{1};") != std::string::npos);

    ResolvedTrait box;
    box.name        = "BoxCollider";
    box.module_name = "std.physics.flat";
    box.is_pub      = true;
    box.is_stdlib   = true;
    box.fields.push_back({.name = "size", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});

    const auto box_code = EnttComponentEmitter::emit_component(box);
    CHECK(box_code.find("Vector2 size{.x = 1.0F, .y = 1.0F};") != std::string::npos);
}

TEST_CASE("Codegen EnTT: registry view system", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event step:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "    var y: float\n"
        "system Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on step:\n"
        "        x = x + step.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("void move_step") != std::string::npos);
            CHECK(code.find("entt::registry& registry") != std::string::npos);
            CHECK(code.find("const stepEvent& step") != std::string::npos);
            CHECK(code.find("Pos_comp.x = (Pos_comp.x + step.dt)") != std::string::npos);
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

            DecoratedProgram prog;
            auto sink = EnttEventEmitter::emit_sink_connection(*event, prog);
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
    CHECK(code.find("move_tick(registry") == std::string::npos);
    CHECK(code.find("struct TickEvent") == std::string::npos);
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
        "use std.input as inp\n"
        "pub event input\n"
        "input Select: button\n"
        "    mouse = inp.MouseButton.Left\n"
        "trait MouseState:\n"
        "    var pos: vec2\n"
        "system ReadMouse:\n"
        "    filter:\n"
        "        MouseState\n"
        "    on input:\n"
        "        if input.pressed(Select):\n"
        "            pos = input.mouse_position()\n",
        program,
        std_input_imports());

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.find("int cactus_input_button_mouse(std::uint8_t button) noexcept") != std::string::npos);
    CHECK(code.find("return MOUSE_BUTTON_LEFT;") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::pressed(action)") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::mouse_position()") != std::string::npos);
}

// ── Golden input-binding tests (unified-name-resolution change, task 1.8) ──
// Guard against the dead-input regression where unmatched binding spellings
// silently emitted `0`/`-1` constants.

TEST_CASE("Codegen EnTT: axis bindings emit raylib key constants, never dead zeros", "[codegen-entt][input][golden]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.input as inp\n"
        "pub event input\n"
        "input MoveX: axis\n"
        "    negative = inp.Key.A\n"
        "    positive = inp.Key.D\n"
        "input MoveY: axis\n"
        "    negative = inp.Key.W\n"
        "    positive = inp.Key.S\n",
        program,
        std_input_imports());

    const auto code = CppEnttCodegen::generate(decorated);
    const auto axis = generated_function(code, "float cactus_input_axis_value");
    CHECK(axis.find("KEY_A") != std::string::npos);
    CHECK(axis.find("KEY_D") != std::string::npos);
    CHECK(axis.find("KEY_W") != std::string::npos);
    CHECK(axis.find("KEY_S") != std::string::npos);
    // The dead-input fallback shape must never appear.
    CHECK(axis.find("return 0 - 0;") == std::string::npos);
}

TEST_CASE("Codegen EnTT: button key and mouse bindings emit raylib constants", "[codegen-entt][input][golden]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.input as inp\n"
        "pub event input\n"
        "input Jump: button\n"
        "    key = inp.Key.Space\n"
        "input Fire: button\n"
        "    mouse = inp.MouseButton.Left\n",
        program,
        std_input_imports());

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("return KEY_SPACE;") != std::string::npos);
    CHECK(code.find("return MOUSE_BUTTON_LEFT;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: alias and canonical binding spellings generate identical code",
          "[codegen-entt][input][golden][canonical]") {
    ProgramNode alias_program;
    auto alias_decorated = full_pipeline(
        "use std.input as inp\n"
        "pub event input\n"
        "input Jump: button\n"
        "    key = inp.Key.Space\n",
        alias_program,
        std_input_imports());
    const auto alias_code = CppEnttCodegen::generate(alias_decorated);

    ProgramNode canonical_program;
    auto canonical_decorated = full_pipeline(
        "use std.input as inp\n"
        "pub event input\n"
        "input Jump: button\n"
        "    key = std.input.Key.Space\n",
        canonical_program,
        std_input_imports());
    const auto canonical_code = CppEnttCodegen::generate(canonical_decorated);

    CHECK(alias_code == canonical_code);
    CHECK(alias_code.find("return KEY_SPACE;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: extern func generates runtime header include", "[codegen-entt][extern-func]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern func lerp(a: float, b: float, t: float) float\n"
        "trait Pos:\n    var x: float\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("#include \"common/cactus_runtime.hpp\"") != std::string::npos);
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
        "    key = std.input.Key.Space\n"
        "trait Controller:\n"
        "    var active: bool = false\n"
        "system Demo:\n"
        "    filter:\n"
        "        Controller\n"
        "    on tick:\n"
        "        active = down(Jump)\n",
        program,
        std_input_imports());

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
        "module std.render.sprites\n"
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
        "event run\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n"
        "extern system AnimatedSpriteSystem:\n"
        "    filter:\n"
        "        AnimatedSprite\n"
        "    on run:\n"
        "        writes:\n"
        "            AnimatedSprite\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::submit_sprite(") != std::string::npos);
    CHECK(code.find("Renderer_comp.layer") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::advance_animated_sprite(") != std::string::npos);
}

TEST_CASE("Codegen EnTT: render glue brackets frames without inferring generic event dispatch",
          "[codegen-entt][assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.sprites\n"
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
        "event run\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::begin_render_frame();") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::end_render_frame();") != std::string::npos);
    const auto render = generated_function(code, "void generated_render_project");
    CHECK(render.find("sprite_renderer_tick(registry);") == std::string::npos);
}

TEST_CASE("Codegen EnTT: mesh renderer extern system binds to backend runtime without user callback",
          "[codegen-entt][assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.meshes\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait Renderer:\n"
        "    let mesh: mesh_id\n"
        "    let material: material_id\n"
        "    var visible: bool\n"
        "    var cast_shadow: bool\n"
        "event run\n"
        "extern system MeshRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::submit_mesh(") != std::string::npos);
    CHECK(code.find("void mesh_renderer_update(") == std::string::npos);
}

TEST_CASE("Codegen EnTT: model renderer extern system binds to backend runtime without user callback",
          "[codegen-entt][assets][dsl-model-assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.models\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait ModelRenderer:\n"
        "    let model: model_id\n"
        "    var visible: bool\n"
        "    var cast_shadow: bool\n"
        "event run\n"
        "extern system ModelRendererSystem:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        ModelRenderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            ModelRenderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::submit_model(") != std::string::npos);
    CHECK(code.find("void model_renderer_system_update(") == std::string::npos);
    // No ModelAnimator trait in the program: the plain submission path only.
    CHECK(code.find("try_get<ModelAnimator>") == std::string::npos);
}

TEST_CASE("Codegen EnTT: model animation extern adapter advances time without inferred update dispatch",
          "[codegen-entt][assets][dsl-model-animation]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.models\n"
        "trait ModelRenderer:\n"
        "    let model: model_id\n"
        "    var visible: bool\n"
        "    var cast_shadow: bool\n"
        "trait ModelAnimator:\n"
        "    var clip: int\n"
        "    var playing: bool\n"
        "    var speed: float\n"
        "    var time: float\n"
        "event run\n"
        "extern system ModelAnimationSystem:\n"
        "    filter:\n"
        "        ModelRenderer\n"
        "        ModelAnimator\n"
        "    on run:\n"
        "        reads:\n"
        "            ModelRenderer\n"
        "        writes:\n"
        "            ModelAnimator\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void model_animation_system_tick");
    CHECK(tick.find("registry.view<ModelRenderer, ModelAnimator>()") != std::string::npos);
    CHECK(tick.find("if (!ModelAnimator_comp.playing)") != std::string::npos);
    CHECK(tick.find("ModelAnimator_comp.time += kFixedDt * ModelAnimator_comp.speed;") != std::string::npos);
    CHECK(tick.find("cactus::runtime::entt_backend::model_animation_duration(") != std::string::npos);
    CHECK(tick.find("std::fmod(ModelAnimator_comp.time, duration)") != std::string::npos);

    // The adapter owns time advancement, but a generic event name no longer
    // causes the backend to infer update or render scheduling.
    const auto update = generated_function(code, "void generated_update_project");
    CHECK(update.find("model_animation_system_tick(registry);") == std::string::npos);
    const auto render = generated_function(code, "void generated_render_project");
    CHECK(render.find("model_animation_system_tick") == std::string::npos);
}

TEST_CASE("Codegen EnTT: model renderer submits animator clip and time when the entity carries one",
          "[codegen-entt][assets][dsl-model-animation]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.models\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait ModelRenderer:\n"
        "    let model: model_id\n"
        "    var visible: bool\n"
        "    var cast_shadow: bool\n"
        "trait ModelAnimator:\n"
        "    var clip: int\n"
        "    var playing: bool\n"
        "    var speed: float\n"
        "    var time: float\n"
        "event run\n"
        "extern system ModelRendererSystem:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        ModelRenderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            ModelRenderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void model_renderer_system_tick");
    // Animated entities go through the extended submit_model signature...
    CHECK(tick.find("registry.try_get<ModelAnimator>(entity)") != std::string::npos);
    CHECK(tick.find("animator->clip, animator->time);") != std::string::npos);
    // ...while entities without ModelAnimator keep the plain submission.
    CHECK(tick.find("ModelRenderer_comp.cast_shadow);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: screen label extern system renders window-space text in flat and volume programs",
          "[codegen-entt][assets][dsl-model-animation]") {
    const std::string screen_label_decls =
        "trait ScreenLabel:\n"
        "    var text: string\n"
        "    var position: vec2\n"
        "    var font_size: int\n"
        "    var color: color\n"
        "    var visible: bool\n"
        "event run\n"
        "extern system ScreenLabelSystem:\n"
        "    filter:\n"
        "        ScreenLabel\n"
        "    on run:\n"
        "        reads:\n"
        "            ScreenLabel\n";

    const std::string flat_transform =
        "module std.render.text\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n";
    const std::string volume_transform =
        "module std.render.text\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n";

    for (const auto& transform : {flat_transform, volume_transform}) {
        ProgramNode program;
        auto decorated = full_pipeline(transform + screen_label_decls, program);

        const auto code = CppEnttCodegen::generate(decorated);
        const auto tick = generated_function(code, "void screen_label_system_tick");
        // No WorldTransform in the view and no flavor gating: the same
        // emission serves flat and volume programs (dsl-model-animation D5).
        CHECK(tick.find("registry.view<ScreenLabel>()") != std::string::npos);
        CHECK(tick.find("cactus::runtime::entt_backend::submit_screen_label(ScreenLabel_comp.position, "
                        "ScreenLabel_comp.font_size, ScreenLabel_comp.color, ScreenLabel_comp.text, "
                        "ScreenLabel_comp.visible);") != std::string::npos);
        CHECK(tick.find("(void)registry;") == std::string::npos);

        // Generic event names do not imply update or render scheduling. A
        // linked phase graph is the sole source of runtime dispatch order.
        const auto render = generated_function(code, "void generated_render_project");
        CHECK(render.find("screen_label_system_tick(registry);") == std::string::npos);
        const auto update = generated_function(code, "void generated_update_project");
        CHECK(update.find("screen_label_system_tick") == std::string::npos);
    }
}

TEST_CASE("Codegen EnTT: std.render.models introspection funcs bind to model-prefixed runtime bridges",
          "[codegen-entt][extern-func][stdlib][dsl-model-animation]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.render.models as models\n"
        "asset Robot: model = \"art/robot.glb\"\n"
        "event tick:\n"
        "    dt: float\n"
        "trait ClipState:\n"
        "    var clips: int\n"
        "    var label: string\n"
        "system Probe:\n"
        "    filter:\n"
        "        ClipState\n"
        "    on tick:\n"
        "        clips = models.animation_count(Robot)\n"
        "        label = models.animation_name(Robot, clips - 1)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::model_animation_count(Robot)") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::model_animation_name(Robot, ") != std::string::npos);
}

TEST_CASE("Codegen EnTT: models.bounds_size binds to the model-prefixed runtime bridge",
          "[codegen-entt][extern-func][stdlib][dynamic-model-spawning]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.render.models as models\n"
        "asset Robot: model = \"art/robot.glb\"\n"
        "event tick:\n"
        "    dt: float\n"
        "trait SizeState:\n"
        "    var height: float\n"
        "system Probe:\n"
        "    filter:\n"
        "        SizeState\n"
        "    on tick:\n"
        "        height = models.bounds_size(Robot).y\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::model_bounds_size(Robot)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: lifecycle-named events do not create implicit startup dispatch",
          "[codegen-entt][dsl-scene-loading][dynamic-model-spawning]") {
    ProgramNode program;
    // `pub event load` stands in for the std.core lifecycle declaration the
    // multi-module pipeline links in; the single-module analyzer needs it to
    // accept `on load:`. It also exercises the loadEvent dedupe: the event
    // declaration supplies the struct, so the empty marker must not be emitted.
    auto decorated = full_pipeline(
        "pub event load\n"
        "trait Marker\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n"
        "entity Bootstrap:\n"
        "    Marker\n"
        "system SpawnEnemies:\n"
        "    filter:\n"
        "        Marker\n"
        "    on load:\n"
        "        spawn Enemy:\n"
        "            Position:\n"
        "                x = 1.0\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(count_occurrences(code, "struct loadEvent") == 1);

    // The compatibility hook remains inert; only graph roots activate handlers.
    const auto load = generated_function(code, "void generated_load_project");
    CHECK(load.find("spawn_enemies_load") == std::string::npos);

    // main() startup order: init project, then load phase, then the frame loop.
    const auto init_pos = code.find("cactus::runtime::entt_backend::generated_init_project(registry);");
    const auto load_pos = code.find("cactus::runtime::entt_backend::generated_load_project(registry);");
    const auto loop_pos = code.find("while (!WindowShouldClose())");
    REQUIRE(init_pos != std::string::npos);
    REQUIRE(load_pos != std::string::npos);
    REQUIRE(loop_pos != std::string::npos);
    CHECK(init_pos < load_pos);
    CHECK(load_pos < loop_pos);
}

TEST_CASE("Codegen EnTT: programs without load handlers emit no loadEvent marker",
          "[codegen-entt][dsl-scene-loading][dynamic-model-spawning]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float = 0.0\n"
        "system Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("struct loadEvent") == std::string::npos);
    // The hook is still exported so main() and no-main hosts can call it.
    const auto load = generated_function(code, "void generated_load_project");
    CHECK(load.find("(void)registry;") != std::string::npos);
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

TEST_CASE("Codegen EnTT: generated init registers declared model assets", "[codegen-entt][assets][dsl-model-assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "asset Robot: model = \"art/robot.glb\"\n"
        "trait Marker\n"
        "entity Bot:\n"
        "    Marker\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("shared_asset_registry().register_model(Robot, \"art/robot.glb\", "
                    "static_cast<int>(Robot));") != std::string::npos);
}

TEST_CASE("Codegen EnTT: asset paths resolve relative to the declaring module directory",
          "[codegen-entt][assets][dsl-model-assets]") {
    ProgramNode program;
    auto decorated = full_pipeline_from_file(
        "asset Robot: model = \"art/robot.glb\"\n"
        "asset PlayerMesh: mesh = \"../shared/player.mesh\"\n"
        "trait Marker\n"
        "entity Bot:\n"
        "    Marker\n",
        "examples/robot/game.cactus",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("shared_asset_registry().register_model(Robot, \"examples/robot/art/robot.glb\", "
                    "static_cast<int>(Robot));") != std::string::npos);
    CHECK(code.find("shared_asset_registry().register_mesh(PlayerMesh, \"examples/shared/player.mesh\", "
                    "static_cast<int>(PlayerMesh));") != std::string::npos);
}

TEST_CASE("Codegen EnTT: imported pub asset keeps its declaring module's path base",
          "[codegen-entt][assets][dsl-model-assets]") {
    // Mirrors the multi-module pipeline: the merged codegen AST holds each
    // module's declarations with their original source locations, so an asset
    // imported from module A resolves against A's directory, not the importer's.
    ErrorReporter errors;
    Lexer lexer_a("module shared\npub asset SharedModel: model = \"art/m.glb\"\n", "mods/shared/assets.cactus", errors);
    Parser parser_a(lexer_a.tokenize(), errors);
    auto prog_a = parser_a.parse_program();
    REQUIRE_FALSE(errors.has_errors());

    Lexer lexer_b(
        "module game\n"
        "trait Marker\n"
        "entity Bot:\n"
        "    Marker\n",
        "game/main.cactus",
        errors);
    Parser parser_b(lexer_b.tokenize(), errors);
    auto merged = parser_b.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    for (auto& decl : prog_a.declarations) {
        if (!std::holds_alternative<ModuleNode>(decl)) {
            merged.declarations.push_back(std::move(decl));
        }
    }

    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(merged);
    REQUIRE_FALSE(errors.has_errors());

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("shared_asset_registry().register_model(SharedModel, \"mods/shared/art/m.glb\", "
                    "static_cast<int>(SharedModel));") != std::string::npos);
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
        "    var stage: int\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.stage\n",
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
        "    var stage: int\n"
        "trait Spike\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.stage\n"
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

TEST_CASE("Codegen EnTT: aliased event handler uses alias in signature and body", "[codegen-entt][event-handler]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event step:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "system Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on step as s:\n"
        "        x = x + s.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("const stepEvent& s") != std::string::npos);
            CHECK(code.find("Pos_comp.x = (Pos_comp.x + s.dt)") != std::string::npos);
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
            CHECK(code.find("void init_spawn(entt::registry& registry, const spawnEvent& spawn)") !=
                  std::string::npos);  // spawn handlers don't get dispatcher (lifecycle event)
        }
    }
}

TEST_CASE("Codegen EnTT: trait match without wildcard emits no else", "[codegen-entt][trait-match]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Collision:\n"
        "    other: entity_id\n"
        "trait Boss:\n"
        "    var stage: int\n"
        "system Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.stage\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("auto* b = registry.try_get<Boss>(__match_entity)") != std::string::npos);
            CHECK(code.find("else {") == std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: unsupported generic extern scaffold is rejected", "[codegen-entt][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var x: float\n"
        "trait Velocity:\n"
        "    var dx: float\n"
        "event run\n"
        "extern system ParticleSystem:\n"
        "    filter:\n"
        "        Position\n"
        "        Velocity\n"
        "    on run:\n"
        "        writes:\n"
        "            Position\n"
        "            Velocity\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            CHECK_THROWS_AS(EnttSystemEmitter::emit_extern_system(*sys, decorated), std::runtime_error);
        }
    }
}

TEST_CASE("Codegen EnTT: unsupported ordered extern scaffold is rejected", "[codegen-entt][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var y: float\n"
        "event run\n"
        "extern system SortedRenderer:\n"
        "    filter:\n"
        "        Position\n"
        "    order by:\n"
        "        Position.y desc\n"
        "    on run:\n"
        "        reads:\n"
        "            Position\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            CHECK_THROWS_AS(EnttSystemEmitter::emit_extern_system(*sys, decorated), std::runtime_error);
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

TEST_CASE("Codegen EnTT: generic extern system name does not infer lifecycle dispatch",
          "[codegen-entt][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var x: float\n"
        "event run\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        Position\n"
        "    on run:\n"
        "        reads:\n"
        "            Position\n",
        program);

    CHECK_THROWS_AS(CppEnttCodegen::generate(decorated), std::runtime_error);
}

TEST_CASE("Codegen EnTT: stdlib-style spelling cannot impersonate compiler-owned extern",
          "[codegen-entt][extern-system]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Renderer:\n"
        "    var layer: int\n"
        "event run\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        Transform\n"
        "        Renderer\n"
        "    on run:\n"
        "        reads:\n"
        "            Transform\n"
        "            Renderer\n",
        program);

    CHECK_THROWS_AS(CppEnttCodegen::generate(decorated), std::runtime_error);
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
        "module std.transform.flat\n"
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
        "event run\n"
        "extern system TransformPropagation:\n"
        "    filter:\n"
        "        Parent\n"
        "        LocalTransform\n"
        "        WorldTransform\n"
        "    on run:\n"
        "        reads:\n"
        "            Parent\n"
        "            LocalTransform\n"
        "        writes:\n"
        "            WorldTransform\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::propagate_hierarchy(") != std::string::npos);
    CHECK(code.find("parent_world.position.x + local.position.x") != std::string::npos);
    CHECK(code.find("if (auto* parent = registry.try_get<Parent>(entity)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: hierarchy propagation handles stale parents and cycles safely", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.transform.flat\n"
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
        "event run\n"
        "extern system TransformPropagation:\n"
        "    filter:\n"
        "        Parent\n"
        "        LocalTransform\n"
        "        WorldTransform\n"
        "    on run:\n"
        "        reads:\n"
        "            Parent\n"
        "            LocalTransform\n"
        "        writes:\n"
        "            WorldTransform\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::propagate_hierarchy(") != std::string::npos);
    CHECK(code.find("return entt::entity{entt::null};") != std::string::npos);
    CHECK(code.find("registry.all_of<LocalTransform, WorldTransform>(entity)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: volume transform propagation extern system is recognized", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.transform.volume\n"
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
        "event run\n"
        "extern system TransformPropagation:\n"
        "    filter:\n"
        "        Parent\n"
        "        LocalTransform\n"
        "        WorldTransform\n"
        "    on run:\n"
        "        reads:\n"
        "            Parent\n"
        "            LocalTransform\n"
        "        writes:\n"
        "            WorldTransform\n",
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
        "module std.editor\n"
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
        "pub extern func hit_test_2d(screen_pos: vec2, mask: int) entity_id\n"
        "pub extern system EditorTemplatePalette:\n"
        "    filter:\n"
        "        EditorState\n"
        "    on tick:\n"
        "        reads:\n"
        "            EditorState\n"
        "        effects:\n"
        "            editor\n",
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
        "module std.editor\n"
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
        "module std.editor\n"
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
        "    on tick:\n"
        "        reads:\n"
        "            EditorState\n"
        "        effects:\n"
        "            editor\n"
        "pub extern system EditorPropertyPanel:\n"
        "    filter:\n"
        "        EditorState\n"
        "    on tick:\n"
        "        reads:\n"
        "            EditorState\n"
        "        effects:\n"
        "            editor\n"
        "pub extern system GizmoRenderer2D:\n"
        "    filter:\n"
        "        EditorGizmo2D\n"
        "    on tick:\n"
        "        reads:\n"
        "            EditorGizmo2D\n"
        "        effects:\n"
        "            graphics\n",
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

TEST_CASE("Codegen EnTT: GizmoRenderer3D emits grid wire boxes and mode handles without inferred dispatch",
          "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "use std.editor\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: int = 0\n"
        "pub trait EditorGizmo3D:\n"
        "    var mode: int = 1\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait ModelRenderer:\n"
        "    let model: model_id\n"
        "    var visible: bool = true\n"
        "event tick\n"
        "pub extern system GizmoRenderer3D:\n"
        "    filter:\n"
        "        EditorGizmo3D\n"
        "    on tick:\n"
        "        reads:\n"
        "            EditorGizmo3D\n"
        "        effects:\n"
        "            graphics\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    const auto tick = generated_function(code, "void gizmo_renderer3_d_tick");
    // Early-return when no EditorState is active
    CHECK(tick.find("__editor_active") != std::string::npos);
    CHECK(tick.find("if (!__editor_active) { return; }") != std::string::npos);
    // Ground grid and selection wire box inside a 3D camera block
    CHECK(tick.find("BeginMode3D(cactus::runtime::entt_backend::get_active_camera_3d())") != std::string::npos);
    CHECK(tick.find("DrawGrid(20, 1.0F)") != std::string::npos);
    CHECK(tick.find("DrawCubeWiresV") != std::string::npos);
    CHECK(tick.find("model_bounds_box") != std::string::npos);
    // Mode handles: translate/scale axis lines, rotate circle, scale tip cubes
    CHECK(tick.find("DrawLine3D") != std::string::npos);
    CHECK(tick.find("DrawCircle3D") != std::string::npos);
    CHECK(tick.find("DrawCubeV") != std::string::npos);
    CHECK(tick.find("EndMode3D()") != std::string::npos);

    // The adapter is not implicitly attached to either legacy hook. Linked
    // phase metadata is required to schedule it.
    const auto render_project = generated_function(code, "void generated_render_project");
    CHECK(render_project.find("gizmo_renderer3_d_tick(registry)") == std::string::npos);
    const auto update_project = generated_function(code, "void generated_update_project");
    CHECK(update_project.find("gizmo_renderer3_d_tick") == std::string::npos);
}

TEST_CASE("Codegen EnTT: volume-transform editor program registers pos3d spawn impl and raycast impl",
          "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.editor\n"
        "trait LocalTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait ModelRenderer:\n"
        "    let model: model_id\n"
        "    var visible: bool = true\n"
        "trait EditorLocked\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    // Spawn impl writes the 3D position into both transform components
    CHECK(code.find("register_editor_spawn_impl") != std::string::npos);
    CHECK(code.find("lt->position = pos3d") != std::string::npos);
    CHECK(code.find("wt->position = pos3d") != std::string::npos);
    CHECK(code.find("wt->position = pos2d") == std::string::npos);

    // Raycast impl tests scaled/translated model bounds and skips locked entities
    CHECK(code.find("register_editor_raycast_impl") != std::string::npos);
    CHECK(code.find("model_bounds_box") != std::string::npos);
    CHECK(code.find("GetRayCollisionBox") != std::string::npos);
    CHECK(code.find("reg.view<WorldTransform, ModelRenderer>(entt::exclude<EditorLocked>)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: flat-transform editor program keeps pos2d spawn impl and no raycast impl",
          "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.editor\n"
        "trait LocalTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "trait EditorLocked\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.find("register_editor_spawn_impl") != std::string::npos);
    CHECK(code.find("lt->position = pos2d") != std::string::npos);
    CHECK(code.find("wt->position = pos2d") != std::string::npos);
    CHECK(code.find("pos3d;") == std::string::npos);
    CHECK(code.find("register_editor_raycast_impl") == std::string::npos);
}

TEST_CASE("Codegen EnTT: volume editor program without ModelRenderer registers no raycast impl",
          "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.editor\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "trait EditorLocked\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    // Spawn impl still registers (WorldTransform only — no LocalTransform patch)
    CHECK(code.find("register_editor_spawn_impl") != std::string::npos);
    CHECK(code.find("wt->position = pos3d") != std::string::npos);
    CHECK(code.find("lt->position") == std::string::npos);
    CHECK(code.find("register_editor_raycast_impl") == std::string::npos);
}

static constexpr const char* kViewportTrait =
    "trait Viewport:\n"
    "    var x: float\n"
    "    var y: float\n"
    "    var width: float\n"
    "    var height: float\n"
    "    var depth: int\n"
    "    var clear: bool\n"
    "    var clear_color: color\n"
    "    var active: bool\n";

static constexpr const char* kCameraFlatTrait =
    "trait Camera:\n"
    "    var zoom: float\n"
    "    var offset: vec2\n"
    "    var rotation: float\n";

TEST_CASE("Codegen EnTT: std.camera.viewport emits viewport render loop and no camera-sync block",
          "[codegen-entt][camera][viewport]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string("use std.camera.viewport\n"
                                               "use std.camera.flat\n") +
                                       kViewportTrait + kCameraFlatTrait,
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);

    // Viewport loop is in generated_render_project
    CHECK(code.find("registry.view<Viewport>()") != std::string::npos);
    CHECK(code.find("BeginScissorMode") != std::string::npos);
    CHECK(code.find("EndScissorMode") != std::string::npos);
    CHECK(code.find("std::ranges::sort") != std::string::npos);
    CHECK(code.find("__vp.active") != std::string::npos);
    CHECK(code.find("__vp.depth") != std::string::npos);
    CHECK(code.find("if (__vp.clear) { ClearBackground") != std::string::npos);

    // 2D camera set per viewport via translate helper
    CHECK(code.find("__translate_camera_2d") != std::string::npos);
    CHECK(code.find("set_active_camera_2d") != std::string::npos);

    // No legacy camera-sync block in generated_update_project
    CHECK(code.find("__cam.active") == std::string::npos);
    CHECK(code.find("registry.view<Camera>()") == std::string::npos);

    // #include <algorithm> is present for std::sort
    CHECK(code.find("#include <algorithm>") != std::string::npos);
}

TEST_CASE("Codegen EnTT: viewport loop sorts by depth (lower depth first)", "[codegen-entt][camera][viewport]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string("use std.camera.viewport\n") + kViewportTrait, program);

    const auto code = CppEnttCodegen::generate(decorated);

    // Depth-ordered collection and sort
    CHECK(code.find("static std::vector<std::pair<int,entt::entity>> __vps") != std::string::npos);
    CHECK(code.find("__vps.emplace_back(__vp.depth, __vp_e)") != std::string::npos);
    CHECK(code.find("std::ranges::sort(__vps)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: viewport loop emits clear and no-clear paths", "[codegen-entt][camera][viewport]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string("use std.camera.viewport\n") + kViewportTrait, program);

    const auto code = CppEnttCodegen::generate(decorated);

    // clear path: conditional ClearBackground per viewport
    CHECK(code.find("if (__vp.clear) { ClearBackground(__vp.clear_color); }") != std::string::npos);
}

TEST_CASE("Codegen EnTT: viewport loop emits scissor for each viewport (split-screen)",
          "[codegen-entt][camera][viewport]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string("use std.camera.viewport\n"
                                               "use std.camera.flat\n") +
                                       kViewportTrait + kCameraFlatTrait,
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);

    // Each viewport iteration sets scissor region based on normalized rect
    CHECK(code.find("BeginScissorMode(\n"
                    "                static_cast<int>(__vp.x * static_cast<float>(__sw))") != std::string::npos);
    CHECK(code.find("static_cast<int>(__vp.width * static_cast<float>(__sw))") != std::string::npos);
    CHECK(code.find("static_cast<int>(__vp.height * static_cast<float>(__sh))") != std::string::npos);

    // Per-viewport camera is set with the entity's Camera component
    CHECK(code.find("registry.all_of<Camera>(__vp_ent)") != std::string::npos);
    CHECK(code.find("__translate_camera_2d(__cam, __sw, __sh)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: Camera trait no longer has active field after multi-viewport change",
          "[codegen-entt][camera][viewport]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.camera.flat\n"
        "trait Camera:\n"
        "    var zoom: float\n"
        "    var offset: vec2\n"
        "    var rotation: float\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    // Camera struct without active field — no camera-sync block is emitted
    CHECK(code.find("__cam.active") == std::string::npos);
    // Camera struct is still generated
    CHECK(code.find("struct Camera") != std::string::npos);
}

// ── Query expression backend codegen tests ──────────────────────────────────

static const char* const kQueryPreamble =
    "use std.query as query\n"
    "pub event tick:\n"
    "    dt: float\n"
    "trait Boss:\n"
    "    var hp: int\n"
    "trait Enemy:\n"
    "    var hp: int\n"
    "trait Dead\n";

TEST_CASE("Codegen EnTT: query.exists lowers to entt view begin/end check", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "system S:\n"
                                       "    on tick:\n"
                                       "        if query.exists[Boss]():\n"
                                       "            let x = 1\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Boss>()") != std::string::npos);
    CHECK(code.find("__v.begin() != __v.end()") != std::string::npos);
}

TEST_CASE("Codegen EnTT: query.exists with negation lowers to excluded view", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "system S:\n"
                                       "    on tick:\n"
                                       "        if query.exists[Enemy, not Dead]():\n"
                                       "            let x = 1\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Enemy>(entt::exclude<Dead>)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: query.count lowers to counting loop", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "system S:\n"
                                       "    on tick:\n"
                                       "        let n = query.count[Enemy, not Dead]()\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Enemy>(entt::exclude<Dead>)") != std::string::npos);
    CHECK(code.find("std::ranges::distance") != std::string::npos);
}

TEST_CASE("Codegen EnTT: query.first lowers to begin/end with null fallback", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "system S:\n"
                                       "    on tick:\n"
                                       "        let t = query.first[Boss]()\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Boss>()") != std::string::npos);
    CHECK(code.find("entt::entity{entt::null}") != std::string::npos);
    // empty result returns null sentinel (total entity_id semantics)
    CHECK(code.find("__it != __v.end()") != std::string::npos);
}

TEST_CASE("Codegen EnTT: query.all lowers to vector collection loop", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "system S:\n"
                                       "    on tick:\n"
                                       "        let all = query.all[Enemy]()\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Enemy>()") != std::string::npos);
    CHECK(code.find("std::vector<entt::entity> __r") != std::string::npos);
    CHECK(code.find("__r.push_back(__e)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: query.parent lowers to Parent component try_get", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.query as query\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Child:\n"
        "    var child_id: entity_id\n"
        "system S:\n"
        "    filter:\n"
        "        Child\n"
        "    on tick:\n"
        "        let p = query.parent(of = child_id)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.try_get<Parent>") != std::string::npos);
    CHECK(code.find("__p->parent") != std::string::npos);
    CHECK(code.find("entt::entity{entt::null}") != std::string::npos);
}

TEST_CASE("Codegen EnTT: fully qualified std.query path is lowered correctly", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.query\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Boss:\n"
        "    var hp: int\n"
        "system S:\n"
        "    on tick:\n"
        "        let t = std.query.first[Boss]()\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Boss>()") != std::string::npos);
    CHECK(code.find("entt::entity{entt::null}") != std::string::npos);
}

static const char* const kFlatQueryPreamble =
    "use std.physics.flat.query as query\n"
    "pub event tick:\n"
    "    dt: float\n"
    "trait WorldTransform:\n"
    "    var position: vec2\n"
    "trait Enemy:\n"
    "    var hp: int\n"
    "trait Pickup\n"
    "trait Collected\n"
    "trait Wall\n";

TEST_CASE("Codegen EnTT: flat query.nearest lowers to WorldTransform distance search",
          "[codegen-entt][query][spatial]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kFlatQueryPreamble) +
                                       "system S:\n"
                                       "    on tick:\n"
                                       "        let p = query.nearest[Enemy](from = tick.dt)\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Enemy>()") != std::string::npos);
    CHECK(code.find("registry.get<WorldTransform>(__e)") != std::string::npos);
    CHECK(code.find("std::numeric_limits<float>::max()") != std::string::npos);
    // empty result returns null sentinel (total entity_id semantics) via __best initialization
    CHECK(code.find("__best{entt::null}") != std::string::npos);
}

TEST_CASE("Codegen EnTT: flat query.overlap_box excludes negative filter matches", "[codegen-entt][query][spatial]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        std::string(kFlatQueryPreamble) +
            "system S:\n"
            "    on tick:\n"
            "        let p = query.overlap_box[Pickup, not Collected](center = tick.dt, size = tick.dt)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Pickup>(entt::exclude<Collected>)") != std::string::npos);
    CHECK(code.find("* 0.5F") != std::string::npos);
    CHECK(code.find("std::abs") != std::string::npos);
}

TEST_CASE("Codegen EnTT: flat query.overlap_circle lowers to radius-based search", "[codegen-entt][query][spatial]") {
    ProgramNode program;
    auto decorated =
        full_pipeline(std::string(kFlatQueryPreamble) +
                          "system S:\n"
                          "    on tick:\n"
                          "        let hits = query.overlap_circle[Enemy](center = tick.dt, radius = tick.dt)\n",
                      program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Enemy>()") != std::string::npos);
    CHECK(code.find("__dx * __dx + __dy * __dy") != std::string::npos);
    CHECK(code.find("std::vector<entt::entity> __r") != std::string::npos);
}

TEST_CASE("Codegen EnTT: flat query.raycast lowers to directional hit search", "[codegen-entt][query][spatial]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        std::string(kFlatQueryPreamble) +
            "system S:\n"
            "    on tick:\n"
            "        let hit = query.raycast[Wall](origin = tick.dt, dir = tick.dt, max_dist = tick.dt)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Wall>()") != std::string::npos);
    CHECK(code.find("__proj") != std::string::npos);
    CHECK(code.find("__best{entt::null}") != std::string::npos);
}

static const char* const kVolumeQueryPreamble =
    "use std.physics.volume.query as query\n"
    "pub event tick:\n"
    "    dt: float\n"
    "trait WorldTransform:\n"
    "    var position: vec3\n"
    "trait Enemy:\n"
    "    var hp: int\n"
    "trait Pickup\n";

TEST_CASE("Codegen EnTT: volume query.nearest lowers to 3D distance search", "[codegen-entt][query][spatial][3d]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kVolumeQueryPreamble) +
                                       "system S:\n"
                                       "    on tick:\n"
                                       "        let e = query.nearest[Enemy](from = tick.dt)\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Enemy>()") != std::string::npos);
    CHECK(code.find("__dz = __wt.position.z") != std::string::npos);
    CHECK(code.find("__best{entt::null}") != std::string::npos);
}

TEST_CASE("Codegen EnTT: volume query.overlap_sphere lowers to 3D radius search",
          "[codegen-entt][query][spatial][3d]") {
    ProgramNode program;
    auto decorated =
        full_pipeline(std::string(kVolumeQueryPreamble) +
                          "system S:\n"
                          "    on tick:\n"
                          "        let hits = query.overlap_sphere[Enemy](center = tick.dt, radius = tick.dt)\n",
                      program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Enemy>()") != std::string::npos);
    CHECK(code.find("__dz = __wt.position.z - __ct.z") != std::string::npos);
    CHECK(code.find("std::vector<entt::entity> __r") != std::string::npos);
}

// ── Hierarchical entity templates (dsl-hierarchical-entity-templates) ───────

static const std::string HIERARCHY_SOURCE_PREFIX =
    "pub event tick:\n"
    "    dt: float\n"
    "trait Parent:\n"
    "    var parent: entity_id\n"
    "trait Tag:\n"
    "    var value: int = 0\n"
    "trait Growth:\n"
    "    var target_scale: float = 1.0\n"
    "template Rig:\n"
    "    Tag\n"
    "    children:\n"
    "        entity Socket:\n"
    "            Tag:\n"
    "                value = 1\n"
    "            children:\n"
    "                entity Gem:\n"
    "                    Growth\n";

TEST_CASE("Codegen EnTT: hierarchical template emits per-node helpers and canonical wrapper",
          "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX, program);
    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.find("static entt::entity create_rig__node(entt::registry& registry)") != std::string::npos);
    CHECK(code.find("static entt::entity create_rig__node__socket(entt::registry& registry)") != std::string::npos);
    CHECK(code.find("static entt::entity create_rig__node__socket__gem(entt::registry& registry)") !=
          std::string::npos);

    const auto wrapper = generated_function(code, "entt::entity create_rig(entt::registry& registry)");
    CHECK(wrapper.find("auto entity = create_rig__node(registry);") != std::string::npos);
    CHECK(wrapper.find("return entity;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: hierarchical creation assigns Parent to the immediate parent", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX, program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto wrapper = generated_function(code, "entt::entity create_rig(entt::registry& registry)");
    // Direct child points at the root; grandchild points at the child, not the root.
    CHECK(wrapper.find("registry.emplace_or_replace<Parent>(__child_0, Parent{.parent = entity});") !=
          std::string::npos);
    CHECK(wrapper.find("registry.emplace_or_replace<Parent>(__child_0_0, Parent{.parent = __child_0});") !=
          std::string::npos);
    // The root never receives a generated Parent relation.
    CHECK(wrapper.find("Parent>(entity") == std::string::npos);
}

TEST_CASE("Codegen EnTT: hierarchical creation is parent-first preorder", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX +
                                        "template Pair:\n"
                                        "    Tag\n"
                                        "    children:\n"
                                        "        entity First:\n"
                                        "            Tag\n"
                                        "            children:\n"
                                        "                entity Deep:\n"
                                        "                    Tag\n"
                                        "        entity Second:\n"
                                        "            Tag\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto wrapper = generated_function(code, "entt::entity create_pair(entt::registry& registry)");
    const auto root    = wrapper.find("create_pair__node(registry)");
    const auto first   = wrapper.find("create_pair__node__first(registry)");
    const auto deep    = wrapper.find("create_pair__node__first__deep(registry)");
    const auto second  = wrapper.find("create_pair__node__second(registry)");
    REQUIRE(root != std::string::npos);
    REQUIRE(first != std::string::npos);
    REQUIRE(deep != std::string::npos);
    REQUIRE(second != std::string::npos);
    // Root, then first child and its whole subtree, then the next sibling.
    CHECK(root < first);
    CHECK(first < deep);
    CHECK(deep < second);
}

TEST_CASE("Codegen EnTT: override-free spawn of hierarchical template calls canonical wrapper and returns root",
          "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX +
                                        "system S:\n"
                                        "    on tick:\n"
                                        "        let root = spawn Rig:\n"
                                        "            Tag:\n"
                                        "                value = 3\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.find("auto __spawned = create_rig(registry);") != std::string::npos);
    CHECK(code.find("return __spawned;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: spawn with child overrides expands inline per node", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX +
                                        "system S:\n"
                                        "    on tick:\n"
                                        "        let root = spawn Rig:\n"
                                        "            Tag:\n"
                                        "                value = 3\n"
                                        "            children:\n"
                                        "                Socket:\n"
                                        "                    Tag:\n"
                                        "                        value = 4\n"
                                        "                    children:\n"
                                        "                        Gem:\n"
                                        "                            Growth:\n"
                                        "                                target_scale = 2.0\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    // Inline expansion uses the per-node helpers, not the canonical wrapper.
    CHECK(code.find("auto __spawned = create_rig__node(registry);") != std::string::npos);
    CHECK(code.find("auto __child_0 = create_rig__node__socket(registry);") != std::string::npos);
    CHECK(code.find("registry.emplace_or_replace<Parent>(__child_0, Parent{.parent = __spawned});") !=
          std::string::npos);
    CHECK(code.find("auto __child_0_0 = create_rig__node__socket__gem(registry);") != std::string::npos);
    CHECK(code.find("registry.emplace_or_replace<Parent>(__child_0_0, Parent{.parent = __child_0});") !=
          std::string::npos);
    // Per-node overrides applied to the matching created entity.
    CHECK(code.find("registry.try_get<Tag>(__child_0)") != std::string::npos);
    CHECK(code.find("registry.try_get<Growth>(__child_0_0)") != std::string::npos);
    CHECK(code.find("return __spawned;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: hierarchical load-time entity creates descendants in setup", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX +
                                        "entity Rig1 from Rig:\n"
                                        "    Tag:\n"
                                        "        value = 5\n"
                                        "    children:\n"
                                        "        Socket:\n"
                                        "            Tag:\n"
                                        "                value = 7\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    // generated_init_project creates the whole tree through the entity wrapper.
    CHECK(code.find("create_rig1(registry);") != std::string::npos);
    const auto wrapper = generated_function(code, "entt::entity create_rig1(entt::registry& registry)");
    CHECK(wrapper.find("create_rig1__node__socket(registry)") != std::string::npos);
    CHECK(wrapper.find("registry.emplace_or_replace<Parent>(__child_0, Parent{.parent = entity});") !=
          std::string::npos);
    // Child override value is baked into the per-node helper.
    const auto socket_fn = generated_function(code, "entt::entity create_rig1__node__socket(entt::registry&");
    CHECK(socket_fn.find("component.value = 7") != std::string::npos);
}

TEST_CASE("Codegen EnTT: destroy cascade code coexists with hierarchical creation", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX +
                                        "system S:\n"
                                        "    filter:\n"
                                        "        Tag\n"
                                        "    on tick:\n"
                                        "        destroy self\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    // The existing Parent-based recursive destroy support keys off the Parent
    // trait, which hierarchical templates rely on unchanged.
    CHECK(code.find("registry.view<Parent>()") != std::string::npos);
}

// ── 5.1: Key constant mapping and new extern declarations ───────────────────

TEST_CASE("Codegen EnTT: Key.Shift/Minus/Equal/F map to correct raylib constants", "[codegen-entt][input][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event input\n"
        "use std.input as inp\n"
        "input PanMod: button\n"
        "    key = inp.Key.Shift\n"
        "input ZoomIn: button\n"
        "    key = inp.Key.Equal\n"
        "input ZoomOut: button\n"
        "    key = inp.Key.Minus\n"
        "input FrameSel: button\n"
        "    key = std.input.Key.F\n",
        program,
        std_input_imports());

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("KEY_LEFT_SHIFT") != std::string::npos);
    CHECK(code.find("KEY_EQUAL") != std::string::npos);
    CHECK(code.find("KEY_MINUS") != std::string::npos);
    CHECK(code.find("KEY_F") != std::string::npos);
}

TEST_CASE("Codegen EnTT: std.input mouse_delta and wheel_delta lower to correct runtime calls",
          "[codegen-entt][editor][stdlib]") {
    ProgramNode program;
    // input.wheel_delta() and input.mouse_delta() are qualified calls on the
    // std.input module and must lower to the prefixed runtime symbols.
    auto decorated = full_pipeline(
        "use std.editor\n"
        "use std.input\n"
        "pub event tick\n"
        "trait Rig:\n"
        "    var active: bool\n"
        "system NavPoll:\n"
        "    filter:\n"
        "        Rig\n"
        "    on tick:\n"
        "        let _w = input.wheel_delta()\n"
        "        let _d = input.mouse_delta()\n",
        program);

    const auto code   = CppEnttCodegen::generate(decorated);
    const auto system = generated_function(code, "void nav_poll_tick");
    CHECK(system.find("cactus::runtime::entt_backend::editor_wheel_delta()") != std::string::npos);
    CHECK(system.find("cactus::runtime::entt_backend::editor_mouse_delta_screen()") != std::string::npos);
}

// ── 5.3: editor_consume generates correct runtime call ───────────────────────

TEST_CASE("Codegen EnTT: input.consume call generates runtime consume invocation", "[codegen-entt][editor][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.editor\n"
        "pub event input\n"
        "use std.input as inp\n"
        "input NavDrag: button\n"
        "    mouse = inp.MouseButton.Right\n"
        "trait EditorSt:\n"
        "    var active: bool = true\n"
        "system EditorInputConsume:\n"
        "    filter:\n"
        "        EditorSt\n"
        "    on input:\n"
        "        input.consume(NavDrag)\n",
        program,
        std_input_imports());

    const auto code   = CppEnttCodegen::generate(decorated);
    const auto system = generated_function(code, "void editor_input_consume_input");
    // The call must pass K_NAV_DRAG (the generated enum constant) to editor_consume.
    CHECK(system.find("cactus::runtime::entt_backend::editor_consume(K_NAV_DRAG)") != std::string::npos);
}

// ── 5.4: Camera rig lifecycle registration in generated_init_project ─────────

static constexpr const char* kCameraVolumeTrait =
    "trait Camera:\n"
    "    var fov_y: float\n"
    "    var near: float\n"
    "    var far: float\n";

static constexpr const char* kWorldTransformVolumeTrait =
    "trait WorldTransform:\n"
    "    var position: vec3\n"
    "    var rotation: quat\n"
    "    var scale: vec3\n";

TEST_CASE("Codegen EnTT: 2D editor+viewport program registers 2D camera rig lifecycle impls",
          "[codegen-entt][editor][camera-rig]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string("use std.editor\n"
                                               "use std.camera.viewport\n"
                                               "use std.camera.flat\n") +
                                       kViewportTrait + kCameraFlatTrait,
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto init = generated_function(code, "void generated_init_project");

    // Enter and exit impls are always registered together
    CHECK(init.find("register_editor_camera_enter_impl") != std::string::npos);
    CHECK(init.find("register_editor_camera_exit_impl") != std::string::npos);

    // Enter impl captures 2D pose and spawns rig with EditorCamera2D
    CHECK(init.find("EditorCamera2D{.view_center") != std::string::npos);
    CHECK(init.find(".min_zoom = 0.05F") != std::string::npos);
    CHECK(init.find(".max_zoom = 20.0F") != std::string::npos);
    CHECK(init.find("set_editor_saved_viewports") != std::string::npos);

    // Exit impl destroys rig and restores viewports
    CHECK(init.find("reg.destroy(rig)") != std::string::npos);
    CHECK(init.find("__vp->active = true") != std::string::npos);

    // 2D apply impl writes zoom and offset back to rig Camera
    CHECK(init.find("register_editor_apply_camera_2d_impl") != std::string::npos);
    CHECK(init.find("__cam->zoom = zoom") != std::string::npos);
    CHECK(init.find("__cam->offset = view_center") != std::string::npos);

    // No 3D impls without uses_volume
    CHECK(init.find("register_editor_apply_camera_3d_impl") == std::string::npos);
}

TEST_CASE("Codegen EnTT: 3D editor+viewport program registers 3D camera rig lifecycle impls",
          "[codegen-entt][editor][camera-rig]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string("use std.editor\n"
                                               "use std.camera.viewport\n"
                                               "use std.camera.volume\n") +
                                       kViewportTrait + kCameraVolumeTrait + kWorldTransformVolumeTrait,
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto init = generated_function(code, "void generated_init_project");

    // Enter impl derives orbit state from camera pose
    CHECK(init.find("register_editor_camera_enter_impl") != std::string::npos);
    CHECK(init.find("std::atan2(-__dx, -__dz)") != std::string::npos);  // yaw derivation
    CHECK(init.find("std::asin") != std::string::npos);                 // pitch derivation
    CHECK(init.find("EditorCamera3D{.focus") != std::string::npos);
    CHECK(init.find(".orbit_speed = 0.005F") != std::string::npos);
    CHECK(init.find(".min_pitch = -1.5F") != std::string::npos);
    CHECK(init.find(".max_pitch = 1.5F") != std::string::npos);
    CHECK(init.find(".min_distance = 0.1F") != std::string::npos);
    CHECK(init.find(".max_distance = 1000.0F") != std::string::npos);

    // Exit impl destroys rig
    CHECK(init.find("register_editor_camera_exit_impl") != std::string::npos);
    CHECK(init.find("reg.destroy(rig)") != std::string::npos);

    // 3D apply impl writes position and rotation to rig WorldTransform (5.5 clamping driven by these writes)
    CHECK(init.find("register_editor_apply_camera_3d_impl") != std::string::npos);
    CHECK(init.find("__wt->position = position") != std::string::npos);
    CHECK(init.find("__wt->rotation = rotation") != std::string::npos);

    // Entity position impl returns WorldTransform.position
    CHECK(init.find("register_editor_entity_position_3d_impl") != std::string::npos);
    CHECK(init.find("return __wt->position") != std::string::npos);

    // No 2D-specific impls without uses_flat
    CHECK(init.find("register_editor_apply_camera_2d_impl") == std::string::npos);
}

// ── 5.5: Camera and transform projection lowering ─────────────────────────

TEST_CASE("Codegen EnTT: std.camera.flat projection helpers lower to correct runtime calls",
          "[codegen-entt][editor][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.camera.flat as camera2d\n"
        "pub event tick\n"
        "trait Scene:\n"
        "    var active: bool\n"
        "system Project2D:\n"
        "    filter:\n"
        "        Scene\n"
        "    on tick:\n"
        "        let _wp = camera2d.screen_to_world(vec2(0.0, 0.0))\n"
        "        let _wd = camera2d.screen_delta_to_world(vec2(1.0, 0.0))\n",
        program);

    const auto code   = CppEnttCodegen::generate(decorated);
    const auto system = generated_function(code, "void project2_d_tick");
    CHECK(system.find("cactus::runtime::entt_backend::editor_screen_to_world_2d(") != std::string::npos);
    CHECK(system.find("cactus::runtime::entt_backend::screen_delta_to_world_2d(") != std::string::npos);
}

TEST_CASE("Codegen EnTT: std.transform.flat world_position injects registry as first argument",
          "[codegen-entt][editor][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.transform.flat as transform2d\n"
        "pub event tick\n"
        "trait Selection:\n"
        "    var sel: entity_id\n"
        "system GetPos:\n"
        "    filter:\n"
        "        Selection\n"
        "    on tick:\n"
        "        let _pos = transform2d.world_position(sel)\n",
        program);

    const auto code   = CppEnttCodegen::generate(decorated);
    const auto system = generated_function(code, "void get_pos_tick");
    CHECK(system.find("cactus::runtime::entt_backend::editor_entity_position_2d(registry,") != std::string::npos);
}

TEST_CASE("Codegen EnTT: clean-named editor extern func spawn_template lowers with registry injected",
          "[codegen-entt][editor][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick\n"
        "pub extern func spawn_template(template_name: string, position_2d: vec2, position_3d: vec3) entity_id\n"
        "trait Placer:\n"
        "    var result: entity_id\n"
        "system PlaceTest:\n"
        "    filter:\n"
        "        Placer\n"
        "    on tick:\n"
        "        result = spawn_template(\"Enemy\", vec2(0.0, 0.0), vec3(0.0, 0.0, 0.0))\n",
        program);

    decorated.funcs["spawn_template"].is_stdlib   = true;
    decorated.funcs["spawn_template"].module_name = "std.editor";

    const auto code   = CppEnttCodegen::generate(decorated);
    const auto system = generated_function(code, "void place_test_tick");
    CHECK(system.find("cactus::runtime::entt_backend::editor_spawn_template(registry,") != std::string::npos);
}

TEST_CASE("Codegen EnTT: same simple component name from different modules produces distinct C++ symbols",
          "[codegen-entt][canonical-identity]") {
    ResolvedTrait flat_wt;
    flat_wt.name        = "WorldTransform";
    flat_wt.module_name = "std.transform.flat";
    flat_wt.fields.push_back({.name = "position", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});

    ResolvedTrait vol_wt;
    vol_wt.name        = "WorldTransform";
    vol_wt.module_name = "std.transform.volume";
    vol_wt.fields.push_back({.name = "position", .type = {.kind = TypeKind::Vec3, .name = "vec3"}, .is_var = true});

    const auto flat_code = EnttComponentEmitter::emit_component(flat_wt);
    const auto vol_code  = EnttComponentEmitter::emit_component(vol_wt);

    CHECK(flat_code.find("struct std_transform_flat__WorldTransform") != std::string::npos);
    CHECK(vol_code.find("struct std_transform_volume__WorldTransform") != std::string::npos);
    CHECK(flat_code.find("struct WorldTransform ") == std::string::npos);
    CHECK(vol_code.find("struct WorldTransform ") == std::string::npos);
}

TEST_CASE("Codegen EnTT: user-defined trait with explicit module_name produces canonical C++ type",
          "[codegen-entt][canonical-identity]") {
    ResolvedTrait trait;
    trait.name        = "WorldTransform";
    trait.module_name = "game.transforms";
    trait.fields.push_back({.name = "position", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});

    const auto code = EnttComponentEmitter::emit_component(trait);
    CHECK(code.find("struct game_transforms__WorldTransform") != std::string::npos);
    CHECK(code.find("struct WorldTransform") == std::string::npos);
}

TEST_CASE("Codegen EnTT: system function name uses canonical module prefix when program.module_name is set",
          "[codegen-entt][canonical-identity]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Velocity:\n"
        "    var vx: float\n"
        "system Move:\n"
        "    filter:\n"
        "        Velocity\n"
        "    on tick:\n"
        "        vx = vx + tick.dt\n",
        program);
    decorated.module_name = "my.game";

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("void my_game__move_tick(") != std::string::npos);
    CHECK(code.find("void move_tick(") == std::string::npos);
}

TEST_CASE("Codegen EnTT: registry view uses canonical C++ type names for module-qualified traits",
          "[codegen-entt][canonical-identity]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Position:\n"
        "    var x: float\n"
        "system Move:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        x = x + tick.dt\n",
        program);
    decorated.traits.at("Position").module_name = "my.game";

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            const auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.view<my_game__Position>()") != std::string::npos);
            CHECK(code.find("my_game__Position& my_game__Position_comp") != std::string::npos);
            CHECK(code.find("my_game__Position_comp.x") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: stdlib func lowering falls back to program.funcs when no local use import",
          "[codegen-entt][canonical-identity][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "trait Value:\n"
        "    var x: float\n"
        "system Demo:\n"
        "    filter:\n"
        "        Value\n"
        "    on tick:\n"
        "        x = lerp(0.0, 10.0, 0.5)\n",
        program);

    ResolvedFunc lerp_func;
    lerp_func.name          = "lerp";
    lerp_func.module_name   = "std.math";
    lerp_func.is_stdlib     = true;
    lerp_func.is_extern     = true;
    decorated.funcs["lerp"] = lerp_func;

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::stdlib::math::lerp(0.0F, 10.0F, 0.5F)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: stdlib extern system lowering uses canonical C++ names for module-qualified traits",
          "[codegen-entt][canonical-identity]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.sprites\n"
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
        "event tick\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on tick:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n",
        program);
    decorated.traits.at("WorldTransform").module_name = "std.transform.flat";
    decorated.traits.at("Renderer").module_name       = "std.render.sprites";

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void sprite_renderer_tick");
    CHECK(tick.find("registry.view<std_transform_flat__WorldTransform, std_render_sprites__Renderer>()") !=
          std::string::npos);
    CHECK(tick.find("std_transform_flat__WorldTransform_comp.position") != std::string::npos);
    CHECK(tick.find("std_render_sprites__Renderer_comp.layer") != std::string::npos);
}

TEST_CASE("Codegen EnTT: ambiguous simple-name trait lookup fails loudly under canonical keys",
          "[codegen-entt][canonical-identity][editor]") {
    DecoratedProgram program;

    ResolvedTrait flat_wt;
    flat_wt.name         = "WorldTransform";
    flat_wt.module_name  = "std.transform.flat";
    flat_wt.canonical_id = "std.transform.flat.WorldTransform";
    flat_wt.fields.push_back({.name = "position", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});

    ResolvedTrait vol_wt;
    vol_wt.name         = "WorldTransform";
    vol_wt.module_name  = "std.transform.volume";
    vol_wt.canonical_id = "std.transform.volume.WorldTransform";
    vol_wt.fields.push_back({.name = "position", .type = {.kind = TypeKind::Vec3, .name = "vec3"}, .is_var = true});

    ResolvedTrait renderer;
    renderer.name         = "ModelRenderer";
    renderer.module_name  = "std.render.models";
    renderer.canonical_id = "std.render.models.ModelRenderer";
    renderer.fields.push_back({.name = "visible", .type = {.kind = TypeKind::Bool, .name = "bool"}, .is_var = true});

    program.traits["std.transform.flat.WorldTransform"]   = flat_wt;
    program.traits["std.transform.volume.WorldTransform"] = vol_wt;
    program.traits["std.render.models.ModelRenderer"]     = renderer;

    // Simple name carried by two distinct canonical ids → loud internal error.
    try {
        (void)EnttCodegenUtils::find_trait(program, "WorldTransform");
        FAIL("expected ambiguous trait lookup to throw");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("WorldTransform") != std::string::npos);
    }

    // Canonical ids resolve to their exact variant.
    const auto* flat = EnttCodegenUtils::find_trait(program, "std.transform.flat.WorldTransform");
    REQUIRE(flat != nullptr);
    CHECK(flat->fields.at(0).type.kind == TypeKind::Vec2);
    const auto* vol = EnttCodegenUtils::find_trait(program, "std.transform.volume.WorldTransform");
    REQUIRE(vol != nullptr);
    CHECK(vol->fields.at(0).type.kind == TypeKind::Vec3);

    // Unique simple names still resolve under canonical map keys.
    CHECK(EnttCodegenUtils::has_trait(program, "ModelRenderer"));
    const auto* mr = EnttCodegenUtils::find_trait(program, "ModelRenderer");
    REQUIRE(mr != nullptr);
    CHECK(mr->canonical_id == "std.render.models.ModelRenderer");
}

TEST_CASE("Codegen EnTT: graph scheduler state owns typed events phases commands and stable dispatch",
          "[codegen-entt][phase-runtime][5.1]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "module game.scheduler\n"
        "pub extern event frame:\n"
        "    dt: float\n"
        "event Contact:\n"
        "    amount: int\n"
        "trait Marker:\n"
        "    var value: int\n"
        "phase input:\n"
        "    from:\n"
        "        frame\n"
        "    dt: float = frame.dt\n"
        "phase fixed_tick:\n"
        "    after:\n"
        "        input\n"
        "    every: 0.5\n"
        "    max: 2\n"
        "system First:\n"
        "    on input:\n"
        "        let sample = input.dt\n"
        "system Second:\n"
        "    filter:\n"
        "        Marker\n"
        "    on input:\n"
        "        value = value + 1\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.find("#include <deque>") != std::string::npos);
    CHECK(code.find("#include <functional>") != std::string::npos);
    CHECK(code.find("#include <variant>") != std::string::npos);
    CHECK(code.find("using CactusEventOccurrence = std::variant<ContactEvent, frameEvent>;") != std::string::npos);
    CHECK(code.find("CactusEventOccurrence occurrence;") != std::string::npos);
    CHECK(code.find("void generated_inject_external_event(frameEvent occurrence)") != std::string::npos);
    CHECK(code.find("void generated_inject_external_event(ContactEvent occurrence)") == std::string::npos);

    CHECK(code.find("std::deque<CactusQueuedEvent> event_queue;") != std::string::npos);
    CHECK(code.find("std::deque<CactusQueuedEvent> deferred_events;") != std::string::npos);
    CHECK(code.find("enum class Kind : std::uint8_t { Spawn, Destroy, Add, Remove };") != std::string::npos);
    CHECK(code.find("std::vector<CactusStructuralCommand> commands;") != std::string::npos);

    CHECK(code.find("struct game_scheduler__inputPhaseRuntimeState") != std::string::npos);
    CHECK(code.find("struct game_scheduler__fixed_tickPhaseRuntimeState") != std::string::npos);
    CHECK(code.find("double accumulator{};") != std::string::npos);
    CHECK(code.find("double alpha{};") != std::string::npos);
    CHECK(code.find("std::uint64_t completed_batches{};") != std::string::npos);
    CHECK(code.find("float dt{};") != std::string::npos);
    CHECK(code.find("game_scheduler__inputPhaseRuntimeState game_scheduler__input;") != std::string::npos);
    CHECK(code.find("game_scheduler__fixed_tickPhaseRuntimeState game_scheduler__fixed_tick;") != std::string::npos);

    const auto input_batch = code.find("void generated_run_phase_batch_game_scheduler__input");
    const auto fixed_batch = code.find("void generated_run_phase_batch_game_scheduler__fixed_tick");
    REQUIRE(input_batch != std::string::npos);
    REQUIRE(fixed_batch != std::string::npos);
    CHECK(input_batch < fixed_batch);
    CHECK(code.find("phase.accumulator += static_cast<double>(root_event.dt);") != std::string::npos);
    CHECK(code.find("const auto due = static_cast<std::uint64_t>(std::floor(phase.accumulator / interval));") !=
          std::string::npos);
    CHECK(code.find("constexpr std::uint64_t max_repetitions = 2;") != std::string::npos);
    CHECK(code.find("const auto repetitions = std::min(due, max_repetitions);") != std::string::npos);
    CHECK(code.find("phase.dt = interval;") != std::string::npos);
    CHECK(code.find("phase.accumulator -= static_cast<double>(due) * interval;") != std::string::npos);
    CHECK(code.find("phase.alpha = phase.accumulator / interval;") != std::string::npos);
    CHECK(code.find("++phase.completed_batches;") != std::string::npos);

    const auto root_dispatch =
        code.find("void generated_process_root_event(entt::registry& registry, const frameEvent&");
    REQUIRE(root_dispatch != std::string::npos);
    const auto input_call =
        code.find("generated_run_phase_batch_game_scheduler__input(registry, root_event);", root_dispatch);
    const auto fixed_call =
        code.find("generated_run_phase_batch_game_scheduler__fixed_tick(registry, root_event);", root_dispatch);
    REQUIRE(input_call != std::string::npos);
    REQUIRE(fixed_call != std::string::npos);
    CHECK(input_call < fixed_call);
    CHECK(code.find("void generated_drain_external_events(entt::registry& registry)") != std::string::npos);

    const auto main_start = code.find("int main()");
    const auto main_end   = code.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start);
    REQUIRE(main_start != std::string::npos);
    REQUIRE(main_end != std::string::npos);
    const auto main = code.substr(main_start, main_end - main_start);
    CHECK(main.find("const float dt = GetFrameTime();") != std::string::npos);
    CHECK(main.find("generated_inject_external_event(frameEvent{.dt = dt});") != std::string::npos);
    CHECK(main.find("generated_drain_external_events(registry);") != std::string::npos);
    CHECK(main.find("generated_update_project") == std::string::npos);
    CHECK(main.find("generated_render_project") == std::string::npos);
    CHECK(main.find("generated_inject_external_event(frameEvent{.dt = dt});") ==
          main.rfind("generated_inject_external_event(frameEvent{.dt = dt});"));
    CHECK(code.find("#ifndef CACTUS_GENERATED_NO_MAIN") != std::string::npos);

    const auto first_dispatch  = code.find("{\"game.scheduler.First/on game.scheduler.input\", true}");
    const auto second_dispatch = code.find("{\"game.scheduler.Second/on game.scheduler.input\", false}");
    REQUIRE(first_dispatch != std::string::npos);
    REQUIRE(second_dispatch != std::string::npos);
    CHECK(first_dispatch < second_dispatch);

    const auto first_handler = code.find(
        "void first_input(entt::registry& registry, const "
        "cactus::runtime::entt_backend::game_scheduler__inputPhaseRuntimeState& input)");
    const auto second_handler = code.find(
        "void second_input(entt::registry& registry, const "
        "cactus::runtime::entt_backend::game_scheduler__inputPhaseRuntimeState& input)");
    REQUIRE(first_handler != std::string::npos);
    REQUIRE(second_handler != std::string::npos);
    CHECK(code.find("(void)registry;", first_handler) < code.find("}\n\n", first_handler));
    CHECK(code.find("registry.storage<entt::entity>()", first_handler) > code.find("}\n\n", first_handler));
    CHECK(code.find("registry.view<Marker>()", second_handler) < code.find("}\n\n", second_handler));

    const auto phase_dispatch = code.find(
        "void generated_dispatch_phase_game_scheduler__input(entt::registry& registry, const "
        "game_scheduler__inputPhaseRuntimeState& phase)");
    REQUIRE(phase_dispatch != std::string::npos);
    const auto first_call  = code.find("::first_input(registry, phase);", phase_dispatch);
    const auto second_call = code.find("::second_input(registry, phase);", phase_dispatch);
    REQUIRE(first_call != std::string::npos);
    REQUIRE(second_call != std::string::npos);
    CHECK(first_call < second_call);

    ProgramNode legacy_program;
    const auto legacy = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "system Legacy:\n"
        "    on tick:\n"
        "        let sample = tick.dt\n",
        legacy_program);
    const auto legacy_code = CppEnttCodegen::generate(legacy);
    CHECK(legacy_code.find("Graph Activation Runtime State") == std::string::npos);
    CHECK(legacy_code.find("CactusSchedulerState") == std::string::npos);
    CHECK(legacy_code.find("#include <variant>") == std::string::npos);
    const auto legacy_main_start = legacy_code.find("int main()");
    const auto legacy_main_end   = legacy_code.find("#endif  // CACTUS_GENERATED_NO_MAIN", legacy_main_start);
    REQUIRE(legacy_main_start != std::string::npos);
    REQUIRE(legacy_main_end != std::string::npos);
    const auto legacy_main = legacy_code.substr(legacy_main_start, legacy_main_end - legacy_main_start);
    CHECK(legacy_main.find("generated_update_project(registry, dispatcher, dt);") != std::string::npos);
    CHECK(legacy_main.find("generated_render_project(registry, dispatcher);") != std::string::npos);
    const auto legacy_update = generated_function(legacy_code, "void generated_update_project");
    CHECK(legacy_update.find("legacy_tick") == std::string::npos);
    CHECK(legacy_code.find("struct TickEvent") == std::string::npos);
}

TEST_CASE("Codegen EnTT: phase activations drain stable bounded event cascades",
          "[codegen-entt][phase-runtime][event-cascade][5.4]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "module game.cascade\n"
        "pub extern event frame:\n"
        "    dt: float\n"
        "event Contact:\n"
        "    amount: int\n"
        "event Reaction:\n"
        "    amount: int\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "system Producer:\n"
        "    on tick:\n"
        "        emit Contact:\n"
        "            amount = 1\n"
        "system FirstContact:\n"
        "    on Contact:\n"
        "        emit Reaction:\n"
        "            amount = Contact.amount\n"
        "system SecondContact:\n"
        "    on Contact:\n"
        "        let sample = Contact.amount\n"
        "system ReactionConsumer:\n"
        "    on Reaction:\n"
        "        let sample = Reaction.amount\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("std::deque<CactusQueuedEvent> root_event_queue;") != std::string::npos);
    CHECK(code.find("const auto next_depth = activation.current_cascade_depth + 1;") != std::string::npos);
    CHECK(code.find("if (next_depth > kCactusMaxEventCascadeDepth)") != std::string::npos);
    CHECK(code.find("activation.deferred_events.push_back(std::move(queued));") != std::string::npos);
    CHECK(code.find("generated_emit_event(ContactEvent{.amount = 1});") != std::string::npos);
    CHECK(code.find("generated_emit_event(ReactionEvent{.amount = Contact.amount});") != std::string::npos);
    CHECK(code.find("generated_drain_event_cascade(registry);") != std::string::npos);
    CHECK(code.find("generated_dispatch_event(registry, occurrence)") != std::string::npos);

    const auto contact_dispatch =
        code.find("void generated_dispatch_event(entt::registry& registry, const ContactEvent& occurrence)");
    REQUIRE(contact_dispatch != std::string::npos);
    const auto first_call  = code.find("::first_contact_Contact(registry, occurrence);", contact_dispatch);
    const auto second_call = code.find("::second_contact_Contact(registry, occurrence);", contact_dispatch);
    REQUIRE(first_call != std::string::npos);
    REQUIRE(second_call != std::string::npos);
    CHECK(first_call < second_call);
}

TEST_CASE("Codegen EnTT: graph structural commands commit after cascades and between fixed repetitions",
          "[codegen-entt][phase-runtime][commands][5.5][5.6]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "module game.commands\n"
        "pub extern event frame:\n"
        "    dt: float\n"
        "event Contact:\n"
        "    victim: entity_id\n"
        "trait Position:\n"
        "    var x: float\n"
        "trait Active\n"
        "template Particle:\n"
        "    Position:\n"
        "        x = 0.0\n"
        "phase fixed_tick:\n"
        "    from:\n"
        "        frame\n"
        "    every: 0.5\n"
        "    max: 2\n"
        "system Producer:\n"
        "    filter:\n"
        "        Position\n"
        "    on fixed_tick:\n"
        "        let particle = spawn Particle:\n"
        "            Position:\n"
        "                x = 1.0\n"
        "        add Active to particle\n"
        "        remove Active from self\n"
        "        emit Contact:\n"
        "            victim = self\n"
        "system ContactConsumer:\n"
        "    on Contact:\n"
        "        destroy Contact.victim\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("auto __spawned = cactus::runtime::entt_backend::generated_reserve_entity(registry);") !=
          std::string::npos);
    CHECK(code.find("create_particle_at(registry, __spawned);") != std::string::npos);
    CHECK(code.find("generated_queue_structural_command(") != std::string::npos);
    CHECK(code.find("CactusStructuralCommand::Kind::Spawn") != std::string::npos);
    CHECK(code.find("CactusStructuralCommand::Kind::Add") != std::string::npos);
    CHECK(code.find("CactusStructuralCommand::Kind::Remove") != std::string::npos);
    CHECK(code.find("CactusStructuralCommand::Kind::Destroy") != std::string::npos);

    const auto fixed_batch = code.find("void generated_run_phase_batch_game_commands__fixed_tick");
    REQUIRE(fixed_batch != std::string::npos);
    const auto repetition =
        code.find("for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition)", fixed_batch);
    const auto cascade  = code.find("generated_drain_event_cascade(registry);", repetition);
    const auto commit   = code.find("generated_commit_activation(registry);", repetition);
    const auto inactive = code.find("scheduler.activation.active = false;", repetition);
    REQUIRE(repetition != std::string::npos);
    REQUIRE(cascade != std::string::npos);
    REQUIRE(commit != std::string::npos);
    REQUIRE(inactive != std::string::npos);
    CHECK(cascade < commit);
    CHECK(commit < inactive);

    const auto commit_fn = code.find("void generated_commit_activation(entt::registry& registry)");
    REQUIRE(commit_fn != std::string::npos);
    const auto move_commands  = code.find("auto commands = std::move(activation.commands);", commit_fn);
    const auto apply_commands = code.find("command.apply(registry);", commit_fn);
    REQUIRE(move_commands != std::string::npos);
    REQUIRE(apply_commands != std::string::npos);
    CHECK(move_commands < apply_commands);

    ProgramNode legacy_program;
    const auto legacy = full_pipeline(
        "trait Position:\n"
        "    var x: float\n"
        "template Particle:\n"
        "    Position:\n"
        "        x = 0.0\n"
        "event tick:\n"
        "    dt: float\n"
        "system LegacySpawner:\n"
        "    on tick:\n"
        "        let particle = spawn Particle:\n"
        "            Position:\n"
        "                x = 1.0\n",
        legacy_program);
    const auto legacy_code = CppEnttCodegen::generate(legacy);
    CHECK(legacy_code.find("auto __spawned = create_particle(registry);") != std::string::npos);
    CHECK(legacy_code.find("generated_reserve_entity") == std::string::npos);
    CHECK(legacy_code.find("generated_queue_structural_command") == std::string::npos);
}

TEST_CASE("Codegen EnTT: compiler-owned contracted effects execute only through stable graph dispatch",
          "[codegen-entt][external-handler][effects][6.4]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.sprites\n"
        "pub extern event frame:\n"
        "    dt: float\n"
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
        "phase render:\n"
        "    from:\n"
        "        frame\n"
        "extern system SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on render:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n"
        "        effects:\n"
        "            graphics\n"
        "extern system AnimatedSpriteSystem:\n"
        "    filter:\n"
        "        AnimatedSprite\n"
        "    on render:\n"
        "        writes:\n"
        "            AnimatedSprite\n"
        "        effects:\n"
        "            graphics\n",
        program);

    for (auto& declaration : program.declarations) {
        if (auto* system = std::get_if<ExternSystemNode>(&declaration)) {
            system->is_stdlib = true;
        }
    }

    const auto code = CppEnttCodegen::generate(decorated);
    const auto dispatch =
        generated_function(code,
                           "void generated_dispatch_phase_std_render_sprites__render(entt::registry& registry, const "
                           "std_render_sprites__renderPhaseRuntimeState& phase)");
    const auto first  = dispatch.find("::sprite_renderer_tick(registry);");
    const auto second = dispatch.find("::animated_sprite_system_tick(registry);");
    REQUIRE(first != std::string::npos);
    REQUIRE(second != std::string::npos);
    CHECK(first < second);

    CHECK(code.find("cactus_external__std_render_sprites__SpriteRenderer") == std::string::npos);
    CHECK(code.find("cactus_external__std_render_sprites__AnimatedSpriteSystem") == std::string::npos);
    const auto update = generated_function(code, "void generated_update_project(");
    const auto render = generated_function(code, "void generated_render_project(");
    CHECK(update.find("sprite_renderer_tick") == std::string::npos);
    CHECK(update.find("animated_sprite_system_tick") == std::string::npos);
    CHECK(render.find("sprite_renderer_tick") == std::string::npos);
    CHECK(render.find("animated_sprite_system_tick") == std::string::npos);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
