// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/delivery_cycles.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <tuple>

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
    "event Ping\n"
    "event Pong\n"
    "event Ignite\n"
    "trait Door:\n"
    "    var knocks: int = 0\n"
    "trait Body:\n"
    "    var x: float = 0.0\n"
    "group contacts:\n"
    "    phase: fixed_tick\n"
    "trait Zone:\n"
    "    var x: float = 0.0\n"
    "    var radius: float = 1.0\n"
    "trait InZone:\n"
    "    var zones: int = 0\n"
    "trait Offspring\n"
    "trait Burning\n"
    "trait Dying\n"
    "trait Fading\n"
    "state Lock:\n"
    "    Closed\n"
    "    Open\n"
    "entity Front:\n"
    "    Door\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] const ExecutionGraph& graph() const {
        return program.execution_graph;
    }

    [[nodiscard]] const EventFlowEdge* event_flow(const std::string& producer, const std::string& consumer) const {
        const auto found = std::ranges::find_if(graph().event_flows, [&](const EventFlowEdge& edge) {
            return edge.producer.rule.local_name == producer && edge.consumer.rule.local_name == consumer;
        });
        return found == graph().event_flows.end() ? nullptr : &*found;
    }

    [[nodiscard]] const LifecycleFlowEdge* lifecycle_flow(const std::string& producer,
                                                          const std::string& trait,
                                                          bool added,
                                                          const std::string& consumer) const {
        const auto found = std::ranges::find_if(graph().lifecycle_flows, [&](const LifecycleFlowEdge& edge) {
            return edge.producer.rule.local_name == producer && edge.trait.local_name == trait &&
                   edge.added == added && edge.consumer.rule.local_name == consumer;
        });
        return found == graph().lifecycle_flows.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool any_delayed() const {
        return std::ranges::any_of(graph().event_flows, &EventFlowEdge::delayed) ||
               std::ranges::any_of(graph().lifecycle_flows, &LifecycleFlowEdge::delayed);
    }

    [[nodiscard]] std::optional<std::uint32_t> depth(ActivationRootKind kind, const std::string& symbol = "") const {
        const auto found = std::ranges::find_if(graph().activation_depths, [&](const ActivationDepth& entry) {
            return entry.kind == kind && (symbol.empty() || (entry.symbol.has_value() && entry.symbol->local_name == symbol));
        });
        return found == graph().activation_depths.end() ? std::nullopt : std::optional(found->depth);
    }

    [[nodiscard]] std::string describe() const {
        std::string text;
        for (const auto& d : diagnostics) {
            text += std::to_string(d.location.line) + ": " + d.message + "\n";
        }
        return text;
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
    analysis.program     = analyzer.analyze(*analysis.ast);
    analysis.diagnostics = errors.diagnostics();
    INFO(analysis.describe());
    REQUIRE_FALSE(errors.has_errors());
    return analysis;
}

const std::string kAnswer =
    "rule Answer:\n"
    "    each:\n"
    "        Door as door\n"
    "    on Knock:\n"
    "        door.knocks = door.knocks + 1\n"
    "        emit Knock to self\n";

const std::string kLogger =
    "rule Logger:\n"
    "    on Knock:\n"
    "        return\n";

const std::string kStarter =
    "rule Starter:\n"
    "    on tick:\n"
    "        emit Knock to Front\n";

using EdgeSummary = std::set<std::tuple<std::string, std::string, bool>>;

EdgeSummary summarize(const Analysis& analysis) {
    EdgeSummary summary;
    for (const auto& edge : analysis.graph().event_flows) {
        summary.emplace(edge.producer.rule.local_name, edge.consumer.rule.local_name, edge.delayed);
    }
    for (const auto& edge : analysis.graph().lifecycle_flows) {
        summary.emplace(edge.producer.rule.local_name, edge.consumer.rule.local_name, edge.delayed);
    }
    return summary;
}

bool has_command(const HandlerContract& contract, HandlerCommandKind kind, const std::string& trait) {
    return std::ranges::any_of(contract.commands, [&](const InferredHandlerCommand& command) {
        return command.kind == kind && command.target.has_value() && command.target->local_name == trait;
    });
}

const HandlerNode& handler_of(const Analysis& analysis, const std::string& rule) {
    const auto found = std::ranges::find_if(analysis.graph().handlers,
                                            [&](const HandlerNode& node) { return node.identity.rule.local_name == rule; });
    REQUIRE(found != analysis.graph().handlers.end());
    return *found;
}

}  // namespace

// ── Event-flow classification ────────────────────────────────────────────────

TEST_CASE("Delivery cycles: a self-emitting handler has a delayed self edge", "[delivery-cycles]") {
    const auto analysis = analyze(kStarter + kAnswer);
    const auto* self    = analysis.event_flow("Answer", "Answer");
    REQUIRE(self != nullptr);
    CHECK(self->delayed);
    const auto* start = analysis.event_flow("Starter", "Answer");
    REQUIRE(start != nullptr);
    CHECK_FALSE(start->delayed);
}

