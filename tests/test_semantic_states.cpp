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
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "trait Body:\n"
    "    var x: float = 0.0\n"
    "trait Log:\n"
    "    var n: int = 0\n"
    "trait Hero\n"
    "state EnemyMode:\n"
    "    Idle\n"
    "    Chasing:\n"
    "        var target: entity_id\n"
    "    Attacking:\n"
    "        var windup: float = 0.3\n"
    "    Dying final:\n"
    "        var elapsed: float = 0.0\n"
    "state JumpPhase:\n"
    "    Grounded\n"
    "    Rising\n"
    "entity Player:\n"
    "    Hero\n";

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
    [[nodiscard]] const EntityNode& entity(const std::string& name) const {
        for (const auto& decl : ast->declarations) {
            if (const auto* entity = std::get_if<EntityNode>(&decl); entity != nullptr && entity->name == name) {
                return *entity;
            }
        }
        FAIL("no entity named " + name);
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

Analysis analyze_source(const std::string& source) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(source, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program     = analyzer.analyze(*analysis.ast);
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

Analysis analyze(const std::string& body) {
    return analyze_source("module game\n" + kPrelude + body);
}

SymbolId trait_id(const std::string& local_name) {
    return make_symbol_id(SymbolKind::Trait, "game", local_name);
}

const std::vector<std::string> kModeTraits{
    "EnemyMode", "EnemyMode.Idle", "EnemyMode.Chasing", "EnemyMode.Attacking", "EnemyMode.Dying"};

bool writes_whole_state(const HandlerContract& contract) {
    return std::ranges::all_of(kModeTraits,
                               [&](const std::string& name) { return contract.writes.contains(trait_id(name)); });
}

std::vector<std::string> archetype_trait_names(const EntityNode& entity) {
    std::vector<std::string> names;
    for (const auto& entry : entity.traits) {
        REQUIRE(entry.resolved_trait_id.has_value());
        names.push_back(entry.resolved_trait_id->local_name);
    }
    return names;
}

}  // namespace

// ── Declarations ────────────────────────────────────────────────────────────

TEST_CASE("Semantic: a state declares one trait per variant and a slot", "[semantic][exclusive-states]") {
    const auto analysis = analyze("");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& states = analysis.program.states;
    REQUIRE(states.contains("EnemyMode"));
    const auto& mode = states.at("EnemyMode");
    REQUIRE(mode.variants.size() == 4);
    CHECK(mode.variants[0].name == "Idle");
    CHECK(mode.variants[3].name == "Dying");
    CHECK_FALSE(mode.variants[0].is_final);
    CHECK(mode.variants[3].is_final);
    for (const auto& name : kModeTraits) {
        REQUIRE(analysis.program.traits.contains(name));
        CHECK(analysis.program.traits.at(name).state == make_symbol_id(SymbolKind::State, "game", "EnemyMode"));
    }
    CHECK(analysis.program.traits.at("EnemyMode.Chasing").fields.size() == 1);
    CHECK(analysis.program.traits.at("EnemyMode.Idle").fields.empty());
    CHECK(analysis.program.traits.at("EnemyMode").is_state_slot());
    CHECK_FALSE(analysis.program.traits.at("EnemyMode.Idle").is_state_slot());
}

TEST_CASE("Semantic: a duplicate state variant is rejected", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "state Mode:\n"
        "    Idle\n"
        "    Idle\n");
    CHECK(analysis.has("duplicate variant 'Idle' in state 'Mode'"));
}

TEST_CASE("Semantic: a state without variants is rejected", "[semantic][exclusive-states]") {
    const auto analysis = analyze("state Empty:\n");
    CHECK(analysis.has("state 'Empty' has no variants"));
}

TEST_CASE("Semantic: a trait and a state cannot share a name", "[semantic][exclusive-states]") {
    const auto analysis = analyze("trait JumpPhase\n");
    CHECK(analysis.has("'JumpPhase'"));
    CHECK(analysis.has("state"));
}

TEST_CASE("Semantic: a state variant cannot declare a persist field", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "state Life:\n"
        "    Alive\n"
        "    Gone:\n"
        "        persist var elapsed: float = 0.0\n");
    CHECK(analysis.has("state variant 'Life.Gone' cannot declare a `persist` field"));
}

