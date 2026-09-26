// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/cpp_entt_codegen.hpp"
#include "backends/cpp-entt/persistence_emitter.hpp"
#include "common/persistence_schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace cactus;

namespace {

// Keeps canonical module identity intact (unlike test_codegen_entt.cpp's
// full_pipeline, which strips it to get unqualified names): provenance is
// addressed by canonical archetype node, so "world.Boss" has to survive.
struct Compilation {
    ErrorReporter errors;
    ProgramNode ast;
    DecoratedProgram program;
    std::string code;
};

std::unique_ptr<Compilation> generate(const std::string& source, const ModuleImports& imports = ModuleImports{}) {
    auto unit = std::make_unique<Compilation>();
    Lexer lexer(source, "world.cactus", unit->errors);
    Parser parser(lexer.tokenize(), unit->errors);
    unit->ast = parser.parse_program();
    REQUIRE_FALSE(unit->errors.has_errors());
    SemanticAnalyzer analyzer(unit->errors);
    unit->program = analyzer.analyze(unit->ast, imports);
    REQUIRE_FALSE(unit->errors.has_errors());
    unit->code = CppEnttCodegen::generate(unit->program);
    return unit;
}

// The body of one generated function, so a check about a particular creation
// path cannot be satisfied by an unrelated one elsewhere in the file. Skips
// forward declarations, which some creation functions also get.
std::string function_body(const std::string& code, const std::string& name) {
    for (auto start = code.find(name + "("); start != std::string::npos; start = code.find(name + "(", start + 1)) {
        const auto open = code.find('{', start);
        if (open == std::string::npos || code.find(';', start) < open) {
            continue;
        }
        const auto end = code.find("\n}", open);
        REQUIRE(end != std::string::npos);
        return code.substr(open, end - open);
    }
    FAIL("no definition of " + name + " in the generated code");
    return {};
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.contains(needle);
}

// The initializer of one generated constexpr array, so a check about a
// particular table cannot be satisfied by a neighbouring one.
std::string array_body(const std::string& code, const std::string& name) {
    const auto start = code.find(name + " = {{");
    REQUIRE(start != std::string::npos);
    const auto end = code.find("}};", start);
    REQUIRE(end != std::string::npos);
    return code.substr(start, end - start);
}

std::string retain(const std::string& trait_cpp) {
    return "cactus::runtime::entt_backend::retain_construction<" + trait_cpp + ">(registry, ";
}

std::string discard(const std::string& trait_cpp) {
    return "cactus::runtime::entt_backend::discard_construction<" + trait_cpp + ">(registry, ";
}

constexpr const char* kOrigin = "cactus::runtime::entt_backend::ArchetypeOrigin{.node = ";

constexpr const char* kDeclarations = R"(module world

event Step

trait Health:
    let maximum: int = 100
    persist var current: int = 100

trait Sparkle:
    var lifetime: float = 0.2

entity Boss:
    Health:
        maximum = 500

template Particle:
    Sparkle
)";

}  // namespace

// ── The whole-program provenance bound ──────────────────────────────────────

TEST_CASE("a program with no persist field emits no creation provenance",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

trait Sparkle:
    var lifetime: float = 0.2

template Particle:
    Sparkle
)");

    CHECK_FALSE(contains(unit->code, "ArchetypeOrigin"));
    CHECK_FALSE(contains(unit->code, "retain_construction"));
    CHECK_FALSE(contains(unit->code, "generated_archetype_nodes"));
}

TEST_CASE("an eligible archetype retains its construction values at creation",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(kDeclarations);
    const auto boss = function_body(unit->code, "create_world__boss_at");

    CHECK(contains(boss, kOrigin));
    CHECK(contains(boss, retain("world__Health")));
    // The copy has to follow the emplace it copies, or it records a default.
    CHECK(boss.find("registry.emplace<world__Health>") < boss.find(retain("world__Health")));
    CHECK(contains(unit->code, "generated_archetype_nodes"));
    CHECK(contains(unit->code, "\"world.Boss\""));
}

