// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "linked_program.hpp"
#include "maintained_examples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace cactus;
using namespace cactus_test;
namespace fs = std::filesystem;

namespace {

const std::string kPrelude =
    "module game\n"
    "use std.core\n";

const std::string kRay =
    "trait Ray:\n"
    "    var origin: float = 0.0\n"
    "    var hit: bool = false\n"
    "    var point: vec3 = vec3(0.0, 0.0, 0.0)\n"
    "    var normal: vec3 = vec3(0.0, 0.0, 0.0)\n"
    "    var cell: entity_id\n"
    "trait Sink:\n"
    "    var total: float = 0.0\n"
    "    var target: entity_id\n"
    "entity R1:\n"
    "    Ray\n"
    "    Sink\n"
    "rule Resolve:\n"
    "    each: Ray as r\n"
    "    on input:\n"
    "        r.hit = r.origin > 0.0\n"
    "        r.point = vec3(r.origin, 0.0, 0.0)\n"
    "        r.normal = vec3(1.0, 0.0, 0.0)\n"
    "        r.cell = self\n";

bool is_encoding_warning(const Diagnostic& diagnostic) {
    return diagnostic.level == DiagnosticLevel::Warning &&
           (diagnostic.message.contains("read only where") || diagnostic.message.contains("only filters rules") ||
            diagnostic.message.contains("is only joined"));
}

struct Checked {
    LinkedProgram linked;
    std::vector<Diagnostic> warnings;

    [[nodiscard]] const Diagnostic& warning() const {
        REQUIRE(warnings.size() == 1);
        return warnings.front();
    }
};

Checked check(const std::string& body) {
    static int next_case = 0;
    const auto dir       = fs::path(CACTUS_TEST_BINARY_DIR) / "encoding" / ("case" + std::to_string(next_case++));
    Checked checked{.linked = link_sources(dir, {{"game", kPrelude + body}}, "game")};
    INFO(checked.linked.describe());
    REQUIRE_FALSE(checked.linked.has_errors());
    REQUIRE(checked.linked.program.has_value());
    std::ranges::copy_if(checked.linked.diagnostics, std::back_inserter(checked.warnings), is_encoding_warning);
    UNSCOPED_INFO(checked.linked.describe());
    return checked;
}

// A reader of the ray result scheduled after Resolve in the same input activation.
std::string ray_reader(const std::string& statements, const std::string& clauses = "", const std::string& name = "Use") {
    return "rule " + name + ":\n    each: Ray as r, Sink as s\n    after: Resolve\n" + clauses + "    on input:\n" +
           statements;
}

}  // namespace

// ── Fields guarded by a flag ────────────────────────────────────────────────

TEST_CASE("Encoding: ray result behind a flag suggests a projected trait", "[encoding]") {
    const auto checked  = check(kRay + ray_reader("        if r.hit:\n"
                                                  "            s.total += r.point.x + r.normal.y\n"
                                                  "            s.target = r.cell\n"));
    const auto& warning = checked.warning();
    CHECK(warning.message.contains("'Ray'"));
    CHECK(warning.message.contains("'hit'"));
    CHECK(warning.message.contains("'point'"));
    CHECK(warning.message.contains("'normal'"));
    CHECK(warning.message.contains("'cell'"));
    CHECK_FALSE(warning.message.contains("'origin'"));
    CHECK(warning.message.contains("projected trait"));
    CHECK_FALSE(warning.message.contains("state"));
    CHECK(warning.location.line == 5);
}

TEST_CASE("Encoding: a lasting flag read after the commit suggests a state", "[encoding]") {
    const auto checked  = check(
        "pub extern event Opened:\n"
        "    by: entity_id\n"
        "event Welcome\n"
        "trait Door:\n"
        "    var open: bool = false\n"
        "    var opened_by: entity_id\n"
        "    var visits: int = 0\n"
        "entity D1:\n"
        "    Door\n"
        "rule Open:\n"
        "    each: Door as d\n"
        "    on Opened as o:\n"
        "        d.open = true\n"
        "        d.opened_by = o.by\n"
        "rule Greet:\n"
        "    each: Door as d\n"
        "    on tick:\n"
        "        if d.open:\n"
        "            emit Welcome to d.opened_by\n"
        "rule Welcomed:\n"
        "    each: Door as d\n"
        "    on Welcome:\n"
        "        d.visits += 1\n");
    const auto& warning = checked.warning();
    CHECK(warning.message.contains("'Door'"));
    CHECK(warning.message.contains("'open'"));
    CHECK(warning.message.contains("'opened_by'"));
    CHECK(warning.message.contains("state"));
    CHECK_FALSE(warning.message.contains("projected"));
}

