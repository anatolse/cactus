// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "common/persistence_metadata.hpp"
#include "common/types.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace cactus;
namespace fs = std::filesystem;

namespace {

struct Compilation {
    ErrorReporter errors;
    ProgramNode ast;
    DecoratedProgram program;
};

// Owns its AST for the lifetime of the test: DecoratedProgram keeps a
// non-owning pointer into it and the archetype baselines are read back through
// that pointer during metadata construction.
std::unique_ptr<Compilation> compile(const std::string& source, const std::string& filename = "world.cactus") {
    auto unit = std::make_unique<Compilation>();
    Lexer lexer(source, filename, unit->errors);
    Parser parser(lexer.tokenize(), unit->errors);
    unit->ast = parser.parse_program();
    SemanticAnalyzer analyzer(unit->errors);
    unit->program = analyzer.analyze(unit->ast);
    return unit;
}

const PersistenceArchetypeDescriptor* archetype(const DecoratedProgram& program,
                                                SymbolKind kind,
                                                const std::string& module_name,
                                                const std::string& name,
                                                const std::vector<std::string>& role_path = {}) {
    return find_archetype_descriptor(
        program.persistence,
        ArchetypeNodeId{.archetype = make_symbol_id(kind, module_name, name), .role_path = role_path});
}

std::vector<std::string> node_strings(const ModulePersistenceMetadata& metadata) {
    std::vector<std::string> names;
    names.reserve(metadata.archetypes.size());
    for (const auto& descriptor : metadata.archetypes) {
        names.push_back(canonical_node_string(descriptor.node));
    }
    return names;
}

fs::path test_build_dir() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR) / "persistence_artifact_build";
}

constexpr const char* kBossSource = R"(module world

trait Health:
    let maximum: int = 100
    persist var current: int = 100

trait Sparkle:
    var lifetime: float = 0.2

template Boss:
    Health:
        maximum = 500

template Particle:
    Sparkle:
        lifetime = 0.5
)";

}  // namespace

// ── Canonical archetype and node identity ───────────────────────────────────

TEST_CASE("persistence metadata identifies archetype nodes by canonical symbol and role path",
          "[persistence][metadata][identity]") {
    const auto unit = compile(R"(module world

trait Parent:
    var parent: entity_id

trait Turret:
    var angle: float = 0.0

trait Barrel:
    var length: float = 1.0

template Tank:
    Turret
    children:
        entity turret:
            Turret
            children:
                entity barrel:
                    Barrel
)");
    REQUIRE_FALSE(unit->errors.has_errors());

    const auto* root = archetype(unit->program, SymbolKind::Template, "world", "Tank");
    REQUIRE(root != nullptr);
    CHECK(canonical_node_string(root->node) == "world.Tank");
    CHECK(root->child_roles == std::vector<std::string>{"turret"});

    const auto* turret = archetype(unit->program, SymbolKind::Template, "world", "Tank", {"turret"});
    REQUIRE(turret != nullptr);
    CHECK(canonical_node_string(turret->node) == "world.Tank/turret");
    CHECK(turret->child_roles == std::vector<std::string>{"barrel"});

    const auto* barrel = archetype(unit->program, SymbolKind::Template, "world", "Tank", {"turret", "barrel"});
    REQUIRE(barrel != nullptr);
    CHECK(canonical_node_string(barrel->node) == "world.Tank/turret/barrel");
    CHECK(barrel->child_roles.empty());
}

TEST_CASE("persistence metadata records authored entities as their own archetype nodes",
          "[persistence][metadata][identity]") {
    const auto unit = compile(R"(module world

trait Health:
    let maximum: int = 100
    persist var current: int = 100

entity Warden:
    Health:
        maximum = 300
)");
    REQUIRE_FALSE(unit->errors.has_errors());

    const auto* warden = archetype(unit->program, SymbolKind::Entity, "world", "Warden");
    REQUIRE(warden != nullptr);
    CHECK(canonical_node_string(warden->node) == "world.Warden");
    // A named entity's declaration overrides belong to its baseline, not to
    // per-instance construction data.
    REQUIRE(warden->baseline_traits.size() == 1);
    CHECK(warden->baseline_traits[0].assigned_fields == std::vector<std::string>{"maximum"});
}

