// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "cir/cir.hpp"
#include "cir/cir_graph_projection.hpp"
#include "cir/cir_graphviz.hpp"
#include "cir/cir_json.hpp"
#include "cir/cir_lowering.hpp"
#include "cir/cir_text.hpp"
#include "cir/cir_validation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace cactus;
using namespace cactus::cir;

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace {

/// Analyzes `source` and lowers the result, keeping the parsed AST alive only
/// for the duration of the call — CIR must survive without it.
CirProgram lower_source(std::string_view source, const ModuleImports& imports = {}) {
    ErrorReporter errors;
    Lexer lexer(std::string{source}, "cir_test.cactus", errors);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens), errors);
    auto ast      = parser.parse_program();
    auto analyzed = SemanticAnalyzer(errors).analyze(ast, imports);
    INFO((errors.has_errors() ? errors.diagnostics().front().message : std::string{}));
    REQUIRE_FALSE(errors.has_errors());
    return lower_program(analyzed);
}

CirNodeId handler_id(const std::string& module,
                     const std::string& rule,
                     SymbolKind trigger_kind,
                     const std::string& trigger) {
    return handler_node_id(HandlerIdentity{
        .rule    = make_symbol_id(SymbolKind::Rule, module, rule),
        .trigger = ResolvedHandlerTrigger{
            .kind   = trigger_kind == SymbolKind::Phase ? HandlerTriggerKind::Phase : HandlerTriggerKind::Event,
            .symbol = make_symbol_id(trigger_kind, module, trigger)}});
}

const CirHandlerNode* find_handler(const CirProgram& cir, const CirNodeId& id) {
    const auto found = std::ranges::find_if(cir.handlers, [&](const auto& node) { return node.id == id; });
    return found == cir.handlers.end() ? nullptr : &*found;
}

const CirScheduleRelation* find_relation(const CirProgram& cir,
                                         const CirNodeId& before,
                                         const CirNodeId& after,
                                         ScheduleEdgeKind kind) {
    const auto found = std::ranges::find_if(cir.schedule_dependencies, [&](const auto& relation) {
        return relation.before == before && relation.after == after && relation.kind == kind;
    });
    return found == cir.schedule_dependencies.end() ? nullptr : &*found;
}

bool has_event_flow(const CirProgram& cir, const CirNodeId& producer, const CirNodeId& consumer) {
    return std::ranges::any_of(
        cir.event_flows, [&](const auto& flow) { return flow.producer == producer && flow.consumer == consumer; });
}

const CirActivationSchedule* find_activation(const CirProgram& cir, const std::string& canonical_trigger) {
    const auto found = std::ranges::find_if(cir.activation_schedules, [&](const auto& schedule) {
        return make_canonical_id(schedule.activation.symbol) == canonical_trigger;
    });
    return found == cir.activation_schedules.end() ? nullptr : &*found;
}

bool validates(const CirProgram& cir) {
    ErrorReporter errors;
    return validate_program(cir, errors);
}

std::string first_validation_error(const CirProgram& cir) {
    ErrorReporter errors;
    if (validate_program(cir, errors)) {
        return {};
    }
    return errors.diagnostics().front().message;
}

/// A minimal but complete program: one multi-handler rule, a marker-selecting
/// unary rule, and a writer/reader conflict pair.
constexpr std::string_view kBaseProgram =
    "module game.cir\n"
    "event tick\n"
    "event Damaged\n"
    "trait Health\n"
    "trait Marker\n"
    "trait Shield\n"
    "extern rule Vitals:\n"
    "    filter:\n"
    "        Health\n"
    "    on tick:\n"
    "        writes:\n"
    "            Health\n"
    "    on Damaged:\n"
    "        reads:\n"
    "            Health\n"
    "extern rule Tagged:\n"
    "    filter:\n"
    "        Marker\n"
    "    exclude:\n"
    "        Shield\n"
    "    on tick:\n"
    "        reads:\n"
    "            Marker\n"
    "extern rule Regen:\n"
    "    filter:\n"
    "        Health\n"
    "    on tick:\n"
    "        reads:\n"
    "            Health\n";

}  // namespace

// ── Task 2.1: owned envelope, modules, traits, rule groups, handler nodes ────

TEST_CASE("CIR carries its schema, version, and linked module list", "[cir][envelope]") {
    const auto cir = lower_source(kBaseProgram);

    CHECK(cir.schema == "cactus-cir");
    CHECK(cir.version == 1);
    CHECK(cir.modules == std::vector<std::string>{"game.cir"});
}

TEST_CASE("CIR owns resolved trait metadata", "[cir][envelope]") {
    const auto cir = lower_source(
        "module game.cir\n"
        "event tick\n"
        "trait Health:\n"
        "    var current: float\n"
        "    let maximum: float\n"
        "extern rule Uses:\n"
        "    on tick:\n"
        "        reads:\n"
        "            Health\n");

    const auto found = std::ranges::find_if(
        cir.traits, [](const auto& trait) { return make_canonical_id(trait.symbol) == "game.cir.Health"; });
    REQUIRE(found != cir.traits.end());
    CHECK(found->symbol.kind == SymbolKind::Trait);
    REQUIRE(found->fields.size() == 2);
    CHECK(found->fields[0].name == "current");
    CHECK(found->fields[0].type == "float");
    CHECK(found->fields[0].is_var);
    CHECK(found->fields[1].name == "maximum");
    CHECK_FALSE(found->fields[1].is_var);
}

TEST_CASE("CIR keeps one node per handler of a multi-handler rule, grouped by its rule", "[cir][nodes]") {
    const auto cir = lower_source(kBaseProgram);

    const auto on_tick    = handler_id("game.cir", "Vitals", SymbolKind::Event, "tick");
    const auto on_damaged = handler_id("game.cir", "Vitals", SymbolKind::Event, "Damaged");
    CHECK(on_tick != on_damaged);

    const auto* tick_node = find_handler(cir, on_tick);
    REQUIRE(tick_node != nullptr);
    const auto* damaged_node = find_handler(cir, on_damaged);
    REQUIRE(damaged_node != nullptr);
    CHECK(tick_node->trigger.symbol.local_name == "tick");
    CHECK(damaged_node->trigger.symbol.local_name == "Damaged");
    CHECK(tick_node->writes.size() == 1);
    CHECK(damaged_node->writes.empty());
    CHECK(damaged_node->reads.size() == 1);

    const auto group = std::ranges::find_if(
        cir.rule_groups, [](const auto& entry) { return make_canonical_id(entry.rule) == "game.cir.Vitals"; });
    REQUIRE(group != cir.rule_groups.end());
    CHECK(group->handlers.size() == 2);
    CHECK(std::ranges::find(group->handlers, on_tick) != group->handlers.end());
    CHECK(std::ranges::find(group->handlers, on_damaged) != group->handlers.end());
}

