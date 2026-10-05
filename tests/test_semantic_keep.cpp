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
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "group contacts:\n"
    "    phase: fixed_tick\n"
    "event Ping\n"
    "trait Body:\n"
    "    var x: float = 0.0\n"
    "trait Zone:\n"
    "    var x: float = 0.0\n"
    "    var radius: float = 1.0\n"
    "    var priority: int = 0\n"
    "    var dps: float = 0.0\n"
    "trait InZone:\n"
    "    var zones: int = 0\n"
    "    var dps: float = 0.0\n"
    "trait Nearest:\n"
    "    var zone: entity_id\n"
    "trait Seen\n"
    "trait Saved:\n"
    "    persist var count: int = 0\n"
    "trait Stats:\n"
    "    var zones: int = 0\n"
    "entity Board:\n"
    "    Stats\n";

const std::string kPairs =
    "    pairs:\n"
    "        body:\n"
    "            Body\n"
    "        zone:\n"
    "            Zone\n";

const std::string kWhere =
    "    where:\n"
    "        (body.Body.x - zone.Zone.x) * (body.Body.x - zone.Zone.x) <= zone.Zone.radius * zone.Zone.radius\n";

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
    [[nodiscard]] const RuleNode& rule(const std::string& name) const {
        for (const auto& decl : ast->declarations) {
            if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name == name) {
                return *rule;
            }
        }
        FAIL("no rule named " + name);
        std::unreachable();
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
    return analysis;
}

std::vector<Diagnostic> parse_errors(const std::string& body) {
    ErrorReporter errors;
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    (void)parser.parse_program();
    return errors.diagnostics();
}

// A keep rule over body x zone, reduced per body, with the given clause lines.
std::string touch_rule(const std::string& reduce_lines,
                       const std::string& keep_lines,
                       const std::string& extra_lines = "") {
    return "rule Touch:\n" + kPairs + "    group: contacts\n" + kWhere + "    reduce:\n" + reduce_lines + keep_lines +
           extra_lines;
}

const std::string kPerBody =
    "        per: body\n"
    "        n = count()\n";

const std::string kKeepZones =
    "    keep InZone on body:\n"
    "        zones = n\n";

bool has_command(const HandlerContract& contract, HandlerCommandKind kind, const std::string& trait) {
    return std::ranges::any_of(contract.commands, [&](const InferredHandlerCommand& command) {
        return command.kind == kind && command.target.has_value() && command.target->local_name == trait;
    });
}

}  // namespace

// ── Parsing ─────────────────────────────────────────────────────────────────

TEST_CASE("Keep: the clause parses into the rule", "[parser][rule-keep]") {
    const auto analysis = analyze(touch_rule(kPerBody, kKeepZones));
    const auto& rule    = analysis.rule("Touch");
    REQUIRE(rule.keep.has_value());
    CHECK(rule.keep->trait_name == "InZone");
    CHECK(rule.keep->binding == "body");
    REQUIRE(rule.keep->fields.size() == 1);
    CHECK(rule.keep->fields.front().name == "zones");
    CHECK(rule.keep->location.line > 0);
}

TEST_CASE("Keep: a fieldless clause needs no block", "[parser][rule-keep]") {
    const auto analysis = analyze(touch_rule(kPerBody, "    keep Seen on body\n"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
    REQUIRE(analysis.rule("Touch").keep.has_value());
    CHECK(analysis.rule("Touch").keep->fields.empty());
}

TEST_CASE("Keep: extern rules reject the clause", "[parser][rule-keep]") {
    const auto errors = parse_errors("extern rule Outside:\n"
                                     "    filter:\n"
                                     "        Body\n"
                                     "    keep Seen on body\n");
    CHECK(std::ranges::any_of(
        errors, [](const Diagnostic& d) { return d.message.contains("'keep' is not valid on external rules"); }));
}

// ── Clause shape ────────────────────────────────────────────────────────────

TEST_CASE("Keep: a valid clause is accepted and runs in its group's phase", "[semantic][rule-keep]") {
    const auto analysis = analyze(touch_rule(kPerBody, kKeepZones));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = analysis.rule("Touch");
    REQUIRE(rule.keep->resolved_trait_id.has_value());
    CHECK(rule.keep->resolved_trait_id->local_name == "InZone");
    REQUIRE(rule.handlers.size() == 1);
    REQUIRE(rule.handlers.front().resolved_trigger.has_value());
    CHECK(rule.handlers.front().resolved_trigger->kind == HandlerTriggerKind::Phase);
    CHECK(rule.handlers.front().resolved_trigger->symbol.local_name == "fixed_tick");

    const auto& contract = analysis.contract("Touch");
    CHECK(has_command(contract, HandlerCommandKind::Add, "InZone"));
    CHECK(has_command(contract, HandlerCommandKind::Remove, "InZone"));
    const auto& handlers = analysis.program.execution_graph.handlers;
    REQUIRE(handlers.size() == 1);
    REQUIRE(handlers.front().group.has_value());
    CHECK(handlers.front().group->local_name == "contacts");
}

TEST_CASE("Keep: a rule with a phase handler keeps through that handler", "[semantic][rule-keep]") {
    const auto analysis = analyze("rule Touch:\n" + kPairs + kWhere +
                                  "    reduce:\n" + kPerBody + kKeepZones +
                                  "    on fixed_tick:\n"
                                  "        body.Body.x = body.Body.x\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK(analysis.rule("Touch").handlers.size() == 1);
}

TEST_CASE("Keep: the binding must be the per binding", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kPerBody,
                             "    keep InZone on zone:\n"
                             "        zones = n\n"))
              .has("must name the `per` binding 'body'"));
}