// ── Eligibility through the declared trait set ──────────────────────────────

TEST_CASE("an archetype whose declared trait set has a persist field is eligible",
          "[persistence][metadata][eligibility]") {
    const auto unit = compile(kBossSource);
    REQUIRE_FALSE(unit->errors.has_errors());

    const auto* boss = archetype(unit->program, SymbolKind::Template, "world", "Boss");
    REQUIRE(boss != nullptr);
    CHECK(boss->declares_persistent_trait);

    const auto* particle = archetype(unit->program, SymbolKind::Template, "world", "Particle");
    REQUIRE(particle != nullptr);
    CHECK_FALSE(particle->declares_persistent_trait);
}

TEST_CASE("a program with no persistence-bearing add path claims no runtime attachment",
          "[persistence][metadata][bound]") {
    const auto unit = compile(kBossSource);
    REQUIRE_FALSE(unit->errors.has_errors());
    CHECK_FALSE(unit->program.persistence.attaches_persistent_trait);
}

TEST_CASE("an authored add of a persistence-bearing trait lifts the whole-program bound",
          "[persistence][metadata][bound]") {
    const auto unit = compile(R"(module world

event Step

trait Health:
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
            add Health
)");
    REQUIRE_FALSE(unit->errors.has_errors());
    CHECK(unit->program.persistence.attaches_persistent_trait);
}

TEST_CASE("adding a trait with no persist field leaves the whole-program bound down",
          "[persistence][metadata][bound]") {
    const auto unit = compile(R"(module world

event Step

trait Health:
    persist var current: int = 100

trait Stunned

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
)");
    REQUIRE_FALSE(unit->errors.has_errors());
    CHECK_FALSE(unit->program.persistence.attaches_persistent_trait);
}

// ── Canonical ordering ──────────────────────────────────────────────────────

TEST_CASE("archetype metadata ordering is independent of declaration order", "[persistence][metadata][ordering]") {
    const auto declared_one = compile(R"(module world

trait Health:
    persist var current: int = 100

trait Sparkle:
    var lifetime: float = 0.2

template Boss:
    Health

template Particle:
    Sparkle
)");
    const auto declared_two = compile(R"(module world

trait Sparkle:
    var lifetime: float = 0.2

trait Health:
    persist var current: int = 100

template Particle:
    Sparkle

template Boss:
    Health
)");
    REQUIRE_FALSE(declared_one->errors.has_errors());
    REQUIRE_FALSE(declared_two->errors.has_errors());

    CHECK(node_strings(declared_one->program.persistence) == std::vector<std::string>{"world.Boss", "world.Particle"});
    CHECK(declared_one->program.persistence == declared_two->program.persistence);
}

TEST_CASE("baseline trait order inside an archetype is canonical, not authored",
          "[persistence][metadata][ordering]") {
    const auto declared_one = compile(R"(module world

trait Armor:
    var plates: int = 1

trait Health:
    persist var current: int = 100

template Boss:
    Armor
    Health
)");
    const auto declared_two = compile(R"(module world

trait Armor:
    var plates: int = 1

trait Health:
    persist var current: int = 100

template Boss:
    Health
    Armor
)");
    REQUIRE_FALSE(declared_one->errors.has_errors());
    REQUIRE_FALSE(declared_two->errors.has_errors());

    const auto* one = archetype(declared_one->program, SymbolKind::Template, "world", "Boss");
    const auto* two = archetype(declared_two->program, SymbolKind::Template, "world", "Boss");
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    REQUIRE(one->baseline_traits.size() == 2);
    CHECK(make_canonical_id(one->baseline_traits[0].trait) == "world.Armor");
    CHECK(make_canonical_id(one->baseline_traits[1].trait) == "world.Health");
    CHECK(*one == *two);
}

// ── Parameterized construction data ─────────────────────────────────────────

