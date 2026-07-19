// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
//
// Unified name resolution tests (unified-name-resolution change, task 1.7):
// enum member resolution matrix across alias-qualified, canonical-qualified,
// bare, local, unknown-member, and wrong-enum spellings, plus input
// declaration property validation.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace cactus;

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Minimal std.input pub-symbol surface, registered under alias "inp".
/// Canonical-path spellings ("std.input.Key.A") resolve through the unified
/// resolver's canonical-qualifier support without separate registration.
static ModuleImports std_input_imports() {
    ImportedSymbols syms;
    syms.module_name = "std.input";
    const auto add_enum = [&syms](const std::string& name, std::vector<std::string> variants) {
        ResolvedEnum enm;
        enm.name         = name;
        enm.module_name  = "std.input";
        enm.symbol_id    = make_symbol_id(SymbolKind::Enum, "std.input", name);
        enm.canonical_id = make_canonical_id(*enm.symbol_id);
        enm.variants     = std::move(variants);
        syms.enums[name] = std::move(enm);
    };
    add_enum("Key", {"A", "D", "S", "W", "Space", "Shift", "Ctrl", "Alt", "PageUp", "PageDown"});
    add_enum("MouseButton", {"Left", "Right", "Middle"});
    add_enum("GamepadButton", {"South", "North"});
    add_enum("GamepadAxis", {"LeftX", "LeftY"});
    ModuleImports imports;
    imports.add("inp", std::move(syms));
    return imports;
}

struct AnalyzedProgram {
    ProgramNode program;
    DecoratedProgram decorated;
    std::vector<std::string> errors;
};

static AnalyzedProgram analyze_source(const std::string& source, const ModuleImports& imports = ModuleImports{}) {
    AnalyzedProgram out;
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    out.program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    out.decorated = analyzer.analyze(out.program, imports);
    for (const auto& diag : errors.diagnostics()) {
        out.errors.push_back(diag.message);
    }
    return out;
}

static bool any_error_contains(const AnalyzedProgram& analyzed, const std::string& fragment) {
    return std::ranges::any_of(analyzed.errors, [&fragment](const std::string& message) {
        return message.find(fragment) != std::string::npos;
    });
}

/// Find the first input declaration and return the resolved enum member of the
/// prop with the given key, or nullptr.
static const ResolvedEnumMember* first_input_prop_member(const ProgramNode& program, const std::string& prop_key) {
    for (const auto& decl : program.declarations) {
        const auto* input = std::get_if<InputDeclNode>(&decl);
        if (input == nullptr) {
            continue;
        }
        for (const auto& prop : input->props) {
            if (prop.key != prop_key) {
                continue;
            }
            const auto* member = std::get_if<MemberExpr>(&prop.value->expr);
            if (member != nullptr && member->resolved_enum_member.has_value()) {
                return &*member->resolved_enum_member;
            }
            return nullptr;
        }
    }
    return nullptr;
}

// ── Enum member resolution matrix ────────────────────────────────────────────

TEST_CASE("name resolution: alias-qualified enum member resolves with identity", "[resolution][enum-member]") {
    auto analyzed = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input MoveX: axis\n"
        "    negative = inp.Key.A\n"
        "    positive = inp.Key.D\n",
        std_input_imports());
    CHECK(analyzed.errors.empty());
    const auto* member = first_input_prop_member(analyzed.program, "negative");
    REQUIRE(member != nullptr);
    CHECK(member->enum_id.module.name == "std.input");
    CHECK(member->enum_id.local_name == "Key");
    CHECK(member->member == "A");
    CHECK(member->index == 0);
}

TEST_CASE("name resolution: canonical-qualified enum member resolves to same identity",
          "[resolution][enum-member][canonical]") {
    auto analyzed = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input Fire: button\n"
        "    key = std.input.Key.Space\n",
        std_input_imports());
    CHECK(analyzed.errors.empty());
    const auto* member = first_input_prop_member(analyzed.program, "key");
    REQUIRE(member != nullptr);
    CHECK(make_canonical_id(member->enum_id) == "std.input.Key");
    CHECK(member->member == "Space");
}