TEST_CASE("CIR outlives the program and AST it was lowered from", "[cir][envelope]") {
    CirProgram cir;
    {
        ErrorReporter errors;
        Lexer lexer(std::string{kBaseProgram}, "cir_test.cactus", errors);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens), errors);
        auto ast      = std::make_unique<ProgramNode>(parser.parse_program());
        auto analyzed = SemanticAnalyzer(errors).analyze(*ast);
        REQUIRE_FALSE(errors.has_errors());
        cir = lower_program(analyzed);
    }

    CHECK(cir.modules == std::vector<std::string>{"game.cir"});
    CHECK_FALSE(cir.handlers.empty());
    CHECK(validates(cir));
}

// ── Task 2.3: domains and contracts ─────────────────────────────────────────

TEST_CASE("CIR records a unary selection without turning it into a read", "[cir][domains]") {
    const auto cir     = lower_source(kBaseProgram);
    const auto* tagged = find_handler(cir, handler_id("game.cir", "Tagged", SymbolKind::Event, "tick"));

    REQUIRE(tagged != nullptr);
    CHECK(tagged->domain == HandlerDomainKind::Unary);
    REQUIRE(tagged->selection.size() == 1);
    CHECK(make_canonical_id(tagged->selection.front()) == "game.cir.Marker");
    REQUIRE(tagged->exclusion.size() == 1);
    CHECK(make_canonical_id(tagged->exclusion.front()) == "game.cir.Shield");
    CHECK(std::ranges::none_of(tagged->writes,
                               [](const auto& trait) { return make_canonical_id(trait) == "game.cir.Shield"; }));
    CHECK(tagged->implementation == HandlerImplementationKind::External);
}

TEST_CASE("CIR preserves pair binding roles and binding-qualified reads", "[cir][domains]") {
    const auto cir = lower_source(
        "module game.pairs\n"
        "event tick\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "rule Contact:\n"
        "    pairs:\n"
        "        body:\n"
        "            Collider\n"
        "        wall:\n"
        "            Collider\n"
        "    on tick:\n"
        "        let total = body.Collider.radius + wall.Collider.radius\n");

    const auto* contact = find_handler(cir, handler_id("game.pairs", "Contact", SymbolKind::Event, "tick"));
    REQUIRE(contact != nullptr);
    CHECK(contact->domain == HandlerDomainKind::Pair);
    REQUIRE(contact->bindings.size() == 2);
    CHECK(contact->bindings[0].name == "body");
    CHECK(contact->bindings[1].name == "wall");

    // The conservative canonical union collapses both reads; the binding-
    // qualified list keeps them apart.
    REQUIRE(contact->reads.size() == 1);
    CHECK(make_canonical_id(contact->reads.front()) == "game.pairs.Collider");
    REQUIRE(contact->bound_reads.size() == 2);
    CHECK(contact->bound_reads[0].binding_index == 0);
    CHECK(contact->bound_reads[1].binding_index == 1);
}

TEST_CASE("CIR records a selectionless handler as such", "[cir][domains]") {
    const auto cir = lower_source(
        "module game.plain\n"
        "event tick\n"
        "rule Bare:\n"
        "    on tick:\n"
        "        let x = 1\n");

    const auto* bare = find_handler(cir, handler_id("game.plain", "Bare", SymbolKind::Event, "tick"));
    REQUIRE(bare != nullptr);
    CHECK(bare->domain == HandlerDomainKind::Selectionless);
    CHECK(bare->selection.empty());
    CHECK(bare->implementation == HandlerImplementationKind::Cactus);
}

// ── Tasks 2.4 / 2.6 / 2.7: relations and typed trait provenance ─────────────

namespace {

/// Writer/reader, write/write, effect, explicit-order, and projection cases in
/// one activation, so a single lowering exercises every relation shape.
constexpr std::string_view kConflictProgram =
    "module game.flow\n"
    "event input\n"
    "trait Position\n"
    "trait Log\n"
    "trait PointerHit\n"
    "extern rule Writer:\n"
    "    on input:\n"
    "        writes:\n"
    "            Position\n"
    "extern rule Reader:\n"
    "    on input:\n"
    "        reads:\n"
    "            Position\n"
    "extern rule LogA:\n"
    "    on input:\n"
    "        writes:\n"
    "            Log\n"
    "extern rule LogB:\n"
    "    on input:\n"
    "        writes:\n"
    "            Log\n"
    "extern rule EarlyReader:\n"
    "    on input:\n"
    "        reads:\n"
    "            Log\n"
    "extern rule LateWriter:\n"
    "    on input:\n"
    "        after:\n"
    "            EarlyReader/on input\n"
    "        writes:\n"
    "            Log\n"
    "extern rule Painter:\n"
    "    on input:\n"
    "        effects:\n"
    "            graphics\n"
    "extern rule Presenter:\n"
    "    on input:\n"
    "        effects:\n"
    "            graphics\n"
    "extern rule Projector:\n"
    "    filter:\n"
    "        PointerHit\n"
    "    on input:\n"
    "        projects:\n"
    "            PointerHit\n"
    "extern rule Selector:\n"
    "    filter:\n"
    "        PointerHit\n"
    "    on input:\n"
    "        reads:\n"
    "            PointerHit\n";

CirNodeId flow_handler(const std::string& rule) {
    return handler_id("game.flow", rule, SymbolKind::Event, "input");
}

}  // namespace

TEST_CASE("CIR names the trait and both endpoint access modes for a writer-to-reader dependency",
          "[cir][relations][provenance]") {
    const auto cir = lower_source(kConflictProgram);

    const auto* relation =
        find_relation(cir, flow_handler("Writer"), flow_handler("Reader"), ScheduleEdgeKind::DataConflict);
    REQUIRE(relation != nullptr);
    CHECK(relation->orientation == ScheduleEdgeOrientation::WriterBeforeReader);
    REQUIRE(relation->trait_provenance.size() == 1);
    const auto& provenance = relation->trait_provenance.front();
    CHECK(make_canonical_id(provenance.trait) == "game.flow.Position");
    // An extern rule's `writes:` is read-modify-write, so the producer reads too.
    CHECK(provenance.before == std::vector<CirTraitAccessMode>{CirTraitAccessMode::Read, CirTraitAccessMode::Write});
    CHECK(provenance.after == std::vector<CirTraitAccessMode>{CirTraitAccessMode::Read});
}