TEST_CASE("the nullary archetype factory routes through the provenance-retaining path",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(kDeclarations);

    // create_<X> is what load-time setup and the editor template palette call;
    // it delegates rather than being a second creation site to keep in step.
    CHECK(contains(function_body(unit->code, "create_world__boss"), "create_world__boss_at(registry"));
}

TEST_CASE("never-eligible archetypes emit no construction metadata without an add path",
          "[codegen-entt][persistence][provenance]") {
    const auto unit     = generate(kDeclarations);
    const auto particle = function_body(unit->code, "create_world__particle_at");

    CHECK_FALSE(contains(particle, "ArchetypeOrigin"));
    CHECK_FALSE(contains(particle, retain("world__Sparkle")));
    CHECK_FALSE(contains(unit->code, "\"world.Particle\""));
}

TEST_CASE("a persistence-bearing add path extends provenance to every archetype",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(std::string(kDeclarations) + R"(
rule Promote:
    filter:
        Sparkle as sparkle

    on Step:
        if sparkle.lifetime > 0.0:
            add Health
)");
    const auto particle = function_body(unit->code, "create_world__particle_at");

    CHECK(contains(particle, kOrigin));
    CHECK(contains(particle, retain("world__Sparkle")));
    CHECK(contains(unit->code, "\"world.Particle\""));
}

TEST_CASE("a persist field no archetype can ever reach leaves structural sites alone",
          "[codegen-entt][persistence][provenance]") {
    // Health is declared durable but no archetype holds it and nothing adds it,
    // so no entity is capturable and the add/remove sites must stay on the same
    // side of the bound as the creation sites.
    const auto unit = generate(R"(module world

event Step

trait Health:
    persist var current: int = 100

trait Stunned:
    var turns: int = 1

trait Sparkle:
    var lifetime: float = 0.2

template Particle:
    Sparkle

rule Stun:
    filter:
        Sparkle as sparkle

    on Step:
        if sparkle.lifetime > 0.0:
            add Stunned
        if sparkle.lifetime <= 0.0:
            remove Stunned
)");

    CHECK_FALSE(contains(unit->code, "generated_archetype_nodes"));
    CHECK_FALSE(contains(unit->code, "ArchetypeOrigin"));
    CHECK_FALSE(contains(unit->code, retain("world__Stunned")));
    CHECK_FALSE(contains(unit->code, discard("world__Stunned")));
}

TEST_CASE("adding a trait with no persist field leaves the bound down",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(std::string(kDeclarations) + R"(
trait Stunned

rule Stun:
    filter:
        Sparkle as sparkle

    on Step:
        if sparkle.lifetime > 0.0:
            add Stunned
)");
    const auto particle = function_body(unit->code, "create_world__particle_at");

    CHECK_FALSE(contains(particle, "ArchetypeOrigin"));
    CHECK_FALSE(contains(particle, retain("world__Sparkle")));
}

// ── Construction data through every creation path ───────────────────────────

TEST_CASE("a parameterized archetype retains its evaluated construction argument",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

trait Health:
    let maximum: int = 100
    persist var current: int = 100

template Enemy(maximum: int):
    Health:
        maximum = maximum
        current = maximum
)");
    const auto node = function_body(unit->code, "create_world__enemy__node_at");

    CHECK(contains(node, kOrigin));
    CHECK(contains(node, retain("world__Health")));
    CHECK(node.find("registry.emplace<world__Health>") < node.find(retain("world__Health")));
}

TEST_CASE("hierarchical nodes carry their own canonical archetype origin",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

trait Parent:
    var parent: entity_id

trait Health:
    persist var current: int = 100

trait Turret:
    persist var angle: float = 0.0

template Tank:
    Health
    children:
        entity turret:
            Turret
)");
    const auto root  = function_body(unit->code, "create_world__tank__node_at");
    const auto child = function_body(unit->code, "create_world__tank__node__turret");

    CHECK(contains(root, kOrigin));
    CHECK(contains(child, kOrigin));
    // Distinct nodes, so a saved child is not mistaken for its parent.
    CHECK(root.substr(root.find(kOrigin)) != child.substr(child.find(kOrigin)));
    CHECK(contains(unit->code, "\"world.Tank\""));
    CHECK(contains(unit->code, "\"world.Tank/turret\""));
    CHECK(contains(child, retain("world__Turret")));
}

