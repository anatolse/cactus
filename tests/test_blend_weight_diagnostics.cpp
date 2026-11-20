// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "frontend/blend_weights.hpp"

#include "linked_program.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

using namespace cactus;
using namespace cactus_test;
using Catch::Matchers::WithinRel;
namespace fs = std::filesystem;

namespace {

const std::string kPrelude =
    "module game\n"
    "use std.core\n"
    "use std.math as math\n"
    "use std.math.vec3 as v3\n"
    "const:\n"
    "    ACCEL = 4.0\n"
    "    RATE = 2.0\n"
    "    TURN_RATE = 10.0\n"
    "    AMBIENT = 1.0\n"
    "trait Unit:\n"
    "    var speed: float = 0.0\n"
    "    var wanted: float = 1.0\n"
    "    var rate: float = 2.0\n"
    "    var smoothing: float = 8.0\n"
    "    var near: bool = false\n"
    "    var count: int = 0\n"
    "    var position: vec3 = vec3(0.0, 0.0, 0.0)\n"
    "    var goal: vec3 = vec3(1.0, 0.0, 0.0)\n"
    "    var scale: vec3 = vec3(1.0, 1.0, 1.0)\n"
    "    var velocity: vec3 = vec3(0.0, 0.0, 0.0)\n"
    "entity U:\n"
    "    Unit\n";

const float kFixedDt = static_cast<float>(1.0 / 60.0);

struct Checked {
    LinkedProgram linked;
    std::vector<BlendWeight> blends;
    std::vector<Diagnostic> warnings;

    [[nodiscard]] const BlendWeight& only() const {
        REQUIRE(blends.size() == 1);
        return blends.front();
    }
    [[nodiscard]] const Diagnostic& warning() const {
        REQUIRE(warnings.size() == 1);
        return warnings.front();
    }
};

bool is_blend_warning(const Diagnostic& diagnostic) {
    return diagnostic.level == DiagnosticLevel::Warning && diagnostic.message.contains("toward its goal");
}

Checked check_modules(const std::map<std::string, std::string>& modules) {
    static int next_case = 0;
    const auto dir       = fs::path(CACTUS_TEST_BINARY_DIR) / "blend_weights" / ("case" + std::to_string(next_case++));
    Checked checked{.linked = link_sources(dir, modules, "game")};
    INFO(checked.linked.describe());
    REQUIRE_FALSE(checked.linked.has_errors());
    REQUIRE(checked.linked.program.has_value());
    checked.blends = find_blend_weights(*checked.linked.program);
    std::ranges::copy_if(checked.linked.diagnostics, std::back_inserter(checked.warnings), is_blend_warning);
    return checked;
}

Checked check(const std::string& body) {
    return check_modules({{"game", kPrelude + body}});
}

std::string rule(const std::string& handler, const std::string& statements, const std::string& name = "R") {
    return "rule " + name + ":\n    each: Unit as u\n    on " + handler + ":\n" + statements;
}

std::string tick_blend(const std::string& weight) {
    return rule("tick", "        u.speed += (u.wanted - u.speed) * " + weight + "\n");
}

std::string fixed_blend(const std::string& weight) {
    return rule("fixed_tick", "        u.speed += (u.wanted - u.speed) * " + weight + "\n");
}

void require_point(const BlendWeight& blend, double value) {
    REQUIRE(blend.lower.value.has_value());
    REQUIRE(blend.upper.value.has_value());
    CHECK_THAT(*blend.lower.value, WithinRel(value, 1e-6));
    CHECK_THAT(*blend.upper.value, WithinRel(value, 1e-6));
}

void require_unit(const BlendWeight& blend) {
    REQUIRE(blend.lower.value.has_value());
    REQUIRE(blend.upper.value.has_value());
    CHECK(*blend.lower.value >= 0.0);
    CHECK(*blend.upper.value <= 1.0);
}

}  // namespace

// ── A blend is a write that moves a field toward a goal by a weight ─────────

TEST_CASE("Blend weights: the accumulating form is a blend", "[blend-weights]") {
    const auto checked = check(tick_blend("0.5"));
    const auto& blend  = checked.only();
    CHECK(blend.rule == "R");
    CHECK(blend.field == "u.speed");
    require_point(blend, 0.5);
}

