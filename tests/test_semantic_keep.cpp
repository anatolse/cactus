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
    "    var hot: entity_id\n"
    "trait Seen\n"
    "trait Saved:\n"
    "    persist var count: int = 0\n"
    "trait Stats:\n"
    "    var zones: int = 0\n"
    "trait Lit\n"
    "trait A\n"
    "trait B\n"
    "state Mood:\n"
    "    Calm\n"
    "    Angry\n"
    "pub extern func mystery(v: float) bool\n"
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
    "        (body[Body].x - zone[Zone].x) * (body[Body].x - zone[Zone].x) <= zone[Zone].radius * zone[Zone].radius\n";

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

ProgramNode parse_program(const std::string& body, ErrorReporter& errors) {
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    return parser.parse_program();
}

const RuleNode& rule_in(const ProgramNode& program, const std::string& name) {
    for (const auto& decl : program.declarations) {
        if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name == name) {
            return *rule;
        }
    }
    FAIL("no rule named " + name);
    std::unreachable();
}

std::vector<Diagnostic> parse_errors(const std::string& body) {
    ErrorReporter errors;
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    (void)parser.parse_program();
    return errors.diagnostics();
}

// A keep rule over body x zone with the given reduce clause and clause lines.
std::string touch_rule(const std::string& reduce_clause,
                       const std::string& keep_lines,
                       const std::string& extra_lines = "") {
    return "rule Touch:\n" + kPairs + "    group: contacts\n" + kWhere + reduce_clause + keep_lines + extra_lines;
}

const std::string kReducePerBody =
    "    reduce per body:\n"
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
    const auto analysis = analyze(touch_rule(kReducePerBody, kKeepZones));
    const auto& rule    = analysis.rule("Touch");
    REQUIRE(rule.keep.has_value());
    CHECK(rule.keep->trait_name == "InZone");
    CHECK(rule.keep->binding == "body");
    REQUIRE(rule.keep->fields.size() == 1);
    CHECK(rule.keep->fields.front().name == "zones");
    CHECK(rule.keep->location.line > 0);
}

TEST_CASE("Keep: a fieldless clause needs no block", "[parser][rule-keep]") {
    const auto analysis = analyze(touch_rule("", "    keep Seen on body\n"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
    REQUIRE(analysis.rule("Touch").keep.has_value());
    CHECK(analysis.rule("Touch").keep->fields.empty());
}

TEST_CASE("Keep: extern rules reject the clause", "[parser][rule-keep]") {
    const auto errors = parse_errors("extern rule Outside:\n"
                                     "    each:\n"
                                     "        Body\n"
                                     "    keep Seen on body\n");
    CHECK(std::ranges::any_of(
        errors, [](const Diagnostic& d) { return d.message.contains("'keep' is not valid on external rules"); }));
}

TEST_CASE("Keep: a clause without `on` parses with no binding", "[parser][rule-keep]") {
    ErrorReporter errors;
    const auto ast  = parse_program("rule Low:\n"
                                    "    each:\n"
                                    "        Body as b\n"
                                    "    keep Seen\n",
                                   errors);
    REQUIRE_FALSE(errors.has_errors());
    const auto& keep = rule_in(ast, "Low").keep;
    REQUIRE(keep.has_value());
    CHECK(keep->trait_name == "Seen");
    CHECK_FALSE(keep->binding.has_value());
    CHECK(keep->condition == nullptr);
}

TEST_CASE("Keep: an `if` condition parses with and without a field block", "[parser][rule-keep]") {
    ErrorReporter errors;
    const auto ast = parse_program(touch_rule(kReducePerBody, "    keep Seen on body if n == 0\n") +
                                       "rule Again:\n" + kPairs + "    group: contacts\n" + kReducePerBody +
                                       "    keep InZone on body if n >= 3:\n"
                                       "        zones = n\n",
                                   errors);
    REQUIRE_FALSE(errors.has_errors());
    const auto& bare = rule_in(ast, "Touch").keep;
    REQUIRE(bare.has_value());
    CHECK(bare->binding == "body");
    REQUIRE(bare->condition != nullptr);
    CHECK(bare->fields.empty());
    const auto& block = rule_in(ast, "Again").keep;
    REQUIRE(block.has_value());
    REQUIRE(block->condition != nullptr);
    REQUIRE(block->fields.size() == 1);
    CHECK(block->fields.front().name == "zones");
}

TEST_CASE("Keep: a bad condition is reported and parsing recovers", "[parser][rule-keep]") {
    ErrorReporter errors;
    const auto ast = parse_program(touch_rule(kReducePerBody, "    keep Seen on body if )\n") +
                                       "rule After:\n"
                                       "    each:\n"
                                       "        Body as b\n"
                                       "    on fixed_tick:\n"
                                       "        b.x = 1.0\n",
                                   errors);
    CHECK(errors.has_errors());
    CHECK(std::ranges::any_of(ast.declarations, [](const Declaration& decl) {
        const auto* rule = std::get_if<RuleNode>(&decl);
        return rule != nullptr && rule->name == "After";
    }));
}

// ── Clause shape ────────────────────────────────────────────────────────────

TEST_CASE("Keep: a valid clause is accepted and runs in its group's phase", "[semantic][rule-keep]") {
    const auto analysis = analyze(touch_rule(kReducePerBody, kKeepZones));
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

TEST_CASE("Keep: the binding must be the per binding", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kReducePerBody,
                             "    keep InZone on zone:\n"
                             "        zones = n\n"))
              .has("must name the `per` binding 'body'"));
}