TEST_CASE("a child node with no durable field of its own carries no provenance",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

trait Parent:
    var parent: entity_id

trait Health:
    persist var current: int = 100

trait Turret:
    var angle: float = 0.0

template Tank:
    Health
    children:
        entity turret:
            Turret
)");

    CHECK(contains(function_body(unit->code, "create_world__tank__node_at"), kOrigin));
    CHECK_FALSE(contains(function_body(unit->code, "create_world__tank__node__turret"), kOrigin));
    CHECK_FALSE(contains(unit->code, "\"world.Tank/turret\""));
}

TEST_CASE("spawn overrides are retained as construction data", "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

event Step

trait Health:
    let maximum: int = 100
    persist var current: int = 100

trait Spawner:
    var pending: int = 1

template Enemy:
    Health

entity Director:
    Spawner

rule SpawnWave:
    filter:
        Spawner as spawner

    on Step:
        if spawner.pending > 0:
            spawn Enemy:
                Health:
                    maximum = 700
)");
    const auto handler = function_body(unit->code, "world__spawn_wave_Step");

    REQUIRE(contains(handler, "emplace_or_replace<world__Health>"));
    CHECK(contains(handler, retain("world__Health")));
    CHECK(handler.rfind("emplace_or_replace<world__Health>") < handler.rfind(retain("world__Health")));
}

TEST_CASE("a runtime add records the trait's new incarnation", "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

event Step

trait Health:
    let maximum: int = 100
    persist var current: int = 100

trait Sparkle:
    var lifetime: float = 0.2

template Particle:
    Sparkle

rule Promote:
    filter:
        Sparkle as sparkle

    on Step:
        if sparkle.lifetime > 0.0:
            add Health:
                current = 5
)");
    const auto handler = function_body(unit->code, "world__promote_Step");

    CHECK(contains(handler, retain("world__Health")));
    CHECK(handler.find("emplace_or_replace<world__Health>") < handler.find(retain("world__Health")));
}

TEST_CASE("native spawn and add capabilities maintain the same provenance",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

pub extern event frame:
    dt: float

trait Health:
    let maximum: int = 100
    persist var current: int = 100

trait Sparkle:
    var lifetime: float = 0.2

template Ember:
    Sparkle

phase simulate:
    from:
        frame

extern rule Native:
    filter:
        Sparkle
    on simulate:
        reads:
            Sparkle
        commands:
            spawn Ember
            add Health
            remove Health
)");

    // The native spawn goes through the archetype factory, so it inherits that
    // path's provenance rather than needing a second one.
    CHECK(contains(unit->code, "::create_world__ember_at(registry, entity)"));
    CHECK(contains(function_body(unit->code, "create_world__ember_at"), kOrigin));

    const auto add_command = function_body(unit->code, "void command_add_world__Health");
    CHECK(contains(add_command, retain("world__Health")));
    CHECK(add_command.find("emplace_or_replace<world__Health>") < add_command.find(retain("world__Health")));

    const auto remove_command = function_body(unit->code, "void command_remove_world__Health");
    CHECK(contains(remove_command, discard("world__Health")));
}

// An entity placed via the in-game editor palette gets its
// LocalTransform/WorldTransform position at editor-click time, the same kind
// of per-entity override an authored `spawn Template: Field: value:` gets —
// this site must retain it the same way, or a saved editor-placed entity
// would report its template's baseline position instead of where it was
// actually placed.
TEST_CASE("editor-placed entity retains its placed position for persistence",
          "[codegen-entt][persistence][provenance][editor]") {
    const auto unit = generate(R"(module world

use std.editor

trait LocalTransform:
    var position: vec3
    var rotation: quat
    var scale: vec3

trait WorldTransform:
    var position: vec3
    var rotation: quat
    var scale: vec3

trait EditorLocked

trait Health:
    persist var current: int = 100

entity Marker:
    Health
)");

    const auto spawn_impl_start = unit->code.find("register_editor_spawn_impl");
    REQUIRE(spawn_impl_start != std::string::npos);
    const auto spawn_impl_end = unit->code.find("});\n", spawn_impl_start);
    REQUIRE(spawn_impl_end != std::string::npos);
    const auto spawn_impl_body = unit->code.substr(spawn_impl_start, spawn_impl_end - spawn_impl_start);

    CHECK(contains(spawn_impl_body, "retain_construction<world__LocalTransform>(reg, entity, *lt)"));
    CHECK(contains(spawn_impl_body, "retain_construction<world__WorldTransform>(reg, entity, *wt)"));
}