TEST_CASE("Delivery cycles: a two-handler event cycle delays both edges", "[delivery-cycles]") {
    const auto analysis = analyze(
        "rule PingSide:\n"
        "    on Ping:\n"
        "        emit Pong\n"
        "rule PongSide:\n"
        "    on Pong:\n"
        "        emit Ping\n"
        "rule Serve:\n"
        "    on tick:\n"
        "        emit Ping\n");
    REQUIRE(analysis.event_flow("PingSide", "PongSide") != nullptr);
    REQUIRE(analysis.event_flow("PongSide", "PingSide") != nullptr);
    CHECK(analysis.event_flow("PingSide", "PongSide")->delayed);
    CHECK(analysis.event_flow("PongSide", "PingSide")->delayed);
    CHECK_FALSE(analysis.event_flow("Serve", "PingSide")->delayed);
}

TEST_CASE("Delivery cycles: an edge leaving a cycle stays immediate", "[delivery-cycles]") {
    const auto analysis = analyze(kStarter + kAnswer + kLogger);
    CHECK(analysis.event_flow("Answer", "Answer")->delayed);
    REQUIRE(analysis.event_flow("Answer", "Logger") != nullptr);
    CHECK_FALSE(analysis.event_flow("Answer", "Logger")->delayed);
}

TEST_CASE("Delivery cycles: an acyclic program has no delayed edge", "[delivery-cycles]") {
    const auto analysis = analyze(kStarter + kLogger +
                                  "rule Relay:\n"
                                  "    on Knock:\n"
                                  "        emit Ping\n"
                                  "rule Sink:\n"
                                  "    on Ping:\n"
                                  "        return\n");
    CHECK_FALSE(analysis.graph().event_flows.empty());
    CHECK_FALSE(analysis.any_delayed());
}

TEST_CASE("Delivery cycles: classification does not depend on declaration order", "[delivery-cycles]") {
    const std::string ping =
        "rule PingSide:\n"
        "    on Ping:\n"
        "        emit Pong\n";
    const std::string pong =
        "rule PongSide:\n"
        "    on Pong:\n"
        "        emit Ping\n"
        "        emit Knock to Front\n";
    const auto forward  = analyze(ping + pong + kAnswer + kLogger + kStarter);
    const auto backward = analyze(kStarter + kLogger + kAnswer + pong + ping);
    CHECK(summarize(forward) == summarize(backward));
    CHECK(forward.event_flow("PingSide", "PongSide")->delayed);
    CHECK_FALSE(forward.event_flow("PongSide", "Answer")->delayed);
}

// ── Lifecycle edges ──────────────────────────────────────────────────────────

TEST_CASE("Delivery cycles: add and remove reach added and removed handlers", "[delivery-cycles][lifecycle]") {
    const auto analysis = analyze(
        "rule Mark:\n"
        "    each:\n"
        "        Door\n"
        "    on tick:\n"
        "        add Dying\n"
        "        remove Burning\n"
        "rule OnDying:\n"
        "    on added Dying:\n"
        "        return\n"
        "rule OnBurnt:\n"
        "    on removed Burning:\n"
        "        return\n"
        "rule OnBurning:\n"
        "    on added Burning:\n"
        "        return\n");
    const auto* added = analysis.lifecycle_flow("Mark", "Dying", true, "OnDying");
    REQUIRE(added != nullptr);
    CHECK_FALSE(added->delayed);
    CHECK(analysis.lifecycle_flow("Mark", "Burning", false, "OnBurnt") != nullptr);
    CHECK(analysis.lifecycle_flow("Mark", "Burning", true, "OnBurning") == nullptr);
    CHECK_FALSE(analysis.any_delayed());
}

TEST_CASE("Delivery cycles: a spawn adds every trait of its flattened template", "[delivery-cycles][lifecycle]") {
    const auto analysis = analyze(
        "template Base:\n"
        "    Offspring\n"
        "template Minion:\n"
        "    use Base\n"
        "    Burning\n"
        "rule Seed:\n"
        "    on tick:\n"
        "        spawn Minion()\n"
        "rule OnOffspring:\n"
        "    on added Offspring:\n"
        "        return\n"
        "rule OnBurning:\n"
        "    on added Burning:\n"
        "        return\n"
        "rule OnDying:\n"
        "    on added Dying:\n"
        "        return\n");
    CHECK(analysis.lifecycle_flow("Seed", "Offspring", true, "OnOffspring") != nullptr);
    CHECK(analysis.lifecycle_flow("Seed", "Burning", true, "OnBurning") != nullptr);
    CHECK(analysis.lifecycle_flow("Seed", "Dying", true, "OnDying") == nullptr);
}

TEST_CASE("Delivery cycles: a lifecycle self-loop through spawn is delayed", "[delivery-cycles][lifecycle]") {
    const auto analysis = analyze(
        "template Minion:\n"
        "    Offspring\n"
        "rule Respawn:\n"
        "    on added Offspring:\n"
        "        spawn Minion()\n");
    const auto* self = analysis.lifecycle_flow("Respawn", "Offspring", true, "Respawn");
    REQUIRE(self != nullptr);
    CHECK(self->delayed);
}

