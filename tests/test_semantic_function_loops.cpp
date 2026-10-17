// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
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
    "pub event tick:\n"
    "    dt: float\n"
    "event Ping:\n"
    "    n: int\n"
    "trait Hp:\n"
    "    var v: int = 0\n"
    "template Blob:\n"
    "    Hp\n"
    "extern func poke() int\n";

struct Analysis {
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] std::string describe() const {
        std::string text;
        for (const auto& d : diagnostics) {
            text += std::to_string(d.location.line) + ":" + std::to_string(d.location.column) + ": " + d.message + "\n";
        }
        return text;
    }
    [[nodiscard]] const Diagnostic* find(const std::string& text) const {
        const auto found = std::ranges::find_if(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error && d.message.contains(text);
        });
        return found == diagnostics.end() ? nullptr : &*found;
    }
    [[nodiscard]] bool has(const std::string& text) const {
        return find(text) != nullptr;
    }
    [[nodiscard]] bool clean() const {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
};

Analysis analyze(const std::string& body) {
    ErrorReporter errors;
    Lexer lexer("module game\n" + kPrelude + body, "game.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    auto ast = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    (void)analyzer.analyze(ast);
    return Analysis{.diagnostics = errors.diagnostics()};
}

constexpr int kPreludeLines = 10;  // `module game` plus kPrelude

}  // namespace

// ── Accepted loops ──────────────────────────────────────────────────────────

TEST_CASE("Function loops: a list loop reduces into a local accumulator", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func total(values: list[float]) float:\n"
        "    var sum = 0.0\n"
        "    for v in values:\n"
        "        sum += v\n"
        "    return sum\n");
    INFO(analysis.describe());
    CHECK(analysis.clean());
}

TEST_CASE("Function loops: a range loop with pure bounds is accepted", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func half(n: int) int:\n"
        "    return n / 2\n"
        "func nearest_two(n: int) int:\n"
        "    var best = 1000\n"
        "    var second = 1000\n"
        "    for i in range(0, half(n) + 1, 1):\n"
        "        let d = i * i\n"
        "        if d < best:\n"
        "            second = best\n"
        "            best = d\n"
        "        else if d < second:\n"
        "            second = d\n"
        "    return best + second\n");
    INFO(analysis.describe());
    CHECK(analysis.clean());
}

TEST_CASE("Function loops: nested loops and a return inside a loop are accepted", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func find(rows: int, cols: int, target: int) int:\n"
        "    for r in range(0, rows):\n"
        "        for c in range(0, cols):\n"
        "            if r * cols + c == target:\n"
        "                return r\n"
        "    return -1\n");
    INFO(analysis.describe());
    CHECK(analysis.clean());
}

TEST_CASE("Function loops: a void function may loop without a return", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func spin(n: int):\n"
        "    var x = 0\n"
        "    for i in range(0, n):\n"
        "        x += i\n");
    INFO(analysis.describe());
    CHECK(analysis.clean());
}

// ── Bindings and mutation ───────────────────────────────────────────────────

TEST_CASE("Function loops: the loop binding is read-only", "[semantic][function-loops]") {
    const auto list_loop = analyze(
        "func f(values: list[int]) int:\n"
        "    for v in values:\n"
        "        v = 2\n"
        "    return 0\n");
    INFO(list_loop.describe());
    CHECK(list_loop.has("foreach loop variable 'v' is read-only"));

    const auto range_loop = analyze(
        "func g() int:\n"
        "    for i in range(0, 3):\n"
        "        i += 1\n"
        "    return 0\n");
    INFO(range_loop.describe());
    CHECK(range_loop.has("foreach loop variable 'i' is read-only"));
}

