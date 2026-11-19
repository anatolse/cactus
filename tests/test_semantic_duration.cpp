// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace cactus;

namespace {

const std::string kPhases =
    "extern event frame:\n"
    "    dt: float\n"
    "    delta: duration\n"
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "    every: 1s / 60\n"
    "    max: 8\n"
    "phase tick:\n"
    "    after:\n"
    "        fixed_tick\n"
    "    dt: float = frame.dt\n"
    "    delta: duration = frame.delta\n";

const std::string kTraits =
    "trait Cooldown:\n"
    "    var left: duration = 0s\n"
    "    var total: duration = 2s\n"
    "    var best: duration = 0s\n"
    "    var ratio: float = 0.0\n"
    "    var secs: float = 0.0\n"
    "    var count: int = 0\n"
    "entity Gun:\n"
    "    Cooldown\n";

ImportedSymbols math_import() {
    ImportedSymbols symbols;
    symbols.module_name = "std.math";
    const auto float_type = make_float_type();
    for (const std::string name : {"max", "min"}) {
        ResolvedFunc func;
        func.name           = name;
        func.module_name    = "std.math";
        func.is_pub         = true;
        func.effect_summary = std::unordered_set<std::string>{};
        func.params         = {ResolvedParam{.name = "a", .type = float_type},
                               ResolvedParam{.name = "b", .type = float_type}};
        func.return_type    = float_type;
        const auto symbol   = make_symbol_id(SymbolKind::Func, "std.math", name);
        func.symbol_id      = symbol;
        func.canonical_id   = make_canonical_id(symbol);
        symbols.funcs[name] = std::move(func);
    }
    return symbols;
}

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
    [[nodiscard]] bool has_edge(const std::string& first, const std::string& second) const {
        return std::ranges::any_of(program.execution_graph.schedule_edges, [&](const ScheduleEdge& edge) {
            const auto& before = edge.before.rule.local_name;
            const auto& after  = edge.after.rule.local_name;
            return (before == first && after == second) || (before == second && after == first);
        });
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
        text += std::to_string(d.location.line) + ":" + std::to_string(d.location.column) + ": " + d.message + "\n";
    }
    return text;
}