TEST_CASE("Delivery cycles: keep both adds and removes its trait", "[delivery-cycles][lifecycle]") {
    const auto analysis = analyze(
        "rule Touch:\n"
        "    pairs:\n"
        "        body:\n"
        "            Body\n"
        "        zone:\n"
        "            Zone\n"
        "    group: contacts\n"
        "    where:\n"
        "        (body[Body].x - zone[Zone].x) * (body[Body].x - zone[Zone].x) <= zone[Zone].radius * zone[Zone].radius\n"
        "    reduce per body:\n"
        "        n = count()\n"
        "    keep InZone on body:\n"
        "        zones = n\n"
        "rule Enter:\n"
        "    on added InZone:\n"
        "        return\n"
        "rule Leave:\n"
        "    on removed InZone:\n"
        "        return\n");
    const auto& keeper = handler_of(analysis, "Touch");
    REQUIRE(has_command(keeper.contract, HandlerCommandKind::Add, "InZone"));
    REQUIRE(has_command(keeper.contract, HandlerCommandKind::Remove, "InZone"));
    CHECK(analysis.lifecycle_flow("Touch", "InZone", true, "Enter") != nullptr);
    CHECK(analysis.lifecycle_flow("Touch", "InZone", false, "Leave") != nullptr);
}

TEST_CASE("Delivery cycles: unary and unreduced pair keeps add and remove their traits", "[delivery-cycles][lifecycle]") {
    const auto analysis = analyze(
        "rule Low:\n"
        "    each:\n"
        "        Body as b\n"
        "    group: contacts\n"
        "    where:\n"
        "        b.x < 0.0\n"
        "    keep Burning\n"
        "rule Near:\n"
        "    pairs:\n"
        "        body:\n"
        "            Body\n"
        "        zone:\n"
        "            Zone\n"
        "    group: contacts\n"
        "    where:\n"
        "        body[Body].x < zone[Zone].x\n"
        "    keep InZone on body:\n"
        "        zones = 1\n"
        "rule Lit:\n"
        "    on added Burning:\n"
        "        return\n"
        "rule Out:\n"
        "    on removed InZone:\n"
        "        return\n");
    CHECK(analysis.lifecycle_flow("Low", "Burning", true, "Lit") != nullptr);
    CHECK(analysis.lifecycle_flow("Near", "InZone", false, "Out") != nullptr);
}

TEST_CASE("Delivery cycles: a state transition enters one variant and leaves the others",
          "[delivery-cycles][lifecycle]") {
    const auto analysis = analyze(
        "rule Unlock:\n"
        "    each:\n"
        "        Door\n"
        "    on tick:\n"
        "        add Lock.Open\n"
        "rule Drop:\n"
        "    each:\n"
        "        Body\n"
        "    on fixed_tick:\n"
        "        remove Lock\n"
        "rule Opened:\n"
        "    on added Lock.Open:\n"
        "        return\n"
        "rule Unclosed:\n"
        "    on removed Lock.Closed:\n"
        "        return\n"
        "rule Unopened:\n"
        "    on removed Lock.Open:\n"
        "        return\n");
    REQUIRE(has_command(handler_of(analysis, "Unlock").contract, HandlerCommandKind::Add, "Lock.Open"));
    REQUIRE(has_command(handler_of(analysis, "Drop").contract, HandlerCommandKind::Remove, "Lock"));
    CHECK(analysis.lifecycle_flow("Unlock", "Lock.Open", true, "Opened") != nullptr);
    CHECK(analysis.lifecycle_flow("Unlock", "Lock.Closed", false, "Unclosed") != nullptr);
    CHECK(analysis.lifecycle_flow("Unlock", "Lock.Open", false, "Unopened") == nullptr);
    CHECK(analysis.lifecycle_flow("Drop", "Lock.Closed", false, "Unclosed") != nullptr);
    CHECK(analysis.lifecycle_flow("Drop", "Lock.Open", false, "Unopened") != nullptr);
}

TEST_CASE("Delivery cycles: a mixed event and lifecycle cycle is delayed", "[delivery-cycles][lifecycle]") {
    const auto analysis = analyze(
        "rule Spread:\n"
        "    on added Burning:\n"
        "        emit Ignite\n"
        "rule Catch:\n"
        "    each:\n"
        "        Door\n"
        "    on Ignite:\n"
        "        add Burning\n"
        "rule Watch:\n"
        "    on Ignite:\n"
        "        return\n");
    REQUIRE(analysis.event_flow("Spread", "Catch") != nullptr);
    CHECK(analysis.event_flow("Spread", "Catch")->delayed);
    CHECK_FALSE(analysis.event_flow("Spread", "Watch")->delayed);
    const auto* back = analysis.lifecycle_flow("Catch", "Burning", true, "Spread");
    REQUIRE(back != nullptr);
    CHECK(back->delayed);
}