TEST_CASE("Blend weights: the goal may be a sum", "[blend-weights]") {
    const auto checked = check(rule("tick", "        u.position += (u.goal + u.velocity - u.position) * 0.25\n"));
    CHECK(checked.only().field == "u.position");
    require_point(checked.only(), 0.25);
}

TEST_CASE("Blend weights: a weight written first and divided", "[blend-weights]") {
    const auto checked = check(rule("tick", "        u.speed += RATE * (u.wanted - u.speed) / 4.0\n"));
    require_point(checked.only(), 0.5);
}

TEST_CASE("Blend weights: the subtracting and the assigning forms", "[blend-weights]") {
    const auto checked = check(rule("tick",
                                    "        u.speed -= (u.speed - u.wanted) * 0.25\n"
                                    "        u.rate = u.rate + (u.wanted - u.rate) * 0.5\n"
                                    "        u.smoothing = u.smoothing - (u.smoothing - u.wanted) * 0.75\n"));
    REQUIRE(checked.blends.size() == 3);
    require_point(checked.blends[0], 0.25);
    require_point(checked.blends[1], 0.5);
    require_point(checked.blends[2], 0.75);
}

TEST_CASE("Blend weights: a difference with no other factor has the weight one", "[blend-weights]") {
    const auto checked = check(rule("tick", "        u.speed += u.wanted - u.speed\n"));
    require_point(checked.only(), 1.0);
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: the lerp form", "[blend-weights]") {
    const auto checked = check(rule("tick",
                                    "        u.position = v3.lerp(u.position, u.goal, 0.25)\n"
                                    "        u.speed = math.lerp(u.speed, u.wanted, 0.5)\n"));
    REQUIRE(checked.blends.size() == 2);
    CHECK(checked.blends[0].field == "u.position");
    require_point(checked.blends[0], 0.25);
    require_point(checked.blends[1], 0.5);
}

TEST_CASE("Blend weights: a lerp that does not start at the field is not a blend", "[blend-weights]") {
    const auto checked = check(rule("tick", "        u.speed = math.lerp(u.wanted, u.speed, 0.5)\n"));
    CHECK(checked.blends.empty());
}

TEST_CASE("Blend weights: a blend inside an if expression", "[blend-weights]") {
    const auto checked =
        check(rule("tick", "        u.speed = if u.near: u.wanted else: u.speed + (u.wanted - u.speed) * 0.25\n"));
    require_point(checked.only(), 0.25);
}

TEST_CASE("Blend weights: a cell rule blends its own cell", "[blend-weights]") {
    const auto checked = check(
        "single trait Heat:\n"
        "    dim x: int in range(0, 4)\n"
        "    var value: float = 0.0\n"
        "entity Stove:\n"
        "    Heat\n"
        "rule Cool:\n"
        "    each: Heat as c\n"
        "    where:\n"
        "        c.value > 0.0\n"
        "    on tick:\n"
        "        c.value += (AMBIENT - c.value) * 0.5\n");
    CHECK(checked.only().rule == "Cool");
    CHECK(checked.only().field == "c.value");
    require_point(checked.only(), 0.5);
}

TEST_CASE("Blend weights: a cell blend must name the same cell", "[blend-weights]") {
    const auto checked = check(
        "single trait Heat:\n"
        "    dim x: int in range(0, 4)\n"
        "    var value: float = 0.0\n"
        "entity Stove:\n"
        "    Heat\n"
        "rule Cool:\n"
        "    each: Unit\n"
        "    on tick:\n"
        "        Heat[1].value += (AMBIENT - Heat[1].value) * 0.5\n"
        "        Heat[2].value += (AMBIENT - Heat[3].value) * 0.5\n");
    CHECK(checked.only().field == "Heat[1].value");
}

