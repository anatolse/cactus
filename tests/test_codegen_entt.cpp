// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/codegen_error.hpp"
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
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

// A load or unload boundary dispatches, drains, commits, then settles persistence requests.
static void check_boundary_activation_order(const std::string& code) {
    const auto boundary = generated_function(
        code, "void generated_run_boundary_activation(entt::registry& registry, const Occurrence& occurrence) {");
    const auto dispatch = boundary.find("generated_dispatch_event(registry, occurrence);");
    const auto drain    = boundary.find("generated_drain_event_cascade(registry);");
    const auto commit   = boundary.find("generated_commit_activation(registry);");
    const auto settle   = boundary.find("generated_process_persistence_boundary(registry);");
    REQUIRE(dispatch != std::string::npos);
    REQUIRE(drain != std::string::npos);
    REQUIRE(commit != std::string::npos);
    REQUIRE(settle != std::string::npos);
    CHECK(dispatch < drain);
    CHECK(drain < commit);
    CHECK(commit < settle);
}

// Drops every harness-only region, leaving what a build without the macro compiles.
static std::string without_linearization_harness(const std::string& code) {
    std::string result;
    std::size_t position = 0;
    while (true) {
        const auto start = code.find("#ifdef CACTUS_LINEARIZATION_HARNESS\n", position);
        if (start == std::string::npos) {
            result += code.substr(position);
            return result;
        }
        result += code.substr(position, start - position);
        const auto end_marker = std::string("#endif  // CACTUS_LINEARIZATION_HARNESS\n");
        const auto end        = code.find(end_marker, start);
        REQUIRE(end != std::string::npos);
        const auto else_marker = code.find("#else  // CACTUS_LINEARIZATION_HARNESS\n", start);
        if (else_marker != std::string::npos && else_marker < end) {
            const auto kept_start = else_marker + std::string("#else  // CACTUS_LINEARIZATION_HARNESS\n").size();
            result += code.substr(kept_start, end - kept_start);
        }
        position = end + end_marker.size();
    }
}

// The payload parameter name of a generated handler function, whatever its spelling.
static std::string handler_payload_param(const std::string& function) {
    const std::string signature_prefix = "(entt::registry& registry, const ";
    const auto type_start              = function.find(signature_prefix);
    REQUIRE(type_start != std::string::npos);
    const auto name_start = function.find("& ", type_start + signature_prefix.size());
    REQUIRE(name_start != std::string::npos);
    const auto name_end = function.find(',', name_start);
    REQUIRE(name_end != std::string::npos);
    return function.substr(name_start + 2, name_end - name_start - 2);
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
    ResolvedFunc set_cursor_captured;
    set_cursor_captured.name         = "set_cursor_captured";
    set_cursor_captured.module_name  = "std.input";
    set_cursor_captured.symbol_id    = make_symbol_id(SymbolKind::Func, "std.input", "set_cursor_captured");
    set_cursor_captured.canonical_id = make_canonical_id(*set_cursor_captured.symbol_id);
    set_cursor_captured.is_pub       = true;
    set_cursor_captured.is_extern    = true;
    set_cursor_captured.params.push_back(
        ResolvedParam{.name = "captured", .type = {.kind = TypeKind::Bool, .name = "bool"}});
    syms.funcs.emplace(set_cursor_captured.name, std::move(set_cursor_captured));
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
                            .is_pub     = false});
    trait.fields.push_back({.name       = "y",
                            .type       = {.kind = TypeKind::Float, .name = "float"},
                            .is_let     = false,
                            .is_var     = true,
                            .is_persist = false,
                            .is_pub     = false});

    auto code = EnttComponentEmitter::emit_component(trait);
    CHECK(code.contains("struct test_codegen__Position"));
    CHECK(code.contains("float x{}"));
    CHECK(code.contains("float y{}"));
    // Should NOT have std::vector (that's SoA)
    CHECK(!code.contains("std::vector"));
}

TEST_CASE("cpp-entt lowers external handlers to canonical contract-shaped callbacks", "[codegen][concise]") {
    std::string source = R"(
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
    each:
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
    SECTION("legacy blocks") {}
    SECTION("compact clauses") {
        const std::vector<std::pair<std::string, std::string>> replacements{
            {"    each:\n        Position\n        Velocity", "    each: Position, Velocity"},
            {"        reads:\n            Position", "        reads: Position"},
            {"        projects:\n            Position", "        projects: Position"},
            {"        writes:\n            Velocity", "        writes: Velocity"},
            {"        emits:\n            Contact", "        emits: Contact"},
            {"        commands:\n            spawn Particle\n            destroy\n            add Disabled\n           "
             " remove Disabled",
             "        commands: spawn Particle, destroy, add Disabled, remove Disabled"},
            {"        effects:\n            physics", "        effects: physics"},
            {"        effects:\n            host.observe", "        effects: host.observe"}};
        for (const auto& [block, compact] : replacements) {
            const auto position = source.find(block);
            REQUIRE(position != std::string::npos);
            source.replace(position, block.size(), compact);
        }
    }
    ProgramNode ast;
    auto program = full_pipeline(source, ast);

    const auto generated = CppEnttCodegen::generate(program);

    CHECK(generated.contains("struct Capabilities__game__Integrate__on__game__simulate"));
    CHECK(generated.contains("void emit_game__Contact(ContactEvent occurrence) const"));
    CHECK(generated.contains("[[nodiscard]] entt::entity command_spawn_game__Particle() const"));
    CHECK(generated.contains("void command_destroy(entt::entity target) const"));
    CHECK(generated.contains("void command_add_game__Disabled(entt::entity target, game__Disabled value = {}) const"));
    CHECK(generated.contains("void command_remove_game__Disabled(entt::entity target) const"));
    CHECK(generated.contains("EffectService effect_physics() const noexcept"));
    CHECK(generated.contains("[[nodiscard]] game__Position* project_game__Position(entt::entity target) const"));
    CHECK(generated.contains("if (!registry.valid(target)) { return nullptr; }"));
    CHECK(generated.contains("return &::project_game__Position(registry, target);"));
    CHECK(generated.contains("void cactus_external__game__Integrate__on__game__simulate("));
    CHECK(generated.contains("entt::entity entity"));
    CHECK(generated.contains("const game__Position& read_game__Position"));
    CHECK(generated.contains("game__Velocity& write_game__Velocity"));
    CHECK(generated.contains("for (const auto entity : registry.view<game__Position, game__Velocity>())"));
    CHECK(generated.contains("::cactus_external__game__Integrate__on__game__simulate("));
    CHECK(generated.contains("Capabilities__game__Integrate__on__game__simulate{registry}"));

    const auto integrate_capabilities = generated.find("struct Capabilities__game__Integrate__on__game__simulate");
    REQUIRE(integrate_capabilities != std::string::npos);
    const auto integrate_capabilities_end = generated.find("\n};\n\n", integrate_capabilities);
    REQUIRE(integrate_capabilities_end != std::string::npos);
    const auto integrate_surface =
        generated.substr(integrate_capabilities, integrate_capabilities_end - integrate_capabilities);
    CHECK(!integrate_surface.contains("std::function"));
    CHECK(integrate_surface.contains("project_game__Position"));
    CHECK(integrate_surface.contains("emit_game__Contact"));
    CHECK(integrate_surface.contains("effect_physics"));
    CHECK(!integrate_surface.contains("effect_host__observe"));

    CHECK(generated.contains("void cactus_external__game__Monitor__on__game__simulate("));
    CHECK(generated.contains("::cactus_external__game__Monitor__on__game__simulate("));
    const auto monitor_capabilities = generated.find("struct Capabilities__game__Monitor__on__game__simulate");
    REQUIRE(monitor_capabilities != std::string::npos);
    const auto monitor_capabilities_end = generated.find("\n};\n\n", monitor_capabilities);
    REQUIRE(monitor_capabilities_end != std::string::npos);
    const auto monitor_surface =
        generated.substr(monitor_capabilities, monitor_capabilities_end - monitor_capabilities);
    CHECK(!monitor_surface.contains("emit_"));
    CHECK(!monitor_surface.contains("project_"));
    CHECK(!monitor_surface.contains("command_"));
    CHECK(monitor_surface.contains("effect_host__observe"));
    CHECK(!monitor_surface.contains("effect_physics"));
    CHECK(!generated.contains("void game__Integrate__tick(entt::registry& registry)"));
    CHECK(!generated.contains("void game__Monitor__tick(entt::registry& registry)"));
}

TEST_CASE("cpp-entt lowers an external set command to a queue-only typed patch capability",
          "[codegen-entt][external-handler][deferred-set]") {
    const auto* const source = R"(
module game

pub extern event frame:
    dt: float

trait Health:
    var current: int = 10
    var max: int = 10

trait Aim:
    var other: entity_id

phase simulate:
    from:
        frame

extern rule Heal:
    each:
        Aim
    on simulate:
        reads:
            Aim
        commands:
            set Health
)";
    ProgramNode ast;
    auto program = full_pipeline(source, ast);

    const auto generated    = CppEnttCodegen::generate(program);
    const auto capabilities = generated.find("struct Capabilities__game__Heal__on__game__simulate");
    REQUIRE(capabilities != std::string::npos);
    const auto capabilities_end = generated.find("\n};\n\n", capabilities);
    REQUIRE(capabilities_end != std::string::npos);
    const auto surface = generated.substr(capabilities, capabilities_end - capabilities);

    CHECK(surface.contains("struct game__Health_patch {"));
    CHECK(surface.contains("std::optional<decltype(game__Health::current)> current;"));
    CHECK(surface.contains("std::optional<decltype(game__Health::max)> max;"));
    CHECK(surface.contains("void command_set_game__Health(entt::entity target, game__Health_patch patch) const"));
    CHECK(surface.contains("StructuralCommand::Kind::Set"));
    CHECK(surface.contains("::durable_game__Health(registry, target)"));
    CHECK(!surface.contains("cancel_projected_"));
    CHECK(!surface.contains("retain_construction"));
    CHECK(!surface.contains("game__Health&"));
    CHECK(!surface.contains("game__Health*"));

    const auto callback = generated.find("void cactus_external__game__Heal__on__game__simulate(");
    REQUIRE(callback != std::string::npos);
    const auto callback_signature = generated.substr(callback, generated.find(')', callback) - callback);
    CHECK(!callback_signature.contains("game__Health"));
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
    CHECK(code.contains("float f = 1.5F;"));
    CHECK(code.contains("Vector2 v2 = vec2(1.0F, 2.0F);"));
    CHECK(code.contains("Vector3 v3 = vec3(1.0F, 2.0F, 3.0F);"));
    CHECK(code.contains("Quat q = quat(0.0F, 0.0F, 0.0F, 1.0F);"));
    CHECK(code.contains("bool flag = true;"));
    CHECK(code.contains("int n = 7;"));
    CHECK(code.contains("Color c = Color{.r = 255, .g = 0, .b = 0, .a = 255};"));
    CHECK(code.contains("std::vector<int> items = {1, 2};"));
    CHECK(code.contains("std::vector<int> empty_items = {};"));
    CHECK(code.contains("std::string s = \"hi\";"));
    // A declared `= ""` default is redundant with std::string's own default
    // construction, and spelling it out trips clang-tidy's
    // readability-redundant-string-init on generated output.
    CHECK(code.contains("std::string empty_s{};"));
    CHECK(!code.contains("empty_s = \"\""));
    CHECK(code.contains("float required{};"));
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
    CHECK(code_a.contains("float x = 1.0F;"));

    const auto code_b = EnttComponentEmitter::emit_component(thing_b, combined_program);
    CHECK(code_b.contains("float x = 2.0F;"));
}

// A default expression may call a module-aliased stdlib extern func (e.g.
// stdlib's own `rand.seeded(0)`), which needs the same stdlib call-site
// rewriting handler-body assignments already get — the DecoratedProgram
// overload of emit_expr, not the bare-AST one.
TEST_CASE("Codegen EnTT: field default calling a module-aliased stdlib func resolves to the runtime namespace",
          "[codegen-entt][defaults][stdlib]") {
    ResolvedFunc seeded;
    seeded.name         = "seeded";
    seeded.module_name  = "std.random";
    seeded.symbol_id    = make_symbol_id(SymbolKind::Func, "std.random", "seeded");
    seeded.canonical_id = make_canonical_id(*seeded.symbol_id);
    seeded.is_pub       = true;
    seeded.is_extern    = true;
    seeded.effect_summary = std::unordered_set<std::string>{};
    seeded.params.push_back(ResolvedParam{.name = "s", .type = {.kind = TypeKind::Int, .name = "int"}});
    seeded.return_type = TypeInfo{.kind = TypeKind::Int, .name = "int"};
    ImportedSymbols random;
    random.module_name = "std.random";
    random.funcs.emplace(seeded.name, std::move(seeded));
    ModuleImports imports;
    imports.add("rand", std::move(random));

    ProgramNode ast;
    auto decorated = full_pipeline(
        "use std.random as rand\n"
        "trait TreeRng:\n"
        "    var rng: int = rand.seeded(0)\n",
        ast,
        imports);

    ResolvedTrait tree_rng;
    tree_rng.name        = "TreeRng";
    tree_rng.module_name = "test";
    tree_rng.fields.push_back({.name = "rng", .type = {.kind = TypeKind::Int, .name = "int"}, .is_var = true});

    const auto code = EnttComponentEmitter::emit_component(tree_rng, decorated);
    CHECK(code.contains("cactus::runtime::stdlib::random::seeded(0)"));
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
        "    each:\n"
        "        Pos\n"
        "    on step:\n"
        "        x = x + step.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("void move_step"));
            CHECK(code.contains("entt::registry& registry"));
            CHECK(code.contains("const stepEvent& step"));
            CHECK(code.contains("Pos_comp.x = (Pos_comp.x + step.dt)"));
            CHECK(code.contains("registry.view<Pos>()"));
            CHECK(code.contains("view.each([&](entt::entity entity, [[maybe_unused]] Pos& Pos_comp)"));
            CHECK(!code.contains("registry.storage<entt::entity>()"));
            CHECK(!code.contains("cactus_try_get_projected_or_durable_Pos(registry, entity)"));
        }
    }
}

TEST_CASE("Codegen EnTT: filtered handler body is emitted once and both dispatch sites call it", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event step:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "    var y: float\n"
        "rule Move:\n"
        "    each:\n"
        "        Pos\n"
        "    on step:\n"
        "        x = x + step.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            // The rewritten handler body's distinguishing statement appears
            // exactly once in the generated function, not once per dispatch site.
            CHECK(count_occurrences(code, "Pos_comp.x = (Pos_comp.x + step.dt)") == 1);

            const auto handler_body_name = extract_temp_name(code, "cactus_gen_handler_body_");
            REQUIRE_FALSE(handler_body_name.empty());
            CHECK(code.contains("auto " + handler_body_name + " = [&](entt::entity entity"));

            const auto recipient_branch = code.find("if (cactus_recipient.has_value()) {");
            REQUIRE(recipient_branch != std::string::npos);
            const auto broadcast_branch = code.find("} else {", recipient_branch);
            REQUIRE(broadcast_branch != std::string::npos);

            // Both the recipient-targeted branch and the broadcast view.each
            // branch invoke the shared lambda by call, not by re-splicing it.
            const auto recipient_call = code.find(handler_body_name + "(entity", recipient_branch);
            REQUIRE(recipient_call != std::string::npos);
            CHECK(recipient_call < broadcast_branch);

            const auto broadcast_call = code.find(handler_body_name + "(entity", broadcast_branch);
            REQUIRE(broadcast_call != std::string::npos);
        }
    }
}

static std::string generated_struct(const std::string& code, const std::string& name) {
    const auto start = code.find("struct " + name + " {");
    REQUIRE(start != std::string::npos);
    const auto end = code.find("\n};\n", start);
    REQUIRE(end != std::string::npos);
    return code.substr(start, end - start + 4);
}

TEST_CASE("Codegen EnTT: event struct", "[codegen-entt]") {
    ResolvedEvent event;
    event.name = "Damage";
    event.fields.push_back({.name = "amount", .type = {.kind = TypeKind::Int, .name = "int"}});

    const auto code = EnttEventEmitter::emit_event(event, "DamageEvent");
    CHECK(code == "struct DamageEvent {\n    int amount{};\n};\n");

    ProgramNode program;
    auto decorated = full_pipeline(
        "event Damage:\n"
        "   amount: int\n",
        program);
    for (auto& decl : program.declarations) {
        if (auto* node = std::get_if<EventNode>(&decl)) {
            auto sink = EnttEventEmitter::emit_sink_connection(*node, decorated);
            CHECK(sink.contains("dispatcher.sink<DamageEvent>"));
        }
    }
}

TEST_CASE("Codegen EnTT: an event supports entity and vector payload fields", "[codegen-entt][events]") {
    ResolvedEvent event;
    event.name = "Contact";
    event.fields.push_back({.name = "other", .type = {.kind = TypeKind::EntityId, .name = "entity_id"}});
    event.fields.push_back({.name = "overlap", .type = {.kind = TypeKind::Vec2, .name = "vec2"}});

    const auto code = EnttEventEmitter::emit_event(event, "ContactEvent");
    CHECK(code.contains("entt::entity other{entt::null};"));
    CHECK(code.contains("Vector2 overlap{};"));
}

TEST_CASE("Codegen EnTT: an unset entity_id field starts stale, never as the first entity",
          "[codegen-entt][entity-handles]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "struct Link:\n"
        "    target: entity_id\n"
        "    weight: int\n"
        "trait Holder:\n"
        "    var who: entity_id\n"
        "    var link: Link\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(generated_struct(code, "Holder").contains("entt::entity who{entt::null};"));
    const auto link = generated_struct(code, "Link");
    CHECK(link.contains("entt::entity target{entt::null};"));
    CHECK(link.contains("int weight{};"));
}

TEST_CASE("Codegen EnTT: enum-typed event field resolves to its generated enum class type",
          "[codegen-entt][events][enum]") {
    const auto gizmo_mode = make_symbol_id(SymbolKind::Enum, "std.editor", "GizmoMode");
    ResolvedEvent event;
    event.name        = "EditorModeChanged";
    event.module_name = "std.editor";
    event.fields.push_back(
        {.name = "previous_mode", .type = {.kind = TypeKind::Enum, .name = "GizmoMode", .symbol_id = gizmo_mode}});
    event.fields.push_back(
        {.name = "current_mode", .type = {.kind = TypeKind::Enum, .name = "GizmoMode", .symbol_id = gizmo_mode}});

    const auto code = EnttEventEmitter::emit_event(event, "std_editor__EditorModeChangedEvent");
    CHECK(code.contains("std_editor__GizmoMode previous_mode{};"));
    CHECK(code.contains("std_editor__GizmoMode current_mode{};"));
}

TEST_CASE("Codegen EnTT: quat event field gets the backend quaternion type", "[codegen-entt][events]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "event Turn:\n"
        "    rotation: quat\n"
        "    amount: int\n",
        program);

    const auto turn = generated_struct(CppEnttCodegen::generate(decorated), "TurnEvent");
    CHECK(turn == "struct TurnEvent {\n    Quat rotation{};\n    int amount{};\n};\n");
}

TEST_CASE("Codegen EnTT: struct, enum, and entity_id event fields use the trait field types",
          "[codegen-entt][events]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "struct Stats:\n"
        "    hp: int\n"
        "enum Mode:\n"
        "    Calm\n"
        "    Angry\n"
        "trait Holder:\n"
        "    var stats: Stats\n"
        "    var mode: Mode\n"
        "    var who: entity_id\n"
        "event Changed:\n"
        "    stats: Stats\n"
        "    mode: Mode\n"
        "    who: entity_id\n",
        program);

    const auto code    = CppEnttCodegen::generate(decorated);
    const auto holder  = generated_struct(code, "Holder");
    const auto changed = generated_struct(code, "ChangedEvent");
    const auto field_type = [](const std::string& text, const std::string& field) {
        const auto end = text.find(" " + field + "{");
        REQUIRE(end != std::string::npos);
        const auto start = text.rfind("    ", end) + 4;
        return text.substr(start, end - start);
    };
    for (const auto* field : {"stats", "mode", "who"}) {
        INFO(field);
        CHECK(field_type(changed, field) == field_type(holder, field));
    }
    CHECK(field_type(changed, "who") == "entt::entity");
    CHECK(field_type(changed, "stats") != "int");
    CHECK(field_type(changed, "mode") != "int");
}

TEST_CASE("Codegen EnTT: an AST event with no resolved event is an internal error", "[codegen-entt][events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Turn:\n"
        "    amount: int\n",
        program);
    decorated.events.clear();

    CHECK_THROWS_AS(CppEnttCodegen::generate(decorated), std::runtime_error);
}

TEST_CASE("Codegen EnTT: an event struct is the same with and without an AST", "[codegen-entt][events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event Turn:\n"
        "    rotation: quat\n"
        "    amount: int\n",
        program);
    decorated.module_name = "test";

    const auto with_ast = generated_struct(CppEnttCodegen::generate(decorated), "test__TurnEvent");
    decorated.ast       = nullptr;
    const auto without_ast = generated_struct(CppEnttCodegen::generate(decorated), "test__TurnEvent");
    CHECK(with_ast == without_ast);
}

TEST_CASE("Codegen EnTT: full pipeline", "[codegen-entt]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick: \n"
        "    dt: float\n"
        "trait Pos:\n    persist var x: float\n    persist var y: float\n"
        "rule Move:\n"
        "    each:\n"
        "        Pos\n"
        "    on tick:\n"
        "        x = x + tick.dt\n ",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.contains("Generated by Cactus DSL Compiler (cpp-entt backend)"));
    CHECK(code.contains("#include <entt/entt.hpp>"));
    CHECK(code.contains("#include <raylib.h>"));
    CHECK(code.contains("struct Pos"));
    CHECK(code.contains("backends/cpp-entt/runtime.hpp"));
    CHECK(code.contains("generated_project_config() noexcept"));
    CHECK(code.contains("generated_init_project(entt::registry& registry)"));
    CHECK(code.contains("generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt)"));
    CHECK(!code.contains("move_tick(registry"));
    CHECK(!code.contains("struct TickEvent"));
    CHECK(code.contains("int main()"));
    CHECK(code.contains("InitWindow(config.window_width, config.window_height, config.window_title)"));
    // Generated entrypoints disable raylib's default ESC-quits-window
    // behavior immediately after window creation (backend-cpp-entrypoints),
    // so authored gameplay can repurpose Escape without the window closing.
    CHECK(
        code.contains("InitWindow(config.window_width, config.window_height, config.window_title);\n"
                      "    SetExitKey(KEY_NULL);\n"));
    CHECK(code.contains("entt::registry registry;"));
    CHECK(code.contains("entt::dispatcher dispatcher;"));
    CHECK(code.contains("cactus::runtime::entt_backend::generated_setup_dispatcher(dispatcher);"));
    CHECK(code.contains("cactus::runtime::entt_backend::generated_init_project(registry);"));
    CHECK(code.contains("cactus::runtime::entt_backend::generated_update_project(registry, dispatcher, dt);"));
    CHECK(code.contains("BeginDrawing();"));
    CHECK(code.contains("ClearBackground(RAYWHITE);"));
    CHECK(code.contains("cactus::runtime::entt_backend::generated_render_project(registry, dispatcher);"));
    CHECK(code.contains("EndDrawing();"));

    // Persist hooks
    CHECK(code.contains("save_Pos"));
    CHECK(code.contains("load_Pos"));

    CHECK(!code.contains("replicate_"));
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
        "    each:\n"
        "        MouseState\n"
        "    on input:\n"
        "        if input.pressed(Select):\n"
        "            pos = input.mouse_position()\n",
        program,
        std_input_imports());

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.contains("int cactus_input_button_mouse(std::uint8_t button) noexcept"));
    CHECK(code.contains("return MOUSE_BUTTON_LEFT;"));
    CHECK(code.contains("cactus::runtime::entt_backend::pressed(action)"));
    CHECK(code.contains("cactus::runtime::entt_backend::mouse_position()"));
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
    CHECK(axis.contains("KEY_A"));
    CHECK(axis.contains("KEY_D"));
    CHECK(axis.contains("KEY_W"));
    CHECK(axis.contains("KEY_S"));
    // The dead-input fallback shape must never appear.
    CHECK(!axis.contains("return 0 - 0;"));
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
    CHECK(code.contains("return KEY_SPACE;"));
    CHECK(code.contains("return MOUSE_BUTTON_LEFT;"));
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
    CHECK(alias_code.contains("return KEY_SPACE;"));
}

TEST_CASE("Codegen EnTT: extern func generates runtime header include", "[codegen-entt][extern-func]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern func lerp(a: float, b: float, t: float) float\n"
        "trait Pos:\n    var x: float\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("#include \"common/cactus_runtime.hpp\""));
}

TEST_CASE("Codegen EnTT: no extern func means no runtime header", "[codegen-entt][extern-func]") {
    ProgramNode program;
    auto decorated = full_pipeline("trait Pos:\n    var x: float\n", program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(!code.contains("#include \"cactus_runtime.hpp\""));
}

TEST_CASE("Codegen EnTT: extern func body is not emitted", "[codegen-entt][extern-func]") {
    ProgramNode program;
    auto decorated = full_pipeline("pub extern func sin(a: float) float\n", program);

    auto code = CppEnttCodegen::generate(decorated);
    // No function definition should be emitted for the extern func
    // (the include is present but no function body)
    CHECK(!code.contains("float sin("));
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
        "    each:\n"
        "        Value\n"
        "    on tick:\n"
        "        x = math.lerp(0.0, 10.0, 0.5)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::stdlib::math::lerp(0.0F, 10.0F, 0.5F)"));
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
        "    each:\n"
        "        Value\n"
        "    on tick:\n"
        "        x = lerp(0.0, 10.0, 0.5)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::stdlib::math::lerp(0.0F, 10.0F, 0.5F)"));
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
        "    each:\n"
        "        Controller\n"
        "    on tick:\n"
        "        active = down(Jump)\n",
        program,
        std_input_imports());

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::down(K_JUMP)"));
}

