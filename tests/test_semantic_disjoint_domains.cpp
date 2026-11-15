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
#include <optional>
#include <string>
#include <vector>

using namespace cactus;

namespace {

const std::string kPrelude =
    "module game\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "trait Health:\n"
    "    var hp: int = 0\n"
    "    var team: int = 0\n"
    "trait Hero\n"
    "trait Enemy\n"
    "entity Knight:\n"
    "    Hero\n"
    "    Health\n"
    "entity Orc:\n"
    "    Enemy\n"
    "    Health\n";

const std::string kHealHero =
    "rule HealHero:\n"
    "    each:\n"
    "        Hero\n"
    "        Health as h\n"
    "    on tick:\n"
    "        h.hp = 10\n";

const std::string kHurtEnemy =
    "rule HurtEnemy:\n"
    "    each:\n"
    "        Enemy\n"
    "        Health as h\n"
    "    on tick:\n"
    "        h.hp = 1\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] std::vector<std::string> errors() const {
        std::vector<std::string> messages;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.level == DiagnosticLevel::Error) {
                messages.push_back(diagnostic.message);
            }
        }
        return messages;
    }
    [[nodiscard]] bool clean() const {
        return errors().empty();
    }
    [[nodiscard]] bool unorientable(const std::string& first, const std::string& second) const {
        const auto prefix = "unorientable conflict between 'game." + first + "/on game.tick' and 'game." + second +
                            "/on game.tick'";
        return std::ranges::any_of(errors(), [&](const std::string& message) { return message.starts_with(prefix); });
    }
    [[nodiscard]] bool has_edge(const std::string& first, const std::string& second) const {
        return std::ranges::any_of(program.execution_graph.schedule_edges, [&](const ScheduleEdge& edge) {
            const auto& before = edge.before.rule.local_name;
            const auto& after  = edge.after.rule.local_name;
            return (before == first && after == second) || (before == second && after == first);
        });
    }
    [[nodiscard]] std::optional<std::uint64_t> level(const std::string& rule) const {
        for (const auto& entry : program.execution_graph.dependency_levels) {
            if (std::ranges::contains(entry.handlers, rule, [](const HandlerIdentity& id) { return id.rule.local_name; })) {
                return entry.index;
            }
        }
        return std::nullopt;
    }
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ":" + std::to_string(d.location.column) + ": " + d.message + "\n";
    }
    return text;
}

// Analyzes the prelude plus `body`, then runs the whole-program passes when analysis is clean.
Analysis analyze(const std::string& body) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program = analyzer.analyze(*analysis.ast);
    if (!errors.has_errors()) {
        run_whole_program_passes(analysis.program, errors);
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

}  // namespace

TEST_CASE("Disjoint domains: two archetypes assign one field with no edge",
          "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(kHealHero + kHurtEnemy);
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK_FALSE(analysis.has_edge("HealHero", "HurtEnemy"));
}

TEST_CASE("Disjoint domains: the two handlers share a dependency level",
          "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(kHealHero + kHurtEnemy);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    REQUIRE(analysis.level("HealHero").has_value());
    CHECK(analysis.level("HealHero") == analysis.level("HurtEnemy"));
}

TEST_CASE("Disjoint domains: variants of one state take arrows on their own row",
          "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(
        "state Alert:\n"
        "    Calm -> Curious:\n"
        "        var calm: int = 0\n"
        "    Curious -> Hunting:\n"
        "        var curious: int = 0\n"
        "    Hunting -> Calm:\n"
        "        var hunting: int = 0\n"
        "entity Guard:\n"
        "    Alert\n"
        "rule Settle:\n"
        "    each:\n"
        "        Alert.Calm as c\n"
        "    on tick:\n"
        "        c.calm = 1\n"
        "        add Alert.Curious\n"
        "rule Look:\n"
        "    each:\n"
        "        Alert.Curious as c\n"
        "    on tick:\n"
        "        c.curious = 1\n"
        "        add Alert.Hunting\n"
        "rule Hunt:\n"
        "    each:\n"
        "        Alert.Hunting as h\n"
        "    on tick:\n"
        "        h.hunting = 1\n"
        "        add Alert.Calm\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK_FALSE(analysis.has_edge("Settle", "Look"));
    CHECK_FALSE(analysis.has_edge("Look", "Hunt"));
    CHECK_FALSE(analysis.has_edge("Settle", "Hunt"));
}

TEST_CASE("Disjoint domains: without: proves the split", "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(
        "trait Unit:\n"
        "    var speed: float = 0.0\n"
        "trait Frozen\n"
        "entity Runner:\n"
        "    Unit\n"
        "entity Statue:\n"
        "    Unit\n"
        "    Frozen\n"
        "rule Run:\n"
        "    each:\n"
        "        Unit as u\n"
        "    without: Frozen\n"
        "    on tick:\n"
        "        u.speed = 2.0\n"
        "rule Hold:\n"
        "    each:\n"
        "        Unit as u\n"
        "        Frozen\n"
        "    on tick:\n"
        "        u.speed = 0.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK_FALSE(analysis.has_edge("Run", "Hold"));
}

