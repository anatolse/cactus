// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parallel_batches.hpp"
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

const std::string kPrelude =
    "module game\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "const:\n"
    "    PASSES = 3\n"
    "    RATE = 2.0\n"
    "trait Settle:\n"
    "    var passes: int = 2\n"
    "    var done: bool = false\n"
    "    var x: float = 0.0\n"
    "trait Rock:\n"
    "    var y: float = 0.0\n"
    "trait Lit\n"
    "relation Near:\n"
    "    from: Settle\n"
    "    to: Rock\n"
    "event Bumped\n"
    "template Stone:\n"
    "    Rock\n"
    "extern func poke()\n"
    "entity Pile:\n"
    "    Settle\n"
    "entity Boulder:\n"
    "    Rock\n";

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

int prelude_lines() {
    return static_cast<int>(std::ranges::count(kPrelude, '\n'));
}

Analysis analyze(const std::string& body) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    if (!errors.has_errors()) {
        SemanticAnalyzer analyzer(errors);
        analysis.program = analyzer.analyze(*analysis.ast);
        if (!errors.has_errors()) {
            run_whole_program_passes(analysis.program, errors);
        }
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

// Checks that `body` fails with an error containing `text` on body line `line` (1-based).
void check_error_at(const std::string& body, const std::string& text, int line) {
    const auto analysis = analyze(body);
    INFO(describe(analysis));
    const auto* found = analysis.error(text);
    REQUIRE(found != nullptr);
    CHECK(found->location.line == prelude_lines() + line);
}

const RuleNode& rule_named(const ProgramNode& program, const std::string& name) {
    for (const auto& decl : program.declarations) {
        if (const auto* rule = std::get_if<RuleNode>(&decl); rule != nullptr && rule->name == name) {
            return *rule;
        }
    }
    FAIL("no rule named " + name);
    std::unreachable();
}

const std::string kUnaryHeader =
    "rule Settle3:\n"
    "    each: Settle as s\n";

const std::string kTickBody =
    "    on tick:\n"
    "        s.x += 1.0\n";

const std::string kPairHeader =
    "rule Sweep:\n"
    "    pairs:\n"
    "        s:\n"
    "            Settle\n"
    "        r:\n"
    "            Rock\n"
    "    reduce per s:\n"
    "        total = sum(r[Rock].y)\n";

}  // namespace

// ── Clause shape ────────────────────────────────────────────────────────────

TEST_CASE("Repeat: a unary rule repeats its pass", "[semantic][rule-repeat]") {
    const auto analysis = analyze(kUnaryHeader + "    repeat: 4\n" + kTickBody);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = rule_named(*analysis.ast, "Settle3");
    REQUIRE(rule.repeat.has_value());
    CHECK(rule.repeat->passes == 4);
}