TEST_CASE("Codegen EnTT: cursor capture aliases and canonical calls lower identically",
          "[codegen-entt][extern-func][stdlib][input][cursor]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.input as inp\n"
        "pub event tick:\n"
        "    dt: float\n"
        "rule Cursor:\n"
        "    on tick:\n"
        "        inp.set_cursor_captured(true)\n"
        "        std.input.set_cursor_captured(false)\n",
        program,
        std_input_imports());

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(count_occurrences(code, "cactus::runtime::entt_backend::set_cursor_captured(") == 2);
    CHECK(code.contains("cactus::runtime::entt_backend::set_cursor_captured(true)"));
    CHECK(code.contains("cactus::runtime::entt_backend::set_cursor_captured(false)"));
    CHECK(!code.contains("inp.set_cursor_captured"));
    CHECK(!code.contains("std.input.set_cursor_captured"));
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
        "    each:\n"
        "        WorldTransform\n"
        "        BoxCollider\n"
        "    on tick:\n"
        "        let hit = phys.query_cast_nearest(self, vec2(1.0, 0.0), 1, self)\n"
        "        if hit.kind == phys.QueryResultKind.Empty:\n"
        "            let contacts = phys.query_overlap_all(self, 1, self)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("auto hit = cactus_query_cast_nearest(registry, entity, vec2(1.0F, 0.0F), 1, entity)"));
    CHECK(code.contains("if (hit.kind == QueryResultKind::Empty)"));
    CHECK(code.contains("auto contacts = cactus_query_overlap_all(registry, entity, 1, entity)"));
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
    CHECK(code.contains("cactus::runtime::entt_backend::destroy_entity_recursive(registry, "
                        "cactus::runtime::entt_backend::hierarchy_index<Parent>(registry), entity);"));
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
        "    each:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n"
        "extern rule SpriteAnimation:\n"
        "    each:\n"
        "        AnimatedSprite\n"
        "    on run:\n"
        "        writes:\n"
        "            AnimatedSprite\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::submit_sprite("));
    CHECK(code.contains("Renderer_comp.layer"));
    CHECK(code.contains("cactus::runtime::entt_backend::advance_animated_sprite("));
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
        "    each:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::begin_render_frame();"));
    CHECK(code.contains("cactus::runtime::entt_backend::end_render_frame();"));
    const auto render = generated_function(code, "void generated_render_project");
    CHECK(!render.contains("sprite_renderer_tick(registry);"));
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
        "    each:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::submit_mesh("));
    CHECK(code.contains("Renderer_comp.color"));
    CHECK(!code.contains("void mesh_renderer_update("));
}

// Regression coverage for the runtime-side directional-light rewire: codegen
// recognition of DirectionalLightRender (system_emitter.cpp) is independent of
// how the cpp-entt runtime shades the light, so it must still emit exactly one
// register_directional_light call fed by the trait's own fields.
TEST_CASE("Codegen EnTT: directional light render extern rule binds to backend runtime without user callback",
          "[codegen-entt][assets]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.render.meshes\n"
        "trait DirectionalLight:\n"
        "    var direction: vec3\n"
        "    var color: color\n"
        "    var intensity: float\n"
        "    var enabled: bool\n"
        "event run\n"
        "extern rule DirectionalLightRender:\n"
        "    each:\n"
        "        DirectionalLight\n"
        "    on run:\n"
        "        reads:\n"
        "            DirectionalLight\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::register_directional_light("));
    CHECK(code.contains("DirectionalLight_comp.direction"));
    CHECK(code.contains("DirectionalLight_comp.color"));
    CHECK(code.contains("DirectionalLight_comp.intensity"));
    CHECK(code.contains("DirectionalLight_comp.enabled"));
    CHECK(!code.contains("void directional_light_render_update("));
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
        "    each:\n"
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
    CHECK(code.contains("case ShapeType::Circle:"));
    CHECK(code.contains("draw_shape_circle(WorldTransform_comp.position"));
    CHECK(code.contains("Shape_comp.size.x,"));
    CHECK(code.contains("Shape_comp.origin,"));
    // Rectangle draws via draw_shape_rectangle, passing origin and rotation through.
    CHECK(code.contains("case ShapeType::Rectangle:"));
    CHECK(code.contains("draw_shape_rectangle(WorldTransform_comp.position"));
    CHECK(code.contains("WorldTransform_comp.rotation,"));
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
    CHECK(line_tick.contains("const DrawDebugLine2DEvent& occurrence"));
    CHECK(line_tick.contains("BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d())"));
    CHECK(line_tick.contains("DrawLineEx(occurrence.start, occurrence.end, occurrence.thickness, occurrence.color)"));
    CHECK(line_tick.contains("EndMode2D()"));
    CHECK(count_occurrences(line_tick, "DrawLineEx(") == 1);

    const auto tri_tick = generated_function(code, "void draw_debug_triangle2_d_renderer_tick");
    CHECK(tri_tick.contains("DrawTriangle(occurrence.a, occurrence.b, occurrence.c, occurrence.color)"));
    CHECK(count_occurrences(tri_tick, "DrawTriangle(") == 1);

    const auto ring_tick = generated_function(code, "void draw_debug_ring_outline2_d_renderer_tick");
    CHECK(
        ring_tick.contains("DrawRing(occurrence.center, occurrence.inner_radius, occurrence.outer_radius, 0.0F, "
                           "360.0F, 32, occurrence.color)"));

    const auto rect_tick = generated_function(code, "void draw_debug_rect_outline2_d_renderer_tick");
    CHECK(rect_tick.contains(".x = occurrence.position.x"));
    CHECK(rect_tick.contains("DrawRectangleLinesEx(__rect, occurrence.thickness, occurrence.color)"));
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
    CHECK(line_tick.contains("const DrawDebugLine3DEvent& occurrence"));
    CHECK(line_tick.contains("BeginMode3D(cactus::runtime::entt_backend::get_active_camera_3d())"));
    CHECK(line_tick.contains("DrawLine3D(occurrence.start, occurrence.end, occurrence.color)"));
    CHECK(line_tick.contains("EndMode3D()"));

    const auto box_tick = generated_function(code, "void draw_debug_wire_box3_d_renderer_tick");
    CHECK(box_tick.contains("DrawCubeWiresV(occurrence.center, occurrence.size, occurrence.color)"));

    const auto circle_tick = generated_function(code, "void draw_debug_circle3_d_renderer_tick");
    CHECK(
        circle_tick.contains("DrawCircle3D(occurrence.center, occurrence.radius, occurrence.normal, 90.0F, "
                             "occurrence.color)"));

    const auto cube_tick = generated_function(code, "void draw_debug_cube3_d_renderer_tick");
    CHECK(cube_tick.contains("DrawCubeV(occurrence.center, occurrence.size, occurrence.color)"));
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
        "    each:\n"
        "        EditorCamera3D\n"
        "    on render:\n"
        "        effects:\n"
        "            graphics\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto tick = generated_function(code, "void debug_grid3_d_tick");
    CHECK(tick.contains("registry.view<EditorCamera3D>()"));
    CHECK(tick.contains("if (!__has_3d_rig) { return; }"));
    CHECK(tick.contains("DrawGrid(20, 1.0F)"));
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
    CHECK(tick.contains("const DrawScreenRectEvent& occurrence"));
    CHECK(tick.contains(".x = occurrence.position.x"));
    CHECK(tick.contains(".width = occurrence.size.x"));
    CHECK(tick.contains("if (occurrence.filled)"));
    CHECK(tick.contains("DrawRectangleRec(__rect, occurrence.color)"));
    CHECK(tick.contains("DrawRectangleLinesEx(__rect, occurrence.thickness, occurrence.color)"));
    // Screen-space: no camera-mode wrapping (world-space std.debug primitives get one).
    CHECK(!tick.contains("BeginMode2D"));
    CHECK(!tick.contains("BeginMode3D"));
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
        "    var color: color = #FFFFFFFF\n"
        "event run\n"
        "extern rule ModelRender:\n"
        "    each:\n"
        "        WorldTransform\n"
        "        ModelRenderer\n"
        "    on run:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            ModelRenderer\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::submit_model("));
    CHECK(code.contains("ModelRenderer_comp.cast_shadow, ModelRenderer_comp.color);"));
    CHECK(!code.contains("void model_render_update("));
    // No ModelAnimator trait in the program: the plain submission path only.
    CHECK(!code.contains("try_get<ModelAnimator>"));
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
        "    var color: color = #FFFFFFFF\n"
        "trait ModelAnimator:\n"
        "    var clip: int\n"
        "    var playing: bool\n"
        "    var speed: float\n"
        "    var time: float\n"
        "event run\n"
        "extern rule ModelAnimation:\n"
        "    each:\n"
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
    CHECK(tick.contains("registry.view<ModelRenderer, ModelAnimator>()"));
    CHECK(tick.contains("if (!ModelAnimator_comp.playing)"));
    CHECK(tick.contains("ModelAnimator_comp.time += kFixedDt * ModelAnimator_comp.speed;"));
    CHECK(tick.contains("cactus::runtime::entt_backend::model_animation_duration("));
    CHECK(tick.contains("std::fmod(ModelAnimator_comp.time, duration)"));

    // The adapter owns time advancement, but a generic event name no longer
    // causes the backend to infer update or render scheduling.
    const auto update = generated_function(code, "void generated_update_project");
    CHECK(!update.contains("model_animation_tick(registry);"));
    const auto render = generated_function(code, "void generated_render_project");
    CHECK(!render.contains("model_animation_tick"));
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
        "    var color: color = #FFFFFFFF\n"
        "trait ModelAnimator:\n"
        "    var clip: int\n"
        "    var playing: bool\n"
        "    var speed: float\n"
        "    var time: float\n"
        "event run\n"
        "extern rule ModelRender:\n"
        "    each:\n"
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
    CHECK(tick.contains("registry.try_get<ModelAnimator>(entity)"));
    CHECK(tick.contains("animator->clip, animator->time, ModelRenderer_comp.color);"));
    // ...while entities without ModelAnimator keep the plain submission.
    CHECK(tick.contains("ModelRenderer_comp.cast_shadow, ModelRenderer_comp.color);"));
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
        "    each:\n"
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
        CHECK(tick.contains("registry.view<ScreenLabel>()"));
        CHECK(
            tick.contains("cactus::runtime::entt_backend::submit_screen_label(ScreenLabel_comp.position, "
                          "ScreenLabel_comp.font_size, ScreenLabel_comp.color, ScreenLabel_comp.text, "
                          "ScreenLabel_comp.visible);"));
        CHECK(!tick.contains("(void)registry;"));

        // Generic event names do not imply update or render scheduling. A
        // linked phase graph is the sole source of runtime dispatch order.
        const auto render = generated_function(code, "void generated_render_project");
        CHECK(!render.contains("screen_label_render_tick(registry);"));
        const auto update = generated_function(code, "void generated_update_project");
        CHECK(!update.contains("screen_label_render_tick"));
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
        "    each:\n"
        "        ClipState\n"
        "    on tick:\n"
        "        clips = models.animation_count(Robot)\n"
        "        label = models.animation_name(Robot, clips - 1)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::model_animation_count(Robot)"));
    CHECK(code.contains("cactus::runtime::entt_backend::model_animation_name(Robot, "));
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
        "    each:\n"
        "        SizeState\n"
        "    on tick:\n"
        "        height = models.bounds_size(Robot).y\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::model_bounds_size(Robot)"));
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
        "    each:\n"
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
    CHECK(!load_hook.contains("spawn_enemies_load"));

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
    const auto boundary_pos  = main_fn.find("generated_run_boundary_activation(registry, loadEvent{});");
    const auto loop_pos      = main_fn.find("while (!WindowShouldClose())");
    REQUIRE(init_pos != std::string::npos);
    REQUIRE(load_call_pos != std::string::npos);
    REQUIRE(boundary_pos != std::string::npos);
    REQUIRE(loop_pos != std::string::npos);
    CHECK(init_pos < load_call_pos);
    CHECK(load_call_pos < boundary_pos);
    CHECK(boundary_pos < loop_pos);
    check_boundary_activation_order(code);
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
        "    each:\n"
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
    CHECK(load_hook.contains("(void)registry;"));

    const auto main_start = code.find("int main() try {");
    REQUIRE(main_start != std::string::npos);
    const auto main_end = code.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start);
    REQUIRE(main_end != std::string::npos);
    const auto main_fn = code.substr(main_start, main_end - main_start);

    CHECK(!main_fn.contains("generated_dispatch_event(registry, loadEvent{});"));

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
        "    each:\n"
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
    const auto boundary_pos    = main_fn.find("generated_run_boundary_activation(registry, unloadEvent{});");
    const auto close_pos       = main_fn.find("CloseWindow();");
    REQUIRE(loop_pos != std::string::npos);
    REQUIRE(end_drawing_pos != std::string::npos);
    REQUIRE(boundary_pos != std::string::npos);
    REQUIRE(close_pos != std::string::npos);
    CHECK(loop_pos < end_drawing_pos);
    CHECK(end_drawing_pos < boundary_pos);
    CHECK(boundary_pos < close_pos);
    check_boundary_activation_order(code);
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
        "    each:\n"
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

    CHECK(!main_fn.contains("generated_dispatch_event(registry, unloadEvent{});"));

    const auto loop_pos  = main_fn.find("while (!WindowShouldClose())");
    const auto close_pos = main_fn.find("CloseWindow();");
    REQUIRE(loop_pos != std::string::npos);
    REQUIRE(close_pos != std::string::npos);
    CHECK(loop_pos < close_pos);
}

TEST_CASE("Codegen EnTT: a deferred internal event runs as its own activation with its target",
          "[codegen-entt][graph-driven-lifecycle-events]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "event ping\n"
        "trait Pos:\n"
        "    var x: float = 0.0\n"
        "rule Pong:\n"
        "    each:\n"
        "        Pos\n"
        "    on ping:\n"
        "        x = x + 1.0\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    const auto root_fn = generated_function(
        code, "void generated_process_root_event(entt::registry& registry, const pingEvent& root_event");
    CHECK(root_fn.contains("std::optional<entt::entity> target"));
    const auto guard    = root_fn.find("if (target.has_value() && !registry.valid(*target)) {");
    const auto dispatch = root_fn.find("generated_dispatch_event(registry, root_event, target);");
    const auto drain    = root_fn.find("generated_drain_event_cascade(registry);");
    const auto commit   = root_fn.find("generated_commit_activation(registry);");
    REQUIRE(guard != std::string::npos);
    REQUIRE(dispatch != std::string::npos);
    REQUIRE(drain != std::string::npos);
    REQUIRE(commit != std::string::npos);
    CHECK(guard < dispatch);
    CHECK(dispatch < drain);
    CHECK(drain < commit);

    const auto drain_fn = generated_function(code, "void generated_drain_external_events");
    CHECK(drain_fn.contains("generated_process_root_event(registry, occurrence, queued.target);"));

    // Every event has a real processor, so no silent no-op fallback remains.
    CHECK(!code.contains("void generated_process_root_event(entt::registry&, const Occurrence&) {}"));
}

static const std::string& lifecycle_program_prefix() {
    static const std::string value =
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "pub event load\n"
        "trait Enemy\n"
        "trait Dying:\n"
        "    var elapsed: float = 0.0\n"
        "trait Burning:\n"
        "    var intensity: float = 1.0\n"
        "trait Unwatched\n";
    return value;
}

TEST_CASE("Codegen EnTT: a program without lifecycle triggers emits no tracking code",
          "[codegen-entt][trait-lifecycle]") {
    ProgramNode program;
    auto decorated = full_pipeline(lifecycle_program_prefix() +
                                       "rule Move:\n"
                                       "    each:\n"
                                       "        Dying\n"
                                       "    on tick:\n"
                                       "        elapsed = elapsed + tick.dt\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);

    const auto commit_fn = generated_function(code, "void generated_commit_activation");
    CHECK(commit_fn.contains("commit_activation(activation, registry, &generated_drain_event_cascade);"));
    CHECK(!code.contains("LifecycleTrackers"));
    CHECK(!code.contains("TraitAdded"));
    CHECK(!code.contains("TraitRemoved"));
    CHECK(!code.contains("deliver_arrivals"));
}

TEST_CASE("Codegen EnTT: on added tracks only the watched trait and dispatches to the target",
          "[codegen-entt][trait-lifecycle]") {
    ProgramNode program;
    auto decorated = full_pipeline(lifecycle_program_prefix() +
                                       "rule StartDying:\n"
                                       "    each:\n"
                                       "        Enemy\n"
                                       "    on added Dying as dying:\n"
                                       "        dying.elapsed = 0.0\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);

    const auto trackers_pos = code.find("LifecycleTrackers<");
    REQUIRE(trackers_pos != std::string::npos);
    const auto trackers_decl = code.substr(trackers_pos, code.find('>', trackers_pos) - trackers_pos + 1);
    CHECK(trackers_decl.contains("Dying"));
    CHECK(!trackers_decl.contains("Burning"));
    CHECK(!trackers_decl.contains("Enemy"));
    CHECK(!trackers_decl.contains("Unwatched"));

    const auto occurrence_pos = code.find("using EventOccurrence = std::variant<");
    REQUIRE(occurrence_pos != std::string::npos);
    const auto occurrence_decl = code.substr(occurrence_pos, code.find(";\n", occurrence_pos) - occurrence_pos);
    CHECK(occurrence_decl.contains("TraitAdded<Dying>"));
    CHECK(occurrence_decl.contains("TraitRemoved<Dying>"));
    CHECK(!occurrence_decl.contains("Burning"));

    const auto commit_fn = generated_function(code, "void generated_commit_activation");
    CHECK(commit_fn.contains("generated_lifecycle_trackers()"));

    const auto dispatch_fn = generated_function(code,
                                                "void generated_dispatch_event(entt::registry& registry, const "
                                                "cactus::runtime::entt_backend::TraitAdded<Dying>& occurrence");
    CHECK(dispatch_fn.contains("(registry, occurrence, target);"));

    const auto root_fn = generated_function(code,
                                            "void generated_process_root_event(entt::registry& registry, const "
                                            "cactus::runtime::entt_backend::TraitAdded<Dying>& root_event");
    CHECK(root_fn.contains("generated_dispatch_event(registry, root_event, target);"));

    // The handler runs only for its recipient, with the trait in its selection and the alias bound.
    const auto handler_fn = generated_function(code, "void start_dying_added_Dying(entt::registry& registry");
    CHECK(handler_fn.contains("cactus_recipient.has_value()"));
    CHECK(handler_fn.contains("registry.all_of<Enemy, Dying>(entity)"));
    CHECK(handler_fn.contains("auto& dying = Dying_comp;"));
    CHECK(!handler_fn.contains("registry.view<"));
}

TEST_CASE("Codegen EnTT: on removed binds the snapshot and excludes the trait", "[codegen-entt][trait-lifecycle]") {
    ProgramNode program;
    auto decorated = full_pipeline(lifecycle_program_prefix() +
                                       "rule StopBurning:\n"
                                       "    on removed Burning as old:\n"
                                       "        let heat = old.intensity\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);

    const auto handler_fn = generated_function(code, "void stop_burning_removed_Burning(entt::registry& registry");
    CHECK(handler_fn.contains("TraitRemoved<Burning>& cactus_trigger"));
    CHECK(handler_fn.contains("cactus_recipient.has_value()"));
    CHECK(handler_fn.contains("!registry.any_of<Burning>(entity)"));
    CHECK(handler_fn.contains("const auto& old = "));
    CHECK(handler_fn.contains(".old;"));
    CHECK(handler_fn.contains("old.intensity"));
    CHECK(!handler_fn.contains("registry.view<"));
}

TEST_CASE("Codegen EnTT: the startup arrival activation runs after init and before load",
          "[codegen-entt][trait-lifecycle]") {
    const auto program_with = [](const std::string& trigger) {
        ProgramNode program;
        auto decorated = full_pipeline(lifecycle_program_prefix() +
                                           "entity Boss:\n"
                                           "    Enemy\n"
                                           "    Dying\n"
                                           "rule React:\n"
                                           "    each:\n"
                                           "        Enemy\n"
                                           "    on " +
                                           trigger +
                                           ":\n"
                                           "        let x = 1\n"
                                           "rule Boot:\n"
                                           "    each:\n"
                                           "        Enemy\n"
                                           "    on load:\n"
                                           "        let y = 1\n",
                                       program);
        return CppEnttCodegen::generate(decorated);
    };

    SECTION("an on added trigger emits the arrival activation") {
        const auto code    = program_with("added Dying");
        const auto load_fn = generated_function(code, "void generated_load_project(entt::registry& registry)");
        const auto active  = load_fn.find("activation.active = true;");
        const auto arrive  = load_fn.find("generated_lifecycle_trackers().deliver_arrivals(");
        const auto drain   = load_fn.find("generated_drain_event_cascade(registry);");
        const auto commit  = load_fn.find("generated_commit_activation(registry);");
        REQUIRE(active != std::string::npos);
        REQUIRE(arrive != std::string::npos);
        REQUIRE(drain != std::string::npos);
        REQUIRE(commit != std::string::npos);
        CHECK(active < arrive);
        CHECK(arrive < drain);
        CHECK(drain < commit);

        const auto main_start = code.find("int main() try {");
        REQUIRE(main_start != std::string::npos);
        const auto main_fn   = code.substr(main_start, code.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start) - main_start);
        const auto init_call = main_fn.find("generated_init_project(registry);");
        const auto load_call = main_fn.find("generated_load_project(registry);");
        const auto load_evt  = main_fn.find("generated_run_boundary_activation(registry, loadEvent{});");
        REQUIRE(init_call != std::string::npos);
        REQUIRE(load_call != std::string::npos);
        REQUIRE(load_evt != std::string::npos);
        CHECK(init_call < load_call);
        CHECK(load_call < load_evt);

        const auto init_fn = generated_function(code, "void generated_init_project(entt::registry& registry)");
        CHECK(init_fn.contains("generated_connect_lifecycle_trackers(registry);"));
    }

    SECTION("only an on removed trigger emits no arrival activation") {
        const auto code    = program_with("removed Dying");
        const auto load_fn = generated_function(code, "void generated_load_project(entt::registry& registry)");
        CHECK(!load_fn.contains("deliver_arrivals"));
        CHECK(code.contains("LifecycleTrackers<"));
    }
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
    CHECK(
        code.contains("shared_asset_registry().register_mesh(BlueCubeMesh, \"models/blue_cube.mesh\", "
                      "static_cast<int>(BlueCubeMesh));"));
    CHECK(
        code.contains("shared_asset_registry().register_material(BlueCubeMaterial, \"materials/blue_cube.mat\", "
                      "static_cast<int>(BlueCubeMaterial));"));
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
    CHECK(
        code.contains("shared_asset_registry().register_model(Robot, \"art/robot.glb\", "
                      "static_cast<int>(Robot));"));
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
    CHECK(
        code.contains("shared_asset_registry().register_model(Robot, \"examples/robot/art/robot.glb\", "
                      "static_cast<int>(Robot));"));
    CHECK(
        code.contains("shared_asset_registry().register_mesh(PlayerMesh, \"examples/shared/player.mesh\", "
                      "static_cast<int>(PlayerMesh));"));
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
    CHECK(
        code.contains("shared_asset_registry().register_model(SharedModel, \"mods/shared/art/m.glb\", "
                      "static_cast<int>(SharedModel));"));
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
    CHECK(code.contains("create_player"));
    CHECK(code.contains("cactus::runtime::entt_backend::create_entity(registry)"));
    CHECK(code.contains("registry.emplace<Pos>"));
    CHECK(code.contains("registry.emplace<Health>"));
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
    CHECK(unit_fn.contains("component.hp"));
    CHECK(unit_fn.contains("= 4;"));
    CHECK(unit_fn.contains("component.armor"));
    CHECK(unit_fn.contains("= 5;"));
    CHECK(unit_fn.contains("component.speed"));
    CHECK(unit_fn.contains("= 2.0F;"));
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
    CHECK(template_fn.contains("component.hp"));
    CHECK(template_fn.contains("= 3;"));
    CHECK(template_fn.contains("component.speed"));
    CHECK(template_fn.contains("= 2.0F;"));

    const auto spawned_name = extract_temp_name(system_fn, "cactus_gen_spawned_");
    REQUIRE_FALSE(spawned_name.empty());
    const auto existing_name = extract_temp_name(system_fn, "cactus_gen_existing_");
    REQUIRE_FALSE(existing_name.empty());
    const auto override_name = extract_temp_name(system_fn, "cactus_gen_override_value_");
    REQUIRE_FALSE(override_name.empty());

    CHECK(system_fn.contains("auto " + spawned_name + " = create_walker_enemy(registry);"));
    CHECK(system_fn.contains("auto " + existing_name + " = registry.try_get<Health>(" + spawned_name + ");"));
    CHECK(
        system_fn.contains("auto " + override_name + " = " + existing_name + " ? *" + existing_name + " : Health{};"));
    CHECK(system_fn.contains(override_name + ".armor = 7;"));
    CHECK(system_fn.contains("registry.emplace_or_replace<Health>(" + spawned_name + ", " + override_name + ");"));
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

    CHECK(system_fn.contains("auto spawned = ([&]()"));
    CHECK(system_fn.contains("auto " + inner_spawned_name + " = create_walker_enemy(registry);"));
    CHECK(system_fn.contains("auto " + existing_name + " = registry.try_get<Patrol>(" + inner_spawned_name + ");"));
    CHECK(system_fn.contains(override_name + ".speed = 3.0F;"));
    CHECK(system_fn.contains("return " + inner_spawned_name + ";"));
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
        "    each:\n"
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
            CHECK(code.contains("registry.emplace_or_replace<Frozen>(entity)"));
            CHECK(code.contains("registry.emplace_or_replace<Stunned>(entity, " + value_name + ")"));
            CHECK(code.contains("registry.remove<Frozen>(entity)"));
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
            CHECK(code.contains("if (registry.valid(c.other))"));
            CHECK(code.contains("registry.emplace_or_replace<Frozen>(c.other)"));
            CHECK(code.contains("registry.remove<Frozen>(c.other)"));
            CHECK(code.contains("cactus_destroy_entity_recursive(registry, c.other)"));
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
            CHECK(code.contains("if (registry.valid(c.other))"));
            // Buffer identifier is built from the canonical event type name (HitEvent), not
            // the raw source spelling, so cross-module dotted emit names (e.g. "std.debug.Foo")
            // never leak a dot into a C++ identifier.
            CHECK(code.contains("HitEvent_buffer.push_back({.amount = 1})"));
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
            CHECK(code.contains("if (registry.valid(c.other))"));
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
        "    each:\n"
        "        Position as p\n"
        "        Motion as m\n"
        "    on tick:\n"
        "        let sum = p.pos + m.velocity\n"
        "        p.pos += m.velocity\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("auto sum = (p.pos + m.velocity);"));
            CHECK(code.contains("p.pos += m.velocity;"));
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
            CHECK(code.contains("auto " + match_entity_name + " = c.other"));
            CHECK(code.contains("if (registry.valid(" + match_entity_name + "))"));
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
            CHECK(code.contains("auto " + match_entity_name + " = c.other"));
            CHECK(code.contains("auto* b = registry.try_get<Boss>(" + match_entity_name + ")"));
            CHECK(code.contains("registry.all_of<Spike>(" + match_entity_name + ")"));
            CHECK(code.contains("else {"));
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
        "    each:\n"
        "        Pos\n"
        "    on step as s:\n"
        "        x = x + s.dt\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("const stepEvent& s"));
            CHECK(code.contains("Pos_comp.x = (Pos_comp.x + s.dt)"));
        }
    }
}