TEST_CASE("CIR reports write-to-write provenance as a hazard on both endpoints", "[cir][relations][provenance]") {
    const auto cir = lower_source(kConflictProgram);

    const auto* relation =
        find_relation(cir, flow_handler("LogA"), flow_handler("LogB"), ScheduleEdgeKind::DataConflict);
    REQUIRE(relation != nullptr);
    REQUIRE(relation->trait_provenance.size() == 1);
    const std::vector<CirTraitAccessMode> read_write{CirTraitAccessMode::Read, CirTraitAccessMode::Write};
    CHECK(relation->trait_provenance.front().before == read_write);
    CHECK(relation->trait_provenance.front().after == read_write);
    CHECK(relation->orientation == ScheduleEdgeOrientation::DeclarationOrder);
}

TEST_CASE("CIR keeps explicit order that opposes the natural writer-before-reader direction",
          "[cir][relations][provenance]") {
    const auto cir = lower_source(kConflictProgram);

    const auto* relation =
        find_relation(cir, flow_handler("EarlyReader"), flow_handler("LateWriter"), ScheduleEdgeKind::DataConflict);
    REQUIRE(relation != nullptr);
    CHECK(relation->orientation == ScheduleEdgeOrientation::Explicit);
    REQUIRE(relation->trait_provenance.size() == 1);
    CHECK(relation->trait_provenance.front().before == std::vector<CirTraitAccessMode>{CirTraitAccessMode::Read});
    CHECK(relation->trait_provenance.front().after ==
          std::vector<CirTraitAccessMode>{CirTraitAccessMode::Read, CirTraitAccessMode::Write});

    // The reverse direction must not also appear.
    CHECK(find_relation(cir, flow_handler("LateWriter"), flow_handler("EarlyReader"), ScheduleEdgeKind::DataConflict) ==
          nullptr);
}

TEST_CASE("CIR identifies projected production feeding a later selection", "[cir][relations][provenance]") {
    const auto cir = lower_source(kConflictProgram);

    const auto* relation =
        find_relation(cir, flow_handler("Projector"), flow_handler("Selector"), ScheduleEdgeKind::DataConflict);
    REQUIRE(relation != nullptr);
    REQUIRE(relation->trait_provenance.size() == 1);
    const auto& provenance = relation->trait_provenance.front();
    CHECK(make_canonical_id(provenance.trait) == "game.flow.PointerHit");
    CHECK(provenance.before ==
          std::vector<CirTraitAccessMode>{CirTraitAccessMode::Project, CirTraitAccessMode::Select});
    CHECK(provenance.after == std::vector<CirTraitAccessMode>{CirTraitAccessMode::Read, CirTraitAccessMode::Select});
}

TEST_CASE("CIR keeps effect conflicts distinguishable from trait dependencies", "[cir][relations][provenance]") {
    const auto cir = lower_source(kConflictProgram);

    const auto* relation =
        find_relation(cir, flow_handler("Painter"), flow_handler("Presenter"), ScheduleEdgeKind::EffectConflict);
    REQUIRE(relation != nullptr);
    CHECK(relation->trait_provenance.empty());
    CHECK(relation->effect_provenance == std::vector<std::string>{"graphics"});
    CHECK(find_relation(cir, flow_handler("Painter"), flow_handler("Presenter"), ScheduleEdgeKind::DataConflict) ==
          nullptr);
}

TEST_CASE("CIR adds no schedule dependency between independent readers", "[cir][relations]") {
    const auto cir = lower_source(
        "module game.readers\n"
        "event tick\n"
        "trait Position\n"
        "extern rule First:\n"
        "    on tick:\n"
        "        reads:\n"
        "            Position\n"
        "extern rule Second:\n"
        "    on tick:\n"
        "        reads:\n"
        "            Position\n");

    CHECK(cir.schedule_dependencies.empty());
}

namespace {

/// A phase lineage, a legal event cycle, and enough co-triggered handlers to
/// produce more than one dependency level.
constexpr std::string_view kActivationProgram =
    "module game.act\n"
    "extern event frame:\n"
    "    dt: float\n"
    "event A:\n"
    "    value: int\n"
    "event B:\n"
    "    value: int\n"
    "phase input:\n"
    "    from:\n"
    "        frame\n"
    "phase fixed_tick:\n"
    "    after:\n"
    "        input\n"
    "rule FixedFirst:\n"
    "    on fixed_tick:\n"
    "        let value = 1\n"
    "rule FixedSecond:\n"
    "    on fixed_tick:\n"
    "        let value = 2\n"
    "rule FeedbackA:\n"
    "    on A:\n"
    "        emit B:\n"
    "            value = A.value\n"
    "rule FeedbackB:\n"
    "    on B:\n"
    "        emit A:\n"
    "            value = B.value\n";

}  // namespace

TEST_CASE("CIR separates phase dependencies and barriers from handler schedule relations", "[cir][relations][phase]") {
    const auto cir = lower_source(kActivationProgram);

    const auto input      = phase_node_id(make_symbol_id(SymbolKind::Phase, "game.act", "input"));
    const auto fixed_tick = phase_node_id(make_symbol_id(SymbolKind::Phase, "game.act", "fixed_tick"));
    REQUIRE(cir.phase_dependencies.size() == 1);
    CHECK(cir.phase_dependencies.front().upstream == input);
    CHECK(cir.phase_dependencies.front().downstream == fixed_tick);

    const auto first  = handler_id("game.act", "FixedFirst", SymbolKind::Phase, "fixed_tick");
    const auto second = handler_id("game.act", "FixedSecond", SymbolKind::Phase, "fixed_tick");
    REQUIRE(cir.phase_barriers.size() == 2);
    CHECK(std::ranges::any_of(cir.phase_barriers, [&](const auto& barrier) {
        return barrier.upstream_phase == input && barrier.downstream_handler == first;
    }));
    CHECK(std::ranges::any_of(cir.phase_barriers, [&](const auto& barrier) {
        return barrier.upstream_phase == input && barrier.downstream_handler == second;
    }));
    CHECK(cir.schedule_dependencies.empty());
}

TEST_CASE("CIR represents legal event feedback without a schedule cycle", "[cir][relations][events]") {
    const auto cir = lower_source(kActivationProgram);

    const auto feedback_a = handler_id("game.act", "FeedbackA", SymbolKind::Event, "A");
    const auto feedback_b = handler_id("game.act", "FeedbackB", SymbolKind::Event, "B");
    CHECK(has_event_flow(cir, feedback_a, feedback_b));
    CHECK(has_event_flow(cir, feedback_b, feedback_a));

    // The cycle lives entirely in event flow; schedule validation still passes.
    CHECK(validates(cir));
}

