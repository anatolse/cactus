// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "common/types.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace cactus;
namespace fs = std::filesystem;

// ── Helpers ─────────────────────────────────────────────────────────────────

static fs::path test_build_dir() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR) / "artifact_test_build";
}

static DecoratedProgram make_test_program() {
    DecoratedProgram prog;

    // Trait: Position (pub)
    ResolvedTrait pos;
    pos.name   = "Position";
    pos.is_pub = true;
    ResolvedField fx;
    fx.name       = "x";
    fx.type       = make_float_type();
    fx.is_var     = true;
    fx.is_persist = true;
    fx.is_sync    = true;
    ResolvedField fy;
    fy.name                 = "y";
    fy.type                 = make_float_type();
    fy.is_var               = true;
    pos.fields              = {fx, fy};
    prog.traits["Position"] = pos;

    // Trait: PlayerPhysics (non-pub)
    ResolvedTrait phys;
    phys.name   = "PlayerPhysics";
    phys.is_pub = false;
    ResolvedField fmass;
    fmass.name                   = "mass";
    fmass.type                   = make_float_type();
    fmass.is_var                 = true;
    phys.fields                  = {fmass};
    prog.traits["PlayerPhysics"] = phys;

    // Enum: Direction
    ResolvedEnum dir;
    dir.name                = "Direction";
    dir.variants            = {"Up", "Down", "Left", "Right"};
    prog.enums["Direction"] = dir;

    // System dependency
    SystemDependency dep;
    dep.system_name = "MoveSystem";
    dep.reads       = {"Position"};
    dep.writes      = {"Position"};
    dep.emits       = {};
    prog.dependency_graph.push_back(dep);

    prog.ast = nullptr;
    return prog;
}

static SymbolId test_symbol(SymbolKind kind, const std::string& local_name) {
    return make_symbol_id(kind, "runtime.lib", local_name);
}

// ── Artifact filename ────────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: artifact_filename simple", "[artifact]") {
    auto p = ModuleArtifact::artifact_filename("player");
    CHECK(p.string() == "player.cmod");
}

TEST_CASE("ModuleArtifact: artifact_filename dotted", "[artifact]") {
    auto p = ModuleArtifact::artifact_filename("enemies.walker");
    CHECK(p.string() == "enemies.walker.cmod");
}

// ── Round-trip ───────────────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: round-trip save and load", "[artifact]") {
    auto build_dir = test_build_dir();
    // Clean up first
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto program = make_test_program();

    // Save
    bool saved = artifact.save(program, "player", build_dir);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(saved);

    auto artifact_path = build_dir / "player.cmod";
    CHECK(fs::exists(artifact_path));

    // Load
    std::string loaded_name;
    auto loaded = artifact.load(artifact_path, loaded_name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());

    CHECK(loaded_name == "player");
    CHECK(loaded->traits.size() == 2);
    CHECK(loaded->enums.size() == 1);
    CHECK(loaded->dependency_graph.size() == 1);
    CHECK(loaded->ast == nullptr);

    // Verify trait data
    REQUIRE(loaded->traits.count("Position") == 1);
    auto& pos = loaded->traits.at("Position");
    CHECK(pos.is_pub);
    REQUIRE(pos.fields.size() == 2);
    CHECK(pos.fields[0].name == "x");
    CHECK(pos.fields[0].is_var);
    CHECK(pos.fields[0].is_persist);
    CHECK(pos.fields[0].is_sync);
    CHECK(pos.fields[0].type.kind == TypeKind::Float);

    // Verify non-pub trait
    REQUIRE(loaded->traits.count("PlayerPhysics") == 1);
    CHECK_FALSE(loaded->traits.at("PlayerPhysics").is_pub);

    // Verify enum
    REQUIRE(loaded->enums.count("Direction") == 1);
    CHECK(loaded->enums.at("Direction").variants.size() == 4);

    // Verify dep graph
    CHECK(loaded->dependency_graph[0].system_name == "MoveSystem");
    CHECK(loaded->dependency_graph[0].reads.count("Position") == 1);

    // Clean up
    fs::remove_all(build_dir, ec);
}

