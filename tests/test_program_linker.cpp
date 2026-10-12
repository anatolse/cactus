// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <set>

using namespace cactus;
namespace fs = std::filesystem;

static fs::path linker_build_dir() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR) / "linker_test_build";
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static DecoratedProgram make_program(const std::string& trait_name, bool is_pub, const std::string& enum_name = "") {
    DecoratedProgram prog;

    ResolvedTrait rt;
    rt.name   = trait_name;
    rt.is_pub = is_pub;
    ResolvedField f;
    f.name   = "x";
    f.type   = make_float_type();
    f.is_var = true;
    rt.fields.push_back(f);
    prog.traits[trait_name] = rt;

    if (!enum_name.empty()) {
        ResolvedEnum re;
        re.name               = enum_name;
        re.variants           = {"A", "B"};
        prog.enums[enum_name] = re;
    }

    return prog;
}

static SymbolId linked_symbol(SymbolKind kind, const std::string& module, const std::string& name) {
    return make_symbol_id(kind, module, name);
}

static HandlerNode linked_handler(const SymbolId& rule,
                                  const ResolvedHandlerTrigger& trigger,
                                  std::uint64_t declaration_index) {
    return HandlerNode{.identity          = {.rule = rule, .trigger = trigger},
                       .declaration_order = {.declaration_index = declaration_index}};
}

// ── Task 5.2: Incremental merge — distinct types ──────────────────────────────

TEST_CASE("program_linker: merge two programs with distinct traits", "[linker][5.2]") {
    auto prog_a = make_program("Position", true);
    auto prog_b = make_program("EnemyAI", true);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, prog_a, "player"));
    REQUIRE(linker.merge_into(merged, prog_b, "enemies"));

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.traits.count("Position") == 1);
    CHECK(merged.traits.count("EnemyAI") == 1);
}

static DecoratedProgram make_const_program(const std::string& module, const std::string& name, TypeInfo type) {
    DecoratedProgram prog;
    const auto symbol = make_symbol_id(SymbolKind::Const, module, name);
    ResolvedConst constant;
    constant.name         = name;
    constant.type         = std::move(type);
    constant.module_name  = module;
    constant.canonical_id = make_canonical_id(symbol);
    constant.symbol_id    = symbol;
    prog.consts[name]     = constant;
    prog.const_order.push_back(symbol);
    return prog;
}

TEST_CASE("program_linker: constants of different modules are merged", "[linker][const]") {
    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, make_const_program("player", "MOVE_SPEED", make_float_type()), "player"));
    REQUIRE(linker.merge_into(merged, make_const_program("level", "TILE_SIZE", make_int_type()), "level"));
    CHECK_FALSE(errors.has_errors());
    CHECK(merged.consts.contains("player.MOVE_SPEED"));
    CHECK(merged.consts.contains("level.TILE_SIZE"));
    REQUIRE(merged.const_order.size() == 2);
    CHECK(merged.const_order[0].local_name == "MOVE_SPEED");
}

TEST_CASE("program_linker: the same constant name in two modules does not conflict", "[linker][const]") {
    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, make_const_program("A", "MAX_HEALTH", make_int_type()), "A"));
    REQUIRE(linker.merge_into(merged, make_const_program("B", "MAX_HEALTH", make_int_type()), "B"));
    CHECK_FALSE(errors.has_errors());
    CHECK(merged.consts.contains("A.MAX_HEALTH"));
    CHECK(merged.consts.contains("B.MAX_HEALTH"));
}

TEST_CASE("program_linker: constants survive an artifact round trip", "[linker][const]") {
    auto build_dir = linker_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    auto prog = make_const_program("tables", "WAVES", make_list_type(make_int_type()));
    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    REQUIRE(artifact.save(prog, "tables", build_dir));
    ProgramLinker linker(errors);
    auto merged = linker.link({build_dir / "tables.cmod"});
    REQUIRE(merged.has_value());
    CHECK_FALSE(errors.has_errors());
    REQUIRE(merged->consts.contains("tables.WAVES"));
    const auto& type = merged->consts.at("tables.WAVES").type;
    CHECK(type.kind == TypeKind::List);
    REQUIRE(type.element != nullptr);
    CHECK(type.element->kind == TypeKind::Int);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: merge includes enums from all modules", "[linker][5.2]") {
    auto prog_a = make_program("Position", true, "Direction");
    auto prog_b = make_program("Velocity", true, "State");

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, prog_a, "player"));
    REQUIRE(linker.merge_into(merged, prog_b, "physics"));

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.enums.count("Direction") == 1);
    CHECK(merged.enums.count("State") == 1);
}

TEST_CASE("program_linker: dependency graphs are concatenated", "[linker][5.2]") {
    DecoratedProgram prog_a;
    RuleDependency dep_a;
    dep_a.rule_name = "MoveSystem";
    dep_a.reads.insert("Position");
    prog_a.dependency_graph.push_back(dep_a);

    DecoratedProgram prog_b;
    RuleDependency dep_b;
    dep_b.rule_name = "RenderSystem";
    dep_b.reads.insert("Sprite");
    prog_b.dependency_graph.push_back(dep_b);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    linker.merge_into(merged, prog_a, "player");
    linker.merge_into(merged, prog_b, "render");

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.dependency_graph.size() == 2);
    CHECK(merged.dependency_graph[0].rule_name == "MoveSystem");
    CHECK(merged.dependency_graph[1].rule_name == "RenderSystem");
}

// ── Task 4.4: Same simple pub name from different modules is accepted ──────────

TEST_CASE("program_linker: same simple pub trait name from different modules is accepted", "[linker][4.4]") {
    // Both modules have pub trait "Position" — different canonical IDs, so no conflict.
    auto prog_a = make_program("Position", true);
    auto prog_b = make_program("Position", true);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, prog_a, "modA"));
    REQUIRE(linker.merge_into(merged, prog_b, "modB"));

    CHECK_FALSE(errors.has_errors());
    // Both canonical keys ("modA.Position", "modB.Position") conflict-detect cleanly;
    // because canonical_id is empty in make_program(), simple name is used as the map key
    // and last-write-wins (both are from different modules with no canonical_id set).
    CHECK(merged.traits.count("Position") == 1);
}

TEST_CASE("program_linker: states of different modules are merged by canonical identity",
          "[linker][exclusive-states]") {
    const auto make_state_program = [](const std::string& module) {
        DecoratedProgram prog;
        ResolvedState state;
        state.name          = "Mode";
        state.module_name   = module;
        state.canonical_id  = make_canonical_id(module, "Mode");
        state.symbol_id     = make_symbol_id(SymbolKind::State, module, "Mode");
        state.variants      = {{.name = "Idle"}, {.name = "Gone", .is_final = true}};
        prog.states["Mode"] = state;
        return prog;
    };

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, make_state_program("modA"), "modA"));
    REQUIRE(linker.merge_into(merged, make_state_program("modB"), "modB"));

    CHECK_FALSE(errors.has_errors());
    REQUIRE(merged.states.contains("modA.Mode"));
    REQUIRE(merged.states.contains("modB.Mode"));
    CHECK(merged.states.at("modB.Mode").variants[1].is_final);
}

TEST_CASE("program_linker: merging same module twice is idempotent", "[linker][std-core]") {
    auto std_core = make_program("KeepOnLoad", true);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, std_core, "std.core"));
    REQUIRE(linker.merge_into(merged, std_core, "std.core"));

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.traits.count("KeepOnLoad") == 1);
}

