// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
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
        "        a[Body].x < 1.0\n"
        "    reduce per b:\n"
        "        n = count()\n"
        "    keep EnemyMode.Idle on b\n");
    CHECK(kept.has("state variant 'EnemyMode.Idle' cannot be kept"));

    const auto projected = analyze(
        "rule Show:\n"
        "    each:\n"
        "        Body\n"
        "    on tick:\n"
        "        project EnemyMode.Idle\n");
    CHECK(projected.has("state variant 'EnemyMode.Idle' cannot be projected"));
}

// ── Variants are traits ─────────────────────────────────────────────────────

TEST_CASE("Semantic: a variant works wherever a trait does", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Chase:\n"
        "    each:\n"
        "        EnemyMode.Chasing as c\n"
        "        Log as log\n"
        "    without:\n"
        "        EnemyMode.Dying\n"
        "    on tick:\n"
        "        log.n = if c.target == Player: 1 else: 0\n"
        "        set EnemyMode.Attacking on c.target:\n"
        "            windup = 1.0\n"
        "rule Enter:\n"
        "    each:\n"
        "        Log as log\n"
        "    on added EnemyMode.Chasing as c:\n"
        "        log.n = 1\n"
        "rule Leave:\n"
        "    each:\n"
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
        "    each:\n"
        "        Log as log\n"
        "    on tick:\n"
        "        for child in query.children[EnemyMode.Dying](of = self):\n"
        "            log.n += 1\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());

    const auto unknown = analyze(
        "rule Count:\n"
        "    each:\n"
        "        Log as log\n"
        "    on tick:\n"
        "        for child in query.children[EnemyMode.Gone](of = self):\n"
        "            log.n += 1\n");
    CHECK(unknown.has("EnemyMode.Gone"));
}

TEST_CASE("Semantic: a bare variant name is not a trait", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Chase:\n"
        "    each:\n"
        "        Chasing\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(analysis.has("unknown trait 'Chasing'"));
}

TEST_CASE("Semantic: an unknown variant of a state is not a trait", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Chase:\n"
        "    each:\n"
        "        EnemyMode.Fleeing\n"
        "    on tick:\n"
        "        let x = 1\n");
    CHECK(analysis.has("state 'EnemyMode' has no variant 'Fleeing'"));
}

// ── The slot ────────────────────────────────────────────────────────────────

TEST_CASE("Semantic: filter and exclude on a state use its slot", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Inspect:\n"
        "    each:\n"
        "        EnemyMode as m\n"
        "        Log as log\n"
        "    without:\n"
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
        "    each:\n"
        "        EnemyMode as m\n"
        "    on tick:\n"
        "        let x = m\n");
    CHECK(as_value.has("a state slot can only be a `match` subject"));

    const auto as_field = analyze(
        "rule Inspect:\n"
        "    each:\n"
        "        EnemyMode as m\n"
        "        Log as log\n"
        "    on tick:\n"
        "        log.n = m.index\n");
    CHECK(as_field.has("a state slot can only be a `match` subject"));
}

TEST_CASE("Semantic: remove names the state, not a variant", "[semantic][exclusive-states]") {
    const auto variant = analyze(
        "rule Calm:\n"
        "    each:\n"
        "        Body\n"
        "    on tick:\n"
        "        remove EnemyMode.Chasing\n");
    CHECK(variant.has("a state variant cannot be removed; use `add` of another variant or `remove EnemyMode`"));

    const auto slot = analyze(
        "rule Calm:\n"
        "    each:\n"
        "        Body\n"
        "    on tick:\n"
        "        remove EnemyMode\n");
    INFO(describe(slot));
    CHECK(slot.clean());
}

TEST_CASE("Semantic: add needs a variant, not the bare state", "[semantic][exclusive-states]") {
    const auto analysis = analyze(
        "rule Calm:\n"
        "    each:\n"
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
        "    each:\n"
        "        Body\n"
        "    on tick:\n"
        "        add EnemyMode.Chasing:\n"
        "            target = Player\n"
        "rule Calm:\n"
        "    each:\n"
        "        Log\n"
        "    after:\n"
        "        Hunt\n"
        "    on tick:\n"
        "        remove EnemyMode\n"
        "rule Wind:\n"
        "    each:\n"
        "        EnemyMode.Attacking as a\n"
        "    after:\n"
        "        Hunt\n"
        "        Calm\n"
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
           "    each:\n"
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
        "    each:\n"
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
        "    each:\n"
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
        "    each:\n"
        "        Body as b\n"
        "    on tick:\n"
        "        match b.x:\n"
        "            _ =>\n"
        "                b.x = 0.0\n");
    CHECK(analysis.has(
        "statement-level `match` subject must be `entity_id`, an enum, `int`, `bool` or a state slot, got `float`"));
}