// ── Pub symbol extraction ────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: extract_pub_symbols only returns pub traits", "[artifact]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto program = make_test_program();

    artifact.save(program, "player", build_dir);
    REQUIRE_FALSE(errors.has_errors());

    auto path    = build_dir / "player.cmod";
    auto symbols = artifact.extract_pub_symbols(path);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(symbols.has_value());

    CHECK(symbols->module_name == "player");
    // Only pub trait "Position" should be extracted
    CHECK(symbols->traits.size() == 1);
    REQUIRE(symbols->traits.contains("Position"));
    CHECK_FALSE(symbols->traits.contains("PlayerPhysics"));

    // Enum should be present
    CHECK(symbols->enums.size() == 1);

    fs::remove_all(build_dir, ec);
}

// ── Build directory creation ─────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: creates build directory if missing", "[artifact]") {
    auto build_dir = test_build_dir() / "new_subdir";
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    REQUIRE_FALSE(fs::exists(build_dir));

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto program = make_test_program();

    bool saved = artifact.save(program, "test_module", build_dir);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(saved);
    CHECK(fs::exists(build_dir));
    CHECK(fs::exists(build_dir / "test_module.cmod"));

    fs::remove_all(test_build_dir(), ec);
}

// ── Overwrite existing artifact ──────────────────────────────────────────────

TEST_CASE("ModuleArtifact: overwrites existing artifact", "[artifact]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto program = make_test_program();

    // Save once
    artifact.save(program, "player", build_dir);
    auto path  = build_dir / "player.cmod";
    auto size1 = fs::file_size(path);

    // Add another trait and save again
    ResolvedTrait extra;
    extra.name              = "Extra";
    extra.is_pub            = true;
    program.traits["Extra"] = extra;

    artifact.save(program, "player", build_dir);
    REQUIRE_FALSE(errors.has_errors());

    // File should exist and be different (larger)
    CHECK(fs::exists(path));
    auto size2 = fs::file_size(path);
    CHECK(size2 > size1);

    // Load and verify
    std::string name;
    auto loaded = artifact.load(path, name);
    REQUIRE(loaded.has_value());
    CHECK(loaded->traits.size() == 3);  // Position + PlayerPhysics + Extra

    fs::remove_all(build_dir, ec);
}

// ── Version mismatch ─────────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: version mismatch error", "[artifact]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    fs::create_directories(build_dir);

    // Write a file with wrong version manually
    auto path = build_dir / "stale.cmod";
    {
        std::ofstream out(path, std::ios::binary);
        out.write("CMOD", 4);
        const char bad_version = 99;
        out.write(&bad_version, 1);
        // rest is garbage
    }

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    std::string name;
    auto result = artifact.load(path, name);
    CHECK_FALSE(result.has_value());
    CHECK(errors.has_errors());

    fs::remove_all(build_dir, ec);
}

// ── Funcs round-trip (task 5.7) ─────────────────────────────────────────────

TEST_CASE("ModuleArtifact: extern func round-trips through save/load", "[artifact][extern-func]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    DecoratedProgram prog = make_test_program();

    // Add an extern func
    ResolvedFunc lerp;
    lerp.name      = "lerp";
    lerp.is_pub    = true;
    lerp.is_extern = true;
    ResolvedParam pa;
    pa.name = "a";
    pa.type = make_float_type();
    lerp.params.push_back(pa);
    ResolvedParam pb;
    pb.name = "b";
    pb.type = make_float_type();
    lerp.params.push_back(pb);
    ResolvedParam pt;
    pt.name = "t";
    pt.type = make_float_type();
    lerp.params.push_back(pt);
    lerp.return_type   = make_float_type();
    prog.funcs["lerp"] = lerp;

    // Also add a non-pub func
    ResolvedFunc helper;
    helper.name                   = "internal_helper";
    helper.is_pub                 = false;
    helper.is_extern              = true;
    prog.funcs["internal_helper"] = helper;

    artifact.save(prog, "mathlib", build_dir);
    REQUIRE_FALSE(errors.has_errors());

    auto path = build_dir / "mathlib.cmod";
    std::string name;
    auto loaded = artifact.load(path, name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());

    CHECK(name == "mathlib");
    REQUIRE(loaded->funcs.contains("lerp"));
    auto& lf = loaded->funcs.at("lerp");
    CHECK(lf.name == "lerp");
    CHECK(lf.is_pub);
    CHECK(lf.is_extern);
    REQUIRE(lf.params.size() == 3);
    CHECK(lf.params[0].name == "a");
    CHECK(lf.params[0].type.kind == TypeKind::Float);
    REQUIRE(lf.return_type.has_value());
    CHECK(lf.return_type->kind == TypeKind::Float);

    // Non-pub func also round-trips
    REQUIRE(loaded->funcs.contains("internal_helper"));

    fs::remove_all(build_dir, ec);
}