TEST_CASE("program_linker: same simple enum name from different modules is accepted", "[linker][4.4]") {
    auto prog_a = make_program("TraitA", true, "Direction");
    auto prog_b = make_program("TraitB", true, "Direction");  // same enum simple name, different module

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, prog_a, "modA"));
    REQUIRE(linker.merge_into(merged, prog_b, "modB"));

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.enums.count("Direction") == 1);
}

TEST_CASE("program_linker: non-pub trait with same name does not conflict", "[linker][5.3]") {
    // Non-pub traits don't enter the global namespace, so no conflict
    auto prog_a = make_program("InternalHelper", false);  // not pub
    auto prog_b = make_program("InternalHelper", false);  // not pub, same name

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, prog_a, "modA"));
    REQUIRE(linker.merge_into(merged, prog_b, "modB"));

    // No error — both are non-pub
    CHECK_FALSE(errors.has_errors());
    // The second one overwrites the first (implementation detail, both are non-pub)
    CHECK(merged.traits.count("InternalHelper") == 1);
}

// ── Task 5.5: String pool merged ─────────────────────────────────────────────

TEST_CASE("program_linker: string pool contains symbol names after merge", "[linker][5.5]") {
    auto prog_a = make_program("Position", true);
    auto prog_b = make_program("Velocity", true);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    linker.merge_into(merged, prog_a, "player");
    linker.merge_into(merged, prog_b, "physics");

    // The pool should have the symbol names interned
    CHECK(merged.string_pool.contains("Position"));
    CHECK(merged.string_pool.contains("Velocity"));
}

// ── Task 5.1: Link from .cmod artifact files ──────────────────────────────────

TEST_CASE("program_linker: link from artifact files round-trip", "[linker][5.1]") {
    auto build_dir = linker_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Save two programs as artifacts
    auto prog_a = make_program("Position", true);
    auto prog_b = make_program("EnemyAI", true);

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(prog_a, "player", build_dir));
    REQUIRE(artifact.save(prog_b, "enemies", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    // Link them
    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "player.cmod", build_dir / "enemies.cmod"});

    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());
    CHECK(merged->traits.count("Position") == 1);
    CHECK(merged->traits.count("EnemyAI") == 1);
    CHECK(merged->ast == nullptr);  // AST not preserved in artifacts

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: link accepts same simple pub name from different module artifacts", "[linker][4.4]") {
    auto build_dir = linker_build_dir() / "same_name";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Both modules define pub trait "Config" — different canonical IDs, so coexistence is fine.
    auto prog_a = make_program("Config", true);
    auto prog_b = make_program("Config", true);

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    artifact.save(prog_a, "modA", build_dir);
    artifact.save(prog_b, "modB", build_dir);
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "modA.cmod", build_dir / "modB.cmod"});

    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());

    fs::remove_all(build_dir, ec);
}

// ── Task 6.2: std.transform.flat + std.transform.volume coexistence ──────────

TEST_CASE("program_linker: std.transform.flat and std.transform.volume link without duplicate symbol error",
          "[linker][6.2]") {
    auto build_dir = linker_build_dir() / "transform_coexist";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Reproduce the motivating case: flat and volume both export LocalTransform
    // and WorldTransform (same local names, distinct canonical IDs).
    auto make_transform_prog = [](const std::string& mod) {
        DecoratedProgram prog;
        prog.module_name = mod;
        for (const char* local : {"LocalTransform", "WorldTransform"}) {
            ResolvedTrait t;
            t.name             = local;
            t.is_pub           = true;
            t.module_name      = mod;
            t.canonical_id     = mod + "." + local;
            prog.traits[local] = t;
        }
        return prog;
    };

    auto flat_prog = make_transform_prog("std.transform.flat");
    auto vol_prog  = make_transform_prog("std.transform.volume");

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(flat_prog, "std.transform.flat", build_dir));
    REQUIRE(artifact.save(vol_prog, "std.transform.volume", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "std.transform.flat.cmod", build_dir / "std.transform.volume.cmod"});

    CHECK_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());

    fs::remove_all(build_dir, ec);
}

// ── Task 4.6: Canonical identity round-trip and conflict detection ────────────

TEST_CASE("program_linker: duplicate canonical identity from distinct artifacts is rejected", "[linker][4.6]") {
    auto build_dir = linker_build_dir() / "canonical_conflict";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Two programs whose traits share the same pre-set canonical_id (simulating
    // two artifacts both claiming to define the same canonical symbol).
    DecoratedProgram prog_a;
    ResolvedTrait ta;
    ta.name                   = "Position";
    ta.is_pub                 = true;
    ta.module_name            = "shared";
    ta.canonical_id           = "shared.Position";
    prog_a.traits["Position"] = ta;

    DecoratedProgram prog_b;
    ResolvedTrait tb;
    tb.name                   = "Position";
    tb.is_pub                 = true;
    tb.module_name            = "shared";
    tb.canonical_id           = "shared.Position";  // same canonical_id as prog_a
    prog_b.traits["Position"] = tb;

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    // Save as distinct artifact files so they appear to be different modules.
    artifact.save(prog_a, "copy_one", build_dir);
    artifact.save(prog_b, "copy_two", build_dir);
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "copy_one.cmod", build_dir / "copy_two.cmod"});

    CHECK_FALSE(merged.has_value());
    CHECK(link_errors.has_errors());
    const auto& msg = link_errors.diagnostics()[0].message;
    CHECK(msg.contains("duplicate canonical symbol"));
    CHECK(msg.contains("shared.Position"));

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: rebuilds cross-module conflicts and event flow", "[linker][runtime-graph][4.4]") {
    const auto tick     = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const auto spawned  = linked_symbol(SymbolKind::Event, "runtime", "Spawned");
    const auto position = linked_symbol(SymbolKind::Trait, "runtime", "Position");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};
    const ResolvedHandlerTrigger spawned_trigger{.kind = HandlerTriggerKind::Event, .symbol = spawned};

    DecoratedProgram producer_program;
    auto producer = linked_handler(linked_symbol(SymbolKind::Rule, "producer", "Move"), tick_trigger, 4);
    producer.contract.reads.insert(position);
    producer.contract.writes.insert(position);
    producer.contract.emits.insert(spawned);
    producer_program.execution_graph.handlers.push_back(producer);

    DecoratedProgram consumer_program;
    auto reader = linked_handler(linked_symbol(SymbolKind::Rule, "consumer", "Observe"), tick_trigger, 1);
    reader.contract.reads.insert(position);
    consumer_program.execution_graph.handlers.push_back(reader);
    auto consumer = linked_handler(linked_symbol(SymbolKind::Rule, "consumer", "Consume"), spawned_trigger, 2);
    consumer_program.execution_graph.handlers.push_back(consumer);

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, producer_program, "producer"));
    REQUIRE(linker.merge_into(merged, consumer_program, "consumer"));
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(merged.execution_graph.handlers.size() == 3);
    CHECK(merged.execution_graph.handlers[0].declaration_order.module_index == 0);
    CHECK(merged.execution_graph.handlers[1].declaration_order.module_index == 1);

    const auto conflict = std::ranges::find_if(merged.execution_graph.schedule_edges, [&](const auto& edge) {
        return edge.kind == ScheduleEdgeKind::DataConflict && edge.before == producer.identity &&
               edge.after == reader.identity;
    });
    REQUIRE(conflict != merged.execution_graph.schedule_edges.end());
    CHECK(conflict->orientation == ScheduleEdgeOrientation::WriterBeforeReader);
    CHECK(conflict->trait_provenance == std::vector<SymbolId>{position});
    REQUIRE(merged.execution_graph.event_flows.size() == 1);
    CHECK(merged.execution_graph.event_flows[0].producer == producer.identity);
    CHECK(merged.execution_graph.event_flows[0].consumer == consumer.identity);
    CHECK(merged.execution_graph.stable_topological_order[0] == producer.identity);
    CHECK(merged.execution_graph.stable_topological_order[1] == reader.identity);
}

