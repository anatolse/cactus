// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace cactus;

namespace {

const std::string kPrelude =
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "group contacts:\n"
    "    phase: tick\n"
    "trait Health:\n"
    "    var hp: int = 10\n"
    "trait Link:\n"
    "    var target: entity_id\n"
    "trait Body\n"
    "trait Crate\n"
    "trait Dying\n"
    "trait Broken\n"
    "trait Flash\n"
    "trait Stats:\n"
    "    var n: int = 0\n"
    "template Enemy:\n"
    "    Health\n"
    "entity Game:\n"
    "    Stats\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] const HandlerContract& contract(const std::string& rule_name) const {
        const auto found = std::ranges::find_if(program.execution_graph.handlers, [&](const HandlerNode& handler) {
            return handler.identity.rule.local_name == rule_name;
        });
        REQUIRE(found != program.execution_graph.handlers.end());
        return found->contract;
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
        UNSCOPED_INFO(std::to_string(diagnostic.location.line) + ": " + diagnostic.message);
    }
    REQUIRE_FALSE(errors.has_errors());
    return analysis;
}

const InferredHandlerCommand& command(const HandlerContract& contract,
                                      HandlerCommandKind kind,
                                      const std::string& trait = "") {
    const auto found = std::ranges::find_if(contract.commands, [&](const InferredHandlerCommand& entry) {
        return entry.kind == kind && (trait.empty() || (entry.target.has_value() && entry.target->local_name == trait));
    });
    REQUIRE(found != contract.commands.end());
    return *found;
}

EntityTarget self_target() {
    return EntityTarget{.kind = EntityTargetKind::Self};
}

EntityTarget unknown_target() {
    return EntityTarget{.kind = EntityTargetKind::Unknown};
}

SymbolId game_symbol(SymbolKind kind, const std::string& name) {
    return SymbolId{.kind = kind, .module = ModuleId{.name = "game"}, .local_name = name};
}

}  // namespace

TEST_CASE("Command targets: a command on the rule's own entity is Self", "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "rule Doom:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        add Dying\n"
        "        remove Broken from self\n"
        "        project Flash\n"
        "        destroy\n");
    const auto& contract = analysis.contract("Doom");
    CHECK(command(contract, HandlerCommandKind::Add, "Dying").entities == std::vector{self_target()});
    CHECK(command(contract, HandlerCommandKind::Remove, "Broken").entities == std::vector{self_target()});
    CHECK(command(contract, HandlerCommandKind::Destroy).entities == std::vector{self_target()});
    REQUIRE(contract.projections.size() == 1);
    CHECK(contract.projections[0].trait.local_name == "Flash");
    CHECK(contract.projections[0].entities == std::vector{self_target()});
}

TEST_CASE("Command targets: a command on a pair binding records its index", "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "rule Smash:\n"
        "    pairs:\n"
        "        body:\n"
        "            Body\n"
        "        box:\n"
        "            Crate\n"
        "    on tick:\n"
        "        add Broken to box\n"
        "        remove Dying from body\n");
    const auto& contract = analysis.contract("Smash");
    CHECK(command(contract, HandlerCommandKind::Add, "Broken").entities ==
          std::vector{EntityTarget{.kind = EntityTargetKind::PairBinding, .binding_index = 1}});
    CHECK(command(contract, HandlerCommandKind::Remove, "Dying").entities ==
          std::vector{EntityTarget{.kind = EntityTargetKind::PairBinding, .binding_index = 0}});
}

TEST_CASE("Command targets: a command on a named entity records the entity", "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "rule Mark:\n"
        "    on tick:\n"
        "        add Dying to Game\n");
    CHECK(command(analysis.contract("Mark"), HandlerCommandKind::Add, "Dying").entities ==
          std::vector{EntityTarget{.kind   = EntityTargetKind::Named,
                                   .symbol = game_symbol(SymbolKind::Entity, "Game")}});
}

TEST_CASE("Command targets: a command on a same-handler spawn records the template",
          "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "rule Wave:\n"
        "    on tick:\n"
        "        let fresh = spawn Enemy()\n"
        "        add Dying to fresh\n");
    const auto& contract = analysis.contract("Wave");
    CHECK(command(contract, HandlerCommandKind::Add, "Dying").entities ==
          std::vector{EntityTarget{.kind   = EntityTargetKind::Spawned,
                                   .symbol = game_symbol(SymbolKind::Template, "Enemy")}});
    CHECK(command(contract, HandlerCommandKind::Spawn).entities.empty());
}

TEST_CASE("Command targets: an entity_id value is Unknown", "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "rule Chain:\n"
        "    each:\n"
        "        Link\n"
        "    on tick:\n"
        "        add Dying to target\n"
        "        destroy target\n");
    const auto& contract = analysis.contract("Chain");
    CHECK(command(contract, HandlerCommandKind::Add, "Dying").entities == std::vector{unknown_target()});
    CHECK(command(contract, HandlerCommandKind::Destroy).entities == std::vector{unknown_target()});
}

TEST_CASE("Command targets: a local bound twice is Unknown", "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "rule Wave:\n"
        "    each:\n"
        "        Link\n"
        "    on tick:\n"
        "        if true:\n"
        "            let fresh = target\n"
        "            add Broken to fresh\n"
        "        let fresh = spawn Enemy()\n"
        "        add Dying to fresh\n");
    const auto& contract = analysis.contract("Wave");
    CHECK(command(contract, HandlerCommandKind::Add, "Broken").entities == std::vector{unknown_target()});
    CHECK(command(contract, HandlerCommandKind::Add, "Dying").entities == std::vector{unknown_target()});
}

TEST_CASE("Command targets: one command on two entity classes keeps both", "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "rule Spread:\n"
        "    each:\n"
        "        Link\n"
        "    on tick:\n"
        "        add Dying\n"
        "        add Dying to target\n");
    CHECK(command(analysis.contract("Spread"), HandlerCommandKind::Add, "Dying").entities ==
          std::vector{self_target(), unknown_target()});
}

TEST_CASE("Command targets: extern rule commands are Unknown; its projections act on its own entity",
          "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "extern rule HostMark:\n"
        "    each: Flash\n"
        "    on tick:\n"
        "        projects:\n"
        "            Flash\n"
        "        commands:\n"
        "            add Dying\n"
        "            destroy\n");
    const auto& contract = analysis.contract("HostMark");
    CHECK(command(contract, HandlerCommandKind::Add, "Dying").entities == std::vector{unknown_target()});
    CHECK(command(contract, HandlerCommandKind::Destroy).entities == std::vector{unknown_target()});
    REQUIRE(contract.projections.size() == 1);
    CHECK(contract.projections[0].entities == std::vector{self_target()});
}

TEST_CASE("Command targets: a keep clause acts on its group binding", "[semantic][archetype-reachability]") {
    const auto analysis = analyze(
        "trait Near:\n"
        "    var n: int = 0\n"
        "rule Touch:\n"
        "    pairs:\n"
        "        body:\n"
        "            Body\n"
        "        box:\n"
        "            Crate\n"
        "    group: contacts\n"
        "    reduce per box:\n"
        "        n = count()\n"
        "    keep Near on box:\n"
        "        n = n\n");
    const auto expected = std::vector{EntityTarget{.kind = EntityTargetKind::PairBinding, .binding_index = 1}};
    const auto& contract = analysis.contract("Touch");
    CHECK(command(contract, HandlerCommandKind::Add, "Near").entities == expected);
    CHECK(command(contract, HandlerCommandKind::Remove, "Near").entities == expected);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
