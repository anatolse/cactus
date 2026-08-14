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

// system_emitter.cpp's gen_temp_name()/foreach_temp_name() mangle compiler-generated
// temporaries with a per-call-site line/column suffix (e.g. "cactus_gen_spawned_12_5") so they
// can never collide with a user-authored DSL identifier spliced into the same scope. Test
// fixtures can't predict the exact suffix in advance, so locate the full mangled identifier by
// its stable prefix (e.g. "cactus_gen_spawned_") and reuse the returned name to check its later,
// consistent occurrences.
static std::string extract_temp_name(const std::string& code, const std::string& prefix) {
    const auto start = code.find(prefix);
    if (start == std::string::npos) {
        return {};
    }
    auto end = start;
    while (end < code.size() && (std::isalnum(static_cast<unsigned char>(code[end])) != 0 || code[end] == '_')) {
        ++end;
    }
    return code.substr(start, end - start);
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

extern rule Integrate:
    filter:
        Position
        Velocity
    on simulate:
        reads:
            Position
        projects:
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

extern rule Monitor:
    on simulate:
        effects:
            host.observe
)";
    ProgramNode ast;
    auto program = full_pipeline(source, ast);

    const auto generated = CppEnttCodegen::generate(program);

    CHECK(generated.find("struct Capabilities__game__Integrate__on__game__simulate") != std::string::npos);
    CHECK(generated.find("void emit_game__Contact(ContactEvent occurrence) const") != std::string::npos);
    CHECK(generated.find("[[nodiscard]] entt::entity command_spawn_game__Particle() const") != std::string::npos);
    CHECK(generated.find("void command_destroy(entt::entity target) const") != std::string::npos);
    CHECK(generated.find("void command_add_game__Disabled(entt::entity target, game__Disabled value = {}) const") !=
          std::string::npos);
    CHECK(generated.find("void command_remove_game__Disabled(entt::entity target) const") != std::string::npos);
    CHECK(generated.find("EffectService effect_physics() const noexcept") != std::string::npos);
    CHECK(generated.find("[[nodiscard]] game__Position* project_game__Position(entt::entity target) const") !=
          std::string::npos);
    CHECK(generated.find("if (!registry.valid(target)) { return nullptr; }") != std::string::npos);
    CHECK(generated.find("return &::project_game__Position(registry, target);") != std::string::npos);
    CHECK(generated.find("void cactus_external__game__Integrate__on__game__simulate(") != std::string::npos);
    CHECK(generated.find("entt::entity entity") != std::string::npos);
    CHECK(generated.find("const game__Position& read_game__Position") != std::string::npos);
    CHECK(generated.find("game__Velocity& write_game__Velocity") != std::string::npos);
    CHECK(generated.find("for (const auto entity : registry.view<game__Position, game__Velocity>())") !=
          std::string::npos);
    CHECK(generated.find("::cactus_external__game__Integrate__on__game__simulate(") != std::string::npos);
    CHECK(generated.find("Capabilities__game__Integrate__on__game__simulate{registry}") != std::string::npos);

    const auto integrate_capabilities = generated.find("struct Capabilities__game__Integrate__on__game__simulate");
    REQUIRE(integrate_capabilities != std::string::npos);
    const auto integrate_capabilities_end = generated.find("\n};\n\n", integrate_capabilities);
    REQUIRE(integrate_capabilities_end != std::string::npos);
    const auto integrate_surface =
        generated.substr(integrate_capabilities, integrate_capabilities_end - integrate_capabilities);
    CHECK(integrate_surface.find("std::function") == std::string::npos);
    CHECK(integrate_surface.find("project_game__Position") != std::string::npos);
    CHECK(integrate_surface.find("emit_game__Contact") != std::string::npos);
    CHECK(integrate_surface.find("effect_physics") != std::string::npos);
    CHECK(integrate_surface.find("effect_host__observe") == std::string::npos);

    CHECK(generated.find("void cactus_external__game__Monitor__on__game__simulate(") != std::string::npos);
    CHECK(generated.find("::cactus_external__game__Monitor__on__game__simulate(") != std::string::npos);
    const auto monitor_capabilities = generated.find("struct Capabilities__game__Monitor__on__game__simulate");
    REQUIRE(monitor_capabilities != std::string::npos);
    const auto monitor_capabilities_end = generated.find("\n};\n\n", monitor_capabilities);
    REQUIRE(monitor_capabilities_end != std::string::npos);
    const auto monitor_surface =
        generated.substr(monitor_capabilities, monitor_capabilities_end - monitor_capabilities);
    CHECK(monitor_surface.find("emit_") == std::string::npos);
    CHECK(monitor_surface.find("project_") == std::string::npos);
    CHECK(monitor_surface.find("command_") == std::string::npos);
    CHECK(monitor_surface.find("effect_host__observe") != std::string::npos);
    CHECK(monitor_surface.find("effect_physics") == std::string::npos);
    CHECK(generated.find("void game__Integrate__tick(entt::registry& registry)") == std::string::npos);
    CHECK(generated.find("void game__Monitor__tick(entt::registry& registry)") == std::string::npos);
}

// A field's default member initializer must come from the trait's own `=
// expression` in the parsed AST (the DSL source of truth), not a backend-side
// copy that can silently drift from it — so `emit_component` takes the
// program's AST and looks the default up by (module, trait, field) instead of
// hardcoding it. A field with no declared default still falls back to `{}`
// (checked by the pre-existing "component struct from trait" test above,
// which calls `emit_component` with no AST at all).
TEST_CASE("Codegen EnTT: component struct field defaults come from the trait's own declaration",
          "[codegen-entt][defaults]") {
    ProgramNode ast;
    auto decorated = full_pipeline(
        "trait Defaults:\n"
        "    var f: float = 1.5\n"
        "    var v2: vec2 = vec2(1.0, 2.0)\n"
        "    var v3: vec3 = vec3(1.0, 2.0, 3.0)\n"
        "    var q: quat = quat(0.0, 0.0, 0.0, 1.0)\n"
        "    var flag: bool = true\n"
        "    var n: int = 7\n"
        "    var c: color = #FF0000FF\n"
        "    var items: list[int] = [1, 2]\n"
        "    var empty_items: list[int] = []\n"
        "    var s: string = \"hi\"\n"
        "    var empty_s: string = \"\"\n"
        "    var required: float\n",
        ast);

    ResolvedTrait defaults;
    defaults.name        = "Defaults";
    defaults.module_name = "test";  // full_pipeline auto-prepends `module test`
    defaults.fields.push_back({.name = "f", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});
    defaults.fields.push_back({.name = "v2", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});
    defaults.fields.push_back({.name = "v3", .type = {.kind = TypeKind::Vec3, .name = "vec3"}, .is_var = true});
    defaults.fields.push_back({.name = "q", .type = {.kind = TypeKind::Quat, .name = "quat"}, .is_var = true});
    defaults.fields.push_back({.name = "flag", .type = {.kind = TypeKind::Bool, .name = "bool"}, .is_var = true});
    defaults.fields.push_back({.name = "n", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});
    defaults.fields.push_back({.name = "c", .type = {.kind = TypeKind::Color, .name = "color"}, .is_var = true});
    const auto int_element = std::make_shared<TypeInfo>(TypeInfo{.kind = TypeKind::Int, .name = "int"});
    defaults.fields.push_back(
        {.name = "items", .type = {.kind = TypeKind::List, .name = "list", .element = int_element}, .is_var = true});
    defaults.fields.push_back({.name   = "empty_items",
                               .type   = {.kind = TypeKind::List, .name = "list", .element = int_element},
                               .is_var = true});
    defaults.fields.push_back({.name = "s", .type = {.kind = TypeKind::String, .name = "string"}, .is_var = true});
    defaults.fields.push_back(
        {.name = "empty_s", .type = {.kind = TypeKind::String, .name = "string"}, .is_var = true});
    defaults.fields.push_back({.name = "required", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});

    const auto code = EnttComponentEmitter::emit_component(defaults, decorated);
    CHECK(code.find("float f = 1.5F;") != std::string::npos);
    CHECK(code.find("Vector2 v2 = vec2(1.0F, 2.0F);") != std::string::npos);
    CHECK(code.find("Vector3 v3 = vec3(1.0F, 2.0F, 3.0F);") != std::string::npos);
    CHECK(code.find("Quat q = quat(0.0F, 0.0F, 0.0F, 1.0F);") != std::string::npos);
    CHECK(code.find("bool flag = true;") != std::string::npos);
    CHECK(code.find("int n = 7;") != std::string::npos);
    CHECK(code.find("Color c = Color{.r = 255, .g = 0, .b = 0, .a = 255};") != std::string::npos);
    CHECK(code.find("std::vector<int> items = {1, 2};") != std::string::npos);
    CHECK(code.find("std::vector<int> empty_items = {};") != std::string::npos);
    CHECK(code.find("std::string s = \"hi\";") != std::string::npos);
    // A declared `= ""` default is redundant with std::string's own default
    // construction, and spelling it out trips clang-tidy's
    // readability-redundant-string-init on generated output.
    CHECK(code.find("std::string empty_s{};") != std::string::npos);
    CHECK(code.find("empty_s = \"\"") == std::string::npos);
    CHECK(code.find("float required{};") != std::string::npos);
}

// Trait names can collide across modules (e.g. stdlib declares a trait named
// "WorldTransform" once in `std.transform.flat` and once in
// `std.transform.volume`, with different defaults) — the AST lookup must key
// off the enclosing module, not match the first same-named trait it finds.
TEST_CASE("Codegen EnTT: same-named traits in different modules resolve independent defaults",
          "[codegen-entt][defaults]") {
    ProgramNode ast_a;
    full_pipeline("module mod_a\n\ntrait Thing:\n    var x: float = 1.0\n", ast_a);
    ProgramNode ast_b;
    full_pipeline("module mod_b\n\ntrait Thing:\n    var x: float = 2.0\n", ast_b);

    ProgramNode combined;
    for (auto& decl : ast_a.declarations) {
        combined.declarations.push_back(std::move(decl));
    }
    for (auto& decl : ast_b.declarations) {
        combined.declarations.push_back(std::move(decl));
    }
    DecoratedProgram combined_program;
    combined_program.ast = &combined;

    ResolvedTrait thing_a;
    thing_a.name        = "Thing";
    thing_a.module_name = "mod_a";
    thing_a.fields.push_back({.name = "x", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});

    ResolvedTrait thing_b;
    thing_b.name        = "Thing";
    thing_b.module_name = "mod_b";
    thing_b.fields.push_back({.name = "x", .type = {.kind = TypeKind::Float, .name = "float"}, .is_var = true});

    const auto code_a = EnttComponentEmitter::emit_component(thing_a, combined_program);
    CHECK(code_a.find("float x = 1.0F;") != std::string::npos);

    const auto code_b = EnttComponentEmitter::emit_component(thing_b, combined_program);
    CHECK(code_b.find("float x = 2.0F;") != std::string::npos);
}

