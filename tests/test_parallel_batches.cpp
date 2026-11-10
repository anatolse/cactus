// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parallel_batches.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace cactus;

namespace {

const std::string kPrelude =
    "extern event frame:\n"
    "    dt: float\n"
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "    every: 1.0 / 60.0\n"
    "phase tick:\n"
    "    after:\n"
    "        fixed_tick\n"
    "event Knock\n"
    "extern func draw(x: float)\n"
    "trait A:\n"
    "    var x: float = 0.0\n"
    "    var y: float = 0.0\n"
    "trait B:\n"
    "    var x: float = 0.0\n"
    "trait C:\n"
    "    var x: float = 0.0\n"
    "trait Flash:\n"
    "    var level: float = 0.0\n"
    "trait Tile:\n"
    "    var code: int = 0\n"
    "relation Route:\n"
    "    from: Tile\n"
    "    to: Tile\n"
    "relation Reach = closure(Route)\n"
    "relation Group = components(Route)\n"
    "template Spark:\n"
    "    C\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;

    [[nodiscard]] const HandlerNode& handler(const std::string& rule) const {
        const auto found = std::ranges::find_if(program.execution_graph.handlers, [&](const HandlerNode& node) {
            return node.identity.rule.local_name == rule;
        });
        REQUIRE(found != program.execution_graph.handlers.end());
        return *found;
    }

    [[nodiscard]] bool eligible(const std::string& rule) const {
        return parallel_eligible(handler(rule), program);
    }
};

Analysis analyze(const std::string& body) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program = analyzer.analyze(*analysis.ast);
    std::string text;
    for (const auto& diagnostic : errors.diagnostics()) {
        text += std::to_string(diagnostic.location.line) + ": " + diagnostic.message + "\n";
    }
    INFO(text);
    REQUIRE_FALSE(errors.has_errors());
    return analysis;
}

// Rule names of each batch of the one parallel level in `plan`.
std::vector<std::vector<std::string>> batch_names(const ParallelPlan& plan) {
    REQUIRE(plan.levels.size() == 1);
    std::vector<std::vector<std::string>> names;
    for (const auto& batch : plan.levels.front().batches) {
        auto& row = names.emplace_back();
        for (const auto& identity : batch.handlers) {
            row.push_back(identity.rule.local_name);
        }
    }
    return names;
}

const std::string kStepA =
    "rule StepA:\n"
    "    each: A as a\n"
    "    on tick:\n"
    "        a.x = a.x + 1.0\n";

const std::string kStepB =
    "rule StepB:\n"
    "    each: B as b\n"
    "    on tick:\n"
    "        b.x = b.x + 1.0\n";

}  // namespace

TEST_CASE("Parallel batches: a pure data handler is eligible", "[parallel-batches]") {
    const auto analysis = analyze(kStepA);
    CHECK(analysis.eligible("StepA"));
}

TEST_CASE("Parallel batches: a handler with an effect stays on the main thread", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule Draw:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        draw(a.x)\n");
    CHECK_FALSE(analysis.eligible("Draw"));
}

TEST_CASE("Parallel batches: a spawning handler stays on the main thread", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule Burst:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        spawn Spark()\n");
    CHECK_FALSE(analysis.eligible("Burst"));
}

TEST_CASE("Parallel batches: a projecting handler stays on the main thread", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule Glow:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        project Flash:\n"
        "            level = a.x\n");
    CHECK_FALSE(analysis.eligible("Glow"));
}

TEST_CASE("Parallel batches: a handler reading host input stays on the main thread", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule Keys:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        let p = input.mouse_position()\n"
        "        a.x = p.x\n");
    CHECK_FALSE(analysis.eligible("Keys"));
}

TEST_CASE("Parallel batches: a handler calling a function that reads host input stays on the main thread",
          "[parallel-batches]") {
    const auto analysis = analyze(
        "func pointer_x() float:\n"
        "    return input.mouse_position().x\n"
        "rule Follow:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        a.x = pointer_x()\n");
    CHECK_FALSE(analysis.eligible("Follow"));
}

TEST_CASE("Parallel batches: event handlers never run on workers", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule Answer:\n"
        "    each: A as a\n"
        "    on Knock:\n"
        "        a.x = 1.0\n"
        "rule Starter:\n"
        "    on tick:\n"
        "        emit Knock\n");
    CHECK_FALSE(analysis.eligible("Answer"));
    CHECK(analysis.eligible("Starter"));
}