TEST_CASE("program_linker: a cycle across modules is delayed after linking", "[linker][delivery-cycles]") {
    const auto ping = linked_symbol(SymbolKind::Event, "a", "Ping");
    const auto pong = linked_symbol(SymbolKind::Event, "b", "Pong");

    DecoratedProgram a_program;
    auto a_handler = linked_handler(linked_symbol(SymbolKind::Rule, "a", "Serve"),
                                    {.kind = HandlerTriggerKind::Event, .symbol = pong},
                                    0);
    a_handler.contract.emits.insert(ping);
    a_program.execution_graph.handlers.push_back(a_handler);

    DecoratedProgram b_program;
    auto b_handler = linked_handler(linked_symbol(SymbolKind::Rule, "b", "Return"),
                                    {.kind = HandlerTriggerKind::Event, .symbol = ping},
                                    0);
    b_handler.contract.emits.insert(pong);
    b_program.execution_graph.handlers.push_back(b_handler);

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, a_program, "a"));
    CHECK(merged.execution_graph.event_flows.empty());
    REQUIRE(linker.merge_into(merged, b_program, "b"));
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(merged.execution_graph.event_flows.size() == 2);
    CHECK(std::ranges::all_of(merged.execution_graph.event_flows, &EventFlowEdge::delayed));
}

TEST_CASE("program_linker: preserves external project capabilities across module merge",
          "[linker][extern-rule][projects]") {
    const auto tick   = linked_symbol(SymbolKind::Phase, "runtime", "input");
    const auto layout = linked_symbol(SymbolKind::Trait, "std.ui", "ComputedLayout");
    const auto rule   = linked_symbol(SymbolKind::Rule, "std.ui", "MeasureUi");
    const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};

    InferredHandlerContract inferred;
    inferred.rule     = rule;
    inferred.trigger  = trigger;
    inferred.projects = {layout};

    DecoratedProgram module;
    module.handler_contracts.push_back(inferred);
    auto producer                   = linked_handler(rule, trigger, 0);
    producer.implementation         = HandlerImplementationKind::External;
    producer.contract.domain_kind   = HandlerDomainKind::Unary;
    producer.contract.selection     = {layout};
    producer.contract.projects      = {layout};
    module.execution_graph.handlers = {producer};

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, module, "std.ui"));
    REQUIRE_FALSE(errors.has_errors());

    REQUIRE(merged.handler_contracts.size() == 1);
    CHECK(merged.handler_contracts.front().projects == std::unordered_set<SymbolId>{layout});
    CHECK(merged.handler_contracts.front().writes.empty());
    REQUIRE(merged.execution_graph.handlers.size() == 1);
    CHECK(merged.execution_graph.handlers.front().contract.projects == std::unordered_set<SymbolId>{layout});
    CHECK(merged.execution_graph.handlers.front().contract.writes.empty());
}

TEST_CASE("program_linker: preserves a contract's iteration order across module merge", "[linker][rule-order-by]") {
    const auto tick = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const auto rule = linked_symbol(SymbolKind::Rule, "game", "Announce");
    const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};

    InferredHandlerContract inferred;
    inferred.rule            = rule;
    inferred.trigger         = trigger;
    inferred.iteration_order = IterationOrder::Creation;

    DecoratedProgram module;
    module.handler_contracts.push_back(inferred);
    auto handler                     = linked_handler(rule, trigger, 0);
    handler.contract.domain_kind     = HandlerDomainKind::Unary;
    handler.contract.iteration_order = IterationOrder::Creation;
    module.execution_graph.handlers  = {handler};

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, module, "game"));
    REQUIRE_FALSE(errors.has_errors());

    REQUIRE(merged.handler_contracts.size() == 1);
    CHECK(merged.handler_contracts.front().iteration_order == IterationOrder::Creation);
    REQUIRE(merged.execution_graph.handlers.size() == 1);
    CHECK(merged.execution_graph.handlers.front().contract.iteration_order == IterationOrder::Creation);
}


TEST_CASE("program_linker: schedule edge reasons survive the artifact and linking", "[linker][handler-execution-graph]") {
    auto build_dir = linker_build_dir() / "edge_reasons";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    Lexer lexer("module reasons\n"
                "pub extern event tick\n"
                "trait Score:\n"
                "    var value: int\n"
                "trait Transform:\n"
                "    var position: float\n"
                "trait Seen:\n"
                "    var value: float\n"
                "entity Game:\n"
                "    Score\n"
                "entity Watcher:\n"
                "    Transform\n"
                "    Seen\n"
                "rule First:\n"
                "    on tick:\n"
                "        Game[Score].value = 1\n"
                "rule Second:\n"
                "    after:\n"
                "        First\n"
                "    on tick:\n"
                "        Game[Score].value = 2\n"
                "rule Third:\n"
                "    after:\n"
                "        Second\n"
                "    on tick:\n"
                "        Game[Score].value = 3\n"
                "rule Reader:\n"
                "    each:\n"
                "        Transform as t\n"
                "        Seen as s\n"
                "    on tick:\n"
                "        s.value = t.position\n"
                "rule Writer:\n"
                "    each:\n"
                "        Transform as t\n"
                "    on tick:\n"
                "        t.position += 1.0\n",
                "reasons.cactus",
                errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    SemanticAnalyzer analyzer(errors);
    const auto module = analyzer.analyze(ast);
    REQUIRE_FALSE(errors.has_errors());
    ModuleArtifact artifact(errors);
    REQUIRE(artifact.save(module, "reasons", build_dir));

    ProgramLinker linker(errors);
    const auto linked = linker.link({build_dir / "reasons.cmod"});
    INFO((errors.diagnostics().empty() ? "" : errors.diagnostics().front().message));
    REQUIRE(linked.has_value());

    std::set<ScheduleEdgeOrientation> reasons;
    for (const auto& edge : module.execution_graph.schedule_edges) {
        const auto found = std::ranges::find_if(linked->execution_graph.schedule_edges, [&](const ScheduleEdge& candidate) {
            return candidate.before == edge.before && candidate.after == edge.after && candidate.kind == edge.kind;
        });
        REQUIRE(found != linked->execution_graph.schedule_edges.end());
        CHECK(found->orientation == edge.orientation);
        reasons.insert(edge.orientation);
    }
    CHECK(reasons == std::set<ScheduleEdgeOrientation>{ScheduleEdgeOrientation::Explicit,
                                                       ScheduleEdgeOrientation::WriterBeforeReader});

    fs::remove_all(build_dir, ec);
}
// ── Pair relations (dsl-pair-relations, 4.4) ─────────────────────────────────

TEST_CASE("program_linker: preserves pair contract domain, bindings, bound reads, and projects across merge",
          "[linker][pair-relations][4.4]") {
    const auto tick           = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const auto dynamic_body   = linked_symbol(SymbolKind::Trait, "producer", "DynamicBody");
    const auto solid          = linked_symbol(SymbolKind::Trait, "producer", "Solid");
    const auto ground_contact = linked_symbol(SymbolKind::Trait, "producer", "GroundContact");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};

    DecoratedProgram producer_program;
    auto detector = linked_handler(linked_symbol(SymbolKind::Rule, "producer", "DetectContacts"), tick_trigger, 0);
    detector.contract.domain_kind   = HandlerDomainKind::Pair;
    detector.contract.pair_bindings = {
        RelationBinding{.name = "body", .required_traits = {dynamic_body}},
        RelationBinding{.name = "wall", .required_traits = {solid}},
    };
    detector.contract.reads       = {dynamic_body, solid};
    detector.contract.bound_reads = {
        BoundTraitAccess{.binding_index = 0, .trait = dynamic_body},
        BoundTraitAccess{.binding_index = 1, .trait = solid},
    };
    detector.contract.projects = {ground_contact};
    producer_program.execution_graph.handlers.push_back(detector);

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, producer_program, "producer"));
    REQUIRE_FALSE(errors.has_errors());

    REQUIRE(merged.execution_graph.handlers.size() == 1);
    const auto& node = merged.execution_graph.handlers[0];
    CHECK(node.contract.domain_kind == HandlerDomainKind::Pair);
    CHECK_FALSE(node.contract.is_selectionless());
    REQUIRE(node.contract.pair_bindings.size() == 2);
    CHECK(node.contract.pair_bindings[0].name == "body");
    CHECK(node.contract.pair_bindings[0].required_traits == std::vector<SymbolId>{dynamic_body});
    CHECK(node.contract.pair_bindings[1].name == "wall");
    CHECK(node.contract.pair_bindings[1].required_traits == std::vector<SymbolId>{solid});
    CHECK(node.contract.bound_reads.size() == 2);
    CHECK(node.contract.projects.contains(ground_contact));
    CHECK_FALSE(node.contract.writes.contains(ground_contact));
}