TEST_CASE("Delivery cycles: a spawn the compiler cannot resolve adds every watched trait",
          "[delivery-cycles][lifecycle]") {
    const auto tick    = make_symbol_id(SymbolKind::Phase, "game", "tick");
    const auto dying   = make_symbol_id(SymbolKind::Trait, "game", "Dying");
    const auto burning = make_symbol_id(SymbolKind::Trait, "game", "Burning");
    const auto known   = make_symbol_id(SymbolKind::Template, "game", "Known");

    const auto node = [](const std::string& rule, ResolvedHandlerTrigger trigger, std::uint64_t index) {
        return HandlerNode{.identity          = {.rule    = make_symbol_id(SymbolKind::Rule, "game", rule),
                                                 .trigger = std::move(trigger)},
                           .declaration_order = {.declaration_index = index}};
    };
    ExecutionGraph graph;
    auto runtime_spawn = node("Runtime", {.kind = HandlerTriggerKind::Phase, .symbol = tick}, 0);
    runtime_spawn.contract.commands.push_back({.kind = HandlerCommandKind::Spawn});
    auto known_spawn = node("Static", {.kind = HandlerTriggerKind::Phase, .symbol = tick}, 1);
    known_spawn.contract.commands.push_back({.kind = HandlerCommandKind::Spawn, .target = known});
    graph.handlers.push_back(runtime_spawn);
    graph.handlers.push_back(known_spawn);
    graph.handlers.push_back(node("OnDying", {.kind = HandlerTriggerKind::TraitAdded, .symbol = dying}, 2));
    graph.handlers.push_back(node("OnBurnt", {.kind = HandlerTriggerKind::TraitRemoved, .symbol = burning}, 3));

    TemplateTraitLookup templates;
    templates[known] = {burning};
    classify_delivery_cycles(graph, templates);

    const auto has_flow = [&](const std::string& producer, const std::string& consumer) {
        return std::ranges::any_of(graph.lifecycle_flows, [&](const LifecycleFlowEdge& edge) {
            return edge.producer.rule.local_name == producer && edge.consumer.rule.local_name == consumer;
        });
    };
    CHECK(has_flow("Runtime", "OnDying"));
    CHECK_FALSE(has_flow("Runtime", "OnBurnt"));
    CHECK_FALSE(has_flow("Static", "OnDying"));
}

// ── Static activation depth ──────────────────────────────────────────────────

TEST_CASE("Delivery cycles: an acyclic chain records its length", "[delivery-cycles][depth]") {
    const auto analysis = analyze(
        "rule Start:\n"
        "    on tick:\n"
        "        emit Ping\n"
        "rule Middle:\n"
        "    on Ping:\n"
        "        emit Pong\n"
        "rule End:\n"
        "    on Pong:\n"
        "        return\n");
    CHECK(analysis.depth(ActivationRootKind::Phase, "tick") == 3U);
}

TEST_CASE("Delivery cycles: a cycle adds no depth beyond one hop", "[delivery-cycles][depth]") {
    const auto analysis = analyze(
        "rule Starter:\n"
        "    on fixed_tick:\n"
        "        emit Knock to Front\n" +
        kAnswer);
    CHECK(analysis.depth(ActivationRootKind::Phase, "fixed_tick") == 2U);
    CHECK(analysis.depth(ActivationRootKind::Phase, "tick") == 0U);
}

TEST_CASE("Delivery cycles: host-clock roots and startup arrival record a depth", "[delivery-cycles][depth]") {
    const auto analysis = analyze(
        "event Boot\n"
        "rule OnBoot:\n"
        "    on Boot:\n"
        "        emit Ping\n"
        "rule OnPing:\n"
        "    on Ping:\n"
        "        return\n"
        "rule Arrive:\n"
        "    on added Offspring:\n"
        "        emit Ping\n");
    CHECK(analysis.depth(ActivationRootKind::Event, "Boot") == 2U);
    CHECK(analysis.depth(ActivationRootKind::StartupArrival) == 2U);
}

TEST_CASE("Delivery cycles: a delayed consumer counts as a starting handler", "[delivery-cycles][depth]") {
    // PongSide only arrives by delay, and starts the longest immediate chain.
    const auto analysis = analyze(
        "rule Serve:\n"
        "    on tick:\n"
        "        emit Ping\n"
        "rule PingSide:\n"
        "    on Ping:\n"
        "        emit Pong\n"
        "rule PongSide:\n"
        "    on Pong:\n"
        "        emit Ping\n"
        "        emit Knock to Front\n"
        "rule Relay:\n"
        "    on Knock:\n"
        "        emit Ignite\n"
        "rule Watch:\n"
        "    on Ignite:\n"
        "        return\n");
    CHECK(analysis.depth(ActivationRootKind::Phase, "tick") == 3U);
}

