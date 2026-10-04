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
    "pub event load\n"
    "event Ping\n"
    "trait Team:\n"
    "    var id: int = 0\n"
    "    var active: bool = true\n"
    "    var total: int = 0\n"
    "    var best: float = 0.0\n"
    "    var crowded: bool = false\n"
    "trait Member:\n"
    "    var team: entity_id\n"
    "    var points: int = 0\n"
    "    var speed: float = 0.0\n"
    "    var ready: bool = false\n"
    "trait Enemy:\n"
    "    var threat: int = 0\n"
    "trait Stats:\n"
    "    var enemies: int = 0\n"
    "entity Board:\n"
    "    Stats\n";

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

    [[nodiscard]] bool has(const std::string& text) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error && d.message.contains(text);
        });
    }
    [[nodiscard]] bool clean() const {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
    [[nodiscard]] const RuleNode& rule(const std::string& name) const {
        for (const auto& decl : ast->declarations) {
            if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name == name) {
                return *rule;
            }
        }
        FAIL("no rule named " + name);
        std::unreachable();
    }
    [[nodiscard]] const InferredHandlerContract& contract(const std::string& rule_name) const {
        const auto found = std::ranges::find_if(
            program.handler_contracts, [&](const auto& contract) { return contract.rule.local_name == rule_name; });
        REQUIRE(found != program.handler_contracts.end());
        return *found;
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

std::string team_rule(const std::string& reduce_lines, const std::string& handler_lines,
                      const std::string& where_lines = "") {
    return "rule TeamScore:\n" + kTeamPairs + (where_lines.empty() ? "" : "    where:\n" + where_lines) +
           "    reduce:\n" + reduce_lines + "    on tick:\n" + handler_lines;
}

bool reads_trait(const HandlerContract& contract, const std::string& trait) {
    return std::ranges::any_of(contract.reads, [&](const SymbolId& id) { return id.local_name == trait; });
}

bool writes_trait(const HandlerContract& contract, const std::string& trait) {
    return std::ranges::any_of(contract.writes, [&](const SymbolId& id) { return id.local_name == trait; });
}

}  // namespace

// ── Reducer typing ──────────────────────────────────────────────────────────