// A default expression may call a module-aliased stdlib extern func (e.g.
// stdlib's own `rand.seeded(0)`), which needs the same stdlib call-site
// rewriting handler-body assignments already get — the DecoratedProgram
// overload of emit_expr, not the bare-AST one.
TEST_CASE("Codegen EnTT: field default calling a module-aliased stdlib func resolves to the runtime namespace",
          "[codegen-entt][defaults][stdlib]") {
    ProgramNode ast;
    auto decorated = full_pipeline(
        "use std.random as rand\n"
        "trait TreeRng:\n"
        "    var rng: int = rand.seeded(0)\n",
        ast);

    ResolvedTrait tree_rng;
    tree_rng.name        = "TreeRng";
    tree_rng.module_name = "test";
    tree_rng.fields.push_back({.name = "rng", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});

    const auto code = EnttComponentEmitter::emit_component(tree_rng, decorated);
    CHECK(code.find("cactus::runtime::stdlib::random::seeded(0)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: registry view rule", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event step:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "    var y: float\n"
        "rule Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on step:\n"
        "        x = x + step.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
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

// event field type resolution previously matched field.type.name against a hardcoded
// primitive list plus program.structs, with no program.enums branch — an enum-typed event
// field silently fell through to the int fallback instead of resolving to its (possibly
// module-prefixed) generated enum class type. Module name is set explicitly here so the
// expected type is prefixed ("std_editor__GizmoMode"), which a naive bare-name fallback
// (no program.enums lookup) would not produce.
TEST_CASE("Codegen EnTT: enum-typed event field resolves to its generated enum class type",
          "[codegen-entt][events][enum]") {
    ResolvedEnum gizmo_mode;
    gizmo_mode.name        = "GizmoMode";
    gizmo_mode.module_name = "std.editor";
    gizmo_mode.variants    = {"Select", "Translate", "Rotate", "Scale", "Place"};

    DecoratedProgram program;
    program.enums["GizmoMode"] = gizmo_mode;

    EventNode event;
    event.name        = "EditorModeChanged";
    event.module_name = "std.editor";
    event.fields.push_back({.name = "previous_mode", .type = {.name = "GizmoMode"}});
    event.fields.push_back({.name = "current_mode", .type = {.name = "GizmoMode"}});

    const auto code = EnttEventEmitter::emit_event(event, program);
    CHECK(code.find("std_editor__GizmoMode previous_mode;") != std::string::npos);
    CHECK(code.find("std_editor__GizmoMode current_mode;") != std::string::npos);
    CHECK(code.find("int previous_mode;") == std::string::npos);
    CHECK(code.find("int current_mode;") == std::string::npos);
}

TEST_CASE("Codegen EnTT: full pipeline", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick: \n"
        "    dt: float\n"
        "trait Pos:\n    persist sync var x: float\n    persist sync var y: float\n"
        "rule Move:\n"
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
        "rule ReadMouse:\n"
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
        "rule Demo:\n"
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
        "rule Demo:\n"
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
        "rule Demo:\n"
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
        "rule QueryProbe:\n"
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
        "rule Cleanup:\n"
        "    on tick:\n"
        "        destroy self\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::destroy_entity_recursive(") != std::string::npos);
}

TEST_CASE("Codegen EnTT: sprite and animation extern rules bind to asset runtime adapters", "[codegen-entt][assets]") {
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
        "extern rule SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n"
        "extern rule SpriteAnimation:\n"
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
        "extern rule SpriteRenderer:\n"
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

TEST_CASE("Codegen EnTT: mesh renderer extern rule binds to backend runtime without user callback",
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
        "    var color: color\n"
        "event run\n"
        "extern rule MeshRenderer:\n"
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
    CHECK(code.find("Renderer_comp.color") != std::string::npos);
    CHECK(code.find("void mesh_renderer_update(") == std::string::npos);
}

TEST_CASE("Codegen EnTT: shape renderer draws Circle shapes via draw_shape_circle alongside Rectangle",
          "[codegen-entt][render][shapes]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.shapes\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "enum ShapeType:\n"
        "    Rectangle\n"
        "    Circle\n"
        "trait Shape:\n"
        "    var type: ShapeType\n"
        "    var size: vec2\n"
        "    var color: color\n"
        "    var visible: bool\n"
        "    var origin: vec2\n"
        "event render\n"
        "extern rule ShapeRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Shape\n"
        "    on render:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Shape\n"
        "        effects:\n"
        "            graphics\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    // Circle draws via draw_shape_circle, passing size.x as diameter and origin through.
    CHECK(code.find("case ShapeType::Circle:") != std::string::npos);
    CHECK(code.find("draw_shape_circle(WorldTransform_comp.position") != std::string::npos);
    CHECK(code.find("Shape_comp.size.x,") != std::string::npos);
    CHECK(code.find("Shape_comp.origin,") != std::string::npos);
    // Rectangle draws via draw_shape_rectangle, passing origin and rotation through.
    CHECK(code.find("case ShapeType::Rectangle:") != std::string::npos);
    CHECK(code.find("draw_shape_rectangle(WorldTransform_comp.position") != std::string::npos);
    CHECK(code.find("WorldTransform_comp.rotation,") != std::string::npos);
}

// editor-debug-draw: each std.debug event has exactly one generic, non-branching,
// non-looping handler — unlike every filter/phase extern rule above (which iterates a
// `registry.view<...>().each(...)`), these take the event occurrence directly (event
// dispatch already delivers exactly one invocation per `emit`), so "emit once -> one draw
// call", "emit N times -> N draw calls", and "emit zero times -> no draw call" all follow
// structurally from there being no view, no loop, and no conditional around the single
// draw call in the generated body — checked here as "the body is exactly one unconditional
// raylib call reading occurrence fields." Declared and tested independent of std.editor.
TEST_CASE("Codegen EnTT: std.debug 2D event renderers issue one unconditional raylib call each",
          "[codegen-entt][render][debug-draw]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.debug\n"
        "pub event DrawDebugLine2D:\n"
        "    start: vec2\n"
        "    end: vec2\n"
        "    color: color\n"
        "    thickness: float\n"
        "pub event DrawDebugTriangle2D:\n"
        "    a: vec2\n"
        "    b: vec2\n"
        "    c: vec2\n"
        "    color: color\n"
        "pub event DrawDebugRingOutline2D:\n"
        "    center: vec2\n"
        "    inner_radius: float\n"
        "    outer_radius: float\n"
        "    color: color\n"
        "pub event DrawDebugRectOutline2D:\n"
        "    position: vec2\n"
        "    size: vec2\n"
        "    thickness: float\n"
        "    color: color\n"
        "pub extern rule DrawDebugLine2DRenderer:\n"
        "    on DrawDebugLine2D:\n"
        "        effects:\n"
        "            graphics\n"
        "pub extern rule DrawDebugTriangle2DRenderer:\n"
        "    on DrawDebugTriangle2D:\n"
        "        effects:\n"
        "            graphics\n"
        "pub extern rule DrawDebugRingOutline2DRenderer:\n"
        "    on DrawDebugRingOutline2D:\n"
        "        effects:\n"
        "            graphics\n"
        "pub extern rule DrawDebugRectOutline2DRenderer:\n"
        "    on DrawDebugRectOutline2D:\n"
        "        effects:\n"
        "            graphics\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    const auto line_tick = generated_function(code, "void draw_debug_line2_d_renderer_tick");
    CHECK(line_tick.find("const DrawDebugLine2DEvent& occurrence") != std::string::npos);
    CHECK(line_tick.find("BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d())") != std::string::npos);
    CHECK(line_tick.find("DrawLineEx(occurrence.start, occurrence.end, occurrence.thickness, occurrence.color)") !=
          std::string::npos);
    CHECK(line_tick.find("EndMode2D()") != std::string::npos);
    CHECK(count_occurrences(line_tick, "DrawLineEx(") == 1);

    const auto tri_tick = generated_function(code, "void draw_debug_triangle2_d_renderer_tick");
    CHECK(tri_tick.find("DrawTriangle(occurrence.a, occurrence.b, occurrence.c, occurrence.color)") !=
          std::string::npos);
    CHECK(count_occurrences(tri_tick, "DrawTriangle(") == 1);

    const auto ring_tick = generated_function(code, "void draw_debug_ring_outline2_d_renderer_tick");
    CHECK(ring_tick.find("DrawRing(occurrence.center, occurrence.inner_radius, occurrence.outer_radius, 0.0F, "
                         "360.0F, 32, occurrence.color)") != std::string::npos);

    const auto rect_tick = generated_function(code, "void draw_debug_rect_outline2_d_renderer_tick");
    CHECK(rect_tick.find(".x = occurrence.position.x") != std::string::npos);
    CHECK(rect_tick.find("DrawRectangleLinesEx(__rect, occurrence.thickness, occurrence.color)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: std.debug 3D event renderers issue one unconditional raylib call each",
          "[codegen-entt][render][debug-draw]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.debug\n"
        "pub event DrawDebugLine3D:\n"
        "    start: vec3\n"
        "    end: vec3\n"
        "    color: color\n"
        "pub event DrawDebugWireBox3D:\n"
        "    center: vec3\n"
        "    size: vec3\n"
        "    color: color\n"
        "pub event DrawDebugCircle3D:\n"
        "    center: vec3\n"
        "    radius: float\n"
        "    normal: vec3\n"
        "    color: color\n"
        "pub event DrawDebugCube3D:\n"
        "    center: vec3\n"
        "    size: vec3\n"
        "    color: color\n"
        "pub extern rule DrawDebugLine3DRenderer:\n"
        "    on DrawDebugLine3D:\n"
        "        effects:\n"
        "            graphics\n"
        "pub extern rule DrawDebugWireBox3DRenderer:\n"
        "    on DrawDebugWireBox3D:\n"
        "        effects:\n"
        "            graphics\n"
        "pub extern rule DrawDebugCircle3DRenderer:\n"
        "    on DrawDebugCircle3D:\n"
        "        effects:\n"
        "            graphics\n"
        "pub extern rule DrawDebugCube3DRenderer:\n"
        "    on DrawDebugCube3D:\n"
        "        effects:\n"
        "            graphics\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    const auto line_tick = generated_function(code, "void draw_debug_line3_d_renderer_tick");
    CHECK(line_tick.find("const DrawDebugLine3DEvent& occurrence") != std::string::npos);
    CHECK(line_tick.find("BeginMode3D(cactus::runtime::entt_backend::get_active_camera_3d())") != std::string::npos);
    CHECK(line_tick.find("DrawLine3D(occurrence.start, occurrence.end, occurrence.color)") != std::string::npos);
    CHECK(line_tick.find("EndMode3D()") != std::string::npos);

    const auto box_tick = generated_function(code, "void draw_debug_wire_box3_d_renderer_tick");
    CHECK(box_tick.find("DrawCubeWiresV(occurrence.center, occurrence.size, occurrence.color)") != std::string::npos);

    const auto circle_tick = generated_function(code, "void draw_debug_circle3_d_renderer_tick");
    CHECK(circle_tick.find("DrawCircle3D(occurrence.center, occurrence.radius, occurrence.normal, 90.0F, "
                           "occurrence.color)") != std::string::npos);

    const auto cube_tick = generated_function(code, "void draw_debug_cube3_d_renderer_tick");
    CHECK(cube_tick.find("DrawCubeV(occurrence.center, occurrence.size, occurrence.color)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: DebugGrid3D draws the grid only when an EditorCamera3D rig exists",
          "[codegen-entt][stdlib][editor][debug-draw]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "pub trait EditorCamera3D:\n"
        "    var focus: vec3\n"
        "event render\n"
        "pub extern rule DebugGrid3D:\n"
        "    filter:\n"
        "        EditorCamera3D\n"
        "    on render:\n"
        "        effects:\n"
        "            graphics\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void debug_grid3_d_tick");
    CHECK(tick.find("registry.view<EditorCamera3D>()") != std::string::npos);
    CHECK(tick.find("if (!__has_3d_rig) { return; }") != std::string::npos);
    CHECK(tick.find("DrawGrid(20, 1.0F)") != std::string::npos);
}

// editor-screen-ui: same one-event-one-unconditional-handler shape as std.debug (see the
// comment above the std.debug tests) — DrawScreenRect's handler branches only on `filled`
// to pick which single raylib call to issue, still with no view/loop, so the same "N emits
// -> N draws" reasoning holds. Declared and tested independent of std.editor.
TEST_CASE("Codegen EnTT: std.ui DrawScreenRect renderer draws filled or outline based on the occurrence",
          "[codegen-entt][render][screen-ui]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.ui\n"
        "pub event DrawScreenRect:\n"
        "    position: vec2\n"
        "    size: vec2\n"
        "    color: color\n"
        "    filled: bool\n"
        "    thickness: float\n"
        "pub extern rule DrawScreenRectRenderer:\n"
        "    on DrawScreenRect:\n"
        "        effects:\n"
        "            graphics\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void draw_screen_rect_renderer_tick");
    CHECK(tick.find("const DrawScreenRectEvent& occurrence") != std::string::npos);
    CHECK(tick.find(".x = occurrence.position.x") != std::string::npos);
    CHECK(tick.find(".width = occurrence.size.x") != std::string::npos);
    CHECK(tick.find("if (occurrence.filled)") != std::string::npos);
    CHECK(tick.find("DrawRectangleRec(__rect, occurrence.color)") != std::string::npos);
    CHECK(tick.find("DrawRectangleLinesEx(__rect, occurrence.thickness, occurrence.color)") != std::string::npos);
    // Screen-space: no camera-mode wrapping (world-space std.debug primitives get one).
    CHECK(tick.find("BeginMode2D") == std::string::npos);
    CHECK(tick.find("BeginMode3D") == std::string::npos);
}

TEST_CASE("Codegen EnTT: model renderer extern rule binds to backend runtime without user callback",
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
        "extern rule ModelRender:\n"
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
    CHECK(code.find("void model_render_update(") == std::string::npos);
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
        "extern rule ModelAnimation:\n"
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
    const auto tick = generated_function(code, "void model_animation_tick");
    CHECK(tick.find("registry.view<ModelRenderer, ModelAnimator>()") != std::string::npos);
    CHECK(tick.find("if (!ModelAnimator_comp.playing)") != std::string::npos);
    CHECK(tick.find("ModelAnimator_comp.time += kFixedDt * ModelAnimator_comp.speed;") != std::string::npos);
    CHECK(tick.find("cactus::runtime::entt_backend::model_animation_duration(") != std::string::npos);
    CHECK(tick.find("std::fmod(ModelAnimator_comp.time, duration)") != std::string::npos);

    // The adapter owns time advancement, but a generic event name no longer
    // causes the backend to infer update or render scheduling.
    const auto update = generated_function(code, "void generated_update_project");
    CHECK(update.find("model_animation_tick(registry);") == std::string::npos);
    const auto render = generated_function(code, "void generated_render_project");
    CHECK(render.find("model_animation_tick") == std::string::npos);
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
        "extern rule ModelRender:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        ModelRenderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            ModelRenderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void model_render_tick");
    // Animated entities go through the extended submit_model signature...
    CHECK(tick.find("registry.try_get<ModelAnimator>(entity)") != std::string::npos);
    CHECK(tick.find("animator->clip, animator->time);") != std::string::npos);
    // ...while entities without ModelAnimator keep the plain submission.
    CHECK(tick.find("ModelRenderer_comp.cast_shadow);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: screen label extern rule renders window-space text in flat and volume programs",
          "[codegen-entt][assets][dsl-model-animation]") {
    const std::string screen_label_decls =
        "trait ScreenLabel:\n"
        "    var text: string\n"
        "    var position: vec2\n"
        "    var font_size: int\n"
        "    var color: color\n"
        "    var visible: bool\n"
        "event run\n"
        "extern rule ScreenLabelRender:\n"
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
        const auto tick = generated_function(code, "void screen_label_render_tick");
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
        CHECK(render.find("screen_label_render_tick(registry);") == std::string::npos);
        const auto update = generated_function(code, "void generated_update_project");
        CHECK(update.find("screen_label_render_tick") == std::string::npos);
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
        "rule Probe:\n"
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
        "rule Probe:\n"
        "    filter:\n"
        "        SizeState\n"
        "    on tick:\n"
        "        height = models.bounds_size(Robot).y\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("cactus::runtime::entt_backend::model_bounds_size(Robot)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: on load handler runs via a one-shot boot activation before the frame loop",
          "[codegen-entt][dsl-scene-loading][dynamic-model-spawning][graph-driven-lifecycle-events]") {
    ProgramNode program;
    // `pub event load` stands in for the std.core lifecycle declaration the
    // multi-module pipeline links in; the single-module analyzer needs it to
    // accept `on load:`. `frame` + `phase tick` make this a graph-driven
    // program (boot/teardown activations are scoped to graph_driven_frame).
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "pub event load\n"
        "trait Marker\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n"
        "entity Bootstrap:\n"
        "    Marker\n"
        "rule SpawnEnemies:\n"
        "    filter:\n"
        "        Marker\n"
        "    on load:\n"
        "        spawn Enemy:\n"
        "            Position:\n"
        "                x = 1.0\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(count_occurrences(code, "struct loadEvent") == 1);

    // The compatibility hook remains inert; the boot activation (emitted
    // inline in main()) is what actually dispatches to the handler.
    const auto load_hook = generated_function(code, "void generated_load_project");
    CHECK(load_hook.find("spawn_enemies_load") == std::string::npos);

    // main() startup order: init project, then the (inert) load hook call,
    // then the boot activation itself — inject the load occurrence, run its
    // cascade, commit — all before the frame loop begins.
    const auto main_start = code.find("int main() try {");
    REQUIRE(main_start != std::string::npos);
    const auto main_end = code.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start);
    REQUIRE(main_end != std::string::npos);
    const auto main_fn = code.substr(main_start, main_end - main_start);

    const auto init_pos      = main_fn.find("generated_init_project(registry);");
    const auto load_call_pos = main_fn.find("generated_load_project(registry);");
    const auto dispatch_pos  = main_fn.find("generated_dispatch_event(registry, loadEvent{});");
    const auto drain_pos     = main_fn.find("generated_drain_event_cascade(registry);");
    const auto commit_pos    = main_fn.find("generated_commit_activation(registry);");
    const auto loop_pos      = main_fn.find("while (!WindowShouldClose())");
    REQUIRE(init_pos != std::string::npos);
    REQUIRE(load_call_pos != std::string::npos);
    REQUIRE(dispatch_pos != std::string::npos);
    REQUIRE(drain_pos != std::string::npos);
    REQUIRE(commit_pos != std::string::npos);
    REQUIRE(loop_pos != std::string::npos);
    CHECK(init_pos < load_call_pos);
    CHECK(load_call_pos < dispatch_pos);
    CHECK(dispatch_pos < drain_pos);
    CHECK(drain_pos < commit_pos);
    CHECK(commit_pos < loop_pos);
}

TEST_CASE("Codegen EnTT: programs without a load handler emit no boot activation",
          "[codegen-entt][dsl-scene-loading][dynamic-model-spawning][graph-driven-lifecycle-events]") {
    ProgramNode program;
    // `load` is declared (as it would be via a real `use std.core`) but no
    // rule handles it — the boot activation must not be emitted even though
    // the program is otherwise graph-driven.
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "pub event load\n"
        "trait Pos:\n"
        "    var x: float = 0.0\n"
        "rule Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    // The event is declared and still gets a struct + dispatch overload
    // (mirrors a real program linking std.core), but nothing consumes it.
    CHECK(count_occurrences(code, "struct loadEvent") == 1);

    // The hook is still exported so main() and no-main hosts can call it.
    const auto load_hook = generated_function(code, "void generated_load_project");
    CHECK(load_hook.find("(void)registry;") != std::string::npos);

    const auto main_start = code.find("int main() try {");
    REQUIRE(main_start != std::string::npos);
    const auto main_end = code.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start);
    REQUIRE(main_end != std::string::npos);
    const auto main_fn = code.substr(main_start, main_end - main_start);

    CHECK(main_fn.find("generated_dispatch_event(registry, loadEvent{});") == std::string::npos);

    // init -> load hook -> frame loop, with nothing injected between them.
    const auto init_pos      = main_fn.find("generated_init_project(registry);");
    const auto load_call_pos = main_fn.find("generated_load_project(registry);");
    const auto loop_pos      = main_fn.find("while (!WindowShouldClose())");
    REQUIRE(init_pos != std::string::npos);
    REQUIRE(load_call_pos != std::string::npos);
    REQUIRE(loop_pos != std::string::npos);
    CHECK(init_pos < load_call_pos);
    CHECK(load_call_pos < loop_pos);
}

TEST_CASE("Codegen EnTT: on unload handler runs via a one-shot teardown activation after the frame loop",
          "[codegen-entt][dsl-scene-loading][graph-driven-lifecycle-events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "pub event unload\n"
        "trait Marker\n"
        "entity Bootstrap:\n"
        "    Marker\n"
        "rule SceneCleanup:\n"
        "    filter:\n"
        "        Marker\n"
        "    on unload:\n"
        "        destroy\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(count_occurrences(code, "struct unloadEvent") == 1);

    const auto main_start = code.find("int main() try {");
    REQUIRE(main_start != std::string::npos);
    const auto main_end = code.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start);
    REQUIRE(main_end != std::string::npos);
    const auto main_fn = code.substr(main_start, main_end - main_start);

    // Teardown order: frame loop exits, then the unload activation (inject,
    // cascade, commit), then CloseWindow().
    const auto loop_pos        = main_fn.find("while (!WindowShouldClose())");
    const auto end_drawing_pos = main_fn.find("EndDrawing();");
    const auto dispatch_pos    = main_fn.find("generated_dispatch_event(registry, unloadEvent{});");
    const auto drain_pos       = main_fn.find("generated_drain_event_cascade(registry);");
    const auto commit_pos      = main_fn.find("generated_commit_activation(registry);");
    const auto close_pos       = main_fn.find("CloseWindow();");
    REQUIRE(loop_pos != std::string::npos);
    REQUIRE(end_drawing_pos != std::string::npos);
    REQUIRE(dispatch_pos != std::string::npos);
    REQUIRE(drain_pos != std::string::npos);
    REQUIRE(commit_pos != std::string::npos);
    REQUIRE(close_pos != std::string::npos);
    CHECK(loop_pos < end_drawing_pos);
    CHECK(end_drawing_pos < dispatch_pos);
    CHECK(dispatch_pos < drain_pos);
    CHECK(drain_pos < commit_pos);
    CHECK(commit_pos < close_pos);
}

TEST_CASE("Codegen EnTT: programs without an unload handler emit no teardown activation",
          "[codegen-entt][dsl-scene-loading][graph-driven-lifecycle-events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "pub event unload\n"
        "trait Pos:\n"
        "    var x: float = 0.0\n"
        "rule Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(count_occurrences(code, "struct unloadEvent") == 1);

    const auto main_start = code.find("int main() try {");
    REQUIRE(main_start != std::string::npos);
    const auto main_end = code.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start);
    REQUIRE(main_end != std::string::npos);
    const auto main_fn = code.substr(main_start, main_end - main_start);

    CHECK(main_fn.find("generated_dispatch_event(registry, unloadEvent{});") == std::string::npos);

    const auto loop_pos  = main_fn.find("while (!WindowShouldClose())");
    const auto close_pos = main_fn.find("CloseWindow();");
    REQUIRE(loop_pos != std::string::npos);
    REQUIRE(close_pos != std::string::npos);
    CHECK(loop_pos < close_pos);
}

TEST_CASE("Codegen EnTT: commit emits a spawn notification only when an on spawn handler exists",
          "[codegen-entt][graph-driven-lifecycle-events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "event spawn\n"
        "event destroy\n"
        "trait Pos:\n"
        "    var x: float\n"
        "rule OnSpawn:\n"
        "    filter:\n"
        "        Pos\n"
        "    on spawn:\n"
        "        x = x + 1.0\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    // Gating is per-event: an `on spawn` handler emits spawn-notification
    // codegen without also emitting destroy-notification codegen.
    const auto commit_fn = generated_function(code, "void generated_commit_activation");
    CHECK(commit_fn.find("while (!activation.commands.empty())") != std::string::npos);
    CHECK(commit_fn.find("StructuralCommand::Kind::Spawn") != std::string::npos);
    CHECK(commit_fn.find("generated_emit_event(spawnEvent{});") != std::string::npos);
    CHECK(commit_fn.find("StructuralCommand::Kind::Destroy") == std::string::npos);
    CHECK(commit_fn.find("generated_drain_event_cascade(registry);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: commit emits a destroy notification only when an on destroy handler exists",
          "[codegen-entt][graph-driven-lifecycle-events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "event spawn\n"
        "event destroy\n"
        "trait Pos:\n"
        "    var x: float\n"
        "rule OnDestroy:\n"
        "    filter:\n"
        "        Pos\n"
        "    on destroy:\n"
        "        x = x + 1.0\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    const auto commit_fn = generated_function(code, "void generated_commit_activation");
    CHECK(commit_fn.find("while (!activation.commands.empty())") != std::string::npos);
    CHECK(commit_fn.find("StructuralCommand::Kind::Destroy") != std::string::npos);
    CHECK(commit_fn.find("generated_emit_event(destroyEvent{});") != std::string::npos);
    CHECK(commit_fn.find("StructuralCommand::Kind::Spawn") == std::string::npos);
    CHECK(commit_fn.find("generated_drain_event_cascade(registry);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: programs without spawn/destroy handlers emit no commit notification codegen",
          "[codegen-entt][graph-driven-lifecycle-events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "event spawn\n"
        "event destroy\n"
        "trait Pos:\n"
        "    var x: float = 0.0\n"
        "rule Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    const auto commit_fn = generated_function(code, "void generated_commit_activation");
    CHECK(commit_fn.find("while (!activation.commands.empty())") == std::string::npos);
    CHECK(commit_fn.find("StructuralCommand::Kind::Spawn") == std::string::npos);
    CHECK(commit_fn.find("StructuralCommand::Kind::Destroy") == std::string::npos);
    CHECK(commit_fn.find("generated_drain_event_cascade(registry);") == std::string::npos);
    CHECK(commit_fn.find("for (auto& command : commands) {") != std::string::npos);
    // The events are still declared (as they would be via a real `use
    // std.core`) and still get struct + dispatch-overload codegen; only the
    // commit-side notification emission is gated on handler presence.
    CHECK(count_occurrences(code, "struct spawnEvent") == 1);
    CHECK(count_occurrences(code, "struct destroyEvent") == 1);
}

TEST_CASE("Codegen EnTT: spawn notification emission reuses the existing cascade-depth cap",
          "[codegen-entt][graph-driven-lifecycle-events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "event spawn\n"
        "trait Pos:\n"
        "    var x: float\n"
        "rule OnSpawn:\n"
        "    filter:\n"
        "        Pos\n"
        "    on spawn:\n"
        "        x = x + 1.0\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    // Commit routes the spawn notification through generated_emit_event — the
    // same cascade-depth-bounded path (kMaxEventCascadeDepth) already
    // used for ordinary handler-emitted events — rather than pushing directly
    // onto the event queue via a new/uncapped path.
    const auto commit_fn = generated_function(code, "void generated_commit_activation");
    CHECK(commit_fn.find("generated_emit_event(spawnEvent{});") != std::string::npos);
    CHECK(commit_fn.find("activation.event_queue.push_back") == std::string::npos);

    // Exactly one cascade-depth cap is declared for the whole program — no
    // second/independent depth-limiting mechanism was introduced.
    CHECK(count_occurrences(code, "kMaxEventCascadeDepth = 64;") == 1);
    CHECK(code.find("if (next_depth > kMaxEventCascadeDepth)") != std::string::npos);
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
        "rule Spawner:\n"
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

    const auto spawned_name = extract_temp_name(system_fn, "cactus_gen_spawned_");
    REQUIRE_FALSE(spawned_name.empty());
    const auto existing_name = extract_temp_name(system_fn, "cactus_gen_existing_");
    REQUIRE_FALSE(existing_name.empty());
    const auto override_name = extract_temp_name(system_fn, "cactus_gen_override_value_");
    REQUIRE_FALSE(override_name.empty());

    CHECK(system_fn.find("auto " + spawned_name + " = create_walker_enemy(registry);") != std::string::npos);
    CHECK(system_fn.find("auto " + existing_name + " = registry.try_get<Health>(" + spawned_name + ");") !=
          std::string::npos);
    CHECK(system_fn.find("auto " + override_name + " = " + existing_name + " ? *" + existing_name + " : Health{};") !=
          std::string::npos);
    CHECK(system_fn.find(override_name + ".armor = 7;") != std::string::npos);
    CHECK(system_fn.find("registry.emplace_or_replace<Health>(" + spawned_name + ", " + override_name + ");") !=
          std::string::npos);
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
        "rule Spawner:\n"
        "    on tick:\n"
        "        let spawned = spawn WalkerEnemy:\n"
        "            Patrol:\n"
        "                speed = 3.0\n",
        program);

    const auto code      = CppEnttCodegen::generate(decorated);
    const auto system_fn = generated_function(code, "void spawner_tick");

    // The outer `let spawned = ...` binding keeps its user-authored name unmangled; the inner
    // spawn-expression temporary (same textual name, previously a genuine shadowing collision)
    // is now mangled precisely so it cannot alias the user's own `spawned` variable.
    const auto inner_spawned_name = extract_temp_name(system_fn, "cactus_gen_spawned_");
    REQUIRE_FALSE(inner_spawned_name.empty());
    const auto existing_name = extract_temp_name(system_fn, "cactus_gen_existing_");
    REQUIRE_FALSE(existing_name.empty());
    const auto override_name = extract_temp_name(system_fn, "cactus_gen_override_value_");
    REQUIRE_FALSE(override_name.empty());

    CHECK(system_fn.find("auto spawned = ([&]()") != std::string::npos);
    CHECK(system_fn.find("auto " + inner_spawned_name + " = create_walker_enemy(registry);") != std::string::npos);
    CHECK(system_fn.find("auto " + existing_name + " = registry.try_get<Patrol>(" + inner_spawned_name + ");") !=
          std::string::npos);
    CHECK(system_fn.find(override_name + ".speed = 3.0F;") != std::string::npos);
    CHECK(system_fn.find("return " + inner_spawned_name + ";") != std::string::npos);
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
        "rule Freeze:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        add Frozen\n"
        "        add Stunned:\n"
        "            duration = 2.0\n"
        "        remove Frozen\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code             = EnttSystemEmitter::emit_system(*sys, decorated);
            const auto value_name = extract_temp_name(code, "cactus_gen_value_");
            REQUIRE_FALSE(value_name.empty());
            CHECK(code.find("registry.emplace_or_replace<Frozen>(entity)") != std::string::npos);
            CHECK(code.find("registry.emplace_or_replace<Stunned>(entity, " + value_name + ")") != std::string::npos);
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
        "rule Cleanup:\n"
        "    on Collision as c:\n"
        "        add Frozen to c.other\n"
        "        remove Frozen from c.other\n"
        "        destroy c.other\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
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
        "rule Combat:\n"
        "    on Collision as c:\n"
        "        emit Hit to c.other:\n"
        "            amount = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (registry.valid(c.other))") != std::string::npos);
            // Buffer identifier is built from the canonical event type name (HitEvent), not
            // the raw source spelling, so cross-module dotted emit names (e.g. "std.debug.Foo")
            // never leak a dot into a C++ identifier.
            CHECK(code.find("HitEvent_buffer.push_back({.amount = 1})") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: exists compiles to registry.valid", "[codegen-entt][entity-id]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Collision:\n"
        "    other: entity_id\n"
        "rule Combat:\n"
        "    on Collision as c:\n"
        "        if exists(c.other):\n"
        "            let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (registry.valid(c.other))") != std::string::npos);
        }
    }
}

