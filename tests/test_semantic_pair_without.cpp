// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
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
    "group links:\n"
    "    phase: tick\n"
    "trait Unit:\n"
    "    let team: int = 0\n"
    "    var hp: int = 0\n"
    "trait Shielded:\n"
    "    var power: int = 0\n"
    "trait Dead\n"
    "trait Frozen\n"
    "trait Guard\n"
    "trait Log:\n"
    "    var n: int = 0\n"
    "trait A\n"
    "trait B\n"
    "state Alert:\n"
    "    Asleep\n"
    "    Awake\n"
    "    Alarmed\n"
    "relation Claim:\n"
    "    from: Unit\n"
    "    to: Unit\n"
    "entity Book:\n"
    "    Log\n"
    "entity Grunt:\n"
    "    Unit\n"
    "entity Hero:\n"
    "    Unit\n"
    "    Shielded\n"
    "entity Corpse:\n"
    "    Unit\n"
    "    Dead\n"
    "entity Ice:\n"
    "    Frozen\n"
    "entity Post:\n"
    "    Guard\n"
    "entity Sleeper:\n"
    "    Guard\n"
    "    Alert.Asleep\n"
    "entity Watcher:\n"
    "    Guard\n"
    "    Alert.Awake\n"
    "entity Siren:\n"
    "    Guard\n"
    "    Alert.Alarmed\n";

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
    [[nodiscard]] const InferredHandlerContract& contract(const std::string& rule_name) const {
        const auto found = std::ranges::find_if(
            program.handler_contracts, [&](const auto& contract) { return contract.rule.local_name == rule_name; });
        REQUIRE(found != program.handler_contracts.end());
        return *found;
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
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ": " + d.message + "\n";
    }
    return text;
}

// Parses, analyzes and runs the whole-program passes, stopping at the first stage with errors.
Analysis analyze(const std::string& body) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    if (!errors.has_errors()) {
        SemanticAnalyzer analyzer(errors);
        analysis.program = analyzer.analyze(*analysis.ast);
        if (!errors.has_errors()) {
            run_whole_program_passes(analysis.program, errors);
        }
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

SymbolId trait_id(const std::string& local_name) {
    return make_symbol_id(SymbolKind::Trait, "game", local_name);
}

// A pair rule over `a: Unit` and an `m` binding with `m_block` as its indented body.
std::string strike(const std::string& m_block, const std::string& body = "        Book[Log].n += 1\n") {
    return "rule Strike:\n"
           "    pairs:\n"
           "        a: Unit\n"
           "        m:\n" +
           m_block +
           "    where:\n"
           "        a != m\n"
           "    on tick:\n" +
           body;
}

std::vector<SymbolId> excluded_of(const Analysis& analysis, const std::string& rule, std::size_t binding) {
    const auto& bindings = analysis.contract(rule).pair_bindings;
    REQUIRE(bindings.size() > binding);
    return bindings[binding].excluded_traits;
}

// The possible sources recorded on the handler's `add` of `variant`, by variant name.
std::vector<std::string> sources_of(const Analysis& analysis, const std::string& rule, const std::string& variant) {
    const auto& commands = analysis.contract(rule).commands;
    const auto found     = std::ranges::find_if(commands, [&](const InferredHandlerCommand& command) {
        return command.kind == HandlerCommandKind::Add && command.target == trait_id(variant);
    });
    REQUIRE(found != commands.end());
    std::vector<std::string> names;
    for (const auto& source : found->sources) {
        names.push_back(source.variant);
    }
    return names;
}

// The commit round a keep rule runs in; 0 is its group's place.
std::uint32_t depth_of(const Analysis& analysis, const std::string& rule) {
    for (const auto& round : analysis.program.execution_graph.keep_rounds) {
        if (std::ranges::any_of(round.handlers,
                                [&](const HandlerIdentity& handler) { return handler.rule.local_name == rule; })) {
            return round.depth;
        }
    }
    return 0;
}

using Names = std::vector<std::string>;

}  // namespace

// ── Accepted forms ──────────────────────────────────────────────────────────

TEST_CASE("Pair without: a block binding excludes one trait", "[semantic][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            Unit\n"
                                         "            without: Shielded\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& binding = analysis.rule("Strike").pairs->bindings[1];
    REQUIRE(binding.exclude.entries.size() == 1);
    CHECK(binding.exclude.entries.front().qualified_name == "Shielded");
    CHECK(excluded_of(analysis, "Strike", 1) == std::vector{trait_id("Shielded")});
    CHECK(excluded_of(analysis, "Strike", 0).empty());
    CHECK(analysis.contract("Strike").observes_membership(trait_id("Shielded")));
}

TEST_CASE("Pair without: a binding excludes several traits", "[semantic][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            Unit as unit\n"
                                         "            without: Shielded, Dead\n",
                                         "        Book[Log].n += unit.hp\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(excluded_of(analysis, "Strike", 1) == std::vector{trait_id("Shielded"), trait_id("Dead")});
}