TEST_CASE("marker traits carry no construction payload", "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

event Step

trait Health:
    persist var current: int = 100

trait Poisoned

entity Boss:
    Health

rule Poison:
    filter:
        Health as health

    on Step:
        if health.current > 0:
            add Poisoned
)");

    CHECK_FALSE(contains(unit->code, retain("world__Poisoned")));
    CHECK(contains(unit->code, retain("world__Health")));
}

TEST_CASE("projected traits are never retained as durable construction data",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

event Step

trait Health:
    persist var current: int = 100

trait Highlight:
    var strength: float = 0.0

entity Boss:
    Health

rule Glow:
    filter:
        Health as health

    on Step:
        if health.current > 0:
            project Highlight:
                strength = 1.0
)");

    CHECK_FALSE(contains(unit->code, retain("world__Highlight")));
}

// ── Eligibility survives removal ────────────────────────────────────────────

TEST_CASE("removing an archetype's persistent trait leaves its creation provenance in place",
          "[codegen-entt][persistence][provenance]") {
    const auto unit = generate(R"(module world

event Step

trait Health:
    let maximum: int = 100
    persist var current: int = 100

entity Boss:
    Health:
        maximum = 500

rule Strip:
    filter:
        Health as health

    on Step:
        if health.current <= 0:
            remove Health
)");
    const auto boss   = function_body(unit->code, "create_world__boss_at");
    const auto strip  = function_body(unit->code, "world__strip_Step");

    CHECK(contains(boss, kOrigin));
    CHECK(contains(boss, retain("world__Health")));
    // The removed incarnation's construction copy goes with it, so a later
    // re-add cannot be read as if the original were still attached.
    CHECK(contains(strip, discard("world__Health")));
    CHECK(strip.find("registry.remove<world__Health>") < strip.find(discard("world__Health")));
}
// ── Generated schema descriptor ─────────────────────────────────────────────

namespace {

std::uint64_t fingerprint_of(const Compilation& unit) {
    return cactus::persistence_fingerprint(EnttPersistenceEmitter::canonical_schema_text(unit.program));
}

constexpr const char* kSchemaSource = R"(module world

enum Stance:
    Guarding
    Charging

struct Slot:
    count: int
    kind: Stance

trait Health:
    let maximum: int = 100
    persist var current: int = 100

trait Inventory:
    persist var slots: list[Slot]
    var scratch: float = 0.0

entity Boss:
    Health
    Inventory
)";

}  // namespace

TEST_CASE("a program with no persistence emits no schema", "[codegen-entt][persistence][schema]") {
    const auto unit = generate(R"(module world

trait Sparkle:
    var lifetime: float = 0.2

template Particle:
    Sparkle
)");

    CHECK_FALSE(contains(unit->code, "generated_schema"));
    CHECK(EnttPersistenceEmitter::emit_schema_descriptor(unit->program).empty());
}

TEST_CASE("the schema describes declared traits, structs, and enums", "[codegen-entt][persistence][schema]") {
    const auto unit = generate(kSchemaSource);

    CHECK(contains(unit->code, "generated_schema = {"));
    CHECK(contains(unit->code, "\"world.Health\""));
    CHECK(contains(unit->code, "\"world.Inventory\""));
    CHECK(contains(unit->code, "{.name = \"world.Slot\""));
    CHECK(contains(unit->code, "{.name = \"world.Stance\""));
    // Enum variants keep declaration order: their position is their value.
    const auto stance = array_body(unit->code, "generated_schema_world_Stance");
    CHECK(stance.find("Guarding") < stance.find("Charging"));
}

