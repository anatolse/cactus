// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace cactus;

namespace {

const std::string kPrelude =
    "pub event tick:\n"
    "    dt: float\n"
    "trait Body:\n"
    "    var x: float = 0.0\n"
    "trait Lava:\n"
    "    var active: bool = true\n"
    "    var heat: float = 1.0\n"
    "trait Match:\n"
    "    var running: bool = true\n"
    "trait Score:\n"
    "    var open: bool = true\n"
    "    var total: int = 0\n"
    "trait Health:\n"
    "    var hp: int = 1\n"
    "trait Team:\n"
    "    var active: bool = true\n"
    "    var crowded: bool = false\n"
    "    var total: int = 0\n"
    "trait Member:\n"
    "    var team: entity_id\n"
    "    var points: int = 0\n"
    "entity Game:\n"
    "    Match\n"
    "    Score\n";

const std::string kBodyLavaPairs =
    "    pairs:\n"
    "        body:\n"
    "            Body\n"
    "        lava:\n"
    "            Lava\n";

const std::string kTeamPairs =
    "    pairs:\n"
    "        team:\n"
    "            Team\n"
    "        player:\n"
    "            Member\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool clean() const {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
    [[nodiscard]] const RuleStaging& staging(const std::string& rule_name) const {
        const auto found = std::ranges::find_if(program.execution_graph.handlers,
                                                [&](const auto& handler) { return handler.identity.rule.local_name == rule_name; });
        REQUIRE(found != program.execution_graph.handlers.end());
        return found->contract.staging;
    }
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ": " + d.message + "\n";
    }
    return text;
}

Analysis analyze(const std::string& body) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program     = analyzer.analyze(*analysis.ast);
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

std::string touch_rule(const std::string& where_lines, const std::string& handler_lines = "        let n = 1\n") {
    return "rule Touch:\n" + kBodyLavaPairs + "    where:\n" + where_lines + "    on tick:\n" + handler_lines;
}

using enum ClauseStage;

}  // namespace

// ── Classification ──────────────────────────────────────────────────────────

TEST_CASE("Staging: pair predicates get the stage of what they read", "[semantic][staging]") {
    const auto analysis = analyze(touch_rule("        lava[Lava].active\n"
                                             "        Game[Match].running\n"
                                             "        body[Body].x > 0.0\n"
                                             "        body[Body].x < lava[Lava].heat\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Touch").where == std::vector{BindingRight, Activation, BindingLeft, Row});
}

TEST_CASE("Staging: a stage does not depend on predicate order", "[semantic][staging]") {
    const auto analysis = analyze(touch_rule("        body[Body].x < lava[Lava].heat\n"
                                             "        body[Body].x > 0.0\n"
                                             "        Game[Match].running\n"
                                             "        lava[Lava].active\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Touch").where == std::vector{Row, BindingLeft, Activation, BindingRight});
}

TEST_CASE("Staging: a join through a function call is row stage", "[semantic][staging]") {
    const auto analysis = analyze("func near(a: float, b: float) bool:\n"
                                  "    return a < b\n" +
                                  touch_rule("        near(body[Body].x, lava[Lava].heat)\n"
                                             "        near(1.0, lava[Lava].heat)\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Touch").where == std::vector{Row, BindingRight});
}

TEST_CASE("Staging: a unary predicate on the entity is row stage", "[semantic][staging]") {
    const auto analysis = analyze("rule Heal:\n"
                                  "    each:\n"
                                  "        Health as h\n"
                                  "    where:\n"
                                  "        h.hp > 0\n"
                                  "        Game[Match].running\n"
                                  "        hp < 10\n"
                                  "    on tick:\n"
                                  "        h.hp = h.hp + 1\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Heal").where == std::vector{Row, Activation, Row});
}

TEST_CASE("Staging: pair sort keys get the stage of what they read", "[semantic][staging]") {
    const auto analysis = analyze("rule Rank:\n" + kBodyLavaPairs +
                                  "    order by:\n"
                                  "        body[Body].x asc\n"
                                  "        lava[Lava].heat - body[Body].x desc\n"
                                  "        lava[Lava].heat asc\n"
                                  "    on tick:\n"
                                  "        let n = 1\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Rank").order_by == std::vector{BindingLeft, Row, BindingRight});
}

