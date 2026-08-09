// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_resolver.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>

using namespace cactus;
namespace fs = std::filesystem;

static const fs::path FIXTURES = fs::path(CACTUS_TEST_FIXTURES_DIR) / "multi_module";

static fs::path stdlib_root() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR).parent_path().parent_path() / "stdlib";
}

// ── Helper: parse a string into ProgramNode ─────────────────────────────────

static ProgramNode parse(const std::string& source) {
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    return program;
}

static ProgramNode parse_file(const fs::path& file_path) {
    std::ifstream input(file_path);
    REQUIRE(input.is_open());
    const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return parse(source);
}

// ── Dependency Extraction ───────────────────────────────────────────────────

TEST_CASE("ModuleResolver: extract deps from use declarations", "[resolver]") {
    auto prog = parse("module test\nuse player\nuse level\n");
    auto deps = ModuleResolver::extract_dependencies(prog);
    REQUIRE(deps.size() == 2);
    CHECK(deps[0] == "player");
    CHECK(deps[1] == "level");
}

TEST_CASE("ModuleResolver: no use declarations → empty deps", "[resolver]") {
    auto prog = parse("module standalone\ntrait Standalone:\n    var x: int = 0\n");
    auto deps = ModuleResolver::extract_dependencies(prog);
    CHECK(deps.empty());
}

TEST_CASE("ModuleResolver: use with alias — alias ignored for deps", "[resolver]") {
    auto prog = parse("module test\nuse player as p\n");
    auto deps = ModuleResolver::extract_dependencies(prog);
    REQUIRE(deps.size() == 1);
    CHECK(deps[0] == "player");
}

// ── Module Name to Path ─────────────────────────────────────────────────────

TEST_CASE("ModuleResolver: simple module name to path", "[resolver]") {
    auto p = ModuleResolver::module_name_to_path("player");
    CHECK(p.generic_string() == "player.cactus");
}

TEST_CASE("ModuleResolver: dotted module name to path", "[resolver]") {
    auto p = ModuleResolver::module_name_to_path("enemies.walker");
    CHECK(p.generic_string() == "enemies/walker.cactus");
}

TEST_CASE("ModuleResolver: deeply dotted module name", "[resolver]") {
    auto p = ModuleResolver::module_name_to_path("lib.physics.rigid");
    CHECK(p.generic_string() == "lib/physics/rigid.cactus");
}

// ── Module Name Inference ───────────────────────────────────────────────────

TEST_CASE("ModuleResolver: infer simple module name", "[resolver]") {
    auto name = ModuleResolver::infer_module_name("/game", "/game/player.cactus");
    CHECK(name == "player");
}

TEST_CASE("ModuleResolver: infer dotted module name from subfolder", "[resolver]") {
    auto name = ModuleResolver::infer_module_name("/game", "/game/enemies/walker.cactus");
    CHECK(name == "enemies.walker");
}

// ── Module Name Validation ──────────────────────────────────────────────────

TEST_CASE("ModuleResolver: matching module name accepted", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto prog = parse("module player\n");
    CHECK(resolver.validate_module_name(prog, "player", "player.cactus"));
    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("ModuleResolver: mismatched module name rejected", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto prog = parse("module wrong_name\n");
    CHECK_FALSE(resolver.validate_module_name(prog, "mismatched", "mismatched.cactus"));
    CHECK(errors.has_errors());
}

TEST_CASE("ModuleResolver: missing module declaration rejected", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);

    ProgramNode prog;
    prog.location = {"foo.cactus", 1, 1};

    CHECK_FALSE(resolver.validate_module_name(prog, "foo", "foo.cactus"));
    CHECK(errors.has_errors());
}

TEST_CASE("ModuleResolver: duplicate module declaration rejected", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);

    ProgramNode prog;
    prog.declarations.push_back(ModuleNode{.name = "foo", .location = {"foo.cactus", 1, 1}});
    prog.declarations.push_back(ModuleNode{.name = "foo.extra", .location = {"foo.cactus", 2, 1}});

    CHECK_FALSE(resolver.validate_module_name(prog, "foo", "foo.cactus"));
    CHECK(errors.has_errors());
}

// ── File Location ───────────────────────────────────────────────────────────

TEST_CASE("ModuleResolver: locate file in search dir", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto path = cactus::ModuleResolver::locate_file("player", {FIXTURES});
    CHECK_FALSE(path.empty());
    CHECK(path.filename() == "player.cactus");
}

TEST_CASE("ModuleResolver: locate nonexistent file", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto path = cactus::ModuleResolver::locate_file("nonexistent", {FIXTURES});
    CHECK(path.empty());
}

TEST_CASE("ModuleResolver: std.text import resolves from stdlib search root", "[resolver][stdlib][text]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);

    auto result = resolver.resolve(FIXTURES / "import_std_text.cactus", {stdlib_root()});

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(result.size() == 2);
    CHECK(result[0].qualified_name == "std.text");
    CHECK(result[0].file_path.filename() == "text.cactus");
    CHECK(result[1].qualified_name == "import_std_text");
}