TEST_CASE("CIR preserves stable topological order and parallel dependency levels", "[cir][schedule]") {
    const auto cir = lower_source(
        "module game.levels\n"
        "event tick\n"
        "trait Left\n"
        "trait Right\n"
        "extern rule ProduceLeft:\n"
        "    on tick:\n"
        "        writes:\n"
        "            Left\n"
        "extern rule ProduceRight:\n"
        "    on tick:\n"
        "        writes:\n"
        "            Right\n"
        "extern rule Consume:\n"
        "    on tick:\n"
        "        reads:\n"
        "            Left\n"
        "            Right\n");

    const auto* schedule = find_activation(cir, "game.levels.tick");
    REQUIRE(schedule != nullptr);
    REQUIRE(schedule->levels.size() == 2);
    CHECK(schedule->levels[0].index == 0);
    CHECK(schedule->levels[0].handlers.size() == 2);
    CHECK(schedule->levels[1].handlers ==
          std::vector<CirNodeId>{handler_id("game.levels", "Consume", SymbolKind::Event, "tick")});
    CHECK(schedule->stable_order.size() == 3);
    CHECK(schedule->stable_order.back() == handler_id("game.levels", "Consume", SymbolKind::Event, "tick"));
    CHECK(validates(cir));
}

// ── Tasks 2.8 / 2.9: runtime-owned producers and rasterization ──────────────

TEST_CASE("CIR gives scheduler-boundary producers their own nodes and edges", "[cir][producers]") {
    const auto cir = lower_source(
        "module std.core\n"
        "pub event load\n"
        "pub event unload\n"
        "rule Boot:\n"
        "    on load:\n"
        "        let x = 1\n"
        "rule Teardown:\n"
        "    on unload:\n"
        "        let y = 1\n");

    const auto load_source =
        event_producer_node_id(CirNodeKind::SchedulerBoundary, make_symbol_id(SymbolKind::Event, "std.core", "load"));
    REQUIRE(std::ranges::any_of(cir.event_producers, [&](const auto& node) {
        return node.id == load_source && node.kind == CirNodeKind::SchedulerBoundary;
    }));
    CHECK(has_event_flow(cir, load_source, handler_id("std.core", "Boot", SymbolKind::Event, "load")));
    CHECK(load_source.value.starts_with("cir:"));

    // No fabricated handler identity stands in for runtime-owned work.
    CHECK(find_handler(cir, load_source) == nullptr);
    CHECK(validates(cir));
}

TEST_CASE("CIR gives activation-commit producers their own nodes", "[cir][producers]") {
    const auto cir = lower_source(
        "module std.core\n"
        "pub event spawn\n"
        "pub event destroy\n"
        "rule Greet:\n"
        "    on spawn:\n"
        "        let x = 1\n"
        "rule Mourn:\n"
        "    on destroy:\n"
        "        let y = 1\n");

    const auto spawn_commit =
        event_producer_node_id(CirNodeKind::ActivationCommit, make_symbol_id(SymbolKind::Event, "std.core", "spawn"));
    CHECK(has_event_flow(cir, spawn_commit, handler_id("std.core", "Greet", SymbolKind::Event, "spawn")));
    CHECK(std::ranges::any_of(cir.event_producers,
                              [&](const auto& node) { return node.kind == CirNodeKind::ActivationCommit; }));
    CHECK(validates(cir));
}

TEST_CASE("CIR gives host external sources their own nodes", "[cir][producers]") {
    const auto cir = lower_source(
        "module game.ext\n"
        "pub extern event frame:\n"
        "    dt: float\n"
        "rule Tick:\n"
        "    on frame:\n"
        "        let x = 1\n");

    const auto source = event_producer_node_id(CirNodeKind::ExternalEventSource,
                                               make_symbol_id(SymbolKind::Event, "game.ext", "frame"));
    CHECK(has_event_flow(cir, source, handler_id("game.ext", "Tick", SymbolKind::Event, "frame")));
    CHECK(validates(cir));
}

TEST_CASE("CIR adds no producer node for an event nothing produces", "[cir][producers]") {
    const auto cir = lower_source(
        "module game.quiet\n"
        "event Damaged\n"
        "rule React:\n"
        "    on Damaged:\n"
        "        let x = 1\n");

    CHECK(cir.event_producers.empty());
    CHECK(cir.event_flows.empty());
}

namespace {

ModuleImports make_render_passes_imports() {
    ImportedSymbols syms;
    syms.module_name = "std.render.passes";
    ResolvedEnum pass_enum;
    pass_enum.name     = "Pass";
    pass_enum.variants = {"Quads"};
    syms.enums["Pass"] = pass_enum;
    ResolvedEnum target_enum;
    target_enum.name     = "Target";
    target_enum.variants = {"Screen"};
    syms.enums["Target"] = target_enum;
    ModuleImports imports;
    imports.add("passes", std::move(syms));
    return imports;
}

}  // namespace

TEST_CASE("CIR lowers a render pass into a rasterization node and pass-local flow", "[cir][render-passes]") {
    const auto cir = lower_source(
        "module game.render\n"
        "use std.render.passes as passes\n"
        "extern event Tick:\n"
        "    dt: float\n"
        "trait WorldTransform:\n"
        "    var position: vec2\n"
        "pub phase my_pass:\n"
        "    from:\n"
        "        Tick\n"
        "    shape: passes.Pass = passes.Pass.Quads\n"
        "    surface: passes.Target = passes.Target.Screen\n"
        "rule MyVertex:\n"
        "    filter:\n"
        "        WorldTransform as xf\n"
        "    on my_pass.vertex as v:\n"
        "        v.screen_position = xf.position\n"
        "        v.uv_out = v.uv\n"
        "        v.tint_out = #FFFFFFFF\n"
        "rule MyFragment:\n"
        "    on my_pass.fragment as f:\n"
        "        f.frag_color = f.tint\n",
        make_render_passes_imports());

    const auto phase  = make_symbol_id(SymbolKind::Phase, "game.render", "my_pass");
    const auto raster = rasterization_node_id(phase);
    REQUIRE(cir.rasterizations.size() == 1);
    CHECK(cir.rasterizations.front().id == raster);
    CHECK(cir.rasterizations.front().phase == phase);

    REQUIRE(cir.render_pass_flows.size() == 3);
    CHECK(std::ranges::count_if(cir.render_pass_flows, [&](const auto& flow) { return flow.after == raster; }) == 1);
    CHECK(std::ranges::count_if(cir.render_pass_flows, [&](const auto& flow) { return flow.before == raster; }) == 1);
    CHECK(std::ranges::any_of(cir.render_pass_flows,
                              [&](const auto& flow) { return flow.after == phase_node_id(phase); }));
    CHECK(validates(cir));
}

// ── Task 2.10: logical spatial-join opportunities ───────────────────────────

