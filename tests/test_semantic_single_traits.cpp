// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
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
    "single trait Terrain:\n"
    "    var size: int = 1\n"
    "trait Unit:\n"
    "    var x: float = 0.0\n"
    "trait Log:\n"
    "    var n: int = 0\n"
    "trait Parent:\n"
    "    let parent: entity_id\n"
    "event Ping\n"
    "template Ground:\n"
    "    Terrain\n"
    "template Grunt:\n"
    "    Unit\n"
    "entity Recorder:\n"
    "    Log\n";

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
    [[nodiscard]] bool has(const std::string& text) const {
        return error(text) != nullptr;
    }
    [[nodiscard]] bool clean() const {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
    [[nodiscard]] bool unaccelerated(const std::string& rule) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Warning &&
                   d.message.starts_with("pair rule '" + rule + "' is not accelerated");
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

std::vector<Diagnostic> parse_errors(const std::string& source, std::unique_ptr<ProgramNode>& ast) {
    ErrorReporter errors;
    Lexer lexer(source, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    ast = std::make_unique<ProgramNode>(parser.parse_program());
    return errors.diagnostics();
}

// Analyzes `source`, then runs the whole-program passes when analysis is clean.
Analysis analyze_source(const std::string& source) {
    Analysis analysis;
    analysis.diagnostics = parse_errors(source, analysis.ast);
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

const TraitNode& trait_node(const ProgramNode& program, const std::string& name) {
    for (const auto& decl : program.declarations) {
        if (const auto* trait = std::get_if<TraitNode>(&decl); trait != nullptr && trait->name == name) {
            return *trait;
        }
    }
    FAIL("no trait named " + name);
    std::unreachable();
}

}  // namespace

// ── Singleton declaration ───────────────────────────────────────────────────

TEST_CASE("Parser: single marks a trait declaration", "[parser][single-traits]") {
    std::unique_ptr<ProgramNode> ast;
    const auto errors = parse_errors(
        "module game\n"
        "single trait Terrain\n"
        "pub single trait Sky:\n"
        "    var single: bool = false\n"
        "trait Rock\n",
        ast);
    REQUIRE(errors.empty());
    CHECK(trait_node(*ast, "Terrain").is_single);
    CHECK_FALSE(trait_node(*ast, "Terrain").is_pub);
    const auto& sky = trait_node(*ast, "Sky");
    CHECK(sky.is_single);
    CHECK(sky.is_pub);
    REQUIRE(sky.fields.size() == 1);
    CHECK(sky.fields[0].name == "single");
    CHECK_FALSE(trait_node(*ast, "Rock").is_single);
}

TEST_CASE("Parser: a single state is rejected at single", "[parser][single-traits]") {
    std::unique_ptr<ProgramNode> ast;
    const auto errors = parse_errors(
        "module game\n"
        "single state Mode:\n"
        "    Idle\n",
        ast);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].message.contains("only a trait can be single"));
    CHECK(errors[0].location.line == 2);
    CHECK(errors[0].location.column == 1);
}

