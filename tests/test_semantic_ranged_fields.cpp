// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/ranged_fields.hpp"
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
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "const:\n"
    "    SIZE = 16\n"
    "trait Volume:\n"
    "    let min_x: int = 0\n"
    "    let max_x: int = 15\n"
    "    var loose: int = 3\n"
    "trait Cell:\n"
    "    let volume: entity_id\n"
    "    var owner: entity_id\n"
    "    let x: int in range(volume[Volume].min_x, volume[Volume].max_x + 1)\n"
    "entity V:\n"
    "    Volume\n"
    "entity C:\n"
    "    Cell\n";

std::string describe(const std::vector<Diagnostic>& diagnostics) {
    std::string text;
    for (const auto& d : diagnostics) {
        text += std::to_string(d.location.line) + ": " + d.message + "\n";
    }
    return text;
}

struct Analysis {
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] std::vector<std::string> errors() const {
        std::vector<std::string> messages;
        for (const auto& d : diagnostics) {
            if (d.level == DiagnosticLevel::Error) {
                messages.push_back(d.message);
            }
        }
        return messages;
    }
    [[nodiscard]] bool has(const std::string& text) const {
        return std::ranges::any_of(errors(), [&](const std::string& message) { return message.contains(text); });
    }
    [[nodiscard]] bool clean() const {
        return errors().empty();
    }
    [[nodiscard]] const ResolvedField& field(const std::string& trait, const std::string& name) const {
        const auto& fields = program.traits.at(trait).fields;
        const auto found   = std::ranges::find(fields, name, &ResolvedField::name);
        REQUIRE(found != fields.end());
        return *found;
    }
};

Analysis analyze(const std::string& body) {
    ErrorReporter errors;
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    INFO(describe(errors.diagnostics()));
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    auto program = analyzer.analyze(ast);
    validate_container_traits(program, errors);
    program.ast = nullptr;
    return Analysis{.program = std::move(program), .diagnostics = errors.diagnostics()};
}

}  // namespace

TEST_CASE("Ranged fields: bounds may read literals, constants, own let fields and container let fields",
          "[semantic][ranged-fields]") {
    const auto analysis = analyze(
        "trait Grid:\n"
        "    let size: int = 4\n"
        "    let i: int in range(0, SIZE)\n"
        "    let j: int in range(-1, size * 2 + 1)\n"
        "entity G:\n"
        "    Grid\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.clean());
    const auto& x = analysis.field("Cell", "x");
    REQUIRE(x.range.has_value());
    CHECK(x.range->containers == std::vector{make_symbol_id(SymbolKind::Trait, "game", "Volume")});
    REQUIRE(analysis.field("Grid", "i").range.has_value());
    CHECK(analysis.field("Grid", "i").range->containers.empty());
    CHECK_FALSE(analysis.field("Grid", "size").range.has_value());
}

TEST_CASE("Ranged fields: a range needs a let int field", "[semantic][ranged-fields]") {
    const auto analysis = analyze(
        "trait Bad:\n"
        "    var x: int in range(0, 16)\n"
        "    let t: float in range(0, 1)\n"
        "entity B:\n"
        "    Bad\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.has("range on field 'Bad.x' needs a 'let' field"));
    CHECK(analysis.has("range on field 'Bad.t' needs an 'int' field"));
}

TEST_CASE("Ranged fields: a state variant field cannot declare a range", "[semantic][ranged-fields]") {
    const auto analysis = analyze(
        "state Mode:\n"
        "    Idle:\n"
        "        let x: int in range(0, 4)\n"
        "entity M:\n"
        "    Mode\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.has("range on field 'Mode.Idle.x' is not allowed on a state variant"));
}

TEST_CASE("Ranged fields: rejected bound reads name the bound", "[semantic][ranged-fields]") {
    const auto analysis = analyze(
        "func four() int:\n"
        "    return 4\n"
        "trait Bad:\n"
        "    let volume: entity_id\n"
        "    var loose: int = 1\n"
        "    let a: int in range(0, volume[Volume].loose)\n"
        "    let b: int in range(0, owner[Volume].max_x)\n"
        "    let c: int in range(0, loose)\n"
        "    let d: int in range(0, four())\n"
        "    let e: int in range(0, V[Volume].max_x)\n"
        "    let f: int in range(0, 1.5)\n"
        "    let g: int in range(0, volume[Volume].missing)\n"
        "    let h: int in range(0, volume[Missing].max_x)\n"
        "entity B:\n"
        "    Bad\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.has("range bound of 'Bad.a' reads 'Volume.loose', which is not a 'let' field"));
    CHECK(analysis.has("range bound of 'Bad.b' reads through 'owner', which is not a 'let entity_id' field of 'Bad'"));
    CHECK(analysis.has("range bound of 'Bad.c' reads 'Bad.loose', which is not a 'let' field"));
    CHECK(analysis.has("range bound of 'Bad.d' can't call a function"));
    CHECK(analysis.has("range bound of 'Bad.e' reads through 'V', which is not a 'let entity_id' field of 'Bad'"));
    CHECK(analysis.has("range bound of 'Bad.f' must be an 'int' expression"));
    CHECK(analysis.has("range bound of 'Bad.g' reads unknown field 'Volume.missing'"));
    CHECK(analysis.has("range bound of 'Bad.h' reads unknown trait 'Missing'"));
}

TEST_CASE("Ranged fields: adding or projecting a container trait is rejected", "[semantic][ranged-fields]") {
    const auto analysis = analyze(
        "rule Grow:\n"
        "    each:\n"
        "        Cell\n"
        "    on tick:\n"
        "        add Volume:\n"
        "            max_x = 8\n"
        "rule Show:\n"
        "    each:\n"
        "        Cell\n"
        "    on tick:\n"
        "        project Volume:\n"
        "            loose = 1\n"
        "extern rule Host:\n"
        "    on tick:\n"
        "        commands:\n"
        "            add Volume\n");
    INFO(describe(analysis.diagnostics));
    const std::string reason = "container trait 'Volume': ranged field 'Cell.x' reads it";
    CHECK(analysis.has("rule 'Grow' can't add " + reason));
    CHECK(analysis.has("rule 'Show' can't project " + reason));
    CHECK(analysis.has("rule 'Host' can't add " + reason));
}

TEST_CASE("Ranged fields: spawning, removing and setting other traits stay allowed", "[semantic][ranged-fields]") {
    const auto analysis = analyze(
        "template VolumeT:\n"
        "    Volume\n"
        "rule Make:\n"
        "    on tick:\n"
        "        spawn VolumeT:\n"
        "            Volume:\n"
        "                max_x = 7\n"
        "rule Drop:\n"
        "    each:\n"
        "        Volume as v\n"
        "    on tick:\n"
        "        v.loose = 2\n"
        "        remove Volume\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.clean());
}

TEST_CASE("Ranged fields: several errors are reported in one compile", "[semantic][ranged-fields]") {
    const auto analysis = analyze(
        "trait Bad:\n"
        "    var x: int in range(0, 16)\n"
        "    let y: int in range(0, 1.5)\n"
        "entity B:\n"
        "    Bad\n"
        "rule Grow:\n"
        "    each:\n"
        "        Cell\n"
        "    on tick:\n"
        "        add Volume\n");
    INFO(describe(analysis.diagnostics));
    CHECK(analysis.errors().size() == 3);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