TEST_CASE("CIR preserves a recognized 2D spatial-join opportunity", "[cir][spatial-join]") {
    const auto cir = lower_source(
        "module std.collision.flat\n"
        "event tick\n"
        "trait Transform:\n"
        "    var position: vec2\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, b_radius: float) bool:\n"
        "    return a_radius + b_radius >= 0.0\n"
        "rule DetectContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Transform\n"
        "            Collider\n"
        "        b:\n"
        "            Transform\n"
        "            Collider\n"
        "    where:\n"
        "        circles_overlap(a.Transform.position, a.Collider.radius, b.Transform.position, b.Collider.radius)\n"
        "    on tick:\n"
        "        let x = 1\n");

    const auto* detect =
        find_handler(cir, handler_id("std.collision.flat", "DetectContact", SymbolKind::Event, "tick"));
    REQUIRE(detect != nullptr);
    REQUIRE(detect->spatial_join.has_value());
    const auto& plan = *detect->spatial_join;
    CHECK(plan.dimension == SpatialJoinDimension::Flat2D);
    CHECK(plan.left.binding_index == 0);
    CHECK(make_canonical_id(plan.left.position.trait) == "std.collision.flat.Transform");
    CHECK(plan.left.position.field_path == std::vector<std::string>{"position"});
    CHECK(make_canonical_id(plan.left.radius.trait) == "std.collision.flat.Collider");
    CHECK(plan.right.binding_index == 1);
    CHECK(plan.matched_predicate_index == 0);
}

TEST_CASE("CIR preserves a recognized 3D spatial-join opportunity", "[cir][spatial-join]") {
    const auto cir = lower_source(
        "module std.collision.volume\n"
        "event tick\n"
        "trait Transform:\n"
        "    var position: vec3\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "pub func spheres_overlap(a_position: vec3, a_radius: float, b_position: vec3, b_radius: float) bool:\n"
        "    return a_radius + b_radius >= 0.0\n"
        "rule DetectContact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Transform\n"
        "            Collider\n"
        "        b:\n"
        "            Transform\n"
        "            Collider\n"
        "    where:\n"
        "        spheres_overlap(a.Transform.position, a.Collider.radius, b.Transform.position, b.Collider.radius)\n"
        "    on tick:\n"
        "        let x = 1\n");

    const auto* detect =
        find_handler(cir, handler_id("std.collision.volume", "DetectContact", SymbolKind::Event, "tick"));
    REQUIRE(detect != nullptr);
    REQUIRE(detect->spatial_join.has_value());
    CHECK(detect->spatial_join->dimension == SpatialJoinDimension::Volume3D);
    // A logical opportunity only — CIR v1 names no broad-phase strategy.
    CHECK(detect->spatial_join->matched_predicate_index == 0);
}

TEST_CASE("CIR fabricates no spatial-join metadata for an ordinary pair domain", "[cir][spatial-join]") {
    const auto cir = lower_source(
        "module game.pairs\n"
        "event tick\n"
        "trait Collider:\n"
        "    var radius: float\n"
        "rule Contact:\n"
        "    pairs:\n"
        "        a:\n"
        "            Collider\n"
        "        b:\n"
        "            Collider\n"
        "    where:\n"
        "        a != b\n"
        "    on tick:\n"
        "        let x = 1\n");

    const auto* contact = find_handler(cir, handler_id("game.pairs", "Contact", SymbolKind::Event, "tick"));
    REQUIRE(contact != nullptr);
    CHECK(contact->domain == HandlerDomainKind::Pair);
    CHECK_FALSE(contact->spatial_join.has_value());
}

// ── Tasks 2.11 / 2.12: CIR validation ───────────────────────────────────────

TEST_CASE("CIR lowered from a valid program validates", "[cir][validation]") {
    CHECK(validates(lower_source(kBaseProgram)));
    CHECK(validates(lower_source(kConflictProgram)));
    CHECK(validates(lower_source(kActivationProgram)));
}

TEST_CASE("CIR validation rejects duplicate node ids", "[cir][validation]") {
    auto cir = lower_source(kBaseProgram);
    cir.handlers.push_back(cir.handlers.front());
    std::ranges::sort(cir.handlers, [](const auto& left, const auto& right) { return left.id < right.id; });

    CHECK(first_validation_error(cir).contains("duplicate node id"));
}

TEST_CASE("CIR validation rejects a relation endpoint that names no node", "[cir][validation]") {
    auto cir = lower_source(kConflictProgram);
    REQUIRE_FALSE(cir.schedule_dependencies.empty());
    cir.schedule_dependencies.front().before = CirNodeId{.value = "game.flow.Ghost/on game.flow.input"};
    std::ranges::sort(cir.schedule_dependencies, schedule_relation_precedes);

    CHECK(first_validation_error(cir).contains("names no node"));
}

TEST_CASE("CIR validation rejects cyclic schedule relations", "[cir][validation]") {
    auto cir = lower_source(kConflictProgram);
    const auto* forward =
        find_relation(cir, flow_handler("Writer"), flow_handler("Reader"), ScheduleEdgeKind::DataConflict);
    REQUIRE(forward != nullptr);
    cir.schedule_dependencies.push_back(CirScheduleRelation{
        .before = flow_handler("Reader"), .after = flow_handler("Writer"), .kind = ScheduleEdgeKind::DataConflict});
    std::ranges::sort(cir.schedule_dependencies, schedule_relation_precedes);

    CHECK(first_validation_error(cir).contains("cyclic schedule relations"));
}

TEST_CASE("CIR validation accepts cyclic event flow", "[cir][validation]") {
    CHECK(validates(lower_source(kActivationProgram)));
}

TEST_CASE("CIR validation rejects non-canonical ordering", "[cir][validation]") {
    auto cir = lower_source(kBaseProgram);
    REQUIRE(cir.handlers.size() > 1);
    std::ranges::reverse(cir.handlers);

    CHECK(first_validation_error(cir).contains("not in canonical order"));
}

TEST_CASE("CIR validation rejects inconsistent dependency-level membership", "[cir][validation]") {
    auto cir      = lower_source(kBaseProgram);
    auto schedule = std::ranges::find_if(cir.activation_schedules, [](const auto& entry) {
        return make_canonical_id(entry.activation.symbol) == "game.cir.tick";
    });
    REQUIRE(schedule != cir.activation_schedules.end());
    REQUIRE_FALSE(schedule->levels.empty());
    schedule->levels.front().handlers.push_back(CirNodeId{.value = "game.cir.Ghost/on game.cir.tick"});

    CHECK(first_validation_error(cir).contains("absent from its stable order"));
}

TEST_CASE("CIR validation reports every problem rather than stopping at the first", "[cir][validation]") {
    auto cir = lower_source(kConflictProgram);
    cir.schedule_dependencies.push_back(CirScheduleRelation{.before = CirNodeId{.value = "a.Ghost/on a.input"},
                                                            .after  = CirNodeId{.value = "b.Ghost/on b.input"},
                                                            .kind   = ScheduleEdgeKind::DataConflict});
    std::ranges::sort(cir.schedule_dependencies, schedule_relation_precedes);

    ErrorReporter errors;
    CHECK_FALSE(validate_program(cir, errors));
    CHECK(errors.error_count() >= 2);
}