// ── Transitions: declarations ───────────────────────────────────────────────

TEST_CASE("Semantic: a state with arrows is accepted", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "state Guard:\n"
        "    Idle -> Chasing, Dying\n"
        "    Chasing -> Attacking, Idle, Dying:\n"
        "        let target: entity_id\n"
        "    Attacking -> Chasing, Dying:\n"
        "        var windup: float = 0.3\n"
        "    Dying final:\n"
        "        var elapsed: float = 0.0\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& guard = analysis.program.states.at("Guard");
    CHECK(guard.has_arrows());
    REQUIRE(guard.variants[1].exits.size() == 3);
    CHECK(guard.variants[1].exits[0].variant == "Attacking");
    CHECK(guard.variants[1].location.line > guard.variants[0].location.line);
    CHECK_FALSE(analysis.program.states.at("EnemyMode").has_arrows());
}

TEST_CASE("Semantic: a non-final variant with no exit is rejected", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "state Guard:\n"
        "    Idle -> Busy\n"
        "    Busy\n"
        "    Gone final\n");
    CHECK(analysis.has("variant 'Busy' of state 'Guard' has no arrow; declare its exits with `->` or mark it `final`"));
    CHECK_FALSE(analysis.has("variant 'Gone'"));
}

TEST_CASE("Semantic: a final variant with an arrow is rejected", "[semantic][exclusive-states][state-transitions]") {
    for (const auto* line : {"    Gone final -> Idle\n", "    Gone -> Idle final\n"}) {
        const auto analysis = analyze(std::string("state Guard:\n    Idle -> Gone\n") + line);
        CHECK(analysis.has("final variant 'Gone' of state 'Guard' can't declare an arrow"));
    }
}

TEST_CASE("Semantic: a self arrow is rejected", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "state Guard:\n"
        "    Idle -> Idle\n");
    CHECK(analysis.has("variant 'Idle' of state 'Guard' can't declare an arrow to itself"));
}

TEST_CASE("Semantic: an arrow to an unknown variant is rejected", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "state Guard:\n"
        "    Idle -> Stunned\n"
        "    Gone final\n");
    CHECK(analysis.has("arrow to unknown variant 'Stunned' of state 'Guard'"));
}

TEST_CASE("Semantic: a duplicate arrow is rejected", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "state Guard:\n"
        "    Idle -> Chasing, Chasing\n"
        "    Chasing -> Idle\n");
    CHECK(analysis.has("duplicate arrow to 'Chasing' from variant 'Idle' of state 'Guard'"));
}

TEST_CASE("Semantic: an arrow error points at its variant", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "state Guard:\n"
        "    Idle -> Idle\n");
    const auto found = std::ranges::find_if(analysis.diagnostics, [](const Diagnostic& d) {
        return d.message.contains("an arrow to itself");
    });
    REQUIRE(found != analysis.diagnostics.end());
    CHECK(found->location.line == 1 + static_cast<int>(std::ranges::count(kPrelude, '\n')) + 2);
}

// ── Transitions: possible sources ───────────────────────────────────────────

namespace {

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

using Names = std::vector<std::string>;

}  // namespace

TEST_CASE("Semantic: a filter narrows the source", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Spot:\n"
        "    each:\n"
        "        EnemyMode.Idle\n"
        "    on tick:\n"
        "        add EnemyMode.Chasing:\n"
        "            target = Player\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Spot", "EnemyMode.Chasing") == Names{"Idle"});
}

TEST_CASE("Semantic: an unnarrowed add can leave every other non-final variant",
          "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Spot:\n"
        "    each:\n"
        "        EnemyMode as m\n"
        "    on tick:\n"
        "        add EnemyMode.Chasing:\n"
        "            target = Player\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Spot", "EnemyMode.Chasing") == Names{"Idle", "Attacking"});
    const auto& command = analysis.contract("Spot").commands.front();
    CHECK(command.sources.front().location.line > 0);
}

TEST_CASE("Semantic: without narrows the source", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Spot:\n"
        "    each:\n"
        "        EnemyMode as m\n"
        "    without:\n"
        "        EnemyMode.Attacking\n"
        "    on tick:\n"
        "        add EnemyMode.Chasing:\n"
        "            target = Player\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Spot", "EnemyMode.Chasing") == Names{"Idle"});
}

