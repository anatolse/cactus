// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace cactus;
namespace fs = std::filesystem;

namespace {

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has(const std::string& text) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error && d.message.contains(text);
        });
    }
    [[nodiscard]] bool warns(const std::string& text) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Warning && d.message.contains(text);
        });
    }
    [[nodiscard]] const Diagnostic* error_with(const std::string& text) const {
        const auto found = std::ranges::find_if(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error && d.message.contains(text);
        });
        return found == diagnostics.end() ? nullptr : &*found;
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
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ": " + d.message + "\n";
    }
    return text;
}

Analysis analyze_source(const std::string& source, const std::string& module, const ModuleImports& imports) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(source, module + ".cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    INFO(module << ": " << (errors.diagnostics().empty() ? std::string() : errors.diagnostics().front().message));
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program     = analyzer.analyze(*analysis.ast, imports);
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

// Compiles a real stdlib module and exports it through a saved artifact, the
// way an importing module sees it.
ImportedSymbols stdlib_module(const std::string& name, const std::string& relative_path, const ModuleImports& imports) {
    std::ifstream file(fs::path(CACTUS_TEST_SOURCE_DIR) / "stdlib" / relative_path);
    REQUIRE(file);
    std::ostringstream text;
    text << file.rdbuf();
    auto analysis = analyze_source(text.str(), name, imports);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto build_dir = fs::path(CACTUS_TEST_FIXTURES_DIR) / "semantic_collider_build";
    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    REQUIRE(artifact.save(analysis.program, name, build_dir));
    auto symbols = artifact.extract_pub_symbols(build_dir / ModuleArtifact::artifact_filename(name));
    REQUIRE(symbols.has_value());
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    return *symbols;
}

const ModuleImports& stdlib_imports() {
    static const ModuleImports imports = [] {
        ModuleImports core;
        core.add("std.core", stdlib_module("std.core", "std/core.cactus", {}));
        ModuleImports result = core;
        result.add("physics", stdlib_module("std.physics.volume", "std/physics/volume.cactus", core));
        result.add("tv", stdlib_module("std.transform.volume", "std/transform/volume.cactus", core));
        return result;
    }();
    return imports;
}

const std::string kPrelude =
    "use std.physics.volume as physics\n"
    "use std.transform.volume as tv\n"
    "trait Probe:\n"
    "    var speed: float = 1.0\n"
    "    var total: float = 0.0\n"
    "trait Bullet:\n"
    "    var velocity: vec3 = vec3(0.0, 0.0, 0.0)\n"
    "    var owner: entity_id\n"
    "    var travelled: float = 0.0\n"
    "trait Solid\n"
    "event Hit\n";

Analysis analyze(const std::string& body) {
    return analyze_source("module game\n" + kPrelude + body, "game", stdlib_imports());
}

const std::string kBulletPairs =
    "    pairs:\n"
    "        bullet:\n"
    "            Bullet\n"
    "            tv.WorldTransform\n"
    "        target:\n"
    "            physics.Collider\n";

std::string bullet_rule(const std::string& clauses, const std::string& handler_lines = "        let x = 1\n") {
    return "rule MoveBullets:\n" + kBulletPairs + clauses + "    on fixed_tick:\n" + handler_lines;
}

const std::string kFirstHit =
    "    reduce:\n"
    "        per: bullet\n"
    "        first = first_hit(physics.sweep(bullet, bullet.Bullet.velocity * fixed_tick.dt, target))\n";

SymbolId trait_id(const std::string& module, const std::string& trait) {
    return make_symbol_id(SymbolKind::Trait, module, trait);
}

bool bound_read(const HandlerContract& contract, std::size_t binding, const SymbolId& trait) {
    return std::ranges::contains(contract.bound_reads, BoundTraitAccess{.binding_index = binding, .trait = trait});
}

const FieldAccess& read_fields(const HandlerContract& contract, const SymbolId& trait) {
    const auto found = contract.read_fields.find(trait);
    REQUIRE(found != contract.read_fields.end());
    return found->second;
}

const ReducerDecl& first_reducer(const Analysis& analysis, const std::string& rule_name) {
    for (const auto& decl : analysis.ast->declarations) {
        if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name == rule_name) {
            REQUIRE(rule->reduce.has_value());
            return rule->reduce->reducers.front();
        }
    }
    FAIL("no rule named " + rule_name);
    std::unreachable();
}

