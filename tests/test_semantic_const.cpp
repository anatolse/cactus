// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using namespace cactus;
namespace fs = std::filesystem;

namespace {

const std::string kLifecycle =
    "pub event tick:\n"
    "    dt: float\n"
    "pub event load\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has(const std::string& text) const {
        return std::ranges::any_of(diagnostics,
                                   [&](const Diagnostic& d) { return d.message.contains(text); });
    }
    [[nodiscard]] bool clean() const {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ": " + d.message + "\n";
    }
    return text;
}

Analysis analyze(const std::string& body, const ModuleImports& imports = {}, const std::string& module = "game") {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer("module " + module + "\n" + kLifecycle + body, module + ".cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program     = analyzer.analyze(*analysis.ast, imports);
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

// Analyzes `source` as module `name` and exports it through a saved artifact,
// the same way an importing module sees it.
ImportedSymbols compile_dependency(const std::string& name, const std::string& body) {
    auto analysis = analyze(body, {}, name);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto build_dir = fs::path(CACTUS_TEST_FIXTURES_DIR) / "semantic_const_build";
    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    REQUIRE(artifact.save(analysis.program, name, build_dir));
    auto symbols = artifact.extract_pub_symbols(build_dir / ModuleArtifact::artifact_filename(name));
    REQUIRE(symbols.has_value());
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    return *symbols;
}

const std::string kSquad =
    "struct Squad:\n"
    "    lead: entity_id\n"
    "    size: int\n"
    "trait Roster:\n"
    "    var squad: Squad\n"
    "    var count: int = 0\n";

std::string in_handler(const std::string& statement) {
    return kSquad +
           "rule Build:\n"
           "    filter:\n"
           "        Roster as r\n"
           "    on tick:\n"
           "        let other = self\n"
           "        " +
           statement + "\n";
}

const CallExpr* find_call(const ExprNode& expr) {
    const CallExpr* found = nullptr;
    if (const auto* call = std::get_if<CallExpr>(&expr.expr)) {
        found = call;
    }
    return found;
}

}  // namespace

// ── Struct construction ─────────────────────────────────────────────────────

TEST_CASE("Struct construction: named fields in any order type-check", "[semantic][struct-construction]") {
    auto analysis = analyze(in_handler("r.squad = Squad(size = 3, lead = other)"));
    INFO(describe(analysis));
    CHECK(analysis.clean());

    const auto& rule  = std::get<RuleNode>(analysis.ast->declarations.back());
    const auto& stmt  = std::get<VarAssign>(rule.handlers.front().body.back()->stmt);
    const auto* call  = find_call(*stmt.value);
    REQUIRE(call != nullptr);
    REQUIRE(call->resolved_struct_id.has_value());
    CHECK(call->resolved_struct_id->local_name == "Squad");
    CHECK(call->resolved_struct_id->kind == SymbolKind::Struct);
}

TEST_CASE("Struct construction: positional arguments are rejected", "[semantic][struct-construction]") {
    auto analysis = analyze(in_handler("r.squad = Squad(other, 3)"));
    INFO(describe(analysis));
    CHECK(analysis.has("struct 'Squad' fields must be named"));
}

TEST_CASE("Struct construction: a missing field is rejected", "[semantic][struct-construction]") {
    auto analysis = analyze(in_handler("r.squad = Squad(lead = other)"));
    INFO(describe(analysis));
    CHECK(analysis.has("struct 'Squad' is missing field 'size'"));
}

TEST_CASE("Struct construction: an unknown field is rejected", "[semantic][struct-construction]") {
    auto analysis = analyze(in_handler("r.squad = Squad(lead = other, size = 3, rank = 1)"));
    INFO(describe(analysis));
    CHECK(analysis.has("struct 'Squad' has no field 'rank'"));
}

TEST_CASE("Struct construction: a repeated field is rejected", "[semantic][struct-construction]") {
    auto analysis = analyze(in_handler("r.squad = Squad(lead = other, size = 3, size = 4)"));
    INFO(describe(analysis));
    CHECK(analysis.has("struct 'Squad' field 'size' is given more than once"));
}

TEST_CASE("Struct construction: a wrong field type is rejected", "[semantic][struct-construction]") {
    auto analysis = analyze(in_handler("r.squad = Squad(lead = other, size = 2.5)"));
    INFO(describe(analysis));
    CHECK(analysis.has("'float' does not match field 'size' of type 'int'"));
}

TEST_CASE("Struct construction: a struct assigned to a non-struct place is rejected",
          "[semantic][struct-construction]") {
    auto analysis = analyze(in_handler("r.count = Squad(lead = other, size = 3)"));
    INFO(describe(analysis));
    CHECK(analysis.has("type mismatch"));
}

TEST_CASE("Struct construction: an imported struct is built through its alias", "[semantic][struct-construction]") {
    ModuleImports imports;
    imports.add("units",
                compile_dependency("unit_defs",
                                   "pub struct UnitDef:\n"
                                   "    speed: float\n"
                                   "    health: int\n"));
    auto analysis = analyze(
        "use unit_defs as units\n"
        "trait Holder:\n"
        "    var unit: units.UnitDef\n"
        "rule Build:\n"
        "    filter:\n"
        "        Holder as h\n"
        "    on tick:\n"
        "        h.unit = units.UnitDef(\n"
        "            health = 3,\n"
        "            speed = 1.5,\n"
        "        )\n",
        imports);
    INFO(describe(analysis));
    CHECK(analysis.clean());

    const auto& rule = std::get<RuleNode>(analysis.ast->declarations.back());
    const auto& stmt = std::get<VarAssign>(rule.handlers.front().body.back()->stmt);
    const auto* call = find_call(*stmt.value);
    REQUIRE(call != nullptr);
    REQUIRE(call->resolved_struct_id.has_value());
    CHECK(call->resolved_struct_id->module.name == "unit_defs");
    CHECK(call->arg_names == std::vector<std::string>{"health", "speed"});
}

TEST_CASE("Struct construction: an imported struct with a wrong field is rejected",
          "[semantic][struct-construction]") {
    ModuleImports imports;
    imports.add("units",
                compile_dependency("unit_defs",
                                   "pub struct UnitDef:\n"
                                   "    speed: float\n"));
    auto analysis = analyze(
        "use unit_defs as units\n"
        "rule Build:\n"
        "    on tick:\n"
        "        let u = units.UnitDef(pace = 1.0)\n",
        imports);
    INFO(describe(analysis));
    CHECK(analysis.has("struct 'UnitDef' has no field 'pace'"));
}

TEST_CASE("Struct construction: reading a field of a constructed value", "[semantic][struct-construction]") {
    auto analysis = analyze(in_handler("r.count = Squad(lead = other, size = 3).size"));
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Named arguments on an ordinary function call are rejected", "[semantic][struct-construction]") {
    auto analysis = analyze(
        "func twice(v: float) float:\n"
        "    return v * 2.0\n"
        "rule Use:\n"
        "    on tick:\n"
        "        let x = twice(v = 2.0)\n");
    INFO(describe(analysis));
    CHECK(analysis.has("named arguments are only allowed when constructing a struct"));
}

// ── Const expressions ───────────────────────────────────────────────────────

namespace {

ModuleImports std_math_imports() {
    ModuleImports imports;
    imports.add("math",
                compile_dependency("std.math",
                                   "const:\n"
                                   "    PI = 3.14159265\n"
                                   "pub extern func sqrt(v: float) float\n"
                                   "pub extern func radians(d: float) float\n"));
    return imports;
}

const ResolvedConst& constant(const Analysis& analysis, const std::string& name) {
    const auto found = analysis.program.consts.find(name);
    REQUIRE(found != analysis.program.consts.end());
    return found->second;
}

const std::string kUnitDef =
    "struct UnitDef:\n"
    "    speed: float\n"
    "    health: int\n";

}  // namespace

TEST_CASE("Const expressions: accepted forms type-check", "[semantic][const]") {
    auto analysis = analyze("use std.math as math\n" + kUnitDef +
                                "func twice(v: float) float:\n"
                                "    return v * 2.0\n"
                                "const:\n"
                                "    HALF_PI = math.PI * 0.5\n"
                                "    ROOT_TWO = math.sqrt(2.0)\n"
                                "    STEER = math.radians(40.0)\n"
                                "    SIX = twice(3.0)\n"
                                "    ORIGIN = vec2(1.0, 2.0)\n"
                                "    COUNT = 2 + 3 * 4\n"
                                "    NAME = \"robot\"\n"
                                "    ALIVE = not false\n"
                                "    ROBOT = UnitDef(speed = 4.0, health = 3)\n"
                                "    WAVES: list[UnitDef] = [ROBOT, UnitDef(speed = 2.5, health = 6)]\n"
                                "trait Probe:\n"
                                "    var x: float = 0.0\n"
                                "    var speed: float = 0.0\n"
                                "rule Read:\n"
                                "    filter:\n"
                                "        Probe as p\n"
                                "    on tick:\n"
                                "        p.x = ORIGIN.x + HALF_PI\n"
                                "        p.speed = ROBOT.speed\n"
                                "        for unit in WAVES:\n"
                                "            p.speed += unit.speed\n",
                            std_math_imports());
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK(constant(analysis, "HALF_PI").type.kind == TypeKind::Float);
    CHECK(constant(analysis, "ROOT_TWO").type.kind == TypeKind::Float);
    CHECK(constant(analysis, "SIX").type.kind == TypeKind::Float);
    CHECK(constant(analysis, "ORIGIN").type.kind == TypeKind::Vec2);
    CHECK(constant(analysis, "COUNT").type.kind == TypeKind::Int);
    CHECK(constant(analysis, "NAME").type.kind == TypeKind::String);
    CHECK(constant(analysis, "ROBOT").type.kind == TypeKind::Struct);
    const auto& waves = constant(analysis, "WAVES").type;
    CHECK(waves.kind == TypeKind::List);
    REQUIRE(waves.element != nullptr);
    CHECK(waves.element->kind == TypeKind::Struct);
}

TEST_CASE("Const expressions: a constant reference resolves to its module symbol", "[semantic][const]") {
    auto analysis = analyze("use std.math as math\n"
                            "const:\n"
                            "    HALF_PI = math.PI * 0.5\n"
                            "    QUARTER = HALF_PI * 0.5\n",
                            std_math_imports());
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& block   = std::get<ConstBlockNode>(analysis.ast->declarations.back());
    const auto& half_pi = std::get<BinaryExpr>(block.assignments[0].value->expr);
    const auto& pi      = std::get<MemberExpr>(half_pi.left->expr);
    REQUIRE(pi.resolved_const_id.has_value());
    CHECK(pi.resolved_const_id->module.name == "std.math");
    CHECK(pi.resolved_const_id->local_name == "PI");
    const auto& quarter = std::get<BinaryExpr>(block.assignments[1].value->expr);
    const auto& local   = std::get<IdentExpr>(quarter.left->expr);
    REQUIRE(local.resolved_const_id.has_value());
    CHECK(local.resolved_const_id->module.name == "game");
}

TEST_CASE("Const expressions: a trait read is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "trait Ping:\n"
        "    var x: float = 0.0\n"
        "const:\n"
        "    X = Ping.x\n");
    INFO(describe(analysis));
    CHECK(analysis.has("a constant cannot read a trait"));
}