TEST_CASE("program_linker: rebuilds a cross-module conflict from a pair handler's projection",
          "[linker][pair-relations][4.4]") {
    const auto tick           = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const auto ground_contact = linked_symbol(SymbolKind::Trait, "producer", "GroundContact");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};

    DecoratedProgram producer_program;
    auto detector = linked_handler(linked_symbol(SymbolKind::Rule, "producer", "DetectContacts"), tick_trigger, 0);
    detector.contract.domain_kind = HandlerDomainKind::Pair;
    detector.contract.projects    = {ground_contact};
    producer_program.execution_graph.handlers.push_back(detector);

    DecoratedProgram consumer_program;
    auto reader = linked_handler(linked_symbol(SymbolKind::Rule, "consumer", "ReadGroundContact"), tick_trigger, 1);
    reader.contract.reads.insert(ground_contact);
    consumer_program.execution_graph.handlers.push_back(reader);

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, producer_program, "producer"));
    REQUIRE(linker.merge_into(merged, consumer_program, "consumer"));
    REQUIRE_FALSE(errors.has_errors());

    const auto conflict = std::ranges::find_if(merged.execution_graph.schedule_edges, [&](const auto& edge) {
        return edge.kind == ScheduleEdgeKind::DataConflict && edge.before == detector.identity &&
               edge.after == reader.identity;
    });
    REQUIRE(conflict != merged.execution_graph.schedule_edges.end());
    CHECK(conflict->orientation == ScheduleEdgeOrientation::WriterBeforeReader);
    CHECK(conflict->trait_provenance == std::vector<SymbolId>{ground_contact});
}

