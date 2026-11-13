// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <initializer_list>
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
    "event Struck\n"
    "trait Unit:\n"
    "    var health: float = 10.0\n"
    "trait Position:\n"
    "    var x: float = 0.0\n"
    "state Order:\n"
    "    Auto\n"
    "    Attack:\n"
    "        let target: Unit\n"
    "template Grunt:\n"
    "    Unit\n"
    "    Position\n"
    "    Order\n"
    "entity Hero:\n"
    "    Unit\n"
    "rule Wake:\n"
    "    on tick:\n"
    "        spawn Grunt()\n";

// Picks an attack target; every program below uses it to make Attack reachable.
const std::string kEngage =
    "rule Engage:\n"
    "    pairs:\n"
    "        attacker:\n"
    "            Unit\n"
    "            Order.Auto\n"
    "        enemy: Unit\n"
    "    where: attacker != enemy\n"
    "    limit: 1 per attacker\n"
    "    on tick:\n"
    "        add Order.Attack to attacker:\n"
    "            target = enemy\n";

// Destroys units, so a link to Unit can be lost.
const std::string kKill =
    "rule Kill:\n"
    "    each: Unit as unit\n"
    "    where: unit.health <= 0.0\n"
    "    on tick:\n"
    "        destroy\n";

const std::string kRetreat =
    "rule Retreat:\n"
    "    each: Unit\n"
    "    on lost Order.Attack.target:\n"
    "        add Order.Auto\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] const Diagnostic* error(const std::string& text) const {
        const auto found = std::ranges::find_if(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error && d.message.contains(text);
        });
        return found == diagnostics.end() ? nullptr : &*found;
    }
    [[nodiscard]] bool has_all(std::initializer_list<std::string> parts) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error &&
                   std::ranges::all_of(parts, [&](const std::string& part) { return d.message.contains(part); });
        });
    }
    [[nodiscard]] bool clean() const {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ":" + std::to_string(d.location.column) + ": " + d.message + "\n";
    }
    return text;
}