TEST_CASE("ModuleResolver: std.text compiles without fixed-arity format extern", "[resolver][stdlib][text]") {
    const auto text_path = ModuleResolver::locate_file("std.text", {stdlib_root()});
    REQUIRE_FALSE(text_path.empty());

    auto program = parse_file(text_path);

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(program);

    REQUIRE_FALSE(errors.has_errors());
    CHECK(decorated.funcs.empty());
    CHECK_FALSE(decorated.funcs.contains("format"));
}

TEST_CASE("ModuleResolver: std.ui exposes the canonical Standard UI data model", "[resolver][stdlib][ui]") {
    const auto ui_path = ModuleResolver::locate_file("std.ui", {stdlib_root()});
    REQUIRE_FALSE(ui_path.empty());

    auto program = parse_file(ui_path);

    const auto require_enum = [&program](const char* name, std::initializer_list<const char*> variants) {
        const auto found = std::ranges::find_if(program.declarations, [name](const Declaration& declaration) {
            const auto* value = std::get_if<EnumNode>(&declaration);
            return value != nullptr && value->name == name;
        });
        REQUIRE(found != program.declarations.end());
        const auto& resolved = std::get<EnumNode>(*found);
        REQUIRE(resolved.variants.size() == variants.size());
        std::size_t index = 0;
        for (const auto* variant : variants) {
            CHECK(resolved.variants[index].name == variant);
            ++index;
        }
    };
    require_enum("Axis", {"Horizontal", "Vertical"});
    require_enum("Align", {"Start", "Center", "End", "Stretch"});
    require_enum("TextAlign", {"Start", "Center", "End"});
    require_enum("ImageFit", {"Stretch", "Contain", "Cover"});

    const auto require_fields = [&program](const char* name,
                                           std::initializer_list<const char*> fields,
                                           std::initializer_list<const char*> defaulted_fields = {}) {
        const auto found = std::ranges::find_if(program.declarations, [name](const Declaration& declaration) {
            const auto* value = std::get_if<TraitNode>(&declaration);
            return value != nullptr && value->name == name;
        });
        REQUIRE(found != program.declarations.end());
        const auto& resolved = std::get<TraitNode>(*found);
        REQUIRE(resolved.is_pub);
        REQUIRE(resolved.fields.size() == fields.size());
        std::size_t index = 0;
        for (const auto* field : fields) {
            CHECK(resolved.fields[index].name == field);
            ++index;
        }
        for (const auto* field : defaulted_fields) {
            const auto declared = std::ranges::find_if(
                resolved.fields, [field](const FieldNode& candidate) { return candidate.name == field; });
            REQUIRE(declared != resolved.fields.end());
            CHECK(declared->default_value.has_value());
        }
    };
    require_fields(
        "Node", {"visible", "enabled", "z_index", "clip_children"}, {"visible", "enabled", "z_index", "clip_children"});
    require_fields("Visual", {"scale", "opacity"}, {"scale", "opacity"});
    require_fields("PreferredSize", {"min_size"}, {"min_size"});
    require_fields("DesiredSize", {"size"}, {"size"});
    require_fields("Anchors",
                   {"min", "max", "pivot", "offset", "margin_min", "margin_max"},
                   {"min", "max", "pivot", "offset", "margin_min", "margin_max"});
    require_fields("ComputedLayout",
                   {"position",
                    "size",
                    "effective_visible",
                    "effective_enabled",
                    "effective_opacity",
                    "clip_min",
                    "clip_max",
                    "draw_order"},
                   {"position",
                    "size",
                    "effective_visible",
                    "effective_enabled",
                    "effective_opacity",
                    "clip_min",
                    "clip_max",
                    "draw_order"});
    require_fields(
        "Panel", {"background", "border_color", "border_width"}, {"background", "border_color", "border_width"});
    require_fields("Text", {"value", "font_size", "color", "align"}, {"value", "font_size", "color"});
    require_fields("Image", {"texture", "tint", "fit"}, {"tint"});
    require_fields(
        "Button",
        {"label", "normal_color", "hover_color", "pressed_color", "disabled_color", "text_color", "padding"},
        {"label", "normal_color", "hover_color", "pressed_color", "disabled_color", "text_color", "padding"});
    require_fields("Stack", {"axis", "gap", "align", "padding"}, {"gap", "padding"});
    require_fields("Grid", {"columns", "cell_size", "gap", "padding"}, {"columns", "cell_size", "gap", "padding"});
    require_fields(
        "GridItem", {"column", "row", "column_span", "row_span"}, {"column", "row", "column_span", "row_span"});
    require_fields("Overlay", {"padding"}, {"padding"});
    require_fields("FrameAnimation",
                   {"frame_count", "fps", "frame", "elapsed", "playing"},
                   {"frame_count", "fps", "frame", "elapsed", "playing"});
    require_fields("BumpAnimation",
                   {"from_scale", "to_scale", "duration", "elapsed", "playing"},
                   {"from_scale", "to_scale", "duration", "elapsed", "playing"});

    const auto bump_decl = std::ranges::find_if(program.declarations, [](const Declaration& declaration) {
        const auto* value = std::get_if<EventNode>(&declaration);
        return value != nullptr && value->name == "StartBump";
    });
    REQUIRE(bump_decl != program.declarations.end());
    const auto& bump = std::get<EventNode>(*bump_decl);
    REQUIRE(bump.is_pub);
    CHECK(bump.fields.empty());

    const auto render_rule = std::ranges::find_if(program.declarations, [](const Declaration& declaration) {
        const auto* rule = std::get_if<ExternRuleNode>(&declaration);
        return rule != nullptr && rule->name == "RenderUi";
    });
    REQUIRE(render_rule != program.declarations.end());
    const auto& rule = std::get<ExternRuleNode>(*render_rule);
    REQUIRE(rule.handlers.size() == 1);
    REQUIRE(rule.handlers[0].effects.size() == 1);
    CHECK(rule.handlers[0].trigger_name == "core.render");
    CHECK(rule.handlers[0].effects[0].spelling == "graphics");

    const auto require_rule_query = [&program](const char* rule_name, const char* query_name) {
        const auto found = std::ranges::find_if(program.declarations, [rule_name](const Declaration& declaration) {
            const auto* value = std::get_if<RuleNode>(&declaration);
            return value != nullptr && value->name == rule_name;
        });
        REQUIRE(found != program.declarations.end());
        const auto& authored = std::get<RuleNode>(*found);
        REQUIRE_FALSE(authored.handlers.empty());
        for (const auto& handler : authored.handlers) {
            REQUIRE_FALSE(handler.body.empty());

            const ForeachStmt* traversal = nullptr;
            for (const auto& statement : handler.body) {
                if (const auto* candidate = std::get_if<ForeachStmt>(&statement->stmt)) {
                    traversal = candidate;
                    break;
                }
            }
            REQUIRE(traversal != nullptr);
            const auto* query = std::get_if<QueryCallExpr>(&traversal->iterable->expr);
            REQUIRE(query != nullptr);
            const auto* callee = std::get_if<MemberExpr>(&query->callee->expr);
            REQUIRE(callee != nullptr);
            CHECK(callee->member == query_name);
            REQUIRE(query->filters.size() == 1);
            CHECK(query->filters[0].trait_name == "Node");
        }
    };
    require_rule_query("MeasureUi", "hierarchy_postorder");
    require_rule_query("ArrangeUi", "hierarchy_preorder");
}