TEST_CASE("Blend weights: spreading through a sum is not a blend", "[blend-weights]") {
    const auto checked = check(
        "single trait Room:\n"
        "    dim x: int in range(0, 4)\n"
        "    var gas: float = 0.0\n"
        "entity Hall:\n"
        "    Room\n"
        "rule Spread:\n"
        "    pairs:\n"
        "        here:\n"
        "            Room\n"
        "        there:\n"
        "            Room\n"
        "    where:\n"
        "        adjacent(here, there)\n"
        "    reduce per here:\n"
        "        net = sum((there.gas - here.gas) * 3.0)\n"
        "    on tick:\n"
        "        here.gas += net\n");
    CHECK(checked.blends.empty());
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: integration is not a blend", "[blend-weights]") {
    const auto checked = check(rule("tick", "        u.position += u.velocity * tick.dt\n"));
    CHECK(checked.blends.empty());
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: a vector weight is not a blend", "[blend-weights]") {
    const auto checked = check(rule("tick", "        u.position += (u.goal - u.position) * u.scale\n"));
    CHECK(checked.blends.empty());
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: an int field is not a blend", "[blend-weights]") {
    const auto checked = check(rule("tick", "        u.count += (10 - u.count) * 3\n"));
    CHECK(checked.blends.empty());
}

TEST_CASE("Blend weights: a local variable is not a blend", "[blend-weights]") {
    const auto checked = check(rule("tick",
                                    "        var x = u.speed\n"
                                    "        x += (u.wanted - x) * 3.0\n"
                                    "        u.speed = x\n"));
    CHECK(checked.blends.empty());
}

// ── The weight range is derived from what the program states ───────────────

TEST_CASE("Blend weights: a periodic phase gives a constant weight", "[blend-weights]") {
    const auto checked = check(fixed_blend("8.0 * fixed_tick.dt"));
    require_point(checked.only(), static_cast<double>(8.0F * kFixedDt));
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: a periodic phase's dt read from another phase is constant", "[blend-weights]") {
    const auto checked = check(tick_blend("8.0 * fixed_tick.dt"));
    require_point(checked.only(), static_cast<double>(8.0F * kFixedDt));
}

TEST_CASE("Blend weights: a variable step has no upper bound", "[blend-weights]") {
    const auto checked = check(tick_blend("8.0 * tick.dt"));
    const auto& blend  = checked.only();
    REQUIRE(blend.lower.value.has_value());
    CHECK(*blend.lower.value == 0.0);
    CHECK_FALSE(blend.upper.value.has_value());
    CHECK(blend.upper.source.spelling == "tick.dt");
    CHECK(blend.upper.source.kind == UnboundedSource::Kind::PhaseStep);
}

TEST_CASE("Blend weights: an aliased phase step", "[blend-weights]") {
    const auto checked = check(rule("late_tick as step", "        u.speed += (u.wanted - u.speed) * step.dt\n"));
    CHECK(*checked.only().lower.value == 0.0);
    CHECK(checked.only().upper.source.spelling == "step.dt");
}

TEST_CASE("Blend weights: a duration step through seconds", "[blend-weights]") {
    const auto checked = check(tick_blend("8.0 * seconds(tick.delta)"));
    const auto& blend  = checked.only();
    REQUIRE(blend.lower.value.has_value());
    CHECK(*blend.lower.value == 0.0);
    CHECK_FALSE(blend.upper.value.has_value());
    CHECK(blend.upper.source.spelling == "tick.delta");
}

TEST_CASE("Blend weights: alpha of a periodic phase lies in the unit interval", "[blend-weights]") {
    const auto checked = check(rule("render", "        u.speed += (u.wanted - u.speed) * render.alpha\n"));
    require_unit(checked.only());
}

TEST_CASE("Blend weights: min gives the upper bound", "[blend-weights]") {
    const auto checked = check(tick_blend("math.min(1.0, tick.dt * TURN_RATE)"));
    require_unit(checked.only());
    CHECK(*checked.only().lower.value == 0.0);
    CHECK(*checked.only().upper.value == 1.0);
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: max, clamp and abs", "[blend-weights]") {
    const auto checked =
        check(rule("tick",
                   "        u.speed += (u.wanted - u.speed) * math.max(0.25, math.min(0.5, u.rate))\n"
                   "        u.rate += (u.wanted - u.rate) * math.clamp(u.smoothing, 0.0, 0.5)\n"
                   "        u.smoothing += (u.wanted - u.smoothing) * math.abs(math.clamp(u.speed, -0.5, 0.25))\n"));
    REQUIRE(checked.blends.size() == 3);
    CHECK(*checked.blends[0].lower.value == 0.25);
    CHECK(*checked.blends[0].upper.value == 0.5);
    CHECK(*checked.blends[1].lower.value == 0.0);
    CHECK(*checked.blends[1].upper.value == 0.5);
    CHECK(*checked.blends[2].lower.value == 0.0);
    CHECK(*checked.blends[2].upper.value == 0.5);
}

