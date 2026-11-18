// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/keep_inputs.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace cactus;

namespace {

const std::string kPrelude =
    "extern event frame:\n"
    "    dt: float\n"
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "group keeps:\n"
    "    phase: fixed_tick\n"
    "trait Unit:\n"
    "    let id: int = 0\n"
    "    var x: int = 0\n"
    "    var y: int = 0\n"
    "trait Frozen\n"
    "trait Marked\n"
    "trait Seen\n"
    "trait Stats:\n"
    "    var limit: int = 0\n"
    "relation Link:\n"
    "    from: Unit\n"
    "    to: Unit\n"
    "relation Reach = closure(Link)\n"
    "relation Kept:\n"
    "    from: Unit\n"
    "    to: Unit\n"
    "relation Echo:\n"
    "    from: Unit\n"
    "    to: Unit\n"
    "relation After:\n"
    "    from: Unit\n"
    "    to: Unit\n"
    "entity Board:\n"
    "    Stats\n"
    "entity Root:\n"
    "    Unit\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] KeepInputs inputs(const std::string& rule) const {
        const auto found = std::ranges::find_if(program.handler_contracts,
                                                [&](const auto& contract) { return contract.rule.local_name == rule; });
        REQUIRE(found != program.handler_contracts.end());
        const auto trait = [&](const SymbolId& symbol) -> const ResolvedTrait* {
            const auto it = program.traits.find(symbol.local_name);
            return it == program.traits.end() ? nullptr : &it->second;
        };
        const auto relation = [&](const SymbolId& symbol) -> const ResolvedRelation* {
            const auto it = program.relations.find(symbol.local_name);
            return it == program.relations.end() ? nullptr : &it->second;
        };
        return keep_inputs(*found, trait, relation);
    }

    [[nodiscard]] KeepEvaluation evaluation(const std::string& rule) const {
        const auto contract = std::ranges::find_if(
            program.handler_contracts, [&](const auto& candidate) { return candidate.rule.local_name == rule; });
        REQUIRE(contract != program.handler_contracts.end());
        const RuleNode* node = nullptr;
        for (const auto& decl : ast->declarations) {
            if (const auto* candidate = std::get_if<RuleNode>(&decl); candidate != nullptr && candidate->name == rule) {
                node = candidate;
            }
        }
        REQUIRE(node != nullptr);
        const auto relation = [&](const SymbolId& symbol) -> const ResolvedRelation* {
            const auto it = program.relations.find(symbol.local_name);
            return it == program.relations.end() ? nullptr : &it->second;
        };
        return classify_keep(*node, *contract, relation);
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
    for (const auto& diagnostic : analysis.diagnostics) {
        INFO(diagnostic.message);
        CHECK(diagnostic.level != DiagnosticLevel::Error);
    }
    return analysis;
}

std::vector<std::string> names(const std::vector<SymbolId>& symbols) {
    std::vector<std::string> result;
    result.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        result.push_back(symbol.local_name);
    }
    return result;
}

std::vector<std::string> names(const std::vector<TraitFieldRef>& fields) {
    std::vector<std::string> result;
    result.reserve(fields.size());
    for (const auto& field : fields) {
        result.push_back(field.trait.local_name + "." + field.field.value_or("*"));
    }
    return result;
}

}  // namespace

TEST_CASE("keep inputs: a unary keep reads its domain and only its var fields", "[keep-inputs]") {
    const auto analysis = analyze(
        "rule Odd:\n"
        "    each: Unit as u\n"
        "    without:\n"
        "        Frozen\n"
        "    group: keeps\n"
        "    where:\n"
        "        u.x % 2 == 1 and u.id > 0\n"
        "    keep Marked\n");
    const auto inputs = analysis.inputs("Odd");
    CHECK(names(inputs.traits) == std::vector<std::string>{"Frozen", "Unit"});
    CHECK(names(inputs.fields) == std::vector<std::string>{"Unit.x"});
    CHECK(inputs.relations.empty());
}

TEST_CASE("keep inputs: a keep over let fields has no field inputs", "[keep-inputs]") {
    const auto analysis = analyze(
        "rule Pairs:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        b: Unit\n"
        "    group: keeps\n"
        "    where:\n"
        "        b[Unit].id == a[Unit].id + 1\n"
        "    keep Kept(a, b)\n");
    const auto inputs = analysis.inputs("Pairs");
    CHECK(names(inputs.traits) == std::vector<std::string>{"Unit"});
    CHECK(inputs.fields.empty());
}

TEST_CASE("keep inputs: named reads are inputs", "[keep-inputs]") {
    const auto analysis = analyze(
        "rule Limited:\n"
        "    each: Unit as u\n"
        "    group: keeps\n"
        "    where:\n"
        "        u.y < Board[Stats].limit\n"
        "    keep Marked\n");
    const auto inputs = analysis.inputs("Limited");
    CHECK(names(inputs.traits) == std::vector<std::string>{"Stats", "Unit"});
    CHECK(names(inputs.fields) == std::vector<std::string>{"Stats.limit", "Unit.y"});
    CHECK(inputs.relations.empty());
}

TEST_CASE("keep inputs: a relation test reads the relation", "[keep-inputs]") {
    const auto analysis = analyze(
        "rule Linked:\n"
        "    each: Unit as u\n"
        "    group: keeps\n"
        "    where:\n"
        "        Link(u, _)\n"
        "    keep Marked\n");
    const auto inputs = analysis.inputs("Linked");
    CHECK(names(inputs.relations) == std::vector<std::string>{"Link"});
}