TEST_CASE("program_linker: artifact link preserves an imported pair trait identity without UseNode lookup",
          "[linker][pair-relations][artifact][4.4]") {
    auto build_dir = linker_build_dir() / "pair_import";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    const auto tick         = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const auto solid        = linked_symbol(SymbolKind::Trait, "phys", "Solid");
    const auto dynamic_body = linked_symbol(SymbolKind::Trait, "game", "DynamicBody");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};

    DecoratedProgram phys_program;
    ResolvedTrait solid_trait;
    solid_trait.name             = "Solid";
    solid_trait.is_pub           = true;
    solid_trait.module_name      = "phys";
    solid_trait.canonical_id     = make_canonical_id(solid);
    phys_program.traits["Solid"] = solid_trait;

    DecoratedProgram game_program;
    auto detector = linked_handler(linked_symbol(SymbolKind::Rule, "game", "DetectContacts"), tick_trigger, 0);
    detector.contract.domain_kind   = HandlerDomainKind::Pair;
    detector.contract.pair_bindings = {
        RelationBinding{.name = "body", .required_traits = {dynamic_body}},
        // "wall" selects Solid through an imported alias — already resolved to
        // its canonical identity by semantic analysis, same as any other
        // aliased import; the artifact never stores the source alias.
        RelationBinding{.name = "wall", .required_traits = {solid}},
    };
    detector.contract.bound_reads = {BoundTraitAccess{.binding_index = 1, .trait = solid}};
    detector.contract.reads       = {solid};
    game_program.execution_graph.handlers.push_back(detector);
    for (const auto& [name, trait] : {std::pair{"Ball", dynamic_body}, std::pair{"Wall", solid}}) {
        game_program.persistence.archetypes.push_back(PersistenceArchetypeDescriptor{
            .node            = ArchetypeNodeId{.archetype = linked_symbol(SymbolKind::Entity, "game", name)},
            .baseline_traits = {PersistenceBaselineTrait{.trait = trait}}});
    }

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(phys_program, "phys", build_dir));
    REQUIRE(artifact.save(game_program, "game", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto linked = linker.link({build_dir / "phys.cmod", build_dir / "game.cmod"});
    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(linked.has_value());

    REQUIRE(linked->execution_graph.handlers.size() == 1);
    const auto& node = linked->execution_graph.handlers[0];
    REQUIRE(node.contract.pair_bindings.size() == 2);
    CHECK(node.contract.pair_bindings[1].name == "wall");
    CHECK(node.contract.pair_bindings[1].required_traits == std::vector<SymbolId>{solid});
    REQUIRE(node.contract.bound_reads.size() == 1);
    CHECK(node.contract.bound_reads[0].trait == solid);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: artifact link preserves explicit cross-module edge and graph levels",
          "[linker][runtime-graph][artifact][4.4]") {
    auto build_dir = linker_build_dir() / "runtime_graph";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    const auto frame = linked_symbol(SymbolKind::Event, "runtime", "frame");
    const auto tick  = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};
    const auto first = linked_handler(linked_symbol(SymbolKind::Rule, "base", "First"), tick_trigger, 3);
    auto second      = linked_handler(linked_symbol(SymbolKind::Rule, "app", "Second"), tick_trigger, 1);
    second.explicit_after.push_back(first.identity);

    DecoratedProgram base;
    base.execution_graph.handlers.push_back(first);
    base.execution_graph.phases.push_back(
        PhasePlan{.phase               = tick,
                  .source_dependencies = {{.kind = HandlerTriggerKind::Event, .symbol = frame}},
                  .runtime_root        = frame,
                  .declaration_order   = {.declaration_index = 2}});

    DecoratedProgram app;
    app.execution_graph.handlers.push_back(second);
    app.execution_graph.schedule_edges.push_back(ScheduleEdge{.before      = first.identity,
                                                              .after       = second.identity,
                                                              .kind        = ScheduleEdgeKind::ExplicitHandler,
                                                              .orientation = ScheduleEdgeOrientation::Explicit});

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(base, "base", build_dir));
    REQUIRE(artifact.save(app, "app", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto linked = linker.link({build_dir / "base.cmod", build_dir / "app.cmod"});
    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(linked.has_value());
    REQUIRE(linked->execution_graph.phases.size() == 1);
    CHECK(linked->execution_graph.phases[0].runtime_root == frame);
    REQUIRE(linked->execution_graph.schedule_edges.size() == 1);
    CHECK(linked->execution_graph.schedule_edges[0].kind == ScheduleEdgeKind::ExplicitHandler);
    CHECK(linked->execution_graph.stable_topological_order ==
          std::vector<HandlerIdentity>{first.identity, second.identity});
    REQUIRE(linked->execution_graph.dependency_levels.size() == 2);
    CHECK(linked->execution_graph.dependency_levels[0].handlers == std::vector<HandlerIdentity>{first.identity});
    CHECK(linked->execution_graph.dependency_levels[1].handlers == std::vector<HandlerIdentity>{second.identity});

    fs::remove_all(build_dir, ec);
}

// ── add-world-save-restore: persistence requires the graph-driven scheduler ─

TEST_CASE("program_linker: std.persistence with no declared phases is rejected at link time", "[linker][persistence]") {
    auto build_dir = linker_build_dir() / "persistence_legacy";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    DecoratedProgram program;
    ResolvedEvent save_requested;
    save_requested.name             = "SaveRequested";
    save_requested.module_name      = "std.persistence";
    save_requested.symbol_id        = linked_symbol(SymbolKind::Event, "std.persistence", "SaveRequested");
    save_requested.is_pub           = true;
    program.events["SaveRequested"] = save_requested;
    // No phases pushed to program.execution_graph: this is the legacy
    // update_project/render_project shape, not graph-driven.

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(program, "legacy_app", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto linked = linker.link({build_dir / "legacy_app.cmod"});

    CHECK_FALSE(linked.has_value());
    REQUIRE(link_errors.has_errors());
    const bool found_diagnostic = std::ranges::any_of(link_errors.diagnostics(), [](const Diagnostic& diagnostic) {
        return diagnostic.message.contains("std.persistence") && diagnostic.message.contains("phase");
    });
    CHECK(found_diagnostic);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: set with no declared phases is rejected at link time", "[linker][deferred-set]") {
    auto build_dir = linker_build_dir() / "set_legacy";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    DecoratedProgram program;
    HandlerNode handler;
    handler.identity = HandlerIdentity{
        .rule    = linked_symbol(SymbolKind::Rule, "legacy_set", "Heal"),
        .trigger = ResolvedHandlerTrigger{.kind   = HandlerTriggerKind::Event,
                                          .symbol = linked_symbol(SymbolKind::Event, "legacy_set", "pulse")}};
    handler.contract.commands.push_back(InferredHandlerCommand{
        .kind = HandlerCommandKind::Set, .target = linked_symbol(SymbolKind::Trait, "legacy_set", "Health")});
    program.execution_graph.handlers.push_back(handler);

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(program, "legacy_set", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto linked = linker.link({build_dir / "legacy_set.cmod"});

    CHECK_FALSE(linked.has_value());
    const bool found_diagnostic = std::ranges::any_of(link_errors.diagnostics(), [](const Diagnostic& diagnostic) {
        return diagnostic.message.contains("`set` requires the graph-driven phase scheduler");
    });
    CHECK(found_diagnostic);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: std.persistence with declared phases links without error", "[linker][persistence]") {
    auto build_dir = linker_build_dir() / "persistence_graph_driven";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    const auto frame = linked_symbol(SymbolKind::Event, "std.core", "frame");
    const auto tick  = linked_symbol(SymbolKind::Phase, "std.core", "tick");

    DecoratedProgram program;
    ResolvedEvent save_requested;
    save_requested.name             = "SaveRequested";
    save_requested.module_name      = "std.persistence";
    save_requested.symbol_id        = linked_symbol(SymbolKind::Event, "std.persistence", "SaveRequested");
    save_requested.is_pub           = true;
    program.events["SaveRequested"] = save_requested;
    program.execution_graph.phases.push_back(
        PhasePlan{.phase = tick, .runtime_root = frame, .declaration_order = {.declaration_index = 0}});

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(program, "graph_app", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto linked = linker.link({build_dir / "graph_app.cmod"});

    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(linked.has_value());

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: a program using only RestoreRequested is also gated on the graph-driven scheduler",
          "[linker][persistence]") {
    // The graph-scheduler gate is keyed on the std.persistence module, not on
    // SaveRequested by name — this confirms restore shares it rather than
    // needing its own duplicate check.
    auto build_dir = linker_build_dir() / "persistence_restore_legacy";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    DecoratedProgram program;
    ResolvedEvent restore_requested;
    restore_requested.name             = "RestoreRequested";
    restore_requested.module_name      = "std.persistence";
    restore_requested.symbol_id        = linked_symbol(SymbolKind::Event, "std.persistence", "RestoreRequested");
    restore_requested.is_pub           = true;
    program.events["RestoreRequested"] = restore_requested;

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(program, "restore_legacy_app", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto linked = linker.link({build_dir / "restore_legacy_app.cmod"});

    CHECK_FALSE(linked.has_value());
    REQUIRE(link_errors.has_errors());
    const bool found_diagnostic = std::ranges::any_of(link_errors.diagnostics(), [](const Diagnostic& diagnostic) {
        return diagnostic.message.contains("std.persistence") && diagnostic.message.contains("phase");
    });
    CHECK(found_diagnostic);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: linked handler cycles are rejected canonically", "[linker][runtime-graph][4.4]") {
    const auto tick = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};
    auto first  = linked_handler(linked_symbol(SymbolKind::Rule, "left", "First"), trigger, 0);
    auto second = linked_handler(linked_symbol(SymbolKind::Rule, "right", "Second"), trigger, 0);
    first.explicit_after.push_back(second.identity);
    second.explicit_after.push_back(first.identity);

    DecoratedProgram left;
    left.execution_graph.handlers.push_back(first);
    left.execution_graph.schedule_edges.push_back(ScheduleEdge{.before      = second.identity,
                                                               .after       = first.identity,
                                                               .kind        = ScheduleEdgeKind::ExplicitHandler,
                                                               .orientation = ScheduleEdgeOrientation::Explicit});
    DecoratedProgram right;
    right.execution_graph.handlers.push_back(second);
    right.execution_graph.schedule_edges.push_back(ScheduleEdge{.before      = first.identity,
                                                                .after       = second.identity,
                                                                .kind        = ScheduleEdgeKind::ExplicitHandler,
                                                                .orientation = ScheduleEdgeOrientation::Explicit});

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, left, "left"));
    CHECK_FALSE(linker.merge_into(merged, right, "right"));
    REQUIRE(errors.has_errors());
    CHECK(errors.diagnostics().back().message.contains("handler cycle:"));
    CHECK(errors.diagnostics().back().message.contains(first.identity.canonical_id()));
    CHECK(errors.diagnostics().back().message.contains(second.identity.canonical_id()));
}

// ── add-rule-groups: group ordering across separately compiled modules ──────

TEST_CASE("program_linker: separately compiled modules keep rule group order", "[linker][rule-groups][artifact]") {
    auto build_dir = linker_build_dir() / "rule_groups";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    std::vector<std::unique_ptr<ProgramNode>> asts;
    ModuleImports imports;
    const auto compile = [&](const std::string& module, const std::string& source) {
        ErrorReporter errors;
        Lexer lexer(source, module + ".cactus", errors);
        Parser parser(lexer.tokenize(), errors);
        asts.push_back(std::make_unique<ProgramNode>(parser.parse_program()));
        SemanticAnalyzer analyzer(errors);
        const auto program = analyzer.analyze(*asts.back(), imports);
        INFO((errors.diagnostics().empty() ? "" : errors.diagnostics().front().message));
        REQUIRE_FALSE(errors.has_errors());
        ModuleArtifact artifact(errors);
        REQUIRE(artifact.save(program, module, build_dir));
        return build_dir / (module + ".cmod");
    };
    const auto import_artifact = [&](const std::string& qualifier, const fs::path& path) {
        ErrorReporter errors;
        ModuleArtifact artifact(errors);
        auto symbols = artifact.extract_pub_symbols(path);
        REQUIRE(symbols.has_value());
        imports.add(qualifier, std::move(*symbols));
    };

    const auto core = compile("std.core",
                              "module std.core\n"
                              "extern event frame:\n"
                              "    dt: float\n"
                              "pub phase fixed_tick:\n"
                              "    from:\n"
                              "        frame\n");
    import_artifact("std.core", core);
    const auto physics = compile("std.physics.volume",
                                 "module std.physics.volume\n"
                                 "pub group solve:\n"
                                 "    phase: fixed_tick\n"
                                 "rule MoveAndSlide:\n"
                                 "    group: solve\n"
                                 "    on fixed_tick:\n"
                                 "        let value = 1\n");
    import_artifact("phys", physics);
    const auto game = compile("game",
                              "module game\n"
                              "rule ReactToGround:\n"
                              "    after:\n"
                              "        phys.solve\n"
                              "    on fixed_tick:\n"
                              "        let value = 2\n"
                              "rule MovePlayer:\n"
                              "    before:\n"
                              "        phys.solve\n"
                              "    on fixed_tick:\n"
                              "        let value = 3\n"
                              "rule Early:\n"
                              "    before:\n"
                              "        phys.MoveAndSlide\n"
                              "    on fixed_tick:\n"
                              "        let value = 4\n");

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto linked = linker.link({core, physics, game});
    INFO((link_errors.diagnostics().empty() ? "" : link_errors.diagnostics().front().message));
    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(linked.has_value());
    const auto& graph = linked->execution_graph;

    const ResolvedHandlerTrigger fixed_tick{.kind   = HandlerTriggerKind::Phase,
                                            .symbol = linked_symbol(SymbolKind::Phase, "std.core", "fixed_tick")};
    const auto handler = [&](const std::string& module, const std::string& rule) {
        return HandlerIdentity{.rule = linked_symbol(SymbolKind::Rule, module, rule), .trigger = fixed_tick};
    };
    const auto member      = handler("std.physics.volume", "MoveAndSlide");
    const auto move_player = handler("game", "MovePlayer");
    const auto react       = handler("game", "ReactToGround");
    const auto early       = handler("game", "Early");
    const auto has_edge    = [&](const HandlerIdentity& before, const HandlerIdentity& after, ScheduleEdgeKind kind) {
        return std::ranges::any_of(graph.schedule_edges, [&](const auto& edge) {
            return edge.before == before && edge.after == after && edge.kind == kind;
        });
    };
    CHECK(has_edge(move_player, member, ScheduleEdgeKind::ExplicitGroup));
    CHECK(has_edge(member, react, ScheduleEdgeKind::ExplicitGroup));
    CHECK(has_edge(early, member, ScheduleEdgeKind::ExplicitRule));
    CHECK(std::ranges::count_if(graph.schedule_edges,
                                [](const auto& edge) { return edge.kind == ScheduleEdgeKind::ExplicitGroup; }) == 2);

    const auto position = [&](const HandlerIdentity& identity) {
        const auto found = std::ranges::find(graph.stable_topological_order, identity);
        REQUIRE(found != graph.stable_topological_order.end());
        return found - graph.stable_topological_order.begin();
    };
    CHECK(position(move_player) < position(member));
    CHECK(position(early) < position(member));
    CHECK(position(member) < position(react));

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: an importing module's handler runs before another module's public group",
          "[linker][rule-groups][artifact]") {
    auto build_dir = linker_build_dir() / "group_tie_break";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    std::vector<std::unique_ptr<ProgramNode>> asts;
    std::map<std::string, fs::path> artifacts;
    const auto compile = [&](const std::string& module,
                             const std::vector<std::pair<std::string, std::string>>& uses,
                             const std::string& source) {
        ModuleImports imports;
        for (const auto& [qualifier, used] : uses) {
            ErrorReporter errors;
            ModuleArtifact artifact(errors);
            auto symbols = artifact.extract_pub_symbols(artifacts.at(used));
            REQUIRE(symbols.has_value());
            imports.add(qualifier, std::move(*symbols));
        }
        ErrorReporter errors;
        Lexer lexer(source, module + ".cactus", errors);
        Parser parser(lexer.tokenize(), errors);
        asts.push_back(std::make_unique<ProgramNode>(parser.parse_program()));
        SemanticAnalyzer analyzer(errors);
        const auto program = analyzer.analyze(*asts.back(), imports);
        INFO((errors.diagnostics().empty() ? "" : errors.diagnostics().front().message));
        REQUIRE_FALSE(errors.has_errors());
        ModuleArtifact artifact(errors);
        REQUIRE(artifact.save(program, module, build_dir));
        artifacts[module] = build_dir / (module + ".cmod");
    };

    compile("std.core",
            {},
            "module std.core\n"
            "extern event frame:\n"
            "    dt: float\n"
            "pub phase fixed_tick:\n"
            "    from:\n"
            "        frame\n"
            "pub trait Clock:\n"
            "    var t: float = 0.0\n"
            "pub trait Score:\n"
            "    var points: int = 0\n");
    compile("lib",
            {{"std.core", "std.core"}},
            "module lib\n"
            "use std.core\n"
            "pub trait Body:\n"
            "    var velocity: float = 0.0\n"
            "    var grounded: bool = false\n"
            "pub trait Spot:\n"
            "    var x: float = 0.0\n"
            "pub group solve:\n"
            "    phase: fixed_tick\n"
            "rule Helper:\n"
            "    each:\n"
            "        Body as body\n"
            "    before:\n"
            "        solve\n"
            "    on fixed_tick:\n"
            "        body.grounded = false\n"
            "rule Step:\n"
            "    each:\n"
            "        Body as body\n"
            "        Clock as clock\n"
            "        Score as score\n"
            "    group: solve\n"
            "    on fixed_tick:\n"
            "        body.velocity = body.velocity * 0.5\n"
            "        body.grounded = true\n"
            "        clock.t += 1.0\n"
            "        score.points += 1\n"
            "rule Place:\n"
            "    each:\n"
            "        Spot as spot\n"
            "    group: solve\n"
            "    after:\n"
            "        Step\n"
            "    on fixed_tick:\n"
            "        spot.x += 1.0\n");
    compile("loner",
            {{"std.core", "std.core"}},
            "module loner\n"
            "use std.core\n"
            "rule Tick:\n"
            "    each:\n"
            "        Score as score\n"
            "    on fixed_tick:\n"
            "        score.points += 2\n");
    compile("mid",
            {{"std.core", "std.core"}, {"lib", "lib"}},
            "module mid\n"
            "use std.core\n"
            "use lib\n"
            "pub trait Marker\n");
    compile("game",
            {{"std.core", "std.core"}, {"lib", "lib"}, {"mid", "mid"}},
            "module game\n"
            "use std.core\n"
            "use lib\n"
            "use mid\n"
            "entity Rig:\n"
            "    lib.Body\n"
            "    lib.Spot\n"
            "    Clock\n"
            "    Score\n"
            "    mid.Marker\n"
            "rule Push:\n"
            "    each:\n"
            "        lib.Body as body\n"
            "    on fixed_tick:\n"
            "        body.velocity = 3.0\n"
            "rule Aim:\n"
            "    each:\n"
            "        lib.Body as body\n"
            "        lib.Spot as spot\n"
            "    after:\n"
            "        Push\n"
            "    on fixed_tick:\n"
            "        body.velocity = spot.x\n"
            "rule Late:\n"
            "    each:\n"
            "        lib.Body as body\n"
            "    after:\n"
            "        lib.solve\n"
            "        Aim\n"
            "    on fixed_tick:\n"
            "        body.velocity = 4.0\n"
            "rule Look:\n"
            "    each:\n"
            "        lib.Body as body\n"
            "        mid.Marker\n"
            "    on fixed_tick:\n"
            "        let seen = body.grounded\n");
    compile("far",
            {{"std.core", "std.core"}, {"mid", "mid"}},
            "module far\n"
            "use std.core\n"
            "use mid\n"
            "rule Wind:\n"
            "    each:\n"
            "        Clock as clock\n"
            "        mid.Marker\n"
            "    on fixed_tick:\n"
            "        clock.t = 3.0\n");

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto linked = linker.link({artifacts.at("std.core"),
                               artifacts.at("lib"),
                               artifacts.at("loner"),
                               artifacts.at("mid"),
                               artifacts.at("game"),
                               artifacts.at("far")});
    INFO((link_errors.diagnostics().empty() ? "" : link_errors.diagnostics().front().message));
    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(linked.has_value());
    const auto& graph = linked->execution_graph;

    const ResolvedHandlerTrigger fixed_tick{.kind   = HandlerTriggerKind::Phase,
                                            .symbol = linked_symbol(SymbolKind::Phase, "std.core", "fixed_tick")};
    const auto handler = [&](const std::string& module, const std::string& rule) {
        return HandlerIdentity{.rule = linked_symbol(SymbolKind::Rule, module, rule), .trigger = fixed_tick};
    };
    const auto position = [&](const HandlerIdentity& identity) {
        const auto found = std::ranges::find(graph.stable_topological_order, identity);
        REQUIRE(found != graph.stable_topological_order.end());
        return found - graph.stable_topological_order.begin();
    };
    const auto oriented = [&](const HandlerIdentity& before, const HandlerIdentity& after) {
        const auto found = std::ranges::find_if(graph.schedule_edges, [&](const ScheduleEdge& edge) {
            return edge.before == before && edge.after == after && edge.kind == ScheduleEdgeKind::DataConflict;
        });
        REQUIRE(found != graph.schedule_edges.end());
        return found->orientation;
    };
    const auto step = handler("lib", "Step");

    // Reciprocal conflict with an imported module's public group: the importer runs first.
    CHECK(position(handler("game", "Push")) < position(step));
    CHECK(oriented(handler("game", "Push"), step) == ScheduleEdgeOrientation::ImporterBeforePublicGroup);
    // The whole group decides: Aim only reads what Place writes, but it also writes what Step reads.
    CHECK(position(handler("game", "Aim")) < position(step));
    CHECK(position(handler("game", "Aim")) < position(handler("lib", "Place")));
    CHECK(oriented(handler("game", "Aim"), handler("lib", "Place")) ==
          ScheduleEdgeOrientation::ImporterBeforePublicGroup);
    // A transitive import counts too.
    CHECK(position(handler("far", "Wind")) < position(step));
    // Explicit order wins over the tie-break.
    CHECK(position(step) < position(handler("game", "Late")));
    // A one-way reader still follows the writer.
    CHECK(position(step) < position(handler("game", "Look")));
    CHECK(oriented(step, handler("game", "Look")) == ScheduleEdgeOrientation::WriterBeforeReader);
    // Accumulations from a module that doesn't import the group's module commute.
    CHECK(std::ranges::none_of(graph.schedule_edges, [&](const ScheduleEdge& edge) {
        return (edge.before == step && edge.after == handler("loner", "Tick")) ||
               (edge.before == handler("loner", "Tick") && edge.after == step);
    }));
    // Rules of the group's own module state their order.
    CHECK(oriented(handler("lib", "Helper"), step) == ScheduleEdgeOrientation::Explicit);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: group edges from artifacts are regenerated, not duplicated", "[linker][rule-groups]") {
    const auto tick = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};
    const auto solve = linked_symbol(SymbolKind::Group, "lib", "solve");
    auto member      = linked_handler(linked_symbol(SymbolKind::Rule, "lib", "Member"), trigger, 0);
    member.group     = solve;
    auto early       = linked_handler(linked_symbol(SymbolKind::Rule, "app", "Early"), trigger, 0);

    DecoratedProgram lib;
    lib.execution_graph.handlers.push_back(member);
    lib.execution_graph.group_declarations.push_back(
        GroupDeclaration{.group = solve, .phase = tick, .is_pub = true, .location = {}});
    DecoratedProgram app;
    app.execution_graph.handlers.push_back(early);
    app.execution_graph.group_orderings.push_back(GroupOrdering{
        .handler = early.identity, .group = solve, .direction = GroupOrderingDirection::Before, .location = {}});
    // The module's own expansion; the linker must not keep it next to its regenerated copy.
    app.execution_graph.schedule_edges.push_back(ScheduleEdge{.before      = early.identity,
                                                              .after       = member.identity,
                                                              .kind        = ScheduleEdgeKind::ExplicitGroup,
                                                              .orientation = ScheduleEdgeOrientation::Explicit});

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, lib, "lib"));
    REQUIRE(linker.merge_into(merged, app, "app"));
    REQUIRE_FALSE(errors.has_errors());
    CHECK(merged.execution_graph.group_declarations.size() == 1);
    CHECK(merged.execution_graph.group_orderings.size() == 1);
    REQUIRE(std::ranges::count_if(merged.execution_graph.schedule_edges,
                                  [](const auto& edge) { return edge.kind == ScheduleEdgeKind::ExplicitGroup; }) == 1);
    CHECK(merged.execution_graph.stable_topological_order ==
          std::vector<HandlerIdentity>{early.identity, member.identity});
}