TEST_CASE("name resolution: bare enum spelling rejected with qualified suggestion",
          "[resolution][enum-member][bare]") {
    auto analyzed = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input Fire: button\n"
        "    key = Key.Space\n",
        std_input_imports());
    REQUIRE_FALSE(analyzed.errors.empty());
    CHECK(any_error_contains(analyzed, "unknown symbol 'Key.Space'"));
    CHECK(any_error_contains(analyzed, "inp.Key.Space"));
}

TEST_CASE("name resolution: unknown enum member rejected", "[resolution][enum-member][unknown]") {
    auto analyzed = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input Fire: button\n"
        "    key = inp.Key.Zzz\n",
        std_input_imports());
    CHECK(any_error_contains(analyzed, "'Zzz' is not a member of enum 'std.input.Key'"));
}

TEST_CASE("name resolution: wrong enum for input property rejected", "[resolution][input][wrong-enum]") {
    auto analyzed = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input Fire: button\n"
        "    key = inp.MouseButton.Left\n",
        std_input_imports());
    CHECK(any_error_contains(analyzed, "input property 'key' requires a std.input.Key member"));
}

TEST_CASE("name resolution: mouse property requires MouseButton", "[resolution][input][wrong-enum]") {
    auto analyzed = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input Fire: button\n"
        "    mouse = inp.Key.Space\n",
        std_input_imports());
    CHECK(any_error_contains(analyzed, "input property 'mouse' requires a std.input.MouseButton member"));
}

TEST_CASE("name resolution: gamepad property enum depends on input kind", "[resolution][input][gamepad]") {
    auto button_ok = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input Jump: button\n"
        "    gamepad = inp.GamepadButton.South\n",
        std_input_imports());
    CHECK(button_ok.errors.empty());

    auto axis_wrong = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input MoveX: axis\n"
        "    gamepad = inp.GamepadButton.South\n",
        std_input_imports());
    CHECK(any_error_contains(axis_wrong, "input property 'gamepad' requires a std.input.GamepadAxis member"));
}

TEST_CASE("name resolution: local enum member resolves bare", "[resolution][enum-member][local]") {
    auto analyzed = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "enum Color:\n"
        "    Red\n"
        "    Green\n"
        "trait Paint:\n"
        "    var c: int\n"
        "system S:\n"
        "    filter:\n"
        "        Paint\n"
        "    on tick:\n"
        "        let chosen = Color.Green\n");
    CHECK(analyzed.errors.empty());
    // Find the let-binding's resolved member inside the system handler.
    const ResolvedEnumMember* member = nullptr;
    for (const auto& decl : analyzed.program.declarations) {
        const auto* sys = std::get_if<SystemNode>(&decl);
        if (sys == nullptr) {
            continue;
        }
        for (const auto& handler : sys->handlers) {
            for (const auto& stmt : handler.body) {
                if (const auto* let_stmt = std::get_if<LetStmt>(&stmt->stmt)) {
                    if (const auto* mem = std::get_if<MemberExpr>(&let_stmt->value->expr)) {
                        if (mem->resolved_enum_member.has_value()) {
                            member = &*mem->resolved_enum_member;
                        }
                    }
                }
            }
        }
    }
    REQUIRE(member != nullptr);
    CHECK(make_canonical_id(member->enum_id) == "test.Color");
    CHECK(member->member == "Green");
    CHECK(member->index == 1);
}

TEST_CASE("name resolution: local enum member with unknown member rejected", "[resolution][enum-member][local]") {
    auto analyzed = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "enum Color:\n"
        "    Red\n"
        "trait Paint:\n"
        "    var c: int\n"
        "system S:\n"
        "    filter:\n"
        "        Paint\n"
        "    on tick:\n"
        "        let chosen = Color.Blue\n");
    CHECK(any_error_contains(analyzed, "'Blue' is not a member of enum 'test.Color'"));
}

TEST_CASE("name resolution: invert input property requires bool", "[resolution][input][invert]") {
    auto ok = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input MoveX: axis\n"
        "    negative = inp.Key.A\n"
        "    invert = true\n",
        std_input_imports());
    CHECK(ok.errors.empty());

    auto bad = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "input MoveX: axis\n"
        "    invert = 3\n",
        std_input_imports());
    CHECK(any_error_contains(bad, "input property 'invert' requires a bool value"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