// dsl-vector-expressions relies on system_emitter.cpp's existing bare-text
// BinaryExpr/VarAssign lowering needing no changes for vec2/vec3 operands
// (design.md: rewrite_expr already emits "(" + left + " " + op + " " + right
// + ")" / lhs + " " + op + " " + rhs with no per-type dispatch). This
// confirms that lowering is still exactly the bare form once vec2/vec3
// operands are involved - the operators become valid C++ solely because
// common/cactus_runtime.hpp now makes real operators reachable, not because
// codegen changed.
TEST_CASE("Codegen EnTT: vector binary expression and compound assignment lower to bare operator text",
          "[codegen-entt][vector-expressions]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Position:\n"
        "    var pos: vec2\n"
        "trait Motion:\n"
        "    var velocity: vec2\n"
        "rule Move:\n"
        "    filter:\n"
        "        Position as p\n"
        "        Motion as m\n"
        "    on tick:\n"
        "        let sum = p.pos + m.velocity\n"
        "        p.pos += m.velocity\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("auto sum = (p.pos + m.velocity);") != std::string::npos);
            CHECK(code.find("p.pos += m.velocity;") != std::string::npos);
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
        "rule Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.stage\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code                    = EnttSystemEmitter::emit_system(*sys, decorated);
            const auto match_entity_name = extract_temp_name(code, "cactus_gen_match_entity_");
            REQUIRE_FALSE(match_entity_name.empty());
            CHECK(code.find("auto " + match_entity_name + " = c.other") != std::string::npos);
            CHECK(code.find("if (registry.valid(" + match_entity_name + "))") != std::string::npos);
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
        "rule Combat:\n"
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
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code                    = EnttSystemEmitter::emit_system(*sys, decorated);
            const auto match_entity_name = extract_temp_name(code, "cactus_gen_match_entity_");
            REQUIRE_FALSE(match_entity_name.empty());
            CHECK(code.find("auto " + match_entity_name + " = c.other") != std::string::npos);
            CHECK(code.find("auto* b = registry.try_get<Boss>(" + match_entity_name + ")") != std::string::npos);
            CHECK(code.find("registry.all_of<Spike>(" + match_entity_name + ")") != std::string::npos);
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
        "rule Move:\n"
        "    filter:\n"
        "        Pos\n"
        "    on step as s:\n"
        "        x = x + s.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
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
        "rule Init:\n"
        "    filter:\n"
        "        Pos\n"
        "    on spawn:\n"
        "        x = 0.0\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("void init_spawn(entt::registry& registry, const spawnEvent& spawn,") !=
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
        "rule Combat:\n"
        "    on Collision as c:\n"
        "        match c.other:\n"
        "            Boss as b =>\n"
        "                let x = b.stage\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code                    = EnttSystemEmitter::emit_system(*sys, decorated);
            const auto match_entity_name = extract_temp_name(code, "cactus_gen_match_entity_");
            REQUIRE_FALSE(match_entity_name.empty());
            CHECK(code.find("auto* b = registry.try_get<Boss>(" + match_entity_name + ")") != std::string::npos);
            CHECK(code.find("else {") == std::string::npos);
        }
    }
}

// Baseline (pre else-if): captures the exact emitted C++ shape for today's
// only way to write a chained condition — `else:` whose body is a single
// nested `if`. The IfStmt lowering recurses generically over else_body, so
// this compiles to doubly-braced `else { if (...) {...} else {...} }`
// rather than a flat `else if` cascade. Section 5 checks a fresh `else if`
// chain compiles to equivalent behavior against this capture.
TEST_CASE("Codegen EnTT: legacy nested else+if compiles to doubly-braced else { if }", "[codegen-entt][control-flow]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Health:\n"
        "    var hp: int = 0\n"
        "rule HealthState:\n"
        "    filter:\n"
        "        Health\n"
        "    on tick:\n"
        "        if hp > 50:\n"
        "            hp = 1\n"
        "        else:\n"
        "            if hp > 0:\n"
        "                hp = 2\n"
        "            else:\n"
        "                hp = 3\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (Health_comp.hp > 50) {") != std::string::npos);
            CHECK(code.find("Health_comp.hp = 1;") != std::string::npos);
            CHECK(code.find("if (Health_comp.hp > 0) {") != std::string::npos);
            CHECK(code.find("Health_comp.hp = 2;") != std::string::npos);
            CHECK(code.find("Health_comp.hp = 3;") != std::string::npos);

            // The doubly-braced shape: an outer `} else {` immediately
            // followed (module whitespace/indentation) by the nested `if`,
            // itself followed by its own `} else {` for the innermost body —
            // two separate else-blocks, not one flat else-if cascade.
            const auto outer_close_else = code.find("} else {\n");
            REQUIRE(outer_close_else != std::string::npos);
            const auto inner_if = code.find("if (Health_comp.hp > 0) {", outer_close_else);
            REQUIRE(inner_if != std::string::npos);
            const auto inner_close_else = code.find("} else {\n", inner_if);
            REQUIRE(inner_close_else != std::string::npos);
        }
    }
}

// Same condition/body pairing and order as the baseline above ("if hp > 50 /
// else nested-if hp > 0 / else"), but spelled with the new `else if` grammar.
// Compiled behavior must be equivalent: same branch selected for every
// combination of hp values. Before section 5.2 extends the IfStmt lowering to
// walk else_if_branches, the emitter silently drops the middle branch — this
// is expected to fail until that lowering lands.
TEST_CASE("Codegen EnTT: else-if chain compiles equivalent to legacy nested else+if", "[codegen-entt][control-flow]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Health:\n"
        "    var hp: int = 0\n"
        "rule HealthState:\n"
        "    filter:\n"
        "        Health\n"
        "    on tick:\n"
        "        if hp > 50:\n"
        "            hp = 1\n"
        "        else if hp > 0:\n"
        "            hp = 2\n"
        "        else:\n"
        "            hp = 3\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (Health_comp.hp > 50) {") != std::string::npos);
            CHECK(code.find("Health_comp.hp = 1;") != std::string::npos);
            CHECK(code.find("else if (Health_comp.hp > 0) {") != std::string::npos);
            CHECK(code.find("Health_comp.hp = 2;") != std::string::npos);
            CHECK(code.find("Health_comp.hp = 3;") != std::string::npos);

            // Flat cascade, not the doubly-braced legacy shape: exactly one
            // `else if` per emitted handler body. emit_system emits the body
            // twice (targeted-recipient path and broadcast view-iteration
            // path), so 2 occurrences total is the flat-cascade signature —
            // 4 would indicate the doubly-braced `else { if` shape leaking
            // back in.
            CHECK(count_occurrences(code, "else if (") == 2);
            CHECK(code.find("} else {\n") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: nested control flow mutates visible lexical locals without redeclaration",
          "[codegen-entt][lexical-locals]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "event Collision:\n"
        "    other: entity_id\n"
        "trait Values:\n"
        "    var items: list[int]\n"
        "trait Boss:\n"
        "    var rank: int\n"
        "rule Accumulate:\n"
        "    filter:\n"
        "        Values\n"
        "    on Collision as c:\n"
        "        let sum = 0\n"
        "        let maximum = 0\n"
        "        let count = 0\n"
        "        let cursor = 1\n"
        "        for value in items:\n"
        "            sum += value\n"
        "            count += 1\n"
        "            if value > maximum:\n"
        "                maximum = value\n"
        "                cursor = cursor + 1\n"
        "            else:\n"
        "                cursor += 2\n"
        "        match c.other:\n"
        "            Boss as boss =>\n"
        "                sum += boss.rank\n"
        "                count += 1\n"
        "            _ =>\n"
        "                cursor += 4\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("auto sum +=") == std::string::npos);
    CHECK(code.find("auto count +=") == std::string::npos);
    CHECK(code.find("auto maximum = value") == std::string::npos);
    CHECK(code.find("auto cursor = (cursor + 1)") == std::string::npos);
    CHECK(code.find("sum += value;") != std::string::npos);
    CHECK(code.find("count += 1;") != std::string::npos);
    CHECK(code.find("maximum = value;") != std::string::npos);
    CHECK(code.find("cursor = (cursor + 1);") != std::string::npos);
    CHECK(code.find("sum += boss->rank;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: unsupported generic extern scaffold is rejected", "[codegen-entt][extern-rule]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var x: float\n"
        "trait Velocity:\n"
        "    var dx: float\n"
        "event run\n"
        "extern rule ParticleSystem:\n"
        "    filter:\n"
        "        Position\n"
        "        Velocity\n"
        "    on run:\n"
        "        writes:\n"
        "            Position\n"
        "            Velocity\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<ExternRuleNode>(&decl)) {
            CHECK_THROWS_AS(EnttSystemEmitter::emit_extern_system(*sys, decorated), std::runtime_error);
        }
    }
}