TEST_CASE("keep inputs: a derived relation is replaced by its source and members", "[keep-inputs]") {
    const auto analysis = analyze(
        "rule Reached:\n"
        "    each: Reach as r\n"
        "    group: keeps\n"
        "    where: r.from == Root\n"
        "    keep Kept(r.from, r.to)\n");
    const auto inputs = analysis.inputs("Reached");
    CHECK(names(inputs.traits) == std::vector<std::string>{"Unit"});
    CHECK(names(inputs.relations) == std::vector<std::string>{"Link"});
}

std::string full_reason(const Analysis& analysis, const std::string& rule) {
    const auto evaluation = analysis.evaluation(rule);
    CHECK(evaluation.level == KeepLevel::Full);
    return evaluation.reason;
}

TEST_CASE("keep levels: own-binding unary and equality pair keeps are delta", "[keep-levels]") {
    const auto analysis = analyze(
        "rule Odd:\n"
        "    each: Unit as u\n"
        "    group: keeps\n"
        "    where:\n"
        "        u.x % 2 == 1\n"
        "    keep Marked\n"
        "    when:\n"
        "        Board[Stats].limit > 0\n"
        "rule Pairs:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        b: Unit\n"
        "    group: keeps\n"
        "    where:\n"
        "        b[Unit].id == a[Unit].id + 1\n"
        "    keep Kept(a, b)\n");
    CHECK(analysis.evaluation("Odd").level == KeepLevel::Delta);
    CHECK(analysis.evaluation("Pairs").level == KeepLevel::Delta);
}

TEST_CASE("keep levels: each construct that reads beyond the row is named", "[keep-levels]") {
    const auto analysis = analyze(
        "rule Limited:\n"
        "    each: Unit as u\n"
        "    group: keeps\n"
        "    where:\n"
        "        u.y < Board[Stats].limit\n"
        "    keep Marked\n"
        "rule Linked:\n"
        "    each: Unit as u\n"
        "    group: keeps\n"
        "    where:\n"
        "        Link(u, _)\n"
        "    keep Frozen\n"
        "rule Reached:\n"
        "    each: Unit as u\n"
        "    group: keeps\n"
        "    where:\n"
        "        Reach(Root, u)\n"
        "    keep Seen\n");
    CHECK(full_reason(analysis, "Limited") == "the named entity read `Board[Stats]`");
    CHECK(full_reason(analysis, "Linked") == "the relation test `Link`");
    CHECK(full_reason(analysis, "Reached") == "the derived relation `Reach`");
}

TEST_CASE("keep levels: a keep reads its own cells at the delta level and a single trait's at full",
          "[keep-levels][trait-dimensions]") {
    const auto analysis = analyze(
        "trait Stock:\n"
        "    dim slot: int in range(0, 3)\n"
        "    var count: int = 0\n"
        "single trait Gas:\n"
        "    dim x: int in range(0, 4)\n"
        "    var density: float = 0.0\n"
        "entity Store:\n"
        "    Stock\n"
        "entity Cloud:\n"
        "    Gas\n"
        "rule Stocked:\n"
        "    each: Stock\n"
        "    group: keeps\n"
        "    where:\n"
        "        self[Stock][1].count > 0\n"
        "    keep Marked\n"
        "rule Gassy:\n"
        "    each: Unit as u\n"
        "    group: keeps\n"
        "    where:\n"
        "        Gas[u.x].density > 0.3\n"
        "    keep Seen\n");
    CHECK(analysis.evaluation("Stocked").level == KeepLevel::Delta);
    CHECK(names(analysis.inputs("Stocked").fields) == std::vector<std::string>{"Stock.count"});
    CHECK(full_reason(analysis, "Gassy") == "the single trait read `Gas[...]`");
    CHECK(names(analysis.inputs("Gassy").fields) == std::vector<std::string>{"Gas.density", "Unit.x"});
}

TEST_CASE("keep levels: a row domain over a stored relation is delta", "[keep-levels]") {
    const auto analysis = analyze(
        "rule Rows:\n"
        "    each: Link as l\n"
        "    group: keeps\n"
        "    keep Kept(l.to, l.from)\n");
    CHECK(analysis.evaluation("Rows").level == KeepLevel::Delta);
}

TEST_CASE("keep levels: a read through a relation end is named", "[keep-levels]") {
    const auto analysis = analyze(
        "rule Ahead:\n"
        "    each: Link as l\n"
        "    group: keeps\n"
        "    where:\n"
        "        l.to[Unit].x > l.from[Unit].x\n"
        "    keep Kept(l.to, l.from)\n");
    CHECK(full_reason(analysis, "Ahead") == "the read through relation end `l.to`");
    CHECK(names(analysis.inputs("Ahead").traits) == std::vector<std::string>{"Unit"});
}

TEST_CASE("keep levels: derived row domains, row bindings and products are full", "[keep-levels]") {
    const auto analysis = analyze(
        "rule Rows:\n"
        "    each: Reach as r\n"
        "    group: keeps\n"
        "    where: r.from == Root\n"
        "    keep Kept(r.from, r.to)\n"
        "rule Mixed:\n"
        "    pairs:\n"
        "        u: Unit\n"
        "        l: Link\n"
        "    group: keeps\n"
        "    where: l.from == u\n"
        "    keep Echo(u, l.to)\n"
        "rule Behind:\n"
        "    pairs:\n"
        "        a: Unit\n"
        "        b: Unit\n"
        "    group: keeps\n"
        "    where:\n"
        "        b[Unit].x < a[Unit].x\n"
        "    keep After(a, b)\n");
    CHECK(full_reason(analysis, "Rows") == "the row domain over the derived relation `Reach`");
    CHECK(full_reason(analysis, "Mixed") == "the row binding `l`");
    CHECK(full_reason(analysis, "Behind") == "the pair plan, which can't look up rows from both bindings");
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access)
