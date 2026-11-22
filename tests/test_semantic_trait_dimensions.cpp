// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parallel_batches.hpp"
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
    "const:\n"
    "    W = 8\n"
    "    RATE = 0.1\n"
    "enum Item:\n"
    "    Key\n"
    "    Grenade\n"
    "    Rope\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] const Diagnostic* find(DiagnosticLevel level, const std::string& text) const {
        const auto found = std::ranges::find_if(
            diagnostics, [&](const Diagnostic& d) { return d.level == level && d.message.contains(text); });
        return found == diagnostics.end() ? nullptr : &*found;
    }
    [[nodiscard]] const Diagnostic* error(const std::string& text) const {
        return find(DiagnosticLevel::Error, text);
    }
    [[nodiscard]] const Diagnostic* warning(const std::string& text) const {
        return find(DiagnosticLevel::Warning, text);
    }
    [[nodiscard]] bool clean() const {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
    [[nodiscard]] bool silent() const {
        return diagnostics.empty();
    }
    [[nodiscard]] bool has_edge(const std::string& first, const std::string& second) const {
        return std::ranges::any_of(program.execution_graph.schedule_edges, [&](const ScheduleEdge& edge) {
            const auto& before = edge.before.rule.local_name;
            const auto& after  = edge.after.rule.local_name;
            return (before == first && after == second) || (before == second && after == first);
        });
    }
    [[nodiscard]] const HandlerContract* contract(const std::string& rule) const {
        for (const auto& handler : program.execution_graph.handlers) {
            if (handler.identity.rule.local_name == rule) {
                return &handler.contract;
            }
        }
        return nullptr;
    }
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ":" + std::to_string(d.location.column) + ": " + d.message + "\n";
    }
    return text;
}

int prelude_lines() {
    return static_cast<int>(std::ranges::count(kPrelude, '\n'));
}

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

// Checks that `body` fails with an error containing `text` on body line `line` (1-based).
void check_error_at(const std::string& body, const std::string& text, int line) {
    const auto analysis = analyze(body);
    INFO(describe(analysis));
    const auto* found = analysis.error(text);
    REQUIRE(found != nullptr);
    CHECK(found->location.line == prelude_lines() + line);
}

void check_clean(const std::string& body) {
    const auto analysis = analyze(body);
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

const TraitNode& trait_named(const ProgramNode& program, const std::string& name) {
    for (const auto& decl : program.declarations) {
        if (const auto* trait = std::get_if<TraitNode>(&decl); trait != nullptr && trait->name == name) {
            return *trait;
        }
    }
    FAIL("no trait named " + name);
    std::unreachable();
}

const std::string kStock =
    "trait Stock:\n"
    "    dim item: Item\n"
    "    var count: int = 0\n";

const std::string kRoom =
    "single trait Room:\n"
    "    dim x: int in range(0, 64)\n"
    "    dim y: int in range(0, 48)\n"
    "    var gas: float = 0.0\n"
    "    var wall: bool = false\n";

const std::string kEntities =
    "entity Shop:\n"
    "    Stock\n"
    "entity Hall:\n"
    "    Room\n";

}  // namespace

// ── Declaration ─────────────────────────────────────────────────────────────