TEST_CASE("Codegen EnTT: marker event handler takes the marker event parameter", "[codegen-entt][event-handler]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event ping\n"
        "trait Pos:\n"
        "    var x: float\n"
        "rule Init:\n"
        "    each:\n"
        "        Pos\n"
        "    on ping:\n"
        "        x = 0.0\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("void init_ping(entt::registry& registry, const pingEvent& ping,"));
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
            CHECK(code.contains("auto* b = registry.try_get<Boss>(" + match_entity_name + ")"));
            CHECK(!code.contains("else {"));
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
        "    each:\n"
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
            CHECK(code.contains("if (Health_comp.hp > 50) {"));
            CHECK(code.contains("Health_comp.hp = 1;"));
            CHECK(code.contains("if (Health_comp.hp > 0) {"));
            CHECK(code.contains("Health_comp.hp = 2;"));
            CHECK(code.contains("Health_comp.hp = 3;"));

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
        "    each:\n"
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
            CHECK(code.contains("if (Health_comp.hp > 50) {"));
            CHECK(code.contains("Health_comp.hp = 1;"));
            CHECK(code.contains("else if (Health_comp.hp > 0) {"));
            CHECK(code.contains("Health_comp.hp = 2;"));
            CHECK(code.contains("Health_comp.hp = 3;"));

            // Flat cascade, not the doubly-braced legacy shape: exactly one
            // `else if` in the shared handler-body lambda (dedupe-handler-
            // dispatch-codegen: emitted once and invoked from both the
            // targeted-recipient and broadcast dispatch sites). 2 would
            // indicate the doubly-braced `else { if` shape leaking back in.
            CHECK(count_occurrences(code, "else if (") == 1);
            CHECK(code.contains("} else {\n"));
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
        "    each:\n"
        "        Values\n"
        "    on Collision as c:\n"
        "        var sum = 0\n"
        "        var maximum = 0\n"
        "        var count = 0\n"
        "        var cursor = 1\n"
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
    CHECK(!code.contains("auto sum +="));
    CHECK(!code.contains("auto count +="));
    CHECK(!code.contains("auto maximum = value"));
    CHECK(!code.contains("auto cursor = (cursor + 1)"));
    CHECK(code.contains("sum = cactus::num::add(sum, value);"));
    CHECK(code.contains("count = cactus::num::add(count, 1);"));
    CHECK(code.contains("maximum = value;"));
    CHECK(code.contains("cursor = cactus::num::add(cursor, 1);"));
    CHECK(code.contains("sum = cactus::num::add(sum, boss->rank);"));
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
        "    each:\n"
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
        "    each:\n"
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
        "    each:\n"
        "        Sprite as s\n"
        "    order by:\n"
        "        s.layer asc\n"
        "    on tick:\n"
        "        let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            // The comparator sorts a snapshot of the actually-filtered
            // entities (safe to call registry.get<> on for every filter
            // trait), then the anchor pool is permuted to match via EnTT's
            // range-sort overload rather than a comparator invoked over the
            // anchor's whole (potentially wider) pool.
            CHECK(code.contains("std::ranges::stable_sort("));
            CHECK(code.contains("registry.storage<Sprite>().sort_as("));
            // A single-storage view always iterates the sorted pool and has no use<>.
            CHECK(!code.contains("view.use<"));
            // Each key is evaluated by a per-entity extractor lambda rather
            // than being inlined twice into the comparator, so an arbitrary
            // expression lowers through the ordinary expression emitter.
            CHECK(code.contains("= [&](entt::entity entity) {"));
            CHECK(code.contains("return s.layer;"));
            CHECK(code.contains("cactus::num::key_order("));
            CHECK(code.contains("(b), false);"));
            // Equal keys fall back to creation order, not the snapshot's pool order.
            CHECK(code.contains("return registry.get<cactus::runtime::entt_backend::CreationOrdinal>(a).value < "
                                "registry.get<cactus::runtime::entt_backend::CreationOrdinal>(b).value;"));
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
        "    each:\n"
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
            // Lexicographic: the primary key compares ascending and only a tie
            // falls through to the secondary key's descending compare.
            CHECK(code.contains("return s.layer;"));
            CHECK(code.contains("return p.pos.y;"));
            CHECK(code.contains("view.use<"));
            const auto ascending  = code.find("(b), false);");
            const auto descending = code.find("(b), true);");
            CHECK(ascending != std::string::npos);
            CHECK(descending != std::string::npos);
            CHECK(ascending < descending);
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
        "    each:\n"
        "        Sprite\n"
        "    on tick:\n"
        "        let x = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(!code.contains("registry.sort<"));
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
        "    each:\n"
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
        "    each:\n"
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
        "trait Link:\n"
        "    var target: entity_id\n"
        "rule Hierarchy:\n"
        "    each:\n"
        "        Link as link\n"
        "    on tick:\n"
        "        add Parent to link.target:\n"
        "            parent = self\n"
        "        destroy self\n",
        program);

    auto code             = CppEnttCodegen::generate(decorated);
    const auto value_name = extract_temp_name(code, "cactus_gen_value_");
    REQUIRE_FALSE(value_name.empty());
    CHECK(code.contains(value_name + ".parent = entity"));
    CHECK(code.contains("cactus_destroy_entity_recursive(registry, entity)"));
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
        "    each:\n"
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
    CHECK(code.contains("cactus::runtime::entt_backend::propagate_hierarchy("));
    CHECK(code.contains("parent_world.position.x + local.position.x"));
    CHECK(code.contains("cactus::runtime::entt_backend::hierarchy_index<Parent>(registry), PROPAGATION"));
}

TEST_CASE("Codegen EnTT: hierarchy propagation callbacks test world and local presence", "[codegen-entt][hierarchy]") {
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
        "    each:\n"
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
    CHECK(code.contains("cactus::runtime::entt_backend::propagate_hierarchy("));
    CHECK_FALSE(code.contains(".get_parent"));
    CHECK(code.contains("registry.all_of<LocalTransform, WorldTransform>(entity)"));
    CHECK(code.contains(".has_world = [&](entt::entity entity) { return registry.all_of<WorldTransform>(entity); }"));
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
        "    each:\n"
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
    CHECK(code.contains("world.position = Vector3{"));
    CHECK(code.contains("parent_world.position.x + local.position.x"));
    CHECK(code.contains("cactus::runtime::stdlib::math::quat::compose(parent_world.rotation, local.rotation)"));
    CHECK(code.contains("parent_world.scale.z * local.scale.z"));
}

TEST_CASE("Codegen EnTT: stdlib flat colliders emit query helpers and no collision pass",
          "[codegen-entt][stdlib][physics]") {
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
    CHECK(code.contains("registry.view<WorldTransform, Collider, BoxCollider>()"));
    CHECK(!code.contains("cactus_dispatch_stdlib_flat_collisions"));
    CHECK(!code.contains("CollisionEnterEvent"));
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
    CHECK(code.contains("QueryResult2D cactus_query_cast_nearest(entt::registry& registry"));
    CHECK(code.contains("if (delta.x == 0.0F && delta.y == 0.0F)"));
    CHECK(code.contains("if (!cactus_find_flat_collider(registry, subject_entity, subject))"));
    CHECK(code.contains("return cactus_empty_query_result();"));
    CHECK(code.contains("if (!nearest.has_value() || contact->distance < nearest->distance)"));
    CHECK(code.contains("entry > exit || entry < 0.0F || entry > 1.0F"));
    CHECK(code.contains("const float distance = cactus_flat_length(delta) * std::max(0.0F, entry)"));
    CHECK(code.contains("normal.y = delta.y > 0.0F ? -1.0F : 1.0F"));
    CHECK(code.contains("QueryResult2D cactus_query_overlap_deepest(entt::registry& registry"));
    CHECK(code.contains("const float amount = std::abs(contact->overlap.x) + std::abs(contact->overlap.y)"));
    CHECK(code.contains("if (!deepest.has_value() || amount > deepest_amount)"));
    CHECK(code.contains("std::vector<QueryContact2D> cactus_query_overlap_all(entt::registry& registry"));
    CHECK(code.contains("candidate.entity == exclude || !cactus_query_mask_allows(candidate, mask)"));
    CHECK(code.contains("contacts.push_back(*contact)"));
    CHECK(code.contains("return contacts;"));
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
        "    each:\n"
        "        Detector\n"
        "    on tick:\n"
        "        for hit in hits:\n"
        "            emit Damage to hit.victim:\n"
        "                amount = 1\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("auto foreach_snapshot_"));
    CHECK(code.contains("= Detector_comp.hits;"));
    CHECK(code.contains("for ([[maybe_unused]] const auto& hit : foreach_snapshot_"));
    CHECK(code.contains("if (registry.valid(hit.victim))"));
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
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        project DamageFlash:\n"
        "            intensity = 1.0\n"
        "rule Consumer:\n"
        "    each:\n"
        "        Health\n"
        "        DamageFlash as flash\n"
        "    without:\n"
        "        Suppressed\n"
        "    after:\n"
        "        Producer\n"
        "    on tick:\n"
        "        hp = hp - 1\n"
        "trait Suppressed\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    // Tracking storage is runtime-hosted (ProjectedTraitTracker,
    // backends/cpp-entt/runtime.hpp) — codegen only instantiates the tracker.
    CHECK(code.contains("cactus::runtime::entt_backend::ProjectedTraitTracker<DamageFlash> projected_DamageFlash;"));
    const auto projected_name = extract_temp_name(code, "cactus_gen_projected_");
    REQUIRE_FALSE(projected_name.empty());
    CHECK(code.contains("auto& " + projected_name + " = project_DamageFlash(registry, entity)"));
    CHECK(code.contains(projected_name + ".intensity = 1.0F"));
    CHECK(code.contains("registry.view<Health, DamageFlash>(entt::exclude<Suppressed>)"));
    CHECK(code.contains("auto& flash = DamageFlash_comp"));
    CHECK(!code.contains("cactus_projected_DamageFlash"));
    CHECK(!code.contains("cactus_try_get_projected_or_durable_DamageFlash"));
    CHECK(!code.contains("cactus_has_projected_or_durable_Suppressed"));
    CHECK(code.contains("clear_projected_traits(registry);"));
}

TEST_CASE("Codegen EnTT: filtered rules use native views and no early-return guards", "[codegen-entt][project]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "trait Wanted\n"
        "trait Blocked\n"
        "rule Consumer:\n"
        "    each:\n"
        "        Wanted\n"
        "    without:\n"
        "        Blocked\n"
        "    on tick:\n"
        "        let seen = 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("registry.view<Wanted>(entt::exclude<Blocked>)"));
            CHECK(!code.contains("registry.storage<entt::entity>()"));
            CHECK(!code.contains("return;"));
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
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        project Flash:\n"
        "            amount = 2\n"
        "        project Flash:\n"
        "            amount = 3\n"
        "        remove Flash\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    // Remember/restore-on-clear logic is runtime-hosted (ProjectedTraitTracker,
    // backends/cpp-entt/runtime.hpp) and covered directly by
    // test_runtime_projected_trait_tracker.cpp; this codegen-level test only
    // checks the tracker is instantiated and the `remove Flash` statement
    // (a separate, non-projected-trait code path) still lowers as before.
    CHECK(code.contains("cactus::runtime::entt_backend::ProjectedTraitTracker<Flash> projected_Flash;"));
    CHECK(code.contains("projected_Flash.project(registry, entity)"));
    CHECK(code.contains("registry.remove<Flash>(entity)"));
    CHECK(code.contains("cancel_projected_Flash(entity)"));
    CHECK(code.contains("projected_Flash.cancel(entity)"));
}

TEST_CASE("Codegen EnTT: projected marker traits avoid value snapshots", "[codegen-entt][project]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub event tick:\n"
        "    dt: float\n"
        "trait Actor\n"
        "trait Grounded\n"
        "rule Producer:\n"
        "    each:\n"
        "        Actor\n"
        "    on tick:\n"
        "        project Grounded\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    // Tag vs. data-bearing storage selection (std::is_empty_v branch) is
    // runtime-hosted and covered by test_runtime_projected_trait_tracker.cpp;
    // this codegen-level test only checks the tracker is instantiated.
    CHECK(code.contains("cactus::runtime::entt_backend::ProjectedTraitTracker<Grounded> projected_Grounded;"));
    CHECK(code.contains("void project_Grounded(entt::registry& registry, entt::entity entity)"));
    CHECK(code.contains("project_Grounded(registry, entity);"));
    CHECK(!code.contains("registry.try_get<Grounded>"));
    CHECK(!code.contains("std::optional<Grounded>"));
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
        "    each:\n"
        "        Score\n"
        "    on tick:\n"
        "        let s = text.format(\"Score: {}\", value)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("std::format("));
    CHECK(code.contains("\"Score: {}\""));
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
        "    each:\n"
        "        Score\n"
        "    on tick:\n"
        "        let s = text.format(\"Score: {}\", value)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("#include <format>"));
}

TEST_CASE("Codegen EnTT: no std.text import means no format header", "[codegen-entt][std-text-format]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Score:\n"
        "    var value: int\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(!code.contains("#include <format>"));
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
        "    each:\n"
        "        Score\n"
        "    on tick:\n"
        "        let s = format(\"Score: {}\", value)\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("std::format("));
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
    CHECK(code.contains("create_gem1"));
    CHECK(code.contains("emplace<Shape>"));
    CHECK(code.contains("emplace<Collectible>"));
    CHECK(code.contains("emplace<WorldTransform>"));
}

TEST_CASE("Codegen EnTT: flat archetype's non-hinted creation function delegates to its hinted counterpart",
          "[codegen-entt][entity]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "trait Position:\n"
        "    var x: float\n"
        "template Particle:\n"
        "    Position:\n"
        "        x = 1.0\n",
        program);

    const auto code    = CppEnttCodegen::generate(decorated);
    const auto wrapper = generated_function(code, "entt::entity create_particle(entt::registry& registry)");
    CHECK(wrapper ==
          "entt::entity create_particle(entt::registry& registry) {\n"
          "    return create_particle_at(registry, cactus::runtime::entt_backend::create_entity(registry));\n"
          "}\n");
    // Not a second copy of create_particle_at's field-initialization statements.
    CHECK(!wrapper.contains("component"));
    CHECK(!wrapper.contains("registry.emplace<Position>"));
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
    // A, B, C should be filled in that order in the init function
    const auto pos_a = code.find("create_a_at(registry, generated_named_slots().test__A)");
    const auto pos_b = code.find("create_b_at(registry, generated_named_slots().test__B)");
    const auto pos_c = code.find("create_c_at(registry, generated_named_slots().test__C)");
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
        "    each:\n"
        "        EditorState\n"
        "    on tick:\n"
        "        reads:\n"
        "            EditorState\n"
        "        effects:\n"
        "            editor\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    // EditorState should have a component struct
    CHECK(code.contains("struct EditorState"));
    // Editor entity should be created
    CHECK(code.contains("create_editor"));
    // EditorSelected marker trait should be registered
    CHECK(code.contains("struct EditorSelected"));
    // EditorSnap should have fields
    CHECK(code.contains("struct EditorSnap"));
    // EditorSelectionChanged event should have a struct
    CHECK(code.contains("struct EditorSelectionChanged"));
    // EditorPropertyPanel extern rule stub (snake_case name)
    CHECK(code.contains("editor_property_panel_tick"));
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
    CHECK(init.contains("register_editor_active_mode_impl("));
    CHECK(init.contains("reg.view<EditorState>()"));
    CHECK(init.contains("-> int {"));
    CHECK(init.contains("static_cast<int>(view.get<EditorState>(entity).mode)"));
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
    CHECK(
        code.contains("std::vector<std::string> editor_template_names() { return "
                      "cactus_template_registry_order; }"));
    CHECK(code.contains("int editor_template_index(const std::string& name) noexcept {"));
    CHECK(code.contains("return -1;"));
}

// The palette stores every factory as entt::entity(*)(entt::registry&), which a
// parameterized template's create_<name> cannot convert to; failing here beats
// emitting a translation unit that does not compile.
TEST_CASE("Codegen EnTT: editor palette rejects a pub template that takes parameters",
          "[codegen-entt][stdlib][editor][template-parameters]") {
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
        "pub template Box(origin: vec2):\n"
        "    Position:\n"
        "        value = origin\n"
        "pub extern func template_names() list[string]\n",
        program);

    CHECK_THROWS_AS(CppEnttCodegen::generate(decorated), std::runtime_error);
}

// A parameterless pub template stays registrable, so the check above must not
// fire on a template whose slots all carry bound values.
TEST_CASE("Codegen EnTT: editor palette still accepts parameterless pub templates",
          "[codegen-entt][stdlib][editor][template-parameters]") {
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
        "pub extern func template_names() list[string]\n",
        program);

    CHECK_NOTHROW(CppEnttCodegen::generate(decorated));
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
        "    each:\n"
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
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_active_mode(registry)"));
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_template_index(\"Box\")"));
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_screen_size()"));
    // Only active_mode reads ECS state (the EditorState singleton); the others read
    // codegen-emitted globals or raw window state and take no registry argument.
    CHECK(!rule.contains("editor_template_index(registry,"));
    CHECK(!rule.contains("editor_screen_size(registry)"));
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
        "    each:\n"
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
    CHECK(rule.contains("static_cast<GizmoMode>(cactus::runtime::entt_backend::editor_active_mode(registry))"));
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
        "    each:\n"
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
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_mode_label(static_cast<int>("));
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
        "    each:\n"
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
    CHECK(tick.contains("editor_is_active(registry)"));
    // Mode drives both the projection and the mode-specific branches, not a hardcoded literal.
    CHECK(tick.contains("editor_active_mode(registry)"));
    CHECK(tick.contains("project_EditorGizmo2D(registry, entity)"));
    CHECK(tick.contains(".mode = mode;"));
    // Always-drawn AABB outline, independent of mode.
    CHECK(tick.contains("DrawDebugRectOutline2DEvent_buffer.push_back("));
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
        "    each:\n"
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
    CHECK(tick.contains("visible = false;"));
    // Active branch: index comes from template_index(name), not a loop-local counter — bounded
    // `for` can't mutate an outer-scope var across iterations.
    CHECK(tick.contains("editor_template_index(name)"));
    // Color comes from the palette_color() extern func, not a DSL if-chain — `if`/`for` bodies
    // can't mutate an outer-scope local either (same root cause), so a computed-then-conditionally
    // -overridden local would silently keep its first value.
    CHECK(tick.contains("editor_palette_color(idx)"));
    CHECK(tick.contains("DrawScreenRectEvent_buffer.push_back("));
    // Click hit-testing and the EditorState writes happen directly in this rule (self bound to
    // EditorState), writing the real component fields, not a generic renderer.
    CHECK(tick.contains(".active_template = name;"));
    CHECK(tick.contains(".mode = GizmoMode::Place;"));
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
        "    each:\n"
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
    CHECK(render.contains("editor_screen_size()"));
    CHECK(render.contains("editor_mode_label("));
    CHECK(render.contains("visible = true;"));
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
        "    each:\n"
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

    // Should generate a projected trait tracker for EditorGizmo2D
    CHECK(code.contains("project_EditorGizmo2D"));
    CHECK(
        code.contains("cactus::runtime::entt_backend::ProjectedTraitTracker<EditorGizmo2D> projected_EditorGizmo2D;"));
    // Should generate a projected trait tracker for EditorGizmo3D
    CHECK(code.contains("project_EditorGizmo3D"));
    CHECK(
        code.contains("cactus::runtime::entt_backend::ProjectedTraitTracker<EditorGizmo3D> projected_EditorGizmo3D;"));
    // Should have the general projected trait cleanup call
    CHECK(code.contains("clear_projected_traits(registry)"));
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
        "    each:\n"
        "        EditorState\n"
        "    on tick:\n"
        "        reads:\n"
        "            EditorState\n"
        "        effects:\n"
        "            editor\n",
        program);

    auto code = CppEnttCodegen::generate(decorated);

    // EditorPropertyPanel should have a tick call (snake_case name)
    CHECK(code.contains("editor_property_panel_tick"));
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
        "    each:\n"
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
    CHECK(tick.contains("editor_is_active(registry)"));
    // Mode drives both the projection and the mode-specific branches, not a hardcoded literal.
    CHECK(tick.contains("editor_active_mode(registry)"));
    CHECK(tick.contains("project_EditorGizmo3D(registry, entity)"));
    CHECK(tick.contains(".mode = mode;"));
    // Always-drawn wire box, independent of mode.
    CHECK(tick.contains("DrawDebugWireBox3DEvent_buffer.push_back("));
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
    CHECK(!render_project.contains("editor_gizmo_renderer3_d_render(registry)"));
    const auto update_project = generated_function(code, "void generated_update_project");
    CHECK(!update_project.contains("editor_gizmo_renderer3_d_render"));
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
    CHECK(code.contains("register_editor_spawn_impl"));
    CHECK(code.contains("lt->position = pos3d"));
    CHECK(code.contains("wt->position = pos3d"));
    CHECK(!code.contains("wt->position = pos2d"));

    // Raycast impl tests scaled/translated model bounds and skips locked entities
    CHECK(code.contains("register_editor_raycast_impl"));
    CHECK(code.contains("model_bounds_box"));
    CHECK(code.contains("GetRayCollisionBox"));
    CHECK(code.contains("reg.view<WorldTransform, ModelRenderer>(entt::exclude<EditorLocked>)"));
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

    CHECK(code.contains("register_editor_spawn_impl"));
    CHECK(code.contains("lt->position = pos2d"));
    CHECK(code.contains("wt->position = pos2d"));
    CHECK(!code.contains("pos3d;"));
    CHECK(!code.contains("register_editor_raycast_impl"));
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
    CHECK(code.contains("register_editor_spawn_impl"));
    CHECK(code.contains("wt->position = pos3d"));
    CHECK(!code.contains("lt->position"));
    CHECK(!code.contains("register_editor_raycast_impl"));
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
    CHECK(code.contains("registry.view<Viewport>()"));
    CHECK(code.contains("BeginScissorMode"));
    CHECK(code.contains("EndScissorMode"));
    CHECK(code.contains("std::ranges::sort"));
    CHECK(code.contains("__vp.active"));
    CHECK(code.contains("__vp.depth"));
    CHECK(code.contains("if (__vp.clear) { cactus::runtime::raylib::ClearBackground"));

    // 2D camera set per viewport via translate helper
    CHECK(code.contains("__translate_camera_2d"));
    CHECK(code.contains("set_active_camera_2d"));

    // No legacy camera-sync block in generated_update_project
    CHECK(!code.contains("__cam.active"));
    CHECK(!code.contains("registry.view<Camera>()"));

    // #include <algorithm> is present for std::sort
    CHECK(code.contains("#include <algorithm>"));
}

TEST_CASE("Codegen EnTT: viewport loop sorts by depth (lower depth first)", "[codegen-entt][camera][viewport]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string("use std.camera.viewport\n") + kViewportTrait, program);

    const auto code = CppEnttCodegen::generate(decorated);

    // Depth-ordered collection and sort
    CHECK(code.contains("static std::vector<std::pair<int,entt::entity>> __vps"));
    CHECK(code.contains("__vps.emplace_back(__vp.depth, __vp_e)"));
    CHECK(code.contains("std::ranges::sort(__vps)"));
}

TEST_CASE("Codegen EnTT: viewport loop emits clear and no-clear paths", "[codegen-entt][camera][viewport]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string("use std.camera.viewport\n") + kViewportTrait, program);

    const auto code = CppEnttCodegen::generate(decorated);

    // clear path: conditional ClearBackground per viewport
    CHECK(code.contains("if (__vp.clear) { cactus::runtime::raylib::ClearBackground(__vp.clear_color); }"));
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
    CHECK(
        code.contains("BeginScissorMode(\n"
                      "                static_cast<int>(__vp.x * static_cast<float>(__sw))"));
    CHECK(code.contains("static_cast<int>(__vp.width * static_cast<float>(__sw))"));
    CHECK(code.contains("static_cast<int>(__vp.height * static_cast<float>(__sh))"));

    // Per-viewport camera is set with the entity's Camera component
    CHECK(code.contains("registry.all_of<Camera>(__vp_ent)"));
    CHECK(code.contains("__translate_camera_2d(__cam, __sw, __sh)"));
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
    CHECK(!code.contains("__cam.active"));
    // Camera struct is still generated
    CHECK(code.contains("struct Camera"));
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
    CHECK(code.contains("registry.view<Boss>()"));
    CHECK(code.contains("view.begin() != view.end()"));
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
    CHECK(code.contains("registry.view<Enemy>(entt::exclude<Dead>)"));
}

TEST_CASE("Codegen EnTT: query.count lowers to counting loop", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "rule S:\n"
                                       "    on tick:\n"
                                       "        let n = query.count[Enemy, not Dead]()\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("registry.view<Enemy>(entt::exclude<Dead>)"));
    CHECK(code.contains("std::ranges::distance"));
}

TEST_CASE("Codegen EnTT: query.first lowers to an oldest-match scan", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "rule S:\n"
                                       "    on tick:\n"
                                       "        let t = query.first[Boss]()\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::oldest(registry, registry.view<Boss>())"));
}