TEST_CASE("Semantic: a state variant cannot be kept or projected", "[semantic][exclusive-states]") {
    const auto kept = analyze(
        "rule Touch:\n"
        "    pairs:\n"
        "        a:\n"
        "            Body\n"
        "        b:\n"
        "            Log\n"
        "    where:\n"
        "        a.Body.x < 1.0\n"
        "    reduce:\n"
        "        per: b\n"
        "        n = count()\n"
        "    keep EnemyMode.Idle on b\n");
    CHECK(kept.has("state variant 'EnemyMode.Idle' cannot be kept"));

    const auto projected = analyze(
        "rule Show:\n"
        "    filter:\n"
        "        Body\n"
        "    on tick:\n"
        "        project EnemyMode.Idle\n");
    CHECK(projected.has("state variant 'EnemyMode.Idle' cannot be projected"));
}

// ── Variants are traits ─────────────────────────────────────────────────────

TEST_CASE("Semantic: a variant works wherever a trait does", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Chase:\n"
        "    filter:\n"
        "        EnemyMode.Chasing as c\n"
        "        Log as log\n"
        "    exclude:\n"
        "        EnemyMode.Dying\n"
        "    on tick:\n"
        "        log.n = if c.target == Player: 1 else: 0\n"
        "        set EnemyMode.Attacking on c.target:\n"
        "            windup = 1.0\n"
        "rule Enter:\n"
        "    filter:\n"
        "        Log as log\n"
        "    on added EnemyMode.Chasing as c:\n"
        "        log.n = 1\n"
        "rule Leave:\n"
        "    filter:\n"
        "        Log as log\n"
        "    on removed EnemyMode.Attacking as old:\n"
        "        log.n = if old.windup > 0.0: 1 else: 2\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& chase = analysis.contract("Chase");
    CHECK(std::ranges::find(chase.selection, trait_id("EnemyMode.Chasing")) != chase.selection.end());
    CHECK(std::ranges::find(chase.exclusion, trait_id("EnemyMode.Dying")) != chase.exclusion.end());
    CHECK(chase.reads.contains(trait_id("EnemyMode.Chasing")));
}

TEST_CASE("Semantic: a variant is a query type argument", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Count:\n"
        "    filter:\n"
        "        Log as log\n"
        "    on tick:\n"
        "        for child in query.children[EnemyMode.Dying](of = self):\n"
        "            log.n += 1\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());

    const auto unknown = analyze(
        "rule Count:\n"
        "    filter:\n"
        "        Log as log\n"
        "    on tick:\n"
        "        for child in query.children[EnemyMode.Gone](of = self):\n"
        "            log.n += 1\n");
    CHECK(unknown.has("EnemyMode.Gone"));
}

TEST_CASE("Semantic: a bare variant name is not a trait", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Chase:\n"
        "    filter:\n"
        "        Chasing\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(analysis.has("unknown trait 'Chasing'"));
}

TEST_CASE("Semantic: an unknown variant of a state is not a trait", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Chase:\n"
        "    filter:\n"
        "        EnemyMode.Fleeing\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(analysis.has("state 'EnemyMode' has no variant 'Fleeing'"));
}

// ── The slot ────────────────────────────────────────────────────────────────

TEST_CASE("Semantic: filter and exclude on a state use its slot", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Inspect:\n"
        "    filter:\n"
        "        EnemyMode as m\n"
        "        Log as log\n"
        "    exclude:\n"
        "        JumpPhase\n"
        "    on tick:\n"
        "        match m:\n"
        "            EnemyMode.Chasing as c =>\n"
        "                log.n = 1\n"
        "            _ =>\n"
        "                log.n = 0\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& inspect = analysis.contract("Inspect");
    CHECK(std::ranges::find(inspect.selection, trait_id("EnemyMode")) != inspect.selection.end());
    CHECK(std::ranges::find(inspect.exclusion, trait_id("JumpPhase")) != inspect.exclusion.end());
    CHECK(inspect.reads.contains(trait_id("EnemyMode")));
}