// ── Section 3: canonical JSON output ────────────────────────────────────────

namespace {

/// The reference program: every CIR node kind, every relation kind, unary /
/// pair / selectionless domains, a recognized spatial join, explicit ordering,
/// effect conflict, and a multi-level activation, in one document.
constexpr std::string_view kReferenceProgram =
    "module std.core\n"
    "use std.collision.flat as collision\n"
    "use std.render.passes as passes\n"
    "pub event load\n"
    "pub event spawn\n"
    "pub extern event frame:\n"
    "    dt: float\n"
    "event Damaged:\n"
    "    amount: int\n"
    "trait Health:\n"
    "    var current: float\n"
    "trait Marker\n"
    "trait Shield\n"
    "trait PointerHit\n"
    "trait Transform:\n"
    "    var position: vec2\n"
    "trait Collider:\n"
    "    var radius: float\n"
    "phase input:\n"
    "    from:\n"
    "        frame\n"
    "phase fixed_tick:\n"
    "    after:\n"
    "        input\n"
    "    every: 0.5\n"
    "pub phase overlay:\n"
    "    from:\n"
    "        frame\n"
    "    shape: passes.Pass = passes.Pass.Quads\n"
    "    surface: passes.Target = passes.Target.Screen\n"
    "rule Boot:\n"
    "    on load:\n"
    "        let ready = 1\n"
    "rule Greet:\n"
    "    on spawn:\n"
    "        emit Damaged:\n"
    "            amount = 1\n"
    "rule React:\n"
    "    on Damaged:\n"
    "        let taken = Damaged.amount\n"
    "extern rule Sampler:\n"
    "    on frame:\n"
    "        commands:\n"
    "            destroy\n"
    "            add Shield\n"
    "extern rule Projector:\n"
    "    filter:\n"
    "        PointerHit\n"
    "    on frame:\n"
    "        projects:\n"
    "            PointerHit\n"
    "extern rule Selector:\n"
    "    filter:\n"
    "        PointerHit\n"
    "    on frame:\n"
    "        reads:\n"
    "            PointerHit\n"
    "extern rule LogA:\n"
    "    on frame:\n"
    "        writes:\n"
    "            Marker\n"
    "extern rule LogB:\n"
    "    on frame:\n"
    "        writes:\n"
    "            Marker\n"
    "extern rule Sweeper:\n"
    "    after:\n"
    "        Writer\n"
    "    on fixed_tick:\n"
    "        writes:\n"
    "            Health\n"
    "extern rule Writer:\n"
    "    filter:\n"
    "        Marker\n"
    "    exclude:\n"
    "        Shield\n"
    "    on fixed_tick:\n"
    "        writes:\n"
    "            Health\n"
    "extern rule Reader:\n"
    "    on fixed_tick:\n"
    "        reads:\n"
    "            Health\n"
    "        effects:\n"
    "            graphics\n"
    "extern rule Painter:\n"
    "    on fixed_tick:\n"
    "        after:\n"
    "            Reader/on fixed_tick\n"
    "        effects:\n"
    "            graphics\n"
    "rule Contact:\n"
    "    pairs:\n"
    "        a:\n"
    "            Transform\n"
    "            Collider\n"
    "        b:\n"
    "            Transform\n"
    "            Collider\n"
    "    where:\n"
    "        collision.circles_overlap(a.Transform.position, a.Collider.radius, b.Transform.position, "
    "b.Collider.radius)\n"
    "    on fixed_tick:\n"
    "        let touching = 1\n"
    "rule OverlayVertex:\n"
    "    filter:\n"
    "        Transform as xf\n"
    "    on overlay.vertex as v:\n"
    "        v.screen_position = xf.position\n"
    "        v.uv_out = v.uv\n"
    "        v.tint_out = #FFFFFFFF\n"
    "rule OverlayFragment:\n"
    "    on overlay.fragment as f:\n"
    "        f.frag_color = f.tint\n";

ImportedSymbols make_circles_overlap_module() {
    ImportedSymbols syms;
    syms.module_name = "std.collision.flat";

    ResolvedFunc func;
    func.name           = "circles_overlap";
    func.module_name    = "std.collision.flat";
    func.is_pub         = true;
    func.effect_summary = std::unordered_set<std::string>{};  // proven pure: allowed in where:
    func.params         = {ResolvedParam{.name = "a_position", .type = make_vec2_type()},
                           ResolvedParam{.name = "a_radius", .type = make_float_type()},
                           ResolvedParam{.name = "b_position", .type = make_vec2_type()},
                           ResolvedParam{.name = "b_radius", .type = make_float_type()}};
    func.return_type    = make_bool_type();
    const auto symbol   = make_symbol_id(SymbolKind::Func, "std.collision.flat", "circles_overlap");
    func.symbol_id      = symbol;
    func.canonical_id   = make_canonical_id(symbol);

    syms.funcs["circles_overlap"] = std::move(func);
    return syms;
}

ModuleImports make_reference_imports() {
    ImportedSymbols passes;
    passes.module_name = "std.render.passes";
    ResolvedEnum pass_enum;
    pass_enum.name       = "Pass";
    pass_enum.variants   = {"Quads"};
    passes.enums["Pass"] = pass_enum;
    ResolvedEnum target_enum;
    target_enum.name       = "Target";
    target_enum.variants   = {"Screen"};
    passes.enums["Target"] = target_enum;

    ModuleImports imports;
    imports.add("passes", std::move(passes));
    imports.add("collision", make_circles_overlap_module());
    return imports;
}

std::filesystem::path golden_path(const std::string& name) {
    return std::filesystem::path{CACTUS_TEST_FIXTURES_DIR} / "cir" / name;
}

/// Compares `actual` against the checked-in golden file, writing a sibling
/// `.actual` file on any mismatch so an intended change can be reviewed and
/// copied over the golden.
void check_golden(const std::string& name, const std::string& actual) {
    const auto path = golden_path(name);
    std::ostringstream expected;
    if (std::ifstream file(path, std::ios::binary); file.is_open()) {
        expected << file.rdbuf();
    }
    if (actual == expected.str()) {
        SUCCEED();
        return;
    }
    const auto rejected = std::filesystem::path{path}.concat(".actual");
    std::filesystem::create_directories(rejected.parent_path());
    std::ofstream(rejected, std::ios::binary) << actual;
    FAIL("golden mismatch for " << path.string() << " — actual written to " << rejected.string());
}

}  // namespace

TEST_CASE("CIR JSON matches its golden document", "[cir][json]") {
    const auto cir  = lower_source(kReferenceProgram, make_reference_imports());
    const auto json = write_json(cir);

    REQUIRE(validates(cir));
    check_golden("reference.json", json);
}