TEST_CASE("field descriptors carry the persistence mask", "[codegen-entt][persistence][schema]") {
    const auto unit   = generate(kSchemaSource);
    const auto health = array_body(unit->code, "generated_schema_world_Health");

    CHECK(contains(health, "{.name = \"current\""));
    CHECK(contains(health, ".persist = true"));
    CHECK(contains(health, "{.name = \"maximum\""));
    CHECK(contains(health, ".persist = false"));
    // Fields are canonically ordered, so reordering a declaration is not a
    // schema change.
    CHECK(health.find("\"current\"") < health.find("\"maximum\""));
}

TEST_CASE("a list element type is described through the shared value-type table",
          "[codegen-entt][persistence][schema]") {
    const auto unit  = generate(kSchemaSource);
    const auto types = array_body(unit->code, "generated_value_types");

    CHECK(contains(types, "PersistenceValueKind::List"));
    CHECK(contains(types, "PersistenceValueKind::Struct"));
    CHECK(contains(types, ".declared_type = \"world.Slot\""));
    // The list entry names its element by index rather than repeating it.
    CHECK_FALSE(contains(types, ".kind = cactus::PersistenceValueKind::List, .bit_width = 0, .lanes = 1, .lane_kind = "
                                "cactus::PersistenceValueKind::Float, .declared_type = \"\", .element = "
                                "cactus::persistence::kNoValueType"));
}

// ── Fingerprint stability and change detection ──────────────────────────────

TEST_CASE("declaration and field reordering leave the schema unchanged", "[codegen-entt][persistence][schema]") {
    const auto declared_one = generate(R"(module world

trait Armor:
    var plates: int = 1

trait Health:
    let maximum: int = 100
    persist var current: int = 100

entity Boss:
    Health
    Armor
)");
    const auto declared_two = generate(R"(module world

trait Health:
    persist var current: int = 100
    let maximum: int = 100

trait Armor:
    var plates: int = 1

entity Boss:
    Armor
    Health
)");

    CHECK(EnttPersistenceEmitter::canonical_schema_text(declared_one->program) ==
          EnttPersistenceEmitter::canonical_schema_text(declared_two->program));
    CHECK(fingerprint_of(*declared_one) == fingerprint_of(*declared_two));
}

TEST_CASE("renaming a field breaks compatibility", "[codegen-entt][persistence][schema]") {
    const auto before = generate(kSchemaSource);
    const auto after  = generate(R"(module world

trait Health:
    let maximum: int = 100
    persist var hitpoints: int = 100

entity Boss:
    Health
)");

    CHECK(fingerprint_of(*before) != fingerprint_of(*after));
}

TEST_CASE("changing a field's type breaks compatibility", "[codegen-entt][persistence][schema]") {
    const auto before = generate(R"(module world

trait Health:
    persist var current: int = 1

entity Boss:
    Health
)");
    const auto after = generate(R"(module world

trait Health:
    persist var current: float = 1.0

entity Boss:
    Health
)");

    CHECK(fingerprint_of(*before) != fingerprint_of(*after));
}

TEST_CASE("changing a declared default breaks compatibility", "[codegen-entt][persistence][schema]") {
    const auto before = generate(R"(module world

trait Health:
    let maximum: int = 100
    persist var current: int = 100

entity Boss:
    Health
)");
    const auto after = generate(R"(module world

trait Health:
    let maximum: int = 250
    persist var current: int = 100

entity Boss:
    Health
)");

    CHECK(fingerprint_of(*before) != fingerprint_of(*after));
}

namespace {

// One imported module holding a durable trait, registered under whatever
// qualifier the importing program spelled.
ModuleImports durable_import(const std::string& alias) {
    ImportedSymbols symbols;
    symbols.module_name = "lib";

    ResolvedTrait health;
    health.name         = "Health";
    health.module_name  = "lib";
    health.symbol_id    = make_symbol_id(SymbolKind::Trait, "lib", "Health");
    health.canonical_id = make_canonical_id(*health.symbol_id);
    health.is_pub       = true;
    health.fields.push_back(ResolvedField{
        .name = "current", .type = make_int_type(), .is_var = true, .is_persist = true, .has_default = false});
    symbols.traits.emplace(health.name, std::move(health));

    ModuleImports imports;
    imports.add(alias, std::move(symbols));
    return imports;
}

}  // namespace

