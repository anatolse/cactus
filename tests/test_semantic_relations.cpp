// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "common/ast_expressions.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace cactus;

namespace {

const std::string kPrelude =
    "extern event frame:\n"
    "    dt: float\n"
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "phase late:\n"
    "    after:\n"
    "        fixed_tick\n"
    "group links:\n"
    "    phase: fixed_tick\n"
    "pub extern event OrderGiven:\n"
    "    robot: entity_id\n"
    "    task: entity_id\n"
    "event Done\n"
    "trait Tile:\n"
    "    let x: int = 0\n"
    "    let height: float = 0.0\n"
    "trait Floor\n"
    "trait Heart\n"
    "trait Connected\n"
    "trait Robot:\n"
    "    var x: float = 0.0\n"
    "    var busy: bool = false\n"
    "trait Task:\n"
    "    var x: float = 0.0\n"
    "    var taken: bool = false\n"
    "trait Room\n"
    "trait Player\n"
    "trait Lens\n"
    "state Mood:\n"
    "    Calm\n"
    "    Angry\n"
    "relation Walk:\n"
    "    from: Tile\n"
    "    to: Tile\n"
    "    let cost: float = 1.0\n"
    "relation Bridge:\n"
    "    from: Tile\n"
    "    to: Tile\n"
    "relation Claim:\n"
    "    from: Robot one\n"
    "    to: Task one\n"
    "    let score: int = 0\n"
    "relation Owns:\n"
    "    from: Player\n"
    "    to: Room one\n"
    "relation Seen:\n"
    "    from: Robot\n"
    "    to: Task\n"
    "entity Boss:\n"
    "    Player\n";

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
    [[nodiscard]] const RuleNode& rule(const std::string& name) const {
        for (const auto& decl : ast->declarations) {
            if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name == name) {
                return *rule;
            }
        }
        FAIL("no rule named " + name);
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

Analysis analyze_source(const std::string& source, const ModuleImports& imports = {}) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(source, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program     = analyzer.analyze(*analysis.ast, imports);
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

Analysis analyze(const std::string& body) {
    return analyze_source("module game\n" + kPrelude + body);
}

const std::string kTilePairs =
    "    pairs:\n"
    "        a: Tile\n"
    "        b: Tile\n";

const std::string kClaimPairs =
    "    pairs:\n"
    "        robot: Robot\n"
    "        task: Task\n";

bool has_command(const HandlerContract& contract, HandlerCommandKind kind, const std::string& table) {
    return std::ranges::any_of(contract.commands, [&](const InferredHandlerCommand& command) {
        return command.kind == kind && command.target.has_value() && command.target->local_name == table;
    });
}

SymbolId relation_id(const std::string& name) {
    return make_symbol_id(SymbolKind::Relation, "game", name);
}

}  // namespace

// ── Declarations ────────────────────────────────────────────────────────────

TEST_CASE("Relations: declarations resolve their ends and fields", "[semantic][relations]") {
    const auto analysis = analyze("");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    REQUIRE(analysis.program.relations.contains("Walk"));
    const auto& walk = analysis.program.relations.at("Walk");
    CHECK(walk.symbol_id == relation_id("Walk"));
    CHECK(walk.from.trait == make_symbol_id(SymbolKind::Trait, "game", "Tile"));
    CHECK(walk.to.trait == make_symbol_id(SymbolKind::Trait, "game", "Tile"));
    REQUIRE(walk.fields.size() == 1);
    CHECK(walk.fields[0].type.kind == TypeKind::Float);
    CHECK(walk.fields[0].has_default);
    const auto& claim = analysis.program.relations.at("Claim");
    CHECK(claim.from.one);
    CHECK(claim.to.one);
}

TEST_CASE("Relations: an end resolves an imported trait", "[semantic][relations]") {
    ErrorReporter library_errors;
    Lexer lexer("module tf\npub trait WorldTransform\n", "tf.cactus", library_errors);
    Parser parser(lexer.tokenize(), library_errors);
    auto library_ast = parser.parse_program();
    SemanticAnalyzer library(library_errors);
    const auto library_program = library.analyze(library_ast);
    REQUIRE_FALSE(library_errors.has_errors());
    ImportedSymbols symbols;
    symbols.module_name = "tf";
    symbols.traits      = library_program.traits;
    ModuleImports imports;
    imports.add("tf", symbols);

    const auto analysis = analyze_source(
        "module game\n"
        "use tf\n"
        "relation Near:\n"
        "    from: tf.WorldTransform\n"
        "    to: tf.WorldTransform\n",
        imports);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.program.relations.at("Near").from.trait ==
          make_symbol_id(SymbolKind::Trait, "tf", "WorldTransform"));
}

TEST_CASE("Relations: invalid ends and clashes are rejected", "[semantic][relations]") {
    SECTION("unknown trait") {
        const auto analysis = analyze(
            "relation Bad:\n"
            "    from: Ghost\n"
            "    to: Tile\n");
        CHECK(analysis.has("unknown trait 'Ghost' at the `from:` end of relation 'Bad'"));
    }
    SECTION("state variant") {
        const auto analysis = analyze(
            "relation Bad:\n"
            "    from: Tile\n"
            "    to: Mood.Calm\n");
        CHECK(analysis.has("the `to:` end of relation 'Bad' can't name state variant 'Mood.Calm'"));
    }
    SECTION("projected trait") {
        const auto analysis = analyze(
            "relation Bad:\n"
            "    from: Lens\n"
            "    to: Tile\n"
            "rule Show:\n"
            "    each: Tile\n"
            "    on fixed_tick:\n"
            "        project Lens\n");
        CHECK(analysis.has("the `from:` end of relation 'Bad' can't name projected trait 'Lens'"));
    }
    SECTION("name clash") {
        const auto analysis = analyze("trait Walk\n");
        CHECK(analysis.has("duplicate module-scope declaration 'Walk'"));
    }
    SECTION("field default type") {
        const auto analysis = analyze(
            "relation Bad:\n"
            "    from: Tile\n"
            "    to: Tile\n"
            "    let cost: float = true\n");
        CHECK(analysis.has("default value type 'bool' does not match field type 'float'"));
    }
}
// ── Row bindings ────────────────────────────────────────────────────────────

