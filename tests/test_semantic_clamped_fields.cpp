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
#include <unordered_set>
#include <vector>

using namespace cactus;

namespace {

const std::string kPrelude =
    "module game\n"
    "use std.math as math\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "const:\n"
    "    CAP = 10\n";

ImportedSymbols math_import() {
    ImportedSymbols symbols;
    symbols.module_name   = "std.math";
    const auto float_type = make_float_type();
    ResolvedFunc func;
    func.name           = "max";
    func.module_name    = "std.math";
    func.is_pub         = true;
    func.effect_summary = std::unordered_set<std::string>{};
    func.params = {ResolvedParam{.name = "a", .type = float_type}, ResolvedParam{.name = "b", .type = float_type}};
    func.return_type     = float_type;
    const auto symbol    = make_symbol_id(SymbolKind::Func, "std.math", "max");
    func.symbol_id       = symbol;
    func.canonical_id    = make_canonical_id(symbol);
    symbols.funcs["max"] = std::move(func);
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
    [[nodiscard]] const ResolvedField& field(const std::string& trait, const std::string& name) const {
        const auto& fields = program.traits.at(trait).fields;
        const auto found   = std::ranges::find(fields, name, &ResolvedField::name);
        REQUIRE(found != fields.end());
        return *found;
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
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ":" + std::to_string(d.location.column) + ": " + d.message + "\n";
    }
    return text;
}

Analysis analyze(const std::string& body) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    if (!errors.has_errors()) {
        ModuleImports imports;
        imports.add("math", math_import());
        SemanticAnalyzer analyzer(errors);
        analysis.program = analyzer.analyze(*analysis.ast, imports);
        if (!errors.has_errors()) {
            run_whole_program_passes(analysis.program, errors);
        }
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

void check_error(const std::string& body, const std::string& text) {
    const auto analysis = analyze(body);
    INFO(describe(analysis));
    CHECK(analysis.has(text));
}

std::string with_entity(const std::string& traits, const std::string& trait = "Health") {
    return traits + "entity E:\n    " + trait + "\n";
}

}  // namespace

TEST_CASE("Clamped fields: one- and two-sided bounds are accepted", "[semantic][clamped-fields]") {
    const auto analysis = analyze(with_entity("trait Health:\n"
                                              "    var hp: float clamped(min = 0.0, max = 100.0) = 100.0\n"
                                              "    var armor: int clamped(min = 0) = 3\n"
                                              "    var rage: int clamped(max = CAP * 2) = 0\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& hp = analysis.field("Health", "hp");
    REQUIRE(hp.clamp.has_value());
    CHECK(hp.clamp->min.has_value());
    CHECK(hp.clamp->max.has_value());
    const auto& armor = analysis.field("Health", "armor");
    REQUIRE(armor.clamp.has_value());
    CHECK(armor.clamp->min.has_value());
    CHECK_FALSE(armor.clamp->max.has_value());
    const auto& rage = analysis.field("Health", "rage");
    REQUIRE(rage.clamp.has_value());
    CHECK_FALSE(rage.clamp->min.has_value());
    REQUIRE(rage.clamp->max.has_value());
    CHECK(rage.clamp->max->let_field.empty());
}

TEST_CASE("Clamped fields: a bound may read a let field of the same trait", "[semantic][clamped-fields]") {
    const auto analysis = analyze(with_entity("trait Health:\n"
                                              "    let max_hp: float = 100.0\n"
                                              "    var hp: float clamped(min = 0.0, max = max_hp) = 100.0\n"));
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& hp = analysis.field("Health", "hp");
    REQUIRE(hp.clamp.has_value());
    REQUIRE(hp.clamp->max.has_value());
    CHECK(hp.clamp->max->let_field == "max_hp");
}

TEST_CASE("Clamped fields: a duration field may be clamped", "[semantic][clamped-fields]") {
    const auto analysis = analyze(with_entity("trait Needs:\n"
                                              "    var fatigue: duration clamped(min = 0s) = 0s\n",
                                              "Needs"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Clamped fields: a bound reading a var field is rejected", "[semantic][clamped-fields]") {
    check_error(with_entity("trait Health:\n"
                            "    var limit: float = 50.0\n"
                            "    var hp: float clamped(max = limit) = 10.0\n"),
                "clamped bound of 'Health.hp' reads 'limit', which is not a 'let' field: a bound must be fixed for "
                "the field's life");
}

TEST_CASE("Clamped fields: a clamped let field is rejected with a hint", "[semantic][clamped-fields]") {
    check_error(with_entity("trait Health:\n"
                            "    let hp: int clamped(min = 0) = 1\n"),
                "'clamped' on field 'Health.hp' needs a 'var' field; a 'let' field uses 'in range'");
}

TEST_CASE("Clamped fields: wrong field and bound types are rejected", "[semantic][clamped-fields]") {
    const auto analysis = analyze(with_entity("trait Health:\n"
                                              "    let low: int = 0\n"
                                              "    var flag: bool clamped(min = 0) = false\n"
                                              "    var pos: vec2 clamped(min = 0.0)\n"
                                              "    var hp: float clamped(min = 0) = 1.0\n"
                                              "    var count: int clamped(max = 2.5) = 1\n"
                                              "    var wait: duration clamped(min = low) = 0s\n"));
    INFO(describe(analysis));
    CHECK(analysis.has("'clamped' on field 'Health.flag' needs an 'int', 'float' or 'duration' field"));
    CHECK(analysis.has("'clamped' on field 'Health.pos' needs an 'int', 'float' or 'duration' field"));
    CHECK(analysis.has("clamped bound of 'Health.hp' must be a 'float' expression"));
    CHECK(analysis.has("clamped bound of 'Health.count' must be an 'int' expression"));
    CHECK(analysis.has("clamped bound of 'Health.wait' must be a 'duration' expression"));
}

TEST_CASE("Clamped fields: bounds that read anything else are rejected", "[semantic][clamped-fields]") {
    const auto analysis = analyze("func four() int:\n"
                                  "    return 4\n"
                                  "trait Other:\n"
                                  "    let cap: int = 3\n"
                                  "trait Health:\n"
                                  "    let owner: entity_id\n"
                                  "    var a: int clamped(max = four()) = 0\n"
                                  "    var b: int clamped(max = owner[Other].cap) = 0\n"
                                  "    var c: int clamped(max = missing) = 0\n"
                                  "entity E:\n"
                                  "    Health\n"
                                  "    Other\n");
    INFO(describe(analysis));
    const std::string reads = "can read only literals, constants and 'let' fields of 'Health'";
    CHECK(analysis.has("clamped bound of 'Health.a' " + reads));
    CHECK(analysis.has("clamped bound of 'Health.b' " + reads));
    CHECK(analysis.has("clamped bound of 'Health.c' reads unknown field 'missing'"));
}

TEST_CASE("Clamped fields: a clamp needs at least one bound", "[semantic][clamped-fields]") {
    check_error(with_entity("trait Health:\n"
                            "    var hp: int clamped() = 1\n"),
                "'clamped' on field 'Health.hp' needs 'min', 'max' or both");
}

TEST_CASE("Clamped fields: constant bounds with min above max are rejected", "[semantic][clamped-fields]") {
    check_error(with_entity("trait Health:\n"
                            "    var hp: float clamped(min = 5.0, max = 1.0) = 1.0\n"
                            "    var n: int clamped(min = CAP, max = 2) = 2\n"),
                "clamped bounds of 'Health.hp' cross: min 5.0 is above max 1.0");
    check_error(with_entity("trait Health:\n"
                            "    var n: int clamped(min = CAP, max = 2) = 2\n"),
                "clamped bounds of 'Health.n' cross: min 10 is above max 2");
}

TEST_CASE("Clamped fields: a cell field and a state variant field can't be clamped", "[semantic][clamped-fields]") {
    check_error("trait Field:\n"
                "    dim x: int in range(0, 4)\n"
                "    var gas: float clamped(min = 0.0) = 0.0\n"
                "entity F:\n"
                "    Field\n",
                "'clamped' on field 'Field.gas' is not supported on a cell field");
    check_error("state Mode:\n"
                "    Idle:\n"
                "        var t: int clamped(min = 0) = 0\n"
                "entity M:\n"
                "    Mode\n",
                "'clamped' on field 'Mode.Idle.t' is not allowed on a state variant");
}

TEST_CASE("Clamped fields: clamping changes no write kind and adds no schedule edge", "[semantic][clamped-fields]") {
    const auto analysis = analyze(with_entity("trait Health:\n"
                                              "    let max_hp: float = 100.0\n"
                                              "    var hp: float clamped(min = 0.0, max = max_hp) = 100.0\n"
                                              "    var best: float clamped(max = 10.0) = 0.0\n") +
                                  "rule Heal:\n"
                                  "    each:\n"
                                  "        Health as h\n"
                                  "    on tick:\n"
                                  "        h.hp += 1.0\n"
                                  "        h.best = math.max(h.best, 2.0)\n"
                                  "rule Hurt:\n"
                                  "    each:\n"
                                  "        Health as h\n"
                                  "    on tick:\n"
                                  "        h.hp -= 1.0\n"
                                  "        h.best = math.max(h.best, 3.0)\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto trait = make_symbol_id(SymbolKind::Trait, "game", "Health");
    CHECK(write_kind(analysis.contract("Heal"), trait, "hp") == FieldWriteKind{.kind = WriteKind::Accum});
    CHECK(write_kind(analysis.contract("Hurt"), trait, "hp") == FieldWriteKind{.kind = WriteKind::Accum});
    CHECK(write_kind(analysis.contract("Heal"), trait, "best") ==
          FieldWriteKind{.kind = WriteKind::Join, .op = JoinOp::Max});
    CHECK_FALSE(analysis.has_edge("Heal", "Hurt"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