TEST_CASE("Semantic: a lifecycle trigger narrows the source", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Spot:\n"
        "    on added EnemyMode.Idle:\n"
        "        add EnemyMode.Attacking\n"
        "rule Resume:\n"
        "    each:\n"
        "        EnemyMode as m\n"
        "    on removed EnemyMode.Chasing:\n"
        "        add EnemyMode.Idle\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Spot", "EnemyMode.Attacking") == Names{"Idle"});
    CHECK(sources_of(analysis, "Resume", "EnemyMode.Idle") == Names{"Attacking"});
}

TEST_CASE("Semantic: a match arm narrows the source", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Step:\n"
        "    each:\n"
        "        EnemyMode as m\n"
        "    on tick:\n"
        "        match m:\n"
        "            EnemyMode.Chasing as c =>\n"
        "                add EnemyMode.Attacking\n"
        "            EnemyMode.Idle =>\n"
        "                add EnemyMode.Chasing:\n"
        "                    target = Player\n"
        "            _ =>\n"
        "                add EnemyMode.Dying\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Step", "EnemyMode.Attacking") == Names{"Chasing"});
    CHECK(sources_of(analysis, "Step", "EnemyMode.Chasing") == Names{"Idle"});
    CHECK(sources_of(analysis, "Step", "EnemyMode.Dying") == Names{"Attacking"});
}

TEST_CASE("Semantic: sources of one add in two arms are merged", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Step:\n"
        "    each:\n"
        "        EnemyMode as m\n"
        "    on tick:\n"
        "        match m:\n"
        "            EnemyMode.Attacking =>\n"
        "                add EnemyMode.Dying\n"
        "            EnemyMode.Idle =>\n"
        "                add EnemyMode.Dying\n"
        "            _ =>\n"
        "                return\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Step", "EnemyMode.Dying") == Names{"Idle", "Attacking"});
    const auto& sources = analysis.contract("Step").commands.front().sources;
    CHECK(sources[0].location.line > sources[1].location.line);
}

TEST_CASE("Semantic: a pair binding narrows the source", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Retreat:\n"
        "    pairs:\n"
        "        actor:\n"
        "            Body\n"
        "            EnemyMode.Attacking\n"
        "        other: Log\n"
        "    on tick:\n"
        "        add EnemyMode.Idle to actor\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Retreat", "EnemyMode.Idle") == Names{"Attacking"});
}

TEST_CASE("Semantic: another entity is not narrowed", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Alert:\n"
        "    each:\n"
        "        EnemyMode.Idle\n"
        "    on tick:\n"
        "        add EnemyMode.Chasing to Player:\n"
        "            target = Player\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Alert", "EnemyMode.Chasing") == Names{"Idle", "Attacking"});
}

TEST_CASE("Semantic: entering a state has no source", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "rule Wake:\n"
        "    each:\n"
        "        Body\n"
        "    without:\n"
        "        EnemyMode\n"
        "    on tick:\n"
        "        add EnemyMode.Attacking\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(sources_of(analysis, "Wake", "EnemyMode.Attacking").empty());
}