TEST_CASE("Relations: a pair rule joins an entity to a row", "[semantic][relations]") {
    const auto analysis = analyze(
        "rule FollowWalk:\n"
        "    pairs:\n"
        "        t: Tile\n"
        "        r: Walk\n"
        "    where:\n"
        "        r.from == t\n"
        "        r.cost > 0.5\n"
        "    on fixed_tick:\n"
        "        emit Done to r.to\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = analysis.rule("FollowWalk");
    REQUIRE(rule.pairs->bindings[1].resolved_relation_id.has_value());
    CHECK(*rule.pairs->bindings[1].resolved_relation_id == relation_id("Walk"));
}

TEST_CASE("Relations: a unary row domain", "[semantic][relations]") {
    const auto analysis = analyze(
        "rule Finish:\n"
        "    each: Claim as claim\n"
        "    order by:\n"
        "        claim.score desc\n"
        "    where:\n"
        "        claim.score >= 0\n"
        "    limit: 3\n"
        "    on fixed_tick:\n"
        "        emit Done to claim.from\n"
        "        remove Claim(claim.from, claim.to)\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = analysis.rule("Finish");
    REQUIRE(rule.rows.has_value());
    CHECK(rule.rows->alias == "claim");
    CHECK(rule.rows->relation == relation_id("Claim"));
    CHECK(rule.filter.entries.empty());
}

TEST_CASE("Relations: row binding errors", "[semantic][relations]") {
    SECTION("write to a row field") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Walk as r\n"
            "    on fixed_tick:\n"
            "        r.cost = 2.0\n");
        CHECK(analysis.has("relation rows are read-only; replace the row with `add Walk(a, b):`"));
    }
    SECTION("brackets on a row") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    pairs:\n"
            "        t: Tile\n"
            "        r: Walk\n"
            "    where:\n"
            "        r[Tile].x == 1\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("'r' is a relation row; join a `Tile` binding on `r.from` or `r.to`"));
    }
    SECTION("row as an emit target") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Walk as r\n"
            "    on fixed_tick:\n"
            "        emit Done to r\n");
        CHECK(analysis.has("'r' is a relation row, not an entity; use `r.from` or `r.to`"));
    }
    SECTION("row as a command target") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Walk as r\n"
            "    on fixed_tick:\n"
            "        destroy r\n");
        CHECK(analysis.has("'r' is a relation row, not an entity; use `r.from` or `r.to`"));
    }
    SECTION("unknown row field") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Walk as r\n"
            "    where:\n"
            "        r.speed > 1.0\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("relation 'Walk' has no field 'speed'"));
    }
    SECTION("two row bindings") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    pairs:\n"
            "        r1: Walk\n"
            "        r2: Bridge\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("a rule may bind at most one relation"));
    }
    SECTION("relation mixed with traits") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    pairs:\n"
            "        t: Tile\n"
            "        r:\n"
            "            Walk\n"
            "            Tile\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("a row binding has exactly one relation entry"));
    }
    SECTION("unary row domain needs an alias") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Walk\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("`each: Walk` binds relation rows; name them with `as`"));
    }
    SECTION("unary row domain with without:") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Walk as r\n"
            "    without: Floor\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("a relation row domain takes no `without:`"));
    }
    SECTION("relation mixed with traits in each:") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each:\n"
            "        Walk as r\n"
            "        Tile\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("a relation row domain has exactly one relation entry"));
    }
    SECTION("self in a row domain") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Walk as r\n"
            "    on fixed_tick:\n"
            "        emit Done to self\n");
        CHECK(analysis.has("`self` is unavailable in a relation row domain"));
    }
    SECTION("reduce on a row domain") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    pairs:\n"
            "        t: Tile\n"
            "        r: Walk\n"
            "    reduce per t:\n"
            "        n = count()\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("`reduce:` can't run over relation rows"));
    }
    SECTION("keep on a row domain") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Walk as r\n"
            "    group: links\n"
            "    keep Floor\n");
        CHECK(analysis.has("`keep` can't run over relation rows"));
    }
}

// ── Relation tests ──────────────────────────────────────────────────────────

namespace {

const RelationTestExpr* first_relation_test(const ExprNode& expr) {
    const RelationTestExpr* found = nullptr;
    visit_expression(expr, [&found](const ExprNode& node) {
        if (const auto* test = std::get_if<RelationTestExpr>(&node.expr); test != nullptr && found == nullptr) {
            found = test;
        }
    });
    return found;
}

}  // namespace