TEST_CASE("Dimensions: an enum dimension is parsed and resolved", "[semantic][trait-dimensions]") {
    const auto analysis = analyze(kStock + "entity Shop:\n    Stock\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& trait = trait_named(*analysis.ast, "Stock");
    REQUIRE(trait.dimensions.size() == 1);
    CHECK(trait.dimensions[0].name == "item");
    CHECK(trait.dimensions[0].type.name == "Item");
    CHECK_FALSE(trait.dimensions[0].range.has_value());
    const auto& resolved = analysis.program.traits.at("Stock");
    REQUIRE(resolved.dimensions.size() == 1);
    CHECK(resolved.dimensions[0].type.kind == TypeKind::Enum);
    CHECK(resolved.is_dimensioned());
}

TEST_CASE("Dimensions: two ranged dimensions on a single trait", "[semantic][trait-dimensions]") {
    const auto analysis = analyze(kRoom + "entity Hall:\n    Room\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& resolved = analysis.program.traits.at("Room");
    REQUIRE(resolved.dimensions.size() == 2);
    CHECK(resolved.dimensions[1].name == "y");
    CHECK(resolved.dimensions[1].range.has_value());
    CHECK(resolved.is_single);
}

TEST_CASE("Dimensions: bounds read constants and let fields; pub is allowed", "[semantic][trait-dimensions]") {
    check_clean(
        "pub trait Grid:\n"
        "    let width: int = 4\n"
        "    dim x: int in range(0, width)\n"
        "    dim y: int in range(-W, W * 2)\n"
        "    var v: vec2 = vec2(0.0, 0.0)\n"
        "    var kind: Item = Item.Key\n"
        "entity Big:\n"
        "    Grid:\n"
        "        width = 8\n");
}

TEST_CASE("Dimensions: `dim` stays an ordinary identifier", "[semantic][trait-dimensions]") {
    check_clean(
        "trait Size:\n"
        "    var dim: int = 0\n"
        "entity Box:\n"
        "    Size\n"
        "rule Grow:\n"
        "    each: Size as s\n"
        "    on tick:\n"
        "        let dim = s.dim + 1\n"
        "        s.dim = dim\n");
}

TEST_CASE("Dimensions: declaration errors", "[semantic][trait-dimensions]") {
    check_error_at(
        "state Mode:\n"
        "    Calm:\n"
        "        dim x: int in range(0, 4)\n",
        "`dim` is allowed only in a trait body",
        3);
    check_error_at(
        "relation Near:\n"
        "    from: Mark\n"
        "    to: Mark\n"
        "    dim x: int in range(0, 4)\n"
        "trait Mark\n",
        "`dim` is allowed only in a trait body",
        4);
    check_error_at(
        "struct P:\n"
        "    dim x: int\n",
        "`dim` is allowed only in a trait body",
        2);
    check_error_at(
        "event Boom:\n"
        "    dim x: int\n",
        "`dim` is allowed only in a trait body",
        2);
    check_error_at(
        "trait T:\n"
        "    dim a: Item\n"
        "    dim b: Item\n"
        "    dim c: Item\n"
        "    dim d: Item\n"
        "    dim e: Item\n"
        "    var v: int = 0\n",
        "at most four dimensions",
        6);
    check_error_at(
        "trait T:\n"
        "    dim a: float\n"
        "    var v: int = 0\n",
        "dimension 'T.a' needs an enum or 'int in range(a, b)'",
        2);
    check_error_at(
        "trait T:\n"
        "    dim a: int\n"
        "    var v: int = 0\n",
        "dimension 'T.a' needs an enum or 'int in range(a, b)'",
        2);
    check_error_at(
        "trait T:\n"
        "    var n: int = 3\n"
        "    dim a: int in range(0, n)\n"
        "    var v: int = 0\n",
        "which is not a 'let' field",
        3);
    check_error_at(
        "trait Holder:\n"
        "    let target: entity_id\n"
        "    dim a: int in range(0, target[Size].n)\n"
        "    var v: int = 0\n"
        "trait Size:\n"
        "    let n: int = 2\n",
        "can read only literals, constants and 'let' fields of 'Holder'",
        3);
    check_error_at(
        "func four() int:\n"
        "    return 4\n"
        "trait T:\n"
        "    dim a: int in range(0, four())\n"
        "    var v: int = 0\n",
        "can't call a function",
        4);
    check_error_at(
        "trait T:\n"
        "    dim a: Item\n"
        "    dim a: Item\n"
        "    var v: int = 0\n",
        "trait 'T' declares 'a' twice",
        3);
    check_error_at(
        "trait T:\n"
        "    dim a: Item\n"
        "    var a: int = 0\n",
        "trait 'T' declares 'a' twice",
        3);
    check_error_at(
        "trait T:\n"
        "    dim a: Item\n"
        "    var v: int\n",
        "cell field 'T.v' needs a constant default",
        3);
    check_error_at(
        "trait T:\n"
        "    dim a: Item\n"
        "    var v: string = \"x\"\n",
        "cell field 'T.v' must be bool, int, float, an enum or a vector",
        3);
    check_error_at(
        "trait T:\n"
        "    dim a: Item\n"
        "    persist var v: int = 0\n",
        "cell field 'T.v' can't be `persist`",
        3);
    check_error_at(
        "trait T:\n"
        "    dim a: Item\n"
        "    let size: int = 1\n",
        "dimensioned trait 'T' needs a `var` field",
        1);
}

TEST_CASE("Dimensions: a non-constant default is rejected", "[semantic][trait-dimensions]") {
    const auto analysis = analyze(
        "trait Mark\n"
        "trait T:\n"
        "    dim a: Item\n"
        "    var v: int = query.count[Mark]()\n");
    INFO(describe(analysis));
    CHECK_FALSE(analysis.clean());
}

TEST_CASE("Dimensions: a dimensioned trait can't be kept or projected", "[semantic][trait-dimensions]") {
    check_error_at(kStock +
                       "trait Shopper\n"
                       "entity Shop:\n"
                       "    Shopper\n"
                       "rule Open:\n"
                       "    each: Shopper\n"
                       "    keep Stock\n",
                   "a dimensioned trait can't be kept",
                   9);
    check_error_at(kStock +
                       "entity Shop:\n"
                       "    Stock\n"
                       "rule Show:\n"
                       "    each: Stock\n"
                       "    on tick:\n"
                       "        project Stock\n",
                   "a dimensioned trait can't be projected",
                   9);
}

// ── Cell access ─────────────────────────────────────────────────────────────

TEST_CASE("Cells: self takes one item from its own stock", "[semantic][trait-dimensions][cell-access]") {
    const auto analysis = analyze(kStock + kRoom + kEntities +
                                  "rule Take:\n"
                                  "    each: Stock\n"
                                  "    on tick:\n"
                                  "        self[Stock][Item.Grenade].count -= 1\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto* contract = analysis.contract("Take");
    REQUIRE(contract != nullptr);
    const auto stock = make_symbol_id(SymbolKind::Trait, "game", "Stock");
    CHECK(contract->writes.contains(stock));
    CHECK(contract->write_fields.at(stock).covers("count"));
    CHECK(contract->own_row_traits.contains(stock));
}

TEST_CASE("Cells: a carrier's cells through a binding and a named entity", "[semantic][trait-dimensions][cell-access]") {
    check_clean(kStock + kRoom + kEntities +
                "trait Shopper\n"
                "entity Buyer:\n"
                "    Shopper\n"
                "    Stock\n"
                "trait Seen:\n"
                "    var keys: int = 0\n"
                "entity Watcher:\n"
                "    Seen\n"
                "rule Count:\n"
                "    each: Seen as seen\n"
                "    on tick:\n"
                "        seen.keys = Shop[Stock][Item.Key].count + Buyer[Stock][Item.Rope].count\n"
                "rule Compare:\n"
                "    pairs:\n"
                "        buyer:\n"
                "            Shopper\n"
                "            Stock\n"
                "        seen:\n"
                "            Seen\n"
                "    where:\n"
                "        buyer[Stock][Item.Key].count > seen[Seen].keys\n"
                "    on tick:\n"
                "        let x = 1\n");
}

TEST_CASE("Cells: the short form names a single trait's cell", "[semantic][trait-dimensions][cell-access]") {
    const auto analysis = analyze(kStock + kRoom + kEntities +
                                  "trait Source:\n"
                                  "    var x: int = 3\n"
                                  "entity Grenade:\n"
                                  "    Source\n"
                                  "rule Puff:\n"
                                  "    each: Source as src\n"
                                  "    on tick:\n"
                                  "        Room[src.x, 4].gas += 0.5\n"
                                  "        let w = Room[src.x + 1, 4].wall\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto* contract = analysis.contract("Puff");
    REQUIRE(contract != nullptr);
    const auto room = make_symbol_id(SymbolKind::Trait, "game", "Room");
    CHECK(contract->write_fields.at(room).covers("gas"));
    CHECK(contract->write_kinds.at(room).at("gas").kind == WriteKind::Accum);
    CHECK_FALSE(contract->own_row_traits.contains(room));
}

TEST_CASE("Cells: a cell access needs one value of the right type per dimension",
          "[semantic][trait-dimensions][cell-access]") {
    const auto rule = [](const std::string& line) {
        return kStock + kRoom + kEntities +
               "rule R:\n"
               "    each: Stock\n"
               "    on tick:\n"
               "        " +
               line + "\n";
    };
    check_error_at(rule("let g = Room[3].gas"), "'Room' has 2 dimensions (x, y); give one value for each", 16);
    check_error_at(rule("let g = Room[1.0, 2].gas"), "dimension 'Room.x' takes 'int'", 16);
    check_error_at(rule("let c = self[Stock][3].count"), "dimension 'Stock.item' takes 'Item'", 16);
    check_error_at(rule("let c = self[Stock].count"), "'Stock.count' holds one value per cell", 16);
}

TEST_CASE("Cells: a let field of a dimensioned trait is read with one bracket",
          "[semantic][trait-dimensions][cell-access]") {
    const std::string grid =
        "trait Grid:\n"
        "    let width: int = 4\n"
        "    dim x: int in range(0, width)\n"
        "    var v: int = 0\n"
        "entity Board:\n"
        "    Grid\n";
    check_clean(grid +
                "rule R:\n"
                "    each: Grid\n"
                "    on tick:\n"
                "        self[Grid][self[Grid].width - 1].v = 1\n");
    check_error_at(grid +
                       "rule R:\n"
                       "    each: Grid\n"
                       "    on tick:\n"
                       "        let w = self[Grid][0].width\n",
                   "'Grid.width' belongs to the carrier; read it without a cell",
                   10);
}

TEST_CASE("Cells: cells are read only in rules", "[semantic][trait-dimensions][cell-access]") {
    check_error_at(kRoom + "entity Hall:\n    Room\n" +
                       "func f() float:\n"
                       "    return Room[1, 2].gas\n",
                   "cells are read and written only in rules",
                   9);
    check_error_at(kRoom + "entity Hall:\n    Room\n" +
                       "const:\n"
                       "    G = Room[1, 2].gas\n",
                   "cells are read and written only in rules",
                   9);
    check_error_at(kStock +
                       "entity Shop:\n"
                       "    Stock:\n"
                       "        count = 5\n",
                   "cells are written by handlers",
                   6);
}

TEST_CASE("Cells: the short form needs a single dimensioned trait", "[semantic][trait-dimensions][cell-access]") {
    const auto rule = [](const std::string& line) {
        return kStock + kRoom + kEntities +
               "trait Plain:\n"
               "    var v: int = 0\n"
               "rule R:\n"
               "    each: Stock\n"
               "    on tick:\n"
               "        " +
               line + "\n";
    };
    check_error_at(rule("let c = Stock[Item.Key].count"), "the short form 'Stock[...]' needs a single trait", 18);
    check_error_at(rule("let c = Plain[1].v"), "trait 'Plain' has no dimensions", 18);
    check_error_at(rule("let c = Hall[1, 2].gas"), "a trait access takes exactly one trait name", 18);
}

TEST_CASE("Cells: two unordered += writers of one cell field need no edge", "[semantic][trait-dimensions][cell-access]") {
    const auto analysis = analyze(kRoom + "entity Hall:\n    Room\n" +
                                  "trait Source\n"
                                  "trait Vent\n"
                                  "entity A:\n"
                                  "    Source\n"
                                  "entity B:\n"
                                  "    Vent\n"
                                  "rule PuffA:\n"
                                  "    each: Source\n"
                                  "    on tick:\n"
                                  "        Room[1, 1].gas += 0.5\n"
                                  "rule PuffB:\n"
                                  "    each: Vent\n"
                                  "    on tick:\n"
                                  "        Room[1, 1].gas += 0.25\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK_FALSE(analysis.has_edge("PuffA", "PuffB"));
}

// ── Unary rules over cells ──────────────────────────────────────────────────

namespace {

const RuleNode& rule_named(const ProgramNode& program, const std::string& name) {
    for (const auto& decl : program.declarations) {
        if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name == name) {
            return *rule;
        }
    }
    FAIL("no rule named " + name);
    std::unreachable();
}

const std::string kHeat =
    "trait Heat:\n"
    "    dim x: int in range(0, 8)\n"
    "    dim y: int in range(0, 8)\n"
    "    var value: float = 0.0\n"
    "    var wall: bool = false\n"
    "trait Hot\n"
    "trait Cold\n"
    "trait Frozen\n"
    "event Alarm\n"
    "entity Stove:\n"
    "    Heat\n"
    "    Hot\n"
    "entity Ice:\n"
    "    Heat\n"
    "    Cold\n"
    "    Frozen\n";

std::string heat_rule(const std::string& clauses, const std::string& body) {
    return kHeat + "rule R:\n" + clauses + "    on tick:\n" + body;
}

}  // namespace

TEST_CASE("Cell rules: an alias gives cell rows, no alias gives carrier rows", "[semantic][trait-dimensions][cell-rules]") {
    const auto analysis = analyze(kStock + kRoom + kEntities +
                                  "rule Decay:\n"
                                  "    each: Room as c\n"
                                  "    where:\n"
                                  "        c.gas > 0.0\n"
                                  "    on tick:\n"
                                  "        c.gas = c.gas * 0.5\n"
                                  "rule Shops:\n"
                                  "    each: Stock\n"
                                  "    on tick:\n"
                                  "        self[Stock][Item.Key].count += 1\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& decay = rule_named(*analysis.ast, "Decay");
    REQUIRE(decay.cells.has_value());
    CHECK(decay.cells->aliases == std::vector<std::string>{"c"});
    CHECK_FALSE(decay.cells->pairs());
    CHECK_FALSE(rule_named(*analysis.ast, "Shops").cells.has_value());
}

TEST_CASE("Cell rules: the alias reads dimensions; self is the carrier", "[semantic][trait-dimensions][cell-rules]") {
    check_clean(heat_rule("    each: Heat as h\n"
                          "    without: Frozen\n"
                          "    where:\n"
                          "        h.value > 1.0 and h.x > 2\n",
                          "        h.value -= 1.0\n"
                          "        if h.y == 0:\n"
                          "            emit Alarm to self\n"
                          "        let mine = self[Heat][h.x, h.y].value\n"));
    check_clean(heat_rule("    each: Heat as h\n",
                          "        if h.value > 3.0:\n"
                          "            emit Alarm to h.carrier\n"));
}

TEST_CASE("Cell rules: reduce folds every row, reduce per self folds a carrier's rows",
          "[semantic][trait-dimensions][cell-rules]") {
    check_clean(kStock + kRoom + kEntities +
                "trait Tally:\n"
                "    var total: int = 0\n"
                "entity Counter:\n"
                "    Tally\n"
                "    Stock\n"
                "rule Total:\n"
                "    each: Stock as s, Tally\n"
                "    reduce per self:\n"
                "        total = sum(s.count)\n"
                "    on tick:\n"
                "        set Tally on self:\n"
                "            total = total\n"
                "rule Everything:\n"
                "    each: Room as c\n"
                "    reduce:\n"
                "        gas = sum(c.gas)\n"
                "    on tick:\n"
                "        let g = gas\n");
}

TEST_CASE("Cell rules: forms a rule over cells can't take", "[semantic][trait-dimensions][cell-rules]") {
    check_error_at(heat_rule("    each: Heat as h, Room as r\n", "        h.value += 1.0\n") + kRoom,
                   "`each:` can hold one aliased dimensioned trait",
                   18);
    check_error_at(heat_rule("    each: Heat as h\n"
                             "    order by:\n"
                             "        h.value\n",
                             "        h.value += 1.0\n"),
                   "a rule over cells can't declare `order by:`",
                   20);
    check_error_at(heat_rule("    each: Heat as h\n"
                             "    limit: 3\n",
                             "        h.value += 1.0\n"),
                   "a rule over cells can't declare `limit:`",
                   19);
    check_error_at(kHeat +
                       "rule R:\n"
                       "    each: Heat as h\n"
                       "    where:\n"
                       "        h.value > 1.0\n"
                       "    keep Hot\n",
                   "a rule over cells can't keep",
                   21);
    check_error_at(heat_rule("    each: Heat as h\n", "        h.value += 1.0\n") +
                       "rule Born:\n"
                       "    each: Heat as h\n"
                       "    on added Heat:\n"
                       "        h.value = 1.0\n",
                   "a rule over cells can't run on a lifecycle trigger",
                   23);
    check_error_at(heat_rule("    each: Heat as h\n", "        emit Alarm to h\n"),
                   "a cell is not an entity; use 'self' or 'h.carrier'",
                   20);
}

// ── Pair rules over cells ───────────────────────────────────────────────────

namespace {

std::string cell_pair(const std::string& here, const std::string& there, const std::string& rest) {
    return kHeat +
           "rule Spread:\n"
           "    pairs:\n"
           "        here:\n"
           "            " +
           here + "\n" + there + rest;
}

const std::string kThere =
    "        there:\n"
    "            Heat\n";

const std::string kSpreadReduce =
    "    reduce per here:\n"
    "        net = sum((there.value - here.value) * RATE)\n"
    "    on tick:\n"
    "        here.value += net\n";

}  // namespace

TEST_CASE("Cell pairs: adjacent bounds a pair of cells", "[semantic][trait-dimensions][cell-pairs]") {
    const auto analysis = analyze(cell_pair("Heat",
                                            kThere,
                                            "    where:\n"
                                            "        adjacent(here, there)\n"
                                            "        not there.wall and not here.wall\n" +
                                                kSpreadReduce));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = rule_named(*analysis.ast, "Spread");
    REQUIRE(rule.cells.has_value());
    CHECK(rule.cells->pairs());
    CHECK(rule.cells->aliases == std::vector<std::string>{"here", "there"});
    CHECK(rule.cells->adjacent == std::optional<std::size_t>{0});
}

TEST_CASE("Cell pairs: equalities bound a pair, also as an or of chains", "[semantic][trait-dimensions][cell-pairs]") {
    const auto analysis = analyze(cell_pair("Heat",
                                            kThere,
                                            "    where:\n"
                                            "        there.carrier == here.carrier\n"
                                            "        (there.x == here.x + 1 and there.y == here.y) or "
                                            "(there.x == here.x and there.y == here.y + 1)\n" +
                                                kSpreadReduce));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = rule_named(*analysis.ast, "Spread");
    REQUIRE(rule.cells.has_value());
    CHECK_FALSE(rule.cells->adjacent.has_value());
    REQUIRE(rule.cells->key_branches.size() == 2);
    CHECK(rule.cells->key_branches[0].size() == 2);
}

TEST_CASE("Cell pairs: the carrier and exclusions", "[semantic][trait-dimensions][cell-pairs]") {
    check_clean(cell_pair("Heat\n"
                          "            without: Frozen",
                          kThere,
                          "    where:\n"
                          "        adjacent(here, there)\n" +
                              kSpreadReduce));
    check_clean(cell_pair("Heat",
                          kThere,
                          "    where:\n"
                          "        adjacent(here, there)\n"
                          "        here.value > there.value + 5.0\n"
                          "    on tick:\n"
                          "        emit Alarm to here.carrier\n"));
}

TEST_CASE("Cell pairs: forms a pair of cells can't take", "[semantic][trait-dimensions][cell-pairs]") {
    check_error_at(cell_pair("Heat", kThere, kSpreadReduce), "a pair of cells must be bounded", 18);
    check_error_at(cell_pair("Heat",
                             "        there:\n"
                             "            Hot\n",
                             "    where:\n"
                             "        adjacent(here, there)\n"
                             "    on tick:\n"
                             "        let x = 1\n"),
                   "can't be paired with an entity or row binding",
                   19);
    check_error_at(cell_pair("Heat",
                             "        there:\n"
                             "            Stock\n",
                             "    where:\n"
                             "        adjacent(here, there)\n" +
                                 kSpreadReduce) +
                       kStock,
                   "both cell bindings of a pair must name one trait",
                   21);
    check_error_at(cell_pair("Heat",
                             "        there:\n"
                             "            Heat\n"
                             "            without: Frozen\n",
                             "    where:\n"
                             "        adjacent(here, there)\n" +
                                 kSpreadReduce),
                   "`without:` belongs on the first cell binding 'here'",
                   23);
    check_error_at(heat_rule("    each: Heat as h\n"
                             "    where:\n"
                             "        adjacent(h, h)\n",
                             "        h.value += 1.0\n"),
                   "`adjacent` is a `where:` condition of a pair of cells",
                   20);
    check_error_at(cell_pair("Heat",
                             kThere,
                             "    where:\n"
                             "        adjacent(here, there)\n"
                             "    reduce per here:\n"
                             "        net = sum(there.value)\n"
                             "    on tick:\n"
                             "        there.value = net\n"),
                   "cell binding 'there' is read-only",
                   28);
    check_error_at(cell_pair("Heat",
                             kThere,
                             "    where:\n"
                             "        there.carrier == Stove\n"
                             "        there.x == here.x\n"
                             "        there.y == here.y\n" +
                                 kSpreadReduce),
                   "both cells of a pair must have one carrier",
                   24);
    check_error_at(cell_pair("Heat",
                             kThere,
                             "    where:\n"
                             "        adjacent(here, there)\n"
                             "    on tick:\n"
                             "        emit Alarm to there\n"),
                   "a cell is not an entity; use 'there.carrier'",
                   26);
}

TEST_CASE("Cell pairs: a pair of cells has no self", "[semantic][trait-dimensions][cell-pairs]") {
    check_error_at(cell_pair("Heat",
                             kThere,
                             "    where:\n"
                             "        adjacent(here, there)\n"
                             "    on tick:\n"
                             "        emit Alarm to self\n"),
                   "a pair of cells has no `self`; use 'here.carrier'",
                   26);
}

TEST_CASE("Cell pairs: an exclusion that excludes nothing is an error", "[semantic][trait-dimensions][cell-pairs]") {
    const auto analysis = analyze(cell_pair("Heat\n"
                                            "            without: Hot\n"
                                            "            without: Cold",
                                            kThere,
                                            "    where:\n"
                                            "        adjacent(here, there)\n" +
                                                kSpreadReduce));
    INFO(describe(analysis));
    CHECK_FALSE(analysis.clean());
}

TEST_CASE("Cell pairs: the first binding's exclusion is the rule's carrier exclusion",
          "[semantic][trait-dimensions][cell-pairs]") {
    const auto analysis = analyze(cell_pair("Heat\n"
                                            "            without: Frozen",
                                            kThere,
                                            "    where:\n"
                                            "        adjacent(here, there)\n" +
                                                kSpreadReduce));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto* contract = analysis.contract("Spread");
    REQUIRE(contract != nullptr);
    const auto frozen = make_symbol_id(SymbolKind::Trait, "game", "Frozen");
    CHECK(std::ranges::contains(contract->exclusion, frozen));
    CHECK(tables_read_by(*contract).contains(frozen));
}

// ── The rest law ────────────────────────────────────────────────────────────

namespace {

const std::string kRestRoom = kRoom + "entity Hall:\n    Room\n";

bool proven(const Analysis& analysis, const std::string& rule) {
    const auto& node = rule_named(*analysis.ast, rule);
    return node.cells.has_value() && node.cells->keeps_rest;
}

}  // namespace

TEST_CASE("Rest law: diffusion between neighbours is proven", "[semantic][trait-dimensions][rest-law]") {
    const auto analysis = analyze(cell_pair("Heat",
                                            kThere,
                                            "    where:\n"
                                            "        adjacent(here, there)\n" +
                                                kSpreadReduce));
    INFO(describe(analysis));
    CHECK(analysis.silent());
    CHECK(proven(analysis, "Spread"));
    CHECK(rule_named(*analysis.ast, "Spread").cells->rest_fields == std::vector<std::string>{"value"});
}

TEST_CASE("Rest law: decay by an unknown amount warns, guarded decay is proven", "[semantic][trait-dimensions][rest-law]") {
    const auto unknown = analyze(kRestRoom +
                                 "rule Decay:\n"
                                 "    each: Room as c\n"
                                 "    on tick as t:\n"
                                 "        c.gas -= t.dt * 0.1\n");
    INFO(describe(unknown));
    const auto* warning = unknown.warning("rule 'Decay' visits every cell of 'Room': the write to 'c.gas'");
    REQUIRE(warning != nullptr);
    CHECK(warning->message.contains("`where: c.gas > 0.0`"));
    CHECK(warning->location.line == prelude_lines() + 11);
    CHECK_FALSE(proven(unknown, "Decay"));

    const auto guarded = analyze(kRestRoom +
                                 "rule Decay:\n"
                                 "    each: Room as c\n"
                                 "    where:\n"
                                 "        c.gas > 0.0\n"
                                 "    on tick as t:\n"
                                 "        c.gas -= t.dt * 0.1\n");
    INFO(describe(guarded));
    CHECK(guarded.silent());
    CHECK(proven(guarded, "Decay"));
}

TEST_CASE("Rest law: other cell fields don't wake a rule", "[semantic][trait-dimensions][rest-law]") {
    const auto analysis = analyze(
        "enum Block:\n"
        "    Air\n"
        "    Stone\n"
        "single trait World:\n"
        "    dim x: int in range(0, 16)\n"
        "    var block: Block = Block.Air\n"
        "    var water: float = 0.0\n"
        "entity Map:\n"
        "    World\n"
        "rule Flow:\n"
        "    each: World as w\n"
        "    where:\n"
        "        w.water > 0.0\n"
        "    on tick:\n"
        "        w.water = w.water * 0.5\n");
    INFO(describe(analysis));
    CHECK(analysis.silent());
    CHECK(rule_named(*analysis.ast, "Flow").cells->rest_fields == std::vector<std::string>{"water"});
}

TEST_CASE("Rest law: an enum-only trait never warns", "[semantic][trait-dimensions][rest-law]") {
    const auto analysis = analyze(kStock + kEntities.substr(0, kEntities.find("entity Hall")) +
                                  "event Low\n"
                                  "rule Restock:\n"
                                  "    each: Stock as s\n"
                                  "    on tick:\n"
                                  "        if s.count < 5:\n"
                                  "            emit Low to self\n");
    INFO(describe(analysis));
    CHECK(analysis.silent());
    CHECK_FALSE(proven(analysis, "Restock"));
}

TEST_CASE("Rest law: conditions select their branch or need both", "[semantic][trait-dimensions][rest-law]") {
    const auto constant = analyze(kRestRoom +
                                  "event Puff\n"
                                  "rule Light:\n"
                                  "    each: Room as c\n"
                                  "    on tick:\n"
                                  "        if c.gas > 0.5:\n"
                                  "            emit Puff to self\n");
    INFO(describe(constant));
    CHECK(constant.silent());
    CHECK(proven(constant, "Light"));

    const auto unknown = analyze(kRestRoom +
                                 "event Puff\n"
                                 "rule Light:\n"
                                 "    each: Room as c\n"
                                 "    on tick as t:\n"
                                 "        if t.dt > 0.5:\n"
                                 "            emit Puff to self\n");
    INFO(describe(unknown));
    CHECK(unknown.warning("`emit Puff`") != nullptr);
}

TEST_CASE("Rest law: reduce per self with sum is proven", "[semantic][trait-dimensions][rest-law]") {
    const auto analysis = analyze(kStock +
                                  "trait Tally:\n"
                                  "    var total: int = 0\n"
                                  "entity Counter:\n"
                                  "    Tally\n"
                                  "    Stock\n"
                                  "rule Total:\n"
                                  "    each: Stock as s, Tally\n"
                                  "    reduce per self:\n"
                                  "        total = sum(s.count)\n"
                                  "    on tick:\n"
                                  "        set Tally on self:\n"
                                  "            total = total\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK(proven(analysis, "Total"));
}

TEST_CASE("Rest law: product and color sum are not proven", "[semantic][trait-dimensions][rest-law]") {
    const auto fold = [](const std::string& reducer) {
        return analyze("trait Strip:\n"
                       "    dim x: int in range(0, 8)\n"
                       "    var level: float = 0.0\n"
                       "entity Lamp:\n"
                       "    Strip\n"
                       "rule Blend:\n"
                       "    each: Strip as s\n"
                       "    reduce per self:\n"
                       "        " + reducer + "\n"
                       "    on tick:\n"
                       "        let unused = folded\n");
    };
    for (const std::string reducer : {"folded = product(s.level)", "folded = sum(color(s.level, 0.0, 0.0, 0.0))"}) {
        const auto analysis = fold(reducer);
        INFO(reducer << "\n" << describe(analysis));
        CHECK(analysis.clean());
        CHECK(analysis.warning("rule 'Blend' visits every cell of 'Strip': reducer 'folded'") != nullptr);
        CHECK_FALSE(proven(analysis, "Blend"));
    }
    const auto summed = fold("folded = sum(s.level)");
    INFO(describe(summed));
    CHECK(proven(summed, "Blend"));
}

// ── Fit with features landed since the explore ──────────────────────────────

TEST_CASE("Cell rules: disjoint carriers write one cell field with no edge", "[semantic][trait-dimensions][disjoint-domains]") {
    const auto analysis = analyze(kHeat +
                                  "rule WarmHot:\n"
                                  "    each: Heat as h, Hot\n"
                                  "    where:\n"
                                  "        h.value > 0.0\n"
                                  "    on tick:\n"
                                  "        h.value = 1.0\n"
                                  "rule WarmCold:\n"
                                  "    each: Heat as h, Cold\n"
                                  "    where:\n"
                                  "        h.value > 0.0\n"
                                  "    on tick:\n"
                                  "        h.value = 2.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK_FALSE(analysis.has_edge("WarmHot", "WarmCold"));
}

TEST_CASE("Cell rules: the short form still conflicts with a cell alias", "[semantic][trait-dimensions][disjoint-domains]") {
    const auto analysis = analyze(kRestRoom + kHeat +
                                  "rule Fill:\n"
                                  "    each: Hot\n"
                                  "    on tick:\n"
                                  "        Room[1, 1].gas = 1.0\n"
                                  "rule Drain:\n"
                                  "    each: Room as c\n"
                                  "    where:\n"
                                  "        c.gas > 0.0\n"
                                  "    on tick:\n"
                                  "        c.gas = 0.0\n");
    INFO(describe(analysis));
    CHECK((analysis.has_edge("Fill", "Drain") || !analysis.clean()));
}

TEST_CASE("Cell rules: a cell rule may repeat, but not with emit", "[semantic][trait-dimensions][rule-repeat]") {
    const auto repeated = analyze(cell_pair("Heat",
                                            kThere,
                                            "    where:\n"
                                            "        adjacent(here, there)\n"
                                            "    reduce per here:\n"
                                            "        net = sum((there.value - here.value) * RATE)\n"
                                            "    repeat: 2\n"
                                            "    on tick:\n"
                                            "        here.value += net\n"));
    INFO(describe(repeated));
    CHECK(repeated.silent());
    check_error_at(heat_rule("    each: Heat as h\n"
                             "    where:\n"
                             "        h.value > 3.0\n"
                             "    repeat: 2\n",
                             "        emit Alarm to self\n"),
                   "a repeating rule may only write fields",
                   23);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