TEST_CASE("Keep: field expressions can't read the eliminated binding", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kReducePerBody,
                             "    keep InZone on body:\n"
                             "        zones = zone[Zone].priority\n"))
              .has("'zone' is not available after `reduce:`"));
}

TEST_CASE("Keep: field expressions read aggregates, the per binding and constants", "[semantic][rule-keep]") {
    const auto analysis = analyze("const:\n"
                                  "    BONUS = 2\n" +
                                  touch_rule(kReducePerBody + "        dps = sum(zone[Zone].dps)\n",
                                             "    keep InZone on body:\n"
                                             "        zones = n + BONUS\n"
                                             "        dps = dps * body[Body].x + Board[Stats].zones\n"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Keep: unknown fields and type mismatches are rejected", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kReducePerBody,
                             "    keep InZone on body:\n"
                             "        nope = n\n"))
              .has("unknown field 'nope'"));
    CHECK(analyze(touch_rule(kReducePerBody,
                             "    keep InZone on body:\n"
                             "        zones = 1.5\n"))
              .has("type"));
    CHECK(analyze(touch_rule(kReducePerBody,
                             "    keep InZone on body:\n"
                             "        zones = n\n"
                             "        zones = n\n"))
              .has("duplicate"));
    CHECK(analyze(touch_rule(kReducePerBody, "    keep Missing on body\n")).has("Missing"));
}

TEST_CASE("Keep: fields without a default must be assigned", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kReducePerBody, "    keep Nearest on body\n")).has("required field 'zone'"));
}

TEST_CASE("Keep: a kept trait can't have persistent fields", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kReducePerBody, "    keep Saved on body\n")).has("persistent"));
}

TEST_CASE("Keep: a keep rule needs a phase", "[semantic][rule-keep]") {
    CHECK(analyze("rule Touch:\n" + kPairs + kWhere + kReducePerBody + kKeepZones)
              .has("add a `group:`"));
}

// ── Writers ─────────────────────────────────────────────────────────────────

TEST_CASE("Keep: two keep rules for one trait are rejected", "[semantic][rule-keep]") {
    const auto second = "rule Again:\n" + kPairs + "    group: contacts\n" + kWhere + kReducePerBody +
                        kKeepZones;
    const auto analysis = analyze(touch_rule(kReducePerBody, kKeepZones) + second);
    CHECK(analysis.has("'InZone' is kept by both 'Touch' and 'Again'"));
}