TEST_CASE("Semantic: a single trait works like any other trait", "[semantic][single-traits]") {
    const auto analysis = analyze(
        "entity Field:\n"
        "    Terrain\n"
        "rule Grow:\n"
        "    each: Terrain as t\n"
        "    on tick:\n"
        "        t.size += 1\n"
        "rule Swap:\n"
        "    each: Log as log\n"
        "    on tick:\n"
        "        remove Terrain from Field\n"
        "        add Terrain to Recorder:\n"
        "            size = 2\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK(analysis.program.traits.at("Terrain").is_single);
    CHECK_FALSE(analysis.program.traits.at("Unit").is_single);
}

// ── Singleton restrictions ──────────────────────────────────────────────────

TEST_CASE("Semantic: two named carriers of a single trait are rejected", "[semantic][single-traits]") {
    const auto analysis = analyze(
        "entity First:\n"
        "    Terrain\n"
        "entity Second from Ground:\n"
        "    Terrain:\n"
        "        size = 2\n");
    INFO(describe(analysis));
    const auto* error = analysis.error("single trait 'game.Terrain' is carried by two named entities");
    REQUIRE(error != nullptr);
    CHECK(error->message.contains("'game.First'"));
    CHECK(error->message.contains("'game.Second'"));
}

TEST_CASE("Semantic: one tree with two carriers of a single trait is rejected", "[semantic][single-traits]") {
    SECTION("a template") {
        const auto analysis = analyze(
            "template Holder:\n"
            "    Terrain\n"
            "    children:\n"
            "        entity Plot:\n"
            "            Terrain\n");
        INFO(describe(analysis));
        const auto* error = analysis.error("'game.Holder' carries single trait 'game.Terrain' on more than one node");
        REQUIRE(error != nullptr);
        CHECK(error->location.line == 22);
    }
    SECTION("a named entity") {
        const auto analysis = analyze(
            "entity Holder:\n"
            "    Unit\n"
            "    children:\n"
            "        entity Left:\n"
            "            Terrain\n"
            "        entity Right:\n"
            "            Terrain\n");
        INFO(describe(analysis));
        CHECK(analysis.has("'game.Holder' carries single trait 'game.Terrain' on more than one node"));
    }
    SECTION("one carrier per tree is fine") {
        const auto analysis = analyze(
            "template Holder:\n"
            "    Unit\n"
            "    children:\n"
            "        entity Plot:\n"
            "            Terrain\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
}

TEST_CASE("Semantic: a single trait can't be projected or kept", "[semantic][single-traits]") {
    SECTION("project") {
        const auto analysis = analyze(
            "rule Show:\n"
            "    each: Unit\n"
            "    on tick:\n"
            "        project Terrain\n");
        INFO(describe(analysis));
        const auto* error = analysis.error("single trait 'Terrain' can't be projected");
        REQUIRE(error != nullptr);
        CHECK(error->location.line == 25);
    }
    SECTION("keep") {
        const auto analysis = analyze(
            "rule Hold:\n"
            "    each: Unit\n"
            "    keep Terrain\n");
        INFO(describe(analysis));
        const auto* error = analysis.error("single trait 'Terrain' can't be kept");
        REQUIRE(error != nullptr);
        CHECK(error->location.line == 24);
    }
}

// ── Two producers need an order ─────────────────────────────────────────────

TEST_CASE("Scheduler: two unordered producers of a single trait conflict", "[scheduler][single-traits]") {
    SECTION("two spawns") {
        const auto analysis = analyze(
            "rule MakeA:\n"
            "    each: Log\n"
            "    on tick:\n"
            "        spawn Ground()\n"
            "rule MakeB:\n"
            "    each: Log\n"
            "    on tick:\n"
            "        spawn Ground()\n");
        INFO(describe(analysis));
        const auto* error = analysis.error("unorientable conflict between 'game.MakeA");
        REQUIRE(error != nullptr);
        CHECK(error->message.contains("game.MakeB"));
        CHECK(error->message.contains("single trait game.Terrain"));
    }
    SECTION("an add and a spawn") {
        const auto analysis = analyze(
            "rule Give:\n"
            "    each: Unit as u\n"
            "    on tick:\n"
            "        add Terrain to u\n"
            "rule Make:\n"
            "    each: Log\n"
            "    on tick:\n"
            "        spawn Ground()\n");
        INFO(describe(analysis));
        CHECK(analysis.has("single trait game.Terrain"));
    }
}

TEST_CASE("Scheduler: ordered producers of a single trait compile", "[scheduler][single-traits]") {
    const auto analysis = analyze(
        "rule MakeA:\n"
        "    each: Log\n"
        "    on tick:\n"
        "        spawn Ground()\n"
        "rule MakeB:\n"
        "    each: Log\n"
        "    after: MakeA\n"
        "    on tick:\n"
        "        spawn Ground()\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Scheduler: destroying and spawning a single trait need no order", "[scheduler][single-traits]") {
    const auto analysis = analyze(
        "rule Clear:\n"
        "    each: Terrain as t\n"
        "    on tick:\n"
        "        destroy t\n"
        "rule Strip:\n"
        "    each: Terrain as t\n"
        "    on tick:\n"
        "        remove Terrain from t\n"
        "rule Make:\n"
        "    each: Log\n"
        "    on tick:\n"
        "        spawn Ground()\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Scheduler: producers of different traits need no order", "[scheduler][single-traits]") {
    const auto analysis = analyze(
        "rule MakeA:\n"
        "    each: Log\n"
        "    on tick:\n"
        "        spawn Ground()\n"
        "rule MakeB:\n"
        "    each: Log\n"
        "    on tick:\n"
        "        spawn Grunt()\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

namespace {

// Declared in std.editor, so the call resolves to the editor's native spawn.
Analysis analyze_editor(const std::string& templates) {
    auto source = kPrelude;
    source.replace(0, std::string{"module game"}.size(), "module std.editor");
    return analyze_source(source +
                          "pub extern func spawn_template(template_name: string, position_2d: vec2, position_3d: vec3) "
                          "entity_id\n" +
                          templates +
                          "rule Palette:\n"
                          "    each: Log\n"
                          "    on tick:\n"
                          "        let id = spawn_template(\"Base\", vec2(0.0, 0.0), vec3(0.0, 0.0, 0.0))\n"
                          "rule Make:\n"
                          "    each: Log\n"
                          "    on tick:\n"
                          "        spawn Ground()\n");
}

}  // namespace

TEST_CASE("Scheduler: the editor's spawn gives the single traits of pub templates", "[scheduler][single-traits]") {
    SECTION("a pub template carries the trait") {
        const auto analysis = analyze_editor(
            "pub template Base:\n"
            "    Terrain\n");
        INFO(describe(analysis));
        const auto* error = analysis.error("single trait std.editor.Terrain");
        REQUIRE(error != nullptr);
        CHECK(error->message.contains("std.editor.Palette"));
    }
    SECTION("no pub template carries it") {
        const auto analysis = analyze_editor(
            "pub template Base:\n"
            "    Unit\n");
        INFO(describe(analysis));
        CHECK(analysis.clean());
    }
}

// ── A singleton bounds a query ──────────────────────────────────────────────

namespace {

std::string pair_rule(const std::string& name, const std::string& first, const std::string& second) {
    return "rule " + name +
           ":\n"
           "    pairs:\n"
           "        a: " +
           first +
           "\n"
           "        b: " +
           second +
           "\n"
           "    on tick:\n"
           "        emit Ping to a\n";
}

// Units and terrain are both spawned, so no named entity bounds either.
const std::string kSpawnBoth =
    "template Settler:\n"
    "    Unit\n"
    "    Terrain\n"
    "rule Make:\n"
    "    each: Log\n"
    "    on tick:\n"
    "        spawn Grunt()\n"
    "        spawn Settler()\n";

}  // namespace

TEST_CASE("Diagnostics: a single binding clears the pair warning", "[diagnostics][single-traits]") {
    const auto analysis = analyze(kSpawnBoth +
                                  "rule Near:\n"
                                  "    pairs:\n"
                                  "        a: Unit\n"
                                  "        t: Terrain\n"
                                  "    on tick:\n"
                                  "        emit Ping to a\n");
    INFO(describe(analysis));
    CHECK_FALSE(analysis.unaccelerated("Near"));
}

TEST_CASE("Diagnostics: a product of two unbounded bindings is still warned", "[diagnostics][single-traits]") {
    const auto analysis = analyze(kSpawnBoth + pair_rule("Crowd", "Unit", "Unit"));
    INFO(describe(analysis));
    CHECK(analysis.unaccelerated("Crowd"));
}

TEST_CASE("Diagnostics: a required single trait bounds a domain to one", "[diagnostics][single-traits]") {
    const auto analysis = analyze(kSpawnBoth +
                                  "rule OnTerrain:\n"
                                  "    each: Terrain\n"
                                  "    on tick:\n"
                                  "        emit Ping\n"
                                  "rule OffTerrain:\n"
                                  "    each: Unit\n"
                                  "    without: Terrain\n"
                                  "    on tick:\n"
                                  "        emit Ping\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto selection = [&](const std::string& rule) {
        const auto found = std::ranges::find_if(analysis.program.handler_contracts,
                                                [&](const auto& contract) { return contract.rule.local_name == rule; });
        REQUIRE(found != analysis.program.handler_contracts.end());
        return found->selection;
    };
    CHECK(named_bound(analysis.program.reachability, selection("OnTerrain")) == std::optional<std::size_t>{1});
    CHECK_FALSE(named_bound(analysis.program.reachability, selection("OffTerrain")).has_value());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