TEST_CASE("Keep: field expressions can't read the eliminated binding", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kPerBody,
                             "    keep InZone on body:\n"
                             "        zones = zone.Zone.priority\n"))
              .has("'zone' is not available after `reduce:`"));
}

TEST_CASE("Keep: field expressions read aggregates, the per binding and constants", "[semantic][rule-keep]") {
    const auto analysis = analyze("const:\n"
                                  "    BONUS = 2\n" +
                                  touch_rule(kPerBody + "        dps = sum(zone.Zone.dps)\n",
                                             "    keep InZone on body:\n"
                                             "        zones = n + BONUS\n"
                                             "        dps = dps * body.Body.x + Board.Stats.zones\n"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Keep: unreduced, unary and globally reduced rules reject keep", "[semantic][rule-keep]") {
    CHECK(analyze("rule Touch:\n" + kPairs + "    group: contacts\n" + kWhere + "    keep Seen on body\n")
              .has("`keep` requires `reduce:` with `per:`"));
    CHECK(analyze("rule Touch:\n"
                  "    filter:\n"
                  "        Body as body\n"
                  "    group: contacts\n"
                  "    reduce:\n"
                  "        n = count()\n"
                  "    keep Seen on body\n")
              .has("`keep` requires `reduce:` with `per:`"));
    CHECK(analyze(touch_rule("        n = count()\n", "    keep Seen on body\n"))
              .has("`keep` requires `reduce:` with `per:`"));
}

TEST_CASE("Keep: unknown fields and type mismatches are rejected", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kPerBody,
                             "    keep InZone on body:\n"
                             "        nope = n\n"))
              .has("unknown field 'nope'"));
    CHECK(analyze(touch_rule(kPerBody,
                             "    keep InZone on body:\n"
                             "        zones = 1.5\n"))
              .has("type"));
    CHECK(analyze(touch_rule(kPerBody,
                             "    keep InZone on body:\n"
                             "        zones = n\n"
                             "        zones = n\n"))
              .has("duplicate"));
    CHECK(analyze(touch_rule(kPerBody, "    keep Missing on body\n")).has("Missing"));
}

TEST_CASE("Keep: fields without a default must be assigned", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kPerBody, "    keep Nearest on body\n")).has("required field 'zone'"));
}

TEST_CASE("Keep: a kept trait can't have persistent fields", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kPerBody, "    keep Saved on body\n")).has("persistent"));
}

TEST_CASE("Keep: a keep rule needs a phase", "[semantic][rule-keep]") {
    CHECK(analyze("rule Touch:\n" + kPairs + kWhere + "    reduce:\n" + kPerBody + kKeepZones)
              .has("add a `group:`"));
}

TEST_CASE("Keep: event and lifecycle handlers are rejected", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kPerBody,
                             kKeepZones,
                             "    on Ping:\n"
                             "        body.Body.x = 0.0\n"))
              .has("keep rules run only on phase activations"));
    CHECK_FALSE(analyze(touch_rule(kPerBody,
                                   kKeepZones,
                                   "    on added Seen:\n"
                                   "        body.Body.x = 0.0\n"))
                    .clean());
}

// ── Writers ─────────────────────────────────────────────────────────────────

TEST_CASE("Keep: two keep rules for one trait are rejected", "[semantic][rule-keep]") {
    const auto second = "rule Again:\n" + kPairs + "    group: contacts\n" + kWhere + "    reduce:\n" + kPerBody +
                        kKeepZones;
    const auto analysis = analyze(touch_rule(kPerBody, kKeepZones) + second);
    CHECK(analysis.has("'InZone' is kept by both 'Touch' and 'Again'"));
}