TEST_CASE("Function loops: immutable locals and undeclared names stay protected", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func f(n: int) int:\n"
        "    let fixed = 1\n"
        "    for i in range(0, n):\n"
        "        fixed = i\n"
        "        missing = i\n"
        "    return fixed\n");
    INFO(analysis.describe());
    CHECK(analysis.has("cannot reassign immutable binding 'fixed'"));
    CHECK(analysis.has("assignment to undeclared local 'missing'"));
}

TEST_CASE("Function loops: a loop body cannot read or write trait state", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func f(e: entity_id) int:\n"
        "    for i in range(0, 2):\n"
        "        e[Hp].v = i\n"
        "    return 0\n");
    INFO(analysis.describe());
    CHECK(analysis.has("can't write trait 'Hp' through entity_id 'e'"));

    const auto read = analyze(
        "func g(e: entity_id) int:\n"
        "    var t = 0\n"
        "    for i in range(0, 2):\n"
        "        t += e[Hp].v\n"
        "    return t\n");
    INFO(read.describe());
    CHECK(read.has("can't read trait 'Hp' through entity_id 'e'"));
}

// ── Effects stay forbidden ──────────────────────────────────────────────────

TEST_CASE("Function loops: emit nested in a loop and conditional is rejected", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func f(n: int) int:\n"
        "    for i in range(0, n):\n"
        "        if i > 1:\n"
        "            emit Ping:\n"
        "                n = i\n"
        "    return 0\n");
    INFO(analysis.describe());
    const auto* diagnostic = analysis.find("func 'f' cannot use 'emit'");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->location.line == kPreludeLines + 4);
}

TEST_CASE("Function loops: structural commands in a loop are rejected", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func f(e: entity_id) int:\n"
        "    for i in range(0, 2):\n"
        "        for j in range(0, 2):\n"
        "            add Hp to e\n"
        "            spawn Blob:\n"
        "                Hp:\n"
        "                    v = 1\n"
        "            destroy e\n"
        "    return 0\n");
    INFO(analysis.describe());
    CHECK(analysis.has("`add` only allowed inside rule event handlers"));
    CHECK(analysis.has("`spawn` only allowed inside rule event handlers"));
    CHECK(analysis.has("`destroy` only allowed inside rule event handlers"));
}

TEST_CASE("Function loops: world reads in bounds or bodies are rejected", "[semantic][function-loops]") {
    const auto in_body = analyze(
        "func f(e: entity_id) bool:\n"
        "    for i in range(0, 2):\n"
        "        if exists(e):\n"
        "            return true\n"
        "    return false\n");
    INFO(in_body.describe());
    CHECK(in_body.has("`exists()` requires world access"));

    const auto in_iterable = analyze(
        "func g() int:\n"
        "    var n = 0\n"
        "    for e in query.all[Hp]():\n"
        "        n += 1\n"
        "    return n\n");
    INFO(in_iterable.describe());
    CHECK(in_iterable.has("query expressions require world access"));

    const auto self_read = analyze(
        "func h() int:\n"
        "    for i in range(0, 2):\n"
        "        let me = self\n"
        "    return 0\n");
    INFO(self_read.describe());
    CHECK(self_read.has("`self` only allowed inside rule event handlers"));
}

TEST_CASE("Function loops: a call not proven pure is rejected in bounds and bodies", "[semantic][function-loops]") {
    const auto in_body = analyze(
        "func f() int:\n"
        "    var n = 0\n"
        "    for i in range(0, 2):\n"
        "        n += poke()\n"
        "    return n\n");
    INFO(in_body.describe());
    const auto* diagnostic = in_body.find("func 'f' cannot call 'poke'");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->location.line == kPreludeLines + 4);

    const auto in_bounds = analyze(
        "func g() int:\n"
        "    var n = 0\n"
        "    for i in range(0, poke()):\n"
        "        n += i\n"
        "    return n\n");
    INFO(in_bounds.describe());
    CHECK(in_bounds.has("func 'g' cannot call 'poke'"));

    const auto outside_loop = analyze(
        "func h() int:\n"
        "    return poke()\n");
    INFO(outside_loop.describe());
    CHECK(outside_loop.has("func 'h' cannot call 'poke'"));

    const auto pure_call = analyze(
        "func k() int:\n"
        "    return 1\n"
        "func m() int:\n"
        "    return k()\n");
    INFO(pure_call.describe());
    CHECK(pure_call.clean());
}