TEST_CASE("Codegen EnTT: query.all lowers to a creation-ordered snapshot", "[codegen-entt][query]") {
    ProgramNode program;
    auto decorated = full_pipeline(std::string(kQueryPreamble) +
                                       "rule S:\n"
                                       "    on tick:\n"
                                       "        let all = query.all[Enemy]()\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("cactus::runtime::entt_backend::creation_ordered(registry, registry.view<Enemy>())"));
}

TEST_CASE("Codegen EnTT: query.children lowers to the runtime children walk", "[codegen-entt][query][hierarchy]") {
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
        "    each:\n"
        "        Selection\n"
        "    on tick:\n"
        "        let snapshot = query.children[Node, not Hidden](of = parent)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const std::string call =
        "cactus::runtime::entt_backend::hierarchy_children(registry, "
        "cactus::runtime::entt_backend::hierarchy_index<Parent>(registry), (Selection_comp.parent), "
        "registry.view<Parent, Node>(entt::exclude<Hidden>))";
    CHECK(code.contains(call));
    // The handler body is shared by both dispatch sites, so the call appears once.
    CHECK(count_occurrences(code, call) == 1);
}

TEST_CASE("Codegen EnTT: hierarchy traversals lower to the runtime forest walk", "[codegen-entt][query][hierarchy]") {
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

    const auto code         = CppEnttCodegen::generate(decorated);
    const std::string index = "cactus::runtime::entt_backend::hierarchy_index<Parent>(registry)";
    CHECK(code.contains("cactus::runtime::entt_backend::hierarchy_order(registry, " + index +
                        ", registry.view<Node>(entt::exclude<Hidden>), false)"));
    CHECK(code.contains("cactus::runtime::entt_backend::hierarchy_order(registry, " + index +
                        ", registry.view<Node>(entt::exclude<Hidden>), true)"));
    CHECK_FALSE(code.contains("visited.insert"));
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
        "    each:\n"
        "        Child\n"
        "    on tick:\n"
        "        let p = query.parent(of = child_id)\n",
        program);

    const auto code                  = CppEnttCodegen::generate(decorated);
    const auto parent_component_name = extract_temp_name(code, "cactus_gen_parent_component_");
    REQUIRE_FALSE(parent_component_name.empty());
    CHECK(code.contains("registry.try_get<Parent>"));
    CHECK(code.contains(parent_component_name + "->parent"));
    CHECK(code.contains("entt::entity{entt::null}"));
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
    CHECK(code.contains("cactus::runtime::entt_backend::oldest(registry, registry.view<Boss>())"));
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
    CHECK(code.contains("registry.view<WorldTransform, Enemy>()"));
    // call site delegates the search to the shared runtime helper instead of an inline loop
    CHECK(code.contains("cactus::runtime::entt_backend::query_nearest("));
    CHECK(code.contains("registry.get<WorldTransform>(e).position"));
    // no reserved double-leading-underscore locals at the call site
    CHECK(!code.contains("__best"));
    CHECK(!code.contains("__from"));
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
    CHECK(code.contains("registry.view<WorldTransform, Pickup>(entt::exclude<Collected>)"));
    CHECK(code.contains("cactus::runtime::entt_backend::query_overlap_box("));
    CHECK(code.contains("registry.get<WorldTransform>(e).position"));
    CHECK(!code.contains("__ct"));
    CHECK(!code.contains("__sz"));
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
    CHECK(code.contains("registry.view<WorldTransform, Enemy>()"));
    CHECK(code.contains("cactus::runtime::entt_backend::query_overlap_circle("));
    CHECK(code.contains("registry.get<WorldTransform>(e).position"));
    CHECK(!code.contains("__rad"));
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
    CHECK(code.contains("registry.view<WorldTransform, Wall>()"));
    CHECK(code.contains("cactus::runtime::entt_backend::query_raycast("));
    CHECK(code.contains("registry.get<WorldTransform>(e).position"));
    CHECK(!code.contains("__proj"));
    CHECK(!code.contains("__best"));
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
    CHECK(code.contains("registry.view<WorldTransform, Enemy>()"));
    CHECK(code.contains("cactus::runtime::entt_backend::query_nearest("));
    CHECK(code.contains("registry.get<WorldTransform>(e).position"));
    CHECK(!code.contains("__dz"));
    CHECK(!code.contains("__best"));
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
    CHECK(code.contains("registry.view<WorldTransform, Enemy>()"));
    CHECK(code.contains("cactus::runtime::entt_backend::query_overlap_sphere("));
    CHECK(code.contains("registry.get<WorldTransform>(e).position"));
    CHECK(!code.contains("__dz"));
    CHECK(!code.contains("__ct"));
}

// ── Hierarchical entity templates (dsl-hierarchical-entity-templates) ───────

static const std::string& hierarchy_source_prefix() {
    static const std::string value =
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
    return value;
}

TEST_CASE("Codegen EnTT: hierarchical template emits per-node helpers and canonical wrapper",
          "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(hierarchy_source_prefix(), program);
    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.contains("static entt::entity create_rig__node(entt::registry& registry)"));
    CHECK(code.contains("static entt::entity create_rig__node__socket(entt::registry& registry)"));
    CHECK(code.contains("static entt::entity create_rig__node__socket__gem(entt::registry& registry)"));

    const auto wrapper = generated_function(code, "entt::entity create_rig(entt::registry& registry)");
    CHECK(wrapper.contains("auto entity = create_rig__node(registry);"));
    CHECK(wrapper.contains("return entity;"));
}

TEST_CASE(
    "Codegen EnTT: hierarchical-root archetype's non-hinted node function delegates to its hinted "
    "counterpart",
    "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(hierarchy_source_prefix(), program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto wrapper = generated_function(code, "static entt::entity create_rig__node(entt::registry& registry)");
    CHECK(wrapper ==
          "static entt::entity create_rig__node(entt::registry& registry) {\n"
          "    return create_rig__node_at(registry, cactus::runtime::entt_backend::create_entity(registry));\n"
          "}\n");
    // Not a second copy of create_rig__node_at's field-initialization statements.
    CHECK(!wrapper.contains("component"));
    CHECK(!wrapper.contains("registry.emplace<Tag>"));
}

TEST_CASE("Codegen EnTT: hierarchical creation assigns Parent to the immediate parent", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(hierarchy_source_prefix(), program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto wrapper = generated_function(code, "entt::entity create_rig(entt::registry& registry)");
    // Direct child points at the root; grandchild points at the child, not the root.
    CHECK(wrapper.contains("registry.emplace_or_replace<Parent>(child_0, Parent{.parent = entity});"));
    CHECK(wrapper.contains("registry.emplace_or_replace<Parent>(child_0_0, Parent{.parent = child_0});"));
    // The root never receives a generated Parent relation.
    CHECK(!wrapper.contains("Parent>(entity"));
}

TEST_CASE("Codegen EnTT: hierarchical creation is parent-first preorder", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(hierarchy_source_prefix() +
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
    auto decorated  = full_pipeline(hierarchy_source_prefix() +
                                        "rule S:\n"
                                        "    on tick:\n"
                                        "        let root = spawn Rig:\n"
                                        "            Tag:\n"
                                        "                value = 3\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto spawned_name = extract_temp_name(code, "cactus_gen_spawned_");
    REQUIRE_FALSE(spawned_name.empty());
    CHECK(code.contains("auto " + spawned_name + " = create_rig(registry);"));
    CHECK(code.contains("return " + spawned_name + ";"));
}

TEST_CASE("Codegen EnTT: spawn with child overrides expands inline per node", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(hierarchy_source_prefix() +
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
    CHECK(code.contains("auto " + spawned_name + " = create_rig__node(registry);"));
    CHECK(code.contains("auto child_0 = create_rig__node__socket(registry);"));
    CHECK(code.contains("registry.emplace_or_replace<Parent>(child_0, Parent{.parent = " + spawned_name + "});"));
    CHECK(code.contains("auto child_0_0 = create_rig__node__socket__gem(registry);"));
    CHECK(code.contains("registry.emplace_or_replace<Parent>(child_0_0, Parent{.parent = child_0});"));
    // Per-node overrides applied to the matching created entity.
    CHECK(code.contains("registry.try_get<Tag>(child_0)"));
    CHECK(code.contains("registry.try_get<Growth>(child_0_0)"));
    CHECK(code.contains("return " + spawned_name + ";"));
}

TEST_CASE("Codegen EnTT: hierarchical load-time entity creates descendants in setup", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(hierarchy_source_prefix() +
                                        "entity Rig1 from Rig:\n"
                                        "    Tag:\n"
                                        "        value = 5\n"
                                        "    children:\n"
                                        "        Socket:\n"
                                        "            Tag:\n"
                                        "                value = 7\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    // generated_init_project fills the pre-allocated root through the entity wrapper, which expands the tree.
    CHECK(code.contains("create_rig1_at(registry, generated_named_slots().test__Rig1);"));
    const auto at_wrapper = generated_function(code, "entt::entity create_rig1_at(entt::registry& registry");
    CHECK(at_wrapper.contains("create_rig1__node__socket(registry)"));
    const auto wrapper = generated_function(code, "entt::entity create_rig1(entt::registry& registry)");
    CHECK(wrapper.contains("create_rig1__node__socket(registry)"));
    CHECK(wrapper.contains("registry.emplace_or_replace<Parent>(child_0, Parent{.parent = entity});"));
    // Child override value is baked into the per-node helper.
    const auto socket_fn = generated_function(code, "entt::entity create_rig1__node__socket(entt::registry&");
    CHECK(socket_fn.contains("component.value = 7"));
}

TEST_CASE("Codegen EnTT: destroy cascade code coexists with hierarchical creation", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    auto decorated  = full_pipeline(hierarchy_source_prefix() +
                                        "rule S:\n"
                                        "    each:\n"
                                        "        Tag\n"
                                        "    on tick:\n"
                                        "        destroy self\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    // The existing Parent-based recursive destroy support keys off the Parent
    // trait, which hierarchical templates rely on unchanged.
    CHECK(code.contains("cactus::runtime::entt_backend::hierarchy_index<Parent>(registry), entity);"));
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
    CHECK(code.contains("KEY_LEFT_SHIFT"));
    CHECK(code.contains("KEY_EQUAL"));
    CHECK(code.contains("KEY_MINUS"));
    CHECK(code.contains("KEY_F"));
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
        "    each:\n"
        "        Rig\n"
        "    on tick:\n"
        "        let _w = input.wheel_delta()\n"
        "        let _d = input.mouse_delta()\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void nav_poll_tick");
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_wheel_delta()"));
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_mouse_delta_screen()"));
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
        "    each:\n"
        "        EditorSt\n"
        "    on input:\n"
        "        input.consume(NavDrag)\n",
        program,
        std_input_imports());

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void editor_input_consume_input");
    // The call must pass K_NAV_DRAG (the generated enum constant) to editor_consume.
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_consume(K_NAV_DRAG)"));
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
    CHECK(init.contains("register_editor_camera_enter_impl"));
    CHECK(init.contains("register_editor_camera_exit_impl"));

    // Enter impl captures 2D pose and spawns rig with EditorCamera2D
    CHECK(init.contains("EditorCamera2D{.view_center"));
    CHECK(init.contains(".min_zoom = 0.05F"));
    CHECK(init.contains(".max_zoom = 20.0F"));
    CHECK(init.contains("set_editor_saved_viewports"));

    // Exit impl destroys rig and restores viewports
    CHECK(init.contains("release_entity(reg, rig)"));
    CHECK(init.contains("__vp->active = true"));

    // 2D apply impl writes zoom and offset back to rig Camera
    CHECK(init.contains("register_editor_apply_camera_2d_impl"));
    CHECK(init.contains("__cam->zoom = zoom"));
    CHECK(init.contains("__cam->offset = view_center"));

    // No 3D impls without uses_volume
    CHECK(!init.contains("register_editor_apply_camera_3d_impl"));
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
    CHECK(init.contains("register_editor_camera_enter_impl"));
    CHECK(init.contains("cactus::runtime::stdlib::math::atan2(-__dx, -__dz)"));  // yaw derivation
    CHECK(init.contains("cactus::runtime::stdlib::math::atan2(__sin_pitch, "));   // pitch derivation
    CHECK(init.contains("EditorCamera3D{.focus"));
    CHECK(init.contains(".orbit_speed = 0.005F"));
    CHECK(init.contains(".min_pitch = -1.5F"));
    CHECK(init.contains(".max_pitch = 1.5F"));
    CHECK(init.contains(".min_distance = 0.1F"));
    CHECK(init.contains(".max_distance = 1000.0F"));

    // Exit impl destroys rig
    CHECK(init.contains("register_editor_camera_exit_impl"));
    CHECK(init.contains("release_entity(reg, rig)"));

    // 3D apply impl writes position and rotation to rig WorldTransform (5.5 clamping driven by these writes)
    CHECK(init.contains("register_editor_apply_camera_3d_impl"));
    CHECK(init.contains("__wt->position = position"));
    CHECK(init.contains("__wt->rotation = rotation"));

    // Entity position impl returns WorldTransform.position
    CHECK(init.contains("register_editor_entity_position_3d_impl"));
    CHECK(init.contains("return __wt->position"));

    // No 2D-specific impls without uses_flat
    CHECK(!init.contains("register_editor_apply_camera_2d_impl"));
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
        "    each:\n"
        "        Scene\n"
        "    on tick:\n"
        "        let _wp = camera2d.screen_to_world(vec2(0.0, 0.0))\n"
        "        let _wd = camera2d.screen_delta_to_world(vec2(1.0, 0.0))\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void project2_d_tick");
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_screen_to_world_2d("));
    CHECK(rule.contains("cactus::runtime::entt_backend::screen_delta_to_world_2d("));
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
        "    each:\n"
        "        Selection\n"
        "    on tick:\n"
        "        let _pos = transform2d.world_position(sel)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void get_pos_tick");
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_entity_position_2d(registry,"));
}

TEST_CASE("Codegen EnTT: std.transform.flat world_position registers live-position impl without std.editor",
          "[codegen-entt][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "use std.transform.flat as transform2d\n"
        "pub event tick\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "trait Selection:\n"
        "    var sel: entity_id\n"
        "rule GetPos:\n"
        "    each:\n"
        "        Selection\n"
        "    on tick:\n"
        "        let _pos = transform2d.world_position(sel)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto init = generated_function(code, "void generated_init_project");
    CHECK(init.contains("register_editor_entity_position_2d_impl"));
    CHECK(init.contains("return Vector2{.x = __wt->position.x, .y = __wt->position.y}"));
}

TEST_CASE("Codegen EnTT: std.transform.volume world_position registers live-position impl without std.editor",
          "[codegen-entt][stdlib]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        std::string("use std.transform.volume as transform3d\n"
                    "pub event tick\n") +
            kWorldTransformVolumeTrait +
            "trait Selection:\n"
            "    var sel: entity_id\n"
            "rule GetPos:\n"
            "    each:\n"
            "        Selection\n"
            "    on tick:\n"
            "        let _pos = transform3d.world_position(sel)\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    const auto init = generated_function(code, "void generated_init_project");
    CHECK(init.contains("register_editor_entity_position_3d_impl"));
    CHECK(init.contains("return __wt->position"));
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
        "    each:\n"
        "        Placer\n"
        "    on tick:\n"
        "        result = spawn_template(\"Enemy\", vec2(0.0, 0.0), vec3(0.0, 0.0, 0.0))\n",
        program);

    decorated.funcs["spawn_template"].is_stdlib   = true;
    decorated.funcs["spawn_template"].module_name = "std.editor";

    const auto code = CppEnttCodegen::generate(decorated);
    const auto rule = generated_function(code, "void place_test_tick");
    CHECK(rule.contains("cactus::runtime::entt_backend::editor_spawn_template(registry,"));
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

    CHECK(flat_code.contains("struct std_transform_flat__WorldTransform"));
    CHECK(vol_code.contains("struct std_transform_volume__WorldTransform"));
    CHECK(!flat_code.contains("struct WorldTransform "));
    CHECK(!vol_code.contains("struct WorldTransform "));
}

TEST_CASE("Codegen EnTT: user-defined trait with explicit module_name produces canonical C++ type",
          "[codegen-entt][canonical-identity]") {
    ResolvedTrait trait;
    trait.name        = "WorldTransform";
    trait.module_name = "game.transforms";
    trait.fields.push_back({.name = "position", .type = {.kind = TypeKind::Vec2, .name = "vec2"}, .is_var = true});

    const auto code = EnttComponentEmitter::emit_component(trait);
    CHECK(code.contains("struct game_transforms__WorldTransform"));
    CHECK(!code.contains("struct WorldTransform"));
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
        "    each:\n"
        "        Velocity\n"
        "    on tick:\n"
        "        vx = vx + tick.dt\n",
        program);
    decorated.module_name = "my.game";

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("void my_game__move_tick("));
    CHECK(!code.contains("void move_tick("));
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
        "    each:\n"
        "        Position\n"
        "    on tick:\n"
        "        x = x + tick.dt\n",
        program);
    decorated.traits.at("Position").module_name = "my.game";

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            const auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("registry.view<my_game__Position>()"));
            CHECK(code.contains("my_game__Position& my_game__Position_comp"));
            CHECK(code.contains("my_game__Position_comp.x"));
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
        "    each:\n"
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
    CHECK(code.contains("cactus::runtime::stdlib::math::lerp(0.0F, 10.0F, 0.5F)"));
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
        "    each:\n"
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
    CHECK(tick.contains("registry.view<std_transform_flat__WorldTransform, std_render_sprites__Renderer>()"));
    CHECK(tick.contains("std_transform_flat__WorldTransform_comp.position"));
    CHECK(tick.contains("std_render_sprites__Renderer_comp.layer"));
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
        CHECK(std::string(e.what()).contains("WorldTransform"));
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
        "    each:\n"
        "        Marker\n"
        "    on input:\n"
        "        value = value + 1\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.contains("#include <deque>"));
    CHECK(code.contains("#include <functional>"));
    CHECK(code.contains("#include <variant>"));
    CHECK(code.contains("using EventOccurrence = std::variant<ContactEvent, frameEvent>;"));
    CHECK(code.contains("void generated_inject_external_event(frameEvent occurrence)"));
    CHECK(!code.contains("void generated_inject_external_event(ContactEvent occurrence)"));

    // QueuedEvent<Occurrence>/ActivationRuntime<Occurrence> are runtime-hosted
    // (backends/cpp-entt/runtime.hpp, covered directly by
    // test_runtime_activation_scheduler.cpp) — codegen only instantiates
    // ActivationRuntime<EventOccurrence> as SchedulerState's activation field,
    // it doesn't redefine either struct's body inline.
    CHECK(!code.contains("struct QueuedEvent"));
    CHECK(!code.contains("struct ActivationRuntime"));
    CHECK(!code.contains("enum class Kind : std::uint8_t { Spawn, Destroy, Add, Remove };"));
    CHECK(code.contains("ActivationRuntime<EventOccurrence> activation;"));

    CHECK(code.contains("struct game_scheduler__inputPhaseRuntimeState"));
    CHECK(code.contains("struct game_scheduler__fixed_tickPhaseRuntimeState"));
    CHECK(code.contains("double accumulator{};"));
    CHECK(code.contains("double alpha{};"));
    CHECK(code.contains("std::uint64_t completed_batches{};"));
    CHECK(code.contains("float dt{};"));
    CHECK(code.contains("game_scheduler__inputPhaseRuntimeState game_scheduler__input;"));
    CHECK(code.contains("game_scheduler__fixed_tickPhaseRuntimeState game_scheduler__fixed_tick;"));

    const auto input_batch = code.find("void generated_run_phase_batch_game_scheduler__input");
    const auto fixed_batch = code.find("void generated_run_phase_batch_game_scheduler__fixed_tick");
    REQUIRE(input_batch != std::string::npos);
    REQUIRE(fixed_batch != std::string::npos);
    CHECK(input_batch < fixed_batch);
    CHECK(code.contains("phase.accumulator += static_cast<double>(root_event.dt);"));
    CHECK(code.contains("const auto due = static_cast<std::uint64_t>(std::floor(phase.accumulator / interval));"));
    CHECK(code.contains("constexpr std::uint64_t max_repetitions = 2;"));
    CHECK(code.contains("const auto repetitions = std::min(due, max_repetitions);"));
    CHECK(code.contains("phase.dt = static_cast<float>(interval);"));
    CHECK(code.contains("phase.accumulator -= static_cast<double>(due) * interval;"));
    CHECK(code.contains("phase.alpha = phase.accumulator / interval;"));
    CHECK(code.contains("++phase.completed_batches;"));

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
    CHECK(code.contains("void generated_drain_external_events(entt::registry& registry)"));

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
    CHECK(!graph_driven_code.contains("begin_render_frame()"));
    CHECK(!graph_driven_code.contains("end_render_frame()"));
    CHECK(!graph_driven_code.contains("clear_projected_traits(registry);"));

    const auto main_start = code.find("int main()");
    const auto main_end   = code.find("#endif  // CACTUS_GENERATED_NO_MAIN", main_start);
    REQUIRE(main_start != std::string::npos);
    REQUIRE(main_end != std::string::npos);
    const auto main = code.substr(main_start, main_end - main_start);
    CHECK(main.contains("const float dt = GetFrameTime();"));
    CHECK(main.contains("generated_inject_external_event(frameEvent{.dt = dt});"));
    CHECK(main.contains("generated_drain_external_events(registry);"));
    CHECK(!main.contains("generated_update_project"));
    CHECK(!main.contains("generated_render_project"));
    CHECK(main.find("generated_inject_external_event(frameEvent{.dt = dt});") ==
          main.rfind("generated_inject_external_event(frameEvent{.dt = dt});"));
    CHECK(code.contains("#ifndef CACTUS_GENERATED_NO_MAIN"));

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
    CHECK(!legacy_code.contains("Graph Activation Runtime State"));
    CHECK(!legacy_code.contains("SchedulerState"));
    CHECK(!legacy_code.contains("#include <variant>"));
    const auto legacy_main_start = legacy_code.find("int main()");
    const auto legacy_main_end   = legacy_code.find("#endif  // CACTUS_GENERATED_NO_MAIN", legacy_main_start);
    REQUIRE(legacy_main_start != std::string::npos);
    REQUIRE(legacy_main_end != std::string::npos);
    const auto legacy_main = legacy_code.substr(legacy_main_start, legacy_main_end - legacy_main_start);
    CHECK(legacy_main.contains("generated_update_project(registry, dispatcher, dt);"));
    CHECK(legacy_main.contains("generated_render_project(registry, dispatcher);"));
    const auto legacy_update = generated_function(legacy_code, "void generated_update_project");
    CHECK(!legacy_update.contains("legacy_tick"));
    CHECK(!legacy_code.contains("struct TickEvent"));
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
    CHECK(!tick_batch_body.contains("(void)root_event;"));

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
    CHECK(fixed_batch_body.contains("phase.dt = static_cast<float>(interval);"));
    CHECK(fixed_batch_body.contains("phase.alpha = phase.accumulator / interval;"));
    CHECK(!fixed_batch_body.contains("phase.dt = static_cast<float>(root_event.dt);"));

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
    CHECK(!fixed_batch_body.contains("begin_render_frame()"));
    CHECK(!fixed_batch_body.contains("end_render_frame()"));
    const auto tick_batch_body_full = code.substr(tick_batch, fixed_batch_decl - tick_batch);
    CHECK(!tick_batch_body_full.contains("begin_render_frame()"));
    CHECK(!tick_batch_body_full.contains("end_render_frame()"));
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
        "    each:\n"
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
        code.find("void generated_process_root_event(entt::registry& registry, const frameEvent& root_event,");
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
    CHECK(!input_batch.contains("begin_render_frame()"));
    CHECK(!input_batch.contains("end_render_frame()"));
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
    // Queueing is runtime-hosted (emit_event, backends/cpp-entt/runtime.hpp,
    // covered directly by test_runtime_activation_scheduler.cpp); codegen
    // only declares SchedulerState's ActivationRuntime<EventOccurrence> field
    // and routes handler-emitted events through the runtime helper by call name.
    CHECK(code.contains("ActivationRuntime<EventOccurrence> activation;"));
    CHECK(code.contains("generated_emit_event(ContactEvent{.amount = 1});"));
    CHECK(!code.contains("Contact.amount"));
    const auto first_contact = generated_function(code, "void first_contact_Contact(");
    CHECK(first_contact.contains(
        "generated_emit_event(ReactionEvent{.amount = " + handler_payload_param(first_contact) + ".amount});"));
    CHECK(code.contains("generated_drain_event_cascade(registry);"));
    CHECK(code.contains("generated_dispatch_event(reg, occurrence, target);"));

    const auto contact_dispatch =
        code.find("void generated_dispatch_event(entt::registry& registry, const ContactEvent& occurrence,");
    REQUIRE(contact_dispatch != std::string::npos);
    const auto first_call  = code.find("::first_contact_Contact(registry, occurrence, target);", contact_dispatch);
    const auto second_call = code.find("::second_contact_Contact(registry, occurrence, target);", contact_dispatch);
    REQUIRE(first_call != std::string::npos);
    REQUIRE(second_call != std::string::npos);
    CHECK(first_call < second_call);
}

TEST_CASE("Codegen EnTT: trigger-name reads lower to the handler payload parameter",
          "[codegen-entt][events][trigger-name]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "event Turn:\n"
        "    amount: int\n"
        "trait Holder:\n"
        "    var amount: int\n"
        "    var elapsed: float\n"
        "rule ReadFiltered:\n"
        "    each:\n"
        "        Holder as h\n"
        "    on Turn:\n"
        "        h.amount = Turn.amount\n"
        "rule ReadSelectionless:\n"
        "    on Turn:\n"
        "        let sample = Turn.amount + 1\n"
        "rule ReadAliased:\n"
        "    each:\n"
        "        Holder as h\n"
        "    after:\n"
        "        ReadFiltered\n"
        "    on Turn as t:\n"
        "        h.amount = t.amount + Turn.amount\n"
        "rule ReadTick:\n"
        "    each:\n"
        "        Holder as h\n"
        "    on tick:\n"
        "        h.elapsed = h.elapsed + tick.dt\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(!code.contains("Turn.amount"));

    const auto filtered = generated_function(code, "void read_filtered_Turn(");
    CHECK(filtered.contains("h.amount = " + handler_payload_param(filtered) + ".amount;"));

    const auto selectionless = generated_function(code, "void read_selectionless_Turn(");
    CHECK(selectionless.contains("cactus::num::add(" + handler_payload_param(selectionless) + ".amount, 1)"));

    const auto aliased = generated_function(code, "void read_aliased_Turn(");
    CHECK(handler_payload_param(aliased) == "t");
    CHECK(aliased.contains("h.amount = cactus::num::add(t.amount, t.amount);"));

    const auto tick = generated_function(code, "void read_tick_tick(");
    CHECK(handler_payload_param(tick) == "tick");
    CHECK(tick.contains("h.elapsed = (h.elapsed + tick.dt);"));
}

TEST_CASE("Codegen EnTT: a module-qualified trigger name lowers to the handler payload parameter",
          "[codegen-entt][events][trigger-name]") {
    ImportedSymbols combat;
    combat.module_name         = "game.combat";
    combat.event_symbols["Hit"] = ImportedEvent{
        .name   = "Hit",
        .fields = {ResolvedField{.name = "amount", .type = {.kind = TypeKind::Int, .name = "int"}}},
    };
    ModuleImports imports;
    imports.add("combat", std::move(combat));

    ProgramNode program;
    const auto decorated = full_pipeline(
        "use game.combat as combat\n"
        "trait Holder:\n"
        "    var amount: int\n"
        "rule ReadQualified:\n"
        "    each:\n"
        "        Holder as h\n"
        "    on combat.Hit:\n"
        "        h.amount = combat.Hit.amount\n",
        program,
        imports);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(!code.contains("combat.Hit"));
    const auto handler = generated_function(code, "void read_qualified_");
    CHECK(handler.contains("h.amount = " + handler_payload_param(handler) + ".amount;"));
}

