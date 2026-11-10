// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/cpp_entt_codegen.hpp"
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parallel_batches.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace cactus;

namespace {

const std::string kPrelude =
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "event Ping\n"
    "extern func draw(x: float)\n"
    "trait A:\n"
    "    var x: float = 0.0\n"
    "trait B:\n"
    "    var x: float = 0.0\n"
    "trait Seeker:\n"
    "    let key: int = 0\n"
    "    var found: int = 0\n"
    "trait Spot:\n"
    "    let key: int = 0\n"
    "entity First:\n"
    "    A\n"
    "    B\n"
    "    Seeker\n"
    "    Spot\n";

const std::string kRules =
    "rule StepA:\n"
    "    each: A as a\n"
    "    on tick:\n"
    "        a.x = a.x + 1.0\n"
    "        emit Ping\n"
    "rule StepB:\n"
    "    each: B as b\n"
    "    on tick:\n"
    "        b.x = b.x + 1.0\n"
    "rule Draw:\n"
    "    each: B as b\n"
    "    on tick:\n"
    "        draw(b.x)\n"
    "rule Match:\n"
    "    pairs:\n"
    "        a: Seeker\n"
    "        b: Spot\n"
    "    where:\n"
    "        a[Seeker].key == b[Spot].key\n"
    "    on tick:\n"
    "        emit Ping to a\n"
    "rule Count:\n"
    "    each: Seeker as s\n"
    "    on Ping:\n"
    "        s.found += 1\n";

struct Compiled {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
};

Compiled compile(const std::string& body) {
    Compiled compiled;
    ErrorReporter errors;
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    compiled.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    compiled.program     = analyzer.analyze(*compiled.ast);
    compiled.program.ast = compiled.ast.get();
    REQUIRE_FALSE(errors.has_errors());
    return compiled;
}

std::string generate(bool parallel) {
    auto compiled = compile(kRules);
    if (parallel) {
        compiled.program.parallel_handlers = plan_parallel_handlers(compiled.program);
    }
    return CppEnttCodegen::generate(compiled.program);
}

}  // namespace

TEST_CASE("Codegen parallel: a build without the option uses no workers", "[codegen-entt][parallel-handlers]") {
    const auto code = generate(false);
    CHECK_FALSE(code.contains("run_parallel_batch"));
    CHECK_FALSE(code.contains("TaskOutputs"));
    CHECK_FALSE(code.contains("_prepare("));
    CHECK_FALSE(code.contains("thread"));
}

TEST_CASE("Codegen parallel: a parallel level runs its batches on the worker pool", "[codegen-entt][parallel-handlers]") {
    const auto code = generate(true);
    CHECK(code.contains("cactus::runtime::entt_backend::run_parallel_batch("));
    CHECK(code.contains("cactus::runtime::entt_backend::merge_task_outputs("));
}

TEST_CASE("Codegen parallel: outputs go to the running task's buffer", "[codegen-entt][parallel-handlers]") {
    const auto code = generate(true);
    CHECK(code.contains("generated_output_activation()"));
    CHECK_FALSE(code.contains("queue_structural_command(generated_scheduler_state().activation"));
    CHECK_FALSE(code.contains("emit_event(generated_scheduler_state().activation"));
}

TEST_CASE("Codegen parallel: a shared index catches up in the handler's prepare step", "[codegen-entt][parallel-handlers]") {
    const auto code = generate(true);
    const auto prepare = code.find("void match_on_prepare(entt::registry& registry, ");
    REQUIRE(prepare != std::string::npos);
    const auto handler = code.find("void match_on(entt::registry& registry");
    REQUIRE(handler != std::string::npos);
    const auto handler_end = code.find("\n}\n", handler);
    const auto body        = code.substr(handler, handler_end - handler);
    CHECK(body.contains("shared_binding_index<"));
    CHECK_FALSE(body.contains(".catch_up("));
    const auto prepare_end = code.find("\n}\n", prepare);
    CHECK(code.substr(prepare, prepare_end - prepare).contains(".catch_up("));
}

TEST_CASE("Codegen parallel: init creates every pool before workers start", "[codegen-entt][parallel-handlers]") {
    const auto code = generate(true);
    CHECK(code.contains("registry.storage<::cactus_gen::game::A>();"));
    CHECK(code.contains("registry.storage<::cactus_gen::game::Spot>();"));
    CHECK(code.contains("cactus::runtime::entt_backend::entity_allocator(registry);"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