TEST_CASE("CIR JSON identifies its schema and version first", "[cir][json]") {
    const auto json = write_json(lower_source(kBaseProgram));

    CHECK(json.starts_with("{\n  \"schema\": \"cactus-cir\",\n  \"version\": 1,\n"));
    CHECK(json.ends_with("}\n"));
}

TEST_CASE("CIR JSON serialization is byte-for-byte repeatable", "[cir][json][determinism]") {
    const auto cir = lower_source(kReferenceProgram, make_reference_imports());

    CHECK(write_json(cir) == write_json(cir));
}

TEST_CASE("CIR JSON is identical across independent compilations of the same source", "[cir][json][determinism]") {
    const auto first  = write_json(lower_source(kReferenceProgram, make_reference_imports()));
    const auto second = write_json(lower_source(kReferenceProgram, make_reference_imports()));

    CHECK(first == second);
}

TEST_CASE("CIR JSON does not inherit unordered-container iteration order", "[cir][json][determinism]") {
    // Semantically identical programs whose declarations are inserted into the
    // analyzer's hash maps in different orders must serialize identically once
    // the source order they legitimately record is matched.
    auto shuffled       = lower_source(kReferenceProgram, make_reference_imports());
    const auto expected = write_json(shuffled);

    std::ranges::reverse(shuffled.traits);
    std::ranges::reverse(shuffled.handlers);
    std::ranges::reverse(shuffled.rule_groups);
    std::ranges::reverse(shuffled.schedule_dependencies);
    std::ranges::reverse(shuffled.event_flows);
    std::ranges::sort(shuffled.traits,
                      [](const auto& left, const auto& right) { return symbol_precedes(left.symbol, right.symbol); });
    std::ranges::sort(shuffled.handlers, [](const auto& left, const auto& right) { return left.id < right.id; });
    std::ranges::sort(shuffled.rule_groups,
                      [](const auto& left, const auto& right) { return symbol_precedes(left.rule, right.rule); });
    std::ranges::sort(shuffled.schedule_dependencies, schedule_relation_precedes);
    std::ranges::sort(shuffled.event_flows, event_flow_precedes);

    CHECK(write_json(shuffled) == expected);
}

TEST_CASE("CIR JSON escapes hostile strings", "[cir][json][escaping]") {
    CHECK(json_escaped(R"(say "hi")") == R"(say \"hi\")");
    CHECK(json_escaped(R"(C:\Data\game.cactus)") == R"(C:\\Data\\game.cactus)");
    CHECK(json_escaped("line\nbreak") == "line\\nbreak");
    CHECK(json_escaped("tab\there") == "tab\\there");
    CHECK(json_escaped("carriage\rreturn") == "carriage\\rreturn");
    CHECK(json_escaped(std::string("bell\x07")) == "bell\\u0007");
    CHECK(json_escaped(std::string("null\0byte", 9)) == "null\\u0000byte");
    CHECK(json_escaped("punctuation: {}[],:") == "punctuation: {}[],:");
    // UTF-8 passes through as-is; JSON strings carry it directly.
    CHECK(json_escaped("caf\xc3\xa9 \xe2\x86\x92") == "caf\xc3\xa9 \xe2\x86\x92");
}

TEST_CASE("CIR JSON escapes an unusual source path in a handler location", "[cir][json][escaping]") {
    ErrorReporter errors;
    Lexer lexer(std::string{kBaseProgram}, R"(C:\odd "dir"\game.cactus)", errors);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens), errors);
    auto ast      = parser.parse_program();
    auto analyzed = SemanticAnalyzer(errors).analyze(ast);
    REQUIRE_FALSE(errors.has_errors());

    const auto json = write_json(lower_program(analyzed));
    CHECK(json.contains(R"("file": "C:\\odd \"dir\"\\game.cactus")"));
}

TEST_CASE("CIR JSON writes finite doubles and rejects non-finite ones", "[cir][json][escaping]") {
    std::string out;
    append_json_number(out, 0.5);
    CHECK(out == "0.5");

    out.clear();
    append_json_number(out, std::numeric_limits<double>::infinity());
    CHECK(out == "null");
}

// ── Section 4: DOT and Mermaid projections ──────────────────────────────────

namespace {

const CirProjectedNode* projected_node(const CirGraphProjection& projection, const CirNodeId& id) {
    const auto found = std::ranges::find_if(projection.nodes, [&](const auto& node) { return node.id == id; });
    return found == projection.nodes.end() ? nullptr : &*found;
}

std::vector<const CirProjectedEdge*> projected_edges(const CirGraphProjection& projection,
                                                     const CirNodeId& before,
                                                     const CirNodeId& after) {
    std::vector<const CirProjectedEdge*> found;
    for (const auto& edge : projection.edges) {
        if (projection.nodes[edge.before].id == before && projection.nodes[edge.after].id == after) {
            found.push_back(&edge);
        }
    }
    return found;
}

}  // namespace

TEST_CASE("the graph projection groups handlers by rule without merging them", "[cir][projection]") {
    const auto projection = project_graph(lower_source(kBaseProgram));

    const auto on_tick       = handler_id("game.cir", "Vitals", SymbolKind::Event, "tick");
    const auto on_damaged    = handler_id("game.cir", "Vitals", SymbolKind::Event, "Damaged");
    const auto* tick_node    = projected_node(projection, on_tick);
    const auto* damaged_node = projected_node(projection, on_damaged);
    REQUIRE(tick_node != nullptr);
    REQUIRE(damaged_node != nullptr);

    CHECK(tick_node->group == damaged_node->group);
    CHECK(tick_node->display_id != damaged_node->display_id);
    REQUIRE(tick_node->group != CirGraphProjection::NO_GROUP);
    CHECK(projection.groups[tick_node->group].label == "game.cir.Vitals");
    // Handler identity survives grouping.
    CHECK(tick_node->label == on_tick.value);
    CHECK(damaged_node->label == on_damaged.value);
}

TEST_CASE("the graph projection assigns short display ids after canonical sorting", "[cir][projection]") {
    const auto projection = project_graph(lower_source(kReferenceProgram, make_reference_imports()));

    for (std::size_t index = 0; index < projection.nodes.size(); ++index) {
        CHECK(projection.nodes[index].display_id == "n" + std::to_string(index));
        if (index > 0) {
            CHECK(projection.nodes[index - 1].id < projection.nodes[index].id);
        }
    }
}