TEST_CASE("template parameters are recorded in declaration order", "[persistence][metadata][parameters]") {
    const auto unit = compile(R"(module world

trait Health:
    let maximum: int = 100
    persist var current: int = 100

template Enemy(maximum: int, tier: int = 1):
    Health:
        maximum = maximum
        current = maximum
)");
    REQUIRE_FALSE(unit->errors.has_errors());

    const auto* enemy = archetype(unit->program, SymbolKind::Template, "world", "Enemy");
    REQUIRE(enemy != nullptr);
    CHECK(enemy->parameters == std::vector<std::string>{"maximum", "tier"});
    REQUIRE(enemy->baseline_traits.size() == 1);
    CHECK(enemy->baseline_traits[0].assigned_fields == std::vector<std::string>{"current", "maximum"});
}

// ── Value representation ────────────────────────────────────────────────────

TEST_CASE("value descriptors report the backend's exact representation", "[persistence][metadata][values]") {
    CHECK(describe_persistence_value(make_int_type()) ==
          PersistenceValueType{.kind = PersistenceValueKind::Int, .bit_width = 32});
    CHECK(describe_persistence_value(make_float_type()) ==
          PersistenceValueType{.kind = PersistenceValueKind::Float, .bit_width = 32});
    CHECK(describe_persistence_value(make_bool_type()) ==
          PersistenceValueType{.kind = PersistenceValueKind::Bool, .bit_width = 8});

    const auto vec3 = describe_persistence_value(make_vec3_type());
    CHECK(vec3.kind == PersistenceValueKind::Vector);
    CHECK(vec3.lanes == 3);
    CHECK(vec3.bit_width == 32);
    CHECK(vec3.lane_kind == PersistenceValueKind::Float);

    // color is four unsigned bytes in the backend, not four floats.
    const auto color = describe_persistence_value(make_color_type());
    CHECK(color.kind == PersistenceValueKind::Vector);
    CHECK(color.lanes == 4);
    CHECK(color.bit_width == 8);
    CHECK(color.lane_kind == PersistenceValueKind::Int);

    CHECK(describe_persistence_value(make_entity_id_type()).kind == PersistenceValueKind::EntityRef);
    CHECK(describe_persistence_value(make_model_id_type()).kind == PersistenceValueKind::AssetRef);
    CHECK(describe_persistence_value(make_input_button_type()).kind == PersistenceValueKind::InputRef);

    const auto list = describe_persistence_value(make_list_type(make_vec2_type()));
    CHECK(list.kind == PersistenceValueKind::List);
    REQUIRE(list.element.size() == 1);
    CHECK(list.element[0].lanes == 2);
}

TEST_CASE("types the schema cannot represent are classified unsupported", "[persistence][metadata][values]") {
    CHECK(describe_persistence_value(make_unknown_type()).kind == PersistenceValueKind::Unsupported);
    CHECK(describe_persistence_value(make_void_type()).kind == PersistenceValueKind::Unsupported);
    CHECK(describe_persistence_value(make_list_type(make_void_type())).element[0].kind ==
          PersistenceValueKind::Unsupported);
}

TEST_CASE("unsupported persist field types are reported by field path", "[persistence][metadata][diagnostics]") {
    DecoratedProgram program;
    program.module_name = "world";

    ResolvedStruct slot;
    slot.name         = "Slot";
    slot.module_name  = "world";
    slot.canonical_id = "world.Slot";
    slot.symbol_id    = make_symbol_id(SymbolKind::Struct, "world", "Slot");
    slot.fields.push_back(ResolvedField{.name = "count", .type = make_int_type()});
    slot.fields.push_back(ResolvedField{.name = "callback", .type = make_type_info(TypeKind::Func, "func")});
    program.structs["world.Slot"] = slot;

    TypeInfo slot_type    = make_type_info(TypeKind::Struct, "Slot");
    slot_type.symbol_id   = slot.symbol_id;

    ResolvedTrait inventory;
    inventory.name         = "Inventory";
    inventory.module_name  = "world";
    inventory.canonical_id = "world.Inventory";
    inventory.fields.push_back(
        ResolvedField{.name = "slots", .type = make_list_type(slot_type), .is_var = true, .is_persist = true});
    inventory.fields.push_back(
        ResolvedField{.name = "label", .type = make_string_type(), .is_var = true, .is_persist = true});
    // Unmarked fields are reconstructed from construction data, so an
    // unsupported type there is not this pass's concern.
    inventory.fields.push_back(ResolvedField{.name = "scratch", .type = make_unknown_type(), .is_var = true});
    program.traits["world.Inventory"] = inventory;

    const auto unsupported = collect_unsupported_persistence_fields(program);
    REQUIRE(unsupported.size() == 1);
    CHECK(unsupported[0].field_path == "world.Inventory.slots[].callback");
    CHECK(unsupported[0].type_spelling == "func");
}