TEST_CASE("Relations: a call naming a relation is a bool test", "[semantic][relations]") {
    const auto analysis = analyze(
        "rule Linked:\n"
        "    pairs:\n"
        "        a: Tile\n"
        "        b: Tile\n"
        "    where:\n"
        "        Walk(a, b) or Bridge(a, b)\n"
        "        Walk(b, a)\n"
        "        Walk(a, _) and not Bridge(_, b)\n"
        "    on fixed_tick:\n"
        "        emit Done to a\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& predicates = analysis.rule("Linked").where_clause->predicates;
    const auto* inverse    = first_relation_test(*predicates[1]);
    REQUIRE(inverse != nullptr);
    CHECK(inverse->relation == relation_id("Walk"));
    CHECK(dotted_name_of(*inverse->from) == "b");
    const auto* wildcard = first_relation_test(*predicates[2]);
    REQUIRE(wildcard != nullptr);
    CHECK(wildcard->from != nullptr);
    CHECK(wildcard->to == nullptr);
}

TEST_CASE("Relations: a test in when: reads named entities", "[semantic][relations]") {
    const auto analysis = analyze(
        "entity Spot:\n"
        "    Room\n"
        "rule Watch:\n"
        "    each: Robot\n"
        "    when:\n"
        "        not Owns(Boss, Spot)\n"
        "    on fixed_tick:\n"
        "        emit Done\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Relations: relation test errors", "[semantic][relations]") {
    SECTION("both wildcards") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Robot\n"
            "    where:\n"
            "        Claim(_, _)\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("`Claim(_, _)` tests nothing; name at least one end"));
    }
    SECTION("in a handler body") {
        const auto analysis = analyze(
            "rule Bad:\n" + kClaimPairs +
            "    on fixed_tick:\n"
            "        if Claim(robot, task):\n"
            "            emit Done\n");
        CHECK(analysis.has("a relation test is allowed only in `where:` and `when:`; move `Claim(...)` into `where:`"));
    }
    SECTION("in a keep field") {
        const auto analysis = analyze(
            "trait Mark:\n"
            "    let linked: bool = false\n"
            "rule Bad:\n"
            "    each: Tile\n"
            "    group: links\n"
            "    keep Mark:\n"
            "        linked = Walk(self, self)\n");
        CHECK(analysis.has("a relation test is allowed only in `where:` and `when:`"));
    }
    SECTION("wrong argument count") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Robot\n"
            "    where:\n"
            "        Claim(self)\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("a relation test takes two entities: `Claim(from, to)`"));
    }
    SECTION("non-entity argument") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Robot\n"
            "    where:\n"
            "        Claim(self, 3)\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("relation test arguments must be of type 'entity_id', got 'int'"));
    }
}

// ── Kept relations ──────────────────────────────────────────────────────────

namespace {

std::string walk_keeper(const std::string& keep_line, const std::string& pairs = kTilePairs) {
    return "rule WalkLinks:\n" + pairs +
           "    group: links\n"
           "    where:\n"
           "        a != b\n" +
           keep_line;
}

}  // namespace