TEST_CASE("Repeat: a pair rule with reduce per repeats its pass", "[semantic][rule-repeat]") {
    const auto analysis = analyze(kPairHeader +
                                  "    repeat: PASSES\n"
                                  "    on tick:\n"
                                  "        s[Settle].x = total\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& rule = rule_named(*analysis.ast, "Sweep");
    REQUIRE(rule.repeat.has_value());
    CHECK(rule.repeat->passes == 3);
}

TEST_CASE("Repeat: a repeating rule stays one schedule node, outside parallel levels", "[semantic][rule-repeat]") {
    const auto analysis = analyze(kUnaryHeader + "    repeat: 2\n" + kTickBody +
                                  "rule Grind:\n"
                                  "    each: Rock as r\n"
                                  "    on tick:\n"
                                  "        r.y += 1.0\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& handlers = analysis.program.execution_graph.handlers;
    const auto is_settle = [](const HandlerNode& node) { return node.identity.rule.local_name == "Settle3"; };
    CHECK(std::ranges::count_if(handlers, is_settle) == 1);
    const auto handler = std::ranges::find_if(handlers, is_settle);
    REQUIRE(handler != handlers.end());
    const auto plan = plan_parallel_handlers(analysis.program);
    CHECK(plan.level_of(handler->identity) == nullptr);
    CHECK_FALSE(plan.eligible.contains(handler->identity));
}

TEST_CASE("Repeat: a count that reads a field is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader + "    repeat: s.passes\n" + kTickBody, "the count must be a constant", 3);
}

TEST_CASE("Repeat: a count below 2 is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader + "    repeat: 1\n" + kTickBody, "at least 2", 3);
    check_error_at(kUnaryHeader + "    repeat: PASSES - 3\n" + kTickBody, "at least 2", 3);
}

TEST_CASE("Repeat: a count that isn't an int is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader + "    repeat: RATE\n" + kTickBody, "must be of type 'int'", 3);
}

TEST_CASE("Repeat: a keep rule can't repeat", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader +
                       "    keep Lit\n"
                       "    repeat: 3\n",
                   "repeat",
                   4);
}

TEST_CASE("Repeat: an extern rule can't repeat", "[parser][rule-repeat]") {
    check_error_at(
        "extern rule Paint:\n"
        "    each: Settle\n"
        "    repeat: 3\n"
        "    on tick:\n"
        "        reads:\n"
        "            Settle\n",
        "'repeat:' is not valid on external rules",
        3);
}

// ── One meaning per pass ────────────────────────────────────────────────────

TEST_CASE("Repeat: emit in a repeating handler is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader + "    repeat: 3\n" + kTickBody + "        emit Bumped\n",
                   "a repeating rule may only write fields",
                   6);
}

TEST_CASE("Repeat: structural commands in a repeating handler are rejected", "[semantic][rule-repeat]") {
    const std::vector<std::pair<std::string, std::string>> commands{
        {"spawn", "            spawn Stone()\n"},
        {"destroy", "            destroy s\n"},
        {"add", "            add Lit to s\n"},
        {"remove", "            remove Lit from s\n"},
        {"set", "            set Settle on s:\n                done = false\n"},
        {"project", "            project Lit to s\n"},
        {"add", "            add Near(s, Boulder)\n"},
        {"remove", "            remove Near(s, Boulder)\n"},
    };
    for (const auto& [keyword, command] : commands) {
        INFO(command);
        std::string body = kUnaryHeader;
        body +=
            "    repeat: 3\n"
            "    on tick:\n"
            "        if s.done:\n";
        body += command;
        check_error_at(body, "`" + keyword + "`", 6);
    }
}

TEST_CASE("Repeat: an effect action in a repeating handler is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader +
                       "    repeat: 3\n"
                       "    on tick:\n"
                       "        poke()\n",
                   "acts on",
                   5);
}

TEST_CASE("Repeat: a second handler is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader + "    repeat: 3\n" + kTickBody +
                       "    on Bumped:\n"
                       "        s.x += 1.0\n"
                       "rule Bump:\n"
                       "    on tick:\n"
                       "        emit Bumped\n",
                   "exactly one handler",
                   6);
}

TEST_CASE("Repeat: an event handler is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader +
                       "    repeat: 3\n"
                       "    on Bumped:\n"
                       "        s.x += 1.0\n"
                       "rule Bump:\n"
                       "    on tick:\n"
                       "        emit Bumped\n",
                   "triggered by a phase",
                   4);
}

TEST_CASE("Repeat: limit on a repeating rule is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader +
                       "    limit: 1\n"
                       "    repeat: 3\n" +
                       kTickBody,
                   "`limit:`",
                   3);
}

TEST_CASE("Repeat: order by on a repeating rule is rejected", "[semantic][rule-repeat]") {
    check_error_at(kUnaryHeader +
                       "    order by:\n"
                       "        s.x\n"
                       "    repeat: 3\n" +
                       kTickBody,
                   "`order by:`",
                   4);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