TEST_CASE("Const expressions: an impure call is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "extern func roll() float\n"
        "const:\n"
        "    X = roll()\n");
    INFO(describe(analysis));
    CHECK(analysis.has("'roll' is not pure; constants must be pure"));
}

TEST_CASE("Const expressions: an entity name is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "trait Ping\n"
        "entity Boss:\n"
        "    Ping\n"
        "const:\n"
        "    X = Boss\n");
    INFO(describe(analysis));
    CHECK(analysis.has("a constant cannot name an entity"));
}

TEST_CASE("Const expressions: self is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    X = self\n");
    INFO(describe(analysis));
    CHECK(analysis.has("a constant cannot use `self`"));
}

TEST_CASE("Const expressions: a world query is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "trait Ping\n"
        "const:\n"
        "    X = q.count[Ping]()\n");
    INFO(describe(analysis));
    CHECK(analysis.has("a constant cannot query the world"));
}

TEST_CASE("Const expressions: spawn is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "trait Ping\n"
        "template Thing:\n"
        "    Ping\n"
        "const:\n"
        "    X = spawn Thing()\n");
    INFO(describe(analysis));
    CHECK(analysis.has("a constant cannot spawn"));
}

TEST_CASE("Const expressions: string operators are rejected", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    X = \"a\" + \"b\"\n");
    INFO(describe(analysis));
    CHECK_FALSE(analysis.clean());
}