TEST_CASE("Delivery cycles: a lifecycle delivery and its event chain share one depth", "[delivery-cycles][depth]") {
    const auto analysis = analyze(
        "rule Mark:\n"
        "    each:\n"
        "        Door\n"
        "    on tick:\n"
        "        add Dying\n"
        "rule OnDying:\n"
        "    on added Dying:\n"
        "        emit Ping\n"
        "rule OnPing:\n"
        "    on Ping:\n"
        "        emit Pong\n"
        "rule OnPong:\n"
        "    on Pong:\n"
        "        return\n");
    CHECK(analysis.depth(ActivationRootKind::Phase, "tick") == 4U);
}

TEST_CASE("Delivery cycles: classification leaves the schedule unchanged", "[delivery-cycles]") {
    auto analysis = analyze(kStarter + kAnswer + kLogger +
                            "template Minion:\n"
                            "    Offspring\n"
                            "rule Respawn:\n"
                            "    on added Offspring:\n"
                            "        spawn Minion()\n");
    auto graph          = analysis.program.execution_graph;
    const auto order    = graph.stable_topological_order;
    const auto edges    = graph.schedule_edges.size();
    const auto barriers = graph.phase_barriers.size();
    std::vector<std::pair<std::uint64_t, std::vector<HandlerIdentity>>> levels;
    levels.reserve(graph.dependency_levels.size());
    for (const auto& level : graph.dependency_levels) {
        levels.emplace_back(level.index, level.handlers);
    }

    classify_delivery_cycles(graph, template_trait_lookup(analysis.program.persistence));

    CHECK(graph.stable_topological_order == order);
    CHECK(graph.schedule_edges.size() == edges);
    CHECK(graph.phase_barriers.size() == barriers);
    REQUIRE(graph.dependency_levels.size() == levels.size());
    for (std::size_t index = 0; index < levels.size(); ++index) {
        CHECK(graph.dependency_levels[index].index == levels[index].first);
        CHECK(graph.dependency_levels[index].handlers == levels[index].second);
    }
    CHECK(std::ranges::any_of(graph.lifecycle_flows, &LifecycleFlowEdge::delayed));
}

// ── Reporting ────────────────────────────────────────────────────────────────

namespace {

std::vector<Diagnostic> cycle_reports(const DecoratedProgram& program) {
    ErrorReporter errors;
    report_delayed_cycles(program, errors);
    CHECK(std::ranges::all_of(errors.diagnostics(),
                              [](const Diagnostic& d) { return d.level == DiagnosticLevel::Info; }));
    return errors.diagnostics();
}

const std::string kPingSide =
    "rule PingSide:\n"
    "    on Ping:\n"
    "        emit Pong\n";

const std::string kPongSide =
    "rule PongSide:\n"
    "    on Pong:\n"
    "        emit Ping\n";

const std::string kServe =
    "rule Serve:\n"
    "    on tick:\n"
    "        emit Ping\n";

HandlerIdentity cycle_identity(const std::string& module, const std::string& rule, const std::string& event) {
    return {.rule    = make_symbol_id(SymbolKind::Rule, module, rule),
            .trigger = {.kind = HandlerTriggerKind::Event, .symbol = make_symbol_id(SymbolKind::Event, module, event)}};
}

HandlerNode cycle_handler(const HandlerIdentity& identity, int line) {
    return HandlerNode{.identity = identity, .location = SourceLocation(identity.rule.module.name + ".cactus", line, 1)};
}

void delayed_ring(ExecutionGraph& graph, const std::vector<HandlerIdentity>& ring) {
    for (std::size_t index = 0; index < ring.size(); ++index) {
        const auto& next = ring[(index + 1) % ring.size()];
        graph.event_flows.push_back(EventFlowEdge{
            .producer = ring[index], .event = next.trigger.symbol, .consumer = next, .delayed = true});
    }
}

}  // namespace

TEST_CASE("Delayed cycle report: a two-handler cycle is reported once", "[delivery-cycles][report]") {
    const auto reports = cycle_reports(analyze(kServe + kPingSide + kPongSide).program);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].message ==
          "deliveries in this cycle are delayed to the next activation: "
          "game.PingSide/on game.Ping, game.PongSide/on game.Pong");
    CHECK(reports[0].location.filename == "game.cactus");
    CHECK(reports[0].location.line > 0);
}

TEST_CASE("Delayed cycle report: a self-loop is reported", "[delivery-cycles][report]") {
    const auto reports = cycle_reports(analyze(kStarter + kAnswer).program);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].message.ends_with(": game.Answer/on game.Knock"));
}

TEST_CASE("Delayed cycle report: a lifecycle cycle is reported", "[delivery-cycles][report]") {
    const auto reports = cycle_reports(analyze("rule Douse:\n"
                                               "    on added Burning:\n"
                                               "        remove Burning\n"
                                               "rule Relight:\n"
                                               "    on removed Burning:\n"
                                               "        add Burning\n")
                                           .program);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].message.contains("game.Douse/on added game.Burning"));
    CHECK(reports[0].message.contains("game.Relight/on removed game.Burning"));
}

