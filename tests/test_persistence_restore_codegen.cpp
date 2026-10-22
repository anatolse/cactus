// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/cpp_entt_codegen.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace cactus;

namespace {

struct Compilation {
    ErrorReporter errors;
    ProgramNode ast;
    DecoratedProgram program;
    std::string code;
};

std::unique_ptr<Compilation> generate(const std::string& source, bool derive_reachability = false) {
    auto unit = std::make_unique<Compilation>();
    Lexer lexer(source, "world.cactus", unit->errors);
    Parser parser(lexer.tokenize(), unit->errors);
    unit->ast = parser.parse_program();
    REQUIRE_FALSE(unit->errors.has_errors());
    SemanticAnalyzer analyzer(unit->errors);
    unit->program = analyzer.analyze(unit->ast, ModuleImports{});
    REQUIRE_FALSE(unit->errors.has_errors());
    if (derive_reachability) {
        derive_archetype_reachability(unit->program, unit->errors);
        REQUIRE_FALSE(unit->errors.has_errors());
    }
    unit->code = CppEnttCodegen::generate(unit->program);
    return unit;
}

// The body of one generated function, so a check about generated_restore_world
// cannot be satisfied by an unrelated function elsewhere in the file.
std::string function_body(const std::string& code, const std::string& name) {
    const auto start = code.find(name + "(");
    REQUIRE(start != std::string::npos);
    const auto open = code.find('{', start);
    REQUIRE(open != std::string::npos);
    const auto end = code.find("\n}", open);
    REQUIRE(end != std::string::npos);
    return code.substr(open, end - open);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.contains(needle);
}

constexpr auto kSource = R"(module world

pub extern event frame:
    dt: float

phase tick:
    from:
        frame

trait Health:
    persist var current: int = 100

trait Alerted

entity Boss:
    Health

rule OnBossArrive:
    each:
        Health

    on added Health as health:
        health.current = 100

rule OnBossCalm:
    each:
        Health

    on removed Alerted:
        current = 0
)";

}  // namespace

TEST_CASE("generated restore validates the document before staging or publishing anything",
          "[codegen-entt][persistence][restore]") {
    const auto unit = generate(kSource);
    const auto body = function_body(unit->code, "generated_restore_world");

    const auto validate_at = body.find("cactus::persistence::validate_document(");
    const auto staged_at   = body.find("entt::registry staged;");
    const auto publish_at  = body.find("registry = std::move(staged);");
    REQUIRE(validate_at != std::string::npos);
    REQUIRE(staged_at != std::string::npos);
    REQUIRE(publish_at != std::string::npos);
    CHECK(validate_at < staged_at);
    CHECK(staged_at < publish_at);
}

TEST_CASE("generated restore fires no lifecycle trigger and keeps tracking the restored world",
          "[codegen-entt][persistence][restore][trait-lifecycle]") {
    const auto unit = generate(kSource);
    const auto body = function_body(unit->code, "generated_restore_world");

    // Restore only ever touches the registry directly (staged.create()/
    // staged.emplace<T>()): no handler, dispatch, or arrival activation runs.
    CHECK_FALSE(contains(body, "on_boss_arrive"));
    CHECK_FALSE(contains(body, "on_boss_calm"));
    CHECK_FALSE(contains(body, "generated_dispatch_event"));
    CHECK_FALSE(contains(body, "emit_targeted_event"));
    CHECK_FALSE(contains(body, "deliver_arrivals"));
    CHECK_FALSE(contains(body, "generated_load_project"));
    CHECK_FALSE(contains(body, "dispatcher.trigger"));

    // The published registry is a fresh one, so the trackers reconnect to it.
    const auto publish_at   = body.find("registry = std::move(staged);");
    const auto reconnect_at = body.find("generated_connect_lifecycle_trackers(registry);");
    REQUIRE(publish_at != std::string::npos);
    REQUIRE(reconnect_at != std::string::npos);
    CHECK(publish_at < reconnect_at);
}