TEST_CASE("Codegen EnTT: unsupported ordered extern scaffold is rejected", "[codegen-entt][extern-rule]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var y: float\n"
        "event run\n"
        "extern rule SortedRenderer:\n"
        "    filter:\n"
        "        Position\n"
        "    order by:\n"
        "        Position.y desc\n"
        "    on run:\n"
        "        reads:\n"
        "            Position\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<ExternRuleNode>(&decl)) {
            CHECK_THROWS_AS(EnttSystemEmitter::emit_extern_system(*sys, decorated), std::runtime_error);
        }
    }
}

TEST_CASE("Codegen EnTT: rule order by emits registry sort for single key", "[codegen-entt][rule-order-by]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Sprite:\n"
        "    var layer: int\n"
        "rule Render:\n"
        "    filter:\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer asc\n"
        "    on tick:\n"
        "        let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.sort<Sprite>([&](entt::entity a, entt::entity b)") != std::string::npos);
            CHECK(code.find("registry.get<Sprite>(a).layer < registry.get<Sprite>(b).layer") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: rule order by emits multi-key comparator", "[codegen-entt][rule-order-by]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Position:\n"
        "    var pos: vec2\n"
        "trait Sprite:\n"
        "    var layer: int\n"
        "rule Render:\n"
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
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (registry.get<Sprite>(a).layer != registry.get<Sprite>(b).layer)") !=
                  std::string::npos);
            CHECK(code.find("return registry.get<Position>(a).pos.y > registry.get<Position>(b).pos.y;") !=
                  std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: rule without order by emits no sort call", "[codegen-entt][rule-order-by]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Sprite:\n"
        "    var layer: int\n"
        "rule Render:\n"
        "    filter:\n"
        "        Sprite\n"
        "    on tick:\n"
        "        let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.sort<") == std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: generic extern rule name does not infer lifecycle dispatch", "[codegen-entt][extern-rule]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var x: float\n"
        "event run\n"
        "extern rule SpriteRenderer:\n"
        "    filter:\n"
        "        Position\n"
        "    on run:\n"
        "        reads:\n"
        "            Position\n",
        program);

    CHECK_THROWS_AS(CppEnttCodegen::generate(decorated), std::runtime_error);
}