std::vector<Diagnostic> parse_errors(const std::string& source, std::unique_ptr<ProgramNode>& ast) {
    ErrorReporter errors;
    Lexer lexer(source, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    ast = std::make_unique<ProgramNode>(parser.parse_program());
    return errors.diagnostics();
}

Analysis analyze_source(const std::string& source) {
    Analysis analysis;
    analysis.diagnostics = parse_errors(source, analysis.ast);
    INFO(describe(analysis));
    REQUIRE(analysis.diagnostics.empty());
    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analysis.program = analyzer.analyze(*analysis.ast);
    if (!errors.has_errors()) {
        run_whole_program_passes(analysis.program, errors);
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

Analysis analyze(const std::string& body) {
    return analyze_source(kPrelude + body);
}

const InferredHandlerContract& contract_of(const Analysis& analysis, const std::string& rule_name) {
    const auto found = std::ranges::find_if(analysis.program.handler_contracts,
                                            [&](const auto& contract) { return contract.rule.local_name == rule_name; });
    REQUIRE(found != analysis.program.handler_contracts.end());
    return *found;
}

const FieldAccess& reads_of(const HandlerContract& contract, const std::string& trait) {
    const auto found = std::ranges::find_if(contract.read_fields,
                                            [&](const auto& entry) { return entry.first.local_name == trait; });
    REQUIRE(found != contract.read_fields.end());
    return found->second;
}

const ResolvedField& field_of(const DecoratedProgram& program, const std::string& trait, const std::string& field) {
    const auto& fields = program.traits.at(trait).fields;
    const auto found   = std::ranges::find(fields, field, &ResolvedField::name);
    REQUIRE(found != fields.end());
    return *found;
}

}  // namespace

// ── Parsing ─────────────────────────────────────────────────────────────────

TEST_CASE("Parser: on lost names a link field", "[parser][required-links]") {
    std::unique_ptr<ProgramNode> ast;
    const auto errors = parse_errors(kPrelude + kRetreat +
                                         "rule Note:\n"
                                         "    each: Unit\n"
                                         "    on lost Order.Attack.target as old:\n"
                                         "        add Order.Auto\n",
                                     ast);
    REQUIRE(errors.empty());
    std::vector<const EventHandlerNode*> handlers;
    for (const auto& decl : ast->declarations) {
        if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name != "Wake") {
            handlers.push_back(&rule->handlers.front());
        }
    }
    REQUIRE(handlers.size() == 2);
    CHECK(handlers[0]->trigger_form == HandlerTriggerForm::Lost);
    CHECK(handlers[0]->event_name == "Order.Attack.target");
    CHECK_FALSE(handlers[0]->alias.has_value());
    CHECK(handlers[1]->trigger_form == HandlerTriggerForm::Lost);
    CHECK(handlers[1]->alias == "old");
}

TEST_CASE("Semantic: lost stays an ordinary identifier", "[semantic][required-links]") {
    const auto analysis = analyze(kEngage +
        "trait Tally:\n"
        "    var lost: int = 0\n"
        "entity Board:\n"
        "    Tally\n"
        "rule Count:\n"
        "    each: Tally as t\n"
        "    on tick:\n"
        "        let lost = t.lost + 1\n"
        "        t.lost = lost\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

// ── Link fields ─────────────────────────────────────────────────────────────

TEST_CASE("Semantic: a variant field can link to a trait", "[semantic][required-links]") {
    const auto analysis = analyze(kEngage);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& target = field_of(analysis.program, "Order.Attack", "target");
    CHECK(target.type.kind == TypeKind::EntityId);
    REQUIRE(target.type.symbol_id.has_value());
    CHECK(target.type.symbol_id->local_name == "Unit");
}

TEST_CASE("Semantic: trait fields and template parameters can link", "[semantic][required-links]") {
    const auto analysis = analyze(kEngage +
        "trait Follow:\n"
        "    var follow: Unit\n"
        "template Shadow(leader: Unit):\n"
        "    Follow:\n"
        "        follow = leader\n"
        "rule Haunt:\n"
        "    each: Unit\n"
        "    on tick:\n"
        "        spawn Shadow(leader = self)\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Semantic: a link type is rejected outside trait, variant and template fields",
          "[semantic][required-links]") {
    const auto rejected = [](const std::string& body) {
        const auto analysis = analyze(body);
        INFO(describe(analysis));
        CHECK(analysis.has_all({"link", "only on trait, state variant and template parameter fields"}));
    };
    SECTION("an event field") {
        rejected("event Aimed:\n    at: Unit\n");
    }
    SECTION("a struct field") {
        rejected("struct Aim:\n    at: Unit\n");
    }
    SECTION("a relation field") {
        rejected("relation Sees:\n    from: Unit\n    to: Unit\n    let at: Unit\n");
    }
    SECTION("a func return type") {
        rejected("func pick() Unit:\n    return Hero\n");
    }
    SECTION("a const") {
        rejected("const:\n    FIRST: Unit = Hero\n");
    }
    SECTION("a phase field") {
        rejected("phase aim:\n    after:\n        tick\n    at: Unit = Hero\n");
    }
}

TEST_CASE("Semantic: a link field can't be persist or have a default", "[semantic][required-links]") {
    SECTION("persist") {
        const auto analysis = analyze("trait Follow:\n    persist let follow: Unit\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"link field 'follow'", "can't be `persist`"}));
    }
    SECTION("default") {
        const auto analysis = analyze("trait Follow:\n    let follow: Unit = Hero\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"link field 'follow'", "can't have a default"}));
    }
}

TEST_CASE("Semantic: a projected trait can't hold or be the target of a link", "[semantic][required-links]") {
    SECTION("projected target") {
        const auto analysis = analyze(
            "trait Glow:\n"
            "    var level: float = 0.0\n"
            "trait Follow:\n"
            "    let follow: Glow\n"
            "rule Light:\n"
            "    each: Unit\n"
            "    on tick:\n"
            "        project Glow\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"link field 'follow'", "projected trait 'Glow'"}));
    }
    SECTION("projected holder") {
        const auto analysis = analyze(
            "trait Follow:\n"
            "    let follow: Unit\n"
            "rule Light:\n"
            "    each: Unit\n"
            "    on tick:\n"
            "        project Follow:\n"
            "            follow = self\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"can't project 'Follow'", "link field 'follow'"}));
    }
}

TEST_CASE("Semantic: every place that makes a holder assigns its link", "[semantic][required-links]") {
    SECTION("an add") {
        const auto analysis = analyze(
            "rule Charge:\n"
            "    each: Unit\n"
            "    on tick:\n"
            "        add Order.Attack\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'target' must be assigned"}));
    }
    SECTION("an entity") {
        const auto analysis = analyze(
            "trait Follow:\n"
            "    let follow: Unit\n"
            "entity Ghost:\n"
            "    Follow\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'follow' must be assigned"}));
    }
}

// ── Link values ─────────────────────────────────────────────────────────────

TEST_CASE("Semantic: link values come from proven sources", "[semantic][required-links]") {
    SECTION("self in a rule over the target trait") {
        const auto analysis = analyze(kEngage +
            "trait Follow:\n"
            "    var follow: Unit\n"
            "rule Mark:\n"
            "    each: Unit\n"
            "    on tick:\n"
            "        add Follow:\n"
            "            follow = self\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("a named entity that declares the trait") {
        const auto analysis = analyze(
            "rule Charge:\n"
            "    each: Unit, Order.Auto\n"
            "    on tick:\n"
            "        add Order.Attack:\n"
            "            target = Hero\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("another link of the same type") {
        const auto analysis = analyze(kEngage +
                                      "trait Follow:\n"
                                      "    var follow: Unit\n"
                                      "rule Copy:\n"
                                      "    each: Order.Attack as order\n"
                                      "    on tick:\n"
                                      "        add Follow:\n"
                                      "            follow = order.target\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("a best over a binding that requires the trait") {
        const auto analysis = analyze(
            "rule Pick:\n"
            "    pairs:\n"
            "        attacker:\n"
            "            Unit\n"
            "            Order.Auto\n"
            "        enemy: Unit as foe\n"
            "    where: attacker != enemy\n"
            "    reduce per attacker:\n"
            "        weakest = best(enemy, by = -foe.health)\n"
            "    on tick:\n"
            "        add Order.Attack to attacker:\n"
            "            target = weakest\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("an event's entity_id is not a link") {
        const auto analysis = analyze(
            "event Aimed:\n"
            "    at: entity_id\n"
            "rule Send:\n"
            "    on tick:\n"
            "        emit Aimed:\n"
            "            at = Hero\n"
            "rule Charge:\n"
            "    each: Unit, Order.Auto\n"
            "    on Aimed as aimed:\n"
            "        add Order.Attack:\n"
            "            target = aimed.at\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'target'", "join the target in `pairs:`", "'Unit'"}));
    }
    SECTION("a binding that does not require the trait") {
        const auto analysis = analyze(
            "rule Engage:\n"
            "    pairs:\n"
            "        attacker:\n"
            "            Unit\n"
            "            Order.Auto\n"
            "        spot: Position\n"
            "    limit: 1 per attacker\n"
            "    on tick:\n"
            "        add Order.Attack to attacker:\n"
            "            target = spot\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'target'", "join the target in `pairs:`"}));
    }
}

TEST_CASE("Semantic: a link stands where an entity_id is expected", "[semantic][required-links]") {
    const auto analysis = analyze(kEngage + kKill + kRetreat +
                                  "rule Strike:\n"
                                  "    each: Order.Attack as order\n"
                                  "    on tick:\n"
                                  "        if order.target != self:\n"
                                  "            emit Struck to order.target\n"
                                  "rule Hurt:\n"
                                  "    each: Unit as unit\n"
                                  "    on Struck:\n"
                                  "        unit.health -= 1.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

// ── Losing links ────────────────────────────────────────────────────────────

TEST_CASE("Semantic: a link the program can lose needs an on lost handler", "[semantic][required-links]") {
    SECTION("missing handler") {
        const auto analysis = analyze(kEngage + kKill);
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'game.Order.Attack.target'", "can be lost", "'game.Kill'", "on lost"}));
    }
    SECTION("covered") {
        const auto analysis = analyze(kEngage + kKill + kRetreat);
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("a link the program can't lose needs no handler") {
        const auto analysis = analyze(kEngage);
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("removing the target trait loses the link") {
        const auto analysis = analyze(kEngage +
                                      "rule Disarm:\n"
                                      "    each: Unit as unit\n"
                                      "    where: unit.health <= 0.0\n"
                                      "    on tick:\n"
                                      "        remove Unit\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'game.Order.Attack.target'", "can be lost", "'game.Disarm'"}));
    }
}

TEST_CASE("Semantic: every reachable holder must be covered", "[semantic][required-links]") {
    const auto analysis = analyze(kEngage + kKill +
                                  "rule Retreat:\n"
                                  "    each: Unit\n"
                                  "    without: Position\n"
                                  "    on lost Order.Attack.target:\n"
                                  "        add Order.Auto\n");
    INFO(describe(analysis));
    CHECK(analysis.has_all({"no `on lost Order.Attack.target` handler covers"}));
}

TEST_CASE("Semantic: an on lost handler resolves on every path", "[semantic][required-links]") {
    SECTION("if without else") {
        const auto analysis = analyze(kEngage + kKill +
                                      "rule Retreat:\n"
                                      "    each: Unit as unit\n"
                                      "    on lost Order.Attack.target:\n"
                                      "        if unit.health > 1.0:\n"
                                      "            add Order.Auto\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"may finish without resolving"}));
    }
    SECTION("if with else, destroy and a new target") {
        const auto analysis = analyze(kEngage + kKill +
                                      "rule Retreat:\n"
                                      "    each: Unit as unit\n"
                                      "    on lost Order.Attack.target:\n"
                                      "        if unit.health > 5.0:\n"
                                      "            add Order.Attack:\n"
                                      "                target = Hero\n"
                                      "        else if unit.health > 1.0:\n"
                                      "            remove Order\n"
                                      "        else:\n"
                                      "            destroy\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
    SECTION("a return before resolving") {
        const auto analysis = analyze(kEngage + kKill +
                                      "rule Retreat:\n"
                                      "    each: Unit as unit\n"
                                      "    on lost Order.Attack.target:\n"
                                      "        if unit.health > 1.0:\n"
                                      "            return\n"
                                      "        add Order.Auto\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"may finish without resolving"}));
    }
    SECTION("a trait match without a wildcard arm") {
        const auto analysis = analyze(kEngage + kKill +
                                      "rule Retreat:\n"
                                      "    each: Unit\n"
                                      "    on lost Order.Attack.target:\n"
                                      "        match self:\n"
                                      "            Position =>\n"
                                      "                add Order.Auto\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"may finish without resolving"}));
    }
    SECTION("a trait match with a wildcard arm") {
        const auto analysis = analyze(kEngage + kKill +
                                      "rule Retreat:\n"
                                      "    each: Unit\n"
                                      "    on lost Order.Attack.target:\n"
                                      "        match self:\n"
                                      "            Position =>\n"
                                      "                add Order.Auto\n"
                                      "            _ =>\n"
                                      "                destroy\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
}

TEST_CASE("Semantic: on lost names a link field and needs a unary rule", "[semantic][required-links]") {
    SECTION("not a link") {
        const auto analysis = analyze(
            "rule Odd:\n"
            "    each: Unit\n"
            "    on lost Unit.health:\n"
            "        destroy\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Unit.health' is not a link field"}));
    }
    SECTION("a pairs rule") {
        const auto analysis = analyze(kEngage + kKill + kRetreat +
                                      "rule Pairwise:\n"
                                      "    pairs:\n"
                                      "        a: Unit\n"
                                      "        b: Position\n"
                                      "    on lost Order.Attack.target:\n"
                                      "        destroy a\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"`on lost`", "`pairs:`"}));
    }
}

// ── Reading through a link ──────────────────────────────────────────────────

TEST_CASE("Semantic: a rule reads the target's declared trait through a link", "[semantic][required-links]") {
    const auto analysis = analyze(kEngage + kKill + kRetreat +
                                  "rule Finish:\n"
                                  "    each: Unit, Order.Attack as order\n"
                                  "    where: order.target[Unit].health < 10.0\n"
                                  "    on tick:\n"
                                  "        emit Struck to order.target\n"
                                  "rule Hurt:\n"
                                  "    each: Unit as unit\n"
                                  "    on Struck:\n"
                                  "        unit.health -= 1.0\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& contract = contract_of(analysis, "Finish");
    CHECK(reads_of(contract, "Unit").covers("health"));
    CHECK(reads_of(contract, "Order.Attack").covers("target"));
}

TEST_CASE("Semantic: reads through a link are limited", "[semantic][required-links]") {
    SECTION("another trait") {
        const auto analysis = analyze(kEngage + kKill + kRetreat +
                                      "rule Peek:\n"
                                      "    each: Order.Attack as order, Position as here\n"
                                      "    on tick:\n"
                                      "        here.x = order.target[Position].x\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Position'", "through link", "pairs:"}));
    }
    SECTION("a write") {
        const auto analysis = analyze(kEngage + kKill + kRetreat +
                                      "rule Poke:\n"
                                      "    each: Order.Attack as order\n"
                                      "    on tick:\n"
                                      "        order.target[Unit].health -= 1.0\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"can't write through link", "set Unit on order.target"}));
    }
    SECTION("a pair binding holder") {
        const auto analysis = analyze(kEngage + kKill + kRetreat +
                                      "rule Peek:\n"
                                      "    pairs:\n"
                                      "        attacker: Order.Attack\n"
                                      "        spot: Position\n"
                                      "    on tick:\n"
                                      "        if attacker[Order.Attack].target[Unit].health < 1.0:\n"
                                      "            emit Struck to attacker\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"through link", "rule's own row", "pairs:"}));
    }
    SECTION("a func body") {
        const auto analysis = analyze(kEngage + kKill + kRetreat +
                                      "func aim(order: Order.Attack) float:\n"
                                      "    return order.target[Unit].health\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"through link", "`func`"}));
    }
}

TEST_CASE("Semantic: a writer of the target trait runs before a reader through a link",
          "[semantic][required-links][handler-contracts]") {
    const auto analysis = analyze(kEngage + kKill + kRetreat +
                                  "rule Heal:\n"
                                  "    each: Unit as unit\n"
                                  "    on tick:\n"
                                  "        unit.health += 1.0\n"
                                  "rule Watch:\n"
                                  "    each: Order.Attack as order, Position as here\n"
                                  "    on tick:\n"
                                  "        here.x = order.target[Unit].health\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto ordered = std::ranges::any_of(analysis.program.execution_graph.schedule_edges, [](const auto& edge) {
        return edge.before.rule.local_name == "Heal" && edge.after.rule.local_name == "Watch";
    });
    CHECK(ordered);
}

TEST_CASE("Semantic: a keep that reads through a link records the target trait read",
          "[semantic][required-links][handler-contracts]") {
    const auto analysis = analyze(kEngage + kKill + kRetreat +
                                  "trait Close\n"
                                  "group links:\n"
                                  "    phase: tick\n"
                                  "rule Near:\n"
                                  "    each: Order.Attack as order\n"
                                  "    group: links\n"
                                  "    where: order.target[Unit].health < 5.0\n"
                                  "    keep Close\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto contract = std::ranges::find_if(analysis.program.handler_contracts,
                                               [](const auto& entry) { return entry.rule.local_name == "Near"; });
    REQUIRE(contract != analysis.program.handler_contracts.end());
    const auto unit = std::ranges::find_if(contract->read_fields,
                                           [](const auto& entry) { return entry.first.local_name == "Unit"; });
    REQUIRE(unit != contract->read_fields.end());
    CHECK((unit->second.all || unit->second.fields.contains("health")));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