TEST_CASE("Delayed cycle report: an acyclic program reports nothing", "[delivery-cycles][report]") {
    CHECK(cycle_reports(analyze(kStarter + kLogger).program).empty());
}

TEST_CASE("Delayed cycle report: separate cycles are reported separately", "[delivery-cycles][report]") {
    const auto reports = cycle_reports(analyze(kStarter + kAnswer + kServe + kPingSide + kPongSide).program);
    REQUIRE(reports.size() == 2);
    CHECK(reports[0].message.ends_with(": game.Answer/on game.Knock"));
    CHECK(reports[1].message.ends_with(": game.PingSide/on game.Ping, game.PongSide/on game.Pong"));
}

TEST_CASE("Delayed cycle report: declaration order does not change the report", "[delivery-cycles][report]") {
    const auto forward  = cycle_reports(analyze(kServe + kPingSide + kPongSide).program);
    const auto backward = cycle_reports(analyze(kPongSide + kPingSide + kServe).program);
    REQUIRE(forward.size() == 1);
    REQUIRE(backward.size() == 1);
    CHECK(forward[0].message == backward[0].message);
}

TEST_CASE("Delayed cycle report: a stdlib cycle is not reported", "[delivery-cycles][report]") {
    const auto first  = cycle_identity("std.net", "Send", "Sent");
    const auto second = cycle_identity("std.net", "Receive", "Received");
    DecoratedProgram program;
    program.execution_graph.handlers = {cycle_handler(first, 1), cycle_handler(second, 2)};
    delayed_ring(program.execution_graph, {first, second});
    CHECK(cycle_reports(program).empty());
}

TEST_CASE("Delayed cycle report: a mixed cycle is located at the first author handler", "[delivery-cycles][report]") {
    const auto stdlib = cycle_identity("std.net", "Send", "Sent");
    const auto later  = cycle_identity("zoo", "Listen", "Heard");
    const auto first  = cycle_identity("game", "Reply", "Replied");
    DecoratedProgram program;
    program.execution_graph.handlers = {cycle_handler(later, 7), cycle_handler(stdlib, 3), cycle_handler(first, 5)};
    delayed_ring(program.execution_graph, {stdlib, later, first});
    const auto reports = cycle_reports(program);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].location.filename == "game.cactus");
    CHECK(reports[0].location.line == 5);
    CHECK(reports[0].message.ends_with(": game.Reply/on game.Replied, std.net.Send/on std.net.Sent, zoo.Listen/on zoo.Heard"));
}

TEST_CASE("Delayed cycle report: handler and edge order do not change the report", "[delivery-cycles][report]") {
    const auto left  = cycle_identity("a", "Left", "L");
    const auto right = cycle_identity("b", "Right", "R");
    DecoratedProgram forward;
    forward.execution_graph.handlers = {cycle_handler(left, 1), cycle_handler(right, 2)};
    delayed_ring(forward.execution_graph, {left, right});
    DecoratedProgram backward;
    backward.execution_graph.handlers = {cycle_handler(right, 2), cycle_handler(left, 1)};
    delayed_ring(backward.execution_graph, {right, left});
    CHECK(cycle_reports(forward) == cycle_reports(backward));
}

// ── Multiplying cycles ───────────────────────────────────────────────────────

namespace {

const std::string kMultiplies = "this cycle can multiply its work each activation";

std::vector<Diagnostic> multiplying_warnings(DecoratedProgram& program) {
    ErrorReporter errors;
    derive_archetype_reachability(program, errors);
    ErrorReporter reported;
    report_delayed_cycles(program, reported);
    std::vector<Diagnostic> warnings;
    std::ranges::copy_if(reported.diagnostics(), std::back_inserter(warnings), [](const Diagnostic& d) {
        return d.level == DiagnosticLevel::Warning;
    });
    for (const auto& warning : warnings) {
        CHECK(warning.message.starts_with(kMultiplies));
    }
    return warnings;
}

std::vector<Diagnostic> multiplying_warnings(const std::string& body) {
    auto analysis = analyze(body);
    return multiplying_warnings(analysis.program);
}

const std::string kKnocker =
    "rule Knocker:\n"
    "    on tick:\n"
    "        emit Knock\n";

FeedNode emit_site(const SymbolId& event) {
    return FeedNode{.kind = FeedKind::Emit, .symbol = event};
}

}  // namespace

TEST_CASE("Multiplying cycles: two emits in one body", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(kKnocker +
                                               "rule Echo:\n"
                                               "    on Knock:\n"
                                               "        emit Knock\n"
                                               "        emit Knock\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Echo/on game.Knock"));
    CHECK(warnings[0].location.filename == "game.cactus");
    CHECK(warnings[0].location.line > 0);
}

TEST_CASE("Multiplying cycles: one emit in a simple cycle", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings(kServe + kPingSide + kPongSide).empty());
}

