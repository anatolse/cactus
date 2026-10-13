// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
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
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "trait Parent:\n"
    "    var parent: entity_id\n"
    "trait Hp:\n"
    "    let max: int = 10\n"
    "    var current: int = 10\n"
    "trait Anchor:\n"
    "    let anchor: vec2 = vec2(0.0, 0.0)\n"
    "trait Team:\n"
    "    let cap: int = 4\n"
    "    var size: int = 0\n"
    "trait Member:\n"
    "    var points: int = 0\n"
    "trait Target:\n"
    "    var other: entity_id\n"
    "trait Glow:\n"
    "    let level: int = 0\n"
    "state Mode:\n"
    "    Idle\n"
    "    Attacking:\n"
    "        let power: int = 1\n"
    "entity Boss:\n"
    "    Hp\n"
    "    Anchor\n"
    "    Team\n"
    "    Member\n"
    "    Target\n"
    "    Mode.Idle\n";

const std::string kTeamPairs =
    "    pairs:\n"
    "        team:\n"
    "            Team\n"
    "        player:\n"
    "            Member\n";

const std::string kLetWrite = "can't assign let field";

std::string describe(const std::vector<Diagnostic>& diagnostics) {
    std::string text;
    for (const auto& d : diagnostics) {
        text += std::to_string(d.location.line) + ": " + d.message + "\n";
    }
    return text;
}

struct Analysis {
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] std::vector<std::string> errors() const {
        std::vector<std::string> messages;
        for (const auto& d : diagnostics) {
            if (d.level == DiagnosticLevel::Error) {
                messages.push_back(d.message);
            }
        }
        return messages;
    }
    [[nodiscard]] bool has(const std::string& text) const {
        return std::ranges::any_of(errors(), [&](const std::string& message) { return message.contains(text); });
    }
    [[nodiscard]] std::size_t count(const std::string& text) const {
        return static_cast<std::size_t>(
            std::ranges::count_if(errors(), [&](const std::string& message) { return message.contains(text); }));
    }
    [[nodiscard]] bool clean() const {
        return errors().empty();
    }
};

Analysis analyze(const std::string& body) {
    ErrorReporter errors;
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    INFO(describe(errors.diagnostics()));
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    (void)analyzer.analyze(ast);
    return Analysis{.diagnostics = errors.diagnostics()};
}

std::string let_write_message(const std::string& trait, const std::string& field) {
    return "can't assign let field '" + trait + "." + field + "'; give it a value with 'add " + trait +
           ":' or declare it 'var'";
}

void check_rejected(const std::string& body, const std::string& trait, const std::string& field) {
    const auto analysis = analyze(body);
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.has(let_write_message(trait, field)));
    CHECK(analysis.count(kLetWrite) == 1);
}

}  // namespace

// ── Rejected writes ─────────────────────────────────────────────────────────

TEST_CASE("Let fields: assignment through a filter alias is rejected", "[semantic][let-fields]") {
    check_rejected("rule Cap:\n"
                   "    each:\n"
                   "        Hp as h\n"
                   "    on tick:\n"
                   "        h.max = 5\n",
                   "Hp",
                   "max");
}

TEST_CASE("Let fields: compound assignment is rejected", "[semantic][let-fields]") {
    check_rejected("rule Cap:\n"
                   "    each:\n"
                   "        Hp as h\n"
                   "    on tick:\n"
                   "        h.max += 1\n",
                   "Hp",
                   "max");
}

TEST_CASE("Let fields: an implicit filter field write is rejected", "[semantic][let-fields]") {
    check_rejected("rule Cap:\n"
                   "    each:\n"
                   "        Hp\n"
                   "    on tick:\n"
                   "        max = 5\n",
                   "Hp",
                   "max");
}