TEST_CASE("Codegen EnTT: stdlib-style spelling cannot impersonate compiler-owned extern",
          "[codegen-entt][extern-rule]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Renderer:\n"
        "    var layer: int\n"
        "event run\n"
        "extern rule SpriteRenderer:\n"
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
        "rule Hierarchy:\n"
        "    on tick:\n"
        "        add Parent:\n"
        "            parent = self\n"
        "        destroy self\n",
        program);

    auto code             = CppEnttCodegen::generate(decorated);
    const auto value_name = extract_temp_name(code, "cactus_gen_value_");
    REQUIRE_FALSE(value_name.empty());
    CHECK(code.find(value_name + ".parent = entity") != std::string::npos);
    CHECK(code.find("cactus_destroy_entity_recursive(registry, entity)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: flat transform propagation extern rule is recognized", "[codegen-entt][hierarchy]") {
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
        "extern rule TransformPropagation:\n"
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
        "extern rule TransformPropagation:\n"
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

TEST_CASE("Codegen EnTT: volume transform propagation extern rule is recognized", "[codegen-entt][hierarchy]") {
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
        "extern rule TransformPropagation:\n"
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
    CHECK(code.find("cactus::runtime::stdlib::math::quat::compose(parent_world.rotation, local.rotation)") !=
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
        "rule Detect:\n"
        "    filter:\n"
        "        Detector\n"
        "    on tick:\n"
        "        for hit in hits:\n"
        "            emit Damage to hit.victim:\n"
        "                amount = 1\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("auto foreach_snapshot_") != std::string::npos);
    CHECK(code.find("= Detector_comp.hits;") != std::string::npos);
    CHECK(code.find("for (const auto& hit : foreach_snapshot_") != std::string::npos);
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
        "rule Producer:\n"
        "    filter:\n"
        "        Health\n"
        "    on tick:\n"
        "        project DamageFlash:\n"
        "            intensity = 1.0\n"
        "rule Consumer:\n"
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
    const auto projected_name = extract_temp_name(code, "cactus_gen_projected_");
    REQUIRE_FALSE(projected_name.empty());
    CHECK(code.find("auto& " + projected_name + " = project_DamageFlash(registry, entity)") != std::string::npos);
    CHECK(code.find(projected_name + ".intensity = 1.0F") != std::string::npos);
    CHECK(code.find("registry.view<Health, DamageFlash>(entt::exclude<Suppressed>)") != std::string::npos);
    CHECK(code.find("auto& flash = DamageFlash_comp") != std::string::npos);
    CHECK(code.find("cactus_projected_DamageFlash") == std::string::npos);
    CHECK(code.find("cactus_try_get_projected_or_durable_DamageFlash") == std::string::npos);
    CHECK(code.find("cactus_has_projected_or_durable_Suppressed") == std::string::npos);
    CHECK(code.find("clear_projected_traits(registry);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: filtered rules use native views and no early-return guards", "[codegen-entt][project]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "trait Wanted\n"
        "trait Blocked\n"
        "rule Consumer:\n"
        "    filter:\n"
        "        Wanted\n"
        "    exclude:\n"
        "        Blocked\n"
        "    on tick:\n"
        "        let seen = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
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
        "rule Producer:\n"
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
        "rule Producer:\n"
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

TEST_CASE("Codegen EnTT: aliased std.text.format lowers to std::format in rule handler",
          "[codegen-entt][std-text-format]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.text as text\n"
        "event tick:\n"
        "    dt: float\n"
        "trait Score:\n"
        "    var value: int\n"
        "rule Display:\n"
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
        "rule Display:\n"
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

TEST_CASE("Codegen EnTT: unaliased std.text format lowers to std::format in rule handler",
          "[codegen-entt][std-text-format]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.text\n"
        "event tick:\n"
        "    dt: float\n"
        "trait Score:\n"
        "    var value: int\n"
        "rule Display:\n"
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

TEST_CASE("Codegen EnTT: editor.cactus module generates EditorState component, Editor entity, extern rule stubs",
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
        "    var mode: GizmoMode = GizmoMode.Select\n"
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
        // EditorPropertyPanel, not EditorTemplatePalette: GizmoRenderer2D/3D and
        // EditorTemplatePalette stopped being compiler-owned extern rules
        // (editor-declarative-rendering) — EditorPropertyPanel is the sole
        // remaining one, still exercising the same generic
        // extern-rule-stub-emission mechanism this test checks.
        "pub extern rule EditorPropertyPanel:\n"
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
    // EditorPropertyPanel extern rule stub (snake_case name)
    CHECK(code.find("editor_property_panel_tick") != std::string::npos);
    // HexColor for #00FF00FF is not in this test, but HexColor conversion path should exist
}

// EditorActiveModeImpl (runtime.hpp) is a fixed std::function<int(entt::registry&)> —
// program-independent, so it can't name a per-program generated enum type (same reasoning as
// the active_mode()/mode_label() call-site bridging below). The registered impl lambda's body
// must cast EditorState.mode (GizmoMode-typed) back to int to match that fixed signature.
TEST_CASE("Codegen EnTT: editor active_mode() reads the singleton EditorState.mode via a registered impl",
          "[codegen-entt][stdlib][editor][enum]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "use std.editor\n"
        "pub event tick\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "pub entity Editor:\n"
        "    EditorState\n"
        "pub extern func active_mode() GizmoMode\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    // Impl registered from generated_init_project, reading EditorState.mode off the
    // singleton Editor entity — mirrors the hit_test/spawn/raycast impl-registration idiom.
    const auto init = generated_function(code, "void generated_init_project");
    CHECK(init.find("register_editor_active_mode_impl(") != std::string::npos);
    CHECK(init.find("reg.view<EditorState>()") != std::string::npos);
    CHECK(init.find("-> int {") != std::string::npos);
    CHECK(init.find("static_cast<int>(view.get<EditorState>(entity).mode)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: editor template_names()/template_index() expose a declaration-ordered companion list",
          "[codegen-entt][stdlib][editor][editor-template-registry]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "use std.editor\n"
        "pub event tick\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "pub entity Editor:\n"
        "    EditorState\n"
        "trait Position:\n"
        "    var value: vec2 = vec2(0.0, 0.0)\n"
        "pub template Box:\n"
        "    Position\n"
        "pub template PlayerSpawn:\n"
        "    Position\n"
        "pub extern func template_names() list[string]\n"
        "pub extern func template_index(name: string) int\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    // Order-stable companion to cactus_template_registry (an unordered_map), populated in
    // the same declaration-order loop as the registry itself.
    const auto order_start = code.find("cactus_template_registry_order = {");
    REQUIRE(order_start != std::string::npos);
    const auto box_pos   = code.find("\"Box\"", order_start);
    const auto spawn_pos = code.find("\"PlayerSpawn\"", order_start);
    REQUIRE(box_pos != std::string::npos);
    REQUIRE(spawn_pos != std::string::npos);
    CHECK(box_pos < spawn_pos);
    CHECK(code.find("std::vector<std::string> editor_template_names() { return "
                    "cactus_template_registry_order; }") != std::string::npos);
    CHECK(code.find("int editor_template_index(const std::string& name) noexcept {") != std::string::npos);
    CHECK(code.find("return -1;") != std::string::npos);
}

TEST_CASE(
    "Codegen EnTT: clean-named editor extern funcs active_mode/template_names/screen_size lower without "
    "registry injection except active_mode",
    "[codegen-entt][editor][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "pub extern func active_mode() GizmoMode\n"
        "pub extern func template_names() list[string]\n"
        "pub extern func template_index(name: string) int\n"
        "pub extern func screen_size() vec2\n"
        "trait Probe:\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "    var idx: int = 0\n"
        "    var sz: vec2 = vec2(0.0, 0.0)\n"
        "rule ProbeTest:\n"
        "    filter:\n"
        "        Probe\n"
        "    on tick:\n"
        "        mode = active_mode()\n"
        "        idx = template_index(\"Box\")\n"
        "        sz = screen_size()\n",
        program);

    for (const auto* name : {"active_mode", "template_names", "template_index", "screen_size"}) {
        decorated.funcs[name].is_stdlib   = true;
        decorated.funcs[name].module_name = "std.editor";
    }

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void probe_test_tick");
    CHECK(rule.find("cactus::runtime::entt_backend::editor_active_mode(registry)") != std::string::npos);
    CHECK(rule.find("cactus::runtime::entt_backend::editor_template_index(\"Box\")") != std::string::npos);
    CHECK(rule.find("cactus::runtime::entt_backend::editor_screen_size()") != std::string::npos);
    // Only active_mode reads ECS state (the EditorState singleton); the others read
    // codegen-emitted globals or raw window state and take no registry argument.
    CHECK(rule.find("editor_template_index(registry,") == std::string::npos);
    CHECK(rule.find("editor_screen_size(registry)") == std::string::npos);
}

// runtime.hpp's editor_active_mode/editor_mode_label stay int-based (program-independent,
// precompiled — see editor-gizmo-mode-enum's design.md decision 1), so a GizmoMode-typed
// active_mode()/mode_label() call must bridge with a static_cast at the codegen call site
// rather than changing the runtime functions themselves.
TEST_CASE("Codegen EnTT: active_mode() lowers to a static_cast from the runtime's int accessor to GizmoMode",
          "[codegen-entt][editor][stdlib][enum]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "pub extern func active_mode() GizmoMode\n"
        "trait Probe:\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "rule ProbeTest:\n"
        "    filter:\n"
        "        Probe\n"
        "    on tick:\n"
        "        mode = active_mode()\n",
        program);

    for (const auto* name : {"active_mode"}) {
        decorated.funcs[name].is_stdlib   = true;
        decorated.funcs[name].module_name = "std.editor";
    }

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void probe_test_tick");
    CHECK(rule.find("static_cast<GizmoMode>(cactus::runtime::entt_backend::editor_active_mode(registry))") !=
          std::string::npos);
}

TEST_CASE("Codegen EnTT: mode_label(mode) lowers to a static_cast from GizmoMode to the runtime's int parameter",
          "[codegen-entt][editor][stdlib][enum]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "pub extern func mode_label(mode: GizmoMode) string\n"
        "trait Probe:\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "    var label: string = \"\"\n"
        "rule ProbeTest:\n"
        "    filter:\n"
        "        Probe\n"
        "    on tick:\n"
        "        label = mode_label(mode)\n",
        program);

    for (const auto* name : {"mode_label"}) {
        decorated.funcs[name].is_stdlib   = true;
        decorated.funcs[name].module_name = "std.editor";
    }

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void probe_test_tick");
    CHECK(rule.find("cactus::runtime::entt_backend::editor_mode_label(static_cast<int>(") != std::string::npos);
}

TEST_CASE("Codegen EnTT: EditorGizmoRenderer2D gates on is_editor_active and emits mode-specific debug-draw events",
          "[codegen-entt][stdlib][editor][debug-draw]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "use std.editor\n"
        "use std.debug as debug\n"
        "pub event render\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "    Rotate\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "pub entity Editor:\n"
        "    EditorState\n"
        "pub trait EditorSelected\n"
        "pub trait EditorGizmo2D:\n"
        "    var mode: GizmoMode = GizmoMode.Translate\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "    var rotation: float\n"
        "    var scale: vec2\n"
        "pub event DrawDebugLine2D:\n"
        "    start: vec2\n"
        "    end: vec2\n"
        "    color: color\n"
        "    thickness: float\n"
        "pub event DrawDebugTriangle2D:\n"
        "    a: vec2\n"
        "    b: vec2\n"
        "    c: vec2\n"
        "    color: color\n"
        "pub event DrawDebugRingOutline2D:\n"
        "    center: vec2\n"
        "    inner_radius: float\n"
        "    outer_radius: float\n"
        "    color: color\n"
        "pub event DrawDebugRectOutline2D:\n"
        "    position: vec2\n"
        "    size: vec2\n"
        "    thickness: float\n"
        "    color: color\n"
        "pub extern func active_mode() GizmoMode\n"
        "pub extern func is_editor_active() bool\n"
        "rule EditorGizmoRenderer2D:\n"
        "    filter:\n"
        "        WorldTransform as xform\n"
        "        EditorSelected\n"
        "    on render:\n"
        "        if not is_editor_active():\n"
        "            return\n"
        "        let mode = active_mode()\n"
        "        project EditorGizmo2D:\n"
        "            mode = mode\n"
        "            color = #00FF00FF\n"
        "            size = 1.0\n"
        "        let center = xform.position\n"
        "        emit DrawDebugRectOutline2D:\n"
        "            position = vec2(center.x - 0.5, center.y - 0.5)\n"
        "            size = vec2(1.0, 1.0)\n"
        "            thickness = 0.05\n"
        "            color = #00FF00FF\n"
        "        if mode == GizmoMode.Translate:\n"
        "            emit DrawDebugLine2D:\n"
        "                start = center\n"
        "                end = vec2(center.x + 1.0, center.y)\n"
        "                color = #E62937FF\n"
        "                thickness = 0.05\n"
        "            emit DrawDebugTriangle2D:\n"
        "                a = center\n"
        "                b = center\n"
        "                c = center\n"
        "                color = #E62937FF\n"
        "        if mode == GizmoMode.Rotate:\n"
        "            emit DrawDebugRingOutline2D:\n"
        "                center = center\n"
        "                inner_radius = 0.8\n"
        "                outer_radius = 1.0\n"
        "                color = #66BFFFFF\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void editor_gizmo_renderer2_d_render");
    // Early return when inactive: EditorSelected persists across an editor toggle-off, so the
    // rule can't rely on the filter alone to gate drawing.
    CHECK(tick.find("editor_is_active(registry)") != std::string::npos);
    // Mode drives both the projection and the mode-specific branches, not a hardcoded literal.
    CHECK(tick.find("editor_active_mode(registry)") != std::string::npos);
    CHECK(tick.find("project_EditorGizmo2D(registry, entity)") != std::string::npos);
    CHECK(tick.find(".mode = mode;") != std::string::npos);
    // Always-drawn AABB outline, independent of mode.
    CHECK(tick.find("DrawDebugRectOutline2DEvent_buffer.push_back(") != std::string::npos);
    // Translate handles only inside the mode == GizmoMode::Translate branch.
    const auto mode1 = tick.find("if (mode == GizmoMode::Translate) {");
    REQUIRE(mode1 != std::string::npos);
    const auto mode2 = tick.find("if (mode == GizmoMode::Rotate) {");
    REQUIRE(mode2 != std::string::npos);
    CHECK(tick.find("DrawDebugLine2DEvent_buffer.push_back(", mode1) < mode2);
    CHECK(tick.find("DrawDebugRingOutline2DEvent_buffer.push_back(", mode2) != std::string::npos);
}

TEST_CASE(
    "Codegen EnTT: EditorTemplatePalette iterates templates by index, tints via palette_color, and "
    "hit-tests clicks",
    "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "use std.editor\n"
        "pub event render\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Place\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "    var active_template: string = \"\"\n"
        "pub entity Editor:\n"
        "    EditorState\n"
        "pub trait ScreenLabel:\n"
        "    var text: string = \"\"\n"
        "    var position: vec2\n"
        "    var font_size: int = 32\n"
        "    var color: color = #FFFFFFFF\n"
        "    var visible: bool = true\n"
        "pub event DrawScreenRect:\n"
        "    position: vec2\n"
        "    size: vec2\n"
        "    color: color\n"
        "    filled: bool\n"
        "    thickness: float\n"
        "pub extern func template_names() list[string]\n"
        "pub extern func template_index(name: string) int\n"
        "pub extern func palette_label_slot(index: int) entity_id\n"
        "pub extern func palette_color(index: int) color\n"
        "rule EditorTemplatePalette:\n"
        "    filter:\n"
        "        EditorState\n"
        "    on render:\n"
        "        let names = template_names()\n"
        "        if not active:\n"
        "            for name in names:\n"
        "                let idx = template_index(name)\n"
        "                let slot = palette_label_slot(idx)\n"
        "                project ScreenLabel to slot:\n"
        "                    text = \"\"\n"
        "                    position = vec2(0.0, 0.0)\n"
        "                    font_size = 14\n"
        "                    color = #FFFFFFFF\n"
        "                    visible = false\n"
        "            return\n"
        "        for name in names:\n"
        "            let idx = template_index(name)\n"
        "            let x = 10.0\n"
        "            let y = 40.0 + (idx * 30.0)\n"
        "            let tint = palette_color(idx)\n"
        "            emit DrawScreenRect:\n"
        "                position = vec2(x, y)\n"
        "                size = vec2(140.0, 26.0)\n"
        "                color = tint\n"
        "                filled = true\n"
        "                thickness = 0.0\n"
        "            let slot = palette_label_slot(idx)\n"
        "            project ScreenLabel to slot:\n"
        "                text = name\n"
        "                position = vec2(x + 6.0, y + 6.0)\n"
        "                font_size = 14\n"
        "                color = #FFFFFFFF\n"
        "                visible = true\n"
        "            if x >= 0.0:\n"
        "                if y >= 0.0:\n"
        "                    active_template = name\n"
        "                    mode = GizmoMode.Place\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void editor_template_palette_render");
    // Inactive branch: labels hidden via ScreenLabel.visible = false, no DrawScreenRect emitted.
    CHECK(tick.find("visible = false;") != std::string::npos);
    // Active branch: index comes from template_index(name), not a loop-local counter — bounded
    // `for` can't mutate an outer-scope var across iterations.
    CHECK(tick.find("editor_template_index(name)") != std::string::npos);
    // Color comes from the palette_color() extern func, not a DSL if-chain — `if`/`for` bodies
    // can't mutate an outer-scope local either (same root cause), so a computed-then-conditionally
    // -overridden local would silently keep its first value.
    CHECK(tick.find("editor_palette_color(idx)") != std::string::npos);
    CHECK(tick.find("DrawScreenRectEvent_buffer.push_back(") != std::string::npos);
    // Click hit-testing and the EditorState writes happen directly in this rule (self bound to
    // EditorState), writing the real component fields, not a generic renderer.
    CHECK(tick.find(".active_template = name;") != std::string::npos);
    CHECK(tick.find(".mode = GizmoMode::Place;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: EditorHUDOverlay draws a screen border and mode-text label only while active",
          "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "use std.editor\n"
        "pub event render\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "pub trait ScreenLabel:\n"
        "    var text: string = \"\"\n"
        "    var position: vec2\n"
        "    var font_size: int = 32\n"
        "    var color: color = #FFFFFFFF\n"
        "    var visible: bool = true\n"
        "pub entity Editor:\n"
        "    EditorState\n"
        "    ScreenLabel:\n"
        "        text = \"\"\n"
        "        position = vec2(10.0, 10.0)\n"
        "        font_size = 14\n"
        "        color = #FFFF00FF\n"
        "        visible = false\n"
        "pub event DrawScreenRect:\n"
        "    position: vec2\n"
        "    size: vec2\n"
        "    color: color\n"
        "    filled: bool\n"
        "    thickness: float\n"
        "pub extern func screen_size() vec2\n"
        "pub extern func mode_label(mode: GizmoMode) string\n"
        "rule EditorHUDOverlay:\n"
        "    filter:\n"
        "        EditorState\n"
        "        ScreenLabel\n"
        "    on render:\n"
        "        if not active:\n"
        "            visible = false\n"
        "            return\n"
        "        emit DrawScreenRect:\n"
        "            position = vec2(0.0, 0.0)\n"
        "            size = screen_size()\n"
        "            color = #FFFF00FF\n"
        "            filled = false\n"
        "            thickness = 3.0\n"
        "        let label = mode_label(mode)\n"
        "        text = label\n"
        "        position = vec2(10.0, 10.0)\n"
        "        color = #FFFF00FF\n"
        "        visible = true\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    // snake_case inserts an underscore before every uppercase letter, including consecutive
    // ones in an acronym: "HUD" -> "_h_u_d", not "_hud".
    const auto render = generated_function(code, "void editor_h_u_d_overlay_render");
    // Inactive: hide the label and return, before the border/text-update code runs at all.
    const auto inactive_hide = render.find("visible = false;");
    const auto early_return  = render.find("return;");
    REQUIRE(inactive_hide != std::string::npos);
    REQUIRE(early_return != std::string::npos);
    CHECK(inactive_hide < early_return);
    // Border/label update only appear after the early return (i.e. gated on active).
    const auto border = render.find("DrawScreenRectEvent_buffer.push_back(");
    REQUIRE(border != std::string::npos);
    CHECK(border > early_return);
    // Active: border spans screen_size(), mode text built via mode_label(), then shown.
    CHECK(render.find("editor_screen_size()") != std::string::npos);
    CHECK(render.find("editor_mode_label(") != std::string::npos);
    CHECK(render.find("visible = true;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: editor projected traits generate registry helpers", "[codegen-entt][stdlib][editor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "pub event tick:\n"
        "    dt: float\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "pub trait EditorGizmo2D:\n"
        "    var mode: GizmoMode = GizmoMode.Translate\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "pub trait EditorGizmo3D:\n"
        "    var mode: GizmoMode = GizmoMode.Translate\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "rule Gizmo2D:\n"
        "    filter:\n"
        "        EditorGizmo2D\n"
        "    on tick:\n"
        "        project EditorGizmo2D:\n"
        "            mode = GizmoMode.Translate\n"
        "            color = #00FF00FF\n"
        "            size = 1.0\n"
        "        project EditorGizmo3D:\n"
        "            mode = GizmoMode.Translate\n"
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

TEST_CASE("Codegen EnTT: editor extern rules generate correct dispatch calls",
          "[codegen-entt][stdlib][editor][extern-rule]") {
    // GizmoRenderer2D/3D and EditorTemplatePalette stopped being compiler-owned extern rules
    // (editor-declarative-rendering) — EditorPropertyPanel is the sole remaining one now; its
    // own coverage is task 6/7's dedicated EditorTemplatePalette/EditorHUDOverlay tests plus
    // the real editor-test.cactus/editor-3d example compiles.
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "pub event tick:\n"
        "    dt: float\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "pub extern rule EditorPropertyPanel:\n"
        "    filter:\n"
        "        EditorState\n"
        "    on tick:\n"
        "        reads:\n"
        "            EditorState\n"
        "        effects:\n"
        "            editor\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    // EditorPropertyPanel should have a tick call (snake_case name)
    CHECK(code.find("editor_property_panel_tick") != std::string::npos);
}

// Replaces the old "GizmoRenderer3D emits grid wire boxes..." test, which asserted the hardcoded
// C++ geometry a compiler-owned GizmoRenderer3D extern rule used to emit — GizmoRenderer3D
// stopped being an extern rule entirely (editor-declarative-rendering); the geometry decision
// moved into the plain rule EditorGizmoRenderer3D, mirroring the EditorGizmoRenderer2D test above.
TEST_CASE("Codegen EnTT: EditorGizmoRenderer3D gates on is_editor_active and emits mode-specific debug-draw events",
          "[codegen-entt][stdlib][editor][debug-draw]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.editor\n"
        "use std.editor\n"
        "use std.debug as debug\n"
        "pub event render\n"
        "pub enum GizmoMode:\n"
        "    Select\n"
        "    Translate\n"
        "    Rotate\n"
        "    Scale\n"
        "pub trait EditorState:\n"
        "    var active: bool = true\n"
        "    var mode: GizmoMode = GizmoMode.Select\n"
        "pub entity Editor:\n"
        "    EditorState\n"
        "pub trait EditorSelected\n"
        "pub trait EditorGizmo3D:\n"
        "    var mode: GizmoMode = GizmoMode.Translate\n"
        "    var color: color = #00FF00FF\n"
        "    var size: float = 1.0\n"
        "trait WorldTransform:\n"
        "    var position: vec3\n"
        "    var rotation: quat\n"
        "    var scale: vec3\n"
        "pub event DrawDebugLine3D:\n"
        "    start: vec3\n"
        "    end: vec3\n"
        "    color: color\n"
        "pub event DrawDebugWireBox3D:\n"
        "    center: vec3\n"
        "    size: vec3\n"
        "    color: color\n"
        "pub event DrawDebugCircle3D:\n"
        "    center: vec3\n"
        "    radius: float\n"
        "    normal: vec3\n"
        "    color: color\n"
        "pub extern func active_mode() GizmoMode\n"
        "pub extern func is_editor_active() bool\n"
        "rule EditorGizmoRenderer3D:\n"
        "    filter:\n"
        "        WorldTransform as xform\n"
        "        EditorSelected\n"
        "    on render:\n"
        "        if not is_editor_active():\n"
        "            return\n"
        "        let mode = active_mode()\n"
        "        project EditorGizmo3D:\n"
        "            mode = mode\n"
        "            color = #00FF00FF\n"
        "            size = 1.0\n"
        "        let origin = xform.position\n"
        "        emit DrawDebugWireBox3D:\n"
        "            center = origin\n"
        "            size = vec3(1.0, 1.0, 1.0)\n"
        "            color = #00FF00FF\n"
        "        if mode == GizmoMode.Translate or mode == GizmoMode.Scale:\n"
        "            emit DrawDebugLine3D:\n"
        "                start = origin\n"
        "                end = vec3(origin.x + 1.0, origin.y, origin.z)\n"
        "                color = #E62937FF\n"
        "        if mode == GizmoMode.Rotate:\n"
        "            emit DrawDebugCircle3D:\n"
        "                center = origin\n"
        "                radius = 1.0\n"
        "                normal = vec3(1.0, 0.0, 0.0)\n"
        "                color = #66BFFFFF\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void editor_gizmo_renderer3_d_render");
    // Early return when inactive: EditorSelected persists across an editor toggle-off, so the
    // rule can't rely on the filter alone to gate drawing.
    CHECK(tick.find("editor_is_active(registry)") != std::string::npos);
    // Mode drives both the projection and the mode-specific branches, not a hardcoded literal.
    CHECK(tick.find("editor_active_mode(registry)") != std::string::npos);
    CHECK(tick.find("project_EditorGizmo3D(registry, entity)") != std::string::npos);
    CHECK(tick.find(".mode = mode;") != std::string::npos);
    // Always-drawn wire box, independent of mode.
    CHECK(tick.find("DrawDebugWireBox3DEvent_buffer.push_back(") != std::string::npos);
    // Translate/scale axis line only inside the (mode == Translate || mode == Scale) branch;
    // circle only inside mode == Rotate.
    const auto translate_branch = tick.find("mode == GizmoMode::Translate");
    REQUIRE(translate_branch != std::string::npos);
    const auto rotate_branch = tick.find("if (mode == GizmoMode::Rotate) {");
    REQUIRE(rotate_branch != std::string::npos);
    CHECK(tick.find("DrawDebugLine3DEvent_buffer.push_back(", translate_branch) < rotate_branch);
    CHECK(tick.find("DrawDebugCircle3DEvent_buffer.push_back(", rotate_branch) != std::string::npos);

    // The rule is not implicitly attached to either legacy hook — linked phase metadata is
    // required to schedule it, same as any other rule.
    const auto render_project = generated_function(code, "void generated_render_project");
    CHECK(render_project.find("editor_gizmo_renderer3_d_render(registry)") == std::string::npos);
    const auto update_project = generated_function(code, "void generated_update_project");
    CHECK(update_project.find("editor_gizmo_renderer3_d_render") == std::string::npos);
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
    CHECK(code.find("if (__vp.clear) { cactus::runtime::raylib::ClearBackground") != std::string::npos);

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
    CHECK(code.find("if (__vp.clear) { cactus::runtime::raylib::ClearBackground(__vp.clear_color); }") !=
          std::string::npos);
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
                                       "rule S:\n"
                                       "    on tick:\n"
                                       "        if query.exists[Boss]():\n"
                                       "            let x = 1\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Boss>()") != std::string::npos);
    CHECK(code.find("view.begin() != view.end()") != std::string::npos);
}

TEST_CASE("Codegen EnTT: query.exists with negation lowers to excluded view", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "rule S:\n"
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
                                       "rule S:\n"
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
                                       "rule S:\n"
                                       "    on tick:\n"
                                       "        let t = query.first[Boss]()\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Boss>()") != std::string::npos);
    CHECK(code.find("entt::entity{entt::null}") != std::string::npos);
    // empty result returns null sentinel (total entity_id semantics)
    CHECK(code.find("it != view.end()") != std::string::npos);
}

TEST_CASE("Codegen EnTT: query.all lowers to vector collection loop", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "rule S:\n"
                                       "    on tick:\n"
                                       "        let all = query.all[Enemy]()\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<Enemy>()") != std::string::npos);
    CHECK(code.find("std::vector<entt::entity> result") != std::string::npos);
    CHECK(code.find("result.push_back(e)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: query.children lowers to a stable filtered direct-child snapshot",
          "[codegen-entt][query][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.query as query\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Selection:\n"
        "    var parent: entity_id\n"
        "trait Node\n"
        "trait Hidden\n"
        "rule CollectChildren:\n"
        "    filter:\n"
        "        Selection\n"
        "    on tick:\n"
        "        let snapshot = query.children[Node, not Hidden](of = parent)\n",
        program);

    const auto code             = CppEnttCodegen::generate(decorated);
    const auto requested_parent = extract_temp_name(code, "cactus_gen_requested_parent_");
    REQUIRE_FALSE(requested_parent.empty());
    CHECK(code.find("const auto " + requested_parent + " = (Selection_comp.parent)") != std::string::npos);
    // The handler body is emitted once for targeted delivery and once for broadcast delivery;
    // each runtime path still evaluates the authored expression exactly once into its temporary.
    CHECK(count_occurrences(code, "const auto " + requested_parent + " = (Selection_comp.parent)") == 2);
    CHECK(code.find("if (!registry.valid(" + requested_parent + ")) return std::vector<entt::entity>{}") !=
          std::string::npos);
    CHECK(code.find("registry.view<Parent, Node>(entt::exclude<Hidden>)") != std::string::npos);
    CHECK(code.find("registry.get<Parent>(e).parent == " + requested_parent) != std::string::npos);
    CHECK(code.find("registry.get<cactus::runtime::entt_backend::CreationOrdinal>(e).value") != std::string::npos);
    CHECK(code.find("std::ranges::sort(ordered)") != std::string::npos);
    CHECK(code.find("std::vector<entt::entity> result; result.reserve(ordered.size())") != std::string::npos);
    CHECK(code.find("result.push_back(e)") != std::string::npos);
}

TEST_CASE("Codegen EnTT: hierarchy traversals lower to stable filtered-forest snapshots with cycle containment",
          "[codegen-entt][query][hierarchy]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.query as query\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Node\n"
        "trait Hidden\n"
        "rule Traverse:\n"
        "    on tick:\n"
        "        let preorder = query.hierarchy_preorder[Node, not Hidden]()\n"
        "        let postorder = query.hierarchy_postorder[Node, not Hidden]()\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(count_occurrences(code, "registry.view<Node>(entt::exclude<Hidden>)") >= 2);
    CHECK(code.find("std::ranges::sort(ordered)") != std::string::npos);
    CHECK(code.find("std::unordered_set<entt::entity> matching") != std::string::npos);
    CHECK(code.find("registry.try_get<Parent>(e)") != std::string::npos);
    CHECK(code.find("registry.valid(relation->parent) && matching.contains(relation->parent)") != std::string::npos);
    CHECK(code.find("else roots.push_back(e)") != std::string::npos);
    CHECK(code.find("children[relation->parent].push_back(e)") != std::string::npos);
    CHECK(code.find("if (!visited.insert(e).second) return") != std::string::npos);
    CHECK(code.find("for (const auto root : roots) visit(visit, root)") != std::string::npos);
    CHECK(code.find("for (const auto& [ordinal, e] : ordered) { (void)ordinal; visit(visit, e); }") !=
          std::string::npos);

    const auto preorder_start = code.find("auto preorder = ");
    REQUIRE(preorder_start != std::string::npos);
    const auto preorder_end = code.find("auto postorder = ", preorder_start);
    REQUIRE(preorder_end != std::string::npos);
    const auto preorder          = code.substr(preorder_start, preorder_end - preorder_start);
    const auto preorder_push     = preorder.find("result.push_back(e)");
    const auto preorder_children = preorder.find("children.find(e)");
    REQUIRE(preorder_push != std::string::npos);
    REQUIRE(preorder_children != std::string::npos);
    CHECK(preorder_push < preorder_children);

    const auto postorder_end = code.find("return result; }();", preorder_end);
    REQUIRE(postorder_end != std::string::npos);
    const auto postorder          = code.substr(preorder_end, postorder_end - preorder_end);
    const auto postorder_children = postorder.find("children.find(e)");
    const auto postorder_push     = postorder.find("result.push_back(e)");
    REQUIRE(postorder_children != std::string::npos);
    REQUIRE(postorder_push != std::string::npos);
    CHECK(postorder_children < postorder_push);
}

TEST_CASE("Codegen EnTT: query.parent lowers to Parent component try_get", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.query as query\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Child:\n"
        "    var child_id: entity_id\n"
        "rule S:\n"
        "    filter:\n"
        "        Child\n"
        "    on tick:\n"
        "        let p = query.parent(of = child_id)\n",
        program);

    const auto code                  = CppEnttCodegen::generate(decorated);
    const auto parent_component_name = extract_temp_name(code, "cactus_gen_parent_component_");
    REQUIRE_FALSE(parent_component_name.empty());
    CHECK(code.find("registry.try_get<Parent>") != std::string::npos);
    CHECK(code.find(parent_component_name + "->parent") != std::string::npos);
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
        "rule S:\n"
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
                                       "rule S:\n"
                                       "    on tick:\n"
                                       "        let p = query.nearest[Enemy](from = tick.dt)\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Enemy>()") != std::string::npos);
    // call site delegates the search to the shared runtime helper instead of an inline loop
    CHECK(code.find("cactus::runtime::entt_backend::query_nearest(") != std::string::npos);
    CHECK(code.find("registry.get<WorldTransform>(e).position") != std::string::npos);
    // no reserved double-leading-underscore locals at the call site
    CHECK(code.find("__best") == std::string::npos);
    CHECK(code.find("__from") == std::string::npos);
}

TEST_CASE("Codegen EnTT: flat query.overlap_box excludes negative filter matches", "[codegen-entt][query][spatial]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        std::string(kFlatQueryPreamble) +
            "rule S:\n"
            "    on tick:\n"
            "        let p = query.overlap_box[Pickup, not Collected](center = tick.dt, size = tick.dt)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Pickup>(entt::exclude<Collected>)") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::query_overlap_box(") != std::string::npos);
    CHECK(code.find("registry.get<WorldTransform>(e).position") != std::string::npos);
    CHECK(code.find("__ct") == std::string::npos);
    CHECK(code.find("__sz") == std::string::npos);
}

TEST_CASE("Codegen EnTT: flat query.overlap_circle lowers to radius-based search", "[codegen-entt][query][spatial]") {
    ProgramNode program;
    auto decorated =
        full_pipeline(std::string(kFlatQueryPreamble) +
                          "rule S:\n"
                          "    on tick:\n"
                          "        let hits = query.overlap_circle[Enemy](center = tick.dt, radius = tick.dt)\n",
                      program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Enemy>()") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::query_overlap_circle(") != std::string::npos);
    CHECK(code.find("registry.get<WorldTransform>(e).position") != std::string::npos);
    CHECK(code.find("__rad") == std::string::npos);
}