TEST_CASE("Blend weights: a var field is unknown", "[blend-weights]") {
    const auto checked = check(fixed_blend("u.smoothing * fixed_tick.dt"));
    const auto& blend  = checked.only();
    CHECK_FALSE(blend.lower.value.has_value());
    CHECK_FALSE(blend.upper.value.has_value());
    CHECK(blend.lower.source.spelling == "u.smoothing");
    CHECK(blend.lower.source.kind == UnboundedSource::Kind::VarField);
    CHECK(blend.lower.source.field == "smoothing");
}

TEST_CASE("Blend weights: a ranged int let field gives its range", "[blend-weights]") {
    const auto checked = check(
        "trait Lane:\n"
        "    let lane: int in range(0, 4)\n"
        "entity L:\n"
        "    Unit\n"
        "    Lane:\n"
        "        lane = 2\n"
        "rule R:\n"
        "    each: Unit as u, Lane as l\n"
        "    on tick:\n"
        "        u.speed += (u.wanted - u.speed) * 0.25 * l.lane\n");
    CHECK(*checked.only().lower.value == 0.0);
    CHECK(*checked.only().upper.value == 0.75);
}

TEST_CASE("Blend weights: a clamped field gives its bounds", "[blend-weights]") {
    const auto checked = check(
        "trait Grip:\n"
        "    var grip: float clamped(min = 0.0, max = 30.0) = 10.0\n"
        "entity G:\n"
        "    Unit\n"
        "    Grip\n"
        "rule R:\n"
        "    each: Unit as u, Grip as g\n"
        "    on fixed_tick:\n"
        "        u.speed += (u.wanted - u.speed) * g.grip * fixed_tick.dt\n");
    CHECK(*checked.only().lower.value == 0.0);
    CHECK_THAT(*checked.only().upper.value, WithinRel(static_cast<double>(30.0F * kFixedDt), 1e-6));
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: a clamped field bounded by a float let field", "[blend-weights]") {
    const auto checked = check(
        "trait Knob:\n"
        "    let top: float = 1.0\n"
        "    var k: float clamped(min = 0.0, max = top) = 0.0\n"
        "entity K:\n"
        "    Unit\n"
        "    Knob\n"
        "rule R:\n"
        "    each: Unit as u, Knob as n\n"
        "    on tick:\n"
        "        u.speed += (u.wanted - u.speed) * n.k\n");
    CHECK(*checked.only().lower.value == 0.0);
    CHECK_FALSE(checked.only().upper.value.has_value());
}

TEST_CASE("Blend weights: a clamped field bounded by a ranged let field", "[blend-weights]") {
    const auto checked = check(
        "trait Knob:\n"
        "    let top: int in range(0, 2)\n"
        "    var k: int clamped(min = 0, max = top) = 0\n"
        "entity K:\n"
        "    Unit\n"
        "    Knob:\n"
        "        top = 1\n"
        "rule R:\n"
        "    each: Unit as u, Knob as n\n"
        "    on tick:\n"
        "        u.speed += (u.wanted - u.speed) * 0.5 * n.k\n");
    CHECK(*checked.only().lower.value == 0.0);
    CHECK(*checked.only().upper.value == 0.5);
}

const std::string kTimer =
    "trait Timer:\n"
    "    var elapsed: duration clamped(min = 0s, max = 2s) = 0s\n"
    "    var big: duration clamped(min = 0s, max = 3000000h) = 0s\n"
    "entity T:\n"
    "    Unit\n"
    "    Timer\n";

std::string timer_blend(const std::string& weight) {
    return kTimer +
           "rule R:\n"
           "    each: Unit as u, Timer as t\n"
           "    on tick:\n"
           "        u.speed += (u.wanted - u.speed) * (" +
           weight + ")\n";
}