TEST_CASE("Keep: every other write to a kept trait is rejected", "[semantic][rule-keep]") {
    const auto with_writer = [](const std::string& writer) {
        return analyze(touch_rule(kReducePerBody, kKeepZones) + writer);
    };
    const std::string kept_by = "'InZone' is kept by rule 'Touch'";
    CHECK(with_writer("rule Manual:\n"
                      "    each:\n"
                      "        Body\n"
                      "    on fixed_tick:\n"
                      "        add InZone\n")
              .has(kept_by));
    CHECK(with_writer("rule Manual:\n"
                      "    each:\n"
                      "        Body\n"
                      "    on fixed_tick:\n"
                      "        remove InZone\n")
              .has(kept_by));
    CHECK(with_writer("rule Manual:\n"
                      "    each:\n"
                      "        InZone as z\n"
                      "    on fixed_tick:\n"
                      "        set InZone on self:\n"
                      "            zones = 3\n")
              .has(kept_by));
    CHECK(with_writer("rule Manual:\n"
                      "    each:\n"
                      "        InZone as z\n"
                      "    on fixed_tick:\n"
                      "        z.zones = 3\n")
              .has(kept_by));
    CHECK(with_writer("rule Manual:\n"
                      "    each:\n"
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

TEST_CASE("Keep: reading and filtering on a kept trait is allowed", "[semantic][rule-keep]") {
    const auto analysis = analyze(touch_rule(kReducePerBody, kKeepZones) +
                                  "rule Burn:\n"
                                  "    each:\n"
                                  "        Body as b\n"
                                  "        InZone as z\n"
                                  "    on fixed_tick:\n"
                                  "        b.x = b.x + z.dps\n"
                                  "rule Enter:\n"
                                  "    each:\n"
                                  "        Body as b\n"
                                  "    on added InZone:\n"
                                  "        b.x = 0.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

// ── best ────────────────────────────────────────────────────────────────────

TEST_CASE("Best: names the winning entity of a binding", "[semantic][rule-reduce][rule-keep]") {
    const auto analysis = analyze(touch_rule(kReducePerBody + "        top = best(zone, by = zone[Zone].priority)\n"
                                                        "        hot = best(zone, by = zone[Zone].dps)\n",
                                             "    keep Nearest on body if n > 1:\n"
                                             "        zone = top\n"
                                             "        hot = hot\n"));
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
    CHECK(analyze(touch_rule(kReducePerBody + "        top = best(body, by = body[Body].x)\n", "    keep Seen on body\n"))
              .has("best(...) takes a pair binding other than the `per` binding 'body'"));
}

TEST_CASE("Best: the key must be int or float", "[semantic][rule-reduce][rule-keep]") {
    CHECK(analyze(touch_rule(kReducePerBody + "        top = best(zone, by = zone[Zone].x > 0.0)\n", "    keep Seen on body\n"))
              .has("best(...) key must be of type 'int' or 'float'"));
    CHECK(analyze(touch_rule(kReducePerBody + "        top = best(zone)\n", "    keep Seen on body\n")).has("`by = <key>`"));
    CHECK(analyze(touch_rule(kReducePerBody + "        top = best(zone[Zone].x, by = zone[Zone].x)\n", "    keep Seen on body\n"))
              .has("best(...) takes a pair binding"));
}

TEST_CASE("Keep: removed body-level per still groups for recovery", "[semantic][rule-keep][concise]") {
    ErrorReporter errors;
    Lexer lexer(
        "module game\n" + kPrelude + touch_rule("    reduce:\n        per: body\n        n = count()\n", kKeepZones),
        "game.cactus",
        errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    SemanticAnalyzer analyzer(errors);
    (void)analyzer.analyze(ast);
    REQUIRE(errors.error_count() == 1);
    CHECK(errors.diagnostics().front().message.contains("reduce per body:"));
}
// ── Generalized forms ───────────────────────────────────────────────────────

namespace {

// A unary keep rule over Body with the given clause lines.
std::string low_rule(const std::string& lines) {
    return "rule Low:\n"
           "    each:\n"
           "        Body as b\n"
           "    group: contacts\n"
           "    where:\n"
           "        b.x < 30.0\n" +
           lines;
}

bool has_command_on(const HandlerContract& contract,
                    HandlerCommandKind kind,
                    const std::string& trait,
                    EntityTargetKind target) {
    return std::ranges::any_of(contract.commands, [&](const InferredHandlerCommand& command) {
        return command.kind == kind && command.target.has_value() && command.target->local_name == trait &&
               std::ranges::any_of(command.entities, [&](const EntityTarget& entity) { return entity.kind == target; });
    });
}

bool reads(const HandlerContract& contract, const std::string& trait) {
    return std::ranges::any_of(contract.reads, [&](const SymbolId& read) { return read.local_name == trait; });
}

}  // namespace

TEST_CASE("Keep: a unary keep puts the trait on each matched entity", "[semantic][rule-keep]") {
    const auto analysis = analyze(low_rule("    keep InZone:\n"
                                           "        dps = b.x + Board[Stats].zones\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = analysis.rule("Low");
    REQUIRE(rule.handlers.size() == 1);
    CHECK(rule.handlers.front().resolved_trigger->kind == HandlerTriggerKind::Phase);
    const auto& contract = analysis.contract("Low");
    CHECK(has_command_on(contract, HandlerCommandKind::Add, "InZone", EntityTargetKind::Self));
    CHECK(has_command_on(contract, HandlerCommandKind::Remove, "InZone", EntityTargetKind::Self));
    CHECK(reads(contract, "Body"));
    CHECK(reads(contract, "Stats"));
}

TEST_CASE("Keep: a unary keep names no recipient", "[semantic][rule-keep]") {
    const auto analysis = analyze(low_rule("    keep Seen on self\n"));
    CHECK(analysis.has("remove `on self`"));
}

TEST_CASE("Keep: an unreduced pair keep puts the trait on its binding", "[semantic][rule-keep]") {
    const auto analysis = analyze(touch_rule("",
                                             "    keep InZone on body:\n"
                                             "        dps = body[Body].x\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& contract = analysis.contract("Touch");
    CHECK(has_command_on(contract, HandlerCommandKind::Add, "InZone", EntityTargetKind::PairBinding));
    CHECK(has_command_on(contract, HandlerCommandKind::Remove, "InZone", EntityTargetKind::PairBinding));
}

TEST_CASE("Keep: a pair keep needs a recipient", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule("", "    keep Seen\n")).has("name a pair binding with `on`"));
    CHECK(analyze(touch_rule("", "    keep Seen on nope\n")).has("'nope' is not a pair binding of rule 'Touch'"));
}

TEST_CASE("Keep: an unreduced pair keep reads only its recipient", "[semantic][rule-keep]") {
    const auto analysis = analyze(touch_rule("",
                                             "    keep InZone on body:\n"
                                             "        dps = zone[Zone].dps\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("'zone' is not available in `keep InZone on body`"));
    CHECK(analysis.has("add `reduce per body:` and store `best(zone, by = ...)`"));
}

TEST_CASE("Keep: a condition works only with reduce per", "[semantic][rule-keep]") {
    CHECK(analyze(low_rule("    keep Seen if b.x > 1.0\n")).has("use `where:`"));
    CHECK(analyze(touch_rule("", "    keep Seen on body if body[Body].x > 1.0\n")).has("use `where:`"));
    const auto reduced = analyze(touch_rule(kReducePerBody, "    keep Seen on body if n == 0\n"));
    INFO(describe(reduced));
    CHECK(reduced.clean());
}

TEST_CASE("Keep: a condition is a pure bool over the reduced scope", "[semantic][rule-keep]") {
    CHECK(analyze(touch_rule(kReducePerBody, "    keep Seen on body if n\n")).has("must be of type 'bool'"));
    CHECK(analyze(touch_rule(kReducePerBody, "    keep Seen on body if n > 0 and mystery(body[Body].x)\n"))
              .has("must be pure"));
    CHECK(analyze(touch_rule(kReducePerBody, "    keep Seen on body if n > 0 and zone[Zone].x > 0.0\n"))
              .has("'zone' is not available after `reduce:`"));
}

TEST_CASE("Keep: rules without a usable domain reject keep", "[semantic][rule-keep]") {
    CHECK(analyze("rule Touch:\n"
                  "    each:\n"
                  "        Body as body\n"
                  "    group: contacts\n"
                  "    reduce:\n"
                  "        n = count()\n"
                  "    keep InZone:\n"
                  "        zones = n\n")
              .has("`keep` can't follow a global `reduce:`"));
    CHECK(analyze(touch_rule("    reduce:\n        n = count()\n",
                             "    keep InZone on body:\n"
                             "        zones = n\n"))
              .has("`keep` can't follow a global `reduce:`"));
    CHECK(analyze("rule Touch:\n"
                  "    group: contacts\n"
                  "    keep Seen\n")
              .has("`keep` needs an `each:` or `pairs:` domain"));
}

TEST_CASE("Keep: trait restrictions hold in every form", "[semantic][rule-keep]") {
    for (const auto& clause : {std::string("Mood.Calm"), std::string("Saved")}) {
        CHECK_FALSE(analyze(low_rule("    keep " + clause + "\n")).clean());
        CHECK_FALSE(analyze(touch_rule("", "    keep " + clause + " on body\n")).clean());
        CHECK_FALSE(analyze(touch_rule(kReducePerBody, "    keep " + clause + " on body if n > 0\n")).clean());
    }
    CHECK(analyze(low_rule("    keep Mood.Calm\n")).has("state variant"));
    CHECK(analyze(touch_rule("", "    keep Saved on body\n")).has("persistent"));
}

// ── A keep rule is a definition ─────────────────────────────────────────────

TEST_CASE("Keep: a keep rule has no handler", "[semantic][rule-keep]") {
    const auto phase = analyze(touch_rule(kReducePerBody,
                                          kKeepZones,
                                          "    on fixed_tick:\n"
                                          "        body[Body].x = 0.0\n"));
    CHECK(phase.has("keep rule 'Touch' can't have a handler"));
    CHECK(phase.has("react with `on added InZone` / `on removed InZone`"));
    CHECK(analyze(low_rule("    keep Seen\n"
                           "    on added Seen:\n"
                           "        b.x = 0.0\n"))
              .has("keep rule 'Low' can't have a handler"));
    CHECK(analyze(touch_rule("",
                             "    keep Seen on body\n",
                             "    on Ping:\n"
                             "        body[Body].x = 0.0\n"))
              .has("keep rule 'Touch' can't have a handler"));
}

TEST_CASE("Keep: order by and limit are rejected", "[semantic][rule-keep]") {
    const auto ordered = analyze(touch_rule(kReducePerBody +
                                                "    order by:\n"
                                                "        n\n",
                                            kKeepZones));
    CHECK(ordered.has("`order by:` has no effect on `keep InZone`"));
    CHECK(ordered.has("narrow rows with `where:` or store `best(...)` in a kept field"));
    const auto limited = analyze(low_rule("    keep Seen\n"
                                          "    limit: 3\n"));
    CHECK(limited.has("`limit:` has no effect on `keep Seen`"));
}

TEST_CASE("Keep: a unary keep rule needs a phase", "[semantic][rule-keep]") {
    CHECK(analyze("rule Low:\n"
                  "    each:\n"
                  "        Body\n"
                  "    keep Seen\n")
              .has("add a `group:`"));
}

// ── Dependency loops ────────────────────────────────────────────────────────

TEST_CASE("Keep: a keeper can't read its own kept trait", "[semantic][rule-keep]") {
    const auto filter = analyze("rule Glow:\n"
                                "    each:\n"
                                "        Body\n"
                                "    without:\n"
                                "        Lit\n"
                                "    group: contacts\n"
                                "    keep Lit\n");
    CHECK(filter.has("keep rule 'Glow' reads its own kept trait 'Lit'"));
    CHECK(analyze("rule Grow:\n"
                  "    each:\n"
                  "        Body as b\n"
                  "    group: contacts\n"
                  "    where:\n"
                  "        Board[Stats].zones > 0\n"
                  "    keep Stats:\n"
                  "        zones = 1\n")
              .has("keep rule 'Grow' reads its own kept trait 'Stats'"));
    CHECK(analyze("rule Count:\n"
                  "    each:\n"
                  "        Body\n"
                  "        InZone as z\n"
                  "    group: contacts\n"
                  "    keep InZone:\n"
                  "        zones = z.zones + 1\n")
              .has("keep rule 'Count' reads its own kept trait 'InZone'"));
}

TEST_CASE("Keep: two keepers can't read each other", "[semantic][rule-keep]") {
    const auto analysis = analyze("rule AFact:\n"
                                  "    each:\n"
                                  "        Body\n"
                                  "    without:\n"
                                  "        B\n"
                                  "    group: contacts\n"
                                  "    keep A\n"
                                  "rule BFact:\n"
                                  "    each:\n"
                                  "        Body\n"
                                  "        A\n"
                                  "    group: contacts\n"
                                  "    keep B\n");
    CHECK(analysis.has("keep rules form a loop through their kept traits: 'AFact'"));
    CHECK(analysis.has("'BFact'"));
    CHECK(std::ranges::count_if(analysis.diagnostics, [](const Diagnostic& d) {
              return d.message.contains("form a loop");
          }) == 1);
}

TEST_CASE("Keep: a chain of keepers is accepted", "[semantic][rule-keep]") {
    const auto analysis = analyze("rule AFact:\n"
                                  "    each:\n"
                                  "        Body as b\n"
                                  "    group: contacts\n"
                                  "    where:\n"
                                  "        b.x < 30.0\n"
                                  "    keep A\n"
                                  "rule BFact:\n"
                                  "    each:\n"
                                  "        Body\n"
                                  "        A\n"
                                  "    group: contacts\n"
                                  "    keep B\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

// ── Unused reducers ─────────────────────────────────────────────────────────

TEST_CASE("Reduce: a keep rule must read its reducers", "[semantic][rule-reduce][rule-keep]") {
    const auto analysis = analyze(touch_rule("    reduce per body:\n"
                                             "        hurt = count()\n",
                                             "    keep Seen on body\n"));
    CHECK(analysis.has("reducer 'hurt' is never read"));
    CHECK(analysis.has("remove `reduce:` to keep 'Seen' while any row matches"));
}

TEST_CASE("Reduce: a handler rule must read its reducers", "[semantic][rule-reduce]") {
    const auto analysis = analyze("rule Rank:\n" + kPairs + kWhere +
                                  "    reduce per body:\n"
                                  "        n = count()\n"
                                  "        top = best(zone, by = zone[Zone].priority)\n"
                                  "    on fixed_tick:\n"
                                  "        if top == body:\n"
                                  "            body[Body].x = 1.0\n");
    CHECK(analysis.has("reducer 'n' is never read"));
    CHECK_FALSE(analysis.has("reducer 'top' is never read"));
}

TEST_CASE("Reduce: sort keys, keep fields and keep conditions read reducers", "[semantic][rule-reduce][rule-keep]") {
    const auto sorted = analyze("rule Rank:\n" + kPairs + kWhere +
                                "    reduce per body:\n"
                                "        n = count()\n"
                                "    order by:\n"
                                "        n\n"
                                "    on fixed_tick:\n"
                                "        body[Body].x = 1.0\n");
    INFO(describe(sorted));
    CHECK(sorted.clean());
    CHECK(analyze(touch_rule(kReducePerBody, kKeepZones)).clean());
    CHECK(analyze(touch_rule(kReducePerBody, "    keep Seen on body if n == 0\n")).clean());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
