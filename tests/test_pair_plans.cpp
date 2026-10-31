// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/lexer.hpp"
#include "frontend/pair_plans.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace cactus;

namespace {

const std::string kPrelude =
    "module game\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "event Done\n"
    "trait Tile:\n"
    "    let x: int = 0\n"
    "trait Player\n"
    "trait Room\n"
    "relation Walk:\n"
    "    from: Tile\n"
    "    to: Tile\n"
    "relation Bridge:\n"
    "    from: Tile\n"
    "    to: Tile\n"
    "relation Owns:\n"
    "    from: Player\n"
    "    to: Room\n"
    "entity Human:\n"
    "    Player\n"
    "entity Rival:\n"
    "    Player\n"
    "template Tiles:\n"
    "    Tile\n"
    "template Rooms:\n"
    "    Room\n"
    "rule Make:\n"
    "    on frame:\n"
    "        spawn Tiles()\n"
    "        spawn Rooms()\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool errors() const {
        return std::ranges::any_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
    [[nodiscard]] bool unaccelerated(const std::string& rule) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Warning && d.message.starts_with("pair rule '" + rule + "' is not accelerated");
        });
    }
    [[nodiscard]] const PairPlan& plan(const std::string& rule) const {
        const auto found = std::ranges::find_if(
            program.handler_contracts, [&](const auto& contract) { return contract.rule.local_name == rule; });
        REQUIRE(found != program.handler_contracts.end());
        return found->pair_plan;
    }
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ": " + d.message + "\n";
    }
    return text;
}