TEST_CASE("Reduce: reducers have exact result types", "[semantic][rule-reduce]") {
    const auto analysis = analyze(team_rule("        per: team\n"
                                            "        rows = count()\n"
                                            "        players = count(player)\n"
                                            "        points = sum(player.Member.points)\n"
                                            "        speed = sum(player.Member.speed)\n"
                                            "        low = min(player.Member.points, default = 0)\n"
                                            "        fast = max(player.Member.speed, default = 0.0)\n"
                                            "        ready = any(player.Member.ready)\n",
                                            "        team.Team.total = rows + players + points + low\n"
                                            "        team.Team.best = speed + fast\n"
                                            "        team.Team.crowded = ready\n",
                                            "        player.Member.team == team\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& reducers = analysis.rule("TeamScore").reduce->reducers;
    REQUIRE(reducers.size() == 7);
    CHECK(reducers[0].result_type.kind == TypeKind::Int);
    CHECK(reducers[1].result_type.kind == TypeKind::Int);
    CHECK(reducers[2].result_type.kind == TypeKind::Int);
    CHECK(reducers[3].result_type.kind == TypeKind::Float);
    CHECK(reducers[4].result_type.kind == TypeKind::Int);
    CHECK(reducers[5].result_type.kind == TypeKind::Float);
    CHECK(reducers[6].result_type.kind == TypeKind::Bool);
}

TEST_CASE("Reduce: aggregate types are checked in the handler", "[semantic][rule-reduce]") {
    const auto analysis = analyze(team_rule("        per: team\n"
                                            "        ready = any(player.Member.ready)\n",
                                            "        let total: int = ready\n"));
    CHECK_FALSE(analysis.clean());
}

TEST_CASE("Reduce: min and max need a default of the input type", "[semantic][rule-reduce]") {
    CHECK(analyze(team_rule("        per: team\n"
                            "        low = min(player.Member.points, default = 0.0)\n",
                            "        let x = 1\n"))
              .has("default"));
    CHECK(analyze(team_rule("        per: team\n"
                            "        low = max(player.Member.speed)\n",
                            "        let x = 1\n"))
              .has("default"));
}

TEST_CASE("Reduce: sum, min and max take int or float", "[semantic][rule-reduce]") {
    CHECK(analyze(team_rule("        per: team\n"
                            "        s = sum(player.Member.ready)\n",
                            "        let x = 1\n"))
              .has("'int' or 'float'"));
    CHECK(analyze(team_rule("        per: team\n"
                            "        s = min(player.Member.team, default = team)\n",
                            "        let x = 1\n"))
              .has("'int' or 'float'"));
}

TEST_CASE("Reduce: any takes a bool", "[semantic][rule-reduce]") {
    CHECK(analyze(team_rule("        per: team\n"
                            "        a = any(player.Member.points)\n",
                            "        let x = 1\n"))
              .has("'bool'"));
}

TEST_CASE("Reduce: count of a name that is not a pair binding is rejected", "[semantic][rule-reduce]") {
    CHECK(analyze(team_rule("        per: team\n"
                            "        n = count(coach)\n",
                            "        let x = 1\n"))
              .has("pair binding"));
    CHECK(analyze("rule Count:\n"
                  "    filter:\n"
                  "        Enemy as e\n"
                  "    reduce:\n"
                  "        n = count(e)\n"
                  "    on tick:\n"
                  "        Board.Stats.enemies = n\n")
              .has("pair binding"));
}

TEST_CASE("Reduce: reducer inputs must be pure", "[semantic][rule-reduce]") {
    CHECK(analyze(team_rule("        per: team\n"
                            "        n = sum(query.count[Enemy]())\n",
                            "        let x = 1\n"))
              .has("pure"));
}

TEST_CASE("Reduce: reducer inputs resolve function calls", "[semantic][rule-reduce]") {
    const auto analysis = analyze("func doubled(value: int) int:\n"
                                  "    return value * 2\n" +
                                  team_rule("        per: team\n"
                                            "        total = sum(doubled(player.Member.points))\n",
                                            "        team.Team.total = total\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& input = *analysis.rule("TeamScore").reduce->reducers.front().input;
    const auto* call  = std::get_if<CallExpr>(&input.expr);
    REQUIRE(call != nullptr);
    REQUIRE(call->resolved_callee_id.has_value());
    CHECK(call->resolved_callee_id->local_name == "doubled");
}

TEST_CASE("Reduce: reducer names are unique and don't shadow bindings", "[semantic][rule-reduce]") {
    CHECK(analyze(team_rule("        per: team\n"
                            "        n = count()\n"
                            "        n = count(player)\n",
                            "        let x = 1\n"))
              .has("duplicate"));
    CHECK(analyze(team_rule("        per: team\n"
                            "        player = count()\n",
                            "        let x = 1\n"))
              .has("player"));
}

TEST_CASE("Reduce: aggregates are immutable", "[semantic][rule-reduce]") {
    CHECK_FALSE(analyze(team_rule("        per: team\n"
                                  "        n = count()\n",
                                  "        n = 3\n"))
                    .clean());
}

// ── Domains ─────────────────────────────────────────────────────────────────

TEST_CASE("Reduce: a selectionless rule rejects reduce", "[semantic][rule-reduce]") {
    CHECK(analyze("rule Nothing:\n"
                  "    reduce:\n"
                  "        n = count()\n"
                  "    on tick:\n"
                  "        Board.Stats.enemies = n\n")
              .has("`reduce:` requires a `filter:` or `pairs:` clause"));
}

TEST_CASE("Reduce: a global reduce over a filter domain is accepted", "[semantic][rule-reduce]") {
    const auto analysis = analyze("rule Count:\n"
                                  "    filter:\n"
                                  "        Enemy as e\n"
                                  "    reduce:\n"
                                  "        n = count()\n"
                                  "        threat = sum(e.threat)\n"
                                  "    on tick:\n"
                                  "        Board.Stats.enemies = n + threat\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Reduce: per needs a declared pair binding", "[semantic][rule-reduce]") {
    CHECK(analyze("rule Count:\n"
                  "    filter:\n"
                  "        Enemy as e\n"
                  "    reduce:\n"
                  "        per: e\n"
                  "        n = count()\n"
                  "    on tick:\n"
                  "        let x = n\n")
              .has("`per` requires a `pairs:` clause"));
    CHECK(analyze(team_rule("        per: coach\n"
                            "        n = count()\n",
                            "        let x = n\n"))
              .has("pair binding"));
}

TEST_CASE("Reduce: lifecycle triggers are rejected on a reduced rule", "[semantic][rule-reduce]") {
    CHECK_FALSE(analyze("rule Count:\n"
                        "    filter:\n"
                        "        Enemy as e\n"
                        "    reduce:\n"
                        "        n = count()\n"
                        "    on added Enemy:\n"
                        "        Board.Stats.enemies = n\n")
                    .clean());
}

// ── Scope after reduction ───────────────────────────────────────────────────

TEST_CASE("Reduce: the eliminated binding is not accessible", "[semantic][rule-reduce]") {
    CHECK_FALSE(analyze(team_rule("        per: team\n"
                                  "        n = count()\n",
                                  "        let p = player.Member.points\n"))
                    .clean());
    CHECK_FALSE(analyze(team_rule("        per: team\n"
                                  "        n = count()\n",
                                  "        emit Ping to player\n"))
                    .clean());
}

TEST_CASE("Reduce: a global reduce eliminates every binding", "[semantic][rule-reduce]") {
    CHECK_FALSE(analyze(team_rule("        n = count()\n", "        let p = team.Team.id\n")).clean());
    CHECK_FALSE(analyze("rule Count:\n"
                        "    filter:\n"
                        "        Enemy as e\n"
                        "    reduce:\n"
                        "        n = count()\n"
                        "    on tick:\n"
                        "        Board.Stats.enemies = e.threat\n")
                    .clean());
}

TEST_CASE("Reduce: the retained binding is writable", "[semantic][rule-reduce]") {
    const auto analysis = analyze(team_rule("        per: team\n"
                                            "        total = sum(player.Member.points)\n",
                                            "        team.Team.total = total\n",
                                            "        player.Member.team == team\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& contract = analysis.contract("TeamScore");
    CHECK(writes_trait(contract, "Team"));
    CHECK(reads_trait(contract, "Member"));
    REQUIRE(contract.reduction.has_value());
    REQUIRE(contract.reduction->group_binding.has_value());
    CHECK(*contract.reduction->group_binding == 0);
}

TEST_CASE("Reduce: reducer reads join the handler contract", "[semantic][rule-reduce]") {
    const auto analysis = analyze("rule Count:\n"
                                  "    filter:\n"
                                  "        Enemy as e\n"
                                  "    reduce:\n"
                                  "        threat = sum(e.threat)\n"
                                  "    on tick:\n"
                                  "        Board.Stats.enemies = threat\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& contract = analysis.contract("Count");
    CHECK(reads_trait(contract, "Enemy"));
    CHECK_FALSE(writes_trait(contract, "Enemy"));
    REQUIRE(contract.reduction.has_value());
    CHECK_FALSE(contract.reduction->group_binding.has_value());
}

// ── order by / limit after reduce ───────────────────────────────────────────

TEST_CASE("Reduce: sort keys read aggregates and the retained binding", "[semantic][rule-reduce]") {
    const auto analysis = analyze("rule Rank:\n" + kTeamPairs +
                                  "    reduce:\n"
                                  "        per: team\n"
                                  "        total = sum(player.Member.points)\n"
                                  "    order by:\n"
                                  "        total desc\n"
                                  "        team.Team.id\n"
                                  "    limit: 2\n"
                                  "    on tick:\n"
                                  "        team.Team.crowded = true\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Reduce: sort keys can't read the eliminated binding", "[semantic][rule-reduce]") {
    CHECK_FALSE(analyze("rule Rank:\n" + kTeamPairs +
                        "    reduce:\n"
                        "        per: team\n"
                        "        total = sum(player.Member.points)\n"
                        "    order by:\n"
                        "        player.Member.points\n"
                        "    on tick:\n"
                        "        let x = total\n")
                    .clean());
}

TEST_CASE("Reduce: a per-binding limit is rejected", "[semantic][rule-reduce]") {
    CHECK(analyze("rule Rank:\n" + kTeamPairs +
                  "    reduce:\n"
                  "        per: team\n"
                  "        total = sum(player.Member.points)\n"
                  "    limit: 1 per team\n"
                  "    on tick:\n"
                  "        let x = total\n")
              .has("`limit: ... per` cannot be combined with `reduce:`"));
}

// ── where: split ────────────────────────────────────────────────────────────

TEST_CASE("Reduce: a where predicate reading only the per binding filters groups", "[semantic][rule-reduce]") {
    const auto analysis = analyze(team_rule("        per: team\n"
                                            "        n = count()\n",
                                            "        team.Team.total = n\n",
                                            "        team.Team.active\n"
                                            "        player.Member.team == team\n"
                                            "        player.Member.points > 0\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& reduce = *analysis.rule("TeamScore").reduce;
    CHECK(reduce.group_predicates == std::vector<std::size_t>{0});
}

TEST_CASE("Reduce: without per every where predicate filters rows", "[semantic][rule-reduce]") {
    const auto analysis = analyze(team_rule("        n = count()\n",
                                            "        Board.Stats.enemies = n\n",
                                            "        team.Team.active\n"
                                            "        player.Member.team == team\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.rule("TeamScore").reduce->group_predicates.empty());
}
TEST_CASE("Reduce: a var local is reassignable in pair handlers", "[semantic][rule-reduce][pair-relations]") {
    const auto analysis = analyze("rule Plain:\n" + kTeamPairs +
                                  "    on tick:\n"
                                  "        var n = 0\n"
                                  "        n = 1\n" +
                                  team_rule("        per: team\n"
                                            "        total = sum(player.Member.points)\n",
                                            "        var best = total\n"
                                            "        best = best + 1\n"
                                            "        team.Team.total = best\n"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK_FALSE(analyze("rule Plain:\n" + kTeamPairs +
                        "    on tick:\n"
                        "        let n = 0\n"
                        "        n = 1\n")
                    .clean());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