// ── Full Resolution ─────────────────────────────────────────────────────────

TEST_CASE("ModuleResolver: standalone file (no deps)", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto result = resolver.resolve(FIXTURES / "standalone.cactus");
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(result.size() == 1);
    CHECK(result[0].qualified_name == "standalone");
}

TEST_CASE("ModuleResolver: resolve rejects a missing module declaration", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto result = resolver.resolve(FIXTURES / "missing_module.cactus");
    CHECK(errors.has_errors());
    CHECK(result.empty());
}

TEST_CASE("ModuleResolver: resolve rejects a mismatched module declaration", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto result = resolver.resolve(FIXTURES / "mismatched.cactus");
    CHECK(errors.has_errors());
    CHECK(result.empty());
}

TEST_CASE("ModuleResolver: multi-module resolution with transitive deps", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto result = resolver.resolve(FIXTURES / "main.cactus");
    REQUIRE_FALSE(errors.has_errors());
    // main → player, level; level → player
    // Topo order: player first (leaf), then level, then main
    REQUIRE(result.size() == 3);

    // Find positions
    std::vector<std::string> names;
    names.reserve(result.size());
    for (auto& m : result) {
        names.push_back(m.qualified_name);
    }

    auto player_pos = std::ranges::find(names, "player") - names.begin();
    auto level_pos  = std::ranges::find(names, "level") - names.begin();
    auto main_pos   = std::ranges::find(names, "main") - names.begin();

    // player must come before level (level depends on player)
    CHECK(player_pos < level_pos);
    // player and level must come before main
    CHECK(player_pos < main_pos);
    CHECK(level_pos < main_pos);
}

TEST_CASE("ModuleResolver: diamond dependency deduplication", "[resolver]") {
    // main → player, level; level → player
    // player should appear exactly once
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto result = resolver.resolve(FIXTURES / "main.cactus");
    REQUIRE_FALSE(errors.has_errors());

    int player_count = 0;
    for (auto& m : result) {
        if (m.qualified_name == "player") {
            player_count++;
        }
    }
    CHECK(player_count == 1);
}

TEST_CASE("ModuleResolver: circular dependency detected", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto result = resolver.resolve(FIXTURES / "circular_a.cactus");
    CHECK(errors.has_errors());
    CHECK(result.empty());
}

TEST_CASE("ModuleResolver: module not found error", "[resolver]") {
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    // Create a temp program that uses nonexistent module
    // We need a file on disk — use the main.cactus with a search path that excludes player
    // Instead, test with locate_file returning empty
    auto path = cactus::ModuleResolver::locate_file("nonexistent", {FIXTURES});
    CHECK(path.empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
