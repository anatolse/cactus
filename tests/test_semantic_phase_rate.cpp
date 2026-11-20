// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
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

const std::string kTraits =
    "trait Pace:\n"
    "    var scale: float = 1.0\n"
    "    var bounded: float clamped(min = 0.0, max = 8.0) = 1.0\n"
    "    var steps: int = 1\n"
    "    let fixed: float = 1.0\n"
    "enum Lane:\n"
    "    Left\n"
    "    Right\n"
    "single trait Lanes:\n"
    "    dim lane: Lane\n"
    "    var speed: float = 1.0\n"
    "entity Clock:\n"
    "    Pace\n"
    "    Lanes\n"
    "trait Other:\n"
    "    var value: float = 1.0\n";

std::string program_with(const std::string& clauses) {
    return "module game\n"
           "extern event frame:\n"
           "    dt: float\n"
           "    delta: duration\n"
           "phase input:\n"
           "    from:\n"
           "        frame\n"
           "phase fixed_tick:\n"
           "    after:\n"
           "        input\n" +
           clauses + "phase tick:\n" + "    after:\n" + "        fixed_tick\n" + "    dt: float = frame.dt\n" +
           kTraits;
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
    [[nodiscard]] std::string describe() const {
        std::string text;
        for (const auto& d : diagnostics) {
            text += std::to_string(d.location.line) + ":" + std::to_string(d.location.column) + ": " + d.message + "\n";
        }
        return text;
    }
    [[nodiscard]] const PhasePlan& plan(const std::string& phase) const {
        const auto found = std::ranges::find_if(program.execution_graph.phases,
                                                [&](const PhasePlan& plan) { return plan.phase.local_name == phase; });
        REQUIRE(found != program.execution_graph.phases.end());
        return *found;
    }
};

std::vector<Diagnostic> parse_into(const std::string& source, std::unique_ptr<ProgramNode>& ast) {
    ErrorReporter errors;
    Lexer lexer(source, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    ast = std::make_unique<ProgramNode>(parser.parse_program());
    return errors.diagnostics();
}

Analysis analyze_clauses(const std::string& clauses) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(program_with(clauses), "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    if (!errors.has_errors()) {
        SemanticAnalyzer analyzer(errors);
        analysis.program = analyzer.analyze(*analysis.ast, ModuleImports{});
        if (!errors.has_errors()) {
            run_whole_program_passes(analysis.program, errors);
        }
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

const PhaseNode& phase_named(const ProgramNode& program, const std::string& name) {
    for (const auto& decl : program.declarations) {
        if (const auto* phase = std::get_if<PhaseNode>(&decl); phase != nullptr && phase->name == name) {
            return *phase;
        }
    }
    FAIL("no phase named " + name);
    std::unreachable();
}

}  // namespace

TEST_CASE("Phase rate: the parser stores rate: on the phase", "[parser][phase-rate]") {
    std::unique_ptr<ProgramNode> ast;
    const auto diagnostics = parse_into(program_with("    every: 1s / 60\n"
                                                     "    rate: Clock[Pace].scale\n"
                                                     "    max: 8\n"),
                                        ast);
    REQUIRE(diagnostics.empty());
    const auto& phase = phase_named(*ast, "fixed_tick");
    REQUIRE(phase.rate.has_value());
    CHECK(std::holds_alternative<MemberExpr>((*phase.rate)->expr));
    CHECK_FALSE(phase_named(*ast, "tick").rate.has_value());
}

TEST_CASE("Phase rate: a second rate: clause is an error", "[parser][phase-rate]") {
    std::unique_ptr<ProgramNode> ast;
    const auto diagnostics = parse_into(program_with("    every: 1s / 60\n"
                                                     "    rate: Clock[Pace].scale\n"
                                                     "    rate: Clock[Pace].bounded\n"),
                                        ast);
    CHECK(std::ranges::any_of(diagnostics,
                              [](const Diagnostic& d) { return d.message.contains("duplicate rate: clause"); }));
}

TEST_CASE("Phase rate: a named float field with a duration every: is recorded", "[semantic][phase-rate]") {
    const auto analysis = analyze_clauses("    every: 1s / 60\n"
                                          "    rate: Clock[Pace].scale\n"
                                          "    max: 8\n");
    INFO(analysis.describe());
    REQUIRE(analysis.clean());
    const auto& resolved = analysis.program.phases.at("fixed_tick");
    REQUIRE(resolved.rate.has_value());
    CHECK(resolved.rate->entity.local_name == "Clock");
    CHECK(resolved.rate->trait.local_name == "Pace");
    CHECK(resolved.rate->field == "scale");
    const auto& plan = analysis.plan("fixed_tick");
    REQUIRE(plan.rate.has_value());
    CHECK(*plan.rate == *resolved.rate);
    CHECK_FALSE(analysis.plan("tick").rate.has_value());
}

TEST_CASE("Phase rate: a float every: and a clamped field are accepted", "[semantic][phase-rate]") {
    const auto analysis = analyze_clauses("    every: 1.0 / 60.0\n"
                                          "    rate: Clock[Pace].bounded\n");
    INFO(analysis.describe());
    REQUIRE(analysis.clean());
    REQUIRE(analysis.plan("fixed_tick").rate.has_value());
    CHECK(analysis.plan("fixed_tick").rate->field == "bounded");
}

TEST_CASE("Phase rate: rate: needs a periodic phase", "[semantic][phase-rate]") {
    const auto analysis = analyze_clauses("    rate: Clock[Pace].scale\n");
    CHECK(analysis.has("'rate:' needs a periodic phase"));
}

TEST_CASE("Phase rate: rate: takes one named float field", "[semantic][phase-rate]") {
    for (const auto* rate : {"0.5", "Clock[Pace].scale * 2.0", "Clock", "frame.dt"}) {
        const auto analysis = analyze_clauses(std::string{"    every: 1s / 60\n    rate: "} + rate + "\n");
        INFO(rate);
        INFO(analysis.describe());
        CHECK(analysis.has("'rate:' takes one named `float` field"));
    }
}

TEST_CASE("Phase rate: the field must be a float", "[semantic][phase-rate]") {
    const auto analysis = analyze_clauses("    every: 1s / 60\n"
                                          "    rate: Clock[Pace].steps\n");
    CHECK(analysis.has("'rate:' field 'Pace.steps' has type int"));
}

TEST_CASE("Phase rate: a cell field is rejected", "[semantic][phase-rate]") {
    const auto analysis = analyze_clauses("    every: 1s / 60\n"
                                          "    rate: Clock[Lanes].speed\n");
    CHECK(analysis.has("'rate:' cannot read cell field 'Lanes.speed'"));
}

TEST_CASE("Phase rate: unknown entity, trait or field is an error", "[semantic][phase-rate]") {
    const auto entity = analyze_clauses("    every: 1s / 60\n"
                                        "    rate: Nobody[Pace].scale\n");
    CHECK(entity.has("'rate:' takes one named `float` field"));
    const auto missing = analyze_clauses("    every: 1s / 60\n"
                                         "    rate: Clock[Other].value\n");
    CHECK(missing.has("entity 'Clock' does not declare trait 'Other'"));
    const auto unknown = analyze_clauses("    every: 1s / 60\n"
                                         "    rate: Clock[Speed].value\n");
    CHECK(unknown.has("entity 'Clock' does not declare trait 'Speed'"));
    const auto field = analyze_clauses("    every: 1s / 60\n"
                                       "    rate: Clock[Pace].speed\n");
    CHECK(field.has("trait 'Pace' has no field 'speed'"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