TEST_CASE("Multiplying cycles: exclusive branches do not add", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings(kKnocker +
                               "rule Echo:\n"
                               "    each:\n"
                               "        Door as door\n"
                               "    on Knock:\n"
                               "        if door.knocks > 3:\n"
                               "            emit Knock to self\n"
                               "        else:\n"
                               "            emit Knock to self\n")
              .empty());
}

TEST_CASE("Multiplying cycles: a guard does not lower the gain", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(kKnocker +
                                               "rule Echo:\n"
                                               "    each:\n"
                                               "        Door as door\n"
                                               "    on Knock:\n"
                                               "        if door.knocks > 3:\n"
                                               "            emit Knock to self\n"
                                               "            emit Knock to self\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Echo/on game.Knock"));
}

TEST_CASE("Multiplying cycles: a returning branch does not add to the rest", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings(kKnocker +
                               "rule Echo:\n"
                               "    each:\n"
                               "        Door as door\n"
                               "    on Knock:\n"
                               "        if door.knocks > 3:\n"
                               "            emit Knock to self\n"
                               "            return\n"
                               "        emit Knock to self\n")
              .empty());
}

TEST_CASE("Multiplying cycles: a returning branch adds to what ran before it", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(kKnocker +
                                               "rule Echo:\n"
                                               "    each:\n"
                                               "        Door as door\n"
                                               "    on Knock:\n"
                                               "        emit Knock to self\n"
                                               "        if door.knocks > 3:\n"
                                               "            emit Knock to self\n"
                                               "            return\n");
    REQUIRE(warnings.size() == 1);
}

TEST_CASE("Multiplying cycles: match arms do not add", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings(kKnocker +
                               "rule Echo:\n"
                               "    each:\n"
                               "        Door as door\n"
                               "    on Knock:\n"
                               "        match door.knocks:\n"
                               "            1 =>\n"
                               "                emit Knock to self\n"
                               "            _ =>\n"
                               "                emit Knock to self\n")
              .empty());
}

TEST_CASE("Multiplying cycles: an emit in a loop", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(kKnocker +
                                               "rule Echo:\n"
                                               "    on Knock:\n"
                                               "        for i in range(0, 3):\n"
                                               "            emit Knock\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Echo/on game.Knock"));
}

TEST_CASE("Multiplying cycles: one event reaches two handlers on the cycle", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(kKnocker +
                                               "rule Echo:\n"
                                               "    on Knock:\n"
                                               "        emit Knock\n"
                                               "rule Answer:\n"
                                               "    on Knock:\n"
                                               "        emit Knock\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Answer/on game.Knock"));
}

TEST_CASE("Multiplying cycles: a broadcast reaches many rows", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(
        "trait Unit\n"
        "template Soldier:\n"
        "    Unit\n"
        "rule Recruit:\n"
        "    on tick:\n"
        "        spawn Soldier()\n"
        "        emit Ping\n"
        "rule Rally:\n"
        "    each:\n"
        "        Unit\n"
        "    on Ping:\n"
        "        emit Ping\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Rally/on game.Ping"));
}

TEST_CASE("Multiplying cycles: a targeted emit reaches one row", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings("trait Unit\n"
                               "template Soldier:\n"
                               "    Unit\n"
                               "rule Recruit:\n"
                               "    on tick:\n"
                               "        spawn Soldier()\n"
                               "        emit Ping\n"
                               "rule Rally:\n"
                               "    each:\n"
                               "        Unit\n"
                               "    on Ping:\n"
                               "        emit Ping to self\n")
              .empty());
}

TEST_CASE("Multiplying cycles: a broadcast to one named entity reaches one row", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings(kKnocker +
                               "rule Echo:\n"
                               "    each:\n"
                               "        Door\n"
                               "    on Knock:\n"
                               "        emit Knock\n")
              .empty());
}

TEST_CASE("Multiplying cycles: a broadcast reaches many relation rows", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(
        "relation Link:\n"
        "    from: Door\n"
        "    to: Door\n"
        "rule Wire:\n"
        "    on tick:\n"
        "        add Link(Front, Front)\n"
        "        emit Ping\n"
        "rule Relay:\n"
        "    each: Link as link\n"
        "    on Ping:\n"
        "        emit Ping\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Relay/on game.Ping"));
}

TEST_CASE("Multiplying cycles: a broadcast reaches many pairs", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(
        "trait Unit\n"
        "template Soldier:\n"
        "    Unit\n"
        "rule Recruit:\n"
        "    on tick:\n"
        "        spawn Soldier()\n"
        "        emit Ping\n"
        "rule Muster:\n"
        "    pairs:\n"
        "        a:\n"
        "            Unit\n"
        "        b:\n"
        "            Unit\n"
        "    on Ping:\n"
        "        emit Ping\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Muster/on game.Ping"));
}

TEST_CASE("Multiplying cycles: a broadcast to one named pair reaches one row", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings(kKnocker +
                               "rule Muster:\n"
                               "    pairs:\n"
                               "        a:\n"
                               "            Door\n"
                               "        b:\n"
                               "            Door\n"
                               "    on Knock:\n"
                               "        emit Knock\n")
              .empty());
}