TEST_CASE("Blend weights: progress as a ratio of durations", "[blend-weights]") {
    const auto checked = check(timer_blend("t.elapsed / 2s"));
    CHECK(*checked.only().lower.value == 0.0);
    CHECK(*checked.only().upper.value == 1.0);
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: seconds of a clamped duration", "[blend-weights]") {
    const auto checked = check(timer_blend("seconds(t.elapsed) * 0.25"));
    CHECK(*checked.only().lower.value == 0.0);
    CHECK(*checked.only().upper.value == 0.5);
}

TEST_CASE("Blend weights: a ratio over a duration that can be zero", "[blend-weights]") {
    const auto checked = check(tick_blend("(1s / tick.delta)"));
    const auto& blend  = checked.only();
    CHECK_FALSE(blend.lower.value.has_value());
    CHECK_FALSE(blend.upper.value.has_value());
}

TEST_CASE("Blend weights: a division whose divisor range contains zero", "[blend-weights]") {
    const auto checked = check(timer_blend("0.001 / seconds(t.elapsed)"));
    CHECK_FALSE(checked.only().lower.value.has_value());
    CHECK_FALSE(checked.only().upper.value.has_value());
}

TEST_CASE("Blend weights: a duration sum that can leave the 64-bit range is unknown", "[blend-weights]") {
    const auto bounded = check(timer_blend("seconds(t.big) / 1000000000000.0"));
    CHECK(*bounded.only().lower.value == 0.0);
    CHECK(*bounded.only().upper.value < 0.02);
    const auto wrapping = check(timer_blend("seconds(t.big + t.big) / 1000000000000.0"));
    CHECK_FALSE(wrapping.only().upper.value.has_value());
    CHECK_FALSE(wrapping.only().lower.value.has_value());
}

TEST_CASE("Blend weights: a condition narrows a field in the else arm", "[blend-weights]") {
    const auto checked =
        check(rule("tick",
                   "        let blend = if u.smoothing <= 0.0: 1.0 else: math.min(1.0, u.smoothing * tick.dt)\n"
                   "        u.speed += (u.wanted - u.speed) * blend\n"));
    require_unit(checked.only());
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: a condition narrows a field in the then arm", "[blend-weights]") {
    const auto checked =
        check(rule("tick",
                   "        let blend = if u.smoothing > 0.0: math.min(1.0, u.smoothing * tick.dt) else: 1.0\n"
                   "        u.speed += (u.wanted - u.speed) * blend\n"));
    require_unit(checked.only());
}

TEST_CASE("Blend weights: an if statement narrows a let binding", "[blend-weights]") {
    const auto checked = check(rule("tick",
                                    "        let k = u.smoothing * tick.dt\n"
                                    "        if k >= 0.0:\n"
                                    "            u.speed += (u.wanted - u.speed) * math.min(1.0, k)\n"
                                    "        else:\n"
                                    "            u.rate += (u.wanted - u.rate) * math.min(1.0, -k)\n"));
    REQUIRE(checked.blends.size() == 2);
    require_unit(checked.blends[0]);
    require_unit(checked.blends[1]);
}

TEST_CASE("Blend weights: a field the handler writes is not narrowed", "[blend-weights]") {
    const auto checked =
        check(rule("tick",
                   "        u.smoothing = u.smoothing * 0.5\n"
                   "        let blend = if u.smoothing <= 0.0: 1.0 else: math.min(1.0, u.smoothing * tick.dt)\n"
                   "        u.speed += (u.wanted - u.speed) * blend\n"));
    CHECK_FALSE(checked.only().lower.value.has_value());
    CHECK(checked.only().lower.source.spelling == "u.smoothing");
}

const std::string kFollowBlend =
    "func follow_blend(smoothing: float, dt: float) float:\n"
    "    if smoothing <= 0.0:\n"
    "        return 1.0\n"
    "    return math.min(1.0, smoothing * dt)\n";

TEST_CASE("Blend weights: a func with an early return", "[blend-weights]") {
    const auto checked = check(kFollowBlend + tick_blend("follow_blend(u.smoothing, tick.dt)"));
    require_unit(checked.only());
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: a func from another module is evaluated at the call", "[blend-weights]") {
    const auto checked =
        check_modules({{"lib", "module lib\nuse std.math as math\npub " + kFollowBlend},
                       {"game", kPrelude + "use lib\n" + tick_blend("lib.follow_blend(u.smoothing, tick.dt)")}});
    require_unit(checked.only());
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: a func gives each parameter the range of its argument", "[blend-weights]") {
    const auto checked = check("func half(x: float) float:\n    return x * 0.5\n" + tick_blend("half(1.5)"));
    require_point(checked.only(), 0.75);
}