TEST_CASE("Encoding: a lasting flag written by a phase handler is silent", "[encoding]") {
    const auto checked = check(
        "trait Body:\n"
        "    var grounded: bool = false\n"
        "    var ground_normal: vec3 = vec3(0.0, 1.0, 0.0)\n"
        "    var velocity: vec3 = vec3(0.0, 0.0, 0.0)\n"
        "    var height: float = 0.0\n"
        "entity B1:\n"
        "    Body\n"
        "rule Land:\n"
        "    each: Body as b\n"
        "    on tick:\n"
        "        b.grounded = b.height <= 0.0\n"
        "        b.ground_normal = vec3(0.0, 1.0, 0.0)\n"
        "rule Jump:\n"
        "    each: Body as b\n"
        "    on input:\n"
        "        if b.grounded:\n"
        "            b.velocity = b.ground_normal\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: a flag with no fitting form is silent", "[encoding]") {
    const auto checked = check(
        "pub extern event Hit:\n"
        "    at: vec3\n"
        "trait Shield:\n"
        "    var broken: bool = false\n"
        "    var break_point: vec3 = vec3(0.0, 0.0, 0.0)\n"
        "    var marks: vec3 = vec3(0.0, 0.0, 0.0)\n"
        "entity S1:\n"
        "    Shield\n"
        "rule Break:\n"
        "    each: Shield as s\n"
        "    on Hit as h:\n"
        "        s.broken = true\n"
        "        s.break_point = h.at\n"
        "rule Mark:\n"
        "    each: Shield as s\n"
        "    after: Break\n"
        "    on Hit:\n"
        "        if s.broken:\n"
        "            s.marks = s.break_point\n"
        "rule Show:\n"
        "    each: Shield as s\n"
        "    on tick:\n"
        "        if s.broken:\n"
        "            s.marks = s.marks + s.break_point\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: an unguarded read leaves the field out", "[encoding]") {
    const auto checked  = check(kRay +
                               ray_reader("        if r.hit:\n"
                                          "            s.total += r.point.x + r.normal.y\n"
                                          "            s.target = r.cell\n") +
                               ray_reader("        s.total += r.point.z\n", "", "Peek"));
    const auto& warning = checked.warning();
    CHECK(warning.message.contains("'normal'"));
    CHECK_FALSE(warning.message.contains("'point'"));
}

TEST_CASE("Encoding: with every guarded field read unguarded somewhere, no warning", "[encoding]") {
    const auto checked = check(kRay + ray_reader("        if r.hit:\n"
                                                 "            s.total += r.point.x\n") +
                               ray_reader("        s.total += r.point.z + r.normal.x\n"
                                          "        s.target = r.cell\n",
                                          "",
                                          "Peek"));
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: a guard through a top-level conjunct", "[encoding]") {
    const auto checked = check(kRay + ray_reader("        if r.hit and r.origin > 1.0:\n"
                                                 "            s.target = r.cell\n"));
    CHECK(checked.warning().message.contains("'cell'"));
}

TEST_CASE("Encoding: a guard through a local is not a guard", "[encoding]") {
    const auto checked = check(kRay + ray_reader("        let ok = r.hit\n"
                                                 "        if ok:\n"
                                                 "            s.target = r.cell\n"));
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: an early return guards the rest of the body", "[encoding]") {
    const auto checked = check(kRay + ray_reader("        if not r.hit:\n"
                                                 "            return\n"
                                                 "        s.target = r.cell\n"));
    CHECK(checked.warning().message.contains("'cell'"));
}

TEST_CASE("Encoding: a where: conjunct guards the rule", "[encoding]") {
    const auto checked = check(kRay + ray_reader("        s.target = r.cell\n", "    where: r.hit\n"));
    CHECK(checked.warning().message.contains("'cell'"));
}

TEST_CASE("Encoding: a read in the else branch is not guarded", "[encoding]") {
    const auto checked = check(kRay + ray_reader("        if r.hit:\n"
                                                 "            s.total += r.normal.x\n"
                                                 "        else:\n"
                                                 "            s.target = r.cell\n"));
    CHECK_FALSE(checked.warning().message.contains("'cell'"));
}

TEST_CASE("Encoding: a func trait parameter read counts where it is called", "[encoding]") {
    const std::string func =
        "func weight(r: Ray) float:\n"
        "    return r.point.x\n";
    const auto guarded = check(kRay + func +
                               ray_reader("        if r.hit:\n"
                                          "            s.total += weight(r)\n"));
    CHECK(guarded.warning().message.contains("'point'"));
    const auto unguarded = check(kRay + func +
                                 ray_reader("        s.total += weight(r)\n"
                                            "        if r.hit:\n"
                                            "            s.target = r.cell\n"));
    CHECK_FALSE(unguarded.warning().message.contains("'point'"));
}

TEST_CASE("Encoding: a writer that sets the flag on one path only fits no projection", "[encoding]") {
    const auto checked = check(
        "trait Ray:\n"
        "    var origin: float = 0.0\n"
        "    var hit: bool = false\n"
        "    var cell: entity_id\n"
        "trait Sink:\n"
        "    var target: entity_id\n"
        "entity R1:\n"
        "    Ray\n"
        "    Sink\n"
        "rule Resolve:\n"
        "    each: Ray as r\n"
        "    on input:\n"
        "        if r.origin > 0.0:\n"
        "            r.hit = true\n"
        "            r.cell = self\n"
        "rule Use:\n"
        "    each: Ray as r, Sink as s\n"
        "    after: Resolve\n"
        "    on input:\n"
        "        if r.hit:\n"
        "            s.target = r.cell\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: a reader in a later phase of the frame fits a projection", "[encoding]") {
    const auto checked = check(kRay +
                               "rule Draw:\n"
                               "    each: Ray as r, Sink as s\n"
                               "    on render:\n"
                               "        if r.hit:\n"
                               "            s.target = r.cell\n");
    CHECK(checked.warning().message.contains("projected trait"));
}

TEST_CASE("Encoding: a flag written in a periodic phase fits no projection", "[encoding]") {
    const auto checked = check(
        "trait Ray:\n"
        "    var origin: float = 0.0\n"
        "    var hit: bool = false\n"
        "    var cell: entity_id\n"
        "trait Sink:\n"
        "    var target: entity_id\n"
        "entity R1:\n"
        "    Ray\n"
        "    Sink\n"
        "rule Resolve:\n"
        "    each: Ray as r\n"
        "    on fixed_tick:\n"
        "        r.hit = r.origin > 0.0\n"
        "        r.cell = self\n"
        "rule Use:\n"
        "    each: Ray as r, Sink as s\n"
        "    after: Resolve\n"
        "    on fixed_tick:\n"
        "        if r.hit:\n"
        "            s.target = r.cell\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: an extern rule's read counts as unguarded", "[encoding]") {
    const auto checked = check(kRay +
                               ray_reader("        if r.hit:\n"
                                          "            s.target = r.cell\n") +
                               "extern rule Inspect:\n"
                               "    each:\n"
                               "        Ray\n"
                               "    on tick:\n"
                               "        reads:\n"
                               "            Ray\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: cell fields are not checked", "[encoding]") {
    const auto checked = check(
        "trait Grid:\n"
        "    dim x: int in range(0, 4)\n"
        "    var lit: bool = false\n"
        "    var level: float = 0.0\n"
        "entity G1:\n"
        "    Grid\n"
        "rule Light:\n"
        "    each: Grid as c\n"
        "    on input:\n"
        "        c.lit = c.x > 1\n"
        "        c.level = 1.0\n"
        "rule Glow:\n"
        "    each: Grid as c\n"
        "    after: Light\n"
        "    on input:\n"
        "        if c.lit:\n"
        "            c.level = c.level * 2.0\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: one warning per field", "[encoding]") {
    const auto checked = check(kRay + ray_reader("        if r.hit:\n"
                                                 "            s.target = r.cell\n") +
                               ray_reader("        if r.hit:\n"
                                          "            s.total += r.point.x\n",
                                          "",
                                          "Again"));
    CHECK(checked.warnings.size() == 1);
}

// ── Flags used only as filters ──────────────────────────────────────────────

namespace {

const std::string kProbe =
    "trait Probe:\n"
    "    let enabled: bool = true\n"
    "    var count: int = 0\n"
    "entity P1:\n"
    "    Probe:\n"
    "        enabled = false\n"
    "rule Count:\n"
    "    each: Probe as p\n"
    "    where: p.enabled\n"
    "    on tick:\n"
    "        p.count += 1\n";

}  // namespace

TEST_CASE("Encoding: a flag used only as a filter suggests a marker trait", "[encoding]") {
    const auto checked  = check(kProbe);
    const auto& warning = checked.warning();
    CHECK(warning.message.contains("'Probe.enabled'"));
    CHECK(warning.message.contains("marker trait"));
    CHECK(warning.location.line == 4);
}

TEST_CASE("Encoding: a negated filter is a filter", "[encoding]") {
    const auto checked = check(
        "trait Probe:\n"
        "    let muted: bool = false\n"
        "    var count: int = 0\n"
        "entity P1:\n"
        "    Probe\n"
        "rule Count:\n"
        "    each: Probe as p\n"
        "    where: not p.muted\n"
        "    on tick:\n"
        "        p.count += 1\n");
    CHECK(checked.warning().message.contains("'Probe.muted'"));
}

TEST_CASE("Encoding: a var filter flag is silent", "[encoding]") {
    const auto checked = check(
        "trait Probe:\n"
        "    var enabled: bool = true\n"
        "    var count: int = 0\n"
        "entity P1:\n"
        "    Probe\n"
        "rule Count:\n"
        "    each: Probe as p\n"
        "    where: p.enabled\n"
        "    on tick:\n"
        "        p.count += 1\n"
        "rule Toggle:\n"
        "    each: Probe as p\n"
        "    on input:\n"
        "        p.enabled = p.count < 3\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: a var flag no handler writes is silent", "[encoding]") {
    const auto checked = check(
        "trait Clip:\n"
        "    var playing: bool = false\n"
        "    var elapsed: float = 0.0\n"
        "    var shown: float = 0.0\n"
        "entity C1:\n"
        "    Clip\n"
        "rule Show:\n"
        "    each: Clip as c\n"
        "    on tick:\n"
        "        if c.playing:\n"
        "            c.shown = c.elapsed\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: a filter flag also read in a body is silent", "[encoding]") {
    const auto checked = check(kProbe +
                               "rule Read:\n"
                               "    each: Probe as p\n"
                               "    on input:\n"
                               "        if p.enabled:\n"
                               "            p.count += 1\n");
    CHECK(checked.warnings.empty());
}

// ── Handles used only as joins ──────────────────────────────────────────────

namespace {

const std::string kOrders =
    "event Hurt\n"
    "trait Unit:\n"
    "    var hp: int = 10\n"
    "trait Order:\n"
    "    var target: entity_id\n"
    "entity Hero:\n"
    "    Unit\n"
    "entity Boss:\n"
    "    Order:\n"
    "        target = Hero\n"
    "rule Strike:\n"
    "    pairs:\n"
    "        order: Order\n"
    "        target: Unit\n"
    "    where: target == order[Order].target\n"
    "    on tick:\n"
    "        emit Hurt to target\n"
    "rule Hurting:\n"
    "    each: Unit as u\n"
    "    on Hurt:\n"
    "        u.hp -= 1\n";

}  // namespace

TEST_CASE("Encoding: a handle used only in joins suggests a link", "[encoding]") {
    const auto checked  = check(kOrders);
    const auto& warning = checked.warning();
    CHECK(warning.message.contains("'Order.target'"));
    CHECK(warning.message.contains("var target: Unit"));
}

TEST_CASE("Encoding: a handle assigned from an event is silent", "[encoding]") {
    const auto checked = check(kOrders +
                               "pub extern event Assign:\n"
                               "    who: entity_id\n"
                               "rule Retarget:\n"
                               "    each: Order as o\n"
                               "    on Assign as a:\n"
                               "        o.target = a.who\n");
    CHECK(checked.warnings.empty());
}

TEST_CASE("Encoding: a handle also used as a value is silent", "[encoding]") {
    const auto checked = check(kOrders +
                               "rule Ping:\n"
                               "    each: Order as o\n"
                               "    on tick:\n"
                               "        emit Hurt to o.target\n");
    CHECK(checked.warnings.empty());
}

// ── The standard library and the examples are silent ────────────────────────

TEST_CASE("Encoding: the standard library is silent", "[encoding]") {
    const auto dir  = fs::path(CACTUS_TEST_BINARY_DIR) / "encoding" / "all_stdlib";
    const auto root = write_all_stdlib_program(dir);
    ProgramPipeline pipeline(dir / "build");
    const auto linked = pipeline.link(root);
    INFO(linked.describe());
    REQUIRE_FALSE(linked.has_errors());
    const auto warnings = located_messages(linked.diagnostics, is_encoding_warning);
    for (const auto& warning : warnings) {
        UNSCOPED_INFO(warning);
    }
    CHECK(warnings.empty());
}

TEST_CASE("Encoding: the examples are silent", "[encoding]") {
    const auto examples = fs::path(CACTUS_TEST_SOURCE_DIR) / "examples";
    std::vector<std::string> warnings;
    for (const auto example : kMaintainedExamples) {
        const auto root = examples / example;
        ProgramPipeline pipeline(fs::path(CACTUS_TEST_BINARY_DIR) / "encoding" / "examples" /
                                 root.parent_path().filename() / root.stem());
        const auto linked = pipeline.link(root, {root.parent_path()});
        INFO(linked.describe());
        REQUIRE_FALSE(linked.has_errors());
        std::ranges::copy(located_messages(linked.diagnostics, is_encoding_warning), std::back_inserter(warnings));
    }
    for (const auto& warning : warnings) {
        UNSCOPED_INFO(warning);
    }
    CHECK(warnings.empty());
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