TEST_CASE("Codegen EnTT: a pair handler reads its trigger name through the payload parameter",
          "[codegen-entt][events][trigger-name][pair-relations]") {
    ProgramNode program;
    const auto decorated = full_pipeline(
        "event Turn:\n"
        "    amount: int\n"
        "trait Transform:\n"
        "    var x: float\n"
        "trait Solid\n"
        "event Contact:\n"
        "    other: entity_id\n"
        "rule PairRead:\n"
        "    pairs:\n"
        "        body:\n"
        "            Transform\n"
        "        wall:\n"
        "            Solid\n"
        "    on Turn:\n"
        "        if Turn.amount > 0:\n"
        "            emit Contact:\n"
        "                other = wall\n",
        program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(!code.contains("Turn.amount"));
    const auto handler = generated_function(code, "void pair_read_Turn(");
    CHECK(handler.contains(handler_payload_param(handler) + ".amount > 0"));
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
        "    each:\n"
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
    CHECK(code.contains("auto " + spawned_name +
                        " = cactus::runtime::entt_backend::generated_reserve_entity(registry);"));
    CHECK(code.contains("create_particle_at(registry, " + spawned_name + ");"));
    CHECK(code.contains("generated_queue_structural_command("));
    CHECK(code.contains("StructuralCommand::Kind::Spawn"));
    CHECK(code.contains("StructuralCommand::Kind::Add"));
    CHECK(code.contains("StructuralCommand::Kind::Remove"));
    CHECK(code.contains("StructuralCommand::Kind::Destroy"));

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
    // No lifecycle trigger here, so commit routes through the
    // runtime's commit_activation with no lifecycle trackers — applying
    // queued commands before draining is now a runtime-level invariant
    // (test_runtime_activation_scheduler.cpp), not per-program text.
    const auto commit_call =
        code.find("commit_activation(activation, registry, &generated_drain_event_cascade);", commit_fn);
    REQUIRE(commit_call != std::string::npos);

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
    CHECK(legacy_code.contains("auto " + legacy_spawned_name + " = create_particle(registry);"));
    CHECK(!legacy_code.contains("generated_reserve_entity"));
    CHECK(!legacy_code.contains("generated_queue_structural_command"));
}

TEST_CASE("Codegen EnTT: set queues a durable-value patch without projection cancel or baseline retain",
          "[codegen-entt][phase-runtime][commands][deferred-set]") {
    ProgramNode program;
    const auto decorated = full_pipeline("module game.patch\n"
                                         "pub extern event frame:\n"
                                         "    dt: float\n"
                                         "trait Health:\n"
                                         "    var current: int = 10\n"
                                         "    var max: int = 10\n"
                                         "trait Aim:\n"
                                         "    var other: entity_id\n"
                                         "phase tick:\n"
                                         "    from:\n"
                                         "        frame\n"
                                         "rule Patch:\n"
                                         "    each:\n"
                                         "        Aim as aim\n"
                                         "    on tick:\n"
                                         "        set Health on aim.other:\n"
                                         "            current = 1\n",
                                         program);

    const auto code  = CppEnttCodegen::generate(decorated);
    const auto start = code.find("StructuralCommand::Kind::Set");
    REQUIRE(start != std::string::npos);
    const auto end = code.find("});", start);
    REQUIRE(end != std::string::npos);
    const auto command = code.substr(start, end - start);

    CHECK(command.contains("durable_Health(registry, "));
    CHECK(command.contains("->current = "));
    CHECK(!command.contains("max"));
    CHECK(!command.contains("cancel_projected_"));
    CHECK(!command.contains("retain_construction"));
    CHECK(!command.contains("emplace"));
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
        "    each:\n"
        "        WorldTransform\n"
        "        Renderer\n"
        "    on render:\n"
        "        reads:\n"
        "            WorldTransform\n"
        "            Renderer\n"
        "        effects:\n"
        "            graphics\n"
        "extern rule SpriteAnimation:\n"
        "    each:\n"
        "        AnimatedSprite\n"
        "    after:\n"
        "        SpriteRenderer\n"
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
    // Phase-dispatch call dedup only collapses genuine same-symbol
    // collisions — two distinct nodes that resolve to two different
    // underlying functions in the same phase batch still each get exactly
    // one call.
    CHECK(count_occurrences(without_linearization_harness(dispatch), "::sprite_renderer_tick(registry);") == 1);
    CHECK(count_occurrences(without_linearization_harness(dispatch), "::sprite_animation_tick(registry);") == 1);

    CHECK(!code.contains("cactus_external__std_render_sprites__SpriteRenderer"));
    CHECK(!code.contains("cactus_external__std_render_sprites__SpriteAnimation"));
    const auto update = generated_function(code, "void generated_update_project(");
    const auto render = generated_function(code, "void generated_render_project(");
    CHECK(!update.contains("sprite_renderer_tick"));
    CHECK(!update.contains("sprite_animation_tick"));
    CHECK(!render.contains("sprite_renderer_tick"));
    CHECK(!render.contains("sprite_animation_tick"));
}

TEST_CASE(
    "Codegen EnTT: phase-dispatch call emission dedupes two execution-graph nodes that resolve to "
    "the same underlying implementation function",
    "[codegen-entt][phase-runtime]") {
    // Mirrors the confirmed real-world collision (build/generated_examples/waving-label-2d.
    // generated.cpp pre-fix): std.editor transitively imports both std.transform.flat and
    // std.transform.volume, so the merged program schedules two distinct TransformPropagation
    // execution-graph nodes (one per module) onto the same late_tick phase batch. Both resolve to
    // the same emitted C++ function because system_function_name only encodes the *compiling
    // program's* module name and the rule's simple name, not the rule's own defining module.
    //
    // Reproduced here by cloning a real, recognized std.transform.flat.TransformPropagation node
    // (AST declaration + execution-graph handler + topological-order entry) and retargeting the
    // clone's resolved identity to std.transform.volume, exactly as a second linked module would
    // produce — full_pipeline only supports a single source module, so this stands in for the
    // artifact linker's cross-module merge.
    ProgramNode program;
    auto decorated = full_pipeline(
        "module std.transform.flat\n"
        "pub extern event frame:\n"
        "    dt: float\n"
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
        "phase late_tick:\n"
        "    from:\n"
        "        frame\n"
        "extern rule TransformPropagation:\n"
        "    each:\n"
        "        Parent\n"
        "        LocalTransform\n"
        "        WorldTransform\n"
        "    on late_tick:\n"
        "        reads:\n"
        "            Parent\n"
        "            LocalTransform\n"
        "        writes:\n"
        "            WorldTransform\n",
        program);

    ExternRuleNode* flat_rule = nullptr;
    for (auto& declaration : program.declarations) {
        if (auto* rule = std::get_if<ExternRuleNode>(&declaration)) {
            rule->is_stdlib = true;
            flat_rule       = rule;
        }
    }
    REQUIRE(flat_rule != nullptr);
    REQUIRE(flat_rule->resolved_rule_id.has_value());

    auto flat_node_it = std::ranges::find_if(decorated.execution_graph.handlers, [&](const auto& node) {
        return node.identity.rule == *flat_rule->resolved_rule_id;
    });
    REQUIRE(flat_node_it != decorated.execution_graph.handlers.end());

    // ExternRuleNode is move-only (order_by's SortKey holds a
    // std::unique_ptr<ExprNode>), and flat_rule must stay intact for its own
    // dispatch below, so clone field-by-field instead of copy-constructing;
    // this fixture never sets order by:, so leaving it default-empty matches
    // flat_rule's own state exactly.
    ExternRuleNode volume_rule;
    volume_rule.name                   = flat_rule->name;
    volume_rule.is_stdlib               = flat_rule->is_stdlib;
    volume_rule.resolved_rule_id        = flat_rule->resolved_rule_id;
    volume_rule.resolved_after_rule_ids = flat_rule->resolved_after_rule_ids;
    volume_rule.filter                  = flat_rule->filter;
    volume_rule.exclude                 = flat_rule->exclude;
    volume_rule.after_rules             = flat_rule->after_rules;
    volume_rule.handlers                = flat_rule->handlers;
    volume_rule.location                = flat_rule->location;
    volume_rule.resolved_rule_id->module.name = "std.transform.volume";
    const auto volume_rule_id                 = *volume_rule.resolved_rule_id;
    program.declarations.emplace_back(std::move(volume_rule));

    HandlerNode volume_node   = *flat_node_it;
    volume_node.identity.rule = volume_rule_id;
    decorated.execution_graph.handlers.push_back(volume_node);
    decorated.execution_graph.stable_topological_order.push_back(volume_node.identity);

    decorated.module_name = "waving_label_test";

    const auto code = CppEnttCodegen::generate(decorated);
    const auto dispatch =
        generated_function(code,
                           "void generated_dispatch_phase_std_transform_flat__late_tick(entt::registry& registry, "
                           "const std_transform_flat__late_tickPhaseRuntimeState& phase)");
    CHECK(count_occurrences(dispatch, "transform_propagation_tick(registry);") == 1);
}

// ── Pair relations (dsl-pair-relations, 5.5) ─────────────────────────────────

static const std::string& pair_codegen_traits() {
    static const std::string value =
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
    return value;
}

TEST_CASE("Codegen EnTT: pair handler snapshots both bindings and iterates their product",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            pair_codegen_traits() +
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            DynamicBody\n"
            "            Transform\n"
            "        wall:\n"
            "            Solid\n"
            "            Collider\n"
            "    on tick:\n"
            "        if body != wall and body[Transform].x > 0.0 and wall[Collider].mask > 0:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            // Two independent typed snapshots, not a materialized tuple list.
            CHECK(code.contains("std::vector<entt::entity> body_snapshot;"));
            CHECK(code.contains("registry.view<DynamicBody, Transform>()"));
            CHECK(code.contains("std::vector<entt::entity> wall_snapshot;"));
            CHECK(code.contains("registry.view<Solid, Collider>()"));
            // Sorted by creation ordinal for deterministic tuple order, reading
            // each entity's ordinal exactly once rather than per comparison.
            CHECK(code.contains("registry.get<cactus::runtime::entt_backend::CreationOrdinal>(pair_entity).value,"));
            CHECK(code.contains("std::ranges::sort(body_ordered);"));
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
            CHECK(code.contains("if (cactus_recipient.has_value()) {"));
            CHECK(code.contains("const auto target = *cactus_recipient;"));
            CHECK(code.contains("if (registry.all_of<DynamicBody, Transform>(target)) {"));
            CHECK(code.contains("if (body == target) { continue; }"));
            // Pair-bound trait reads are const.
            CHECK(code.contains("registry.get<const Transform>(body).x"));
            CHECK(code.contains("registry.get<const Collider>(wall).mask"));
            // Bare binding identifiers used directly as entity_id (emit target).
            CHECK(code.contains("(body != wall)"));
        }
    }
}

TEST_CASE(
    "Codegen EnTT: pair handler wraps the tuple body in a single shared per-invocation lambda so "
    "`return` skips only the current tuple",
    "[codegen-entt][pair-relations]") {
    // Regression test: DetectBubbleContact in examples/bouncy-bubbles rejects
    // self-pairs with `if a == b: return`. Both bindings snapshot the same
    // entity set in the same order, so the very first tuple the broadcast
    // double loop produces is always a self-pair — if `return` were a bare
    // C++ `return;` spliced directly into the loop body (no lambda), it
    // would exit the whole generated handler function on that first tuple
    // and no other pair would ever be evaluated. dedupe-handler-dispatch-
    // codegen: the body lives in one named lambda, defined once ahead of
    // every dispatch branch, and each branch invokes it by name.
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            pair_codegen_traits() +
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
            auto code                 = EnttSystemEmitter::emit_system(*sys, decorated);
            const auto pair_body_name = extract_temp_name(code, "cactus_gen_pair_body_");
            REQUIRE_FALSE(pair_body_name.empty());

            const auto lambda_def = code.find("auto " + pair_body_name + " = [&](entt::entity a, entt::entity b) {");
            REQUIRE(lambda_def != std::string::npos);
            const auto return_stmt = code.find("return;", lambda_def);
            REQUIRE(return_stmt != std::string::npos);
            const auto recipient_branch = code.find("if (cactus_recipient.has_value()) {");
            REQUIRE(recipient_branch != std::string::npos);
            // The lambda (and the `return` inside it) is defined once, ahead
            // of every dispatch branch, so `return` ends only that call's
            // tuple instead of the enclosing generated function.
            CHECK(lambda_def < recipient_branch);
            CHECK(return_stmt < recipient_branch);

            const auto broadcast_branch = code.find("} else {", recipient_branch);
            REQUIRE(broadcast_branch != std::string::npos);
            const auto a_loop = code.find("for (auto a : a_snapshot)", broadcast_branch);
            const auto b_loop = code.find("for (auto b : b_snapshot)", broadcast_branch);
            REQUIRE(a_loop != std::string::npos);
            REQUIRE(b_loop != std::string::npos);
            CHECK(a_loop < b_loop);
            // The broadcast full scan dispatches into the shared lambda by call.
            const auto broadcast_call = code.find(pair_body_name + "(a, b);", b_loop);
            CHECK(broadcast_call != std::string::npos);
        }
    }
}

TEST_CASE(
    "Codegen EnTT: pair handler dispatches both recipient-targeted branches into the shared "
    "per-invocation lambda",
    "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            pair_codegen_traits() +
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
            auto code                 = EnttSystemEmitter::emit_system(*sys, decorated);
            const auto pair_body_name = extract_temp_name(code, "cactus_gen_pair_body_");
            REQUIRE_FALSE(pair_body_name.empty());
            const auto lambda_def = code.find("auto " + pair_body_name + " = [&](entt::entity a, entt::entity b) {");
            REQUIRE(lambda_def != std::string::npos);

            const auto recipient_branch = code.find("if (cactus_recipient.has_value()) {");
            REQUIRE(recipient_branch != std::string::npos);
            CHECK(lambda_def < recipient_branch);
            const auto broadcast_branch = code.find("} else {", recipient_branch);
            REQUIRE(broadcast_branch != std::string::npos);

            // Recipient-as-left: `a` fixed to the target, looping over `b`,
            // dispatching into the shared lambda by call.
            const auto left_loop = code.find("for (auto b : b_snapshot)", recipient_branch);
            REQUIRE(left_loop != std::string::npos);
            CHECK(left_loop < broadcast_branch);
            const auto left_call = code.find(pair_body_name + "(a, b);", left_loop);
            REQUIRE(left_call != std::string::npos);
            CHECK(left_call < broadcast_branch);

            // Recipient-as-right: `b` fixed to the target, looping over `a`,
            // dispatching into the shared lambda by call.
            const auto right_loop = code.find("for (auto a : a_snapshot)", left_call);
            REQUIRE(right_loop != std::string::npos);
            CHECK(right_loop < broadcast_branch);
            const auto right_call = code.find(pair_body_name + "(a, b);", right_loop);
            REQUIRE(right_call != std::string::npos);
            CHECK(right_call < broadcast_branch);
        }
    }
}

TEST_CASE("Codegen EnTT: pair handler body text appears exactly once across all three dispatch sites",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            pair_codegen_traits() +
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
            // Recipient-as-left, recipient-as-right, and the broadcast full
            // scan all dispatch into one shared lambda, so the body's
            // distinguishing `return;` statement is text-present exactly
            // once in the generated function rather than once per site.
            CHECK(count_occurrences(code, "return;") == 1);
        }
    }
}

TEST_CASE("Codegen EnTT: pair binding selecting the same trait as the other binding gets distinct access",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            pair_codegen_traits() +
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            Collider\n"
            "        wall:\n"
            "            Collider\n"
            "    on tick:\n"
            "        if body[Collider].mask > wall[Collider].mask:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("registry.get<const Collider>(body).mask"));
            CHECK(code.contains("registry.get<const Collider>(wall).mask"));
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
            pair_codegen_traits() +
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            DynamicBody\n"
            "        wall:\n"
            "            Solid as Collider\n"
            "    on tick:\n"
            "        if Collider.active:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("registry.get<const Solid>(wall).active"));
            CHECK(!code.contains("registry.get<const Collider>(wall)"));
        }
    }
}

TEST_CASE("Codegen EnTT: pair binding-local alias and qualified imported trait resolve to canonical access",
          "[codegen-entt][pair-relations]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            pair_codegen_traits() +
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            DynamicBody\n"
            "        wall:\n"
            "            Collider as c\n"
            "    on tick:\n"
            "        if c.mask > 0:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("registry.get<const Collider>(wall).mask"));
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
            pair_codegen_traits() +
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
    // CreationOrdinal is runtime-hosted (backends/cpp-entt/runtime.hpp);
    // codegen must not redefine it inline (duplicate-definition error, since
    // runtime.hpp is always included).
    CHECK(!code.contains("struct CreationOrdinal {"));
    // Targeted emit resolves `body` to the per-tuple loop variable, evaluates
    // it once, and carries it into the queued occurrence via the targeted
    // emit path (targeted-event-delivery) rather than a validity guard around
    // broadcast dispatch. The recipient temporary is mangled so it can never
    // collide with a `to:` expression that is itself the bare identifier
    // `target` (e.g. `emit X(to: target)`).
    const auto emit_target_name = extract_temp_name(code, "cactus_gen_emit_target_");
    REQUIRE_FALSE(emit_target_name.empty());
    CHECK(code.contains("const auto " + emit_target_name + " = body;"));
    CHECK(code.contains("if (registry.valid(" + emit_target_name + ")) {"));
    CHECK(code.contains("generated_emit_targeted_event(ContactEvent{.other = wall}, " + emit_target_name + ");"));
    // Project remains an immediate (non-buffered) call, same as unary rules.
    CHECK(code.contains("project_GroundContact(registry, body);"));
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
            CHECK(code.contains("registry.view<Solid>()"));
            CHECK(code.contains("std::vector<entt::entity> wall_snapshot;"));
            CHECK(code.contains("for (auto wall : wall_snapshot)"));
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
    // The stale-recipient guard ("checked once, before dispatch, drops the
    // occurrence for every consumer") is runtime-hosted (drain_event_cascade,
    // backends/cpp-entt/runtime.hpp) and covered directly by
    // test_runtime_activation_scheduler.cpp; this codegen-level test only
    // checks that generated_drain_event_cascade routes through it with a
    // dispatch lambda wrapping generated_dispatch_event.
    // A forward declaration of this function precedes its definition, so the
    // search must include the opening brace to skip past it.
    const auto drain = generated_function(code, "void generated_drain_event_cascade(entt::registry& registry) {");
    CHECK(drain.contains("drain_event_cascade("));
    CHECK(drain.contains("generated_scheduler_state().activation, registry,"));
    CHECK(drain.contains("generated_dispatch_event(reg, occurrence, target);"));
}

TEST_CASE("Codegen EnTT: a targeted emit forwards its recipient to the runtime",
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
    // The queued envelope keeps the recipient through any delay; that is
    // runtime-hosted and covered by test_runtime_activation_scheduler.cpp.
    const auto targeted =
        generated_function(code, "void generated_emit_targeted_event(Occurrence occurrence, entt::entity target)");
    CHECK(targeted.contains(
        "emit_targeted_event(generated_scheduler_state().activation, std::move(occurrence), target);"));
}

namespace {

const std::string kCycleFrame =
    "pub extern event frame:\n"
    "    dt: float\n"
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "    every: 1.0 / 60.0\n"
    "phase tick:\n"
    "    after:\n"
    "        fixed_tick\n"
    "event Knock\n"
    "trait Door:\n"
    "    var knocks: int = 0\n"
    "entity Front:\n"
    "    Door\n"
    "rule Starter:\n"
    "    on fixed_tick:\n"
    "        emit Knock to Front\n";

const std::string kAnswerAndLogger =
    "rule Answer:\n"
    "    each:\n"
    "        Door as door\n"
    "    on Knock:\n"
    "        door.knocks = door.knocks + 1\n"
    "        emit Knock to self\n"
    "rule Logger:\n"
    "    on Knock:\n"
    "        let seen = 1\n"
    "rule Idle:\n"
    "    on tick:\n"
    "        let idle = 1\n";

}  // namespace

TEST_CASE("Codegen EnTT: an acyclic program generates no delay machinery", "[codegen-entt][delivery-cycles]") {
    ProgramNode program;
    auto decorated = full_pipeline(kCycleFrame +
                                       "rule Logger:\n"
                                       "    on Knock:\n"
                                       "        let seen = 1\n",
                                   program);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK_FALSE(code.contains("DelayedQueue"));
    CHECK_FALSE(code.contains("current_delayed"));
    CHECK_FALSE(code.contains("current_component"));
    CHECK_FALSE(code.contains("route_admits"));
    CHECK_FALSE(code.contains("deliver_delayed_events"));
}

TEST_CASE("Codegen EnTT: a cyclic program queues delays per producing phase and on the host clock",
          "[codegen-entt][delivery-cycles]") {
    ProgramNode program;
    auto decorated = full_pipeline(kCycleFrame + kAnswerAndLogger, program);

    const auto code  = CppEnttCodegen::generate(decorated);
    const auto state = generated_function(code, "struct SchedulerState {");
    CHECK(state.contains("DelayedQueue<EventOccurrence> delayed_host;"));
    CHECK(state.contains("DelayedQueue<EventOccurrence> delayed_test__fixed_tick;"));
    CHECK_FALSE(state.contains("delayed_tick;"));

    const auto fixed = generated_function(code, "void generated_run_phase_batch_test__fixed_tick(");
    CHECK(fixed.contains("current_delayed = &scheduler.delayed_test__fixed_tick;"));
    const auto dispatch_at = fixed.find("generated_dispatch_phase_test__fixed_tick(registry, phase);");
    const auto deliver_at  = fixed.find("deliver_delayed_events(scheduler.activation);");
    const auto drain_at    = fixed.find("generated_drain_event_cascade(registry);");
    REQUIRE(dispatch_at != std::string::npos);
    REQUIRE(deliver_at != std::string::npos);
    CHECK(dispatch_at < deliver_at);
    CHECK(deliver_at < drain_at);
    const auto idle = generated_function(code, "void generated_run_phase_batch_test__tick(");
    CHECK_FALSE(idle.contains("deliver_delayed_events"));

    const auto external = generated_function(code, "void generated_drain_external_events(entt::registry& registry) {");
    CHECK(external.contains("delayed_host"));
}

TEST_CASE("Codegen EnTT: emits and dispatch follow the routes the graph implies", "[codegen-entt][delivery-cycles]") {
    ProgramNode program;
    auto decorated = full_pipeline(kCycleFrame + kAnswerAndLogger, program);
    const auto code = CppEnttCodegen::generate(decorated);

    // Answer's Knock reaches Answer by delay and Logger now, so it splits.
    const auto targeted = generated_function(code, "void generated_emit_targeted_event(KnockEvent occurrence, entt::entity target) {");
    CHECK(targeted.contains("switch (activation.current_component) {"));
    CHECK(targeted.contains("DeliveryRoute::ImmediateOnly"));
    CHECK(targeted.contains("emit_delayed_targeted_event(activation, std::move(occurrence), target, DeliveryRoute::DelayedOnly"));
    CHECK(targeted.contains("default:"));

    const auto dispatch = generated_function(code, "void generated_dispatch_event(entt::registry& registry, const KnockEvent& occurrence");
    CHECK(dispatch.contains("route_admits(activation.delivery_route, activation.delivery_component, 0U)"));
    CHECK(dispatch.contains("route_admits(activation.delivery_route, activation.delivery_component, kNoComponent)"));
    CHECK(dispatch.contains("activation.current_component = 0U;"));

    const auto phase = generated_function(code, "void generated_dispatch_phase_test__fixed_tick(entt::registry& registry");
    CHECK(phase.contains("current_component = kNoComponent;"));
    CHECK_FALSE(phase.contains("route_admits"));
}

TEST_CASE("Codegen EnTT: a lifecycle cycle passes its route table to commit", "[codegen-entt][delivery-cycles]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "trait Offspring\n"
        "template Minion:\n"
        "    Offspring\n"
        "rule Seed:\n"
        "    on tick:\n"
        "        spawn Minion()\n"
        "rule Respawn:\n"
        "    on added Offspring:\n"
        "        spawn Minion()\n",
        program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto commit = generated_function(code, "void generated_commit_activation(entt::registry& registry) {");
    CHECK(commit.contains("generated_lifecycle_trackers(), GeneratedLifecycleRoutes{});"));
    const auto routes = generated_function(code, "struct GeneratedLifecycleRoutes {");
    CHECK(routes.contains("if (trait_index == 0 && added && component == 0U) {"));
    CHECK(routes.contains("return {.immediate = false, .delayed = true};"));
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
            CHECK(code.contains("std::optional<entt::entity> cactus_recipient = std::nullopt"));
            CHECK(code.contains("(void)cactus_recipient;"));
            // No recipient-conditioned branch — a selectionless consumer runs
            // once regardless of targeting (targeted-event-delivery,
            // "Selectionless observer receives targeted occurrence once").
            CHECK(!code.contains("cactus_recipient.has_value()"));
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
        "    each:\n"
        "        Health\n"
        "    on Contact:\n"
        "        hp = hp - 1\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("if (cactus_recipient.has_value()) {"));
            CHECK(code.contains("entt::entity entity = *cactus_recipient;"));
            CHECK(code.contains("if (registry.all_of<Health>(entity)) {"));
            // Component access for the targeted branch mirrors the broadcast
            // branch's binding name so the same body text works unmodified.
            CHECK(code.contains("auto& Health_comp = registry.get<Health>(entity);"));
            // Broadcast fallback for an untargeted occurrence remains the
            // ordinary full-view iteration.
            CHECK(code.contains("registry.view<Health>()"));
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
        "    each:\n"
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
    CHECK(tail.contains("if (target.has_value()) {"));
    CHECK(tail.contains("entt::entity entity = *target;"));
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
        "    each:\n"
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
            CHECK(code.contains("hp.current = cactus::num::sub(hp.current, dmg.amount);"));
            CHECK(!code.contains("auto hp = "));
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
        CHECK(!code.contains("std::vector"));
        CHECK(!code.contains("foreach_snapshot_"));

        const auto step_name = extract_temp_name(code, "cactus_gen_range_step_");
        REQUIRE_FALSE(step_name.empty());
        CHECK(code.contains("if (" + step_name + " > 0) {"));
        CHECK(code.contains("} else if (" + step_name + " < 0) {"));
        // No unconditional trailing branch: a step of 0 falls through both
        // arms and executes the loop body zero times.
        CHECK(!code.contains("} else {\n"));
        CHECK(code.contains("for (int k = "));
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
        CHECK(code.contains("k < "));
        CHECK(code.contains("k > "));
        CHECK(code.contains("k += "));
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
        CHECK(code.contains("const int " + step_name + " = 1;"));
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
        "    each:\n"
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
        CHECK(!own_closes.contains("else"));
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
        CHECK(code.contains("vec2(static_cast<float>(k), 0.0F)"));
        CHECK(code.contains("vec3(static_cast<float>(k), static_cast<float>(0), 0.0F)"));
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
        "        var v = vec2(0.0, 0.0)\n"
        "        for k in range(0, 1):\n"
        "            v = vec2(k, 0.0)\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "Demo") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(code.contains("static_cast<float>(k)"));
        CHECK(code.contains("0.0F"));
        // The already-float argument isn't redundantly wrapped.
        CHECK(!code.contains("static_cast<float>(0.0F)"));
    }
}

TEST_CASE("Codegen EnTT: color(...) constructor routes through color_from_components",
          "[codegen-entt][vector-expressions]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "rule Demo:\n"
        "    on tick:\n"
        "        let c = color(1.0, 0.0, 0.0, 1.0)\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "Demo") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(code.contains("color_from_components(1.0F, 0.0F, 0.0F, 1.0F)"));
        // Never a raw `color(...)` call left un-rewritten.
        CHECK(!code.contains(" color(1.0F"));
    }
}