TEST_CASE("Function loops: a string literal in a loop body is still rejected", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func f() int:\n"
        "    for i in range(0, 2):\n"
        "        let s = \"text\"\n"
        "    return 0\n");
    INFO(analysis.describe());
    CHECK(analysis.has("string literals are only allowed in const blocks"));
}

// ── Recursion ───────────────────────────────────────────────────────────────

TEST_CASE("Function loops: direct recursion in a loop body is rejected", "[semantic][function-loops]") {
    const auto analysis = analyze(
        "func f(n: int) int:\n"
        "    var t = 0\n"
        "    for i in range(0, n):\n"
        "        t += f(i)\n"
        "    return t\n");
    INFO(analysis.describe());
    CHECK(analysis.has("func 'f' is recursive"));
}

TEST_CASE("Function loops: indirect recursion through a loop is rejected", "[semantic][function-loops]") {
    const auto in_body = analyze(
        "func a(n: int) int:\n"
        "    var t = 0\n"
        "    for i in range(0, n):\n"
        "        t += b(i)\n"
        "    return t\n"
        "func b(n: int) int:\n"
        "    return a(n - 1)\n");
    INFO(in_body.describe());
    CHECK(in_body.has("func 'a' is recursive"));

    const auto in_bounds = analyze(
        "func c(n: int) int:\n"
        "    var t = 0\n"
        "    for i in range(0, d(n)):\n"
        "        t += i\n"
        "    return t\n"
        "func d(n: int) int:\n"
        "    return c(n)\n");
    INFO(in_bounds.describe());
    CHECK(in_bounds.has("func 'c' is recursive"));
}

// ── Return paths ────────────────────────────────────────────────────────────

TEST_CASE("Function loops: a return only inside a loop leaves a missing return path", "[semantic][function-loops]") {
    const auto range_loop = analyze(
        "func first_even(n: int) int:\n"
        "    for i in range(0, n):\n"
        "        if i % 2 == 0:\n"
        "            return i\n");
    INFO(range_loop.describe());
    const auto* diagnostic = range_loop.find("func 'first_even' does not return a value on every path");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->location.line == kPreludeLines + 1);

    const auto list_loop = analyze(
        "func head(values: list[int]) int:\n"
        "    for v in values:\n"
        "        return v\n");
    INFO(list_loop.describe());
    CHECK(list_loop.has("func 'head' does not return a value on every path"));
}

TEST_CASE("Function loops: return paths through conditionals and matches", "[semantic][function-loops]") {
    const auto both_branches = analyze(
        "func sign(n: int) int:\n"
        "    if n < 0:\n"
        "        return -1\n"
        "    else if n == 0:\n"
        "        return 0\n"
        "    else:\n"
        "        return 1\n");
    INFO(both_branches.describe());
    CHECK(both_branches.clean());

    const auto no_else = analyze(
        "func pos(n: int) int:\n"
        "    if n > 0:\n"
        "        return 1\n");
    INFO(no_else.describe());
    CHECK(no_else.has("func 'pos' does not return a value on every path"));

    const auto exhaustive_match = analyze(
        "func flag(b: bool) int:\n"
        "    match b:\n"
        "        true =>\n"
        "            return 1\n"
        "        false =>\n"
        "            return 0\n");
    INFO(exhaustive_match.describe());
    CHECK(exhaustive_match.clean());

    const auto empty_body = analyze(
        "func nothing() int:\n"
        "    let x = 1\n");
    INFO(empty_body.describe());
    CHECK(empty_body.has("func 'nothing' does not return a value on every path"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
