// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "linked_program.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace cactus;
using namespace cactus_test;
namespace fs = std::filesystem;

namespace {

const std::string kPrelude =
    "module game\n"
    "use std.core\n"
    "use std.color as color\n"
    "use std.math as math\n"
    "use std.transform.flat as tf\n"
    "use std.render.layers as layers\n";

const std::string kDeclarations =
    "enum Mood:\n"
    "    Calm\n"
    "    Wild\n"
    "trait Ember:\n"
    "    var heat: float = 1.0\n"
    "    var level: int = 0\n"
    "    var lift: vec3 = vec3(0.0, 0.0, 0.0)\n"
    "    var mood: Mood = Mood.Calm\n"
    "    var name: string = \"\"\n"
    "    var owner: entity_id\n"
    "    var span: duration = 0s\n"
    "entity Spark:\n"
    "    tf.WorldTransform\n"
    "    Ember\n";

const std::string kVertex = "        v.world_position = xf.position + v.corner * 4.0\n";
const std::string kFragment = "        f.frag_color = f.tint\n";

// The drawing rule `Fire` over WorldTransform and Ember, in the sprites layer.
std::string fire(const std::string& clauses = "",
                 const std::string& vertex = kVertex,
                 const std::string& fragment = kFragment) {
    return "rule Fire:\n"
           "    each:\n"
           "        tf.WorldTransform as xf\n"
           "        Ember as ember\n"
           "    group: layers.sprites\n" +
           clauses + "    on render.vertex as v:\n" + vertex + "    on render.fragment as f:\n" + fragment;
}

LinkedProgram link_game(const std::string& body, const std::string& uses = "") {
    static int next_case = 0;
    const auto dir = fs::path(CACTUS_TEST_BINARY_DIR) / "drawing_rules" / ("case" + std::to_string(next_case++));
    auto linked    = link_sources(dir, {{"game", kPrelude + uses + kDeclarations + body}}, "game");
    UNSCOPED_INFO(linked.describe());
    return linked;
}

bool has_error(const LinkedProgram& linked, const std::string& text) {
    return std::ranges::any_of(linked.diagnostics, [&](const Diagnostic& d) {
        return d.level == DiagnosticLevel::Error && d.message.contains(text);
    });
}

const SymbolId kFire   = make_symbol_id(SymbolKind::Rule, "game", "Fire");
const SymbolId kRender = make_symbol_id(SymbolKind::Phase, "std.core", "render");

const HandlerNode* node_of(const DecoratedProgram& program, const SymbolId& rule) {
    const auto found = std::ranges::find_if(program.execution_graph.handlers,
                                            [&](const HandlerNode& node) { return node.identity.rule == rule; });
    return found == program.execution_graph.handlers.end() ? nullptr : &*found;
}

}  // namespace

// ── Shape of a drawing rule ─────────────────────────────────────────────────

TEST_CASE("Drawing rules: a rule with both stage handlers is one render rule", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire("", kVertex, "        f.frag_color = color.with_alpha(f.tint, ember.heat)\n"));
    REQUIRE_FALSE(linked.has_errors());
    const auto& program = *linked.program;
    const auto nodes    = std::ranges::count_if(program.execution_graph.handlers,
                                             [](const HandlerNode& node) { return node.identity.rule == kFire; });
    CHECK(nodes == 1);
    const auto* node = node_of(program, kFire);
    REQUIRE(node != nullptr);
    CHECK(node->identity.trigger == ResolvedHandlerTrigger{.kind = HandlerTriggerKind::Phase, .symbol = kRender});
    CHECK(node->contract.writes.empty());
    CHECK(node->contract.commands.empty());
    CHECK(node->contract.effects.contains("graphics"));
    CHECK(node->contract.reads.contains(make_symbol_id(SymbolKind::Trait, "std.transform.flat", "WorldTransform")));
    CHECK(node->contract.reads.contains(make_symbol_id(SymbolKind::Trait, "game", "Ember")));
}

TEST_CASE("Drawing rules: the graph holds a rasterization step between the stages", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire());
    REQUIRE_FALSE(linked.has_errors());
    const auto& graph = linked.program->execution_graph;
    REQUIRE(graph.drawing_rules.size() == 1);
    const auto& plan = graph.drawing_rules.front();
    CHECK(plan.rule == kFire);
    CHECK(plan.vertex_handler.rule == kFire);
    CHECK(plan.vertex_handler.trigger.kind == HandlerTriggerKind::RenderStage);
    CHECK(plan.vertex_handler.trigger.symbol.local_name == "render__vertex");
    CHECK(plan.fragment_handler.trigger.symbol.local_name == "render__fragment");
    CHECK(std::ranges::none_of(graph.schedule_edges, [&](const ScheduleEdge& edge) {
        return edge.before.rule == kFire && edge.after.rule == kFire;
    }));
}