TEST_CASE("Semantic: a state slot is only a match subject", "[semantic][exclusive-states]") {
    const auto as_value = analyze(
        "rule Inspect:\n"
        "    filter:\n"
        "        EnemyMode as m\n"
        "    on tick:\n"
        "        let x = m\n");
    CHECK(as_value.has("a state slot can only be a `match` subject"));

    const auto as_field = analyze(
        "rule Inspect:\n"
        "    filter:\n"
        "        EnemyMode as m\n"
        "        Log as log\n"
        "    on tick:\n"
        "        log.n = m.index\n");
    CHECK(as_field.has("a state slot can only be a `match` subject"));
}

TEST_CASE("Semantic: remove names the state, not a variant", "[semantic][exclusive-states]") {
    const auto variant = analyze(
        "rule Calm:\n"
        "    filter:\n"
        "        Body\n"
        "    on tick:\n"
        "        remove EnemyMode.Chasing\n");
    CHECK(variant.has("a state variant cannot be removed; use `add` of another variant or `remove EnemyMode`"));

    const auto slot = analyze(
        "rule Calm:\n"
        "    filter:\n"
        "        Body\n"
        "    on tick:\n"
        "        remove EnemyMode\n");
    INFO(describe(slot));
    CHECK(slot.clean());
}

TEST_CASE("Semantic: add needs a variant, not the bare state", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Calm:\n"
        "    filter:\n"
        "        Body\n"
        "    on tick:\n"
        "        add EnemyMode\n");
    CHECK(analysis.has("`add` needs a variant of state 'EnemyMode'"));
}

// ── Archetypes ──────────────────────────────────────────────────────────────

TEST_CASE("Semantic: a bare state in an entity starts in its first variant", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "entity Grunt:\n"
        "    EnemyMode\n"
        "    Body\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(archetype_trait_names(analysis.entity("Grunt")) == std::vector<std::string>{"EnemyMode.Idle", "Body"});
}

TEST_CASE("Semantic: an archetype names at most one variant per state", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "entity Grunt:\n"
        "    EnemyMode.Idle\n"
        "    EnemyMode.Chasing:\n"
        "        target = Player\n");
    CHECK(analysis.has("entity 'Grunt' names two variants of state 'EnemyMode'"));
}

TEST_CASE("Semantic: an entity variant replaces its template's", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "template Enemy:\n"
        "    EnemyMode.Idle\n"
        "    Body\n"
        "entity Boss from Enemy:\n"
        "    EnemyMode.Chasing:\n"
        "        target = Player\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(archetype_trait_names(analysis.entity("Boss")) == std::vector<std::string>{"EnemyMode.Chasing", "Body"});
}

// ── Contracts ───────────────────────────────────────────────────────────────

TEST_CASE("Semantic: add and remove of a state write the whole state", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Hunt:\n"
        "    filter:\n"
        "        Body\n"
        "    on tick:\n"
        "        add EnemyMode.Chasing:\n"
        "            target = Player\n"
        "rule Calm:\n"
        "    filter:\n"
        "        Log\n"
        "    on tick:\n"
        "        remove EnemyMode\n"
        "rule Wind:\n"
        "    filter:\n"
        "        EnemyMode.Attacking as a\n"
        "    on tick:\n"
        "        a.windup -= 0.1\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(writes_whole_state(analysis.contract("Hunt")));
    CHECK(writes_whole_state(analysis.contract("Calm")));
    CHECK_FALSE(analysis.contract("Wind").writes.contains(trait_id("EnemyMode")));

    const auto& edges = analysis.program.execution_graph.schedule_edges;
    const auto orders = [&](const std::string& first, const std::string& second) {
        return std::ranges::any_of(edges, [&](const ScheduleEdge& edge) {
            return edge.before.rule.local_name == first && edge.after.rule.local_name == second;
        });
    };
    CHECK(orders("Hunt", "Wind"));
    CHECK(orders("Hunt", "Calm"));
}

// ── State match ─────────────────────────────────────────────────────────────

namespace {

std::string inspect_rule(const std::string& arms) {
    return "rule Inspect:\n"
           "    filter:\n"
           "        EnemyMode as m\n"
           "        Log as log\n"
           "    on tick:\n"
           "        match m:\n" +
           arms;
}

}  // namespace