TEST_CASE("Codegen EnTT: flat query.raycast lowers to directional hit search", "[codegen-entt][query][spatial]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        std::string(kFlatQueryPreamble) +
            "rule S:\n"
            "    on tick:\n"
            "        let hit = query.raycast[Wall](origin = tick.dt, dir = tick.dt, max_dist = tick.dt)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Wall>()") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::query_raycast(") != std::string::npos);
    CHECK(code.find("registry.get<WorldTransform>(e).position") != std::string::npos);
    CHECK(code.find("__proj") == std::string::npos);
    CHECK(code.find("__best") == std::string::npos);
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
                                       "rule S:\n"
                                       "    on tick:\n"
                                       "        let e = query.nearest[Enemy](from = tick.dt)\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Enemy>()") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::query_nearest(") != std::string::npos);
    CHECK(code.find("registry.get<WorldTransform>(e).position") != std::string::npos);
    CHECK(code.find("__dz") == std::string::npos);
    CHECK(code.find("__best") == std::string::npos);
}

TEST_CASE("Codegen EnTT: volume query.overlap_sphere lowers to 3D radius search",
          "[codegen-entt][query][spatial][3d]") {
    ProgramNode program;
    auto decorated =
        full_pipeline(std::string(kVolumeQueryPreamble) +
                          "rule S:\n"
                          "    on tick:\n"
                          "        let hits = query.overlap_sphere[Enemy](center = tick.dt, radius = tick.dt)\n",
                      program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("registry.view<WorldTransform, Enemy>()") != std::string::npos);
    CHECK(code.find("cactus::runtime::entt_backend::query_overlap_sphere(") != std::string::npos);
    CHECK(code.find("registry.get<WorldTransform>(e).position") != std::string::npos);
    CHECK(code.find("__dz") == std::string::npos);
    CHECK(code.find("__ct") == std::string::npos);
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
    CHECK(wrapper.find("registry.emplace_or_replace<Parent>(child_0, Parent{.parent = entity});") != std::string::npos);
    CHECK(wrapper.find("registry.emplace_or_replace<Parent>(child_0_0, Parent{.parent = child_0});") !=
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
                                        "rule S:\n"
                                        "    on tick:\n"
                                        "        let root = spawn Rig:\n"
                                        "            Tag:\n"
                                        "                value = 3\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto spawned_name = extract_temp_name(code, "cactus_gen_spawned_");
    REQUIRE_FALSE(spawned_name.empty());
    CHECK(code.find("auto " + spawned_name + " = create_rig(registry);") != std::string::npos);
    CHECK(code.find("return " + spawned_name + ";") != std::string::npos);
}

TEST_CASE("Codegen EnTT: spawn with child overrides expands inline per node", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX +
                                        "rule S:\n"
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

    const auto spawned_name = extract_temp_name(code, "cactus_gen_spawned_");
    REQUIRE_FALSE(spawned_name.empty());

    // Inline expansion uses the per-node helpers, not the canonical wrapper.
    CHECK(code.find("auto " + spawned_name + " = create_rig__node(registry);") != std::string::npos);
    CHECK(code.find("auto child_0 = create_rig__node__socket(registry);") != std::string::npos);
    CHECK(code.find("registry.emplace_or_replace<Parent>(child_0, Parent{.parent = " + spawned_name + "});") !=
          std::string::npos);
    CHECK(code.find("auto child_0_0 = create_rig__node__socket__gem(registry);") != std::string::npos);
    CHECK(code.find("registry.emplace_or_replace<Parent>(child_0_0, Parent{.parent = child_0});") != std::string::npos);
    // Per-node overrides applied to the matching created entity.
    CHECK(code.find("registry.try_get<Tag>(child_0)") != std::string::npos);
    CHECK(code.find("registry.try_get<Growth>(child_0_0)") != std::string::npos);
    CHECK(code.find("return " + spawned_name + ";") != std::string::npos);
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
    CHECK(wrapper.find("registry.emplace_or_replace<Parent>(child_0, Parent{.parent = entity});") != std::string::npos);
    // Child override value is baked into the per-node helper.
    const auto socket_fn = generated_function(code, "entt::entity create_rig1__node__socket(entt::registry&");
    CHECK(socket_fn.find("component.value = 7") != std::string::npos);
}

TEST_CASE("Codegen EnTT: destroy cascade code coexists with hierarchical creation", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(HIERARCHY_SOURCE_PREFIX +
                                        "rule S:\n"
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
        "rule NavPoll:\n"
        "    filter:\n"
        "        Rig\n"
        "    on tick:\n"
        "        let _w = input.wheel_delta()\n"
        "        let _d = input.mouse_delta()\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void nav_poll_tick");
    CHECK(rule.find("cactus::runtime::entt_backend::editor_wheel_delta()") != std::string::npos);
    CHECK(rule.find("cactus::runtime::entt_backend::editor_mouse_delta_screen()") != std::string::npos);
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
        "rule EditorInputConsume:\n"
        "    filter:\n"
        "        EditorSt\n"
        "    on input:\n"
        "        input.consume(NavDrag)\n",
        program,
        std_input_imports());

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void editor_input_consume_input");
    // The call must pass K_NAV_DRAG (the generated enum constant) to editor_consume.
    CHECK(rule.find("cactus::runtime::entt_backend::editor_consume(K_NAV_DRAG)") != std::string::npos);
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
        "rule Project2D:\n"
        "    filter:\n"
        "        Scene\n"
        "    on tick:\n"
        "        let _wp = camera2d.screen_to_world(vec2(0.0, 0.0))\n"
        "        let _wd = camera2d.screen_delta_to_world(vec2(1.0, 0.0))\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void project2_d_tick");
    CHECK(rule.find("cactus::runtime::entt_backend::editor_screen_to_world_2d(") != std::string::npos);
    CHECK(rule.find("cactus::runtime::entt_backend::screen_delta_to_world_2d(") != std::string::npos);
}

TEST_CASE("Codegen EnTT: std.transform.flat world_position injects registry as first argument",
          "[codegen-entt][editor][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.transform.flat as transform2d\n"
        "pub event tick\n"
        "trait Selection:\n"
        "    var sel: entity_id\n"
        "rule GetPos:\n"
        "    filter:\n"
        "        Selection\n"
        "    on tick:\n"
        "        let _pos = transform2d.world_position(sel)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void get_pos_tick");
    CHECK(rule.find("cactus::runtime::entt_backend::editor_entity_position_2d(registry,") != std::string::npos);
}

TEST_CASE("Codegen EnTT: clean-named editor extern func spawn_template lowers with registry injected",
          "[codegen-entt][editor][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick\n"
        "pub extern func spawn_template(template_name: string, position_2d: vec2, position_3d: vec3) entity_id\n"
        "trait Placer:\n"
        "    var result: entity_id\n"
        "rule PlaceTest:\n"
        "    filter:\n"
        "        Placer\n"
        "    on tick:\n"
        "        result = spawn_template(\"Enemy\", vec2(0.0, 0.0), vec3(0.0, 0.0, 0.0))\n",
        program);

    decorated.funcs["spawn_template"].is_stdlib   = true;
    decorated.funcs["spawn_template"].module_name = "std.editor";

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void place_test_tick");
    CHECK(rule.find("cactus::runtime::entt_backend::editor_spawn_template(registry,") != std::string::npos);
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

TEST_CASE("Codegen EnTT: rule function name uses canonical module prefix when program.module_name is set",
          "[codegen-entt][canonical-identity]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Velocity:\n"
        "    var vx: float\n"
        "rule Move:\n"
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
        "rule Move:\n"
        "    filter:\n"
        "        Position\n"
        "    on tick:\n"
        "        x = x + tick.dt\n",
        program);
    decorated.traits.at("Position").module_name = "my.game";

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
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
        "rule Demo:\n"
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

TEST_CASE("Codegen EnTT: stdlib extern rule lowering uses canonical C++ names for module-qualified traits",
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
        "extern rule SpriteRenderer:\n"
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
        "rule First:\n"
        "    on input:\n"
        "        let sample = input.dt\n"
        "rule Second:\n"
        "    filter:\n"
        "        Marker\n"
        "    on input:\n"
        "        value = value + 1\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.find("#include <deque>") != std::string::npos);
    CHECK(code.find("#include <functional>") != std::string::npos);
    CHECK(code.find("#include <variant>") != std::string::npos);
    CHECK(code.find("using EventOccurrence = std::variant<ContactEvent, frameEvent>;") != std::string::npos);
    CHECK(code.find("EventOccurrence occurrence;") != std::string::npos);
    CHECK(code.find("void generated_inject_external_event(frameEvent occurrence)") != std::string::npos);
    CHECK(code.find("void generated_inject_external_event(ContactEvent occurrence)") == std::string::npos);

    CHECK(code.find("std::deque<QueuedEvent> event_queue;") != std::string::npos);
    CHECK(code.find("std::deque<QueuedEvent> deferred_events;") != std::string::npos);
    CHECK(code.find("enum class Kind : std::uint8_t { Spawn, Destroy, Add, Remove };") != std::string::npos);
    CHECK(code.find("std::vector<StructuralCommand> commands;") != std::string::npos);

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

    // reset_consumed_input() fires once per frame occurrence, before any phase
    // batch runs, regardless of the module's own phase-name choices.
    const auto reset_call = code.find("reset_consumed_input();", root_dispatch);
    REQUIRE(reset_call != std::string::npos);
    CHECK(reset_call < input_call);
    // This module declares phases named "input"/"fixed_tick" but none named
    // "render", so the graph-driven scheduler section (everything before the
    // always-emitted legacy generated_update_project/generated_render_project,
    // which unconditionally call these regardless of graph-driven usage) must
    // not contain the render-flush boundary or projected-trait cleanup.
    const auto legacy_start = code.find("void generated_update_project");
    REQUIRE(legacy_start != std::string::npos);
    const auto graph_driven_code = code.substr(0, legacy_start);
    CHECK(graph_driven_code.find("begin_render_frame()") == std::string::npos);
    CHECK(graph_driven_code.find("end_render_frame()") == std::string::npos);
    CHECK(graph_driven_code.find("clear_projected_traits(registry);") == std::string::npos);

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
        "cactus::runtime::entt_backend::game_scheduler__inputPhaseRuntimeState& input,");
    const auto second_handler = code.find(
        "void second_input(entt::registry& registry, const "
        "cactus::runtime::entt_backend::game_scheduler__inputPhaseRuntimeState& input,");
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
        "rule Legacy:\n"
        "    on tick:\n"
        "        let sample = tick.dt\n",
        legacy_program);
    const auto legacy_code = CppEnttCodegen::generate(legacy);
    CHECK(legacy_code.find("Graph Activation Runtime State") == std::string::npos);
    CHECK(legacy_code.find("SchedulerState") == std::string::npos);
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

