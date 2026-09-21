// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
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

std::unique_ptr<Compilation> generate(const std::string& source) {
    auto unit = std::make_unique<Compilation>();
    Lexer lexer(source, "world.cactus", unit->errors);
    Parser parser(lexer.tokenize(), unit->errors);
    unit->ast = parser.parse_program();
    REQUIRE_FALSE(unit->errors.has_errors());
    SemanticAnalyzer analyzer(unit->errors);
    unit->program = analyzer.analyze(unit->ast, ModuleImports{});
    REQUIRE_FALSE(unit->errors.has_errors());
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

event spawn
event destroy

trait Health:
    persist var current: int = 100

trait Alerted

entity Boss:
    Health

rule OnBossSpawn:
    filter:
        Health

    on spawn:
        current = 100

rule OnBossDestroy:
    filter:
        Health

    on destroy:
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

TEST_CASE("generated restore never dispatches ordinary spawn or destroy handlers",
          "[codegen-entt][persistence][restore]") {
    const auto unit = generate(kSource);
    const auto body = function_body(unit->code, "generated_restore_world");

    // OnBossSpawn/OnBossDestroy above would show up here by their generated
    // handler function names, or via a call into the event dispatcher, if
    // restore ran ordinary gameplay lifecycle handlers as a side effect of
    // reconstructing entities. Restore only ever touches the registry
    // directly (staged.create()/staged.emplace<T>()).
    CHECK_FALSE(contains(body, "OnBossSpawn"));
    CHECK_FALSE(contains(body, "OnBossDestroy"));
    CHECK_FALSE(contains(body, "generated_dispatch_event"));
    CHECK_FALSE(contains(body, "dispatcher.trigger"));
}

TEST_CASE("publication resets every enumerated holder of a replaced-world handle",
          "[codegen-entt][persistence][restore]") {
    const auto unit = generate(kSource);
    const auto body = function_body(unit->code, "generated_restore_world");

    const auto publish_at            = body.find("registry = std::move(staged);");
    const auto clear_projected_at    = body.find("clear_projected_traits(registry);");
    const auto reset_pointer_at      = body.find("reset_pointer_router_state();");
    const auto reset_destruction_at  = body.find("reset_pending_destruction_state();");
    const auto reset_editor_at       = body.find("reset_editor_camera_rig_state();");
    const auto reset_activation_at   = body.find("generated_reset_scheduler_state();");
    REQUIRE(publish_at != std::string::npos);
    REQUIRE(clear_projected_at != std::string::npos);
    REQUIRE(reset_pointer_at != std::string::npos);
    REQUIRE(reset_destruction_at != std::string::npos);
    REQUIRE(reset_editor_at != std::string::npos);
    REQUIRE(reset_activation_at != std::string::npos);

    // Frame-local projections are tracked against the pre-publication
    // registry, so they must be cleared before it is replaced; the
    // handle-only holders have nothing to do with the registry object
    // itself and are reset after.
    CHECK(clear_projected_at < publish_at);
    CHECK(publish_at < reset_pointer_at);
    CHECK(publish_at < reset_destruction_at);
    CHECK(publish_at < reset_editor_at);
    CHECK(publish_at < reset_activation_at);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