TEST_CASE("Drawing rules: a program may hold several drawing rules", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire() +
                                  "rule Halo:\n"
                                  "    each:\n"
                                  "        Ember as ember\n"
                                  "    group: layers.sprites\n"
                                  "    after: Fire\n"
                                  "    on render.vertex as v:\n"
                                  "        v.screen_position = vec2(10.0, 10.0) + v.corner * ember.heat\n"
                                  "    on render.fragment as f:\n"
                                  "        f.frag_color = #FFFFFFFF\n");
    REQUIRE_FALSE(linked.has_errors());
    CHECK(linked.program->execution_graph.drawing_rules.size() == 2);
}

TEST_CASE("Drawing rules: one stage handler without the other is an error", "[semantic][drawing-rules]") {
    const auto linked = link_game(
        "rule Fire:\n"
        "    each:\n"
        "        Ember as ember\n"
        "    group: layers.sprites\n"
        "    on render.vertex as v:\n"
        "        v.screen_position = v.corner\n");
    CHECK(has_error(linked, "drawing rule 'Fire' has no `render.fragment` handler"));
}

TEST_CASE("Drawing rules: a second handler of one stage is an error", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire() + "    on render.vertex as w:\n        w.world_position = w.corner\n");
    CHECK(has_error(linked, "drawing rule 'Fire' has more than one `render.vertex` handler"));
}

TEST_CASE("Drawing rules: another handler in a drawing rule is an error", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire() + "    on tick:\n        ember.heat = 0.0\n");
    CHECK(has_error(linked, "a drawing rule has only its `render.vertex` and `render.fragment` handlers"));
}

TEST_CASE("Drawing rules: clauses that change how rows run are rejected", "[semantic][drawing-rules]") {
    SECTION("pairs") {
        const auto linked = link_game(
            "rule Fire:\n"
            "    pairs:\n"
            "        a: Ember\n"
            "        b: Ember\n"
            "    on render.vertex as v:\n"
            "        v.screen_position = v.corner\n"
            "    on render.fragment as f:\n"
            "        f.frag_color = f.tint\n");
        CHECK(has_error(linked, "a drawing rule needs a unary `each:`"));
    }
    SECTION("reduce") {
        CHECK(has_error(link_game(fire("    reduce:\n        total = sum(ember.heat)\n")),
                        "a drawing rule can't declare `reduce:`"));
    }
    SECTION("limit") {
        CHECK(has_error(link_game(fire("    limit: 3\n")), "a drawing rule can't declare `limit:`"));
    }
    SECTION("repeat") {
        CHECK(has_error(link_game(fire("    repeat: 2\n")), "a drawing rule draws each row once"));
    }
    SECTION("keep") {
        const auto linked = link_game(
            "trait Lit\n"
            "rule Fire:\n"
            "    each:\n"
            "        Ember as ember\n"
            "    keep Lit\n"
            "    on render.vertex as v:\n"
            "        v.screen_position = v.corner\n"
            "    on render.fragment as f:\n"
            "        f.frag_color = f.tint\n");
        CHECK(has_error(linked, "a drawing rule can't keep"));
    }
}

TEST_CASE("Drawing rules: a drawing rule draws entities, not cells", "[semantic][drawing-rules][trait-dimensions]") {
    const auto linked = link_game(
        "trait Room:\n"
        "    dim x: int in range(0, 4)\n"
        "    var gas: float = 0.0\n"
        "entity Hall:\n"
        "    Room\n"
        "rule Fire:\n"
        "    each:\n"
        "        Room as c\n"
        "    on render.vertex as v:\n"
        "        v.screen_position = v.corner\n"
        "    on render.fragment as f:\n"
        "        f.frag_color = f.tint\n");
    CHECK(has_error(linked, "a drawing rule draws entities"));
}

TEST_CASE("Drawing rules: a stage handler does not read cells", "[semantic][drawing-rules][trait-dimensions]") {
    const auto linked = link_game("single trait Room:\n"
                                  "    dim x: int in range(0, 4)\n"
                                  "    var gas: float = 0.0\n"
                                  "entity Hall:\n"
                                  "    Room\n" +
                                  fire("", kVertex, "        f.frag_color = f.tint * Room[2].gas\n"));
    CHECK(has_error(linked, "a stage handler does not read cells"));
}

TEST_CASE("Drawing rules: a duration read suggests seconds(...)", "[semantic][drawing-rules][dsl-duration]") {
    const auto linked =
        link_game(fire("", "        v.world_position = xf.position + v.corner * (ember.span / ember.span)\n"));
    CHECK(has_error(linked, "a stage handler can't use a `duration`"));
    CHECK(has_error(linked, "seconds("));
}