TEST_CASE("Blend weights: an extern func not listed is unknown", "[blend-weights]") {
    const auto checked = check(tick_blend("math.sin(u.speed)"));
    const auto& blend  = checked.only();
    CHECK_FALSE(blend.lower.value.has_value());
    CHECK_FALSE(blend.upper.value.has_value());
}

// ── Outcomes ─────────────────────────────────────────────────────────────────

TEST_CASE("Blend weights: a constant half is silent", "[blend-weights]") {
    const auto checked = check(
        "trait Order:\n"
        "    let target: Unit\n"
        "entity Follower:\n"
        "    Unit\n"
        "    Order:\n"
        "        target = U\n"
        "rule Drop:\n"
        "    each: Order\n"
        "    on lost Order.target:\n"
        "        remove Order\n"
        "rule Chase:\n"
        "    each: Unit as me, Order as order\n"
        "    on tick:\n"
        "        me.speed += (order.target[Unit].speed - me.speed) * 0.5\n");
    CHECK(checked.blends.size() == 1);
    CHECK(checked.warnings.empty());
}

TEST_CASE("Blend weights: a constant above one always passes the goal", "[blend-weights]") {
    const auto checked  = check(tick_blend("1.5"));
    const auto& warning = checked.warning();
    CHECK(warning.message.contains("rule 'R'"));
    CHECK(warning.message.contains("'u.speed' always passes its goal"));
    CHECK(warning.message.contains("1.5"));
    CHECK(warning.location.line == checked.only().location.line);
    CHECK(warning.location.filename.ends_with("game.cactus"));
}

TEST_CASE("Blend weights: a fixed step with a large rate always passes the goal", "[blend-weights]") {
    const auto checked = check(fixed_blend("90.0 * fixed_tick.dt"));
    CHECK(*checked.only().lower.value > 1.5);
    CHECK(checked.warning().message.contains("always passes its goal"));
    CHECK(checked.warning().message.contains("1.5"));
}

TEST_CASE("Blend weights: a negative constant always moves away from the goal", "[blend-weights]") {
    const auto checked = check(tick_blend("-0.25"));
    CHECK(checked.warning().message.contains("'u.speed' always moves away from its goal"));
    CHECK(checked.warning().message.contains("-0.25"));
}

TEST_CASE("Blend weights: a warning does not fail the link", "[blend-weights]") {
    const auto checked = check(tick_blend("1.5"));
    CHECK(checked.warnings.size() == 1);
    CHECK_FALSE(checked.linked.has_errors());
}

TEST_CASE("Blend weights: rate times a variable step", "[blend-weights]") {
    const auto checked  = check(tick_blend("ACCEL * tick.dt"));
    const auto& message = checked.warning().message;
    CHECK(message.contains("rule 'R'"));
    CHECK(message.contains("no upper bound"));
    CHECK_FALSE(message.contains("no lower bound"));
    CHECK(message.contains("'tick.dt'"));
    CHECK(message.contains("periodic phase"));
    CHECK(message.contains("math.min(1.0, ...)"));
    CHECK_FALSE(message.contains("clamped"));
}

TEST_CASE("Blend weights: a var field in a fixed step", "[blend-weights]") {
    const auto checked =
        check(rule("fixed_tick", "        u.position += (u.goal - u.position) * u.smoothing * fixed_tick.dt\n"));
    const auto& message = checked.warning().message;
    CHECK(message.contains("no lower and no upper bound"));
    CHECK(message.contains("'u.smoothing'"));
    CHECK(message.contains("math.clamp(..., 0.0, 1.0)"));
    CHECK(message.contains("'smoothing' clamped(min = ..., max = ...)"));
    CHECK_FALSE(message.contains("periodic phase"));
}

TEST_CASE("Blend weights: min without a lower bound", "[blend-weights]") {
    const auto checked  = check(tick_blend("math.min(1.0, u.rate * tick.dt)"));
    const auto& message = checked.warning().message;
    CHECK(message.contains("no lower bound"));
    CHECK_FALSE(message.contains("upper"));
    CHECK(message.contains("'u.rate'"));
    CHECK(message.contains("math.max(0.0, ...)"));
    CHECK(message.contains("'rate' clamped(min = ...)"));
}