TEST_CASE("an unsupported persist field type is a compile error naming the field path",
          "[persistence][metadata][diagnostics]") {
    DecoratedProgram program;
    program.module_name = "world";

    ResolvedTrait broken;
    broken.name         = "Broken";
    broken.module_name  = "world";
    broken.canonical_id = "world.Broken";
    broken.fields.push_back(
        ResolvedField{.name = "callback", .type = make_type_info(TypeKind::Func, "func"), .is_var = true, .is_persist = true});
    program.traits["world.Broken"] = broken;

    ErrorReporter errors;
    report_unsupported_persistence_fields(program, errors);

    REQUIRE(errors.has_errors());
    const auto& message = errors.diagnostics().front().message;
    CHECK(message.contains("world.Broken.callback"));
    CHECK(message.contains("func"));
}

TEST_CASE("a program with no unsupported persist field type reports no error",
          "[persistence][metadata][diagnostics]") {
    DecoratedProgram program;
    program.module_name = "world";

    ResolvedTrait health;
    health.name         = "Health";
    health.module_name  = "world";
    health.canonical_id = "world.Health";
    health.fields.push_back(
        ResolvedField{.name = "current", .type = make_int_type(), .is_var = true, .is_persist = true});
    program.traits["world.Health"] = health;

    ErrorReporter errors;
    report_unsupported_persistence_fields(program, errors);
    CHECK_FALSE(errors.has_errors());
}

// ── Whole-program construction-data bound ───────────────────────────────────

TEST_CASE("never-eligible archetypes retain no construction data without an add path",
          "[persistence][metadata][bound]") {
    const auto unit = compile(kBossSource);
    REQUIRE_FALSE(unit->errors.has_errors());

    const auto* boss     = archetype(unit->program, SymbolKind::Template, "world", "Boss");
    const auto* particle = archetype(unit->program, SymbolKind::Template, "world", "Particle");
    REQUIRE(boss != nullptr);
    REQUIRE(particle != nullptr);

    const bool attaches = unit->program.persistence.attaches_persistent_trait;
    CHECK(archetype_retains_construction_data(*boss, attaches));
    CHECK_FALSE(archetype_retains_construction_data(*particle, attaches));
    // The same particle archetype does pay once the program can attach a
    // persistence-bearing trait at runtime.
    CHECK(archetype_retains_construction_data(*particle, true));
}

// ── Module artifact round trip ──────────────────────────────────────────────

TEST_CASE("persistence metadata survives a module artifact round trip", "[persistence][metadata][artifact]") {
    const auto unit = compile(R"(module world

event Step

trait Parent:
    var parent: entity_id

trait Health:
    let maximum: int = 100
    persist var current: int = 100

trait Turret:
    var angle: float = 0.0

template Boss(maximum: int = 500):
    Health:
        maximum = maximum
    children:
        entity turret:
            Turret

rule Promote:
    filter:
        Turret as turret

    on Step:
        if turret.angle > 0.0:
            add Health
)");
    REQUIRE_FALSE(unit->errors.has_errors());
    REQUIRE(unit->program.persistence.attaches_persistent_trait);
    REQUIRE(unit->program.persistence.archetypes.size() == 2);

    ErrorReporter artifact_errors;
    ModuleArtifact artifact(artifact_errors);
    const auto directory = test_build_dir();
    REQUIRE(artifact.save(unit->program, "world", directory));

    std::string module_name;
    const auto reloaded = artifact.load(directory / "world.cmod", module_name);
    REQUIRE_FALSE(artifact_errors.has_errors());
    REQUIRE(reloaded.has_value());
    CHECK(module_name == "world");
    CHECK(reloaded->persistence == unit->program.persistence);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