TEST_CASE("Parallel batches: a closure lookup stays on the main thread, a components lookup does not",
          "[parallel-batches]") {
    const auto analysis = analyze(
        "rule Spread:\n"
        "    pairs:\n"
        "        a: Tile\n"
        "        b: Tile\n"
        "    where:\n"
        "        Reach(a, b)\n"
        "    on tick:\n"
        "        emit Knock to b\n"
        "rule Merge:\n"
        "    pairs:\n"
        "        a: Tile\n"
        "        b: Tile\n"
        "    where:\n"
        "        Group(a, b)\n"
        "    on fixed_tick:\n"
        "        emit Knock to b\n");
    CHECK_FALSE(analysis.eligible("Spread"));
    CHECK(analysis.eligible("Merge"));
}

TEST_CASE("Parallel batches: a handler that reads a shared index is eligible", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule Match:\n"
        "    pairs:\n"
        "        a: Tile\n"
        "        b: Tile\n"
        "    where:\n"
        "        a[Tile].code == b[Tile].code\n"
        "    on tick:\n"
        "        emit Knock to b\n");
    CHECK(analysis.eligible("Match"));
}

TEST_CASE("Parallel batches: disjoint handlers of one level share a batch", "[parallel-batches]") {
    const auto analysis = analyze(kStepA + kStepB);
    const auto plan     = plan_parallel_handlers(analysis.program);
    CHECK(batch_names(plan) == std::vector<std::vector<std::string>>{{"StepA", "StepB"}});
}

TEST_CASE("Parallel batches: handlers writing different fields of one trait share a batch", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule MoveX:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        a.x = a.x + 1.0\n"
        "rule MoveY:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        a.y = a.y + 1.0\n");
    const auto plan = plan_parallel_handlers(analysis.program);
    CHECK(batch_names(plan) == std::vector<std::vector<std::string>>{{"MoveX", "MoveY"}});
}

TEST_CASE("Parallel batches: commuting writes of one field never share a batch", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule AddOne:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        a.x += 1.0\n"
        "rule AddTwo:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        a.x += 2.0\n" +
        kStepB);
    const auto& graph = analysis.program.execution_graph;
    const auto level  = std::ranges::find_if(graph.dependency_levels, [](const DependencyLevel& candidate) {
        return candidate.handlers.size() == 3;
    });
    REQUIRE(level != graph.dependency_levels.end());
    const auto plan = plan_parallel_handlers(analysis.program);
    CHECK(batch_names(plan) == std::vector<std::vector<std::string>>{{"AddOne", "StepB"}, {"AddTwo"}});
}

TEST_CASE("Parallel batches: an ineligible handler joins a batch it shares no data with", "[parallel-batches]") {
    const auto analysis = analyze(kStepA +
                                  "rule Draw:\n"
                                  "    each: B as b\n"
                                  "    on tick:\n"
                                  "        draw(b.x)\n");
    const auto plan = plan_parallel_handlers(analysis.program);
    CHECK(batch_names(plan) == std::vector<std::vector<std::string>>{{"StepA", "Draw"}});
    CHECK(plan.eligible.contains(analysis.handler("StepA").identity));
    CHECK_FALSE(plan.eligible.contains(analysis.handler("Draw").identity));
}

TEST_CASE("Parallel batches: a level with no eligible handler is not planned", "[parallel-batches]") {
    const auto analysis = analyze(
        "rule DrawA:\n"
        "    each: A as a\n"
        "    on tick:\n"
        "        draw(a.x)\n"
        "rule DrawB:\n"
        "    each: B as b\n"
        "    on tick:\n"
        "        spawn Spark()\n");
    CHECK(plan_parallel_handlers(analysis.program).levels.empty());
}

TEST_CASE("Parallel batches: a level of one handler is not planned", "[parallel-batches]") {
    const auto analysis = analyze(kStepA);
    CHECK(plan_parallel_handlers(analysis.program).levels.empty());
}

TEST_CASE("Parallel batches: a native system never shares a batch", "[parallel-batches]") {
    const auto analysis = analyze(kStepA +
                                  "extern rule Physics:\n"
                                  "    each:\n"
                                  "        C\n"
                                  "    on tick:\n"
                                  "        writes:\n"
                                  "            C\n");
    const auto plan = plan_parallel_handlers(analysis.program);
    CHECK(plan.levels.empty());
    CHECK_FALSE(analysis.eligible("Physics"));
}

TEST_CASE("Parallel batches: planning is the same on every run", "[parallel-batches]") {
    const auto analysis = analyze(kStepA + kStepB +
                                  "rule StepC:\n"
                                  "    each: C as c\n"
                                  "    on tick:\n"
                                  "        c.x = c.x + 1.0\n");
    const auto first  = batch_names(plan_parallel_handlers(analysis.program));
    const auto second = batch_names(plan_parallel_handlers(analysis.program));
    CHECK(first == second);
    CHECK(first == std::vector<std::vector<std::string>>{{"StepA", "StepB", "StepC"}});
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access)