TEST_CASE("Blend weights: a clamped field that is too wide", "[blend-weights]") {
    const auto checked = check(
        "trait Knob:\n"
        "    var k: float clamped(min = 0.0, max = 1.5) = 0.0\n"
        "entity K:\n"
        "    Unit\n"
        "    Knob\n"
        "rule R:\n"
        "    each: Unit as u, Knob as n\n"
        "    on tick:\n"
        "        u.speed += (u.wanted - u.speed) * n.k\n");
    CHECK(checked.warning().message.contains("can reach 1.5"));
    CHECK(checked.warning().message.contains("math.min(1.0, ...)"));
}

TEST_CASE("Blend weights: a known bound that is too wide", "[blend-weights]") {
    const auto checked  = check(tick_blend("math.clamp(u.rate, 0.0, 1.5)"));
    const auto& message = checked.warning().message;
    CHECK(message.contains("can reach 1.5"));
    CHECK_FALSE(message.contains("lower"));
}

TEST_CASE("Blend weights: one warning per statement", "[blend-weights]") {
    const auto checked = check(tick_blend("u.rate"));
    CHECK(checked.warnings.size() == 1);
}

// ── The standard library and the examples are silent ────────────────────────

namespace {

std::vector<std::string> blend_warnings(const LinkedProgram& linked) {
    std::vector<std::string> found;
    for (const auto& diagnostic : linked.diagnostics) {
        if (is_blend_warning(diagnostic)) {
            found.push_back(diagnostic.location.filename + ":" + std::to_string(diagnostic.location.line) + ": " +
                            diagnostic.message);
        }
    }
    return found;
}

}  // namespace

TEST_CASE("Blend weights: the standard library is silent", "[blend-weights]") {
    const auto dir = fs::path(CACTUS_TEST_BINARY_DIR) / "blend_weights" / "all_stdlib";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto root = dir / "all_stdlib.cactus";
    {
        std::ofstream out(root);
        out << "module all_stdlib\n";
        for (const auto& entry : fs::recursive_directory_iterator(stdlib_root())) {
            if (entry.path().extension() == ".cactus" && !entry.path().string().contains("build")) {
                auto name = fs::relative(entry.path(), stdlib_root()).replace_extension().generic_string();
                std::ranges::replace(name, '/', '.');
                out << "use " << name << "\n";
            }
        }
    }
    ProgramPipeline pipeline(dir / "build");
    const auto linked = pipeline.link(root);
    INFO(linked.describe());
    REQUIRE_FALSE(linked.has_errors());
    CHECK(find_blend_weights(*linked.program).size() >= 3);
    const auto warnings = blend_warnings(linked);
    for (const auto& warning : warnings) {
        UNSCOPED_INFO(warning);
    }
    CHECK(warnings.empty());
}