TEST_CASE("program_linker: a group's after: orders members from every module", "[linker][rule-groups]") {
    const auto tick = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};
    const auto solve    = linked_symbol(SymbolKind::Group, "lib", "solve");
    const auto contacts = linked_symbol(SymbolKind::Group, "lib", "contacts");
    auto mover          = linked_handler(linked_symbol(SymbolKind::Rule, "lib", "Mover"), trigger, 0);
    mover.group         = solve;
    auto toucher        = linked_handler(linked_symbol(SymbolKind::Rule, "app", "Toucher"), trigger, 0);
    toucher.group       = contacts;

    DecoratedProgram lib;
    lib.execution_graph.handlers.push_back(mover);
    lib.execution_graph.group_declarations.push_back(
        GroupDeclaration{.group = solve, .phase = tick, .is_pub = true, .location = {}});
    lib.execution_graph.group_declarations.push_back(
        GroupDeclaration{.group = contacts, .phase = tick, .is_pub = true, .location = {}, .after_groups = {solve}});
    DecoratedProgram app;
    app.execution_graph.handlers.push_back(toucher);

    ErrorReporter errors;
    ProgramLinker linker(errors);
    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, app, "app"));
    REQUIRE(linker.merge_into(merged, lib, "lib"));
    REQUIRE_FALSE(errors.has_errors());
    CHECK(merged.execution_graph.stable_topological_order ==
          std::vector<HandlerIdentity>{mover.identity, toucher.identity});
}