TEST_CASE("Codegen EnTT: color(...) constructor int arguments are wrapped in static_cast<float>",
          "[codegen-entt][range][vector-expressions]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n"
        "rule Demo:\n"
        "    on tick:\n"
        "        for k in range(0, 1):\n"
        "            let c = color(k, 0, 0, 1.0)\n",
        program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "Demo") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(
            code.contains("color_from_components(static_cast<float>(k), static_cast<float>(0), "
                          "static_cast<float>(0), 1.0F)"));
    }
}

TEST_CASE("Codegen EnTT: pair-bound color channel read normalizes the raw byte field",
          "[codegen-entt][pair-relations][vector-expressions]") {
    ProgramNode program;
    auto decorated = full_pipeline(
        "event tick:\n"
        "    dt: float\n" +
            pair_codegen_traits() +
            "trait Tint:\n"
            "    var tint: color\n"
            "rule DetectContacts:\n"
            "    pairs:\n"
            "        body:\n"
            "            DynamicBody\n"
            "            Tint\n"
            "        wall:\n"
            "            Solid\n"
            "    on tick:\n"
            "        if body != wall and body[Tint].tint.a > 0.5:\n"
            "            emit Contact:\n"
            "                other = wall\n",
        program);

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl)) {
            auto code = EnttSystemEmitter::emit_system(*sys, decorated);
            CHECK(code.contains("static_cast<float>(registry.get<const Tint>(body).tint.a) / 255.0F"));
        }
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
        CHECK(code.contains("static_cast<float>(k) * (test__TAU / static_cast<float>(test__PARTICLE_COUNT))"));
    }
}

// ── SAP-eligible pair rules (spatial-broadphase-runtime) ────────────────────
//
// circles_overlap is declared locally within a module literally named
// "std.collision.flat" so its canonical id matches the recognized target
// exactly, mirroring test_semantic.cpp's spatial-join recognition tests —
// recognition only ever inspects resolved_callee_id's canonical identity,
// never the declaring module's actual provenance.

static std::string emit_named_rule(const std::string& source, const std::string& rule_name) {
    ProgramNode program;
    auto decorated = full_pipeline(source, program);
    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys != nullptr && sys->name == rule_name) {
            return EnttSystemEmitter::emit_system(*sys, decorated);
        }
    }
    FAIL("rule '" + rule_name + "' not found in the analyzed program");
    return {};
}

static const std::string& spatial_join_traits() {
    static const std::string value =
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, b_radius: float) bool:\n"
        "    return a_radius + b_radius >= 0.0\n"
        "event tick:\n"
        "    dt: float\n"
        "event Contact:\n"
        "    other: entity_id\n";
    return value;
}

TEST_CASE("Codegen EnTT: SAP-eligible pair rule calls the runtime broad phase instead of a hand-rolled sweep",
          "[codegen-entt][spatial-join]") {
    ProgramNode program;
    auto decorated = full_pipeline("module std.collision.flat\n" + spatial_join_traits() +
                                       "rule DetectContact:\n"
                                       "    pairs:\n"
                                       "        a:\n"
                                       "            Transform\n"
                                       "            Collider\n"
                                       "        b:\n"
                                       "            Transform\n"
                                       "            Collider\n"
                                       "    where:\n"
                                       "        circles_overlap(a[Transform].position, a[Collider].radius, "
                                       "b[Transform].position, b[Collider].radius)\n"
                                       "    on tick:\n"
                                       "        emit Contact:\n"
                                       "            other = b\n",
                                   program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "DetectContact") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        // Runtime-owned SAP API called with the same ordinal-sorted snapshot
        // machinery every pair rule already builds.
        CHECK(code.contains("cactus::runtime::entt_backend::circle_proxy("));
        CHECK(code.contains("cactus::runtime::entt_backend::SapBroadPhase2D"));
        CHECK(code.contains(".sync(__sap_proxies)"));
        CHECK(code.contains("__sap.candidate_pairs()"));
        // The recognized predicate is still re-verified per tuple (SAP's
        // candidates are a conservative superset), via the exact same
        // synthesized-guard/body-emission machinery as before.
        CHECK(code.contains("std_collision_flat__circles_overlap("));
        // No hand-rolled sort/sweep/candidate-generation loop of its own: the
        // untargeted (broadcast) branch no longer contains the plain nested
        // Cartesian double-loop. The recipient-targeted branch precedes it and
        // keeps its own (unrelated, already-cheap) incident-tuple loop shapes.
        const auto broadcast_branch = code.find("} else {");
        REQUIRE(broadcast_branch != std::string::npos);
        CHECK(code.find("for (auto a : a_snapshot)", broadcast_branch) == std::string::npos);
        CHECK(code.find("for (auto b : b_snapshot)", broadcast_branch) == std::string::npos);
        CHECK(code.find("std::ranges::sort", broadcast_branch) == std::string::npos);
    }
}

// dot is declared locally within a module literally named "std.math.vec2" so
// its canonical id matches the recognized target exactly, mirroring
// spatial_join_traits() above.
static const std::string& manual_dot_spatial_join_traits() {
    static const std::string value =
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func dot(a: vec2, b: vec2) float:\n"
        "    return 0.0\n"
        "event tick:\n"
        "    dt: float\n"
        "event Contact:\n"
        "    other: entity_id\n";
    return value;
}

TEST_CASE("Codegen EnTT: manual dot-product where: shape calls the runtime broad phase instead of a hand-rolled sweep",
          "[codegen-entt][spatial-join]") {
    ProgramNode program;
    auto decorated =
        full_pipeline("module std.math.vec2\n" + manual_dot_spatial_join_traits() +
                          "rule DetectContact:\n"
                          "    pairs:\n"
                          "        a:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "        b:\n"
                          "            Transform\n"
                          "            Collider\n"
                          "    where:\n"
                          "        dot(b[Transform].position - a[Transform].position, b[Transform].position - "
                          "a[Transform].position) < (a[Collider].radius + b[Collider].radius) * "
                          "(a[Collider].radius + b[Collider].radius)\n"
                          "    on tick:\n"
                          "        emit Contact:\n"
                          "            other = b\n",
                      program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "DetectContact") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(code.contains("cactus::runtime::entt_backend::circle_proxy("));
        CHECK(code.contains("cactus::runtime::entt_backend::SapBroadPhase2D"));
        CHECK(code.contains(".sync(__sap_proxies)"));
        CHECK(code.contains("__sap.candidate_pairs()"));
    }
}

// Registered under alias "collision", matching examples/bouncy-bubbles/main.cactus's
// `use std.collision.flat as collision` and circles_overlap's real stdlib
// signature (stdlib/std/collision/flat.cactus), so this fixture exercises the
// same recognized-call shape that example now relies on for its
// DetectBubbleContact rule (add-sap-broadphase).
static ModuleImports bouncy_bubbles_collision_imports() {
    ImportedSymbols syms;
    syms.module_name = "std.collision.flat";

    TypeInfo vec2_type;
    vec2_type.kind = TypeKind::Vec2;
    TypeInfo float_type;
    float_type.kind = TypeKind::Float;
    TypeInfo bool_type;
    bool_type.kind = TypeKind::Bool;

    ResolvedFunc func;
    func.name           = "circles_overlap";
    func.module_name    = "std.collision.flat";
    func.is_pub         = true;
    func.effect_summary = std::unordered_set<std::string>{};
    func.params         = {
        ResolvedParam{.name = "a_position", .type = vec2_type},
        ResolvedParam{.name = "a_radius", .type = float_type},
        ResolvedParam{.name = "b_position", .type = vec2_type},
        ResolvedParam{.name = "b_radius", .type = float_type},
    };
    func.return_type              = bool_type;
    const auto symbol             = make_symbol_id(SymbolKind::Func, "std.collision.flat", "circles_overlap");
    func.symbol_id                = symbol;
    func.canonical_id             = make_canonical_id(symbol);
    syms.funcs["circles_overlap"] = std::move(func);

    ModuleImports imports;
    imports.modules["collision"] = std::move(syms);
    return imports;
}

TEST_CASE("Codegen EnTT: bouncy-bubbles' migrated DetectBubbleContact where: shape calls the runtime broad phase",
          "[codegen-entt][spatial-join]") {
    ProgramNode program;
    const auto imports = bouncy_bubbles_collision_imports();
    auto decorated     = full_pipeline(
        "trait Bubble:\n"
        "    var velocity: vec2\n"
        "trait CircleCollider:\n"
        "    var radius: float\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "event tick:\n"
        "    dt: float\n"
        "event BubbleBounce:\n"
        "    new_velocity: vec2\n"
        "rule DetectBubbleContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Bubble\n"
        "            CircleCollider\n"
        "            WorldTransform\n"
        "        b:\n"
        "            Bubble\n"
        "            CircleCollider\n"
        "            WorldTransform\n"
        "    where:\n"
        "        a != b\n"
        "        collision.circles_overlap(a[WorldTransform].position, "
        "a[CircleCollider].radius, b[WorldTransform].position, b[CircleCollider].radius)\n"
        "    on tick:\n"
        "        emit BubbleBounce to a:\n"
        "            new_velocity = a[Bubble].velocity\n",
        program,
        imports);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "DetectBubbleContact") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        CHECK(code.contains("cactus::runtime::entt_backend::circle_proxy("));
        CHECK(code.contains("cactus::runtime::entt_backend::SapBroadPhase2D"));
    }
}

TEST_CASE("Codegen EnTT: cross-domain pair rule with a recognized-shape call builds proxies from both snapshots",
          "[codegen-entt][spatial-join]") {
    ProgramNode program;
    auto decorated = full_pipeline("module std.collision.flat\n" + spatial_join_traits() +
                                       "trait Wall:\n"
                                       "    var active: bool = true\n"
                                       "rule DetectContact:\n"
                                       "    pairs:\n"
                                       "        a:\n"
                                       "            Transform\n"
                                       "            Collider\n"
                                       "        b:\n"
                                       "            Transform\n"
                                       "            Collider\n"
                                       "            Wall\n"
                                       "    where:\n"
                                       "        circles_overlap(a[Transform].position, a[Collider].radius, "
                                       "b[Transform].position, b[Collider].radius)\n"
                                       "    on tick:\n"
                                       "        emit Contact:\n"
                                       "            other = b\n",
                                   program);

    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr || sys->name != "DetectContact") {
            continue;
        }
        auto code = EnttSystemEmitter::emit_system(*sys, decorated);
        const auto broadcast_branch = code.find("} else {");
        REQUIRE(broadcast_branch != std::string::npos);
        CHECK(code.find("for (auto a : a_snapshot)", broadcast_branch) == std::string::npos);
        const auto a_proxies = code.find("< a_snapshot.size(); ++__sap_i)", broadcast_branch);
        const auto b_proxies = code.find("< b_snapshot.size(); ++__sap_i)", broadcast_branch);
        REQUIRE(a_proxies != std::string::npos);
        REQUIRE(b_proxies != std::string::npos);
        CHECK(code.find("cactus::runtime::entt_backend::SapSide::Left", a_proxies) < b_proxies);
        CHECK(code.find("cactus::runtime::entt_backend::SapSide::Right", b_proxies) != std::string::npos);
        CHECK(code.find("cactus::runtime::entt_backend::SapBroadPhase2D", b_proxies) != std::string::npos);
    }
}

static const std::string& volume_join_traits() {
    static const std::string value =
        "const:\n"
        "    PROBE = 0.5\n"
        "trait Body:\n"
        "    var position: vec3\n"
        "    var radius: float\n"
        "trait Solid:\n"
        "    var position: vec3\n"
        "    var size: vec3\n"
        "    var rotation: quat\n"
        "pub func sphere_box_overlap(sphere_position: vec3, sphere_radius: float, box_position: vec3, box_size: vec3, "
        "box_rotation: quat) bool:\n"
        "    return sphere_radius >= 0.0\n"
        "event tick:\n"
        "    dt: float\n";
    return value;
}

static std::string emit_sphere_box_rule(const std::string& bindings, const std::string& predicate) {
    return emit_named_rule("module std.collision.volume\n" + volume_join_traits() + "rule Detect:\n    pairs:\n" +
                               bindings + "    where:\n        " + predicate + "\n    on tick:\n        let x = 1\n",
                           "Detect");
}

TEST_CASE("Codegen EnTT: sphere-box pair rule builds sphere and box proxies from both snapshots",
          "[codegen-entt][spatial-join]") {
    const auto code = emit_sphere_box_rule("        actor:\n            Body\n        wall:\n            Solid\n",
                                           "sphere_box_overlap(actor[Body].position, actor[Body].radius + PROBE, "
                                           "wall[Solid].position, wall[Solid].size, wall[Solid].rotation)");
    const auto actor_loop = code.find("< actor_snapshot.size(); ++__sap_i)");
    const auto wall_loop  = code.find("< wall_snapshot.size(); ++__sap_i)");
    REQUIRE(actor_loop != std::string::npos);
    REQUIRE(wall_loop != std::string::npos);
    // Targeted delivery keeps its incident-tuple loops ahead of the broad phase.
    const auto targeted = code.find("if (cactus_recipient.has_value())");
    REQUIRE(targeted != std::string::npos);
    CHECK(code.find("for (auto wall : wall_snapshot)", targeted) < actor_loop);
    CHECK(code.find("for (auto actor : actor_snapshot)", targeted) < actor_loop);
    const auto sphere = code.find("cactus::runtime::entt_backend::sphere_proxy(actor, __sap_i, ", actor_loop);
    const auto box    = code.find("cactus::runtime::entt_backend::box_proxy(wall, __sap_i, ", wall_loop);
    REQUIRE(sphere != std::string::npos);
    REQUIRE(box != std::string::npos);
    CHECK(code.find("SapSide::Left", sphere) < wall_loop);
    CHECK(code.find("SapSide::Right", box) != std::string::npos);
    // Each slot is rewritten for its own binding, constants included.
    const auto sphere_line = code.substr(sphere, code.find('\n', sphere) - sphere);
    CHECK(sphere_line.contains("PROBE"));
    CHECK(!sphere_line.contains("wall"));
    const auto box_line = code.substr(box, code.find('\n', box) - box);
    CHECK(box_line.contains(".size"));
    CHECK(box_line.contains(".rotation"));
    CHECK(!box_line.contains("actor"));
    CHECK(code.contains("cactus::runtime::entt_backend::SapBroadPhase3D"));
    CHECK(code.contains("for (const auto& __sap_pair : __sap.candidate_pairs())"));
    CHECK(code.contains("__sap_proxies[__sap_pair.left].entity, __sap_proxies[__sap_pair.right].entity"));
}

TEST_CASE("Codegen EnTT: a shape on the right binding builds right-side proxies",
          "[codegen-entt][spatial-join]") {
    const auto code = emit_sphere_box_rule("        wall:\n            Solid\n        actor:\n            Body\n",
                                           "sphere_box_overlap(actor[Body].position, actor[Body].radius, "
                                           "wall[Solid].position, wall[Solid].size, wall[Solid].rotation)");
    CHECK(code.contains("sphere_proxy(actor, __sap_i, cactus::runtime::entt_backend::SapSide::Right, "));
    CHECK(code.contains("box_proxy(wall, __sap_i, cactus::runtime::entt_backend::SapSide::Left, "));
}

// dsl-render-passes: a stage handler's `let`-bound local is an ordinary
// Cactus identifier the author is free to choose without knowing anything
// about GLSL — but GLSL reserves a set of words (e.g. `half`) whether or not
// the corresponding feature is used, and headless test builds never actually
// compile the embedded GLSL (CACTUS_RAYLIB_FAKE makes LoadShaderFromMemory a
// no-op), so a collision like this has no path to failing any existing
// automated test. This test locks in the fix directly against the generated
// GLSL text instead: every `let` local must be emitted under a mangled name,
// unconditionally, so no Cactus identifier can ever collide with a GLSL
// keyword regardless of which one a future example happens to pick.
TEST_CASE("Codegen EnTT: render-pass stage handler's `let` locals are mangled to avoid GLSL reserved words",
          "[codegen-entt][render-passes]") {
    ImportedSymbols passes_syms;
    passes_syms.module_name = "std.render.passes";
    ResolvedEnum pass_enum;
    pass_enum.name            = "Pass";
    pass_enum.variants        = {"Quads"};
    passes_syms.enums["Pass"] = pass_enum;
    ResolvedEnum target_enum;
    target_enum.name            = "Target";
    target_enum.variants        = {"Screen"};
    passes_syms.enums["Target"] = target_enum;
    ModuleImports imports;
    imports.add("passes", std::move(passes_syms));

    ProgramNode program;
    auto decorated = full_pipeline(
        "module render_pass_reserved_word\n"
        "\n"
        "use std.render.passes as passes\n"
        "\n"
        "extern event Tick:\n"
        "    dt: float\n"
        "\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "\n"
        "pub phase my_pass:\n"
        "    from:\n"
        "        Tick\n"
        "    pipeline: passes.Pass = passes.Pass.Quads\n"
        "    output: passes.Target = passes.Target.Screen\n"
        "\n"
        "rule MyVertex:\n"
        "    each:\n"
        "        WorldTransform as xf\n"
        "\n"
        "    on my_pass.vertex as v:\n"
        "        let half = 10.0\n"
        "        v.screen_position = vec2(xf.position.x + v.corner.x * half, xf.position.y + v.corner.y * half)\n"
        "        v.uv_out = v.uv\n"
        "        v.tint_out = #FFFFFFFF\n"
        "\n"
        "rule MyFragment:\n"
        "    on my_pass.fragment as f:\n"
        "        f.frag_color = f.tint\n",
        program,
        imports);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("let_half"));
    CHECK(!code.contains("float half"));
}

// dsl-render-passes: a stage handler body's binary-operator expressions must
// accept exactly the operand-type combinations dsl-vector-expressions'
// general table accepts (lookup_vector_binary_op_result) — `vec2 + vec2 *
// float` already type-checks in an ordinary (non-stage) handler body, so it
// must type-check identically here instead of being rejected by
// render_pass_emitter.cpp's own narrower same-type-or-float-promotion rule.
TEST_CASE("Codegen EnTT: render-pass stage handler accepts vec2 + vec2 * float, matching an ordinary handler body",
          "[codegen-entt][render-passes]") {
    ImportedSymbols passes_syms;
    passes_syms.module_name = "std.render.passes";
    ResolvedEnum pass_enum;
    pass_enum.name            = "Pass";
    pass_enum.variants        = {"Quads"};
    passes_syms.enums["Pass"] = pass_enum;
    ResolvedEnum target_enum;
    target_enum.name            = "Target";
    target_enum.variants        = {"Screen"};
    passes_syms.enums["Target"] = target_enum;
    ModuleImports imports;
    imports.add("passes", std::move(passes_syms));

    ProgramNode program;
    auto decorated = full_pipeline(
        "module render_pass_vector_ops\n"
        "\n"
        "use std.render.passes as passes\n"
        "\n"
        "extern event Tick:\n"
        "    dt: float\n"
        "\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "\n"
        "pub phase my_pass:\n"
        "    from:\n"
        "        Tick\n"
        "    pipeline: passes.Pass = passes.Pass.Quads\n"
        "    output: passes.Target = passes.Target.Screen\n"
        "\n"
        "rule MyVertex:\n"
        "    each:\n"
        "        WorldTransform as xf\n"
        "\n"
        "    on my_pass.vertex as v:\n"
        "        let half_size = 5.0\n"
        "        v.screen_position = xf.position + v.corner * half_size\n"
        "        v.uv_out = v.uv\n"
        "        v.tint_out = #FFFFFFFF\n"
        "\n"
        "rule MyFragment:\n"
        "    on my_pass.fragment as f:\n"
        "        f.frag_color = f.tint\n",
        program,
        imports);

    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("screen_position = (u_xf_position + (corner * let_half_size));"));
}

// dsl-render-passes: the shared operand-type table still rejects a genuinely
// invalid combination identically to before — extending translate_binary to
// consult lookup_vector_binary_op_result must not make it permissive. Vec2 +
// Color is rejected during general semantic analysis itself (the same
// lookup_vector_binary_op_result table an ordinary handler body's binary
// expressions already go through), before render_pass_emitter.cpp's own
// translation ever runs — so this deliberately doesn't use full_pipeline
// (which hard-asserts a clean compile) or reach CppEnttCodegen::generate.
TEST_CASE("Codegen EnTT: render-pass stage handler still rejects Vec2 + Color", "[codegen-entt][render-passes]") {
    ImportedSymbols passes_syms;
    passes_syms.module_name = "std.render.passes";
    ResolvedEnum pass_enum;
    pass_enum.name            = "Pass";
    pass_enum.variants        = {"Quads"};
    passes_syms.enums["Pass"] = pass_enum;
    ResolvedEnum target_enum;
    target_enum.name            = "Target";
    target_enum.variants        = {"Screen"};
    passes_syms.enums["Target"] = target_enum;
    ModuleImports imports;
    imports.add("passes", std::move(passes_syms));

    const std::string source =
        "module render_pass_vector_ops_invalid\n"
        "\n"
        "use std.render.passes as passes\n"
        "\n"
        "extern event Tick:\n"
        "    dt: float\n"
        "\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "\n"
        "pub phase my_pass:\n"
        "    from:\n"
        "        Tick\n"
        "    pipeline: passes.Pass = passes.Pass.Quads\n"
        "    output: passes.Target = passes.Target.Screen\n"
        "\n"
        "rule MyVertex:\n"
        "    each:\n"
        "        WorldTransform as xf\n"
        "\n"
        "    on my_pass.vertex as v:\n"
        "        v.screen_position = xf.position + v.tint_out\n"
        "        v.uv_out = v.uv\n"
        "        v.tint_out = #FFFFFFFF\n"
        "\n"
        "rule MyFragment:\n"
        "    on my_pass.fragment as f:\n"
        "        f.frag_color = f.tint\n";

    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    ProgramNode program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    (void)analyzer.analyze(program, imports);
    CHECK(errors.has_errors());
}

// ── Conditional expressions and value match ────────────────────────────────

static const std::string& conditional_codegen_prelude() {
    static const std::string value =
        "event tick:\n"
        "    dt: float\n"
        "enum Mode:\n"
        "    Idle\n"
        "    Run\n"
        "    Flee\n"
        "const:\n"
        "    FAST = true\n"
        "    STEP = if FAST: 2.0 else: 1.0\n"
        "    LEVEL = 2\n"
        "    GAIN = match LEVEL:\n"
        "        1 => 0.5\n"
        "        2 => 1.0\n"
        "        _ => 2.0\n"
        "trait Actor:\n"
        "    var mode: Mode = Mode.Idle\n"
        "    var hp: int = 0\n"
        "    var speed: float = 0.0\n"
        "pub extern func pick() int\n";
    return value;
}

static std::string conditional_codegen(const std::string& declarations) {
    ProgramNode program;
    auto decorated = full_pipeline(conditional_codegen_prelude() + declarations, program);
    return CppEnttCodegen::generate(decorated);
}

TEST_CASE("Codegen EnTT: conditional expressions and value match lower without placeholders",
          "[codegen-entt][conditional-expressions]") {
    const auto code = conditional_codegen(
        "func cost(mode: Mode) int:\n"
        "    var c = 0\n"
        "    match mode:\n"
        "        Mode.Idle =>\n"
        "            c = 1\n"
        "        _ =>\n"
        "            c = 2\n"
        "    return c\n"
        "rule R:\n"
        "    each:\n"
        "        Actor as a\n"
        "    on tick:\n"
        "        a.hp = if a.speed > 1.0: 1 else if a.speed > 0.5: 2 else: 3\n"
        "        let s = match a.mode:\n"
        "            Mode.Idle => 0.0\n"
        "            _ => STEP\n"
        "        a.speed = s\n"
        "        match a.hp:\n"
        "            0 =>\n"
        "                a.speed = 1.0\n"
        "            -1 =>\n"
        "                a.speed = 2.0\n"
        "            _ =>\n"
        "                a.speed = 3.0\n");
    CHECK(!code.contains("unsupported expr"));
    CHECK(code.contains("? 1 : cactus_chosen("));
    CHECK(code.contains("== -1"));
    CHECK(code.contains("LEVEL == 1) ? 0.5F : cactus_chosen((("));
    CHECK(code.contains("LEVEL == 2) ? 1.0F : 2.0F)))"));
}

TEST_CASE("Codegen EnTT: a match subject is evaluated once", "[codegen-entt][conditional-expressions]") {
    const auto code = conditional_codegen(
        "rule R:\n"
        "    each:\n"
        "        Actor as a\n"
        "    on tick:\n"
        "        a.speed = match pick():\n"
        "            0 => 1.0\n"
        "            1 => 2.0\n"
        "            _ => 3.0\n"
        "        match pick():\n"
        "            0 =>\n"
        "                a.hp = 1\n"
        "            1 =>\n"
        "                a.hp = 2\n"
        "            _ =>\n"
        "                a.hp = 3\n");
    CHECK(count_occurrences(code, "pick()") == 2);
}

TEST_CASE("Codegen EnTT: an exhaustive match emits its last arm unconditionally",
          "[codegen-entt][conditional-expressions]") {
    const auto code = conditional_codegen(
        "rule R:\n"
        "    each:\n"
        "        Actor as a\n"
        "    on tick:\n"
        "        a.speed = match a.mode:\n"
        "            Mode.Idle => 0.0\n"
        "            Mode.Run => 1.0\n"
        "            Mode.Flee => 2.0\n"
        "        match a.mode:\n"
        "            Mode.Idle =>\n"
        "                a.hp = 1\n"
        "            Mode.Run =>\n"
        "                a.hp = 2\n"
        "            Mode.Flee =>\n"
        "                a.hp = 3\n");
    CHECK(code.contains("Mode::Run"));
    CHECK(!code.contains("Mode::Flee"));
}

TEST_CASE("Codegen EnTT: conditional expressions are accepted in rule clauses",
          "[codegen-entt][conditional-expressions]") {
    const auto code = conditional_codegen(
        "rule R:\n"
        "    each:\n"
        "        Actor as a\n"
        "    order by:\n"
        "        if a.hp > 0: a.speed else: 0.0 desc\n"
        "    where:\n"
        "        if a.hp > 0: a.speed > 1.0 else: false\n"
        "    when:\n"
        "        if FAST: STEP > 1.0 else: false\n"
        "    on tick:\n"
        "        a.hp += 1\n");
    CHECK(!code.contains("unsupported expr"));
    CHECK(count_occurrences(code, "? (a.speed > 1.0F) : false)") >= 1);
}

TEST_CASE("Codegen EnTT: only the selected branch evaluates its call", "[codegen-entt][conditional-expressions]") {
    const auto code = conditional_codegen(
        "rule R:\n"
        "    each:\n"
        "        Actor as a\n"
        "    on tick:\n"
        "        a.hp = if a.speed > 1.0: pick() else: 0\n"
        "        a.speed = match a.hp:\n"
        "            0 => 1.0\n"
        "            _ => STEP\n"
        "        a.hp = match a.mode:\n"
        "            Mode.Idle => pick()\n"
        "            _ => 2\n");
    // C++ evaluates only the chosen operand of `?:`, and a returned arm only when its test passed.
    CHECK(code.contains("((a.speed > 1.0F) ? pick() : 0)"));
    CHECK(code.contains("== Mode::Idle) { return pick(); } return 2; }()"));
}