TEST_CASE("Disjoint domains: own-row adds with fields don't conflict",
          "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(
        "trait Boost:\n"
        "    var power: int = 0\n"
        "trait Charged\n"
        "entity Spark:\n"
        "    Hero\n"
        "    Charged\n"
        "rule BoostHero:\n"
        "    each:\n"
        "        Hero\n"
        "    without: Boost\n"
        "    on tick:\n"
        "        add Boost:\n"
        "            power = 1\n"
        "rule BoostEnemy:\n"
        "    each:\n"
        "        Enemy\n"
        "    without: Boost\n"
        "    on tick:\n"
        "        add Boost:\n"
        "            power = 2\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK_FALSE(analysis.has_edge("BoostHero", "BoostEnemy"));
}

TEST_CASE("Disjoint domains: an archetype that carries both keeps the conflict and is named",
          "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(
        "entity Traitor:\n"
        "    Hero\n"
        "    Enemy\n"
        "    Health\n" +
        kHealHero + kHurtEnemy);
    INFO(describe(analysis));
    REQUIRE(analysis.unorientable("HealHero", "HurtEnemy"));
    const auto errors = analysis.errors();
    CHECK(std::ranges::any_of(errors, [](const std::string& message) {
        return message.contains("game.Enemy, game.Health, game.Hero") && message.contains("game.Traitor");
    }));
}

TEST_CASE("Disjoint domains: nested domains conflict without naming a joining archetype",
          "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(kHealHero +
                                  "rule HealAll:\n"
                                  "    each:\n"
                                  "        Health as h\n"
                                  "    on tick:\n"
                                  "        h.hp = 5\n");
    INFO(describe(analysis));
    REQUIRE(analysis.unorientable("HealAll", "HealHero"));
    CHECK(std::ranges::none_of(analysis.errors(),
                               [](const std::string& message) { return message.contains("meet only in"); }));
}

TEST_CASE("Disjoint domains: a named read keeps the conflict", "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(
        "entity Boss:\n"
        "    Health\n"
        "rule HealHero:\n"
        "    each:\n"
        "        Hero\n"
        "        Health as h\n"
        "    on tick:\n"
        "        h.hp = Boss[Health].hp\n" +
        kHurtEnemy);
    INFO(describe(analysis));
    CHECK(analysis.unorientable("HealHero", "HurtEnemy"));
}

TEST_CASE("Disjoint domains: a pair rule keeps the conflict", "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(kHealHero +
                                  "rule Strike:\n"
                                  "    pairs:\n"
                                  "        a:\n"
                                  "            Enemy\n"
                                  "            Health\n"
                                  "        b:\n"
                                  "            Hero\n"
                                  "    limit: 1 per a\n"
                                  "    on tick:\n"
                                  "        a[Health].hp = 1\n");
    INFO(describe(analysis));
    CHECK(analysis.unorientable("HealHero", "Strike"));
}

TEST_CASE("Disjoint domains: a where: split keeps the conflict", "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(
        "rule HealBlue:\n"
        "    each:\n"
        "        Health as h\n"
        "    where: h.team == 1\n"
        "    on tick:\n"
        "        h.hp = 10\n"
        "rule HealRed:\n"
        "    each:\n"
        "        Health as h\n"
        "    where: h.team == 2\n"
        "    on tick:\n"
        "        h.hp = 1\n");
    INFO(describe(analysis));
    CHECK(analysis.unorientable("HealBlue", "HealRed"));
}

TEST_CASE("Disjoint domains: an order-sensitive consumer of own-row adds still conflicts",
          "[semantic][handler-execution-graph][disjoint-domains]") {
    const auto analysis = analyze(
        "trait Marked\n"
        "trait Tally:\n"
        "    var last: int = 0\n"
        "entity Counter:\n"
        "    Tally\n"
        "rule MarkHero:\n"
        "    each:\n"
        "        Hero\n"
        "    without: Marked\n"
        "    on tick:\n"
        "        add Marked\n"
        "rule MarkEnemy:\n"
        "    each:\n"
        "        Enemy\n"
        "    without: Marked\n"
        "    on tick:\n"
        "        add Marked\n"
        "rule Record:\n"
        "    each:\n"
        "        Health as h\n"
        "    on added Marked:\n"
        "        Counter[Tally].last = h.hp\n");
    INFO(describe(analysis));
    CHECK(analysis.unorientable("MarkEnemy", "MarkHero"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