TEST_CASE("Staging: reduce per stages group predicates, reducer inputs and keys", "[semantic][staging]") {
    const auto analysis = analyze("rule TeamScore:\n" + kTeamPairs +
                                  "    where:\n"
                                  "        team[Team].active\n"
                                  "        player[Member].points > 0\n"
                                  "        player[Member].team == team\n"
                                  "        Game[Match].running\n"
                                  "    reduce per team:\n"
                                  "        n = count()\n"
                                  "        points = sum(player[Member].points)\n"
                                  "        mixed = max(team[Team].total + player[Member].points, default = 0)\n"
                                  "    order by:\n"
                                  "        points desc\n"
                                  "    on tick:\n"
                                  "        team[Team].crowded = n > mixed\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& staging = analysis.staging("TeamScore");
    CHECK(staging.where == std::vector{BindingLeft, BindingRight, Row, Activation});
    CHECK(staging.reducers == std::vector{Activation, BindingRight, Row});
    CHECK(staging.order_by == std::vector{Row});
}

TEST_CASE("Staging: a global unary reduction stages its inputs", "[semantic][staging]") {
    const auto analysis = analyze("rule Total:\n"
                                  "    each:\n"
                                  "        Health\n"
                                  "    where:\n"
                                  "        Game[Match].running\n"
                                  "    reduce:\n"
                                  "        sum_hp = sum(hp)\n"
                                  "    on tick:\n"
                                  "        Game[Score].total = sum_hp\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& staging = analysis.staging("Total");
    CHECK(staging.where == std::vector{Activation});
    CHECK(staging.reducers == std::vector{Row});
}

// ── Legality ────────────────────────────────────────────────────────────────

TEST_CASE("Staging: a named write the predicate reads demotes it to row", "[semantic][staging]") {
    const auto analysis = analyze("rule Close:\n"
                                  "    each:\n"
                                  "        Health\n"
                                  "    where:\n"
                                  "        Game[Score].open\n"
                                  "        Game[Match].running\n"
                                  "    on tick:\n"
                                  "        Game[Score].open = false\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Close").where == std::vector{Row, Activation});
}

TEST_CASE("Staging: accumulation on a read field demotes it to row", "[semantic][staging]") {
    const auto analysis = analyze("rule Count:\n"
                                  "    each:\n"
                                  "        Health\n"
                                  "    where:\n"
                                  "        Game[Score].total < 10\n"
                                  "    on tick:\n"
                                  "        Game[Score].total += 1\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Count").where == std::vector{Row});
}

TEST_CASE("Staging: a deferred set does not demote", "[semantic][staging]") {
    const auto analysis = analyze(touch_rule("        lava[Lava].active\n",
                                             "        set Lava on lava:\n"
                                             "            active = false\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Touch").where == std::vector{BindingRight});
}

TEST_CASE("Staging: a limited pass filters before its first invocation, so its writes don't demote",
          "[semantic][staging]") {
    const auto analysis = analyze("rule Grab:\n" + kTeamPairs +
                                  "    where:\n"
                                  "        team[Team].active\n"
                                  "        player[Member].points > 0\n"
                                  "    limit: 1 per team\n"
                                  "    on tick:\n"
                                  "        team[Team].active = false\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Grab").where == std::vector{BindingLeft, BindingRight});
}

TEST_CASE("Staging: a reduced pass folds before its handler runs, so its writes don't demote", "[semantic][staging]") {
    const auto analysis = analyze("rule Tally:\n" + kTeamPairs +
                                  "    where:\n"
                                  "        team[Team].total < 10\n"
                                  "        Game[Score].total < 10\n"
                                  "    reduce per team:\n"
                                  "        points = sum(player[Member].points)\n"
                                  "    on tick:\n"
                                  "        team[Team].total = points\n"
                                  "        Game[Score].total += points\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.staging("Tally").where == std::vector{BindingLeft, Activation});
}

TEST_CASE("Staging: whole-trait writes and unnamed field reads overlap any field", "[semantic][staging]") {
    const auto trait = make_symbol_id(SymbolKind::Trait, ModuleId{.name = "game"}, "Score");
    const auto other = make_symbol_id(SymbolKind::Trait, ModuleId{.name = "game"}, "Match");
    const std::unordered_map<SymbolId, FieldAccess> named_read{{trait, FieldAccess{.all = false, .fields = {"open"}}}};
    const std::unordered_map<SymbolId, FieldAccess> unnamed_read{{trait, FieldAccess{.all = true, .fields = {}}}};
    const std::unordered_map<SymbolId, FieldAccess> field_write{{trait, FieldAccess{.all = false, .fields = {"total"}}}};
    const std::unordered_map<SymbolId, FieldAccess> whole_write{{trait, FieldAccess{.all = true, .fields = {}}}};
    const std::unordered_map<SymbolId, FieldAccess> other_write{{other, FieldAccess{.all = true, .fields = {}}}};
    CHECK_FALSE(clause_reads_written(named_read, field_write));
    CHECK(clause_reads_written(named_read, whole_write));
    CHECK(clause_reads_written(unnamed_read, field_write));
    CHECK_FALSE(clause_reads_written(unnamed_read, other_write));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