TEST_CASE("Drawing rules: only render has stages", "[semantic][drawing-rules]") {
    const auto linked = link_game(
        "rule Fire:\n"
        "    each:\n"
        "        Ember as ember\n"
        "    on tick.vertex as v:\n"
        "        v.screen_position = v.corner\n");
    CHECK(has_error(linked, "'std.core.tick' has no stage 'vertex'"));
}

TEST_CASE("Drawing rules: a stage handler writes only stage outputs", "[semantic][drawing-rules]") {
    SECTION("trait write") {
        const auto linked = link_game(fire("", kVertex + "        ember.heat = 0.0\n"));
        CHECK(has_error(linked, "a stage handler writes only stage outputs"));
    }
    SECTION("command") {
        const auto linked = link_game(fire("", kVertex, kFragment + "        destroy\n"));
        CHECK(has_error(linked, "a stage handler writes only stage outputs"));
    }
    SECTION("world query") {
        const auto linked = link_game(
            fire("", kVertex, "        f.frag_color = f.tint * (1.0 * query.count[Ember]())\n"), "use std.query as query\n");
        CHECK(has_error(linked, "a stage handler can't run the world query 'query.count'"));
    }
}

TEST_CASE("Drawing rules: a bounded for is not a stage statement", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire("", kVertex, "        for i in range(0, 2):\n            f.frag_color = f.tint\n" + kFragment));
    CHECK(has_error(linked, "'for' is not allowed in a stage handler body"));
}

TEST_CASE("Drawing rules: a stage reads only values a shader holds", "[semantic][drawing-rules]") {
    SECTION("string") {
        const auto linked = link_game(fire("", kVertex, "        let s = ember.name\n" + kFragment));
        CHECK(has_error(linked, "a stage handler can't read a value of type 'string'"));
    }
    SECTION("entity_id") {
        const auto linked = link_game(fire("", kVertex, "        let e = ember.owner\n" + kFragment));
        CHECK(has_error(linked, "a stage handler can't read a value of type 'entity_id'"));
    }
}

TEST_CASE("Drawing rules: the vertex handler places the quad in exactly one space", "[semantic][drawing-rules]") {
    SECTION("both") {
        const auto linked = link_game(fire("", kVertex + "        v.screen_position = v.corner\n"));
        CHECK(has_error(linked, "assign exactly one of `world_position` and `screen_position`"));
    }
    SECTION("neither on a path") {
        const auto linked = link_game(fire("",
                                           "        if ember.heat > 0.5:\n"
                                           "            v.world_position = xf.position\n"));
        CHECK(has_error(linked, "assign `world_position` or `screen_position` on every path"));
    }
    SECTION("on every path") {
        const auto linked = link_game(fire("",
                                           "        if ember.heat > 0.5:\n"
                                           "            v.world_position = xf.position\n"
                                           "        else:\n"
                                           "            v.world_position = xf.position + v.corner\n"));
        CHECK_FALSE(linked.has_errors());
    }
}

TEST_CASE("Drawing rules: the fragment handler assigns frag_color on every path", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire("", kVertex,
                                       "        if f.uv.x > 0.5:\n"
                                       "            f.frag_color = f.tint\n"));
    CHECK(has_error(linked, "assign `frag_color` on every path"));
}

TEST_CASE("Drawing rules: an extern func with no shader translation is rejected", "[semantic][drawing-rules]") {
    const auto linked = link_game("extern func something(x: float) float\n" +
                                  fire("", kVertex, "        let y = something(1.0)\n" + kFragment));
    CHECK(has_error(linked, "game.something"));
    CHECK(has_error(linked, "no registered portable GLSL translation"));
}

TEST_CASE("Drawing rules: stage triggers are rejected on an extern rule", "[semantic][drawing-rules]") {
    const auto linked = link_game(
        "extern rule Bogus:\n"
        "    each:\n"
        "        Ember\n"
        "    on render.vertex:\n"
        "        reads:\n"
        "            Ember\n");
    CHECK(has_error(linked, "stage triggers are not valid on 'extern rule' declarations"));
}

// ── Reads ───────────────────────────────────────────────────────────────────

TEST_CASE("Drawing rules: the fragment handler reads its row", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire("", kVertex, "        f.frag_color = color.with_alpha(f.tint, ember.heat)\n"));
    CHECK_FALSE(linked.has_errors());
}

TEST_CASE("Drawing rules: a stage reads a named entity", "[semantic][drawing-rules]") {
    const auto linked =
        link_game(fire("", "        v.screen_position = vp.Screen[vp.ScreenSize].size * 0.5 + v.corner\n"),
                  "use std.camera.viewport as vp\n");
    CHECK_FALSE(linked.has_errors());
    const auto* node = node_of(*linked.program, kFire);
    REQUIRE(node != nullptr);
    CHECK(node->contract.reads.contains(make_symbol_id(SymbolKind::Trait, "std.camera.viewport", "ScreenSize")));
}