Analysis analyze(const std::string& body, const std::string& prelude = kPrelude) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(prelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    for (const auto& diagnostic : errors.diagnostics()) {
        UNSCOPED_INFO(diagnostic.location.line << ": " << diagnostic.message);
    }
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program = analyzer.analyze(*analysis.ast);
    if (!errors.has_errors()) {
        derive_archetype_reachability(analysis.program, errors);
        finalize_pair_plans(analysis.program, errors);
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

std::string tile_rule(const std::string& name, const std::string& predicate) {
    return "rule " + name +
           ":\n"
           "    pairs:\n"
           "        a: Tile\n"
           "        b: Tile\n"
           "    where:\n"
           "        " +
           predicate +
           "\n"
           "    on fixed_tick:\n"
           "        emit Done to a\n";
}

const std::string kGridPrelude =
    "module game\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "event Hit\n"
    "trait Volume:\n"
    "    let min_x: int = 0\n"
    "    let max_x: int = 15\n"
    "trait Cell:\n"
    "    let volume: entity_id\n"
    "    let x: int in range(volume[Volume].min_x, volume[Volume].max_x + 1)\n"
    "    let y: int in range(0, 4)\n"
    "    let z: int in range(volume[Volume].min_x, volume[Volume].max_x + 1)\n"
    "trait Box:\n"
    "    var lo: int = 0\n"
    "    var hi: int = 3\n"
    "trait Eye:\n"
    "    var x: float = 0.0\n"
    "    var z: float = 0.0\n"
    "template Cells:\n"
    "    Cell\n"
    "template Boxes:\n"
    "    Box\n"
    "template Eyes:\n"
    "    Eye\n"
    "rule Make:\n"
    "    on frame:\n"
    "        spawn Cells()\n"
    "        spawn Boxes()\n"
    "        spawn Eyes()\n"
    "func inside(box: Box, cell: Cell) bool:\n"
    "    let low = box.lo\n"
    "    return cell.x >= low and cell.x <= box.hi and cell.z >= low and cell.z <= box.hi\n";

std::string region_rule(const std::string& name, const std::string& left, const std::string& predicate) {
    return "rule " + name +
           ":\n"
           "    pairs:\n"
           "        probe: " +
           left +
           "\n"
           "        cell: Cell as c\n"
           "    where:\n"
           "        " +
           predicate +
           "\n"
           "    on fixed_tick:\n"
           "        emit Hit to cell\n";
}

}  // namespace

TEST_CASE("Pair plans: a relation test on both bindings walks relation rows", "[pair-plans][relation-rows]") {
    const auto forward = analyze(tile_rule("Linked", "Walk(a, b)"));
    INFO(describe(forward));
    REQUIRE_FALSE(forward.errors());
    CHECK(forward.plan("Linked").kind == PairPlanKind::RelationRows);
    REQUIRE(forward.plan("Linked").rows.has_value());
    CHECK(forward.plan("Linked").rows->left_is_from);
    CHECK_FALSE(forward.unaccelerated("Linked"));

    const auto backward = analyze(tile_rule("Linked", "a[Tile].x > 0 and Walk(b, a)"));
    REQUIRE(backward.plan("Linked").rows.has_value());
    CHECK_FALSE(backward.plan("Linked").rows->left_is_from);
    CHECK_FALSE(backward.unaccelerated("Linked"));
}

TEST_CASE("Pair plans: a disjunction of planned branches is a union", "[pair-plans][union]") {
    const auto identity = analyze(tile_rule("Near", "b == a or Walk(a, b)"));
    INFO(describe(identity));
    REQUIRE_FALSE(identity.errors());
    const auto& plan = identity.plan("Near");
    CHECK(plan.kind == PairPlanKind::Union);
    REQUIRE(plan.union_branches.size() == 2);
    CHECK_FALSE(plan.union_branches[0].rows.has_value());
    CHECK(plan.union_branches[1].rows.has_value());
    CHECK_FALSE(identity.unaccelerated("Near"));

    const auto relations = analyze(tile_rule("Near", "(Walk(a, b) and a[Tile].x > 0) or Bridge(b, a)"));
    CHECK(relations.plan("Near").kind == PairPlanKind::Union);
    CHECK_FALSE(relations.unaccelerated("Near"));
}

TEST_CASE("Pair plans: a union with an unplanned branch stays a product", "[pair-plans][union]") {
    const auto analysis = analyze(tile_rule("Near", "Walk(a, b) or a[Tile].x > 3"));
    INFO(describe(analysis));
    REQUIRE_FALSE(analysis.errors());
    CHECK(analysis.plan("Near").kind == PairPlanKind::Product);
    CHECK(analysis.unaccelerated("Near"));
}

TEST_CASE("Pair plans: bounds on ranged cell fields form a grid region", "[pair-plans][grid-region]") {
    const auto box = analyze(region_rule("Select", "Box as b", "inside(b, c)"), kGridPrelude);
    INFO(describe(box));
    REQUIRE_FALSE(box.errors());
    const auto& plan = box.plan("Select");
    CHECK(plan.kind == PairPlanKind::GridRegion);
    REQUIRE(plan.grid.has_value());
    CHECK(plan.grid->region_binding == 1);
    REQUIRE(plan.grid->axes.size() == 3);
    CHECK(plan.grid->axes[0].field == "x");
    CHECK(plan.grid->axes[0].lo != nullptr);
    CHECK(plan.grid->axes[0].hi != nullptr);
    CHECK(plan.grid->axes[1].lo == nullptr);
    CHECK(plan.grid->axes[1].hi == nullptr);
    CHECK(plan.grid->axes[2].lo != nullptr);
    CHECK_FALSE(box.unaccelerated("Select"));

    const auto disk = analyze(
        region_rule("Sense", "Eye as e", "(e.x - c.x - 0.5) * (e.x - c.x - 0.5) + (e.z - c.z - 0.5) * (e.z - c.z - 0.5) <= 36.0"),
        kGridPrelude);
    INFO(describe(disk));
    const auto& sense = disk.plan("Sense");
    CHECK(sense.kind == PairPlanKind::GridRegion);
    REQUIRE(sense.grid.has_value());
    CHECK(sense.grid->radius_sq != nullptr);
    CHECK(sense.grid->axes[0].center != nullptr);
    CHECK(sense.grid->axes[2].center != nullptr);
    CHECK(sense.grid->axes[1].center == nullptr);
    CHECK_FALSE(disk.unaccelerated("Sense"));

    const auto unbounded = analyze(region_rule("Other", "Box as b", "c.x != b.lo"), kGridPrelude);
    CHECK(unbounded.plan("Other").kind == PairPlanKind::Product);
    CHECK(unbounded.unaccelerated("Other"));
}

TEST_CASE("Pair plans: a binding held only by named entities is a bounded product", "[pair-plans][bounded]") {
    const auto analysis = analyze(
        "rule Count:\n"
        "    pairs:\n"
        "        player: Player\n"
        "        room: Room\n"
        "    on fixed_tick:\n"
        "        emit Done to player\n");
    INFO(describe(analysis));
    REQUIRE_FALSE(analysis.errors());
    const auto& plan = analysis.plan("Count");
    CHECK(plan.kind == PairPlanKind::BoundedProduct);
    REQUIRE(plan.bounded.has_value());
    CHECK(plan.bounded->binding == 0);
    CHECK(plan.bounded->bound == 2);
    CHECK_FALSE(analysis.unaccelerated("Count"));
}

TEST_CASE("Pair plans: a template-spawned binding stays an unbounded product", "[pair-plans][bounded]") {
    const auto analysis = analyze(
        "template Recruit:\n"
        "    Player\n"
        "rule Hire:\n"
        "    on frame:\n"
        "        spawn Recruit()\n"
        "rule Count:\n"
        "    pairs:\n"
        "        player: Player\n"
        "        room: Room\n"
        "    on fixed_tick:\n"
        "        emit Done to player\n");
    INFO(describe(analysis));
    REQUIRE_FALSE(analysis.errors());
    CHECK(analysis.plan("Count").kind == PairPlanKind::Product);
    CHECK(analysis.unaccelerated("Count"));
}

TEST_CASE("Pair plans: the analyzer alone records the plan but leaves the warning to finalization",
          "[pair-plans][warning]") {
    ErrorReporter errors;
    Lexer lexer(kPrelude + tile_rule("Any", "a[Tile].x > b[Tile].x"), "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    SemanticAnalyzer analyzer(errors);
    auto program = analyzer.analyze(ast);
    CHECK(std::ranges::none_of(errors.diagnostics(), [](const Diagnostic& d) { return d.level == DiagnosticLevel::Warning; }));
    finalize_pair_plans(program, errors);
    CHECK(std::ranges::count(errors.diagnostics(), DiagnosticLevel::Warning, &Diagnostic::level) == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