TEST_CASE("Semantic: an exhaustive state match binds variant data", "[semantic][exclusive-states]") {
    const auto analysis = analyze(inspect_rule(
        "            EnemyMode.Idle =>\n"
        "                log.n = 0\n"
        "            EnemyMode.Chasing as c =>\n"
        "                log.n = if c.target == Player: 1 else: 2\n"
        "            EnemyMode.Attacking as a =>\n"
        "                a.windup -= 0.1\n"
        "            EnemyMode.Dying as d =>\n"
        "                d.elapsed += 0.1\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& inspect = analysis.contract("Inspect");
    CHECK(inspect.writes.contains(trait_id("EnemyMode.Attacking")));
    CHECK(inspect.writes.contains(trait_id("EnemyMode.Dying")));
    CHECK(inspect.reads.contains(trait_id("EnemyMode.Chasing")));
}

TEST_CASE("Semantic: a state match names its missing variants", "[semantic][exclusive-states]") {
    const auto analysis = analyze(inspect_rule(
        "            EnemyMode.Idle =>\n"
        "                log.n = 0\n"
        "            EnemyMode.Chasing =>\n"
        "                log.n = 1\n"));
    CHECK(analysis.has("is missing `EnemyMode.Attacking`, `EnemyMode.Dying`"));
}

TEST_CASE("Semantic: a state match arm must be a variant of the subject's state", "[semantic][exclusive-states]") {
    const auto analysis = analyze(inspect_rule(
        "            JumpPhase.Rising =>\n"
        "                log.n = 0\n"
        "            _ =>\n"
        "                log.n = 1\n"));
    CHECK(analysis.has("`JumpPhase.Rising` is not a variant of `EnemyMode`"));
}

TEST_CASE("Semantic: a state match rejects duplicate and misplaced arms", "[semantic][exclusive-states]") {
    const auto duplicate = analyze(inspect_rule(
        "            EnemyMode.Idle =>\n"
        "                log.n = 0\n"
        "            EnemyMode.Idle =>\n"
        "                log.n = 1\n"
        "            _ =>\n"
        "                log.n = 2\n"));
    CHECK(duplicate.has("duplicate pattern `EnemyMode.Idle`"));

    const auto wildcard = analyze(inspect_rule(
        "            _ =>\n"
        "                log.n = 2\n"
        "            EnemyMode.Idle =>\n"
        "                log.n = 0\n"));
    CHECK(wildcard.has("`_` must be the last arm of a `match`"));
}

TEST_CASE("Semantic: a marker variant arm cannot declare an alias", "[semantic][exclusive-states]") {
    const auto analysis = analyze(inspect_rule(
        "            EnemyMode.Idle as i =>\n"
        "                log.n = 0\n"
        "            _ =>\n"
        "                log.n = 1\n"));
    CHECK(analysis.has("marker variant 'EnemyMode.Idle' cannot declare an alias"));
}

TEST_CASE("Semantic: the match expression accepts a state slot", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Inspect:\n"
        "    filter:\n"
        "        EnemyMode as m\n"
        "        Log as log\n"
        "    on tick:\n"
        "        log.n = match m:\n"
        "            EnemyMode.Idle => 1\n"
        "            EnemyMode.Chasing => 2\n"
        "            _ => 3\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());

    const auto incomplete = analyze(
        "rule Inspect:\n"
        "    filter:\n"
        "        EnemyMode as m\n"
        "        Log as log\n"
        "    on tick:\n"
        "        log.n = match m:\n"
        "            EnemyMode.Idle => 1\n");
    CHECK(incomplete.has("is missing `EnemyMode.Chasing`"));
}

TEST_CASE("Semantic: a state is not a value type", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "func code(m: EnemyMode) int:\n"
        "    match m:\n"
        "        EnemyMode.Idle =>\n"
        "            return 0\n"
        "        _ =>\n"
        "            return 1\n");
    CHECK(analysis.has("state 'EnemyMode' is not a value type"));
}

TEST_CASE("Semantic: the statement match subject diagnostic lists state slots", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule R:\n"
        "    filter:\n"
        "        Body as b\n"
        "    on tick:\n"
        "        match b.x:\n"
        "            _ =>\n"
        "                b.x = 0.0\n");
    CHECK(analysis.has(
        "statement-level `match` subject must be `entity_id`, an enum, `int`, `bool` or a state slot, got `float`"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