TEST_CASE("the graph projection keeps synthetic nodes standalone", "[cir][projection]") {
    const auto cir        = lower_source(kReferenceProgram, make_reference_imports());
    const auto projection = project_graph(cir);

    for (const auto& node : projection.nodes) {
        if (node.kind == CirNodeKind::Handler) {
            continue;
        }
        INFO(node.id.value);
        CHECK(node.group == CirGraphProjection::NO_GROUP);
    }
    const auto* boundary = projected_node(
        projection,
        event_producer_node_id(CirNodeKind::SchedulerBoundary, make_symbol_id(SymbolKind::Event, "std.core", "load")));
    REQUIRE(boundary != nullptr);
    CHECK(boundary->label == "scheduler-boundary: std.core.load");
}

TEST_CASE("the graph projection labels trait dependencies with canonical provenance", "[cir][projection]") {
    const auto projection = project_graph(lower_source(kConflictProgram));

    const auto edges = projected_edges(projection, flow_handler("Writer"), flow_handler("Reader"));
    REQUIRE(edges.size() == 1);
    CHECK(edges.front()->style == CirEdgeStyle::ScheduleTrait);
    CHECK(edges.front()->label == "game.flow.Position (read+write/read)");
}

TEST_CASE("the graph projection distinguishes every relation kind", "[cir][projection]") {
    const auto projection = project_graph(lower_source(kReferenceProgram, make_reference_imports()));

    std::vector<CirEdgeStyle> seen;
    for (const auto& edge : projection.edges) {
        if (std::ranges::find(seen, edge.style) == seen.end()) {
            seen.push_back(edge.style);
        }
    }
    for (const auto style : {CirEdgeStyle::ScheduleTrait,
                             CirEdgeStyle::ScheduleEffect,
                             CirEdgeStyle::ScheduleExplicit,
                             CirEdgeStyle::PhaseDependency,
                             CirEdgeStyle::PhaseBarrier,
                             CirEdgeStyle::EventFlow,
                             CirEdgeStyle::RenderPassFlow}) {
        INFO(cir_edge_style_name(style));
        CHECK(std::ranges::find(seen, style) != seen.end());
    }
}

TEST_CASE("CIR DOT matches its golden document", "[cir][dot]") {
    const auto cir = lower_source(kReferenceProgram, make_reference_imports());

    check_golden("reference.dot", write_dot(cir));
}

TEST_CASE("CIR DOT is a syntactically balanced digraph with rule clusters", "[cir][dot]") {
    const auto dot = write_dot(lower_source(kReferenceProgram, make_reference_imports()));

    CHECK(dot.starts_with("digraph cactus_cir {\n"));
    CHECK(dot.ends_with("}\n"));
    CHECK(dot.contains("subgraph cluster_g0 {"));
    CHECK(std::ranges::count(dot, '{') == std::ranges::count(dot, '}'));
}

TEST_CASE("CIR DOT escapes hostile label text", "[cir][dot][escaping]") {
    CHECK(dot_escaped(R"(say "hi")") == R"(say \"hi\")");
    CHECK(dot_escaped(R"(C:\Data\game.cactus)") == R"(C:\\Data\\game.cactus)");
    CHECK(dot_escaped("line\nbreak") == "line\\nbreak");
    CHECK(dot_escaped(std::string("bell\x07")) == "bell ");
    CHECK(dot_escaped("punctuation: <>|[]{}") == "punctuation: <>|[]{}");
    CHECK(dot_escaped("caf\xc3\xa9") == "caf\xc3\xa9");
}

TEST_CASE("CIR Mermaid matches its golden document", "[cir][mermaid]") {
    const auto cir = lower_source(kReferenceProgram, make_reference_imports());

    check_golden("reference.mmd", write_mermaid(cir));
}

TEST_CASE("CIR Mermaid is a flowchart with balanced rule subgraphs", "[cir][mermaid]") {
    const auto mermaid = write_mermaid(lower_source(kReferenceProgram, make_reference_imports()));

    CHECK(mermaid.starts_with("flowchart LR\n"));
    CHECK(mermaid.contains("  subgraph g0[\"std.core.Boot\"]\n"));

    std::size_t subgraphs = 0;
    std::size_t ends      = 0;
    std::istringstream lines(mermaid);
    for (std::string line; std::getline(lines, line);) {
        subgraphs += line.starts_with("  subgraph ") ? 1 : 0;
        ends += line == "  end" ? 1 : 0;
    }
    CHECK(subgraphs > 0);
    CHECK(subgraphs == ends);
}

TEST_CASE("CIR Mermaid escapes hostile label text", "[cir][mermaid][escaping]") {
    CHECK(mermaid_escaped(R"(say "hi")") == "say &quot;hi&quot;");
    CHECK(mermaid_escaped(R"(C:\Data\game.cactus)") == R"(C:\Data\game.cactus)");
    CHECK(mermaid_escaped("a & b") == "a &amp; b");
    CHECK(mermaid_escaped("<tag>") == "&lt;tag&gt;");
    CHECK(mermaid_escaped("#FFFFFFFF") == "&#35;FFFFFFFF");
    CHECK(mermaid_escaped("line\nbreak") == "line break");
    CHECK(mermaid_escaped("caf\xc3\xa9") == "caf\xc3\xa9");
}

TEST_CASE("both projections keep cyclic event edges visually distinct from schedule edges", "[cir][dot][mermaid]") {
    const auto cir     = lower_source(kActivationProgram);
    const auto dot     = write_dot(cir);
    const auto mermaid = write_mermaid(cir);

    // The A -> B -> A event cycle is present in both, drawn with the event
    // style rather than the solid schedule arrow.
    CHECK(dot.contains(R"([label="event game.act.B", color="#2e7d32", style=dashed, constraint=false])"));
    CHECK(dot.contains(R"([label="event game.act.A", color="#2e7d32", style=dashed, constraint=false])"));
    CHECK(mermaid.contains(R"(-.->|"event game.act.B"|)"));
    CHECK(mermaid.contains("linkStyle "));
    CHECK(mermaid.contains("stroke:#2e7d32;"));
}

TEST_CASE("both projections stay lossy rather than embedding full contract detail", "[cir][dot][mermaid]") {
    const auto cir     = lower_source(kReferenceProgram, make_reference_imports());
    const auto dot     = write_dot(cir);
    const auto mermaid = write_mermaid(cir);

    for (const auto& text : {dot, mermaid}) {
        CHECK_FALSE(text.contains("\"declaration_order\""));
        CHECK_FALSE(text.contains("\"bound_reads\""));
        CHECK_FALSE(text.contains("\"spatial_join\""));
        CHECK_FALSE(text.contains("cactus-cir"));
    }
}

TEST_CASE("graphical CIR output is byte-for-byte repeatable", "[cir][dot][mermaid][determinism]") {
    const auto cir = lower_source(kReferenceProgram, make_reference_imports());

    CHECK(write_dot(cir) == write_dot(cir));
    CHECK(write_mermaid(cir) == write_mermaid(cir));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