TEST_CASE("Semantic: an extern rule add is not narrowed", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze(
        "extern rule HostStep:\n"
        "    each:\n"
        "        EnemyMode.Idle\n"
        "    on tick:\n"
        "        commands:\n"
        "            add EnemyMode.Attacking\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& handlers = analysis.program.execution_graph.handlers;
    const auto host      = std::ranges::find_if(handlers, [](const HandlerNode& handler) {
        return handler.identity.rule.local_name == "HostStep";
    });
    REQUIRE(host != handlers.end());
    REQUIRE(host->contract.commands.size() == 1);
    Names names;
    for (const auto& source : host->contract.commands.front().sources) {
        names.push_back(source.variant);
    }
    CHECK(names == Names{"Idle", "Chasing"});
}

// ── Transitions: the whole-program check ────────────────────────────────────

namespace {

const std::string kTransitionPrelude =
    "module game\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "trait Log:\n"
    "    var n: int = 0\n"
    "state Mode:\n"
    "    Idle -> Chasing, Dying\n"
    "    Chasing -> Attacking, Idle, Dying\n"
    "    Attacking -> Chasing, Dying\n"
    "    Dying final\n"
    "entity Grunt:\n"
    "    Mode\n"
    "    Log\n"
    "entity Boss:\n"
    "    Log\n";

Analysis analyze_program(const std::string& source) {
    Analysis analysis = analyze_source(source);
    ErrorReporter errors;
    if (analysis.clean()) {
        run_whole_program_passes(analysis.program, errors);
    }
    std::ranges::copy(errors.diagnostics(), std::back_inserter(analysis.diagnostics));
    return analysis;
}

std::vector<std::string> transition_errors(const Analysis& analysis) {
    std::vector<std::string> messages;
    for (const auto& d : analysis.diagnostics) {
        if (d.level == DiagnosticLevel::Error && d.message.contains("no arrow to")) {
            messages.push_back(d.message);
        }
    }
    return messages;
}

}  // namespace

TEST_CASE("Semantic: a state without arrows is unrestricted", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(
        "module game\n"
        "extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "state Door:\n"
        "    Shut\n"
        "    Open\n"
        "entity Gate:\n"
        "    Door\n"
        "rule Flip:\n"
        "    each:\n"
        "        Door as d\n"
        "    on tick:\n"
        "        add Door.Open\n"
        "rule Flop:\n"
        "    each:\n"
        "        Door as d\n"
        "    after:\n"
        "        Flip\n"
        "    on tick:\n"
        "        add Door.Shut\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Semantic: a declared transition compiles", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kTransitionPrelude +
                                          "rule Spot:\n"
                                          "    each:\n"
                                          "        Mode.Idle\n"
                                          "    on tick:\n"
                                          "        add Mode.Chasing\n");
    INFO(describe(analysis));
    CHECK(transition_errors(analysis).empty());
}

TEST_CASE("Semantic: an undeclared transition is rejected", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kTransitionPrelude +
                                          "rule Strike:\n"
                                          "    each:\n"
                                          "        Mode.Idle\n"
                                          "    on tick:\n"
                                          "        add Mode.Attacking\n");
    INFO(describe(analysis));
    REQUIRE(transition_errors(analysis).size() == 1);
    CHECK(transition_errors(analysis)[0] ==
          "`add Mode.Attacking` can move an entity out of 'Idle', which has no arrow to 'Attacking'; narrow the "
          "source with `each:` or a state `match` arm, or declare the arrow");
    const auto found = std::ranges::find_if(analysis.diagnostics, [](const Diagnostic& d) {
        return d.message.contains("no arrow to");
    });
    CHECK(found->location.line == 1 + static_cast<int>(std::ranges::count(kTransitionPrelude, '\n')) + 4);
}

TEST_CASE("Semantic: an unknown source needs every arrow", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kTransitionPrelude +
                                          "rule Doom:\n"
                                          "    each:\n"
                                          "        Log\n"
                                          "    on tick:\n"
                                          "        add Mode.Dying to Grunt\n");
    INFO(describe(analysis));
    CHECK(transition_errors(analysis).empty());
}

TEST_CASE("Semantic: an unknown source with one missing arrow is rejected",
          "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kTransitionPrelude +
                                          "rule Provoke:\n"
                                          "    each:\n"
                                          "        Log\n"
                                          "    on tick:\n"
                                          "        add Mode.Attacking to Grunt\n");
    INFO(describe(analysis));
    REQUIRE(transition_errors(analysis).size() == 1);
    CHECK(transition_errors(analysis)[0].starts_with("`add Mode.Attacking` can move an entity out of 'Idle', which"));
    CHECK(transition_errors(analysis)[0].contains("narrow the source"));
}

TEST_CASE("Semantic: a transition error lists every source without an arrow",
          "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kTransitionPrelude +
                                          "rule Reset:\n"
                                          "    each:\n"
                                          "        Mode as m\n"
                                          "    on tick:\n"
                                          "        add Mode.Idle\n");
    INFO(describe(analysis));
    REQUIRE(transition_errors(analysis).size() == 1);
    CHECK(transition_errors(analysis)[0].starts_with(
        "`add Mode.Idle` can move an entity out of 'Attacking', which has no arrow to 'Idle'"));

    const auto two = analyze_program(kTransitionPrelude +
                                     "rule Strike:\n"
                                     "    each:\n"
                                     "        Mode as m\n"
                                     "    without:\n"
                                     "        Mode.Chasing\n"
                                     "    on tick:\n"
                                     "        add Mode.Attacking\n");
    REQUIRE(transition_errors(two).size() == 1);
    CHECK(transition_errors(two)[0].starts_with(
        "`add Mode.Attacking` can move an entity out of 'Idle', which has no arrow to 'Attacking'"));
}