TEST_CASE("Multiplying cycles: two native spawns feed a lifecycle cycle twice", "[delivery-cycles][gain]") {
    const auto offspring = make_symbol_id(SymbolKind::Trait, "game", "Offspring");
    const HandlerIdentity breed{.rule    = make_symbol_id(SymbolKind::Rule, "game", "Breed"),
                                .trigger = {.kind = HandlerTriggerKind::TraitAdded, .symbol = offspring}};
    const FeedNode native{.kind = FeedKind::NativeSpawn, .symbol = make_symbol_id(SymbolKind::Func, "std.editor", "spawn_template")};
    auto handler                   = cycle_handler(breed, 4);
    handler.contract.feed.children = {native, native};
    DecoratedProgram program;
    program.execution_graph.handlers = {handler};
    program.execution_graph.lifecycle_flows.push_back(
        LifecycleFlowEdge{.producer = breed, .trait = offspring, .added = true, .consumer = breed, .delayed = true});
    const auto warnings = multiplying_warnings(program);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Breed/on added game.Offspring"));
}

TEST_CASE("Multiplying cycles: a spawn feeds a lifecycle cycle twice", "[delivery-cycles][gain]") {
    const auto warnings = multiplying_warnings(
        "template Child:\n"
        "    Offspring\n"
        "entity Adam:\n"
        "    Offspring\n"
        "rule Breed:\n"
        "    on added Offspring:\n"
        "        spawn Child()\n"
        "        spawn Child()\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.contains("game.Breed/on added game.Offspring"));
}

TEST_CASE("Multiplying cycles: an emit off the cycle does not count", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings(kKnocker +
                               "rule Echo:\n"
                               "    on Knock:\n"
                               "        emit Knock\n"
                               "        emit Ignite\n"
                               "rule Log:\n"
                               "    on Ignite:\n"
                               "        return\n")
              .empty());
}

TEST_CASE("Multiplying cycles: adding one trait twice in one run delivers once", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings("entity Torch:\n"
                               "    Burning\n"
                               "rule Douse:\n"
                               "    on added Burning:\n"
                               "        remove Burning\n"
                               "rule Relight:\n"
                               "    on removed Burning:\n"
                               "        add Burning\n"
                               "        add Burning\n")
              .empty());
}

TEST_CASE("Multiplying cycles: an extern rule on a cycle is not warned", "[delivery-cycles][gain]") {
    CHECK(multiplying_warnings(kKnocker +
                               "extern rule HostEcho:\n"
                               "    on Knock:\n"
                               "        emits: Knock, Ping\n"
                               "rule PingBack:\n"
                               "    on Ping:\n"
                               "        emit Knock\n")
              .empty());
}

TEST_CASE("Multiplying cycles: a stdlib handler counts inside a mixed cycle", "[delivery-cycles][gain]") {
    const auto stdlib = cycle_identity("std.net", "Send", "Sent");
    const auto author = cycle_identity("game", "Reply", "Replied");
    DecoratedProgram program;
    auto sender = cycle_handler(stdlib, 3);
    sender.contract.feed.children = {emit_site(author.trigger.symbol), emit_site(author.trigger.symbol)};
    auto replier = cycle_handler(author, 5);
    replier.contract.feed.children = {emit_site(stdlib.trigger.symbol)};
    program.execution_graph.handlers = {sender, replier};
    delayed_ring(program.execution_graph, {stdlib, author});
    const auto warnings = multiplying_warnings(program);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].location.filename == "game.cactus");
    CHECK(warnings[0].location.line == 5);
    CHECK(warnings[0].message.contains("std.net.Send/on std.net.Sent"));
}

TEST_CASE("Multiplying cycles: a stdlib-only cycle is not warned", "[delivery-cycles][gain]") {
    const auto first  = cycle_identity("std.net", "Send", "Sent");
    const auto second = cycle_identity("std.net", "Receive", "Received");
    DecoratedProgram program;
    auto sender = cycle_handler(first, 1);
    sender.contract.feed.children = {emit_site(second.trigger.symbol), emit_site(second.trigger.symbol)};
    program.execution_graph.handlers = {sender, cycle_handler(second, 2)};
    delayed_ring(program.execution_graph, {first, second});
    CHECK(multiplying_warnings(program).empty());
}

TEST_CASE("Multiplying cycles: the warning follows the cycle info", "[delivery-cycles][gain]") {
    auto analysis = analyze(kKnocker +
                            "rule Echo:\n"
                            "    on Knock:\n"
                            "        emit Knock\n"
                            "        emit Knock\n");
    ErrorReporter errors;
    report_delayed_cycles(analysis.program, errors);
    const auto& reported = errors.diagnostics();
    REQUIRE(reported.size() == 2);
    CHECK(reported[0].level == DiagnosticLevel::Info);
    CHECK(reported[1].level == DiagnosticLevel::Warning);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
