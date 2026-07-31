// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

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

static HandlerNode linked_handler(const SymbolId& system,
                                  const ResolvedHandlerTrigger& trigger,
                                  std::uint64_t declaration_index) {
    return HandlerNode{.identity          = {.system = system, .trigger = trigger},
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
    SystemDependency dep_a;
    dep_a.system_name = "MoveSystem";
    dep_a.reads.insert("Position");
    prog_a.dependency_graph.push_back(dep_a);

    DecoratedProgram prog_b;
    SystemDependency dep_b;
    dep_b.system_name = "RenderSystem";
    dep_b.reads.insert("Sprite");
    prog_b.dependency_graph.push_back(dep_b);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    linker.merge_into(merged, prog_a, "player");
    linker.merge_into(merged, prog_b, "render");

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.dependency_graph.size() == 2);
    CHECK(merged.dependency_graph[0].system_name == "MoveSystem");
    CHECK(merged.dependency_graph[1].system_name == "RenderSystem");
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

TEST_CASE("program_linker: merging same module twice is idempotent", "[linker][std-core]") {
    auto std_core = make_program("Persistent", true);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, std_core, "std.core"));
    REQUIRE(linker.merge_into(merged, std_core, "std.core"));

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.traits.count("Persistent") == 1);
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
    CHECK(msg.find("duplicate canonical symbol") != std::string::npos);
    CHECK(msg.find("shared.Position") != std::string::npos);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: rebuilds cross-module conflicts and event flow", "[linker][runtime-graph][4.4]") {
    const auto tick     = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const auto spawned  = linked_symbol(SymbolKind::Event, "runtime", "Spawned");
    const auto position = linked_symbol(SymbolKind::Trait, "runtime", "Position");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};
    const ResolvedHandlerTrigger spawned_trigger{.kind = HandlerTriggerKind::Event, .symbol = spawned};

    DecoratedProgram producer_program;
    auto producer = linked_handler(linked_symbol(SymbolKind::System, "producer", "Move"), tick_trigger, 4);
    producer.contract.reads.insert(position);
    producer.contract.writes.insert(position);
    producer.contract.emits.insert(spawned);
    producer_program.execution_graph.handlers.push_back(producer);

    DecoratedProgram consumer_program;
    auto reader = linked_handler(linked_symbol(SymbolKind::System, "consumer", "Observe"), tick_trigger, 1);
    reader.contract.reads.insert(position);
    consumer_program.execution_graph.handlers.push_back(reader);
    auto consumer = linked_handler(linked_symbol(SymbolKind::System, "consumer", "Consume"), spawned_trigger, 2);
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

// ── Pair relations (dsl-pair-relations, 4.4) ─────────────────────────────────

TEST_CASE("program_linker: preserves pair contract domain, bindings, bound reads, and projects across merge",
          "[linker][pair-relations][4.4]") {
    const auto tick           = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const auto dynamic_body   = linked_symbol(SymbolKind::Trait, "producer", "DynamicBody");
    const auto solid          = linked_symbol(SymbolKind::Trait, "producer", "Solid");
    const auto ground_contact = linked_symbol(SymbolKind::Trait, "producer", "GroundContact");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};

    DecoratedProgram producer_program;
    auto detector                   = linked_handler(linked_symbol(SymbolKind::System, "producer", "DetectContacts"),
                                    tick_trigger,
                                    0);
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
    auto detector = linked_handler(linked_symbol(SymbolKind::System, "producer", "DetectContacts"), tick_trigger, 0);
    detector.contract.domain_kind = HandlerDomainKind::Pair;
    detector.contract.projects    = {ground_contact};
    producer_program.execution_graph.handlers.push_back(detector);

    DecoratedProgram consumer_program;
    auto reader = linked_handler(linked_symbol(SymbolKind::System, "consumer", "ReadGroundContact"), tick_trigger, 1);
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

    const auto tick          = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const auto solid         = linked_symbol(SymbolKind::Trait, "phys", "Solid");
    const auto dynamic_body  = linked_symbol(SymbolKind::Trait, "game", "DynamicBody");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};

    DecoratedProgram phys_program;
    ResolvedTrait solid_trait;
    solid_trait.name             = "Solid";
    solid_trait.is_pub           = true;
    solid_trait.module_name      = "phys";
    solid_trait.canonical_id     = make_canonical_id(solid);
    phys_program.traits["Solid"] = solid_trait;

    DecoratedProgram game_program;
    auto detector                   = linked_handler(linked_symbol(SymbolKind::System, "game", "DetectContacts"),
                                    tick_trigger,
                                    0);
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
    const auto first = linked_handler(linked_symbol(SymbolKind::System, "base", "First"), tick_trigger, 3);
    auto second      = linked_handler(linked_symbol(SymbolKind::System, "app", "Second"), tick_trigger, 1);
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

TEST_CASE("program_linker: linked handler cycles are rejected canonically", "[linker][runtime-graph][4.4]") {
    const auto tick = linked_symbol(SymbolKind::Phase, "runtime", "tick");
    const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};
    auto first  = linked_handler(linked_symbol(SymbolKind::System, "left", "First"), trigger, 0);
    auto second = linked_handler(linked_symbol(SymbolKind::System, "right", "Second"), trigger, 0);
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
    CHECK(errors.diagnostics().back().message.find("handler cycle:") != std::string::npos);
    CHECK(errors.diagnostics().back().message.find(first.identity.canonical_id()) != std::string::npos);
    CHECK(errors.diagnostics().back().message.find(second.identity.canonical_id()) != std::string::npos);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