TEST_CASE("Keep: every other write to a kept trait is rejected", "[semantic][rule-keep]") {
    const auto with_writer = [](const std::string& writer) {
        return analyze(touch_rule(kPerBody, kKeepZones) + writer);
    };
    const std::string kept_by = "'InZone' is kept by rule 'Touch'";
    CHECK(with_writer("rule Manual:\n"
                      "    filter:\n"
                      "        Body\n"
                      "    on fixed_tick:\n"
                      "        add InZone\n")
              .has(kept_by));
    CHECK(with_writer("rule Manual:\n"
                      "    filter:\n"
                      "        Body\n"
                      "    on fixed_tick:\n"
                      "        remove InZone\n")
              .has(kept_by));
    CHECK(with_writer("rule Manual:\n"
                      "    filter:\n"
                      "        InZone as z\n"
                      "    on fixed_tick:\n"
                      "        set InZone on self:\n"
                      "            zones = 3\n")
              .has(kept_by));
    CHECK(with_writer("rule Manual:\n"
                      "    filter:\n"
                      "        InZone as z\n"
                      "    on fixed_tick:\n"
                      "        z.zones = 3\n")
              .has(kept_by));
    CHECK(with_writer("rule Manual:\n"
                      "    filter:\n"
                      "        Body\n"
                      "    on fixed_tick:\n"
                      "        project InZone:\n"
                      "            zones = 1\n")
              .has(kept_by));
    CHECK(with_writer("template Walker:\n"
                      "    Body\n"
                      "    InZone\n")
              .has(kept_by));
    CHECK(with_writer("entity Camper:\n"
                      "    Body\n"
                      "    InZone\n")
              .has(kept_by));
}

TEST_CASE("Keep: the keep rule's own handler can't write the kept trait", "[semantic][rule-keep]") {
    CHECK(analyze("rule Touch:\n" + kPairs + kWhere + "    reduce:\n" + kPerBody + kKeepZones +
                  "    on fixed_tick:\n"
                  "        remove InZone from body\n")
              .has("'InZone' is kept by rule 'Touch'"));
}

TEST_CASE("Keep: reading and filtering on a kept trait is allowed", "[semantic][rule-keep]") {
    const auto analysis = analyze(touch_rule(kPerBody, kKeepZones) +
                                  "rule Burn:\n"
                                  "    filter:\n"
                                  "        Body as b\n"
                                  "        InZone as z\n"
                                  "    on fixed_tick:\n"
                                  "        b.x = b.x + z.dps\n"
                                  "rule Enter:\n"
                                  "    filter:\n"
                                  "        Body as b\n"
                                  "    on added InZone:\n"
                                  "        b.x = 0.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

// ── best ────────────────────────────────────────────────────────────────────

TEST_CASE("Best: names the winning entity of a binding", "[semantic][rule-reduce][rule-keep]") {
    const auto analysis = analyze(touch_rule(kPerBody + "        top = best(zone, by = zone.Zone.priority)\n"
                                                        "        hot = best(zone, by = zone.Zone.dps)\n",
                                             "    keep Nearest on body:\n"
                                             "        zone = top\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& reducers = analysis.rule("Touch").reduce->reducers;
    REQUIRE(reducers.size() == 3);
    CHECK(reducers[1].kind == ReducerKind::Best);
    CHECK(reducers[1].result_type.kind == TypeKind::EntityId);
    REQUIRE(reducers[1].key != nullptr);
    CHECK(reducers[1].key_type.kind == TypeKind::Int);
    CHECK(reducers[2].key_type.kind == TypeKind::Float);
}

TEST_CASE("Best: the per binding is rejected", "[semantic][rule-reduce][rule-keep]") {
    CHECK(analyze(touch_rule(kPerBody + "        top = best(body, by = body.Body.x)\n", "    keep Seen on body\n"))
              .has("best(...) takes a pair binding other than the `per` binding 'body'"));
}

TEST_CASE("Best: the key must be int or float", "[semantic][rule-reduce][rule-keep]") {
    CHECK(analyze(touch_rule(kPerBody + "        top = best(zone, by = zone.Zone.x > 0.0)\n", "    keep Seen on body\n"))
              .has("best(...) key must be of type 'int' or 'float'"));
    CHECK(analyze(touch_rule(kPerBody + "        top = best(zone)\n", "    keep Seen on body\n")).has("`by = <key>`"));
    CHECK(analyze(touch_rule(kPerBody + "        top = best(zone.Zone.x, by = zone.Zone.x)\n", "    keep Seen on body\n"))
              .has("best(...) takes a pair binding"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