TEST_CASE("Let fields: a bare field writes the first selected trait that declares it", "[semantic][let-fields]") {
    const std::string limit = "trait Limit:\n"
                              "    var max: int = 1\n";
    check_rejected(limit +
                       "rule Cap:\n"
                       "    each:\n"
                       "        Hp\n"
                       "        Limit\n"
                       "    on tick:\n"
                       "        max = 5\n",
                   "Hp",
                   "max");
    const auto analysis = analyze(limit +
                                  "rule Cap:\n"
                                  "    each:\n"
                                  "        Limit\n"
                                  "        Hp\n"
                                  "    on tick:\n"
                                  "        max = 5\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.count(kLetWrite) == 0);
}

TEST_CASE("Let fields: a member write into a let vector field is rejected", "[semantic][let-fields]") {
    check_rejected("rule Pin:\n"
                   "    each:\n"
                   "        Anchor as a\n"
                   "    on tick:\n"
                   "        a.anchor.x = 1.0\n",
                   "Anchor",
                   "anchor");
}

TEST_CASE("Let fields: a write through a retained reduce binding is rejected", "[semantic][let-fields]") {
    check_rejected("rule Fill:\n" + kTeamPairs +
                       "    reduce per team:\n"
                       "        n = count()\n"
                       "    on tick:\n"
                       "        team[Team].cap = 3\n",
                   "Team",
                   "cap");
}

TEST_CASE("Let fields: a write through a provably-one limit binding is rejected", "[semantic][let-fields]") {
    check_rejected("rule Fill:\n" + kTeamPairs +
                       "    limit: 1 per team\n"
                       "    on tick:\n"
                       "        team[Team].cap = 3\n",
                   "Team",
                   "cap");
}

TEST_CASE("Let fields: a write through a named entity is rejected", "[semantic][let-fields]") {
    check_rejected("rule Buff:\n"
                   "    on tick:\n"
                   "        Boss[Hp].max = 50\n",
                   "Hp",
                   "max");
}

TEST_CASE("Let fields: a write through a lifecycle alias is rejected", "[semantic][let-fields]") {
    check_rejected("rule Spawned:\n"
                   "    on added Hp as h:\n"
                   "        h.max = 50\n",
                   "Hp",
                   "max");
}

TEST_CASE("Let fields: a write through a trait-match alias is rejected", "[semantic][let-fields]") {
    check_rejected("rule Strike:\n"
                   "    each:\n"
                   "        Target as t\n"
                   "    on tick:\n"
                   "        match t.other:\n"
                   "            Hp as h =>\n"
                   "                h.max = 5\n",
                   "Hp",
                   "max");
}

TEST_CASE("Let fields: a write through a state-match alias is rejected", "[semantic][let-fields]") {
    check_rejected("rule Swing:\n"
                   "    each:\n"
                   "        Mode as m\n"
                   "    on tick:\n"
                   "        match m:\n"
                   "            Mode.Idle =>\n"
                   "                let x = 0\n"
                   "            Mode.Attacking as a =>\n"
                   "                a.power = 2\n",
                   "Mode.Attacking",
                   "power");
}

TEST_CASE("Let fields: every rejected write is reported", "[semantic][let-fields]") {
    const auto analysis = analyze("rule First:\n"
                                  "    each:\n"
                                  "        Hp as h\n"
                                  "        Anchor as a\n"
                                  "    on tick:\n"
                                  "        h.max = 1\n"
                                  "        a.anchor = vec2(1.0, 1.0)\n"
                                  "rule Second:\n"
                                  "    each:\n"
                                  "        Team as t\n"
                                  "    on tick:\n"
                                  "        t.cap = 2\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.count(kLetWrite) == 3);
    CHECK(analysis.has(let_write_message("Hp", "max")));
    CHECK(analysis.has(let_write_message("Anchor", "anchor")));
    CHECK(analysis.has(let_write_message("Team", "cap")));
}

// ── Accepted forms ──────────────────────────────────────────────────────────

TEST_CASE("Let fields: values given when the trait value is made are accepted", "[semantic][let-fields]") {
    const auto analysis = analyze("template Grunt(cap: int = 4):\n"
                                  "    Hp:\n"
                                  "        max = cap\n"
                                  "template Rig:\n"
                                  "    Hp\n"
                                  "    children:\n"
                                  "        entity Arm from Grunt:\n"
                                  "            Hp:\n"
                                  "                max = 2\n"
                                  "entity Brute from Grunt(cap = 3)\n"
                                  "entity Tank:\n"
                                  "    Hp:\n"
                                  "        max = 50\n"
                                  "entity Frame from Rig:\n"
                                  "    Hp:\n"
                                  "        max = 9\n"
                                  "rule Grow:\n"
                                  "    each:\n"
                                  "        Hp as h\n"
                                  "    on tick:\n"
                                  "        spawn Grunt:\n"
                                  "            Hp:\n"
                                  "                max = 7\n"
                                  "        add Hp:\n"
                                  "            max = h.max + 1\n"
                                  "        project Glow:\n"
                                  "            level = 3\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.clean());
}

TEST_CASE("Let fields: reading a let field is accepted", "[semantic][let-fields]") {
    const auto analysis = analyze("rule Read:\n"
                                  "    each:\n"
                                  "        Hp as h\n"
                                  "    where:\n"
                                  "        h.max > 0\n"
                                  "    on tick:\n"
                                  "        let cap = h.max\n"
                                  "        h.current = cap\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.clean());
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