TEST_CASE("importing a module under a different alias leaves the fingerprint unchanged",
          "[codegen-entt][persistence][schema]") {
    const auto under_alias = generate(R"(module world

use lib as hp

entity Boss:
    hp.Health
)",
                                      durable_import("hp"));
    const auto under_other = generate(R"(module world

use lib as vitals

entity Boss:
    vitals.Health
)",
                                      durable_import("vitals"));

    CHECK(EnttPersistenceEmitter::canonical_schema_text(under_alias->program) ==
          EnttPersistenceEmitter::canonical_schema_text(under_other->program));
    CHECK(fingerprint_of(*under_alias) == fingerprint_of(*under_other));
    // The alias never reaches the document either: records name the canonical
    // declaration.
    CHECK(contains(under_alias->code, "\"lib.Health\""));
    CHECK_FALSE(contains(under_alias->code, "\"hp.Health\""));
}

// ── Canonical asset and input references ────────────────────────────────────

TEST_CASE("a persistent asset field is captured by canonical declaration, not by handle",
          "[codegen-entt][persistence][capture]") {
    const auto unit = generate(R"(module world

asset Icon: texture = "art/icon.png"

trait Loadout:
    persist var icon: texture_id

entity Player:
    Loadout
)");

    // The handle->declaration table names the asset canonically, in
    // declaration order, so a save never depends on allocation order.
    const auto assets = array_body(unit->code, "generated_asset_names");
    CHECK(contains(assets, "\"world.Icon\""));
    CHECK(contains(unit->code, "generated_asset_name(live->icon)"));
    CHECK(contains(unit->code, "cactus::persistence::Value::of_asset(std::string(generated_asset_name"));
}

TEST_CASE("persistent input fields are captured through their own button/axis tables",
          "[codegen-entt][persistence][capture]") {
    const auto unit = generate(R"(module world

trait Loadout:
    persist var fire: InputButton
    persist var steer: InputAxis

entity Player:
    Loadout
)");

    CHECK(contains(unit->code, "generated_input_button_name(live->fire)"));
    CHECK(contains(unit->code, "generated_input_axis_name(live->steer)"));
    CHECK(contains(unit->code, "cactus::persistence::Value::of_input(std::string(generated_input_button_name"));
    CHECK(contains(unit->code, "cactus::persistence::Value::of_input(std::string(generated_input_axis_name"));
}

TEST_CASE("a program with no declared assets still emits a callable, empty lookup",
          "[codegen-entt][persistence][capture]") {
    const auto unit = generate(kDeclarations);

    // No AssetRef field exists, but the table must still exist and be valid
    // C++ so any future field of that type compiles without a second pass.
    const auto assets = array_body(unit->code, "generated_asset_names");
    CHECK_FALSE(assets.contains('"'));
}

// ── Capture-side collection and nesting limits ──────────────────────────────

TEST_CASE("nested struct capture is guarded by the configured depth bound",
          "[codegen-entt][persistence][capture][limits]") {
    const auto unit = generate(kSchemaSource);
    const auto slot = function_body(unit->code, "generated_capture_world__Slot");

    CHECK(contains(slot, "if (!ids.enter_nesting())"));
    CHECK(contains(slot, "ids.exit_nesting();"));
}

TEST_CASE("list capture is guarded by the configured collection-size bound",
          "[codegen-entt][persistence][capture][limits]") {
    const auto unit = generate(kSchemaSource);

    CHECK(contains(unit->code, "cactus::persistence::kMaxPersistenceCollectionSize"));
    CHECK(contains(unit->code, "ids.mark_limit_exceeded();"));
}

TEST_CASE("the captured snapshot records whether a limit truncated it",
          "[codegen-entt][persistence][capture][limits]") {
    const auto unit  = generate(kSchemaSource);
    const auto world = function_body(unit->code, "generated_capture_world_snapshot");

    CHECK(contains(world, "snapshot.truncated = ids.limit_exceeded();"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