namespace {

std::string beams(const std::string& read) {
    return "trait Beam:\n"
           "    let sink: tf.WorldTransform\n"
           "entity Ray:\n"
           "    Beam:\n"
           "        sink = Spark\n"
           "rule DropBeams:\n"
           "    each: Beam\n"
           "    on lost Beam.sink:\n"
           "        destroy\n"
           "rule Beams:\n"
           "    each:\n"
           "        Beam as beam\n"
           "    on render.vertex as v:\n"
           "        v.world_position = " +
           read +
           " + v.corner\n"
           "    on render.fragment as f:\n"
           "        f.frag_color = f.tint\n";
}

}  // namespace

TEST_CASE("Drawing rules: a stage reads through a link of its row", "[semantic][drawing-rules]") {
    const auto linked = link_game(beams("beam.sink[tf.WorldTransform].position"));
    CHECK_FALSE(linked.has_errors());
}

TEST_CASE("Drawing rules: a link read of a trait the link does not carry is rejected", "[semantic][drawing-rules]") {
    const auto linked = link_game(beams("vec2(beam.sink[Ember].heat, 0.0)"));
    CHECK(has_error(linked, "through link"));
}

TEST_CASE("Drawing rules: int, vec3 and enum reads are accepted", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire("",
                                       "        let k = ember.level / 2\n"
                                       "        let wild = ember.mood == Mood.Wild\n"
                                       "        v.world_position = xf.position + v.corner * ember.lift.x\n"
                                       "        if wild or k > 1:\n"
                                       "            v.tint_out = #FF0000FF\n"));
    CHECK_FALSE(linked.has_errors());
}

TEST_CASE("Drawing rules: stage reads order the rule after a render writer", "[semantic][drawing-rules]") {
    const auto linked = link_game(fire("", kVertex, "        f.frag_color = color.with_alpha(f.tint, ember.heat)\n") +
                                  "rule Cool:\n"
                                  "    each:\n"
                                  "        Ember as ember\n"
                                  "    on render:\n"
                                  "        ember.heat = ember.heat * 0.5\n");
    REQUIRE_FALSE(linked.has_errors());
    const auto cool = make_symbol_id(SymbolKind::Rule, "game", "Cool");
    CHECK(std::ranges::any_of(linked.program->execution_graph.schedule_edges, [&](const ScheduleEdge& edge) {
        return edge.before.rule == cool && edge.after.rule == kFire;
    }));
}

// A rule of the importing module runs before an imported public group, so an ungrouped drawing rule draws under it.
TEST_CASE("Drawing rules: an ungrouped drawing rule draws under the standard layers", "[semantic][drawing-rules]") {
    const auto linked = link_game(
        "rule Fire:\n"
        "    each:\n"
        "        tf.WorldTransform as xf\n"
        "        Ember as ember\n"
        "    on render.vertex as v:\n" +
            kVertex + "    on render.fragment as f:\n" + kFragment +
            "entity Plate:\n"
            "    tf.WorldTransform\n"
            "    shapes.Shape\n",
        "use std.render.shapes as shapes\n");
    REQUIRE_FALSE(linked.has_errors());
    const auto shapes = make_symbol_id(SymbolKind::Rule, "std.render.shapes", "ShapeRenderer");
    CHECK(std::ranges::any_of(linked.program->execution_graph.schedule_edges, [&](const ScheduleEdge& edge) {
        return edge.before.rule == kFire && edge.after.rule == shapes;
    }));
}

// ── Render passes are gone ──────────────────────────────────────────────────

TEST_CASE("Drawing rules: a Pass-typed phase field makes no render pass", "[semantic][drawing-rules]") {
    const auto linked = link_game(
        "enum Pass:\n"
        "    Quads\n"
        "pub phase my_pass:\n"
        "    after:\n"
        "        render\n"
        "    pipeline: Pass = Pass.Quads\n"
        "rule Fire:\n"
        "    each:\n"
        "        Ember as ember\n"
        "    on my_pass.vertex as v:\n"
        "        v.screen_position = v.corner\n");
    CHECK(has_error(linked, "'game.my_pass' has no stage 'vertex'"));
}

TEST_CASE("Drawing rules: std.render.passes is gone", "[semantic][drawing-rules]") {
    CHECK_FALSE(fs::exists(stdlib_root() / "std" / "render" / "passes.cactus"));
    const auto linked = link_game("", "use std.render.passes as passes\n");
    CHECK(linked.has_errors());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