Analysis analyze_source(const std::string& source) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(source, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    ModuleImports imports;
    imports.add("math", math_import());
    SemanticAnalyzer analyzer(errors);
    analysis.program = analyzer.analyze(*analysis.ast, imports);
    if (!errors.has_errors()) {
        run_whole_program_passes(analysis.program, errors);
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

Analysis analyze(const std::string& body, const std::string& phases = kPhases) {
    return analyze_source("module game\nuse std.math as math\n" + phases + kTraits + body);
}

std::string cooldown_rule(const std::string& body, const std::string& phase = "tick") {
    return "rule Update:\n"
           "    each:\n"
           "        Cooldown as c\n"
           "    on " +
           phase + " as step:\n" + body;
}

void check_error(const std::string& body, const std::string& text) {
    const auto analysis = analyze(body);
    INFO(describe(analysis));
    CHECK(analysis.has(text));
}

void check_clean(const std::string& body) {
    const auto analysis = analyze(body);
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

}  // namespace

// ── Conversions ─────────────────────────────────────────────────────────────

TEST_CASE("Duration: a duration field takes a literal default", "[semantic][dsl-duration]") {
    const auto analysis = analyze("");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Duration: a float constant is rejected and a literal is suggested", "[semantic][dsl-duration]") {
    check_error(cooldown_rule("        c.left = 0.5\n"), "`0.5s`");
    check_error("trait Bad:\n    var wait: duration = 0.5\n", "`0.5s`");
    check_error(cooldown_rule("        c.left = 0\n"), "`0s`");
}

TEST_CASE("Duration: a run-time float is rejected and duration(...) is suggested", "[semantic][dsl-duration]") {
    check_error(cooldown_rule("        c.left = c.secs\n"), "duration(");
    check_clean(cooldown_rule("        c.left = duration(c.secs)\n"));
    check_clean(cooldown_rule("        c.secs = seconds(c.left)\n"));
}

TEST_CASE("Duration: a duration is not a float", "[semantic][dsl-duration]") {
    check_error(cooldown_rule("        c.secs = c.left\n"), "seconds(");
    check_error(cooldown_rule("        let x: float = c.left\n"), "seconds(");
}

TEST_CASE("Duration: step.dt in a duration expression suggests step.delta", "[semantic][dsl-duration]") {
    check_error(cooldown_rule("        c.left -= step.dt\n"), "step.delta");
    check_error(cooldown_rule("        c.left = c.left - step.dt\n"), "step.delta");
    check_clean(cooldown_rule("        c.left -= step.delta\n"));
}

TEST_CASE("Duration: scaling by a float is rejected", "[semantic][dsl-duration]") {
    check_error(cooldown_rule("        c.left = c.total * 0.5\n"), "duration(seconds(c.total) * 0.5)");
    check_error(cooldown_rule("        c.left = c.total / 0.5\n"), "duration(seconds(c.total) / 0.5)");
}

TEST_CASE("Duration: % is rejected", "[semantic][dsl-duration]") {
    check_error(cooldown_rule("        c.left = c.total % 2\n"), "`%`");
}

TEST_CASE("Duration: int scaling, int division and the ratio type", "[semantic][dsl-duration]") {
    check_clean(cooldown_rule("        c.left = c.total / 3\n"
                              "        c.left = 3 * c.total + c.total * c.count - -c.left\n"
                              "        c.ratio = c.left / c.total\n"
                              "        c.best = math.max(c.best, c.left)\n"
                              "        c.best = math.min(c.best, c.left)\n"
                              "        if c.left > 0s and c.left != c.total:\n"
                              "            c.count += 1\n"));
    check_error(cooldown_rule("        c.ratio = c.left / 3\n"), "seconds(");
    check_error(cooldown_rule("        c.left = c.total + 1\n"), "duration");
    check_error(cooldown_rule("        let x = 3 / c.total\n"), "duration");
}

TEST_CASE("Duration: a comparison with a float is rejected", "[semantic][dsl-duration]") {
    check_error(cooldown_rule("        if c.left > 0.0:\n"
                              "            c.count += 1\n"),
                "`0.0s`");
}

TEST_CASE("Duration: a struct field and an event field are checked", "[semantic][dsl-duration]") {
    check_error("event Restart:\n"
                "    wait: duration\n" +
                    cooldown_rule("        emit Restart:\n"
                                  "            wait = 1.5\n"),
                "`1.5s`");
}

// ── Phases ──────────────────────────────────────────────────────────────────

TEST_CASE("Duration: every: takes a duration and synthesizes delta", "[semantic][dsl-duration][phase-activation]") {
    const auto analysis = analyze("");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& phase = analysis.program.phases.at("fixed_tick");
    REQUIRE(phase.every_units.has_value());
    CHECK(*phase.every_units == 11'760'000);
    REQUIRE(phase.every_seconds.has_value());
    CHECK(std::bit_cast<std::uint32_t>(static_cast<float>(*phase.every_seconds)) ==
          std::bit_cast<std::uint32_t>(static_cast<float>(1.0 / 60.0)));
    const auto delta = std::ranges::find_if(phase.fields, [](const ResolvedField& f) { return f.name == "delta"; });
    REQUIRE(delta != phase.fields.end());
    CHECK(delta->type.kind == TypeKind::Duration);
    CHECK(delta->is_synthesized);
}

TEST_CASE("Duration: a float every: gives delta as duration(every)", "[semantic][dsl-duration][phase-activation]") {
    auto phases = kPhases;
    phases.replace(phases.find("1s / 60"), 7, "1.0 / 60.0");
    const auto analysis = analyze("", phases);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.program.phases.at("fixed_tick").every_units == 11'760'001);
}

TEST_CASE("Duration: every: rejects a zero or negative duration", "[semantic][dsl-duration][phase-activation]") {
    for (const auto* period : {"0s", "-1s", "1s / 0"}) {
        auto phases = kPhases;
        phases.replace(phases.find("1s / 60"), 7, period);
        const auto analysis = analyze("", phases);
        INFO(period);
        CHECK(analysis.has("every value must be a positive compile-time"));
    }
}

TEST_CASE("Duration: fixed_tick.delta is a constant expression", "[semantic][dsl-duration][phase-activation]") {
    check_clean("const:\n"
                "    GRACE = fixed_tick.delta * 6\n" +
                cooldown_rule("        c.left = GRACE\n"));
}

TEST_CASE("Duration: tick.delta outside a tick handler is rejected", "[semantic][dsl-duration][phase-activation]") {
    check_error("const:\n"
                "    GRACE = tick.delta * 6\n",
                "'tick.delta' is only available inside 'tick' handlers");
}

TEST_CASE("Duration: a timer on the variable tick reads step.delta", "[semantic][dsl-duration][phase-activation]") {
    check_clean(cooldown_rule("        c.left -= step.delta\n", "tick"));
}

// ── Write kinds ─────────────────────────────────────────────────────────────

TEST_CASE("Duration: a countdown is Accum and a running max is Join", "[semantic][dsl-duration][handler-contracts]") {
    const auto trait = make_symbol_id(SymbolKind::Trait, "game", "Cooldown");
    const auto accum = analyze(cooldown_rule("        c.left -= step.delta\n", "fixed_tick"));
    INFO(describe(accum));
    REQUIRE(accum.clean());
    CHECK(write_kind(accum.contract("Update"), trait, "left") == FieldWriteKind{.kind = WriteKind::Accum});
    const auto join = analyze(cooldown_rule("        c.best = math.max(c.best, step.delta)\n", "fixed_tick"));
    INFO(describe(join));
    REQUIRE(join.clean());
    CHECK(write_kind(join.contract("Update"), trait, "best") ==
          FieldWriteKind{.kind = WriteKind::Join, .op = JoinOp::Max});
}

TEST_CASE("Duration: two unordered countdowns of one timer need no edge", "[semantic][dsl-duration][handler-contracts]") {
    const auto analysis = analyze("rule First:\n"
                                  "    each:\n"
                                  "        Cooldown as c\n"
                                  "    on fixed_tick as step:\n"
                                  "        c.left -= step.delta\n"
                                  "rule Second:\n"
                                  "    each:\n"
                                  "        Cooldown as c\n"
                                  "    on fixed_tick as step:\n"
                                  "        c.left -= step.delta\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK_FALSE(analysis.has_edge("First", "Second"));
}

// ── Data model ──────────────────────────────────────────────────────────────

TEST_CASE("Duration: a cell field may be a duration with a constant default", "[semantic][dsl-duration][trait-dimensions]") {
    check_clean("trait Field:\n"
                "    dim x: int in range(0, 16)\n"
                "    var regrow: duration = 0s\n");
    check_error("trait Field:\n"
                "    dim x: int in range(0, 16)\n"
                "    var regrow: duration\n",
                "constant default");
}

namespace {

const std::string kSquads =
    "trait Squad:\n"
    "    var wait: duration = 0s\n"
    "    var longest: duration = 0s\n"
    "    var slowest: entity_id\n"
    "trait Member:\n"
    "    var squad: entity_id\n"
    "    var wait: duration = 0s\n"
    "    var speed: float = 0.0\n"
    "entity Alpha:\n"
    "    Squad:\n"
    "        slowest = Alpha\n"
    "entity Bob:\n"
    "    Member:\n"
    "        squad = Alpha\n";

std::string squad_rule(const std::string& reduce_lines) {
    return kSquads +
           "rule Waits:\n"
           "    pairs:\n"
           "        squad:\n"
           "            Squad\n"
           "        member:\n"
           "            Member\n"
           "    where:\n"
           "        member[Member].squad == squad\n"
           "    reduce per squad:\n" +
           reduce_lines +
           "    on tick:\n"
           "        squad[Squad].wait = total\n"
           "        squad[Squad].longest = math.max(longest, shortest)\n"
           "        squad[Squad].slowest = slowest\n";
}

}  // namespace

TEST_CASE("Duration: sum, min, max and best take durations", "[semantic][dsl-duration][rule-reduce]") {
    const auto analysis = analyze(squad_rule("        total = sum(member[Member].wait + fixed_tick.delta)\n"
                                             "        longest = max(member[Member].wait, default = 0s)\n"
                                             "        shortest = min(member[Member].wait, default = 1s)\n"
                                             "        slowest = best(member, by = member[Member].wait)\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& reducers = analysis.rule("Waits").reduce->reducers;
    REQUIRE(reducers.size() == 4);
    CHECK(reducers[0].result_type.kind == TypeKind::Duration);
    CHECK(reducers[1].result_type.kind == TypeKind::Duration);
    CHECK(reducers[2].result_type.kind == TypeKind::Duration);
    CHECK(reducers[3].result_type.kind == TypeKind::EntityId);
}

TEST_CASE("Duration: tick.delta in a reducer is rejected", "[semantic][dsl-duration][phase-activation]") {
    check_error(squad_rule("        total = sum(member[Member].wait + tick.delta)\n"
                           "        longest = max(member[Member].wait, default = 0s)\n"
                           "        shortest = min(member[Member].wait, default = 1s)\n"
                           "        slowest = best(member, by = member[Member].wait)\n"),
                "'tick.delta' is only available inside 'tick' handlers");
}

TEST_CASE("Duration: a duration max needs a duration default", "[semantic][dsl-duration][rule-reduce]") {
    check_error(squad_rule("        total = sum(member[Member].wait)\n"
                           "        longest = max(member[Member].wait, default = 0.0)\n"
                           "        shortest = min(member[Member].wait, default = 1s)\n"
                           "        slowest = best(member, by = member[Member].wait)\n"),
                "max(...) default must be of type 'duration'");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