TEST_CASE("Semantic: entering a state with arrows is allowed", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kTransitionPrelude +
                                          "rule Recruit:\n"
                                          "    each:\n"
                                          "        Log\n"
                                          "    without:\n"
                                          "        Mode\n"
                                          "    on tick:\n"
                                          "        add Mode.Attacking\n");
    INFO(describe(analysis));
    CHECK(transition_errors(analysis).empty());
}

TEST_CASE("Semantic: an extern rule add is checked with no narrowing", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kTransitionPrelude +
                                          "extern rule HostStrike:\n"
                                          "    each:\n"
                                          "        Mode.Idle\n"
                                          "    on tick:\n"
                                          "        commands:\n"
                                          "            add Mode.Attacking\n");
    INFO(describe(analysis));
    REQUIRE(transition_errors(analysis).size() == 1);
    CHECK(transition_errors(analysis)[0].contains("out of 'Idle', which has no arrow to 'Attacking'"));
}

TEST_CASE("Semantic: an unreachable add is not checked", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kTransitionPrelude +
                                          "trait Ghost\n"
                                          "rule Haunt:\n"
                                          "    each:\n"
                                          "        Mode.Idle\n"
                                          "        Ghost\n"
                                          "    on tick:\n"
                                          "        add Mode.Attacking\n");
    INFO(describe(analysis));
    CHECK(analysis.has("rule 'game.Haunt' never runs"));
    CHECK(transition_errors(analysis).empty());
}

// ── Transitions: non-vacuity ────────────────────────────────────────────────

namespace {

const std::string kDoorPrelude =
    "module game\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "state Door:\n"
    "    Shut -> Open\n"
    "    Open -> Shut, Broken\n"
    "    Broken final\n"
    "entity Gate:\n"
    "    Door\n"
    "rule OpenIt:\n"
    "    each:\n"
    "        Door.Shut\n"
    "    on tick:\n"
    "        add Door.Open\n"
    "rule ShutIt:\n"
    "    each:\n"
    "        Door.Open\n"
    "    after:\n"
    "        OpenIt\n"
    "    on tick:\n"
    "        add Door.Shut\n";

}  // namespace

TEST_CASE("Semantic: every arrow and variant in use compiles", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kDoorPrelude +
                                          "rule BreakIt:\n"
                                          "    each:\n"
                                          "        Door.Open\n"
                                          "    after:\n"
                                          "        ShutIt\n"
                                          "    on tick:\n"
                                          "        add Door.Broken\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Semantic: an unused arrow and a variant nothing enters are rejected",
          "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(kDoorPrelude);
    INFO(describe(analysis));
    CHECK(analysis.has(
        "arrow 'Open -> Broken' of state 'Door' is never taken: no reachable `add Door.Broken` can leave 'Open'"));
    CHECK(analysis.has("variant 'Broken' of state 'Door' is never entered: no entity starts in it and no reachable "
                       "`add` enters it"));
    CHECK_FALSE(analysis.has("'Shut -> Open'"));
    CHECK_FALSE(analysis.has("variant 'Shut'"));
    const auto arrow = std::ranges::find_if(analysis.diagnostics, [](const Diagnostic& d) {
        return d.message.contains("'Open -> Broken'");
    });
    CHECK(arrow->location.line == 9);
    CHECK(arrow->location.column > 10);
    const auto variant = std::ranges::find_if(analysis.diagnostics, [](const Diagnostic& d) {
        return d.message.contains("variant 'Broken'");
    });
    CHECK(variant->location.line == 10);
}

TEST_CASE("Semantic: a variant nothing can enter is rejected in a state without arrows",
          "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(
        "module game\n"
        "extern event frame:\n"
        "    dt: float\n"
        "phase tick:\n"
        "    from:\n"
        "        frame\n"
        "state Mood:\n"
        "    Calm\n"
        "    Angry\n"
        "    Stunned\n"
        "entity Hero:\n"
        "    Mood\n"
        "rule Rage:\n"
        "    each:\n"
        "        Mood.Calm\n"
        "    on tick:\n"
        "        add Mood.Angry\n");
    INFO(describe(analysis));
    CHECK(analysis.has("variant 'Stunned' of state 'Mood' is never entered"));
    CHECK_FALSE(analysis.has("variant 'Calm'"));
    CHECK_FALSE(analysis.has("variant 'Angry'"));
}

TEST_CASE("Semantic: an unreached stdlib state is not an error", "[semantic][exclusive-states][state-transitions]") {
    const auto analysis = analyze_program(
        "module std.doors\n"
        "pub state Door:\n"
        "    Shut -> Open\n"
        "    Open -> Shut, Broken\n"
        "    Broken final\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