TEST_CASE("publication resets every enumerated holder of a replaced-world handle",
          "[codegen-entt][persistence][restore]") {
    const auto unit = generate(kSource);
    const auto body = function_body(unit->code, "generated_restore_world");

    const auto publish_at            = body.find("registry = std::move(staged);");
    const auto clear_projected_at    = body.find("clear_projected_traits(registry);");
    const auto reset_pointer_at      = body.find("reset_pointer_router_state();");
    const auto reset_editor_at       = body.find("reset_editor_camera_rig_state();");
    const auto reset_activation_at   = body.find("generated_reset_scheduler_state();");
    REQUIRE(publish_at != std::string::npos);
    REQUIRE(clear_projected_at != std::string::npos);
    REQUIRE(reset_pointer_at != std::string::npos);
    REQUIRE(reset_editor_at != std::string::npos);
    REQUIRE(reset_activation_at != std::string::npos);

    // Frame-local projections are tracked against the pre-publication
    // registry, so they must be cleared before it is replaced; the
    // handle-only holders have nothing to do with the registry object
    // itself and are reset after.
    CHECK(clear_projected_at < publish_at);
    CHECK(publish_at < reset_pointer_at);
    CHECK(publish_at < reset_editor_at);
    CHECK(publish_at < reset_activation_at);
}
TEST_CASE("restore rebinds named slots by archetype origin", "[codegen-entt][persistence][restore][named-entity]") {
    const auto unit = generate(R"(module world
event tick:
    dt: float
trait Match:
    persist var best: int = 0
trait Label:
    var visible: bool = true
entity Game:
    Match
entity Hud:
    Label
)");
    const auto rebind = function_body(unit->code, "void generated_rebind_named_slots");
    CHECK(contains(rebind, "cactus::runtime::entt_backend::rebind_named_slots(registry, {{.node = "));
    CHECK(contains(rebind, ", .slot = &::cactus_gen::detail::generated_named_slots().world.Game}});"));
    CHECK(contains(rebind, "::cactus_gen::detail::generated_named_slots().world.Hud = entt::null;"));
    const auto restore = function_body(unit->code, "generated_restore_world");
    const auto publish = restore.find("registry = std::move(staged);");
    const auto rebound = restore.find("::generated_rebind_named_slots(registry);");
    REQUIRE(rebound != std::string::npos);
    CHECK(publish < rebound);
}
TEST_CASE("the schema lists each node's reachable trait sets outside the fingerprint",
          "[codegen-entt][persistence][restore][archetype-reachability]") {
    const std::string base = R"(module world
pub extern event frame:
    dt: float
phase tick:
    from:
        frame
trait Health:
    persist var current: int = 100
trait Alerted
state Mood:
    Calm
    Angry
entity Boss:
    Health
    Mood.Calm
)";
    const auto plain   = generate(base, true);
    const auto alerted = generate(base + R"(rule Alert:
    each:
        Health
    on tick:
        add Alerted
)",
                                  true);
    CHECK(contains(plain->code, ".checks_trait_sets = true,"));
    CHECK(contains(plain->code,
                   "generated_unchecked_traits = {{\n    \"world.Mood\",\n    \"world.Mood.Angry\",\n    \"world.Mood.Calm\","));
    CHECK(contains(plain->code, "generated_schema_world_Boss_reachable_0 = {{\n    \"world.Health\",\n}};"));
    CHECK(contains(plain->code, ".reachable_trait_sets = generated_schema_world_Boss_reachable}"));
    CHECK_FALSE(contains(plain->code, "generated_schema_world_Boss_reachable_1"));
    CHECK(contains(alerted->code,
                   "generated_schema_world_Boss_reachable_0 = {{\n    \"world.Alerted\",\n    \"world.Health\",\n}};"));
    CHECK(contains(alerted->code, "generated_schema_world_Boss_reachable_1 = {{\n    \"world.Health\",\n}};"));

    const auto fingerprint = [](const std::string& code) {
        const auto at = code.find(".fingerprint = ");
        REQUIRE(at != std::string::npos);
        return code.substr(at, code.find(',', at) - at);
    };
    CHECK(fingerprint(plain->code) == fingerprint(alerted->code));
}

TEST_CASE("a schema without derived reachability checks no trait sets", "[codegen-entt][persistence][restore]") {
    const auto unit = generate(kSource);
    CHECK(contains(unit->code, ".checks_trait_sets = false,"));
}

TEST_CASE("a schema requires parents first only when the program never reparents",
          "[codegen-entt][persistence][restore][hierarchy-forest]") {
    const std::string base = R"(module world

pub extern event frame:
    dt: float

phase tick:
    from:
        frame

trait Parent:
    let parent: entity_id

trait Health:
    persist var current: int = 100

pub extern event Grab:
    item: entity_id

entity Boss:
    Health
)";
    const auto fixed  = generate(base, true);
    const auto moving = generate(base + R"(
rule Hold:
    each:
        Health

    on Grab:
        add Parent to Grab.item:
            parent = self
)",
                                 true);
    CHECK(contains(fixed->code, ".parents_first = true,"));
    CHECK(contains(moving->code, ".parents_first = false,"));
    CHECK(contains(generate(kSource)->code, ".parents_first = false,"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