TEST_CASE("Codegen EnTT: an expression form with no lowering is a located error, not placeholder text",
          "[codegen-entt][conditional-expressions]") {
    const SourceLocation location("game.cactus", 7, 12);
    const ExprNode self_expr(ExprNode::Variant{SelfExpr{.location = location}}, location);
    try {
        (void)EnttCodegenUtils::emit_expr(self_expr);
        FAIL("emit_expr accepted an expression form it cannot lower");
    } catch (const CodegenError& error) {
        CHECK(error.location() == location);
        CHECK(std::string(error.what()).contains("cannot be lowered"));
    }
}

static std::string conditional_render_pass_codegen(const std::string& fragment_body) {
    ImportedSymbols passes_syms;
    passes_syms.module_name = "std.render.passes";
    ResolvedEnum pass_enum;
    pass_enum.name            = "Pass";
    pass_enum.variants        = {"Quads"};
    passes_syms.enums["Pass"] = pass_enum;
    ResolvedEnum target_enum;
    target_enum.name            = "Target";
    target_enum.variants        = {"Screen"};
    passes_syms.enums["Target"] = target_enum;
    ModuleImports imports;
    imports.add("passes", std::move(passes_syms));

    ProgramNode program;
    auto decorated = full_pipeline(
        "module render_pass_conditionals\n"
        "\n"
        "use std.render.passes as passes\n"
        "\n"
        "extern event Tick:\n"
        "    dt: float\n"
        "\n"
        "const:\n"
        "    LAYER = 2\n"
        "\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "\n"
        "pub phase my_pass:\n"
        "    from:\n"
        "        Tick\n"
        "    pipeline: passes.Pass = passes.Pass.Quads\n"
        "    output: passes.Target = passes.Target.Screen\n"
        "\n"
        "rule MyVertex:\n"
        "    each:\n"
        "        WorldTransform as xf\n"
        "\n"
        "    on my_pass.vertex as v:\n"
        "        v.screen_position = xf.position + v.corner\n"
        "        v.uv_out = v.uv\n"
        "        v.tint_out = #FFFFFFFF\n"
        "\n"
        "rule MyFragment:\n"
        "    on my_pass.fragment as f:\n" +
            fragment_body,
        program,
        imports);
    return CppEnttCodegen::generate(decorated);
}

TEST_CASE("Codegen EnTT: render-pass fragment handler lowers an if expression to GLSL",
          "[codegen-entt][render-passes][conditional-expressions]") {
    const auto code = conditional_render_pass_codegen(
        "        let k = if f.uv.x > 0.5: 1.0 else: 0.0\n"
        "        f.frag_color = f.tint * k\n");
    CHECK(code.contains("float let_k = ((uv.x > 0.5) ? 1.0 : 0.0);"));
}

TEST_CASE("Codegen EnTT: render-pass fragment handler lowers a match expression to GLSL",
          "[codegen-entt][render-passes][conditional-expressions]") {
    const auto code = conditional_render_pass_codegen(
        "        let k = match LAYER:\n"
        "            1 => 0.25\n"
        "            _ => 0.5\n"
        "        f.frag_color = f.tint * k\n");
    CHECK(code.contains("float let_k = ((2.0 == 1.0) ? 0.25 : 0.5);"));
}

TEST_CASE("Codegen EnTT: render-pass fragment handler lowers a value match statement to GLSL",
          "[codegen-entt][render-passes][conditional-expressions]") {
    const auto code = conditional_render_pass_codegen(
        "        match LAYER:\n"
        "            2 =>\n"
        "                f.frag_color = f.tint\n"
        "            _ =>\n"
        "                f.frag_color = #000000FF\n");
    CHECK(code.contains("if ((match_subject_"));
    CHECK(code.contains("== 2.0)) {"));
    CHECK(code.contains("else {"));
}

TEST_CASE("Codegen EnTT: render-pass int division and remainder call floor helpers in GLSL",
          "[codegen-entt][render-passes][numeric-semantics]") {
    const auto code = conditional_render_pass_codegen(
        "        let q = LAYER / 0\n"
        "        let r = -7 % 2\n"
        "        f.frag_color = f.tint * (1.0 * q * r)\n");
    CHECK(code.contains("float cactus_div(float a, float b)"));
    CHECK(code.contains("float cactus_mod(float a, float b)"));
    CHECK(code.contains("float let_q = cactus_div(2.0, 0.0);"));
    CHECK(code.contains("float let_r = cactus_mod((-7.0), 2.0);"));
    CHECK(code.find("float cactus_div(float a, float b)") < code.find("float let_q"));
}

TEST_CASE("Codegen EnTT: render-pass float division stays a plain GLSL operator",
          "[codegen-entt][render-passes][numeric-semantics]") {
    const auto code = conditional_render_pass_codegen(
        "        let k = f.uv.x / 2.0\n"
        "        f.frag_color = f.tint * k\n");
    CHECK(code.contains("float let_k = (uv.x / 2.0);"));
    CHECK(!code.contains("cactus_div"));
}

// ── Limit clause (dsl-rule-limit) ───────────────────────────────────────────

static const std::string& limit_traits() {
    static const std::string value =
        "event tick:\n"
        "    dt: float\n"
        "trait Actor:\n"
        "    var ground: int = 0\n"
        "trait Surface:\n"
        "    var code: int = 0\n";
    return value;
}

TEST_CASE("Codegen EnTT: a pair sort key that reads one binding marks both parameters maybe-unused",
          "[codegen-entt][rule-order-by][pair-relations]") {
    const auto code = emit_named_rule(limit_traits() +
                                          "rule Rank:\n"
                                          "    pairs:\n"
                                          "        actor:\n"
                                          "            Actor\n"
                                          "        surface:\n"
                                          "            Surface\n"
                                          "    order by:\n"
                                          "        surface[Surface].code desc\n"
                                          "    on tick:\n"
                                          "        let x = 1\n",
                                      "Rank");
    CHECK(code.contains("[&]([[maybe_unused]] entt::entity actor, [[maybe_unused]] entt::entity surface)"));
}

TEST_CASE("Codegen EnTT: a grouped reduction folds broad-phase rows and writes its retained binding",
          "[codegen-entt][rule-reduce][spatial-join]") {
    const auto code = emit_named_rule("module std.collision.volume\n" + volume_join_traits() +
                                          "rule Sense:\n"
                                          "    pairs:\n"
                                          "        actor:\n"
                                          "            Body\n"
                                          "        wall:\n"
                                          "            Solid\n"
                                          "    where:\n"
                                          "        actor[Body].radius > 0.0\n"
                                          "        sphere_box_overlap(actor[Body].position, actor[Body].radius + PROBE, "
                                          "wall[Solid].position, wall[Solid].size, wall[Solid].rotation)\n"
                                          "    reduce per actor:\n"
                                          "        near = any(wall[Solid].size.x > 1.0)\n"
                                          "        walls = count()\n"
                                          "        low = min(wall[Solid].position.y, default = 9.0)\n"
                                          "    on tick:\n"
                                          "        if near:\n"
                                          "            actor[Body].radius = low + walls\n",
                                      "Sense");
    // Groups and their filter come first, then the broad phase feeds the fold.
    const auto groups = code.find("cactus_group_live[cactus_i] = 1;");
    const auto fold   = code.find("auto cactus_fold = [&]([[maybe_unused]] entt::entity actor, [[maybe_unused]] entt::entity wall)");
    const auto sap    = code.find("cactus::runtime::entt_backend::sphere_proxy(");
    const auto rows   = code.find("std::vector<cactus_reduce_row> cactus_rows;");
    REQUIRE(groups != std::string::npos);
    REQUIRE(fold != std::string::npos);
    REQUIRE(sap != std::string::npos);
    REQUIRE(rows != std::string::npos);
    CHECK(groups < fold);
    CHECK(fold < sap);
    CHECK(sap < rows);
    CHECK(code.find("cactus_fold(__sap_proxies[__sap_pair.left].entity", sap) != std::string::npos);
    // The group filter runs once per actor, not per row.
    const auto group_filter = code.find("registry.get<const Body>(actor).radius > 0.0");
    CHECK(group_filter < fold);
    CHECK(code.find("registry.get<const Body>(actor).radius > 0.0", fold) == std::string::npos);
    // Folds use the runtime helpers; min's default applies only to empty input.
    CHECK(code.contains("cactus_acc.walls = cactus::num::add(cactus_acc.walls, 1);"));
    CHECK(code.contains("cactus::runtime::reduce::min(cactus_acc.low, cactus_value)"));
    CHECK(code.contains("cactus_acc.low_seen ? cactus_acc.low : 9.0F;"));
    CHECK(code.contains("if (!cactus_acc.near) {"));
    // The handler writes the retained binding through a mutable reference.
    CHECK(code.find("registry.get<Body>(actor).radius =", rows) != std::string::npos);
    // With no group there is no handler run, so the pass skips the fold and
    // never snapshots the other bindings.
    const auto empty = code.find("if (actor_snapshot.empty()) {");
    REQUIRE(empty != std::string::npos);
    CHECK(empty < groups);
    CHECK(code.find("actor_snapshot.reserve") < empty);
    CHECK(empty < code.find("wall_snapshot.reserve"));
    // Spawned entities have huge indices, so groups are found by snapshot position.
    CHECK(code.find("SnapshotPositions cactus_positions_actor(actor_snapshot);") < groups);
    CHECK(code.find("cactus_positions_actor.find(actor)", fold) != std::string::npos);
    CHECK(!code.contains("entt::to_entity"));
}

TEST_CASE("Codegen EnTT: a global unary reduction folds in creation order and runs once",
          "[codegen-entt][rule-reduce]") {
    const auto code = emit_named_rule(limit_traits() +
                                          "rule Total:\n"
                                          "    each:\n"
                                          "        Surface as surface\n"
                                          "    where:\n"
                                          "        surface.code > 0\n"
                                          "    reduce:\n"
                                          "        total = sum(surface.code)\n"
                                          "    limit: 1\n"
                                          "    on tick:\n"
                                          "        let x = total\n",
                                      "Total");
    const auto sort = code.find("cactus::runtime::entt_backend::creation_ordered(registry, view)");
    const auto fold = code.find("cactus_acc.total.add(");
    REQUIRE(sort != std::string::npos);
    REQUIRE(fold != std::string::npos);
    CHECK(sort < fold);
    CHECK(code.contains("cactus_rows.resize("));
    CHECK(code.contains("cactus_reduced_body(cactus_row);"));
}

TEST_CASE("Codegen EnTT: a provably-one per-binding limit emits a mutable write through its binding",
          "[codegen-entt][rule-limit][pair-relations]") {
    const auto code = emit_named_rule(limit_traits() +
                                          "rule Ground:\n"
                                          "    pairs:\n"
                                          "        actor:\n"
                                          "            Actor\n"
                                          "        surface:\n"
                                          "            Surface\n"
                                          "    limit: 1 per actor\n"
                                          "    on tick:\n"
                                          "        actor[Actor].ground = surface[Surface].code\n",
                                      "Ground");

    // The assignment target must be a mutable component reference; the
    // non-limited binding it reads from stays const.
    CHECK(code.contains("registry.get<Actor>(actor).ground ="));
    CHECK(code.contains("registry.get<const Surface>(surface).code"));
    CHECK(!code.contains("registry.get<const Actor>(actor)"));
}

TEST_CASE("Codegen EnTT: a pair binding under a non-provable limit keeps read-only access",
          "[codegen-entt][rule-limit][pair-relations]") {
    const auto code = emit_named_rule(limit_traits() +
                                          "rule Ground:\n"
                                          "    pairs:\n"
                                          "        actor:\n"
                                          "            Actor\n"
                                          "        surface:\n"
                                          "            Surface\n"
                                          "    limit: 2 per actor\n"
                                          "    on tick:\n"
                                          "        let x = actor[Actor].ground\n",
                                      "Ground");

    CHECK(code.contains("registry.get<const Actor>(actor)"));
    CHECK(!code.contains("registry.get<Actor>(actor)"));
}

// Task 6.3's regression guard, checked directly rather than inferred: a rule
// with `where:` but no `limit:` must still go through the full-desugar path,
// so no separate predicate lambda appears and the body carries the guard.
TEST_CASE("Codegen EnTT: a where: rule without a limit keeps the desugared body guard",
          "[codegen-entt][rule-limit][where-clause]") {
    const auto code = emit_named_rule(limit_traits() +
                                          "rule Ground:\n"
                                          "    pairs:\n"
                                          "        actor:\n"
                                          "            Actor\n"
                                          "        surface:\n"
                                          "            Surface\n"
                                          "    where:\n"
                                          "        actor[Actor].ground == 0\n"
                                          "    on tick:\n"
                                          "        let x = surface[Surface].code\n",
                                      "Ground");

    CHECK(!code.contains("cactus_gen_pair_where_"));
    CHECK(code.contains("if (!("));
}

// Recipient-targeted delivery is a separate runtime branch from the broadcast
// view loop a global `limit:` truncates; without its own guard, a live
// recipient would always activate regardless of the count (e.g. `limit: 0`
// would still deliver to a directly targeted entity).
TEST_CASE("Codegen EnTT: a global limit also gates recipient-targeted delivery",
          "[codegen-entt][rule-limit]") {
    const auto code = emit_named_rule(limit_traits() +
                                          "rule TakeOne:\n"
                                          "    each:\n"
                                          "        Actor as a\n"
                                          "    limit: 1\n"
                                          "    on tick:\n"
                                          "        let x = 1\n",
                                      "TakeOne");

    const auto recipient_start = code.find("if (cactus_recipient.has_value())");
    REQUIRE(recipient_start != std::string::npos);
    const auto broadcast_start = code.find("} else {", recipient_start);
    REQUIRE(broadcast_start != std::string::npos);
    const auto recipient_block = code.substr(recipient_start, broadcast_start - recipient_start);
    CHECK(recipient_block.contains(" > 0)"));
}

// ── Linearization harness (handler-execution-graph) ─────────────────────────

static std::string linearization_codegen() {
    ProgramNode program;
    auto decorated = full_pipeline(lifecycle_program_prefix() +
                                       "event Hit\n"
                                       "trait Speed:\n"
                                       "    var value: float = 0.0\n"
                                       "rule Heat:\n"
                                       "    each:\n"
                                       "        Burning as b\n"
                                       "    on tick:\n"
                                       "        b.intensity = b.intensity + 1.0\n"
                                       "rule Age:\n"
                                       "    each:\n"
                                       "        Dying as d\n"
                                       "    on tick:\n"
                                       "        d.elapsed = d.elapsed + tick.dt\n"
                                       "rule Spread:\n"
                                       "    each:\n"
                                       "        Burning as b\n"
                                       "        Speed as s\n"
                                       "    on tick:\n"
                                       "        s.value = b.intensity\n"
                                       "rule HitA:\n"
                                       "    each:\n"
                                       "        Speed as s\n"
                                       "    on Hit:\n"
                                       "        s.value = 1.0\n"
                                       "rule HitB:\n"
                                       "    each:\n"
                                       "        Speed as s\n"
                                       "    after:\n"
                                       "        HitA\n"
                                       "    on Hit:\n"
                                       "        s.value = 2.0\n"
                                       "rule AddedA:\n"
                                       "    each:\n"
                                       "        Speed as s\n"
                                       "    on added Burning as b:\n"
                                       "        s.value = 3.0\n"
                                       "rule AddedB:\n"
                                       "    each:\n"
                                       "        Dying as d\n"
                                       "    on added Burning as b:\n"
                                       "        d.elapsed = 0.0\n",
                                   program);
    return CppEnttCodegen::generate(decorated);
}

TEST_CASE("Codegen EnTT: each dispatch set gets a harness call table with predecessor edges",
          "[codegen-entt][handler-execution-graph]") {
    const auto code = linearization_codegen();
    CHECK(code.contains("#include \"backends/cpp-entt/linearization_harness.hpp\""));

    const auto tick = generated_function(code, "void generated_dispatch_phase_test__tick(entt::registry& registry");
    CHECK(tick.contains("dispatch_linear_extension(3, kLinearizationEdges, [&](std::size_t cactus_index) {"));
    // Heat writes what Spread reads: one edge, from Heat's slot to Spread's.
    CHECK(tick.contains("std::array<cactus::runtime::entt_backend::LinearizationEdge, 1> kLinearizationEdges{{"));
    CHECK(tick.contains("{0, 2}"));

    const auto hit = generated_function(code, "void generated_dispatch_event(entt::registry& registry, const HitEvent&");
    CHECK(hit.contains("dispatch_linear_extension(2, kLinearizationEdges,"));
    CHECK(hit.contains("{0, 1}"));

    const auto added = generated_function(code, "TraitAdded<Burning>& occurrence");
    CHECK(added.contains("dispatch_linear_extension(2, kLinearizationEdges,"));
    CHECK(added.contains("std::array<cactus::runtime::entt_backend::LinearizationEdge, 0> kLinearizationEdges{};"));
}

TEST_CASE("Codegen EnTT: without the harness macro dispatch calls handlers in stable order",
          "[codegen-entt][handler-execution-graph]") {
    const auto plain = without_linearization_harness(linearization_codegen());
    CHECK_FALSE(plain.contains("CACTUS_LINEARIZATION_HARNESS"));
    CHECK_FALSE(plain.contains("dispatch_linear_extension"));
    CHECK_FALSE(plain.contains("generated_world_digest"));
    const auto tick  = generated_function(plain, "void generated_dispatch_phase_test__tick(entt::registry& registry");
    const auto heat  = tick.find("::heat_tick(registry, phase);");
    const auto age   = tick.find("::age_tick(registry, phase);");
    const auto spread = tick.find("::spread_tick(registry, phase);");
    REQUIRE(heat != std::string::npos);
    REQUIRE(age != std::string::npos);
    REQUIRE(spread != std::string::npos);
    CHECK(heat < spread);
}

TEST_CASE("Codegen EnTT: the harness digests every trait of every entity in creation order",
          "[codegen-entt][handler-execution-graph]") {
    const auto code   = linearization_codegen();
    const auto digest = generated_function(code, "std::uint64_t generated_world_digest(entt::registry& registry)");
    CHECK(digest.contains("creation_ordered(registry, registry.view<CreationOrdinal>())"));
    // Traits in canonical order; markers hash presence only.
    const auto burning = digest.find("registry.try_get<Burning>(entity)");
    const auto dying   = digest.find("registry.try_get<Dying>(entity)");
    const auto enemy   = digest.find("registry.all_of<Enemy>(entity)");
    REQUIRE(burning != std::string::npos);
    REQUIRE(dying != std::string::npos);
    REQUIRE(enemy != std::string::npos);
    CHECK(burning < dying);
    CHECK(dying < enemy);
    CHECK(digest.contains("digest.add(component->intensity);"));
}

// ── Creation-order iteration (dsl-rule-order-by) ────────────────────────────

static const std::string& iteration_order_traits() {
    static const std::string value = limit_traits() +
                                     "event Seen:\n"
                                     "    code: int\n"
                                     "trait Board:\n"
                                     "    var last: int = 0\n"
                                     "entity Game:\n"
                                     "    Board\n";
    return value;
}

static constexpr const char* kCreationOrderedLoop =
    "for (auto entity : cactus::runtime::entt_backend::creation_ordered(registry, view))";

TEST_CASE("Codegen EnTT: a unary pass that observes its order iterates in creation order",
          "[codegen-entt][rule-order-by]") {
    const auto code = emit_named_rule(iteration_order_traits() +
                                          "rule Announce:\n"
                                          "    each:\n"
                                          "        Surface as surface\n"
                                          "    on tick:\n"
                                          "        emit Seen:\n"
                                          "            code = surface.code\n",
                                      "Announce");
    CHECK(code.contains(kCreationOrderedLoop));
    CHECK_FALSE(code.contains("view.each("));
}

TEST_CASE("Codegen EnTT: a limit without order by takes rows in creation order", "[codegen-entt][rule-order-by]") {
    const auto code = emit_named_rule(iteration_order_traits() +
                                          "rule Oldest:\n"
                                          "    each:\n"
                                          "        Surface as surface\n"
                                          "    limit: 2\n"
                                          "    on tick:\n"
                                          "        surface.code = 1\n",
                                      "Oldest");
    const auto loop  = code.find(kCreationOrderedLoop);
    const auto taken = code.find("int cactus_gen_taken_");
    REQUIRE(loop != std::string::npos);
    REQUIRE(taken != std::string::npos);
    CHECK(taken < loop);
    CHECK(code.find(">= cactus_gen_limit_", loop) != std::string::npos);
}

TEST_CASE("Codegen EnTT: a self-only pass keeps unordered view iteration", "[codegen-entt][rule-order-by]") {
    const auto code = emit_named_rule(iteration_order_traits() +
                                          "rule Step:\n"
                                          "    each:\n"
                                          "        Surface as surface\n"
                                          "    on tick:\n"
                                          "        surface.code += 1\n",
                                      "Step");
    CHECK(code.contains("view.each("));
    CHECK_FALSE(code.contains("creation_ordered"));
}

TEST_CASE("Codegen EnTT: an order by pass keeps its own sort", "[codegen-entt][rule-order-by]") {
    const auto code = emit_named_rule(iteration_order_traits() +
                                          "rule Ranked:\n"
                                          "    each:\n"
                                          "        Surface as surface\n"
                                          "    order by:\n"
                                          "        surface.code desc\n"
                                          "    on tick:\n"
                                          "        Game[Board].last = surface.code\n",
                                      "Ranked");
    CHECK(code.contains(".sort_as("));
    CHECK(code.contains("view.each("));
    CHECK_FALSE(code.contains("creation_ordered"));
}

TEST_CASE("Codegen EnTT: a creation-ordered pass still delivers a targeted event to one recipient",
          "[codegen-entt][rule-order-by]") {
    const auto code = emit_named_rule(iteration_order_traits() +
                                          "rule Record:\n"
                                          "    each:\n"
                                          "        Surface as surface\n"
                                          "    on Seen:\n"
                                          "        Game[Board].last = surface.code\n",
                                      "Record");
    const auto recipient = code.find("if (cactus_recipient.has_value()) {");
    const auto broadcast = code.find("} else {", recipient);
    REQUIRE(recipient != std::string::npos);
    REQUIRE(broadcast != std::string::npos);
    CHECK(code.find("entt::entity entity = *cactus_recipient;", recipient) < broadcast);
    CHECK(code.find(kCreationOrderedLoop, broadcast) != std::string::npos);
}

// ── Named entity access (dsl-named-entity-access, dsl-when-clause) ─────────

static const std::string& named_codegen_source() {
    static const std::string value =
        "event tick:\n"
        "    dt: float\n"
        "event Hit\n"
        "trait Match:\n"
        "    var over: bool = false\n"
        "    var score: int = 0\n"
        "trait Stats:\n"
        "    var ticks: int = 0\n"
        "trait Enemy:\n"
        "    var steps: int = 0\n"
        "trait Rival:\n"
        "    var rival: entity_id\n"
        "entity Game:\n"
        "    Match\n"
        "    Stats\n"
        "entity Nemesis:\n"
        "    Rival:\n"
        "        rival = Boss\n"
        "entity Boss:\n"
        "    Rival:\n"
        "        rival = Nemesis\n"
        "rule Move:\n"
        "    each:\n"
        "        Enemy as enemy\n"
        "    when:\n"
        "        not Game[Match].over\n"
        "    on tick:\n"
        "        enemy.steps += 1\n"
        "        Game[Stats].ticks += 1\n"
        "rule Score:\n"
        "    on Hit:\n"
        "        Game[Match].score += 1\n"
        "        emit Hit to Boss\n";
    return value;
}

static std::string named_codegen(DecoratedProgram& decorated, ProgramNode& program) {
    decorated     = full_pipeline(named_codegen_source(), program);
    decorated.ast = &program;
    return CppEnttCodegen::generate(decorated);
}

static std::string named_rule_code(const std::string& rule_name) {
    ProgramNode program;
    auto decorated = full_pipeline(named_codegen_source(), program);
    for (auto& decl : program.declarations) {
        auto* sys = std::get_if<RuleNode>(&decl);
        if (sys != nullptr && sys->name == rule_name) {
            return EnttSystemEmitter::emit_system(*sys, decorated);
        }
    }
    FAIL("rule '" + rule_name + "' not found");
    return {};
}

TEST_CASE("Codegen EnTT: each declared entity gets a named slot", "[codegen-entt][named-entity]") {
    ProgramNode program;
    DecoratedProgram decorated;
    const auto code = named_codegen(decorated, program);
    for (const auto* name : {"Game", "Nemesis", "Boss"}) {
        CHECK(count_occurrences(code,
                                std::string("    entt::entity test__") + name + " = entt::null;") ==
              1);
    }
}

TEST_CASE("Codegen EnTT: module entities are allocated before any initializer runs", "[codegen-entt][named-entity]") {
    ProgramNode program;
    DecoratedProgram decorated;
    const auto code = named_codegen(decorated, program);
    const auto init = generated_function(code, "void generated_init_project(entt::registry& registry)");
    REQUIRE_FALSE(init.empty());
    const auto last_allocation =
        init.find("generated_named_slots().test__Boss = cactus::runtime::entt_backend::create_entity(registry);");
    const auto first_create = init.find("(registry, generated_named_slots().test__Game);");
    REQUIRE(last_allocation != std::string::npos);
    REQUIRE(first_create != std::string::npos);
    CHECK(last_allocation < first_create);
    CHECK(init.find("generated_named_slots().test__Nemesis = cactus::runtime::entt_backend::create_entity(registry);") <
          last_allocation);
    CHECK(init.find("(registry, generated_named_slots().test__Nemesis);") <
          init.find("(registry, generated_named_slots().test__Boss);"));
}

TEST_CASE("Codegen EnTT: an entity name as an override value reads its slot", "[codegen-entt][named-entity]") {
    ProgramNode program;
    DecoratedProgram decorated;
    const auto code = named_codegen(decorated, program);
    CHECK(code.contains("component.rival = generated_named_slots().test__Boss;"));
    CHECK(code.contains("component.rival = generated_named_slots().test__Nemesis;"));
}