TEST_CASE("Codegen EnTT: declared phase fields are assigned from their resolved binding",
          "[codegen-entt][phase-runtime][phase-field-initializer]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "module game.propagation\n"
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "    dt: float = frame.dt\n"
        "phase fixed_tick:\n"
        "    after:\n"
        "        tick\n"
        "    every: 0.1\n"
        "phase render:\n"
        "    after:\n"
        "        fixed_tick\n"
        "    alpha: float = fixed_tick.alpha\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    // Root-event-bound field: `tick.dt` is assigned from the batch's own `root_event` parameter.
    const auto tick_batch = code.find("void generated_run_phase_batch_game_propagation__tick");
    REQUIRE(tick_batch != std::string::npos);
    const auto tick_dispatch =
        code.find("generated_dispatch_phase_game_propagation__tick(registry, phase);", tick_batch);
    REQUIRE(tick_dispatch != std::string::npos);
    const auto tick_assignment = code.find("phase.dt = static_cast<float>(root_event.dt);", tick_batch);
    REQUIRE(tick_assignment != std::string::npos);
    CHECK(tick_assignment < tick_dispatch);

    const auto fixed_batch_decl = code.find("void generated_run_phase_batch_game_propagation__fixed_tick");
    REQUIRE(fixed_batch_decl != std::string::npos);
    REQUIRE(tick_batch < fixed_batch_decl);
    const auto tick_batch_body = code.substr(tick_batch, fixed_batch_decl - tick_batch);
    // A field is read from root_event, so the batch no longer needs to silence an unused parameter.
    CHECK(tick_batch_body.find("(void)root_event;") == std::string::npos);

    // Upstream-phase-bound field: `render.alpha` is assigned from `fixed_tick`'s own persisted runtime state.
    const auto render_batch = code.find("void generated_run_phase_batch_game_propagation__render");
    REQUIRE(render_batch != std::string::npos);
    const auto render_dispatch =
        code.find("generated_dispatch_phase_game_propagation__render(registry, phase);", render_batch);
    REQUIRE(render_dispatch != std::string::npos);
    const auto render_assignment =
        code.find("phase.alpha = static_cast<float>(scheduler.game_propagation__fixed_tick.alpha);", render_batch);
    REQUIRE(render_assignment != std::string::npos);
    CHECK(render_assignment < render_dispatch);

    // The periodic phase's own synthesized dt/alpha bookkeeping is unaffected.
    REQUIRE(fixed_batch_decl < render_batch);
    const auto fixed_batch_body = code.substr(fixed_batch_decl, render_batch - fixed_batch_decl);
    CHECK(fixed_batch_body.find("phase.dt = interval;") != std::string::npos);
    CHECK(fixed_batch_body.find("phase.alpha = phase.accumulator / interval;") != std::string::npos);
    CHECK(fixed_batch_body.find("phase.dt = static_cast<float>(root_event.dt);") == std::string::npos);

    // This module never declares phases named input/fixed_tick/tick/late_tick — its
    // pipeline is tick -> fixed_tick -> render, from a non-std.core module. The
    // render-flush boundary must still land on the phase literally named "render"
    // (module-agnostic), bracketing only its dispatch call.
    const auto render_begin_flush = code.find("begin_render_frame();", render_batch);
    const auto render_end_flush   = code.find("end_render_frame();", render_batch);
    REQUIRE(render_begin_flush != std::string::npos);
    REQUIRE(render_end_flush != std::string::npos);
    CHECK(render_begin_flush < render_dispatch);
    CHECK(render_dispatch < render_end_flush);
    CHECK(fixed_batch_body.find("begin_render_frame()") == std::string::npos);
    CHECK(fixed_batch_body.find("end_render_frame()") == std::string::npos);
    const auto tick_batch_body_full = code.substr(tick_batch, fixed_batch_decl - tick_batch);
    CHECK(tick_batch_body_full.find("begin_render_frame()") == std::string::npos);
    CHECK(tick_batch_body_full.find("end_render_frame()") == std::string::npos);
}

TEST_CASE("Codegen EnTT: graph-driven render flush wires per-frame housekeeping in the correct order",
          "[codegen-entt][phase-runtime][render-flush]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module game.flush\n"
        "pub extern event frame:\n"
        "    dt: float\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "trait Renderer:\n"
        "    let texture: texture_id\n"
        "phase input:\n"
        "    from:\n"
        "        frame\n"
        "phase render:\n"
        "    after:\n"
        "        input\n"
        "extern rule SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on render:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    // reset_consumed_input() fires before the input phase batch, and
    // clear_projected_traits() fires after the render phase batch, both
    // within the frame-typed root-event handler.
    const auto root_event_fn =
        code.find("void generated_process_root_event(entt::registry& registry, const frameEvent& root_event)");
    REQUIRE(root_event_fn != std::string::npos);
    const auto reset_call = code.find("reset_consumed_input();", root_event_fn);
    const auto input_call =
        code.find("generated_run_phase_batch_game_flush__input(registry, root_event);", root_event_fn);
    const auto render_call =
        code.find("generated_run_phase_batch_game_flush__render(registry, root_event);", root_event_fn);
    const auto clear_call = code.find("clear_projected_traits(registry);", root_event_fn);
    REQUIRE(reset_call != std::string::npos);
    REQUIRE(input_call != std::string::npos);
    REQUIRE(render_call != std::string::npos);
    REQUIRE(clear_call != std::string::npos);
    CHECK(reset_call < input_call);
    CHECK(input_call < render_call);
    CHECK(render_call < clear_call);

    // begin_render_frame()/end_render_frame() bracket exactly the render
    // phase's dispatch call — not the event-cascade drain or activation commit.
    const auto render_batch = code.find("void generated_run_phase_batch_game_flush__render");
    REQUIRE(render_batch != std::string::npos);
    const auto begin_flush   = code.find("begin_render_frame();", render_batch);
    const auto dispatch_call = code.find("generated_dispatch_phase_game_flush__render(registry, phase);", render_batch);
    const auto end_flush     = code.find("end_render_frame();", render_batch);
    const auto cascade_drain = code.find("generated_drain_event_cascade(registry);", render_batch);
    const auto commit        = code.find("generated_commit_activation(registry);", render_batch);
    REQUIRE(begin_flush != std::string::npos);
    REQUIRE(dispatch_call != std::string::npos);
    REQUIRE(end_flush != std::string::npos);
    REQUIRE(cascade_drain != std::string::npos);
    REQUIRE(commit != std::string::npos);
    CHECK(begin_flush < dispatch_call);
    CHECK(dispatch_call < end_flush);
    CHECK(end_flush < cascade_drain);
    CHECK(cascade_drain < commit);

    // The input phase batch is untouched by the render-flush wrap.
    const auto input_batch = generated_function(code, "void generated_run_phase_batch_game_flush__input");
    CHECK(input_batch.find("begin_render_frame()") == std::string::npos);
    CHECK(input_batch.find("end_render_frame()") == std::string::npos);
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
        "rule Producer:\n"
        "    on tick:\n"
        "        emit Contact:\n"
        "            amount = 1\n"
        "rule FirstContact:\n"
        "    on Contact:\n"
        "        emit Reaction:\n"
        "            amount = Contact.amount\n"
        "rule SecondContact:\n"
        "    on Contact:\n"
        "        let sample = Contact.amount\n"
        "rule ReactionConsumer:\n"
        "    on Reaction:\n"
        "        let sample = Reaction.amount\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.find("std::deque<QueuedEvent> root_event_queue;") != std::string::npos);
    CHECK(code.find("const auto next_depth = activation.current_cascade_depth + 1;") != std::string::npos);
    CHECK(code.find("if (next_depth > kMaxEventCascadeDepth)") != std::string::npos);
    CHECK(code.find("activation.deferred_events.push_back(std::move(queued));") != std::string::npos);
    CHECK(code.find("generated_emit_event(ContactEvent{.amount = 1});") != std::string::npos);
    CHECK(code.find("generated_emit_event(ReactionEvent{.amount = Contact.amount});") != std::string::npos);
    CHECK(code.find("generated_drain_event_cascade(registry);") != std::string::npos);
    CHECK(code.find("generated_dispatch_event(registry, occurrence, queued.target)") != std::string::npos);

    const auto contact_dispatch =
        code.find("void generated_dispatch_event(entt::registry& registry, const ContactEvent& occurrence,");
    REQUIRE(contact_dispatch != std::string::npos);
    const auto first_call  = code.find("::first_contact_Contact(registry, occurrence, target);", contact_dispatch);
    const auto second_call = code.find("::second_contact_Contact(registry, occurrence, target);", contact_dispatch);
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
        "rule Producer:\n"
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
        "rule ContactConsumer:\n"
        "    on Contact:\n"
        "        destroy Contact.victim\n",
        program);

    const auto code         = CppEnttCodegen::generate(decorated);
    const auto spawned_name = extract_temp_name(code, "cactus_gen_spawned_");
    REQUIRE_FALSE(spawned_name.empty());
    CHECK(code.find("auto " + spawned_name + " = cactus::runtime::entt_backend::generated_reserve_entity(registry);") !=
          std::string::npos);
    CHECK(code.find("create_particle_at(registry, " + spawned_name + ");") != std::string::npos);
    CHECK(code.find("generated_queue_structural_command(") != std::string::npos);
    CHECK(code.find("StructuralCommand::Kind::Spawn") != std::string::npos);
    CHECK(code.find("StructuralCommand::Kind::Add") != std::string::npos);
    CHECK(code.find("StructuralCommand::Kind::Remove") != std::string::npos);
    CHECK(code.find("StructuralCommand::Kind::Destroy") != std::string::npos);

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
        "rule LegacySpawner:\n"
        "    on tick:\n"
        "        let particle = spawn Particle:\n"
        "            Position:\n"
        "                x = 1.0\n",
        legacy_program);
    const auto legacy_code         = CppEnttCodegen::generate(legacy);
    const auto legacy_spawned_name = extract_temp_name(legacy_code, "cactus_gen_spawned_");
    REQUIRE_FALSE(legacy_spawned_name.empty());
    CHECK(legacy_code.find("auto " + legacy_spawned_name + " = create_particle(registry);") != std::string::npos);
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
        "extern rule SpriteRenderer:\n"
        "    filter:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on render:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n"
        "        effects:\n"
        "            graphics\n"
        "extern rule SpriteAnimation:\n"
        "    filter:\n"
        "        AnimatedSprite\n"
        "    on render:\n"
        "        writes:\n"
        "            AnimatedSprite\n"
        "        effects:\n"
        "            graphics\n",
        program);

    for (auto& declaration : program.declarations) {
        if (auto* rule = std::get_if<ExternRuleNode>(&declaration)) {
            rule->is_stdlib = true;
        }
    }

    const auto code = CppEnttCodegen::generate(decorated);
    const auto dispatch =
        generated_function(code,
                           "void generated_dispatch_phase_std_render_sprites__render(entt::registry& registry, const "
                           "std_render_sprites__renderPhaseRuntimeState& phase)");
    const auto first  = dispatch.find("::sprite_renderer_tick(registry);");
    const auto second = dispatch.find("::sprite_animation_tick(registry);");
    REQUIRE(first != std::string::npos);
    REQUIRE(second != std::string::npos);
    CHECK(first < second);

    CHECK(code.find("cactus_external__std_render_sprites__SpriteRenderer") == std::string::npos);
    CHECK(code.find("cactus_external__std_render_sprites__SpriteAnimation") == std::string::npos);
    const auto update = generated_function(code, "void generated_update_project(");
    const auto render = generated_function(code, "void generated_render_project(");
    CHECK(update.find("sprite_renderer_tick") == std::string::npos);
    CHECK(update.find("sprite_animation_tick") == std::string::npos);
    CHECK(render.find("sprite_renderer_tick") == std::string::npos);
    CHECK(render.find("sprite_animation_tick") == std::string::npos);
}

// ── Pair relations (dsl-pair-relations, 5.5) ─────────────────────────────────

static const std::string PAIR_CODEGEN_TRAITS =
    "trait DynamicBody:\n"
    "    var vx: float\n"
    "trait Transform:\n"
    "    var x: float\n"
    "trait Solid:\n"
    "    var active: bool = true\n"
    "trait Collider:\n"
    "    var mask: int\n"
    "event Contact:\n"
    "    other: entity_id\n";

TEST_CASE("Codegen EnTT: pair handler snapshots both bindings and iterates their product",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            PAIR_CODEGEN_TRAITS +
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            DynamicBody\n"
            "            Transform\n"
            "        wall:\n"
            "            Solid\n"
            "            Collider\n"
            "    on tick:\n"
            "        if body != wall and body.Transform.x > 0.0 and wall.Collider.mask > 0:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            // Two independent typed snapshots, not a materialized tuple list.
            CHECK(code.find("std::vector<entt::entity> body_snapshot;") != std::string::npos);
            CHECK(code.find("registry.view<DynamicBody, Transform>()") != std::string::npos);
            CHECK(code.find("std::vector<entt::entity> wall_snapshot;") != std::string::npos);
            CHECK(code.find("registry.view<Solid, Collider>()") != std::string::npos);
            // Sorted by creation ordinal for deterministic tuple order, reading
            // each entity's ordinal exactly once rather than per comparison.
            CHECK(code.find("registry.get<cactus::runtime::entt_backend::CreationOrdinal>(pair_entity).value,") !=
                  std::string::npos);
            CHECK(code.find("std::ranges::sort(body_ordered);") != std::string::npos);
            // Left-binding-major nested iteration over the snapshots, in the
            // untargeted (broadcast) branch — the recipient-targeted branch
            // precedes it and has its own incident-tuple loop shapes.
            const auto broadcast_branch = code.find("} else {");
            REQUIRE(broadcast_branch != std::string::npos);
            const auto body_loop = code.find("for (auto body : body_snapshot)", broadcast_branch);
            const auto wall_loop = code.find("for (auto wall : wall_snapshot)", broadcast_branch);
            REQUIRE(body_loop != std::string::npos);
            REQUIRE(wall_loop != std::string::npos);
            CHECK(body_loop < wall_loop);
            // Recipient-targeted delivery: incident tuples only, tested via an
            // O(1) all_of<> membership check against the recipient.
            CHECK(code.find("if (cactus_recipient.has_value()) {") != std::string::npos);
            CHECK(code.find("const auto target = *cactus_recipient;") != std::string::npos);
            CHECK(code.find("if (registry.all_of<DynamicBody, Transform>(target)) {") != std::string::npos);
            CHECK(code.find("if (body == target) { continue; }") != std::string::npos);
            // Pair-bound trait reads are const.
            CHECK(code.find("registry.get<const Transform>(body).x") != std::string::npos);
            CHECK(code.find("registry.get<const Collider>(wall).mask") != std::string::npos);
            // Bare binding identifiers used directly as entity_id (emit target).
            CHECK(code.find("(body != wall)") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: pair handler wraps the broadcast tuple body in a per-invocation lambda so "
          "`return` skips only the current tuple",
          "[codegen-entt][pair-relations]") {
    // Regression test: DetectBubbleContact in examples/bouncy-bubbles rejects
    // self-pairs with `if a == b: return`. Both bindings snapshot the same
    // entity set in the same order, so the very first tuple the broadcast
    // double loop produces is always a self-pair — if `return` were a bare
    // C++ `return;` spliced directly into the loop body (no lambda), it
    // would exit the whole generated handler function on that first tuple
    // and no other pair would ever be evaluated.
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            PAIR_CODEGEN_TRAITS +
            "rule DetectBubbleContact:\n"
            "    pairs:\n"
            "        a:\n"
            "            Collider\n"
            "        b:\n"
            "            Collider\n"
            "    on tick:\n"
            "        if a == b:\n"
            "            return\n"
            "        emit Contact:\n"
            "            other = b\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            const auto broadcast_branch = code.find("} else {");
            REQUIRE(broadcast_branch != std::string::npos);
            const auto a_loop = code.find("for (auto a : a_snapshot)", broadcast_branch);
            const auto b_loop = code.find("for (auto b : b_snapshot)", broadcast_branch);
            REQUIRE(a_loop != std::string::npos);
            REQUIRE(b_loop != std::string::npos);
            CHECK(a_loop < b_loop);
            const auto lambda_open = code.find("[&]() {", b_loop);
            const auto return_stmt = code.find("return;", b_loop);
            REQUIRE(lambda_open != std::string::npos);
            REQUIRE(return_stmt != std::string::npos);
            // The tuple body must execute inside its own per-invocation lambda so
            // `return` ends only the current tuple instead of the enclosing
            // generated function.
            CHECK(lambda_open < return_stmt);
            const auto lambda_close = code.find("}();", return_stmt);
            CHECK(lambda_close != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: pair handler wraps both recipient-targeted tuple bodies in a per-invocation "
          "lambda",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            PAIR_CODEGEN_TRAITS +
            "rule DetectBubbleContact:\n"
            "    pairs:\n"
            "        a:\n"
            "            Collider\n"
            "        b:\n"
            "            Collider\n"
            "    on tick:\n"
            "        if a == b:\n"
            "            return\n"
            "        emit Contact:\n"
            "            other = b\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            const auto recipient_branch = code.find("if (cactus_recipient.has_value()) {");
            REQUIRE(recipient_branch != std::string::npos);
            const auto broadcast_branch = code.find("} else {", recipient_branch);
            REQUIRE(broadcast_branch != std::string::npos);

            // Recipient-as-left: `a` fixed to the target, looping over `b`.
            const auto left_loop = code.find("for (auto b : b_snapshot)", recipient_branch);
            REQUIRE(left_loop != std::string::npos);
            CHECK(left_loop < broadcast_branch);
            const auto left_lambda = code.find("[&]() {", left_loop);
            const auto left_return = code.find("return;", left_loop);
            REQUIRE(left_lambda != std::string::npos);
            REQUIRE(left_return != std::string::npos);
            CHECK(left_lambda < left_return);
            CHECK(left_return < broadcast_branch);
            const auto left_close = code.find("}();", left_return);
            REQUIRE(left_close != std::string::npos);
            CHECK(left_close < broadcast_branch);

            // Recipient-as-right: `b` fixed to the target, looping over `a`.
            const auto right_loop = code.find("for (auto a : a_snapshot)", left_loop);
            REQUIRE(right_loop != std::string::npos);
            CHECK(right_loop < broadcast_branch);
            const auto right_lambda = code.find("[&]() {", right_loop);
            const auto right_return = code.find("return;", right_loop);
            REQUIRE(right_lambda != std::string::npos);
            REQUIRE(right_return != std::string::npos);
            CHECK(right_lambda < right_return);
            CHECK(right_return < broadcast_branch);
            const auto right_close = code.find("}();", right_return);
            REQUIRE(right_close != std::string::npos);
            CHECK(right_close < broadcast_branch);
        }
    }
}

TEST_CASE("Codegen EnTT: pair binding selecting the same trait as the other binding gets distinct access",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            PAIR_CODEGEN_TRAITS +
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            Collider\n"
            "        wall:\n"
            "            Collider\n"
            "    on tick:\n"
            "        if body.Collider.mask > wall.Collider.mask:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.get<const Collider>(body).mask") != std::string::npos);
            CHECK(code.find("registry.get<const Collider>(wall).mask") != std::string::npos);
        }
    }
}