TEST_CASE("ModuleArtifact: extract_pub_symbols returns pub extern funcs only", "[artifact][extern-func]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    DecoratedProgram prog = make_test_program();

    // Add pub extern func
    ResolvedFunc pub_func;
    pub_func.name      = "lerp";
    pub_func.is_pub    = true;
    pub_func.is_extern = true;
    prog.funcs["lerp"] = pub_func;

    // Add non-pub extern func
    ResolvedFunc priv_func;
    priv_func.name         = "internal";
    priv_func.is_pub       = false;
    priv_func.is_extern    = true;
    prog.funcs["internal"] = priv_func;

    artifact.save(prog, "testmod", build_dir);
    REQUIRE_FALSE(errors.has_errors());

    auto path    = build_dir / "testmod.cmod";
    auto symbols = artifact.extract_pub_symbols(path);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(symbols.has_value());

    // Only pub func should be in symbols
    CHECK(symbols->funcs.size() == 1);
    REQUIRE(symbols->funcs.contains("lerp"));
    CHECK_FALSE(symbols->funcs.contains("internal"));
    CHECK(symbols->funcs.at("lerp").is_pub);
    CHECK(symbols->funcs.at("lerp").is_extern);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("ModuleArtifact: stdlib physics query API symbols round-trip", "[artifact][extern-func][stdlib][physics]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    DecoratedProgram prog;

    ResolvedEnum kind;
    kind.name             = "QueryResultKind";
    kind.variants         = {"Empty", "Hit"};
    prog.enums[kind.name] = kind;

    ResolvedStruct contact;
    contact.name               = "QueryContact2D";
    contact.fields             = {{.name = "other", .type = make_entity_id_type()},
                                  {.name = "normal", .type = make_vec2_type()},
                                  {.name = "distance", .type = make_float_type()},
                                  {.name = "overlap", .type = make_vec2_type()}};
    prog.structs[contact.name] = contact;

    ResolvedStruct query_result;
    query_result.name   = "QueryResult2D";
    query_result.fields = {{.name = "kind", .type = {.kind = TypeKind::Enum, .name = "QueryResultKind"}},
                           {.name = "contact", .type = {.kind = TypeKind::Struct, .name = "QueryContact2D"}}};
    prog.structs[query_result.name] = query_result;

    ResolvedFunc cast;
    cast.name             = "query_cast_nearest";
    cast.is_pub           = true;
    cast.is_extern        = true;
    cast.return_type      = TypeInfo{.kind = TypeKind::Struct, .name = "QueryResult2D"};
    cast.params           = {{.name = "subject", .type = make_entity_id_type()},
                             {.name = "delta", .type = make_vec2_type()},
                             {.name = "mask", .type = make_int_type()},
                             {.name = "exclude", .type = make_entity_id_type()}};
    prog.funcs[cast.name] = cast;

    ResolvedFunc overlap_all;
    overlap_all.name             = "query_overlap_all";
    overlap_all.is_pub           = true;
    overlap_all.is_extern        = true;
    overlap_all.return_type      = make_list_type(TypeInfo{.kind = TypeKind::Struct, .name = "QueryContact2D"});
    overlap_all.params           = {{.name = "subject", .type = make_entity_id_type()},
                                    {.name = "mask", .type = make_int_type()},
                                    {.name = "exclude", .type = make_entity_id_type()}};
    prog.funcs[overlap_all.name] = overlap_all;

    REQUIRE(artifact.save(prog, "std.physics.flat", build_dir));
    REQUIRE_FALSE(errors.has_errors());

    std::string loaded_name;
    auto loaded = artifact.load(build_dir / "std.physics.flat.cmod", loaded_name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());
    CHECK(loaded_name == "std.physics.flat");

    REQUIRE(loaded->enums.contains("QueryResultKind"));
    CHECK(loaded->enums.at("QueryResultKind").variants == std::vector<std::string>{"Empty", "Hit"});
    REQUIRE(loaded->structs.contains("QueryContact2D"));
    REQUIRE(loaded->structs.contains("QueryResult2D"));
    REQUIRE(loaded->funcs.contains("query_cast_nearest"));
    CHECK(loaded->funcs.at("query_cast_nearest").return_type->name == "QueryResult2D");
    REQUIRE(loaded->funcs.contains("query_overlap_all"));
    REQUIRE(loaded->funcs.at("query_overlap_all").return_type.has_value());
    CHECK(loaded->funcs.at("query_overlap_all").return_type->kind == TypeKind::List);
    REQUIRE(loaded->funcs.at("query_overlap_all").return_type->element != nullptr);
    CHECK(loaded->funcs.at("query_overlap_all").return_type->element->name == "QueryContact2D");

    auto symbols = artifact.extract_pub_symbols(build_dir / "std.physics.flat.cmod");
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(symbols.has_value());
    CHECK(symbols->funcs.contains("query_cast_nearest"));
    CHECK(symbols->funcs.contains("query_overlap_all"));
    CHECK(symbols->structs.contains("QueryContact2D"));
    CHECK(symbols->structs.contains("QueryResult2D"));
    CHECK(symbols->enums.contains("QueryResultKind"));

    fs::remove_all(build_dir, ec);
}

TEST_CASE("ModuleArtifact: version-1 artifact rejected with error", "[artifact][extern-func]") {
    // This is covered by the existing "version mismatch error" test (version 99)
    // which also rejects version 1 (since CURRENT_VERSION is now 2)
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    fs::create_directories(build_dir);

    // Write a file with version 1 (old format, no funcs section)
    auto path = build_dir / "v1.cmod";
    {
        std::ofstream out(path, std::ios::binary);
        out.write("CMOD", 4);
        const char v1 = 1;
        out.write(&v1, 1);
        // rest doesn't matter
    }

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    std::string name;
    auto result = artifact.load(path, name);
    CHECK_FALSE(result.has_value());
    CHECK(errors.has_errors());  // version 1 != CURRENT_VERSION(2) → error

    fs::remove_all(build_dir, ec);
}

// ── Task 4.6: Canonical identity round-trip ───────────────────────────────────

TEST_CASE("ModuleArtifact: canonical identity round-trips through save/load", "[artifact][4.6]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    DecoratedProgram prog;

    // Trait with pre-set canonical identity (as set by semantic analysis).
    ResolvedTrait wt;
    wt.name                       = "WorldTransform";
    wt.is_pub                     = true;
    wt.module_name                = "std.transform.flat";
    wt.canonical_id               = "std.transform.flat.WorldTransform";
    prog.traits["WorldTransform"] = wt;

    // Func with pre-set canonical identity.
    ResolvedFunc wp;
    wp.name                      = "world_position";
    wp.is_pub                    = true;
    wp.is_extern                 = true;
    wp.module_name               = "std.transform.flat";
    wp.canonical_id              = "std.transform.flat.world_position";
    prog.funcs["world_position"] = wp;

    REQUIRE(artifact.save(prog, "std.transform.flat", build_dir));
    REQUIRE_FALSE(errors.has_errors());

    std::string loaded_name;
    auto loaded = artifact.load(build_dir / "std.transform.flat.cmod", loaded_name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());
    CHECK(loaded_name == "std.transform.flat");

    REQUIRE(loaded->traits.count("WorldTransform") == 1);
    const auto& lt = loaded->traits.at("WorldTransform");
    CHECK(lt.canonical_id == "std.transform.flat.WorldTransform");
    CHECK(lt.module_name == "std.transform.flat");

    REQUIRE(loaded->funcs.count("world_position") == 1);
    const auto& lf = loaded->funcs.at("world_position");
    CHECK(lf.canonical_id == "std.transform.flat.world_position");
    CHECK(lf.module_name == "std.transform.flat");

    // extract_pub_symbols also preserves canonical identity
    auto syms = artifact.extract_pub_symbols(build_dir / "std.transform.flat.cmod");
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(syms.has_value());
    CHECK(syms->traits.at("WorldTransform").canonical_id == "std.transform.flat.WorldTransform");
    CHECK(syms->funcs.at("world_position").canonical_id == "std.transform.flat.world_position");

    fs::remove_all(build_dir, ec);
}

TEST_CASE("ModuleArtifact: canonical identity preserved as-is (not derived) when not set", "[artifact][4.6]") {
    // Traits/structs/enums with empty canonical_id are serialised with an empty canonical_id.
    // The linker is responsible for deriving canonical identity from src_module_name at merge time.
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto prog = make_test_program();  // traits have empty canonical_id

    REQUIRE(artifact.save(prog, "physics", build_dir));
    REQUIRE_FALSE(errors.has_errors());

    std::string loaded_name;
    auto loaded = artifact.load(build_dir / "physics.cmod", loaded_name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());

    REQUIRE(loaded->traits.count("Position") == 1);
    CHECK(loaded->traits.at("Position").canonical_id.empty());  // not derived by serializer
    CHECK(loaded->traits.at("Position").module_name == "physics");

    REQUIRE(loaded->enums.count("Direction") == 1);
    CHECK(loaded->enums.at("Direction").canonical_id.empty());  // not derived by serializer

    fs::remove_all(build_dir, ec);
}

TEST_CASE("ModuleArtifact: dep_graph after_systems round-trips through save/load", "[artifact][4.6]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    DecoratedProgram prog;

    SystemDependency dep;
    dep.system_name = "Follow2D";
    dep.reads.insert("std.transform.flat.WorldTransform");
    dep.after_systems = {"std.transform.flat.TransformPropagation"};
    prog.dependency_graph.push_back(dep);

    REQUIRE(artifact.save(prog, "game", build_dir));
    REQUIRE_FALSE(errors.has_errors());

    std::string loaded_name;
    auto loaded = artifact.load(build_dir / "game.cmod", loaded_name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());

    REQUIRE(loaded->dependency_graph.size() == 1);
    const auto& d = loaded->dependency_graph[0];
    CHECK(d.system_name == "Follow2D");
    CHECK(d.reads.count("std.transform.flat.WorldTransform") == 1);
    REQUIRE(d.after_systems.size() == 1);
    CHECK(d.after_systems[0] == "std.transform.flat.TransformPropagation");

    fs::remove_all(build_dir, ec);
}

TEST_CASE("ModuleArtifact: runtime declarations and handler graph round-trip", "[artifact][runtime-graph][4.1]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    const auto frame    = test_symbol(SymbolKind::Event, "frame");
    const auto tick     = test_symbol(SymbolKind::Phase, "tick");
    const auto position = test_symbol(SymbolKind::Trait, "Position");
    const auto spawned  = test_symbol(SymbolKind::Event, "Spawned");
    const auto tmpl     = test_symbol(SymbolKind::Template, "Actor");
    const auto first    = test_symbol(SymbolKind::System, "First");
    const auto second   = test_symbol(SymbolKind::System, "Second");
    const ResolvedHandlerTrigger tick_trigger{.kind = HandlerTriggerKind::Phase, .symbol = tick};
    const HandlerIdentity first_handler{.system = first, .trigger = tick_trigger};
    const HandlerIdentity second_handler{.system = second, .trigger = tick_trigger};

    DecoratedProgram prog;
    ResolvedEvent frame_decl;
    frame_decl.name         = "frame";
    frame_decl.module_name  = "runtime.lib";
    frame_decl.canonical_id = make_canonical_id(frame);
    frame_decl.symbol_id    = frame;
    frame_decl.is_pub       = true;
    frame_decl.is_external  = true;
    frame_decl.fields       = {{.name = "dt", .type = make_float_type(), .has_default = true}};
    prog.events.emplace(frame_decl.name, frame_decl);
    prog.pub_events.insert(frame_decl.name);

    ResolvedPhase tick_decl;
    tick_decl.name            = "tick";
    tick_decl.module_name     = "runtime.lib";
    tick_decl.canonical_id    = make_canonical_id(tick);
    tick_decl.symbol_id       = tick;
    tick_decl.is_pub          = true;
    tick_decl.has_every       = true;
    tick_decl.has_max         = true;
    tick_decl.from_sources    = {{.kind = HandlerTriggerKind::Event, .symbol = frame}};
    tick_decl.runtime_root    = frame;
    tick_decl.every_seconds   = 0.02;
    tick_decl.max_repetitions = 4;
    tick_decl.fields          = {
        {.name = "dt", .type = make_float_type(), .is_synthesized = true},
        {.name = "alpha", .type = make_float_type(), .is_synthesized = true, .is_completion_only = true},
        {.name           = "frame_dt",
         .type           = make_float_type(),
         .has_default    = true,
         .source_binding = PhaseFieldSource{.kind = PhaseFieldSource::Kind::RootEvent, .source = frame, .member = "dt"}}};
    prog.phases.emplace(tick_decl.name, tick_decl);

    HandlerContract contract;
    contract.selection        = {position};
    contract.is_selectionless = false;
    contract.reads            = {position};
    contract.writes           = {position};
    contract.emits            = {spawned};
    contract.commands         = {{.kind = HandlerCommandKind::Spawn, .target = tmpl}};
    contract.effects          = {"graphics"};
    InferredHandlerContract inferred;
    static_cast<HandlerContract&>(inferred) = contract;
    inferred.system                         = second;
    inferred.trigger                        = tick_trigger;
    prog.handler_contracts.push_back(std::move(inferred));

    prog.execution_graph.phases.push_back(PhasePlan{.phase               = tick,
                                                    .source_dependencies = tick_decl.from_sources,
                                                    .fields              = tick_decl.fields,
                                                    .runtime_root        = frame,
                                                    .every_seconds       = 0.02,
                                                    .max_repetitions     = 4,
                                                    .declaration_order   = {.declaration_index = 1}});
    prog.execution_graph.handlers.push_back(HandlerNode{.identity          = first_handler,
                                                        .contract          = {},
                                                        .declaration_order = {.declaration_index = 2},
                                                        .location          = {"runtime.cactus", 10, 3}});
    prog.execution_graph.handlers.push_back(HandlerNode{.identity          = second_handler,
                                                        .implementation    = HandlerImplementationKind::External,
                                                        .contract          = contract,
                                                        .explicit_after    = {first_handler},
                                                        .declaration_order = {.declaration_index = 3},
                                                        .location          = {"runtime.cactus", 20, 5}});
    prog.execution_graph.schedule_edges.push_back(ScheduleEdge{.before            = first_handler,
                                                               .after             = second_handler,
                                                               .kind              = ScheduleEdgeKind::ExplicitHandler,
                                                               .orientation       = ScheduleEdgeOrientation::Explicit,
                                                               .trait_provenance  = {position},
                                                               .effect_provenance = {"graphics"}});
    prog.execution_graph.phase_barriers.push_back(
        PhaseBarrierEdge{.upstream_phase = tick, .downstream_handler = second_handler});
    prog.execution_graph.event_flows.push_back(
        EventFlowEdge{.producer = first_handler, .event = spawned, .consumer = second_handler});
    prog.execution_graph.stable_topological_order = {first_handler, second_handler};
    prog.execution_graph.dependency_levels.push_back(
        DependencyLevel{.activation = tick_trigger, .index = 1, .handlers = {second_handler}});

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    REQUIRE(artifact.save(prog, "runtime.lib", build_dir));
    std::string module_name;
    auto loaded = artifact.load(build_dir / "runtime.lib.cmod", module_name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());
    CHECK(module_name == "runtime.lib");
    REQUIRE(loaded->events.contains("frame"));
    CHECK(loaded->events.at("frame").is_external);
    CHECK(loaded->events.at("frame").fields[0].has_default);
    REQUIRE(loaded->phases.contains("tick"));
    CHECK(loaded->phases.at("tick").every_seconds == 0.02);
    CHECK(loaded->phases.at("tick").max_repetitions == 4);
    CHECK(loaded->phases.at("tick").fields[1].is_completion_only);
    REQUIRE(loaded->phases.at("tick").fields[2].source_binding.has_value());
    CHECK(loaded->phases.at("tick").fields[2].source_binding->kind == PhaseFieldSource::Kind::RootEvent);
    CHECK(loaded->phases.at("tick").fields[2].source_binding->source == frame);
    CHECK(loaded->phases.at("tick").fields[2].source_binding->member == "dt");
    REQUIRE(loaded->handler_contracts.size() == 1);
    CHECK(loaded->handler_contracts[0].effects.contains("graphics"));
    CHECK(loaded->handler_contracts[0].commands == contract.commands);
    REQUIRE(loaded->execution_graph.handlers.size() == 2);
    CHECK(loaded->execution_graph.handlers[1].identity == second_handler);
    CHECK(loaded->execution_graph.handlers[1].explicit_after == std::vector<HandlerIdentity>{first_handler});
    CHECK(loaded->execution_graph.handlers[1].location == SourceLocation{"runtime.cactus", 20, 5});
    REQUIRE(loaded->execution_graph.schedule_edges.size() == 1);
    CHECK(loaded->execution_graph.schedule_edges[0].trait_provenance == std::vector<SymbolId>{position});
    CHECK(loaded->execution_graph.schedule_edges[0].effect_provenance == std::vector<std::string>{"graphics"});
    CHECK(loaded->execution_graph.phase_barriers.size() == 1);
    CHECK(loaded->execution_graph.event_flows.size() == 1);
    CHECK(loaded->execution_graph.stable_topological_order ==
          std::vector<HandlerIdentity>{first_handler, second_handler});
    CHECK(loaded->execution_graph.dependency_levels[0].activation == tick_trigger);

    auto symbols = artifact.extract_pub_symbols(build_dir / "runtime.lib.cmod");
    REQUIRE(symbols.has_value());
    REQUIRE(symbols->event_symbols.contains("frame"));
    CHECK(symbols->event_symbols.at("frame").is_external);
    CHECK(symbols->event_symbols.at("frame").symbol_id == frame);
    REQUIRE(symbols->phase_symbols.contains("tick"));
    CHECK(symbols->phase_symbols.at("tick").runtime_root == frame);
    CHECK(symbols->phase_symbols.at("tick").fields[1].is_completion_only);
    REQUIRE(symbols->phase_symbols.at("tick").fields[2].source_binding.has_value());
    CHECK(symbols->phase_symbols.at("tick").fields[2].source_binding->kind == PhaseFieldSource::Kind::RootEvent);

    fs::remove_all(build_dir, ec);
}

// ── Invalid magic ─────────────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: invalid magic rejected", "[artifact]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    fs::create_directories(build_dir);

    auto path = build_dir / "bad.cmod";
    {
        std::ofstream out(path, std::ios::binary);
        out.write("JUNK", 4);
    }

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    std::string name;
    auto result = artifact.load(path, name);
    CHECK_FALSE(result.has_value());
    CHECK(errors.has_errors());

    fs::remove_all(build_dir, ec);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