TEST_CASE("Blend weights: the examples are silent", "[blend-weights]") {
    const auto examples = fs::path(CACTUS_TEST_SOURCE_DIR) / "examples";
    std::size_t blends  = 0;
    std::vector<std::string> warnings;
    for (const auto* example : {"blue-square/square.cactus",
                                "bouncy-balls-3d/main.cactus",
                                "bouncy-bubbles/main.cactus",
                                "bracketed_trait_access.cactus",
                                "cactus-dungeon/small_map.cactus",
                                "clamped_fields.cactus",
                                "collider_sweep_queries.cactus",
                                "concise_rule_notation.cactus",
                                "conditional_expressions.cactus",
                                "const_tables.cactus",
                                "deferred_field_evaluation.cactus",
                                "deferred_set.cactus",
                                "derived_relations.cactus",
                                "deterministic_target_selection.cactus",
                                "disjoint_rule_domains.cactus",
                                "editor-3d/main.cactus",
                                "editor-test/editor-test.cactus",
                                "entity_handle_reuse.cactus",
                                "exact_accumulation.cactus",
                                "exact_duration.cactus",
                                "exclusive_states.cactus",
                                "first-person-arena/main.cactus",
                                "generalized_keep.cactus",
                                "gradient-square/gradient_square.cactus",
                                "grid_region_joins.cactus",
                                "keep_chains.cactus",
                                "mesh-renderer/main.cactus",
                                "model-animation/main.cactus",
                                "model-renderer/main.cactus",
                                "named_entity_access.cactus",
                                "native_field_reads.cactus",
                                "neighbor_joins.cactus",
                                "pair-contact/main.cactus",
                                "pair_binding_without.cactus",
                                "parameterized_templates.cactus",
                                "particle-burst/particle_burst.cactus",
                                "platformer/platformer.cactus",
                                "relation_end_reads.cactus",
                                "relation_endpoint_limits.cactus",
                                "relation_row_joins.cactus",
                                "relation_row_projection.cactus",
                                "relations.cactus",
                                "render-pass-domain/render_pass_domain.cactus",
                                "required_links.cactus",
                                "robot-sandbox/main.cactus",
                                "robot-sandbox/small_world.cactus",
                                "rule_groups.cactus",
                                "rule_keep.cactus",
                                "rule_reductions.cactus",
                                "singleton_traits.cactus",
                                "split-screen-forest-bombs/forest_bombs.cactus",
                                "split-screen/split_screen.cactus",
                                "standard-ui/main.cactus",
                                "state_transitions.cactus",
                                "stdlib-fixtures/animation_event_order.cactus",
                                "stdlib-fixtures/app_quit.cactus",
                                "stdlib-fixtures/camera_projection.cactus",
                                "stdlib-fixtures/camera_view.cactus",
                                "stdlib-fixtures/character_body.cactus",
                                "stdlib-fixtures/event_payload_reads.cactus",
                                "stdlib-fixtures/flat_query_ties.cactus",
                                "stdlib-fixtures/follow_cameras.cactus",
                                "stdlib-fixtures/gameplay_timers.cactus",
                                "stdlib-fixtures/grid.cactus",
                                "stdlib-fixtures/hierarchy_reparent.cactus",
                                "stdlib-fixtures/minimap.cactus",
                                "stdlib-fixtures/model_animation_events.cactus",
                                "stdlib-fixtures/noise.cactus",
                                "stdlib-fixtures/per_view_visibility.cactus",
                                "stdlib-fixtures/persistence_save_events.cactus",
                                "stdlib-fixtures/persistence_save_on_load.cactus",
                                "stdlib-fixtures/ranged_fields.cactus",
                                "stdlib-fixtures/smooth_volume.cactus",
                                "stdlib-fixtures/spatial_query_ties.cactus",
                                "stdlib-fixtures/staged_predicates.cactus",
                                "stdlib-fixtures/stdlib_extern_coverage.cactus",
                                "stdlib-fixtures/texture_assets.cactus",
                                "stdlib-fixtures/texture_flipbook.cactus",
                                "stdlib-fixtures/trait_lifecycle.cactus",
                                "stdlib-fixtures/trait_parameters.cactus",
                                "stdlib-fixtures/transform_hierarchy.cactus",
                                "stdlib-fixtures/voxel_rendering.cactus",
                                "stdlib-fixtures/voxel_volume.cactus",
                                "stdlib-fixtures/world_persistence.cactus",
                                "stdlib-fixtures/world_persistence_dynamic.cactus",
                                "total_arithmetic.cactus",
                                "trait_dimensions.cactus",
                                "waving-label-2d/waving_label_2d.cactus",
                                "waving-label-3d/waving_label_3d.cactus"}) {
        const auto root = examples / example;
        ProgramPipeline pipeline(fs::path(CACTUS_TEST_BINARY_DIR) / "blend_weights" / "examples" /
                                 root.parent_path().filename() / root.stem());
        auto merged = pipeline.merge(root, {root.parent_path()});
        INFO(merged.describe());
        REQUIRE_FALSE(merged.has_errors());
        blends += find_blend_weights(*merged.program).size();
        ErrorReporter errors;
        check_blend_weights(*merged.program, errors);
        merged.diagnostics = errors.diagnostics();
        std::ranges::copy(blend_warnings(merged), std::back_inserter(warnings));
    }
    CHECK(blends >= 5);
    for (const auto& warning : warnings) {
        UNSCOPED_INFO(warning);
    }
    CHECK(warnings.empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