TEST_CASE("Const expressions: a forward reference is accepted and ordered", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    HALF = LATER * 0.5\n"
        "    LATER = 2.0\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(constant(analysis, "HALF").type.kind == TypeKind::Float);
    const auto& order = analysis.program.const_order;
    REQUIRE(order.size() == 2);
    CHECK(order[0].local_name == "LATER");
    CHECK(order[1].local_name == "HALF");
}

TEST_CASE("Const expressions: a cycle is rejected naming every constant", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    A = B\n"
        "    B = A\n"
        "    C = 1\n");
    INFO(describe(analysis));
    CHECK(analysis.has("constant cycle: A -> B -> A"));
}

TEST_CASE("Const expressions: a matching type annotation is accepted", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    SPEED: float = 2.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK(constant(analysis, "SPEED").type.kind == TypeKind::Float);
}

TEST_CASE("Const expressions: a mismatched type annotation is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    SPEED: int = 2.5\n");
    INFO(describe(analysis));
    CHECK(analysis.has("value type 'float' does not match the declared type 'int'"));
}

TEST_CASE("Const expressions: an empty list needs a type annotation", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    NONE = []\n");
    INFO(describe(analysis));
    CHECK(analysis.has("constant 'NONE' needs a type annotation"));
}

TEST_CASE("Const expressions: an annotated empty list is accepted", "[semantic][const]") {
    auto analysis = analyze(kUnitDef +
                            "const:\n"
                            "    NONE: list[UnitDef] = []\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    const auto& type = constant(analysis, "NONE").type;
    CHECK(type.kind == TypeKind::List);
    REQUIRE(type.element != nullptr);
    CHECK(type.element->kind == TypeKind::Struct);
}

TEST_CASE("Const expressions: writing through a constant is rejected", "[semantic][const]") {
    auto analysis = analyze(kUnitDef +
                            "const:\n"
                            "    ROBOT = UnitDef(speed = 4.0, health = 3)\n"
                            "    SPEED = 1.0\n"
                            "rule Write:\n"
                            "    on tick:\n"
                            "        ROBOT.speed = 1.0\n"
                            "        SPEED = 2.0\n");
    INFO(describe(analysis));
    CHECK(analysis.has("constant 'ROBOT' is immutable"));
    CHECK(analysis.has("constant 'SPEED' is immutable"));
}

TEST_CASE("Const expressions: a copy of a struct constant is a mutable local", "[semantic][const]") {
    auto analysis = analyze(kUnitDef +
                            "const:\n"
                            "    ROBOT = UnitDef(speed = 4.0, health = 3)\n"
                            "rule Copy:\n"
                            "    on tick:\n"
                            "        var copy = ROBOT\n"
                            "        copy.speed = 1.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Const expressions: a local shadows a constant", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    SPEED = 1.0\n"
        "rule Shadow:\n"
        "    on tick:\n"
        "        var SPEED = 2\n"
        "        SPEED = 3\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Const expressions: same name in two modules", "[semantic][const]") {
    ModuleImports imports;
    imports.add("other", compile_dependency("other",
                                            "const:\n"
                                            "    SPEED = 2.0\n"));
    auto analysis = analyze(
        "use other\n"
        "const:\n"
        "    SPEED = 5.0\n"
        "    BOTH = SPEED + other.SPEED\n",
        imports);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& block = std::get<ConstBlockNode>(analysis.ast->declarations.back());
    const auto& both  = std::get<BinaryExpr>(block.assignments[1].value->expr);
    CHECK(std::get<IdentExpr>(both.left->expr).resolved_const_id->module.name == "game");
    CHECK(std::get<MemberExpr>(both.right->expr).resolved_const_id->module.name == "other");
}

TEST_CASE("Const expressions: a bare name does not reach an imported constant", "[semantic][const]") {
    auto analysis = analyze(
        "use std.math as math\n"
        "trait Probe:\n"
        "    var x: float = 0.0\n"
        "rule Read:\n"
        "    filter:\n"
        "        Probe as p\n"
        "    on tick:\n"
        "        p.x = PI\n",
        std_math_imports());
    INFO(describe(analysis));
    CHECK(analysis.has("unknown identifier 'PI'"));
}

TEST_CASE("Const expressions: an imported constant keeps its type", "[semantic][const]") {
    auto analysis = analyze("use std.math as math\n"
                            "const:\n"
                            "    BAD: int = math.PI\n",
                            std_math_imports());
    INFO(describe(analysis));
    CHECK(analysis.has("value type 'float' does not match the declared type 'int'"));
}

TEST_CASE("Const expressions: a duplicate name in one module is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    MAX_HEALTH = 100\n"
        "    MAX_HEALTH = 200\n");
    INFO(describe(analysis));
    CHECK(analysis.has("duplicate module-scope declaration 'MAX_HEALTH'"));
}

// ── Render-pass stage handlers ──────────────────────────────────────────────

namespace {

ModuleImports render_pass_imports() {
    ModuleImports imports;
    imports.add("passes", compile_dependency("std.render.passes",
                                             "pub enum Pass:\n"
                                             "    Quads\n"
                                             "pub enum Target:\n"
                                             "    Screen\n"));
    imports.add("math", compile_dependency("std.math",
                                           "pub extern func sqrt(v: float) float\n"
                                           "pub extern func sin(a: float) float\n"
                                           "pub extern func radians(d: float) float\n"
                                           "pub extern func degrees(r: float) float\n"));
    return imports;
}

// A render-pass program whose fragment handler writes `f.frag_color = <color_expr>`
// and whose vertex handler scales the quad corner by `<size_expr>`.
std::string render_pass_program(const std::string& constants,
                                const std::string& size_expr,
                                const std::string& color_expr = "f.tint") {
    return "use std.render.passes as passes\n"
           "use std.math as math\n" +
           kUnitDef + constants +
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
           "        v.screen_position = s.position + v.corner * " +
           size_expr +
           "\n"
           "        v.uv_out = v.uv\n"
           "        v.tint_out = #FFFFFFFF\n"
           "rule Fragment:\n"
           "    on my_pass.fragment as f:\n"
           "        f.frag_color = " +
           color_expr + "\n";
}

}  // namespace

TEST_CASE("Render-pass constants: a derived scalar constant is accepted", "[semantic][const][render-passes]") {
    auto analysis = analyze(render_pass_program("const:\n"
                                                "    SIZE = 200.0\n"
                                                "    HALF = math.sqrt(SIZE) * 0.5\n",
                                                "HALF"),
                            render_pass_imports());
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Render-pass constants: angle conversion is GLSL-portable", "[semantic][const][render-passes]") {
    auto analysis = analyze(render_pass_program("const:\n"
                                                "    STEER = math.radians(40.0)\n",
                                                "math.degrees(STEER)"),
                            render_pass_imports());
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Render-pass constants: a struct constant is rejected naming the constant",
          "[semantic][const][render-passes]") {
    auto analysis = analyze(render_pass_program("const:\n"
                                                "    ROBOT = UnitDef(speed = 4.0, health = 3)\n",
                                                "ROBOT.speed"),
                            render_pass_imports());
    INFO(describe(analysis));
    CHECK(analysis.has("render-pass vertex-stage handler cannot read constant 'ROBOT'"));
}

TEST_CASE("Render-pass constants: a constant built with a non-portable call is rejected",
          "[semantic][const][render-passes]") {
    auto analysis = analyze(render_pass_program("const:\n"
                                                "    WAVE = math.sin(1.0)\n"
                                                "    SCALE = WAVE * 2.0\n",
                                                "SCALE"),
                            render_pass_imports());
    INFO(describe(analysis));
    CHECK(analysis.has("render-pass vertex-stage handler cannot read constant 'SCALE'"));
}

TEST_CASE("Render-pass constants: an imported non-portable constant is rejected", "[semantic][const][render-passes]") {
    auto imports = render_pass_imports();
    imports.add("tables", compile_dependency("tables",
                                             "pub struct Pair:\n"
                                             "    a: float\n"
                                             "    b: float\n"
                                             "const:\n"
                                             "    SIZE = 4.0\n"
                                             "    PAIR = Pair(a = 1.0, b = 2.0)\n"));
    auto accepted = analyze("use tables\n" + render_pass_program("", "tables.SIZE"), imports);
    INFO(describe(accepted));
    CHECK(accepted.clean());
    auto rejected = analyze("use tables\n" + render_pass_program("", "tables.PAIR.a"), imports);
    INFO(describe(rejected));
    CHECK(rejected.has("cannot read constant 'tables.PAIR'"));
}

// ── Trait field defaults ────────────────────────────────────────────────────

TEST_CASE("Trait defaults: a default may read a constant", "[semantic][const]") {
    auto analysis = analyze(
        "const:\n"
        "    MOVE_SPEED = 6.0\n"
        "trait Mover:\n"
        "    var speed: float = MOVE_SPEED * 0.5\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Trait defaults: a default may construct a struct", "[semantic][const]") {
    auto analysis = analyze(kUnitDef +
                            "trait Holder:\n"
                            "    var stats: UnitDef = UnitDef(speed = 4.0, health = 3)\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("Trait defaults: a default with a trait read is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "trait Ping:\n"
        "    var x: float = 0.0\n"
        "trait Pong:\n"
        "    var y: float = Ping.x\n");
    INFO(describe(analysis));
    CHECK(analysis.has("a constant cannot read a trait"));
}

TEST_CASE("Trait defaults: a default with a mismatched type is rejected", "[semantic][const]") {
    auto analysis = analyze(
        "trait Counter:\n"
        "    var count: int = 3.14\n");
    INFO(describe(analysis));
    CHECK(analysis.has("default value type 'float' does not match field type 'int'"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