TEST_CASE("Codegen EnTT: a named handler guards the pass, checks when:, and hoists trait references",
          "[codegen-entt][named-entity][when-clause]") {
    const auto code = named_rule_code("Move");
    INFO(code);
    const auto guard = code.find(
        "if (!cactus::runtime::entt_backend::named_alive<Stats, Match>(registry, generated_named_slots().test__Game)) "
        "{");
    const auto match_ref =
        code.find("auto& named_ref__test__Game__Match = registry.get<Match>(generated_named_slots().test__Game);");
    const auto stats_ref =
        code.find("auto& named_ref__test__Game__Stats = registry.get<Stats>(generated_named_slots().test__Game);");
    const auto when      = code.find("if (!(!named_ref__test__Game__Match.over)) {");
    const auto loop      = code.find("steps");
    REQUIRE(guard != std::string::npos);
    REQUIRE(match_ref != std::string::npos);
    REQUIRE(stats_ref != std::string::npos);
    REQUIRE(when != std::string::npos);
    CHECK(guard < match_ref);
    CHECK(match_ref < when);
    CHECK(when < loop);
    CHECK(code.contains("named_ref__test__Game__Stats.ticks = cactus::num::add(named_ref__test__Game__Stats.ticks, 1);"));
    CHECK(count_occurrences(code, "registry.get<Match>(generated_named_slots().test__Game)") == 1);
}

TEST_CASE("Codegen EnTT: a named read in a limited rule's where: is guarded and hoisted",
          "[codegen-entt][named-entity][rule-limit]") {
    ProgramNode program;
    auto decorated = full_pipeline(named_codegen_source() +
                                       "rule Pick:\n"
                                       "    each:\n"
                                       "        Enemy as enemy\n"
                                       "    after:\n"
                                       "        Move\n"
                                       "    where:\n"
                                       "        enemy.steps < Game[Stats].ticks\n"
                                       "    limit: 3\n"
                                       "    on tick:\n"
                                       "        enemy.steps += 1\n",
                                   program);
    std::string code;
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<RuleNode>(&decl); sys != nullptr && sys->name == "Pick") {
            code = EnttSystemEmitter::emit_system(*sys, decorated);
        }
    }
    INFO(code);
    CHECK(code.contains("named_alive<Stats>(registry, generated_named_slots().test__Game)"));
    const auto declared = code.find("auto& named_ref__test__Game__Stats = registry.get<Stats>(");
    const auto used     = code.find("< named_ref__test__Game__Stats.ticks");
    REQUIRE(declared != std::string::npos);
    REQUIRE(used != std::string::npos);
    CHECK(declared < used);
}

TEST_CASE("Codegen EnTT: an event handler writes by name and targets a name", "[codegen-entt][named-entity]") {
    const auto code = named_rule_code("Score");
    CHECK(code.contains("named_alive<Match>(registry, generated_named_slots().test__Game)"));
    CHECK(code.contains("named_ref__test__Game__Match.score = cactus::num::add(named_ref__test__Game__Match.score, 1);"));
    CHECK(code.contains("generated_named_slots().test__Boss"));
    CHECK(!code.contains("named_alive<Rival>"));
}

// ── Exclusive states ────────────────────────────────────────────────────────

static const std::string& state_program_prefix() {
    static const std::string value =
        "pub extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "trait Log:\n"
        "    var n: int = 0\n"
        "state EnemyMode:\n"
        "    Idle\n"
        "    Chasing:\n"
        "        var speed: float = 2.0\n"
        "    Dying final:\n"
        "        var elapsed: float = 0.0\n";
    return value;
}

TEST_CASE("Codegen EnTT: a state emits variant components, a slot and its final mask",
          "[codegen-entt][exclusive-states]") {
    ProgramNode program;
    auto decorated  = full_pipeline(state_program_prefix(), program);
    const auto code = CppEnttCodegen::generate(decorated);

    CHECK(code.contains("struct EnemyMode__Idle {};"));
    CHECK(code.contains("struct EnemyMode__Chasing {\n    float speed"));
    CHECK(
        code.contains("struct EnemyMode {\n    std::uint8_t index = 0;\n"
                      "    static constexpr std::uint64_t final_mask = 0x4;\n};"));

    const auto remove_fn = generated_function(code, "inline void remove_state_variant<EnemyMode>(");
    CHECK(remove_fn.contains("case 0:\n            registry.remove<EnemyMode__Idle>(entity);"));
    CHECK(remove_fn.contains("case 2:\n            registry.remove<EnemyMode__Dying>(entity);"));
}

TEST_CASE("Codegen EnTT: add of a variant swaps and remove of a state leaves it", "[codegen-entt][exclusive-states]") {
    ProgramNode program;
    auto decorated  = full_pipeline(state_program_prefix() +
                                        "entity Grunt:\n"
                                        "    EnemyMode\n"
                                        "    Log\n"
                                        "rule Hunt:\n"
                                        "    each:\n"
                                        "        Log\n"
                                        "    on tick:\n"
                                        "        add EnemyMode.Chasing:\n"
                                        "            speed = 3.0\n"
                                        "        add EnemyMode.Dying\n"
                                        "        remove EnemyMode\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto handler = generated_function(code, "void hunt_tick(entt::registry& registry");
    CHECK(handler.contains("cactus::runtime::entt_backend::enter_state<EnemyMode, 1>(registry, "));
    CHECK(handler.contains("cactus::runtime::entt_backend::enter_state<EnemyMode, 2>(registry, "));
    CHECK(handler.contains("cactus::runtime::entt_backend::leave_state<EnemyMode>(registry, "));
    CHECK(!handler.contains("registry.emplace_or_replace<EnemyMode__Chasing>"));
    CHECK(!handler.contains("registry.remove<EnemyMode>"));

    CHECK(code.contains(
        "cactus::runtime::entt_backend::enter_state<EnemyMode, 0>(registry, entity, EnemyMode__Idle{});"));
}

TEST_CASE("Codegen EnTT: a state match switches on the slot index", "[codegen-entt][exclusive-states]") {
    ProgramNode program;
    auto decorated  = full_pipeline(state_program_prefix() +
                                        "rule Inspect:\n"
                                        "    each:\n"
                                        "        EnemyMode as m\n"
                                        "        Log as log\n"
                                        "    on tick:\n"
                                        "        match m:\n"
                                        "            EnemyMode.Chasing as c =>\n"
                                        "                c.speed = 1.0\n"
                                        "            _ =>\n"
                                        "                log.n = 1\n"
                                        "        log.n = match m:\n"
                                        "            EnemyMode.Idle => 10\n"
                                        "            _ => 20\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto handler = generated_function(code, "void inspect_tick(entt::registry& registry");
    CHECK(handler.contains("switch (m.index) {"));
    CHECK(handler.contains("case 1: {"));
    CHECK(handler.contains("auto& c = registry.get<EnemyMode__Chasing>(entity);"));
    CHECK(handler.contains("default: {"));
    const auto subject = extract_temp_name(handler, "cactus_gen_match_subject_");
    REQUIRE_FALSE(subject.empty());
    CHECK(handler.contains("const auto " + subject + " = m.index; switch (" + subject +
                           ") { case 0: { return 10; } default: { return 20; } }"));
}

TEST_CASE("Codegen EnTT: a trait match arm without an alias only tests presence", "[codegen-entt][exclusive-states]") {
    ProgramNode program;
    auto decorated  = full_pipeline(state_program_prefix() +
                                        "trait Watch:\n"
                                        "    var subject: entity_id\n"
                                        "rule Look:\n"
                                        "    each:\n"
                                        "        Watch as w\n"
                                        "        Log as log\n"
                                        "    on tick:\n"
                                        "        match w.subject:\n"
                                        "            EnemyMode.Chasing =>\n"
                                        "                log.n = 1\n"
                                        "            _ =>\n"
                                        "                log.n = 0\n",
                                    program);
    const auto code = CppEnttCodegen::generate(decorated);

    const auto handler = generated_function(code, "void look_tick(entt::registry& registry");
    CHECK(handler.contains("if (registry.all_of<EnemyMode__Chasing>("));
    CHECK(!handler.contains("registry.try_get<EnemyMode__Chasing>"));
}

TEST_CASE("Codegen EnTT: large states widen the index and final mask", "[codegen-entt][exclusive-states]") {
    std::string source = "module test\nstate Large:\n";
    for (std::size_t index = 0; index < 257; ++index) {
        source += "    V" + std::to_string(index) + (index == 64 || index == 256 ? " final\n" : "\n");
    }
    ProgramNode program;
    const auto decorated = full_pipeline(source, program);
    const auto code      = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("std::size_t index = 0;"));
    CHECK(code.contains("std::array<std::uint64_t, 5> final_mask{0x0, 0x1, 0x0, 0x0, 0x1}"));
}

TEST_CASE("Codegen: compact domains normalize to the same native strategy", "[codegen][concise]") {
    const std::string declarations = R"(module notation
pub event tick
trait Position:
    var x: float = 0.0
trait Motion:
    var speed: float = 1.0
trait Frozen
)";
    const std::string compact      = R"(rule Move:
    each: Position as p, Motion as m
    without: Frozen
    order by: p.x desc, m.speed
    where: m.speed > 0.0, p.x >= 0.0
    when: true, not false
    on tick:
        p.x += m.speed
)";
    const std::string block        = R"(rule Move:
    each:
        Position as p
        Motion as m
    without:
        Frozen
    order by:
        p.x desc
        m.speed
    where:
        m.speed > 0.0
        p.x >= 0.0
    when:
        true
        not false
    on tick:
        p.x += m.speed
)";
    const auto generate            = [&declarations](const std::string& rules) {
        ErrorReporter errors;
        Lexer lexer(declarations + rules, "notation.cactus", errors);
        auto tokens = lexer.tokenize();
        for (auto& token : tokens) {
            token.location = SourceLocation{"notation.cactus", 1, 1};
        }
        Parser parser(std::move(tokens), errors);
        auto ast = parser.parse_program();
        REQUIRE_FALSE(errors.has_errors());
        SemanticAnalyzer analyzer(errors);
        auto program = analyzer.analyze(ast);
        REQUIRE_FALSE(errors.has_errors());
        return CppEnttCodegen::generate(program);
    };
    CHECK(generate(compact) == generate(block));
}
// ── Total integer arithmetic ───────────────────────────────────────────────

static const std::string& arithmetic_traits() {
    static const std::string value =
        "event tick:\n"
        "    dt: float\n"
        "trait Cell:\n"
        "    var n: int = 0\n"
        "    var d: int = 1\n"
        "    var f: float = 0.0\n"
        "    var v: vec2 = vec2(0.0, 0.0)\n";
    return value;
}

TEST_CASE("Codegen EnTT: int operators in a handler lower to total helpers", "[codegen-entt][numeric-semantics]") {
    const auto code = emit_named_rule(arithmetic_traits() +
                                          "rule R:\n"
                                          "    each:\n"
                                          "        Cell as c\n"
                                          "    on tick:\n"
                                          "        c.n = c.n + c.d - c.n * c.d / c.d % c.d\n"
                                          "        c.d = -c.n\n"
                                          "        c.n = -7\n"
                                          "        c.d = -2147483648 / c.d\n",
                                      "R");
    for (const auto* helper : {"add(", "sub(", "mul(", "div(", "mod(", "neg("}) {
        CAPTURE(helper);
        CHECK(code.contains(std::string("cactus::num::") + helper));
    }
    CHECK(code.contains("= -7;"));
    CHECK(!code.contains("cactus::num::neg(7)"));
    CHECK(code.contains("cactus::num::div((-2147483647 - 1), c.d)"));
}

TEST_CASE("Codegen EnTT: int compound assignments lower to total helpers", "[codegen-entt][numeric-semantics]") {
    const auto code = emit_named_rule(arithmetic_traits() +
                                          "rule R:\n"
                                          "    each:\n"
                                          "        Cell as c\n"
                                          "    on tick:\n"
                                          "        c.n += 2\n"
                                          "        c.n -= c.d\n"
                                          "        c.n *= 3\n"
                                          "        c.n /= c.d\n",
                                      "R");
    CHECK(code.contains("c.n = cactus::num::add(c.n, 2);"));
    CHECK(code.contains("c.n = cactus::num::sub(c.n, c.d);"));
    CHECK(code.contains("c.n = cactus::num::mul(c.n, 3);"));
    CHECK(code.contains("c.n = cactus::num::div(c.n, c.d);"));
    CHECK(!code.contains("+="));
    CHECK(!code.contains("/="));
}

TEST_CASE("Codegen EnTT: int operators in where, order by and reduce lower to total helpers",
          "[codegen-entt][numeric-semantics]") {
    const auto filtered = emit_named_rule(arithmetic_traits() +
                                              "rule R:\n"
                                              "    each:\n"
                                              "        Cell as c\n"
                                              "    where:\n"
                                              "        c.n % 2 == 0\n"
                                              "    on tick:\n"
                                              "        c.f = 1.0\n",
                                          "R");
    CHECK(filtered.contains("cactus::num::mod("));
    const auto ordered = emit_named_rule(arithmetic_traits() +
                                             "rule R:\n"
                                             "    each:\n"
                                             "        Cell as c\n"
                                             "    order by:\n"
                                             "        c.n * 2 desc\n"
                                             "    on tick:\n"
                                             "        c.f = 1.0\n",
                                         "R");
    CHECK(ordered.contains("cactus::num::mul("));
    const auto reduced = emit_named_rule(arithmetic_traits() +
                                             "rule R:\n"
                                             "    each:\n"
                                             "        Cell as c\n"
                                             "    reduce:\n"
                                             "        total = sum(c.n - 1)\n"
                                             "    on tick:\n"
                                             "        let x = total\n",
                                         "R");
    CHECK(reduced.contains("cactus::num::sub("));
}

TEST_CASE("Codegen EnTT: an int constant initializer lowers to total helpers", "[codegen-entt][numeric-semantics]") {
    ProgramNode program;
    auto decorated  = full_pipeline(arithmetic_traits() +
                                       "const:\n"
                                       "    CELL = -7 / 2\n"
                                       "    HALF = 7.0 / 2.0\n",
                                   program);
    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("CELL = cactus::num::div(-7, 2);"));
    CHECK(code.contains("HALF = (7.0F / 2.0F);"));
}

TEST_CASE("Codegen EnTT: float and vector operators stay plain operators", "[codegen-entt][numeric-semantics]") {
    const auto code = emit_named_rule(arithmetic_traits() +
                                          "rule R:\n"
                                          "    each:\n"
                                          "        Cell as c\n"
                                          "    on tick:\n"
                                          "        c.f = c.f / 2.0 + c.f * c.f - -c.f\n"
                                          "        c.f += 1.0\n"
                                          "        c.v = c.v + c.v * 2.0\n"
                                          "        c.v += c.v\n",
                                      "R");
    CHECK(!code.contains("cactus::num::"));
    CHECK(code.contains("c.f += 1.0F;"));
    CHECK(code.contains("c.v += c.v;"));
}

TEST_CASE("Codegen EnTT: the generated file sets the float mode before any other include",
          "[codegen-entt][numeric-semantics]") {
    ProgramNode program;
    auto decorated  = full_pipeline(arithmetic_traits(), program);
    const auto code = CppEnttCodegen::generate(decorated);
    const auto mode = code.find("#include \"common/float_mode.hpp\"\n");
    REQUIRE(mode != std::string::npos);
    CHECK(mode < code.find("#include", mode + 1));
    CHECK(code.find("#include") == mode);
}

TEST_CASE("Codegen EnTT: int operators in call arguments, emit payloads and template arguments use total helpers",
          "[codegen-entt][numeric-semantics]") {
    ProgramNode program;
    auto decorated  = full_pipeline(arithmetic_traits() +
                                       "event Hit:\n"
                                       "    amount: int\n"
                                       "func halve(v: int) int:\n"
                                       "    return v\n"
                                       "template Box(size: int):\n"
                                       "    Cell:\n"
                                       "        n = size / 3\n"
                                       "        d = -size\n"
                                       "entity A from Box(size = 9)\n"
                                       "rule R:\n"
                                       "    each:\n"
                                       "        Cell as c\n"
                                       "    on tick:\n"
                                       "        c.n = halve(c.n * 3)\n"
                                       "        emit Hit:\n"
                                       "            amount = c.d % 4\n",
                                   program);
    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(code.contains("halve(cactus::num::mul(c.n, 3))"));
    CHECK(code.contains("cactus::num::mod(c.d, 4)"));
    CHECK(code.contains("cactus::num::div(__cactus_template_slot_0, 3)"));
    CHECK(code.contains("cactus::num::neg(__cactus_template_slot_0)"));
}

// ── Exact float accumulation (dsl-numeric-semantics) ───────────────────────

static std::string accumulation_codegen() {
    ProgramNode program;
    auto decorated = full_pipeline(lifecycle_program_prefix() +
                                       "event Hit\n"
                                       "trait Tally:\n"
                                       "    var total: float = 0.0\n"
                                       "trait Body:\n"
                                       "    var x: float = 0.0\n"
                                       "    var y: float = 0.0\n"
                                       "trait Pair:\n"
                                       "    var a: vec2\n"
                                       "trait Seen:\n"
                                       "    var v: float = 0.0\n"
                                       "entity Game:\n"
                                       "    Tally\n"
                                       "rule Move:\n"
                                       "    each:\n"
                                       "        Body as b\n"
                                       "    on tick:\n"
                                       "        b.x += 0.1\n"
                                       "rule Count:\n"
                                       "    each:\n"
                                       "        Body\n"
                                       "    on tick:\n"
                                       "        Game[Tally].total += 1.0\n"
                                       "rule PushA:\n"
                                       "    each:\n"
                                       "        Pair as p\n"
                                       "    on tick:\n"
                                       "        p.a.x += 1.0\n"
                                       "rule PushB:\n"
                                       "    each:\n"
                                       "        Pair as p\n"
                                       "    on tick:\n"
                                       "        p.a -= vec2(2.0, 1.0)\n"
                                       "rule Bump:\n"
                                       "    each:\n"
                                       "        Body as b\n"
                                       "    on Hit:\n"
                                       "        b.y += 1.0\n"
                                       "rule Watch:\n"
                                       "    each:\n"
                                       "        Pair as p\n"
                                       "        Seen as s\n"
                                       "    on tick:\n"
                                       "        s.v = p.a.x + Game[Tally].total\n",
                                   program);
    return CppEnttCodegen::generate(decorated);
}

TEST_CASE("Codegen EnTT: a lone per-row accumulation stays a plain +=", "[codegen-entt][write-kinds]") {
    const auto code = accumulation_codegen();
    const auto move = generated_function(code, "void move_tick(entt::registry& registry");
    CHECK(move.contains("b.x += 0.1F;"));
    CHECK(!code.contains("generated_accumulation__Body__x"));
}

TEST_CASE("Codegen EnTT: accumulations that can meet are buffered", "[codegen-entt][write-kinds]") {
    const auto code = accumulation_codegen();
    // Many rows add to one named entity.
    const auto count = generated_function(code, "void count_tick(entt::registry& registry");
    CHECK(count.contains("generated_accumulation__Tally__total().add(named_ref__test__Game__Tally.total, 1.0F);"));
    // Two handlers add to one field, one through a component.
    const auto push_a = generated_function(code, "void push_a_tick(entt::registry& registry");
    CHECK(push_a.contains("generated_accumulation__Pair__a().add(p.a.x, 1.0F);"));
    const auto push_b = generated_function(code, "void push_b_tick(entt::registry& registry");
    CHECK(push_b.contains("generated_accumulation__Pair__a().subtract(p.a, "));
    // Several occurrences of one event add to one field.
    const auto bump = generated_function(code, "void bump_Hit(entt::registry& registry");
    CHECK(bump.contains("generated_accumulation__Body__y().add(b.y, 1.0F);"));
}

TEST_CASE("Codegen EnTT: a reader of a buffered field flushes it on entry", "[codegen-entt][write-kinds]") {
    const auto code     = accumulation_codegen();
    const auto dispatch = without_linearization_harness(
        generated_function(code, "void generated_dispatch_phase_test__tick(entt::registry& registry"));
    const auto flush_pair  = dispatch.find("generated_accumulation__Pair__a().flush();");
    const auto flush_tally = dispatch.find("generated_accumulation__Tally__total().flush();");
    const auto watch       = dispatch.find("::watch_tick(registry, phase);");
    REQUIRE(watch != std::string::npos);
    REQUIRE(flush_pair != std::string::npos);
    REQUIRE(flush_tally != std::string::npos);
    CHECK(flush_pair < watch);
    CHECK(flush_tally < watch);
    // Writers that only accumulate don't flush.
    CHECK(count_occurrences(dispatch, "generated_accumulation__Pair__a().flush();") == 1);
}

TEST_CASE("Codegen EnTT: the commit flushes every buffered field first", "[codegen-entt][write-kinds]") {
    const auto code   = accumulation_codegen();
    const auto commit = generated_function(code, "void generated_commit_activation");
    const auto flush  = commit.find("generated_flush_accumulations();");
    REQUIRE(flush != std::string::npos);
    CHECK(flush < commit.find("commit_activation(activation"));
    const auto flush_all = generated_function(code, "void generated_flush_accumulations()");
    for (const auto* field : {"Tally__total", "Pair__a", "Body__y"}) {
        CHECK(flush_all.contains(std::string("generated_accumulation__") + field + "().flush();"));
    }
    const auto drain = generated_function(code, "void generated_drain_event_cascade(entt::registry& registry)");
    CHECK(drain.contains("generated_flush_accumulations();"));
}

TEST_CASE("Codegen EnTT: a program without buffered accumulation emits no tables", "[codegen-entt][write-kinds]") {
    ProgramNode program;
    auto decorated   = full_pipeline(lifecycle_program_prefix() +
                                         "rule Age:\n"
                                         "    each:\n"
                                         "        Dying as d\n"
                                         "    on tick:\n"
                                         "        d.elapsed += tick.dt\n",
                                     program);
    const auto code = CppEnttCodegen::generate(decorated);
    CHECK(!code.contains("generated_accumulation"));
    CHECK(!code.contains("generated_flush_accumulations"));
}
TEST_CASE("Codegen EnTT: a handler that may move component storage flushes first", "[codegen-entt][write-kinds]") {
    ProgramNode program;
    auto decorated   = full_pipeline(lifecycle_program_prefix() +
                                         "trait Pair:\n"
                                         "    var a: float = 0.0\n"
                                         "extern func poke()\n"
                                         "rule PushA:\n"
                                         "    each:\n"
                                         "        Pair as p\n"
                                         "    on tick:\n"
                                         "        p.a += 1.0\n"
                                         "rule PushB:\n"
                                         "    each:\n"
                                         "        Pair as p\n"
                                         "    on tick:\n"
                                         "        p.a += 2.0\n"
                                         "rule Poke:\n"
                                         "    on tick:\n"
                                         "        poke()\n",
                                     program);
    const auto code     = CppEnttCodegen::generate(decorated);
    const auto dispatch = without_linearization_harness(
        generated_function(code, "void generated_dispatch_phase_test__tick(entt::registry& registry"));
    const auto flush = dispatch.find("generated_accumulation__Pair__a().flush();");
    REQUIRE(flush != std::string::npos);
    CHECK(flush < dispatch.find("::poke_tick(registry, phase);"));
}
// ── Hierarchy facts select the strategy (hierarchy-forest) ─────────────────

namespace {

const std::string kHierarchyPrelude =
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "trait Parent:\n"
    "    let parent: entity_id\n"
    "trait Hand\n"
    "trait Item\n"
    "pub extern event Grab:\n"
    "    item: entity_id\n"
    "entity Root:\n"
    "    Hand\n";

// Runs the link-time passes a build runs, so the hierarchy facts are derived.
std::string hierarchy_program(const std::string& body, ProgramNode& program) {
    ErrorReporter errors;
    Lexer lexer("module game\n" + kHierarchyPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(program);
    REQUIRE_FALSE(errors.has_errors());
    derive_archetype_reachability(decorated, errors);
    INFO((errors.diagnostics().empty() ? std::string{} : errors.diagnostics().front().message));
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(decorated.hierarchy.derived);
    return CppEnttCodegen::generate(decorated);
}

const std::string kCycleCheck = "cactus::runtime::entt_backend::reparent_would_cycle(";

}  // namespace

TEST_CASE("Codegen EnTT: a program without a reparent emits no cycle check", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    const auto code = hierarchy_program("template Arm:\n"
                                        "    Hand\n"
                                        "    children:\n"
                                        "        entity Finger:\n"
                                        "            Item\n"
                                        "            children:\n"
                                        "                entity Nail:\n"
                                        "                    Item\n"
                                        "rule Wave:\n"
                                        "    each:\n"
                                        "        Hand\n"
                                        "    on tick:\n"
                                        "        spawn Arm()\n"
                                        "rule Drop:\n"
                                        "    each:\n"
                                        "        Item\n"
                                        "    on tick:\n"
                                        "        remove Parent\n",
                                        program);
    CHECK_FALSE(code.contains(kCycleCheck));
    CHECK(code.contains("constexpr auto kCactusHierarchyStrategy = "
                        "cactus::runtime::entt_backend::HierarchyStrategy::CreationSweep;"));
    CHECK(code.contains("constexpr std::uint32_t kCactusHierarchyDepthBound = 2;"));
}

TEST_CASE("Codegen EnTT: an add Parent is checked at commit and selects depth order", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    const auto code = hierarchy_program("rule PickUp:\n"
                                        "    each:\n"
                                        "        Hand\n"
                                        "    on Grab:\n"
                                        "        add Parent to Grab.item:\n"
                                        "            parent = self\n",
                                        program);
    CHECK(count_occurrences(code, kCycleCheck) == 1);
    const auto check  = code.find(kCycleCheck);
    const auto commit = code.find("registry.emplace_or_replace<game__Parent>(", check);
    REQUIRE(commit != std::string::npos);
    CHECK(check < commit);
    CHECK(code.contains("constexpr auto kCactusHierarchyStrategy = "
                        "cactus::runtime::entt_backend::HierarchyStrategy::DepthOrder;"));
    CHECK_FALSE(code.contains("kCactusHierarchyDepthBound"));
}

TEST_CASE("Codegen EnTT: an extern add Parent capability is checked at commit", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    const auto code = hierarchy_program("extern rule HostRig:\n"
                                        "    on tick:\n"
                                        "        commands:\n"
                                        "            add Parent\n",
                                        program);
    CHECK(code.contains("if (cactus::runtime::entt_backend::reparent_would_cycle("
                        "cactus::runtime::entt_backend::hierarchy_index<game__Parent>(registry), target, value.parent)) "
                        "{ return; }"));
}

TEST_CASE("Codegen EnTT: declared entities load parent first and connect the index", "[codegen-entt][hierarchy]") {
    ProgramNode program;
    const auto code = hierarchy_program("entity Sword:\n"
                                        "    Item\n"
                                        "    Parent:\n"
                                        "        parent = Palm\n"
                                        "entity Palm:\n"
                                        "    Hand\n",
                                        program);
    const auto init = generated_function(code, "void generated_init_project(entt::registry& registry)");
    const auto connect = init.find("cactus::runtime::entt_backend::hierarchy_index<game__Parent>(registry);");
    const auto palm    = init.find("palm_at(registry");
    const auto sword   = init.find("sword_at(registry");
    REQUIRE(connect != std::string::npos);
    REQUIRE(palm != std::string::npos);
    REQUIRE(sword != std::string::npos);
    CHECK(connect < palm);
    CHECK(palm < sword);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