const std::string kProbePairs =
    "    pairs:\n"
    "        probe:\n"
    "            Probe\n"
    "        other:\n"
    "            Solid\n";

}  // namespace

// ── Periodic phase dt as a constant ─────────────────────────────────────────

TEST_CASE("Phase dt: a periodic phase's dt is a constant in const blocks", "[semantic][phase-activation]") {
    const auto analysis = analyze(
        "const:\n"
        "    STEP_DISTANCE = 4.0 * fixed_tick.dt\n"
        "rule Use:\n"
        "    filter:\n"
        "        Probe as probe\n"
        "    on fixed_tick:\n"
        "        probe.total = STEP_DISTANCE\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Phase dt: a periodic phase's dt is a constant in reduce:", "[semantic][phase-activation][rule-reduce]") {
    const auto analysis = analyze("rule Sum:\n" + kProbePairs +
                                  "    reduce:\n"
                                  "        per: probe\n"
                                  "        travelled = sum(probe.Probe.speed * fixed_tick.dt)\n"
                                  "    on tick:\n"
                                  "        probe.Probe.total = travelled\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Phase dt: a periodic phase's dt is a constant in where:", "[semantic][phase-activation][where-clause]") {
    const auto analysis = analyze(
        "rule Fast:\n"
        "    filter:\n"
        "        Probe as probe\n"
        "    where:\n"
        "        probe.speed * fixed_tick.dt > 1.0\n"
        "    on tick:\n"
        "        probe.total = 1.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Phase dt: a periodic phase's dt is a constant in order by:", "[semantic][phase-activation][order-by]") {
    const auto analysis = analyze(
        "rule Ranked:\n"
        "    filter:\n"
        "        Probe as probe\n"
        "    order by:\n"
        "        probe.speed * fixed_tick.dt desc\n"
        "    on tick:\n"
        "        probe.total = 1.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Phase dt: a periodic phase's dt still reads the event inside its handlers", "[semantic][phase-activation]") {
    const auto analysis = analyze(
        "rule Step:\n"
        "    filter:\n"
        "        Probe as probe\n"
        "    on fixed_tick:\n"
        "        probe.total += fixed_tick.dt\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Phase dt: a variable-rate phase's dt is not a constant", "[semantic][phase-activation]") {
    const auto analysis = analyze("rule Sum:\n" + kProbePairs +
                                  "    reduce:\n"
                                  "        per: probe\n"
                                  "        travelled = sum(probe.Probe.speed * tick.dt)\n"
                                  "    on tick:\n"
                                  "        probe.Probe.total = travelled\n");
    const auto* error = analysis.error_with("'tick.dt' is only available inside 'tick' handlers");
    INFO(describe(analysis));
    REQUIRE(error != nullptr);
    CHECK(error->location.line > 0);
}

TEST_CASE("Phase dt: a variable-rate phase's dt is rejected in const blocks", "[semantic][phase-activation]") {
    const auto analysis = analyze(
        "const:\n"
        "    STEP = 2.0 * tick.dt\n");
    INFO(describe(analysis));
    CHECK(analysis.has("'tick.dt' is only available inside 'tick' handlers"));
}

// ── Binding-only collider queries ───────────────────────────────────────────

TEST_CASE("Collider queries: sweep and touching accept the rule's bindings", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(bullet_rule("    where:\n        physics.touching(bullet, target)\n" + kFirstHit,
                                              "        bullet.Bullet.travelled = first.t\n"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Collider queries: a named entity argument is rejected", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(
        "entity Game:\n"
        "    Solid\n" +
        bullet_rule("    reduce:\n"
                    "        per: bullet\n"
                    "        first = first_hit(physics.sweep(bullet, vec3(1.0, 0.0, 0.0), Game))\n"));
    const auto* error = analysis.error_with("'Game' is not a binding of rule 'MoveBullets'");
    INFO(describe(analysis));
    REQUIRE(error != nullptr);
    CHECK(error->location.line > 0);
}

TEST_CASE("Collider queries: an entity_id field argument is rejected", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(bullet_rule("    where:\n        physics.touching(bullet, bullet.Bullet.owner)\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("physics.touching takes bindings of the calling rule"));
}

TEST_CASE("Collider queries: a local copy of a binding is rejected", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(bullet_rule("",
                                              "        let other = target\n"
                                              "        if physics.touching(bullet, other):\n"
                                              "            bullet.Bullet.travelled = 1.0\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("'other' is not a binding of rule 'MoveBullets'"));
}

TEST_CASE("Collider queries: a func can't call them", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(
        "func near(a: entity_id, b: entity_id) bool:\n"
        "    return physics.touching(a, b)\n");
    INFO(describe(analysis));
    CHECK(analysis.has("physics.touching takes bindings of the calling rule"));
}

TEST_CASE("Collider queries: both bindings' collider data join the contract", "[semantic][stdlib-physics][contracts]") {
    const auto analysis = analyze(bullet_rule("    where:\n        physics.touching(bullet, target)\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& contract = analysis.contract("MoveBullets");
    for (const auto* shape : {"Collider", "BoxCollider", "SphereCollider", "CapsuleCollider"}) {
        const auto trait = trait_id("std.physics.volume", shape);
        CHECK(contract.reads.contains(trait));
        CHECK(bound_read(contract, 0, trait));
        CHECK(bound_read(contract, 1, trait));
    }
    const auto transform = trait_id("std.transform.volume", "WorldTransform");
    CHECK(contract.reads.contains(transform));
    CHECK(bound_read(contract, 1, transform));
    CHECK_FALSE(read_fields(contract, transform).all);
    CHECK(read_fields(contract, transform).fields == std::set<std::string>{"position", "rotation"});
    CHECK(read_fields(contract, trait_id("std.physics.volume", "Collider")).fields ==
          std::set<std::string>{"layer", "mask"});
    CHECK(read_fields(contract, trait_id("std.physics.volume", "BoxCollider")).all);
    // Shape traits are read when present; they don't narrow the selection.
    CHECK(contract.pair_bindings[1].required_traits.size() == 1);
}

// ── first_hit ───────────────────────────────────────────────────────────────

TEST_CASE("first_hit: the result is a SweepHit", "[semantic][rule-reduce][stdlib-physics]") {
    const auto analysis = analyze(bullet_rule(kFirstHit,
                                              "        if first.hit and exists(first.other):\n"
                                              "            bullet.Bullet.travelled = first.t + first.point.x + first.normal.y\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& reducer = first_reducer(analysis, "MoveBullets");
    CHECK(reducer.kind == ReducerKind::FirstHit);
    CHECK(reducer.result_type.kind == TypeKind::Struct);
    REQUIRE(reducer.result_type.symbol_id.has_value());
    CHECK(make_canonical_id(*reducer.result_type.symbol_id) == "std.physics.volume.SweepHit");
    const auto& plan = analysis.contract("MoveBullets").reduction;
    REQUIRE(plan.has_value());
    CHECK(plan->reducers == std::vector<ReducerKind>{ReducerKind::FirstHit});
}

TEST_CASE("first_hit: the input must be a SweepHit", "[semantic][rule-reduce][stdlib-physics]") {
    const auto analysis = analyze(bullet_rule(
        "    reduce:\n"
        "        per: bullet\n"
        "        first = first_hit(bullet.Bullet.travelled)\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("first_hit(...) input must be of type 'std.physics.volume.SweepHit', got 'float'"));
}

TEST_CASE("first_hit: the aggregate is immutable", "[semantic][rule-reduce][stdlib-physics]") {
    const auto analysis = analyze(bullet_rule(kFirstHit, "        first = first\n"));
    INFO(describe(analysis));
    CHECK_FALSE(analysis.clean());
}

// ── One shape per collider ──────────────────────────────────────────────────

TEST_CASE("Collider shapes: a collider without a shape is rejected", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(
        "template Bare:\n"
        "    tv.WorldTransform\n"
        "    physics.Collider\n");
    const auto* error = analysis.error_with("template 'Bare' applies physics.Collider without a shape trait");
    INFO(describe(analysis));
    REQUIRE(error != nullptr);
    CHECK(error->location.line > 0);
}

TEST_CASE("Collider shapes: a collider with two shapes is rejected", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(
        "template Double:\n"
        "    physics.Collider\n"
        "    physics.BoxCollider\n"
        "    physics.SphereCollider\n");
    INFO(describe(analysis));
    CHECK(analysis.has("template 'Double' applies physics.Collider with more than one shape trait (BoxCollider, "
                       "SphereCollider)"));
}

TEST_CASE("Collider shapes: shapes gained through use and from count", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(
        "template Round:\n"
        "    physics.SphereCollider\n"
        "template Ball:\n"
        "    use Round\n"
        "    physics.Collider\n"
        "entity GoodBall from Ball:\n"
        "    physics.SphereCollider:\n"
        "        radius = 2.0\n"
        "template BoxedBall:\n"
        "    use Ball\n"
        "    physics.BoxCollider\n");
    INFO(describe(analysis));
    CHECK_FALSE(analysis.has("template 'Ball'"));
    CHECK_FALSE(analysis.has("'GoodBall'"));
    CHECK(analysis.has("template 'BoxedBall' applies physics.Collider with more than one shape trait (SphereCollider, "
                       "BoxCollider)"));
}

TEST_CASE("Collider shapes: a child entity is checked too", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(
        "entity Parent:\n"
        "    Solid\n"
        "    children:\n"
        "        entity Hitbox:\n"
        "            physics.Collider\n");
    INFO(describe(analysis));
    CHECK(analysis.has("applies physics.Collider without a shape trait"));
}

TEST_CASE("Collider shapes: a shape without a collider is fine", "[semantic][stdlib-physics]") {
    const auto analysis = analyze(
        "template Walkable:\n"
        "    physics.BoxCollider\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

// ── Planner recognition ─────────────────────────────────────────────────────

namespace {

const std::string kUnaccelerated = "pair rule 'MoveBullets' is not accelerated";

}  // namespace

TEST_CASE("Collider planner: touching in where: is broad-phase eligible", "[semantic][where-clause][spatial-join]") {
    const auto analysis = analyze(bullet_rule("    where:\n        bullet != target\n        physics.touching(bullet, target)\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK_FALSE(analysis.warns(kUnaccelerated));
    const auto& plan = analysis.contract("MoveBullets").spatial_join;
    REQUIRE(plan.has_value());
    CHECK(plan->source == SpatialJoinSource::Where);
    CHECK(plan->matched_predicate_index == 1);
    CHECK(plan->dimension == SpatialJoinDimension::Volume3D);
    CHECK(plan->left.kind == SpatialShapeKind::Collider);
    CHECK(plan->left.binding_index == 0);
    CHECK(plan->right.kind == SpatialShapeKind::Collider);
    CHECK(plan->right.binding_index == 1);
}

TEST_CASE("Collider planner: touching keeps its argument order", "[semantic][where-clause][spatial-join]") {
    const auto analysis = analyze(bullet_rule("    where:\n        physics.touching(target, bullet)\n"));
    const auto& plan = analysis.contract("MoveBullets").spatial_join;
    REQUIRE(plan.has_value());
    CHECK(plan->left.binding_index == 1);
    CHECK(plan->right.binding_index == 0);
}

TEST_CASE("Collider planner: a sole first_hit sweep in reduce: is eligible", "[semantic][rule-reduce][spatial-join]") {
    const auto analysis = analyze(bullet_rule(kFirstHit));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK_FALSE(analysis.warns(kUnaccelerated));
    const auto& plan = analysis.contract("MoveBullets").spatial_join;
    REQUIRE(plan.has_value());
    CHECK(plan->source == SpatialJoinSource::Reduce);
    CHECK(plan->matched_predicate_index == 0);
    CHECK(plan->left.kind == SpatialShapeKind::SweptCollider);
    CHECK(plan->left.binding_index == 0);
    CHECK(plan->left.slots == std::vector<ExprPath>{ExprPath{1}});
    CHECK(plan->right.kind == SpatialShapeKind::Collider);
    CHECK(plan->right.binding_index == 1);
}

TEST_CASE("Collider planner: repeated first_hit over the same sweep stays eligible", "[semantic][rule-reduce][spatial-join]") {
    const auto analysis = analyze(bullet_rule(
        kFirstHit + "        again = first_hit(physics.sweep(bullet, bullet.Bullet.velocity * fixed_tick.dt, target))\n"));
    INFO(describe(analysis));
    CHECK(analysis.contract("MoveBullets").spatial_join.has_value());
}

TEST_CASE("Collider planner: mixed reducers are not eligible", "[semantic][rule-reduce][spatial-join]") {
    const auto analysis = analyze(bullet_rule(kFirstHit + "        near = count()\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK_FALSE(analysis.contract("MoveBullets").spatial_join.has_value());
    CHECK(analysis.warns(kUnaccelerated));
}

TEST_CASE("Collider planner: sweeps that differ are not eligible", "[semantic][rule-reduce][spatial-join]") {
    const auto analysis = analyze(bullet_rule(
        kFirstHit + "        far = first_hit(physics.sweep(bullet, bullet.Bullet.velocity, target))\n"));
    INFO(describe(analysis));
    CHECK_FALSE(analysis.contract("MoveBullets").spatial_join.has_value());
}

TEST_CASE("Collider planner: wrapped touching is an ordinary predicate", "[semantic][where-clause][spatial-join]") {
    for (const auto* predicate : {"not physics.touching(bullet, target)", "physics.touching(bullet, target) or bullet == target"}) {
        const auto analysis = analyze(bullet_rule(std::string("    where:\n        ") + predicate + "\n"));
        INFO(describe(analysis));
        CHECK(analysis.clean());
        CHECK_FALSE(analysis.contract("MoveBullets").spatial_join.has_value());
        CHECK(analysis.warns(kUnaccelerated));
    }
}

TEST_CASE("Collider planner: a delta reading the target is not eligible", "[semantic][rule-reduce][spatial-join]") {
    const auto analysis = analyze(bullet_rule(
        "    reduce:\n"
        "        per: bullet\n"
        "        first = first_hit(physics.sweep(bullet, target.tv.WorldTransform.position, target))\n"));
    INFO(describe(analysis));
    CHECK_FALSE(analysis.contract("MoveBullets").spatial_join.has_value());
    CHECK(analysis.warns(kUnaccelerated));
}

TEST_CASE("Collider planner: a where: overlap predicate still wins over reduce:", "[semantic][rule-reduce][spatial-join]") {
    const auto analysis = analyze(bullet_rule("    where:\n        physics.touching(bullet, target)\n" + kFirstHit));
    const auto& plan = analysis.contract("MoveBullets").spatial_join;
    REQUIRE(plan.has_value());
    CHECK(plan->source == SpatialJoinSource::Where);
}