// ── Unorientable conflicts across modules ─────────────────────────────────────

namespace {

// Compiles modules in order into artifacts; each `uses` entry names an earlier module.
class ModuleSet {
public:
    explicit ModuleSet(fs::path build_dir) : build_dir_(std::move(build_dir)) {
        std::error_code ec;
        fs::remove_all(build_dir_, ec);
    }

    void compile(const std::string& module, const std::vector<std::string>& uses, const std::string& source) {
        ModuleImports imports;
        for (const auto& used : uses) {
            ErrorReporter errors;
            ModuleArtifact artifact(errors);
            auto symbols = artifact.extract_pub_symbols(artifacts_.at(used));
            REQUIRE(symbols.has_value());
            imports.add(used, std::move(*symbols));
        }
        ErrorReporter errors;
        Lexer lexer(source, module + ".cactus", errors);
        Parser parser(lexer.tokenize(), errors);
        asts_.push_back(std::make_unique<ProgramNode>(parser.parse_program()));
        SemanticAnalyzer analyzer(errors);
        const auto program = analyzer.analyze(*asts_.back(), imports);
        INFO((errors.diagnostics().empty() ? "" : errors.diagnostics().front().message));
        REQUIRE_FALSE(errors.has_errors());
        ModuleArtifact artifact(errors);
        REQUIRE(artifact.save(program, module, build_dir_));
        artifacts_[module] = build_dir_ / (module + ".cmod");
        order_.push_back(artifacts_[module]);
    }

