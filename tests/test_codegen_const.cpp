// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/cpp_entt_codegen.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cactus;

namespace {

const std::string kLifecycle =
    "pub event tick:\n"
    "    dt: float\n"
    "pub event load\n";

struct Generated {
    std::unique_ptr<ProgramNode> ast;
    std::string code;

    [[nodiscard]] std::size_t at(const std::string& text) const {
        const auto position = code.find(text);
        INFO("missing: " << text);
        REQUIRE(position != std::string::npos);
        return position;
    }
    [[nodiscard]] bool has(const std::string& text) const {
        return code.contains(text);
    }
};

Generated generate(const std::string& body, const ModuleImports& imports = {}) {
    Generated generated;
    ErrorReporter errors;
    Lexer lexer("module test\n" + kLifecycle + body, "test.cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    generated.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    auto program = analyzer.analyze(*generated.ast, imports);
    REQUIRE_FALSE(errors.has_errors());
    generated.code = CppEnttCodegen::generate(program);
    return generated;
}

const std::string kUnitDef =
    "struct UnitDef:\n"
    "    speed: float\n"
    "    health: int\n";

}  // namespace

TEST_CASE("Codegen constants: module-qualified names in dependency order", "[codegen-entt][const]") {
    const auto generated = generate(
        "const:\n"
        "    HALF = LATER * 0.5\n"
        "    LATER = 2.0\n");
    const auto later = generated.at("inline const float test__LATER = 2.0F;");
    const auto half  = generated.at("inline const float test__HALF = (test__LATER * 0.5F);");
    CHECK(later < half);
}

TEST_CASE("Codegen constants: vector, struct, and list constants", "[codegen-entt][const]") {
    const auto generated = generate(kUnitDef +
                                    "const:\n"
                                    "    ORIGIN = vec2(1.0, 2.0)\n"
                                    "    ROBOT = UnitDef(health = 3, speed = 4.0)\n"
                                    "    WAVES: list[UnitDef] = [ROBOT, UnitDef(speed = 2.5, health = 6)]\n"
                                    "    NONE: list[UnitDef] = []\n");
    CHECK(generated.has("inline const Vector2 test__ORIGIN = vec2(1.0F, 2.0F);"));
    const auto unit = generated.at("struct test__UnitDef");
    const auto robot =
        generated.at("inline const test__UnitDef test__ROBOT = test__UnitDef{.speed = 4.0F, .health = 3};");
    CHECK(unit < robot);
    CHECK(generated.has("inline const std::vector<test__UnitDef> test__WAVES = {test__ROBOT, "
                        "test__UnitDef{.speed = 2.5F, .health = 6}};"));
    CHECK(generated.has("inline const std::vector<test__UnitDef> test__NONE = {};"));
}

TEST_CASE("Codegen constants: a constant calling a pure func follows its declaration", "[codegen-entt][const]") {
    const auto generated = generate(
        "func twice(v: float) float:\n"
        "    return v * 2.0\n"
        "const:\n"
        "    SIX = twice(3.0)\n");
    const auto prototype = generated.at("float test__twice(float v);");
    const auto six       = generated.at("inline const float test__SIX = test__twice(3.0F);");
    CHECK(prototype < six);
}

TEST_CASE("Codegen constants: trait defaults may read constants", "[codegen-entt][const]") {
    const auto generated = generate(
        "const:\n"
        "    MOVE_SPEED = 6.0\n"
        "trait Mover:\n"
        "    var speed: float = MOVE_SPEED * 0.5\n");
    const auto constant = generated.at("inline const float test__MOVE_SPEED");
    const auto mover    = generated.at("struct test__Mover");
    CHECK(constant < mover);
    CHECK(generated.has("(test__MOVE_SPEED * 0.5F)"));
}

TEST_CASE("Codegen constants: handlers read constants and iterate list constants by reference",
          "[codegen-entt][const]") {
    const auto generated = generate(kUnitDef +
                                    "const:\n"
                                    "    ROBOT = UnitDef(speed = 4.0, health = 3)\n"
                                    "    WAVES: list[UnitDef] = [ROBOT]\n"
                                    "trait Probe:\n"
                                    "    var speed: float = 0.0\n"
                                    "rule Read:\n"
                                    "    filter:\n"
                                    "        Probe as p\n"
                                    "    on tick:\n"
                                    "        p.speed = ROBOT.speed\n"
                                    "        for unit in WAVES:\n"
                                    "            p.speed += unit.speed\n");
    CHECK(generated.has("= test__ROBOT.speed;"));
    CHECK(generated.has("const auto& foreach_snapshot_"));
    CHECK(generated.has(" = test__WAVES;"));
}

TEST_CASE("Codegen constants: window configuration reads constants by symbol", "[codegen-entt][const]") {
    const auto generated = generate(
        "const:\n"
        "    WINDOW_WIDTH = 640 * 2\n"
        "    WINDOW_TITLE = \"Tables\"\n");
    CHECK(generated.has("inline const int test__WINDOW_WIDTH = (640 * 2);"));
    CHECK(generated.has(".window_width = test__WINDOW_WIDTH"));
    CHECK(generated.has(".window_title = test__WINDOW_TITLE"));
}

TEST_CASE("Codegen struct construction: designated initializers in field order", "[codegen-entt][struct-construction]") {
    const auto generated = generate(kUnitDef +
                                    "trait Holder:\n"
                                    "    var unit: UnitDef\n"
                                    "rule Build:\n"
                                    "    filter:\n"
                                    "        Holder as h\n"
                                    "    on tick:\n"
                                    "        h.unit = UnitDef(health = 2, speed = 1.0)\n"
                                    "        add Holder to self:\n"
                                    "            unit = UnitDef(speed = 3.0, health = 4)\n");
    CHECK(generated.has("test__UnitDef{.speed = 1.0F, .health = 2}"));
    CHECK(generated.has("test__UnitDef{.speed = 3.0F, .health = 4}"));
}

TEST_CASE("Codegen struct construction: an impure reordered argument keeps source evaluation order",
          "[codegen-entt][struct-construction]") {
    const auto generated = generate(kUnitDef +
                                    "extern func roll() int\n"
                                    "trait Holder:\n"
                                    "    var unit: UnitDef\n"
                                    "rule Build:\n"
                                    "    filter:\n"
                                    "        Holder as h\n"
                                    "    on tick:\n"
                                    "        h.unit = UnitDef(health = roll(), speed = 1.0)\n");
    const auto first  = generated.at("auto cactus_gen_field_0 = ");
    const auto second = generated.at("auto cactus_gen_field_1 = 1.0F;");
    CHECK(first < second);
    CHECK(generated.has("test__UnitDef{.speed = cactus_gen_field_1, .health = cactus_gen_field_0}"));
}

TEST_CASE("Codegen struct construction: template and entity bodies", "[codegen-entt][struct-construction]") {
    const auto generated = generate(kUnitDef +
                                    "trait Holder:\n"
                                    "    var unit: UnitDef\n"
                                    "template Unit:\n"
                                    "    Holder:\n"
                                    "        unit = UnitDef(speed = 1.0, health = 1)\n"
                                    "entity Boss:\n"
                                    "    Holder:\n"
                                    "        unit = UnitDef(speed = 9.0, health = 9)\n");
    CHECK(generated.has("test__UnitDef{.speed = 1.0F, .health = 1}"));
    CHECK(generated.has("test__UnitDef{.speed = 9.0F, .health = 9}"));
    CHECK_FALSE(generated.has("UnitDef(speed"));
}
TEST_CASE("Codegen constants: a stage handler inlines a derived scalar constant into GLSL",
          "[codegen-entt][const][render-passes]") {
    ImportedSymbols passes;
    passes.module_name = "std.render.passes";
    ResolvedEnum pass;
    pass.name               = "Pass";
    pass.variants           = {"Quads"};
    passes.enums["Pass"]    = pass;
    ResolvedEnum target;
    target.name             = "Target";
    target.variants         = {"Screen"};
    passes.enums["Target"]  = target;
    ModuleImports imports;
    imports.add("passes", std::move(passes));

    const auto generated = generate(
        "use std.render.passes as passes\n"
        "const:\n"
        "    SIZE = 200.0\n"
        "    HALF = SIZE * 0.5\n"
        "trait Sprite:\n"
        "    var position: vec2 = vec2(0.0, 0.0)\n"
        "extern event Frame:\n"
        "    dt: float\n"
        "pub phase my_pass:\n"
        "    from:\n"
        "        Frame\n"
        "    pipeline: passes.Pass = passes.Pass.Quads\n"
        "    output: passes.Target = passes.Target.Screen\n"
        "rule Vertex:\n"
        "    filter:\n"
        "        Sprite as s\n"
        "    on my_pass.vertex as v:\n"
        "        v.screen_position = s.position + v.corner * HALF\n"
        "        v.uv_out = v.uv\n"
        "        v.tint_out = #FFFFFFFF\n"
        "rule Fragment:\n"
        "    on my_pass.fragment as f:\n"
        "        f.frag_color = f.tint\n",
        imports);
    CHECK(generated.has("(corner * (200.0 * 0.5))"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
