// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>

using namespace cactus;

namespace {

const std::string kPrelude =
    "module game\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "event Ping:\n"
    "    v: int\n"
    "trait Unit:\n"
    "    var id: int = 0\n"
    "trait Active:\n"
    "    var power: float = 0.0\n"
    "single trait Leader\n"
    "state Mode:\n"
    "    Off\n"
    "    On:\n"
    "        var power: float = 0.0\n"
    "relation Follows:\n"
    "    from: Unit\n"
    "    to: Unit\n"
    "entity Game:\n"
    "    Unit\n"
    "entity Grunt:\n"
    "    Unit\n"
    "    Mode\n"
    "entity Elite:\n"
    "    Unit\n"
    "    Mode.On\n"
    "entity Scout:\n"
    "    Unit\n"
    "    Active\n"
    "    Leader\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    bool clean = false;
    std::string diagnostics;

    [[nodiscard]] IterationOrder order_of(const std::string& rule) const {
        const auto node = std::ranges::find_if(program.execution_graph.handlers, [&](const HandlerNode& candidate) {
            return candidate.identity.rule.local_name == rule;
        });
        REQUIRE(node != program.execution_graph.handlers.end());
        return node->contract.iteration_order;
    }
};

Analysis analyze(const std::string& body, bool link = true) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program = analyzer.analyze(*analysis.ast);
    if (link && !errors.has_errors()) {
        run_whole_program_passes(analysis.program, errors);
    }
    analysis.clean = !errors.has_errors();
    for (const auto& diagnostic : errors.diagnostics()) {
        analysis.diagnostics += std::to_string(diagnostic.location.line) + ": " + diagnostic.message + "\n";
    }
    return analysis;
}

std::string rule(const std::string& name, const std::string& filter, const std::string& body) {
    return "rule " + name + ":\n    each:\n" + filter + "    on tick:\n" + body;
}

const std::string kEnter = rule("R", "        Unit as u\n        Mode.Off\n", "        add Mode.On:\n            power = 1.0\n");
const std::string kArrive = rule("R", "        Unit as u\n", "        add Active:\n            power = 1.0\n");
const std::string kDepart = rule("R", "        Unit as u\n        Active\n", "        remove Active\n");

}  // namespace

TEST_CASE("Self command order: an unwatched self add or remove runs in free order",
          "[semantic][rule-order-by][self-command-order]") {
    SECTION("state variant") {
        const auto analysis = analyze(kEnter);
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Free);
    }
    SECTION("trait add") {
        const auto analysis = analyze(kArrive);
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Free);
    }
    SECTION("trait remove") {
        const auto analysis = analyze(kDepart);
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Free);
    }
}

TEST_CASE("Self command order: before linking the pass waits for the whole program",
          "[semantic][rule-order-by][self-command-order]") {
    const auto analysis = analyze(kArrive, false);
    INFO(analysis.diagnostics);
    REQUIRE(analysis.clean);
    CHECK(analysis.order_of("R") == IterationOrder::SelfCommands);
}

TEST_CASE("Self command order: a visible command order keeps creation order",
          "[semantic][rule-order-by][self-command-order]") {
    SECTION("on added watches the trait") {
        const auto analysis = analyze(kArrive +
                                      "rule W:\n"
                                      "    each:\n"
                                      "        Unit as u\n"
                                      "    on added Active:\n"
                                      "        u.id += 1\n");
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Creation);
    }
    SECTION("on removed watches another variant of the state") {
        const auto analysis = analyze(kEnter +
                                      "rule W:\n"
                                      "    each:\n"
                                      "        Unit as u\n"
                                      "    on removed Mode.Off:\n"
                                      "        u.id += 1\n");
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Creation);
    }
    SECTION("single trait") {
        const auto analysis = analyze(rule("R", "        Unit as u\n", "        add Leader\n"));
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Creation);
    }
    SECTION("another entity") {
        const auto analysis = analyze(rule("R", "        Unit as u\n", "        add Active to Game\n"));
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Creation);
    }
    SECTION("relation") {
        const auto analysis = analyze(rule("R", "        Unit as u\n", "        add Follows(self, Game)\n"));
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Creation);
    }
    SECTION("another reason") {
        const auto analysis =
            analyze(rule("R", "        Unit as u\n", "        add Active\n        emit Ping:\n            v = u.id\n"));
        INFO(analysis.diagnostics);
        REQUIRE(analysis.clean);
        CHECK(analysis.order_of("R") == IterationOrder::Creation);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