TEST_CASE(
    "Codegen EnTT: pair binding-local alias shadowing another trait's name still resolves to its own "
    "canonical trait, not the shadowed spelling",
    "[codegen-entt][pair-relations]") {
    // `wall` aliases `Solid` as `Collider`, deliberately colliding with the
    // real `Collider` trait's name. Codegen must consume the resolved
    // BoundTraitAccess identity attached by semantic analysis rather than
    // re-deriving the trait from the dotted source spelling — a naive
    // string-based resolver would mistake `wall.Collider.active` for the
    // unrelated `Collider` trait (which has no `active` field) instead of
    // `Solid` (which does).
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            PAIR_CODEGEN_TRAITS +
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            DynamicBody\n"
            "        wall:\n"
            "            Solid as Collider\n"
            "    on tick:\n"
            "        if wall.Collider.active:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.get<const Solid>(wall).active") != std::string::npos);
            CHECK(code.find("registry.get<const Collider>(wall)") == std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: pair binding-local alias and qualified imported trait resolve to canonical access",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            PAIR_CODEGEN_TRAITS +
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            DynamicBody\n"
            "        wall:\n"
            "            Collider as c\n"
            "    on tick:\n"
            "        if wall.c.mask > 0:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.get<const Collider>(wall).mask") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: pair handler under the graph runtime uses targeted emit and buffered project",
          "[codegen-entt][pair-relations][runtime-graph]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n" +
            PAIR_CODEGEN_TRAITS +
            "trait GroundContact:\n"
            "    var active: bool = true\n"
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            DynamicBody\n"
            "        wall:\n"
            "            Solid\n"
            "    on tick:\n"
            "        emit Contact to body:\n"
            "            other = wall\n"
            "        project GroundContact to body\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    // Creation ordinal scaffolding is emitted and assigned at entity creation.
    CHECK(code.find("struct CreationOrdinal {") != std::string::npos);
    CHECK(code.find("generated_next_creation_ordinal()") != std::string::npos);
    // Targeted emit resolves `body` to the per-tuple loop variable, evaluates
    // it once, and carries it into the queued occurrence via the targeted
    // emit path (targeted-event-delivery) rather than a validity guard around
    // broadcast dispatch. The recipient temporary is mangled so it can never
    // collide with a `to:` expression that is itself the bare identifier
    // `target` (e.g. `emit X(to: target)`).
    const auto emit_target_name = extract_temp_name(code, "cactus_gen_emit_target_");
    REQUIRE_FALSE(emit_target_name.empty());
    CHECK(code.find("const auto " + emit_target_name + " = body;") != std::string::npos);
    CHECK(code.find("if (registry.valid(" + emit_target_name + ")) {") != std::string::npos);
    CHECK(code.find("generated_emit_targeted_event(ContactEvent{.other = wall}, " + emit_target_name + ");") !=
          std::string::npos);
    // Project remains an immediate (non-buffered) call, same as unary rules.
    CHECK(code.find("project_GroundContact(registry, body);") != std::string::npos);
}

TEST_CASE("Codegen EnTT: pair binding on a marker (zero-field) trait still snapshots correctly",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait DynamicBody:\n"
        "    var vx: float\n"
        "trait Solid\n"
        "event Contact:\n"
        "    other: entity_id\n"
        "rule DetectContacts:\n"
        "    pairs:\n"
        "        body:\n"
        "            DynamicBody\n"
        "        wall:\n"
        "            Solid\n"
        "    on tick:\n"
        "        emit Contact to body:\n"
        "            other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("registry.view<Solid>()") != std::string::npos);
            CHECK(code.find("std::vector<entt::entity> wall_snapshot;") != std::string::npos);
            CHECK(code.find("for (auto wall : wall_snapshot)") != std::string::npos);
        }
    }
}

// ── Recipient-aware event runtime (targeted-event-delivery, 6.4) ────────────

TEST_CASE("Codegen EnTT: drain_event_cascade drops a stale-recipient occurrence before any dispatch",
          "[codegen-entt][targeted-event-delivery]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "event Contact:\n"
        "    other: entity_id\n"
        "rule Consumer:\n"
        "    on Contact:\n"
        "        let x = 1\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    // The recipient is checked once, before generated_dispatch_event is
    // called at all — a stale target drops the occurrence for every consumer,
    // not just the ones that happen to check validity themselves.
    // A forward declaration of this function precedes its definition, so the
    // search must include the opening brace to skip past it.
    const auto drain = generated_function(code, "void generated_drain_event_cascade(entt::registry& registry) {");
    CHECK(drain.find("if (queued.target.has_value() && !registry.valid(*queued.target)) {") != std::string::npos);
    const auto guard_pos    = drain.find("if (queued.target.has_value()");
    const auto continue_pos = drain.find("continue;", guard_pos);
    const auto dispatch_pos = drain.find("generated_dispatch_event(registry, occurrence, queued.target);");
    REQUIRE(guard_pos != std::string::npos);
    REQUIRE(continue_pos != std::string::npos);
    REQUIRE(dispatch_pos != std::string::npos);
    CHECK(continue_pos < dispatch_pos);
}

TEST_CASE("Codegen EnTT: deferred cascade preserves a targeted occurrence's recipient",
          "[codegen-entt][targeted-event-delivery]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "event Contact:\n"
        "    other: entity_id\n"
        "rule Producer:\n"
        "    on tick:\n"
        "        emit Contact to Producer:\n"
        "            other = Producer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    // generated_emit_targeted_event builds one `queued` envelope carrying the
    // target and pushes that same envelope to either the immediate queue or
    // deferred_events depending on cascade depth — there is no separate path
    // that drops the target when deferring.
    const auto targeted =
        generated_function(code, "void generated_emit_targeted_event(Occurrence occurrence, entt::entity target)");
    CHECK(targeted.find(".target = target};") != std::string::npos);
    CHECK(targeted.find("activation.deferred_events.push_back(std::move(queued));") != std::string::npos);
}

TEST_CASE("Codegen EnTT: selectionless event handler accepts and ignores an optional recipient",
          "[codegen-entt][targeted-event-delivery]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Contact:\n"
        "    other: entity_id\n"
        "rule Observer:\n"
        "    on Contact:\n"
        "        let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("std::optional<entt::entity> cactus_recipient = std::nullopt") != std::string::npos);
            CHECK(code.find("(void)cactus_recipient;") != std::string::npos);
            // No recipient-conditioned branch — a selectionless consumer runs
            // once regardless of targeting (targeted-event-delivery,
            // "Selectionless observer receives targeted occurrence once").
            CHECK(code.find("cactus_recipient.has_value()") == std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: targeted unary handler runs at most once for the recipient, gated on its selection",
          "[codegen-entt][targeted-event-delivery]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Contact:\n"
        "    other: entity_id\n"
        "trait Health:\n"
        "    var hp: int\n"
        "rule ResolveContact:\n"
        "    filter:\n"
        "        Health\n"
        "    on Contact:\n"
        "        hp = hp - 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.find("if (cactus_recipient.has_value()) {") != std::string::npos);
            CHECK(code.find("entt::entity entity = *cactus_recipient;") != std::string::npos);
            CHECK(code.find("if (registry.all_of<Health>(entity)) {") != std::string::npos);
            // Component access for the targeted branch mirrors the broadcast
            // branch's binding name so the same body text works unmodified.
            CHECK(code.find("auto& Health_comp = registry.get<Health>(entity);") != std::string::npos);
            // Broadcast fallback for an untargeted occurrence remains the
            // ordinary full-view iteration.
            CHECK(code.find("registry.view<Health>()") != std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: targeted external handler dispatch is gated on the recipient satisfying its selection",
          "[codegen-entt][targeted-event-delivery][external-handler]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "event Contact:\n"
        "    other: entity_id\n"
        "trait Health:\n"
        "    var hp: int\n"
        "extern rule ResolveContact:\n"
        "    filter:\n"
        "        Health\n"
        "    on Contact:\n"
        "        reads:\n"
        "            Health\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto dispatch =
        code.find("void generated_dispatch_event(entt::registry& registry, const ContactEvent& occurrence,");
    REQUIRE(dispatch != std::string::npos);
    const auto tail = code.substr(dispatch);
    CHECK(tail.find("if (target.has_value()) {") != std::string::npos);
    CHECK(tail.find("entt::entity entity = *target;") != std::string::npos);
}

TEST_CASE("Codegen EnTT: dotted assignment through a filter alias writes the field instead of shadowing the alias",
          "[codegen-entt][assignment]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Contact:\n"
        "    amount: int\n"
        "trait Health:\n"
        "    var current: int\n"
        "rule ResolveContact:\n"
        "    filter:\n"
        "        Health as hp\n"
        "    on Contact as dmg:\n"
        "        hp.current = hp.current - dmg.amount\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            // The alias `hp` is already declared as a reference to the Health
            // component; the assignment must reuse it via ordinary member
            // access rather than redeclaring it with `auto`, which would be a
            // compile error (redefinition of `hp`).
            CHECK(code.find("hp.current = (hp.current - dmg.amount);") != std::string::npos);
            CHECK(code.find("auto hp = ") == std::string::npos);
        }
    }
}

TEST_CASE("Codegen EnTT: range() lowers to a zero-allocation ascending/descending counting loop",
          "[codegen-entt][range]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "event Tally:\n"
        "    n: int\n"
        "rule CountUp:\n"
        "    on tick:\n"
        "        for k in range(0, 5):\n"
        "            emit Tally:\n"
        "                n = k\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "CountUp") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);

        // No list[T] snapshot (std::vector, or the generic foreach_snapshot_
        // temp) is constructed for the range itself.
        CHECK(code.find("std::vector") == std::string::npos);
        CHECK(code.find("foreach_snapshot_") == std::string::npos);

        const auto step_name = extract_temp_name(code, "cactus_gen_range_step_");
        REQUIRE_FALSE(step_name.empty());
        CHECK(code.find("if (" + step_name + " > 0) {") != std::string::npos);
        CHECK(code.find("} else if (" + step_name + " < 0) {") != std::string::npos);
        // No unconditional trailing branch: a step of 0 falls through both
        // arms and executes the loop body zero times.
        CHECK(code.find("} else {\n") == std::string::npos);
        CHECK(code.find("for (int k = ") != std::string::npos);
    }
}

TEST_CASE("Codegen EnTT: range() descending iteration counts down", "[codegen-entt][range]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "event Tally:\n"
        "    n: int\n"
        "rule CountDown:\n"
        "    on tick:\n"
        "        for k in range(5, 0, -1):\n"
        "            emit Tally:\n"
        "                n = k\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "CountDown") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(code.find("k < ") != std::string::npos);
        CHECK(code.find("k > ") != std::string::npos);
        CHECK(code.find("k += ") != std::string::npos);
    }
}

TEST_CASE("Codegen EnTT: range() begin/end/step expressions are each emitted exactly once", "[codegen-entt][range]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "event Tally:\n"
        "    n: int\n"
        "rule CountRange:\n"
        "    on tick:\n"
        "        for k in range(111 + 0, 222 + 0, 333 + 0):\n"
        "            emit Tally:\n"
        "                n = k\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "CountRange") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(count_occurrences(code, "111") == 1);
        CHECK(count_occurrences(code, "222") == 1);
        CHECK(count_occurrences(code, "333") == 1);
    }
}

TEST_CASE("Codegen EnTT: range() default step omits a third argument but still emits step 1", "[codegen-entt][range]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "event Tally:\n"
        "    n: int\n"
        "rule DefaultStep:\n"
        "    on tick:\n"
        "        for k in range(0, 3):\n"
        "            emit Tally:\n"
        "                n = k\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "DefaultStep") {
            continue;
        }
        auto code            = EnttSystemEmitter::emit_system(*sys, decorated);
        const auto step_name = extract_temp_name(code, "cactus_gen_range_step_");
        REQUIRE_FALSE(step_name.empty());
        CHECK(code.find("const int " + step_name + " = 1;") != std::string::npos);
    }
}

// range()'s step need not be a compile-time literal — an author-supplied
// trait field could describe a zero or direction-mismatched span. Confirms
// no unconditional/default branch exists that would run the loop body
// regardless of step, since a non-literal step can't be checked at
// compile time.
TEST_CASE("Codegen EnTT: range() with a non-literal step has no unconditional fallback branch",
          "[codegen-entt][range]") {
    ProgramNode program;
    // A brace-free loop body (`let x = k`, unlike e.g. an `emit` statement's
    // `push_back({.n = k})`) so the only braces between the descending
    // for-loop's increment and end-of-block are the loop's own structural
    // closes, making the brace-counting check below unambiguous.
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "trait Source:\n"
        "    var step_value: int\n"
        "rule CountVariableStep:\n"
        "    filter:\n"
        "        Source\n"
        "    on tick:\n"
        "        for k in range(0, 10, step_value):\n"
        "            let x = k\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "CountVariableStep") {
            continue;
        }
        auto code            = EnttSystemEmitter::emit_system(*sys, decorated);
        const auto step_name = extract_temp_name(code, "cactus_gen_range_step_");
        REQUIRE_FALSE(step_name.empty());

        const std::string else_if_text = "} else if (" + step_name + " < 0) {";
        const auto else_if_pos         = code.find(else_if_text);
        REQUIRE(else_if_pos != std::string::npos);
        // The descending for-loop's own increment (its last use of the step
        // temp), after the condition matched above.
        const auto increment_pos = code.find(step_name, else_if_pos + else_if_text.size());
        REQUIRE(increment_pos != std::string::npos);

        // From the increment, the next 3 closing braces are exactly this
        // range's own structural closes: the for-loop, the if/else-if chain,
        // and the enclosing block — with no further branch. Confirms a
        // runtime step of 0 (falling through both `if`/`else if` arms) has no
        // unconditional fallback that would run the loop body anyway.
        auto brace_pos = increment_pos;
        for (int i = 0; i < 3; ++i) {
            brace_pos = code.find('}', brace_pos + 1);
            REQUIRE(brace_pos != std::string::npos);
        }
        const auto own_closes = code.substr(increment_pos, brace_pos + 1 - increment_pos);
        CHECK(own_closes.find("else") == std::string::npos);
    }
}

TEST_CASE("Codegen EnTT: vec2/vec3 int arguments are wrapped in static_cast<float> (generic call fallback)",
          "[codegen-entt][range][vector-expressions]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "rule Demo:\n"
        "    on tick:\n"
        "        for k in range(0, 1):\n"
        "            let a = vec2(k, 0.0)\n"
        "            let b = vec3(k, 0, 0.0)\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "Demo") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        // int-kind arguments (the loop variable `k`, and the bare int
        // literal `0`) are cast; the already-float argument `0.0` is not
        // redundantly wrapped.
        CHECK(code.find("vec2(static_cast<float>(k), 0.0F)") != std::string::npos);
        CHECK(code.find("vec3(static_cast<float>(k), static_cast<float>(0), 0.0F)") != std::string::npos);
    }
}

TEST_CASE("Codegen EnTT: vec2 int arguments are wrapped in static_cast<float> (VarAssign pretty-printer)",
          "[codegen-entt][range][vector-expressions]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "rule Demo:\n"
        "    on tick:\n"
        "        for k in range(0, 1):\n"
        "            v = vec2(k, 0.0)\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "Demo") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(code.find("static_cast<float>(k)") != std::string::npos);
        CHECK(code.find("0.0F") != std::string::npos);
        // The already-float argument isn't redundantly wrapped.
        CHECK(code.find("static_cast<float>(0.0F)") == std::string::npos);
    }
}

// infer_numeric_kind previously only resolved lexical locals and trait
// fields, so a module-level `const:` identifier (e.g. `PARTICLE_COUNT`)
// mixed with a range loop variable in float arithmetic (the particle-burst
// example's `k * (TAU / PARTICLE_COUNT)` angle-step pattern) produced an
// uncast `int * float` multiplication in generated code — a narrowing
// conversion clang-tidy's bugprone-narrowing-conversions rejects.
TEST_CASE("Codegen EnTT: binary arithmetic mixing a range loop variable with a const casts correctly",
          "[codegen-entt][range]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "const:\n"
        "    TAU = 6.283185307\n"
        "    PARTICLE_COUNT = 8\n"
        "rule Demo:\n"
        "    on tick:\n"
        "        for k in range(0, PARTICLE_COUNT):\n"
        "            let angle = k * (TAU / PARTICLE_COUNT)\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "Demo") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(code.find("static_cast<float>(k) * (TAU / static_cast<float>(PARTICLE_COUNT))") != std::string::npos);
    }
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