TEST_CASE("Relations: a pair rule keeps a relation", "[semantic][relations][rule-keep]") {
    const auto analysis = analyze(walk_keeper(
        "    keep Walk(a, b):\n"
        "        cost = a[Tile].height + b[Tile].height\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = analysis.rule("WalkLinks");
    CHECK(rule.keep->resolved_target == relation_id("Walk"));
    CHECK(analysis.program.relations.at("Walk").kept_by == "WalkLinks");
    const auto& contract = analysis.contract("WalkLinks");
    CHECK(has_command(contract, HandlerCommandKind::Add, "Walk"));
    CHECK(has_command(contract, HandlerCommandKind::Remove, "Walk"));
}

TEST_CASE("Relations: a relation keep may pass its bindings in either order", "[semantic][relations][rule-keep]") {
    const auto analysis = analyze(walk_keeper("    keep Walk(b, a)\n"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Relations: relation keep errors", "[semantic][relations][rule-keep]") {
    SECTION("arguments other than the two bindings") {
        const auto analysis = analyze(walk_keeper("    keep Walk(a, a)\n"));
        CHECK(analysis.has("the arguments of `keep Walk(...)` must be the rule's two bindings 'a' and 'b'"));
    }
    SECTION("binding lacks the end trait") {
        const auto analysis = analyze(walk_keeper("    keep Walk(a, b)\n",
                                                  "    pairs:\n"
                                                  "        a: Floor\n"
                                                  "        b: Tile\n"));
        CHECK(analysis.has("'a' must select `Tile` to be the `from` end of `Walk`"));
    }
    SECTION("reduced rule") {
        const auto analysis = analyze(
            "rule Bad:\n" + kTilePairs +
            "    group: links\n"
            "    reduce per a:\n"
            "        n = count()\n"
            "    keep Walk(a, b)\n");
        CHECK(analysis.has("a relation keep needs an unreduced `pairs:` rule"));
    }
    SECTION("unary rule") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Tile\n"
            "    group: links\n"
            "    keep Walk(a, b)\n");
        CHECK(analysis.has("a relation keep needs an unreduced `pairs:` rule"));
    }
    SECTION("a kept relation can't mark one") {
        const auto analysis = analyze(
            "rule Bad:\n" + kClaimPairs +
            "    group: links\n"
            "    keep Claim(robot, task)\n");
        CHECK(analysis.has("kept relation 'Claim' can't mark an end `one`"));
    }
    SECTION("two keepers") {
        const auto analysis = analyze(walk_keeper("    keep Walk(a, b)\n") + "rule Again:\n" + kTilePairs +
                                      "    group: links\n"
                                      "    keep Walk(a, b)\n");
        CHECK(analysis.has("'Walk' is kept by both 'WalkLinks' and 'Again'"));
    }
    SECTION("add of a kept relation") {
        const auto analysis = analyze(walk_keeper("    keep Walk(a, b)\n") + "rule Adder:\n" + kTilePairs +
                                      "    on fixed_tick:\n"
                                      "        add Walk(a, b)\n");
        CHECK(analysis.has("'Walk' is kept by rule 'WalkLinks'; rule 'Adder' can't change it"));
    }
    SECTION("remove of a kept relation") {
        const auto analysis = analyze(walk_keeper("    keep Walk(a, b)\n") + "rule Remover:\n" + kTilePairs +
                                      "    on fixed_tick:\n"
                                      "        remove Walk(a, _)\n");
        CHECK(analysis.has("'Walk' is kept by rule 'WalkLinks'; rule 'Remover' can't change it"));
    }
    SECTION("a trait named with bindings") {
        const auto analysis = analyze(walk_keeper("    keep Floor(a, b)\n"));
        CHECK(analysis.has("'Floor' is not a relation; `keep Floor(...)` needs one"));
    }
    SECTION("a relation named without bindings") {
        const auto analysis = analyze(walk_keeper("    keep Walk on a\n"));
        CHECK(analysis.has("`keep Walk` keeps a relation; write `keep Walk(a, b)`"));
    }
    SECTION("unknown field") {
        const auto analysis = analyze(walk_keeper(
            "    keep Walk(a, b):\n"
            "        speed = 1.0\n"));
        CHECK(analysis.has("unknown field 'speed' in `keep Walk(a, b)`"));
    }
    SECTION("condition") {
        const auto analysis = analyze(walk_keeper("    keep Walk(a, b) if true\n"));
        CHECK(analysis.has("`if` works only on a `reduce per` keep"));
    }
}

TEST_CASE("Relations: keep loops through relations", "[semantic][relations][rule-keep]") {
    SECTION("keeper tests its own relation") {
        const auto analysis = analyze(
            "relation Reach:\n"
            "    from: Tile\n"
            "    to: Tile\n"
            "rule ReachLinks:\n" +
            kTilePairs +
            "    group: links\n"
            "    where:\n"
            "        Reach(a, _) or a == b\n"
            "    keep Reach(a, b)\n");
        CHECK(analysis.has("keep rule 'ReachLinks' reads its own kept relation 'Reach'"));
    }
    SECTION("a trait kept from a relation") {
        const auto analysis = analyze(walk_keeper("    keep Walk(a, b)\n") +
                                      "entity Core:\n"
                                      "    Tile\n"
                                      "    Heart\n"
                                      "rule ConnectedFact:\n"
                                      "    each: Tile\n"
                                      "    group: links\n"
                                      "    where:\n"
                                      "        Walk(Core, self)\n"
                                      "    keep Connected\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("two relation keepers read each other") {
        const auto analysis = analyze(
            "relation Left:\n"
            "    from: Tile\n"
            "    to: Tile\n"
            "relation Right:\n"
            "    from: Tile\n"
            "    to: Tile\n"
            "rule LeftLinks:\n" +
            kTilePairs +
            "    group: links\n"
            "    where:\n"
            "        not Right(a, b)\n"
            "    keep Left(a, b)\n"
            "rule RightLinks:\n" +
            kTilePairs +
            "    group: links\n"
            "    where:\n"
            "        Left(b, a)\n"
            "    keep Right(a, b)\n");
        CHECK(analysis.has("keep rules form a loop through their kept relations: 'LeftLinks'"));
    }
}

// ── Stored relation commands ────────────────────────────────────────────────

TEST_CASE("Relations: add and remove of a stored relation", "[semantic][relations]") {
    const auto analysis = analyze(
        "rule Link:\n" + kClaimPairs +
        "    on fixed_tick:\n"
        "        add Seen(robot, task)\n"
        "        add Walk(robot, robot):\n"
        "            cost = 2.0\n"
        "        remove Seen(robot, _)\n"
        "        remove Seen(_, task)\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& contract = analysis.contract("Link");
    CHECK(has_command(contract, HandlerCommandKind::Add, "Seen"));
    CHECK(has_command(contract, HandlerCommandKind::Add, "Walk"));
    CHECK(has_command(contract, HandlerCommandKind::Remove, "Seen"));
    CHECK_FALSE(contract.reads.contains(relation_id("Seen")));
    CHECK_FALSE(contract.writes.contains(relation_id("Seen")));
}

TEST_CASE("Relations: stored command errors", "[semantic][relations]") {
    SECTION("non-entity argument") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Robot\n"
            "    on fixed_tick:\n"
            "        add Seen(self, 3)\n");
        CHECK(analysis.has("relation row arguments must be of type 'entity_id', got 'int'"));
    }
    SECTION("wildcard in add") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Robot\n"
            "    on fixed_tick:\n"
            "        add Seen(self, _)\n");
        CHECK(analysis.has("`_` is allowed only in `remove` and in relation tests"));
    }
    SECTION("not a relation") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Robot\n"
            "    on fixed_tick:\n"
            "        add Floor(self, self)\n");
        CHECK(analysis.has("unknown relation 'Floor'"));
    }
    SECTION("unknown field") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Tile\n"
            "    on fixed_tick:\n"
            "        add Walk(self, self):\n"
            "            speed = 1.0\n");
        CHECK(analysis.has("unknown field 'speed' in `add Walk(...)`"));
    }
    SECTION("field type") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Tile\n"
            "    on fixed_tick:\n"
            "        add Walk(self, self):\n"
            "            cost = true\n");
        CHECK(analysis.has("type mismatch for field 'cost' in `add Walk(...)`"));
    }
    SECTION("required field") {
        const auto analysis = analyze(
            "relation Priced:\n"
            "    from: Tile\n"
            "    to: Tile\n"
            "    let price: int\n"
            "rule Bad:\n"
            "    each: Tile\n"
            "    on fixed_tick:\n"
            "        add Priced(self, self)\n");
        CHECK(analysis.has("required field 'price' must be supplied in `add Priced(...)`"));
    }
    SECTION("command in a func") {
        const auto analysis = analyze(
            "func link(a: entity_id, b: entity_id) int:\n"
            "    remove Seen(a, b)\n"
            "    return 1\n");
        CHECK_FALSE(analysis.clean());
    }
}