TEST_CASE("Pair without: a binding excludes a state or a state variant", "[semantic][pair-relations][pair-without]") {
    const auto rule = [](const std::string& excluded) {
        return "rule Watch:\n"
               "    pairs:\n"
               "        a: Unit\n"
               "        g:\n"
               "            Guard\n"
               "            without: " +
               excluded +
               "\n"
               "    on tick:\n"
               "        Book[Log].n += 1\n";
    };
    const auto state = analyze(rule("Alert"));
    INFO(describe(state));
    REQUIRE(state.clean());
    CHECK(excluded_of(state, "Watch", 1) == std::vector{trait_id("Alert")});

    const auto variant = analyze(rule("Alert.Asleep"));
    INFO(describe(variant));
    REQUIRE(variant.clean());
    CHECK(excluded_of(variant, "Watch", 1) == std::vector{trait_id("Alert.Asleep")});
}

// ── Errors ──────────────────────────────────────────────────────────────────

TEST_CASE("Pair without: a row binding takes no without", "[semantic][pair-relations][pair-without]") {
    const auto analysis = analyze(
        "rule Settle:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        r:\n"
        "            Claim\n"
        "            without: Dead\n"
        "    where:\n"
        "        r.from == a\n"
        "    on tick:\n"
        "        Book[Log].n += 1\n");
    INFO(describe(analysis));
    CHECK(analysis.has("a row binding selects rows, not entities"));
}

TEST_CASE("Pair without: the inline form is rejected with the block form as help",
          "[parser][pair-relations][pair-without]") {
    const auto analysis = analyze(
        "rule Strike:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        m: Unit without: Shielded\n"
        "    on tick:\n"
        "        Book[Log].n += 1\n");
    INFO(describe(analysis));
    CHECK(analysis.has("block form"));
    CHECK(analysis.has("without: Shielded"));
}

TEST_CASE("Pair without: a second without line is rejected", "[parser][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            Unit\n"
                                         "            without: Shielded\n"
                                         "            without: Dead\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("duplicate `without:` in pair binding 'm'"));
}

TEST_CASE("Pair without: a trait entry after without is rejected", "[parser][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            without: Shielded\n"
                                         "            Unit\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("trait entries of pair binding 'm' must precede `without:`"));
}

TEST_CASE("Pair without: a trait both required and excluded is rejected", "[semantic][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            Unit\n"
                                         "            Shielded\n"
                                         "            without: Shielded\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("pair binding 'm' both requires and excludes 'Shielded'"));
}

TEST_CASE("Pair without: an excluded trait takes no alias", "[parser][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            Unit\n"
                                         "            without: Shielded as s\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("an excluded trait takes no alias"));
}

TEST_CASE("Pair without: reading an excluded trait is rejected", "[semantic][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            Unit\n"
                                         "            without: Shielded\n",
                                         "        Book[Log].n += m[Shielded].power\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("pair binding 'm' excludes trait 'Shielded'"));
}

TEST_CASE("Pair without: a top-level without next to pairs stays an error", "[parser][pair-relations][pair-without]") {
    const auto analysis = analyze(
        "rule Strike:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        m: Unit\n"
        "    without: Shielded\n"
        "    on tick:\n"
        "        Book[Log].n += 1\n");
    INFO(describe(analysis));
    CHECK(analysis.has("'without:' cannot be combined with 'pairs:'"));
    CHECK(analysis.has("put `without:` under the binding it filters"));
}

// ── Domain analyses ─────────────────────────────────────────────────────────

TEST_CASE("Pair without: an exclusion that excludes nothing is an error", "[semantic][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            Unit\n"
                                         "            without: Frozen\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("`without: game.Frozen` on pair binding 'm' excludes nothing"));
}

TEST_CASE("Pair without: a binding no archetype matches never runs", "[semantic][pair-relations][pair-without]") {
    const auto analysis = analyze(strike("            Shielded\n"
                                         "            without: Unit\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("pair binding 'm' matches no reachable archetype (it needs game.Shielded without game.Unit)"));
}

TEST_CASE("Pair without: an excluded variant narrows the source of an add", "[semantic][pair-relations][pair-without]") {
    const auto analysis = analyze(
        "rule Rouse:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        g:\n"
        "            Guard\n"
        "            Alert\n"
        "            without: Alert.Awake\n"
        "    on tick:\n"
        "        add Alert.Alarmed to g\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Rouse", "Alert.Alarmed") == Names{"Asleep"});
}

TEST_CASE("Pair without: a keep rule depends on the keeper of an excluded trait",
          "[semantic][pair-relations][pair-without][rule-keep]") {
    const auto analysis = analyze(
        "rule AFact:\n"
        "    each:\n"
        "        Unit as u\n"
        "    group: links\n"
        "    where:\n"
        "        u.hp > 0\n"
        "    keep A\n"
        "rule BFact:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        m:\n"
        "            Unit\n"
        "            without: A\n"
        "    group: links\n"
        "    where:\n"
        "        a != m\n"
        "    keep B on m\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(depth_of(analysis, "AFact") == 0);
    CHECK(depth_of(analysis, "BFact") == 1);
}

TEST_CASE("Pair without: a keep cycle through an exclusion is reported",
          "[semantic][pair-relations][pair-without][rule-keep]") {
    const auto analysis = analyze(
        "rule AFact:\n"
        "    each:\n"
        "        Unit\n"
        "    without:\n"
        "        B\n"
        "    group: links\n"
        "    keep A\n"
        "rule BFact:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        m:\n"
        "            Unit\n"
        "            without: A\n"
        "    group: links\n"
        "    where:\n"
        "        a != m\n"
        "    keep B on m\n");
    INFO(describe(analysis));
    CHECK(analysis.has("keep rules form a loop through their kept traits"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