    std::optional<DecoratedProgram> link(ErrorReporter& errors) const {
        ProgramLinker linker(errors);
        return linker.link(order_);
    }

private:
    fs::path build_dir_;
    std::vector<std::unique_ptr<ProgramNode>> asts_;
    std::map<std::string, fs::path> artifacts_;
    std::vector<fs::path> order_;
};

const std::string& core_source() {
    static const std::string value = "module std.core\n"
                                     "extern event frame:\n"
                                     "    dt: float\n"
                                     "pub phase tick:\n"
                                     "    from:\n"
                                     "        frame\n"
                                     "pub trait Score:\n"
                                     "    var value: int = 0\n"
                                     "pub trait Seen:\n"
                                     "    var value: int = 0\n";
    return value;
}

// Every schedule edge as (before rule, after rule, kind), by local rule name.
std::set<std::tuple<std::string, std::string, ScheduleEdgeKind>> edge_directions(const DecoratedProgram& program) {
    std::set<std::tuple<std::string, std::string, ScheduleEdgeKind>> directions;
    for (const auto& edge : program.execution_graph.schedule_edges) {
        directions.emplace(edge.before.rule.local_name, edge.after.rule.local_name, edge.kind);
    }
    return directions;
}

}  // namespace

TEST_CASE("program_linker: link records reachable archetypes across modules", "[linker][archetype-reachability]") {
    ModuleSet modules(linker_build_dir() / "reachability_facts");
    modules.compile("std.core", {}, core_source());
    modules.compile("spawner",
                    {"std.core"},
                    "module spawner\n"
                    "use std.core\n"
                    "pub template Counter:\n"
                    "    Score\n"
                    "rule Unused:\n"
                    "    each:\n"
                    "        Seen\n"
                    "    on tick:\n"
                    "        spawn Counter()\n");
    modules.compile("game",
                    {"std.core", "spawner"},
                    "module game\n"
                    "use std.core\n"
                    "use spawner\n"
                    "entity Board:\n"
                    "    Score\n"
                    "rule Mark:\n"
                    "    each:\n"
                    "        Score\n"
                    "    on tick:\n"
                    "        add Seen\n");

    ErrorReporter errors;
    const auto linked = modules.link(errors);
    REQUIRE(linked.has_value());
    const auto score = make_symbol_id(SymbolKind::Trait, "std.core", "Score");
    const auto seen  = make_symbol_id(SymbolKind::Trait, "std.core", "Seen");
    CHECK(linked->reachability.trait_sets ==
          std::vector<ReachableTraitSet>{{score}, {score, seen}});
    REQUIRE(linked->reachability.origins.size() == 2);
    CHECK(canonical_node_string(linked->reachability.origins[0].node) == "game.Board");
    CHECK(canonical_node_string(linked->reachability.origins[1].node) == "spawner.Counter");
    CHECK(linked->reachability.free_traits.empty());
    CHECK(linked->reachability.unreachable_handlers.empty());

    std::error_code ec;
    fs::remove_all(linker_build_dir() / "reachability_facts", ec);
}

TEST_CASE("program_linker: a cross-module ambiguity is a link error", "[linker][handler-execution-graph][unorientable]") {
    ModuleSet modules(linker_build_dir() / "cross_module_ambiguity");
    modules.compile("std.core", {}, core_source());
    modules.compile("alpha",
                    {"std.core"},
                    "module alpha\n"
                    "use std.core\n"
                    "rule SetScore:\n"
                    "    each:\n"
                    "        Score as s\n"
                    "    on tick:\n"
                    "        s.value = 1\n");
    modules.compile("beta",
                    {"std.core"},
                    "module beta\n"
                    "use std.core\n"
                    "rule ResetScore:\n"
                    "    each:\n"
                    "        Score as s\n"
                    "    on tick:\n"
                    "        s.value = 0\n");

    ErrorReporter errors;
    const auto linked = modules.link(errors);
    REQUIRE(errors.has_errors());
    const auto& message = errors.diagnostics().front().message;
    CHECK(message.contains("unorientable conflict between 'alpha.SetScore/on std.core.tick' and "
                           "'beta.ResetScore/on std.core.tick'"));
    CHECK(message.contains("add 'after: beta.ResetScore/on tick' to the alpha.SetScore/on tick handler"));

    std::error_code ec;
    fs::remove_all(linker_build_dir() / "cross_module_ambiguity", ec);
}

TEST_CASE("program_linker: moving a rule to another module keeps every edge direction",
          "[linker][handler-execution-graph][unorientable]") {
    const std::string rules = "entity Board:\n"
                              "    Score\n"
                              "    Seen\n"
                              "rule Count:\n"
                              "    each:\n"
                              "        Score as s\n"
                              "    on tick:\n"
                              "        s.value += 1\n"
                              "rule Bonus:\n"
                              "    each:\n"
                              "        Score as s\n"
                              "    on tick:\n"
                              "        s.value += 2\n"
                              "rule Watch:\n"
                              "    each:\n"
                              "        Score as s\n"
                              "        Seen as seen\n"
                              "    on tick:\n"
                              "        seen.value = s.value\n";

    ModuleSet together(linker_build_dir() / "rules_together");
    together.compile("std.core", {}, core_source());
    together.compile("game", {"std.core"}, "module game\nuse std.core\n" + rules);
    ErrorReporter together_errors;
    const auto one = together.link(together_errors);
    REQUIRE_FALSE(together_errors.has_errors());
    REQUIRE(one.has_value());

    ModuleSet split(linker_build_dir() / "rules_split");
    split.compile("std.core", {}, core_source());
    split.compile("watcher",
                  {"std.core"},
                  "module watcher\n"
                  "use std.core\n"
                  "rule Watch:\n"
                  "    each:\n"
                  "        Score as s\n"
                  "        Seen as seen\n"
                  "    on tick:\n"
                  "        seen.value = s.value\n");
    split.compile("game",
                  {"std.core"},
                  "module game\n"
                  "use std.core\n"
                  "entity Board:\n"
                  "    Score\n"
                  "    Seen\n"
                  "rule Bonus:\n"
                  "    each:\n"
                  "        Score as s\n"
                  "    on tick:\n"
                  "        s.value += 2\n"
                  "rule Count:\n"
                  "    each:\n"
                  "        Score as s\n"
                  "    on tick:\n"
                  "        s.value += 1\n");
    ErrorReporter split_errors;
    const auto two = split.link(split_errors);
    REQUIRE_FALSE(split_errors.has_errors());
    REQUIRE(two.has_value());

    CHECK_FALSE(edge_directions(*one).empty());
    CHECK(edge_directions(*one) == edge_directions(*two));

    std::error_code ec;
    fs::remove_all(linker_build_dir() / "rules_together", ec);
    fs::remove_all(linker_build_dir() / "rules_split", ec);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