// ── The `one` proof ─────────────────────────────────────────────────────────

namespace {

std::string claim_rule(const std::string& name,
                       const std::string& limit,
                       const std::string& where,
                       const std::string& trigger = "fixed_tick") {
    return "rule " + name + ":\n" + kClaimPairs + "    order by:\n        robot[Robot].x - task[Task].x asc\n" + where +
           limit + "    on " + trigger + ":\n        add Claim(robot, task)\n";
}

const std::string kBothLimits    = "    limit: 1 per robot, 1 per task\n";
const std::string kBothFreeTests = "    where:\n        not Claim(robot, _)\n        not Claim(_, task)\n";

}  // namespace

TEST_CASE("Relations: a proven claim is accepted", "[semantic][relations][one]") {
    const auto analysis = analyze(claim_rule("Assign", kBothLimits, kBothFreeTests));
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Relations: the free tests may sit in an and chain", "[semantic][relations][one]") {
    const auto analysis = analyze(claim_rule(
        "Assign", kBothLimits, "    where:\n        not Claim(robot, _) and not Claim(_, task) and robot != task\n"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Relations: a unary owner is proven", "[semantic][relations][one]") {
    const auto analysis = analyze(
        "rule Claim1:\n"
        "    each: Room\n"
        "    where:\n"
        "        not Owns(_, self)\n"
        "    on fixed_tick:\n"
        "        add Owns(Boss, self)\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Relations: unproven adds of one ends", "[semantic][relations][one]") {
    SECTION("missing limit part") {
        const auto analysis = analyze(claim_rule("Assign", "    limit: 1 per robot\n", kBothFreeTests));
        CHECK(analysis.has("the `to` end of `Claim` is `one` and `task` is not limited to one"));
        CHECK(analysis.has("a proven add of `Claim` has `limit: 1 per robot, 1 per task`"));
    }
    SECTION("limit not provably one") {
        const auto analysis = analyze(claim_rule("Assign", "    limit: 1 per robot, 2 per task\n", kBothFreeTests));
        CHECK(analysis.has("the `to` end of `Claim` is `one` and `task` is not limited to one"));
    }
    SECTION("missing existence test") {
        const auto analysis =
            analyze(claim_rule("Assign", kBothLimits, "    where:\n        not Claim(_, task)\n"));
        CHECK(analysis.has("the `from` end of `Claim` is `one` and `where:` lacks `not Claim(robot, _)`"));
    }
    SECTION("an or does not prove the test") {
        const auto analysis = analyze(claim_rule(
            "Assign", kBothLimits, "    where:\n        not Claim(robot, _) or robot == task\n        not Claim(_, task)\n"));
        CHECK(analysis.has("`where:` lacks `not Claim(robot, _)`"));
    }
    SECTION("event trigger") {
        const auto analysis = analyze(claim_rule("Assign", kBothLimits, kBothFreeTests, "OrderGiven"));
        CHECK(analysis.has("its handler runs on 'OrderGiven', which can run more than once per activation"));
        CHECK(analysis.has("record the order as a trait and claim in a phase rule"));
    }
    SECTION("two adders in one phase") {
        const auto analysis = analyze(claim_rule("ClaimA", kBothLimits, kBothFreeTests) +
                                      claim_rule("ClaimB", kBothLimits, kBothFreeTests));
        CHECK(analysis.has("rules 'ClaimA' and 'ClaimB' both add `Claim` in phase 'fixed_tick'"));
    }
    SECTION("adders in different phases") {
        const auto analysis = analyze(claim_rule("ClaimA", kBothLimits, kBothFreeTests) +
                                      claim_rule("ClaimB", kBothLimits, kBothFreeTests, "late"));
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("unary end that is not self") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Room\n"
            "    where:\n"
            "        not Owns(_, Boss)\n"
            "    on fixed_tick:\n"
            "        add Owns(self, Boss)\n");
        CHECK(analysis.has("the `to` end of `Owns` is `one` and `Boss` is not `self` or a pair binding limited to one"));
    }
    SECTION("other argument computed") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    each: Room\n"
            "    where:\n"
            "        not Owns(_, self)\n"
            "    on fixed_tick:\n"
            "        let owner = Boss\n"
            "        add Owns(owner, self)\n");
        CHECK(analysis.has("the other argument of `add Owns(...)` must be a pair binding, `self`, a named entity or a constant"));
    }
}

// ── Two-sided limit ─────────────────────────────────────────────────────────

TEST_CASE("Relations: two-sided limit checks", "[semantic][relations][rule-limit]") {
    SECTION("two parts on one binding") {
        const auto analysis = analyze(
            "rule Bad:\n" + kClaimPairs +
            "    limit: 1 per robot, 2 per robot\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("the two parts of `limit:` must name different bindings"));
    }
    SECTION("a part on a row binding") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    pairs:\n"
            "        t: Tile\n"
            "        r: Walk\n"
            "    limit: 1 per t, 1 per r\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("a two-sided limit needs two entity bindings"));
    }
    SECTION("a one-sided part on a row binding") {
        const auto analysis = analyze(
            "rule Bad:\n"
            "    pairs:\n"
            "        t: Tile\n"
            "        r: Walk\n"
            "    limit: 1 per r\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(analysis.has("`per r` names a relation row; a `per` part needs an entity binding"));
    }
    SECTION("each count reads only its own binding") {
        const auto ok = analyze(
            "rule Fine:\n" + kTilePairs +
            "    limit: a[Tile].x per a, b[Tile].x per b\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        INFO(describe(ok));
        CHECK(ok.clean());
        const auto bad = analyze(
            "rule Bad:\n" + kTilePairs +
            "    limit: 1 per a, a[Tile].x per b\n"
            "    on fixed_tick:\n"
            "        emit Done\n");
        CHECK(bad.has("a `per b` count may only read 'b'"));
    }
    SECTION("both bindings writable under one and one") {
        const auto analysis = analyze(
            "rule Pick:\n" + kClaimPairs +
            "    limit: 1 per robot, 1 per task\n"
            "    on fixed_tick:\n"
            "        robot[Robot].busy = true\n"
            "        task[Task].taken = true\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
        const auto& limit = *analysis.rule("Pick").limit;
        CHECK(limit.parts[0].provably_one);
        CHECK(limit.parts[1].provably_one);
    }
    SECTION("only the provably-one binding is writable") {
        const auto analysis = analyze(
            "rule Pick:\n" + kClaimPairs +
            "    limit: 1 per robot, 2 per task\n"
            "    on fixed_tick:\n"
            "        robot[Robot].busy = true\n"
            "        task[Task].taken = true\n");
        CHECK(analysis.has("pair-bound durable traits are read-only"));
        CHECK(std::ranges::count_if(analysis.diagnostics, [](const Diagnostic& d) {
                  return d.level == DiagnosticLevel::Error;
              }) == 1);
    }
}

// ── Contracts ───────────────────────────────────────────────────────────────

namespace {

std::size_t edges_between(const Analysis& analysis, const std::string& first, const std::string& second) {
    return static_cast<std::size_t>(
        std::ranges::count_if(analysis.program.execution_graph.schedule_edges, [&](const ScheduleEdge& edge) {
            const auto& before = edge.before.rule.local_name;
            const auto& after  = edge.after.rule.local_name;
            return (before == first && after == second) || (before == second && after == first);
        }));
}

}  // namespace

TEST_CASE("Relations: tests and row bindings are relation reads", "[semantic][relations][contracts]") {
    SECTION("a test reads the relation and no field") {
        const auto analysis = analyze(
            "rule Pick:\n" + kClaimPairs +
            "    where:\n"
            "        not Claim(robot, _)\n"
            "    on fixed_tick:\n"
            "        emit Done to robot\n");
        INFO(describe(analysis));
        const auto& contract = analysis.contract("Pick");
        CHECK(contract.reads.contains(relation_id("Claim")));
        REQUIRE(contract.read_fields.contains(relation_id("Claim")));
        CHECK_FALSE(contract.read_fields.at(relation_id("Claim")).all);
        CHECK(contract.read_fields.at(relation_id("Claim")).fields.empty());
    }
    SECTION("a row field read names its field") {
        const auto analysis = analyze(
            "rule Costs:\n"
            "    each: Walk as r\n"
            "    on fixed_tick:\n"
            "        emit Done to r.from\n"
            "        let c = r.cost\n");
        INFO(describe(analysis));
        REQUIRE(analysis.clean());
        const auto& contract = analysis.contract("Costs");
        CHECK(contract.domain_kind == HandlerDomainKind::Rows);
        REQUIRE(contract.pair_bindings.size() == 1);
        CHECK(contract.pair_bindings[0].relation == relation_id("Walk"));
        CHECK(contract.read_fields.at(relation_id("Walk")).fields == std::set<std::string>{"cost"});
    }
    SECTION("a pair row binding") {
        const auto analysis = analyze(
            "rule Follow:\n"
            "    pairs:\n"
            "        t: Tile\n"
            "        r: Walk\n"
            "    where:\n"
            "        r.from == t\n"
            "    on fixed_tick:\n"
            "        emit Done to t\n");
        INFO(describe(analysis));
        const auto& contract = analysis.contract("Follow");
        CHECK(contract.domain_kind == HandlerDomainKind::Pair);
        CHECK(contract.pair_bindings[1].relation == relation_id("Walk"));
        CHECK(contract.pair_bindings[1].required_traits.empty());
        CHECK(contract.reads.contains(relation_id("Walk")));
    }
}

TEST_CASE("Relations: a keeper and its readers order like a kept trait's", "[semantic][relations][contracts]") {
    const std::string reader =
        "rule Reader:\n" + kTilePairs +
        "    where:\n"
        "        Walk(a, b)\n"
        "    on fixed_tick:\n"
        "        emit Done to a\n";
    const auto relation = analyze(walk_keeper("    keep Walk(a, b)\n") + reader);
    INFO(describe(relation));
    REQUIRE(relation.clean());
    const auto trait = analyze(
        "rule FloorFact:\n"
        "    each: Tile\n"
        "    group: links\n"
        "    keep Floor\n"
        "rule Reader:\n"
        "    each:\n"
        "        Tile\n"
        "        Floor\n"
        "    on fixed_tick:\n"
        "        emit Done\n");
    INFO(describe(trait));
    REQUIRE(trait.clean());
    CHECK(edges_between(relation, "WalkLinks", "Reader") == edges_between(trait, "FloorFact", "Reader"));
}

TEST_CASE("Relations: unordered relation commands race like trait commands", "[semantic][relations][contracts]") {
    SECTION("add and remove") {
        const auto analysis = analyze("rule A:\n" + kClaimPairs +
                                      "    on fixed_tick:\n"
                                      "        add Seen(robot, task)\n"
                                      "rule B:\n" +
                                      kClaimPairs +
                                      "    on fixed_tick:\n"
                                      "        remove Seen(robot, _)\n");
        CHECK(analysis.has("on commands game.Seen: add / remove;"));
    }
    SECTION("two field adds") {
        const auto analysis = analyze("rule A:\n" + kTilePairs +
                                      "    on fixed_tick:\n"
                                      "        add Walk(a, b):\n"
                                      "            cost = 1.0\n"
                                      "rule B:\n" +
                                      kTilePairs +
                                      "    on fixed_tick:\n"
                                      "        add Walk(b, a):\n"
                                      "            cost = 2.0\n");
        CHECK(analysis.has("on commands game.Walk: add / add;"));
    }
    SECTION("marker adds are idempotent") {
        const auto analysis = analyze("rule A:\n" + kClaimPairs +
                                      "    on fixed_tick:\n"
                                      "        add Seen(robot, task)\n"
                                      "rule B:\n" +
                                      kClaimPairs +
                                      "    on fixed_tick:\n"
                                      "        add Seen(robot, task)\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
}

// ── Derived relations ───────────────────────────────────────────────────────

namespace {

const std::string kCore =
    "entity Core:\n"
    "    Tile\n";

const std::string kStep =
    "relation Step:\n"
    "    from: Tile\n"
    "    to: Tile\n"
    "    let cost: int = 1\n" +
    kCore;

}  // namespace

TEST_CASE("Derived relations: declarations take the source's end trait", "[semantic][relations][derived]") {
    const auto analysis = analyze(kStep +
                                  "relation Reach = closure(Walk)\n"
                                  "relation Route = shortest(Step, by = cost)\n"
                                  "relation Group = components(Bridge)\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& reach = analysis.program.relations.at("Reach");
    REQUIRE(reach.derivation.has_value());
    CHECK(reach.derivation->kind == DerivationKind::Closure);
    CHECK(reach.derivation->source == relation_id("Walk"));
    CHECK(reach.from.trait.local_name == "Tile");
    CHECK(reach.to.trait.local_name == "Tile");
    CHECK(reach.fields.empty());
    const auto& route = analysis.program.relations.at("Route");
    CHECK(route.derivation->cost_field == "cost");
    REQUIRE(route.fields.size() == 2);
    CHECK(route.find_field("next")->type.kind == TypeKind::EntityId);
    CHECK(route.find_field("distance")->type.kind == TypeKind::Int);
    const auto& group = analysis.program.relations.at("Group");
    CHECK(group.from.one);
    CHECK_FALSE(group.to.one);
}

TEST_CASE("Derived relations: a derived relation may derive from another", "[semantic][relations][derived]") {
    const auto analysis = analyze(
        "relation Group = components(Reach)\n"
        "relation Reach = closure(Walk)\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.program.relations.at("Group").derivation->source == relation_id("Reach"));
    CHECK(analysis.program.relations.at("Group").from.trait.local_name == "Tile");
}

TEST_CASE("Derived relations: declaration errors", "[semantic][relations][derived]") {
    SECTION("unknown source") {
        const auto analysis = analyze("relation Reach = closure(Nope)\n");
        CHECK(analysis.has("unknown relation 'Nope' in `closure(Nope)`"));
    }
    SECTION("ends differ") {
        const auto analysis = analyze("relation Chain = closure(Claim)\n");
        CHECK(analysis.has("`closure` needs one trait on both ends; 'Claim' has `from: Robot` and `to: Task`"));
    }
    SECTION("missing cost field") {
        const auto analysis = analyze(kStep + "relation Route = shortest(Step, by = speed)\n");
        CHECK(analysis.has("relation 'Step' has no field 'speed' for `by`"));
    }
    SECTION("float cost field") {
        const auto analysis = analyze("relation Route = shortest(Walk, by = cost)\n");
        CHECK(analysis.has("`by` needs an `int` field; 'Walk.cost' is 'float'; scale costs to integers"));
    }
    SECTION("derived from itself") {
        const auto analysis = analyze(
            "relation Left = closure(Right)\n"
            "relation Right = closure(Left)\n");
        CHECK(analysis.has("relation 'Left' is derived from itself"));
    }
    SECTION("keep of a derived relation") {
        const auto analysis = analyze("relation Reach = closure(Walk)\n"
                                      "rule ReachLinks:\n" +
                                      kTilePairs +
                                      "    group: links\n"
                                      "    keep Reach(a, b)\n");
        CHECK(analysis.has("'Reach' is derived from 'Walk' and is read-only"));
    }
    SECTION("add and remove of a derived relation") {
        const auto analysis = analyze("relation Reach = closure(Walk)\n"
                                      "rule Adder:\n" +
                                      kTilePairs +
                                      "    on fixed_tick:\n"
                                      "        add Reach(a, b)\n"
                                      "        remove Reach(a, _)\n");
        CHECK(std::ranges::count_if(analysis.diagnostics, [](const Diagnostic& d) {
                  return d.message.contains("'Reach' is derived from 'Walk' and is read-only");
              }) == 2);
    }
}

TEST_CASE("Derived relations: closure and shortest row bindings fix an end", "[semantic][relations][derived]") {
    const std::string relations = kStep +
                                  "relation Reach = closure(Walk)\n"
                                  "relation Route = shortest(Step, by = cost)\n"
                                  "relation Group = components(Walk)\n";
    const auto follow = [&](const std::string& relation, const std::string& where) {
        return analyze(relations + "rule Follow:\n" +
                       "    pairs:\n"
                       "        t: Tile\n"
                       "        r: " +
                       relation + "\n" + where +
                       "    on fixed_tick:\n"
                       "        emit Done to t\n");
    };
    SECTION("closure without an end equality") {
        const auto analysis = follow("Reach", "");
        CHECK(analysis.has("row binding 'r' of derived relation 'Reach' lists every pair; fix one end with "
                           "`r.from == ...` or `r.to == ...` in `where:`"));
    }
    SECTION("an inequality fixes nothing") {
        const auto analysis = follow("Route",
                                     "    where:\n"
                                     "        r.from != t\n");
        CHECK(analysis.has("row binding 'r' of derived relation 'Route' lists every pair"));
    }
    SECTION("from fixed") {
        const auto analysis = follow("Reach",
                                     "    where:\n"
                                     "        r.from == t\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("to fixed") {
        const auto analysis = follow("Route",
                                     "    where:\n"
                                     "        t == r.to\n"
                                     "        r.distance > 0\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("components need no end") {
        const auto analysis = follow("Group", "");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("unary row domain") {
        const auto unfixed = analyze(relations +
                                     "rule Spread:\n"
                                     "    each: Reach as r\n"
                                     "    on fixed_tick:\n"
                                     "        emit Done to r.to\n");
        CHECK(unfixed.has("row binding 'r' of derived relation 'Reach' lists every pair"));
        const auto fixed = analyze(relations +
                                   "rule Spread:\n"
                                   "    each: Reach as r\n"
                                   "    where:\n"
                                   "        r.from == Core\n"
                                   "    on fixed_tick:\n"
                                   "        emit Done to r.to\n");
        INFO(describe(fixed));
        CHECK(fixed.clean());
    }
    SECTION("tests need no end") {
        const auto analysis = analyze(relations + "rule Probe:\n" + kTilePairs +
                                      "    where:\n"
                                      "        Reach(a, b) or Route(a, _)\n"
                                      "    on fixed_tick:\n"
                                      "        emit Done to a\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
}

TEST_CASE("Derived relations: keep loops pass through derived relations", "[semantic][relations][derived][rule-keep]") {
    SECTION("a keeper reads the closure of its own relation") {
        const auto analysis = analyze("relation Reach = closure(Walk)\n" +
                                      walk_keeper("        not Reach(a, b)\n"
                                                  "    keep Walk(a, b)\n"));
        CHECK(analysis.has("keep rule 'WalkLinks' reads its own kept relation 'Walk'"));
    }
    SECTION("a loop through a closure") {
        const auto analysis = analyze("relation Reach = closure(Walk)\n"
                                      "rule WalkLinks:\n"
                                      "    pairs:\n"
                                      "        a:\n"
                                      "            Tile\n"
                                      "            Floor\n"
                                      "        b: Tile\n"
                                      "    group: links\n"
                                      "    keep Walk(a, b)\n"
                                      "rule FloorFact:\n"
                                      "    each: Tile\n"
                                      "    group: links\n"
                                      "    where:\n"
                                      "        Reach(self, _)\n"
                                      "    keep Floor\n");
        CHECK(analysis.has("keep rules form a loop through their kept traits and relations"));
    }
    SECTION("a chain through a closure orders the keepers") {
        const auto analysis = analyze("relation Reach = closure(Walk)\n" + kCore + walk_keeper("    keep Walk(a, b)\n") +
                                      "rule ConnectedFact:\n"
                                      "    each: Tile\n"
                                      "    group: links\n"
                                      "    where:\n"
                                      "        Reach(Core, self) or self == Core\n"
                                      "    keep Connected\n");
        INFO(describe(analysis));
        REQUIRE(analysis.clean());
        CHECK(analysis.contract("ConnectedFact").reads.contains(relation_id("Walk")));
    }
}

TEST_CASE("Derived relations: reads are reads of the source", "[semantic][relations][derived][contracts]") {
    SECTION("closure test") {
        const auto analysis = analyze("relation Reach = closure(Walk)\n"
                                      "rule Probe:\n" +
                                      kTilePairs +
                                      "    where:\n"
                                      "        Reach(a, b)\n"
                                      "    on fixed_tick:\n"
                                      "        emit Done to a\n");
        INFO(describe(analysis));
        CHECK(analysis.contract("Probe").reads.contains(relation_id("Walk")));
    }
    SECTION("shortest row binding") {
        const auto analysis = analyze(kStep +
                                      "relation Route = shortest(Step, by = cost)\n"
                                      "rule Follow:\n"
                                      "    pairs:\n"
                                      "        t: Tile\n"
                                      "        r: Route\n"
                                      "    where:\n"
                                      "        r.to == t\n"
                                      "    on fixed_tick:\n"
                                      "        emit Done to r.next\n");
        INFO(describe(analysis));
        const auto& contract = analysis.contract("Follow");
        CHECK(contract.reads.contains(relation_id("Step")));
        REQUIRE(contract.read_fields.contains(relation_id("Step")));
        CHECK(contract.read_fields.at(relation_id("Step")).fields.contains("cost"));
    }
    SECTION("through two derivations") {
        const auto analysis = analyze("relation Reach = closure(Walk)\n"
                                      "relation Group = components(Reach)\n"
                                      "rule Probe:\n" +
                                      kTilePairs +
                                      "    where:\n"
                                      "        Group(a, b)\n"
                                      "    on fixed_tick:\n"
                                      "        emit Done to a\n");
        const auto& contract = analysis.contract("Probe");
        CHECK(contract.reads.contains(relation_id("Reach")));
        CHECK(contract.reads.contains(relation_id("Walk")));
    }
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
