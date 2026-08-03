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

// ── Post-analysis resolution invariant (task 4.2) ────────────────────────────
// After an error-free analysis, every trait/template reference consumed by
// linking/codegen must carry a resolved SymbolId. Exemptions:
//  - event references (`on`/`emit`): the grammar has no qualified event
//    spelling, so cross-module events are validated and consumed by name;
//  - the builtin hierarchical trait `Parent`, which has no declaration.

static void collect_unresolved_expr(const ExprNode& expr, std::vector<std::string>& out);
static void collect_unresolved_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                     std::vector<std::string>& out);

static void check_trait_entry(const ArchetypeTraitEntry& entry, std::vector<std::string>& out) {
    if (entry.trait_name != "Parent" && !entry.resolved_trait_id.has_value()) {
        out.push_back("trait entry '" + entry.trait_name + "'");
    }
}

static void collect_unresolved_child_overrides(const std::vector<ChildOverrideNode>& overrides,
                                               std::vector<std::string>& out) {
    for (const auto& override_node : overrides) {
        for (const auto& entry : override_node.traits) {
            check_trait_entry(entry, out);
            for (const auto& assign : entry.assignments) {
                collect_unresolved_expr(*assign.value, out);
            }
        }
        collect_unresolved_child_overrides(override_node.children, out);
    }
}

static void collect_unresolved_children(const std::vector<ChildArchetypeNode>& children,
                                        std::vector<std::string>& out) {
    for (const auto& child : children) {
        if (child.template_ref.has_value() && !child.resolved_template_ref_id.has_value()) {
            out.push_back("child template ref '" + *child.template_ref + "'");
        }
        for (const auto& use : child.template_uses) {
            if (!use.resolved_template_id.has_value()) {
                out.push_back("child template use '" + use.template_name + "'");
            }
        }
        for (const auto& entry : child.traits) {
            check_trait_entry(entry, out);
        }
        collect_unresolved_children(child.children, out);
        collect_unresolved_child_overrides(child.child_overrides, out);
    }
}

static void check_filter_clause(const FilterClause& clause, const char* kind, std::vector<std::string>& out) {
    for (const auto& entry : clause.entries) {
        if (!entry.resolved_trait_id.has_value()) {
            out.push_back(std::string(kind) + " entry '" + entry.qualified_name + "'");
        }
    }
}

static void collect_unresolved_expr(const ExprNode& expr, std::vector<std::string>& out) {
    std::visit(
        [&out](const auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, UnaryExpr>) {
                collect_unresolved_expr(*e.operand, out);
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                collect_unresolved_expr(*e.left, out);
                collect_unresolved_expr(*e.right, out);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                collect_unresolved_expr(*e.callee, out);
                for (const auto& arg : e.args) {
                    collect_unresolved_expr(*arg, out);
                }
            } else if constexpr (std::is_same_v<E, LambdaExpr>) {
                collect_unresolved_expr(*e.body, out);
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                collect_unresolved_expr(*e.subject, out);
                for (const auto& arm : e.arms) {
                    collect_unresolved_expr(*arm.pattern, out);
                    collect_unresolved_expr(*arm.body, out);
                }
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                collect_unresolved_expr(*e.condition, out);
                collect_unresolved_expr(*e.then_expr, out);
                collect_unresolved_expr(*e.else_expr, out);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                for (const auto& element : e.elements) {
                    collect_unresolved_expr(*element, out);
                }
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                if (!e.resolved_template_id.has_value()) {
                    out.push_back("spawn expr template '" + e.template_name + "'");
                }
                for (const auto& override_entry : e.overrides) {
                    check_trait_entry(override_entry, out);
                }
                collect_unresolved_child_overrides(e.child_overrides, out);
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                for (const auto& pred : e.filters) {
                    if (!pred.resolved_trait_id.has_value()) {
                        out.push_back("query filter trait '" + pred.trait_name + "'");
                    }
                }
                for (const auto& arg : e.named_args) {
                    collect_unresolved_expr(*arg.value, out);
                }
            }
        },
        expr.expr);
}

static void collect_unresolved_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                     std::vector<std::string>& out) {
    for (const auto& stmt : stmts) {
        std::visit(
            [&out](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, LetStmt> || std::is_same_v<S, VarAssign>) {
                    collect_unresolved_expr(*s.value, out);
                } else if constexpr (std::is_same_v<S, ExprStmt>) {
                    collect_unresolved_expr(*s.expr, out);
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    collect_unresolved_expr(*s.condition, out);
                    collect_unresolved_stmts(s.then_body, out);
                    collect_unresolved_stmts(s.else_body, out);
                } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                    collect_unresolved_expr(*s.iterable, out);
                    collect_unresolved_stmts(s.body, out);
                } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                    if (!s.resolved_template_id.has_value()) {
                        out.push_back("spawn template '" + s.template_name + "'");
                    }
                    for (const auto& override_entry : s.overrides) {
                        check_trait_entry(override_entry, out);
                    }
                    collect_unresolved_child_overrides(s.child_overrides, out);
                } else if constexpr (std::is_same_v<S, AddTraitStmt> || std::is_same_v<S, RemoveTraitStmt> ||
                                     std::is_same_v<S, ProjectTraitStmt>) {
                    if (s.trait_name != "Parent" && !s.resolved_trait_id.has_value()) {
                        out.push_back("trait stmt '" + s.trait_name + "'");
                    }
                } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                    collect_unresolved_expr(*s.subject, out);
                    for (const auto& arm : s.arms) {
                        if (!arm.resolved_trait_id.has_value()) {
                            out.push_back("match arm trait '" + arm.trait_name + "'");
                        }
                        collect_unresolved_stmts(arm.body, out);
                    }
                    if (s.wildcard.has_value()) {
                        collect_unresolved_stmts(s.wildcard->body, out);
                    }
                }
            },
            stmt->stmt);
    }
}

/// All codegen-consumed references lacking a resolved SymbolId (task 4.2).
static std::vector<std::string> unresolved_references(const ProgramNode& program) {
    std::vector<std::string> out;
    for (const auto& decl : program.declarations) {
        std::visit(
            [&out](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, RuleNode>) {
                    check_filter_clause(node.filter, "filter", out);
                    check_filter_clause(node.exclude, "exclude", out);
                    for (const auto& handler : node.handlers) {
                        collect_unresolved_stmts(handler.body, out);
                    }
                } else if constexpr (std::is_same_v<T, ExternRuleNode>) {
                    check_filter_clause(node.filter, "filter", out);
                    check_filter_clause(node.exclude, "exclude", out);
                } else if constexpr (std::is_same_v<T, TemplateNode> || std::is_same_v<T, EntityNode>) {
                    for (const auto& entry : node.traits) {
                        check_trait_entry(entry, out);
                    }
                    for (const auto& use : node.template_uses) {
                        if (!use.resolved_template_id.has_value()) {
                            out.push_back("template use '" + use.template_name + "'");
                        }
                    }
                    collect_unresolved_children(node.children, out);
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    collect_unresolved_stmts(node.body, out);
                }
            },
            decl);
    }
    return out;
}

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
    // Task 4.2 invariant: error-free analysis leaves no codegen-consumed
    // reference without a resolved SymbolId.
    if (out.errors.empty()) {
        CHECK(unresolved_references(out.program) == std::vector<std::string>{});
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
        "rule S:\n"
        "    filter:\n"
        "        Paint\n"
        "    on tick:\n"
        "        let chosen = Color.Green\n");
    CHECK(analyzed.errors.empty());
    // Find the let-binding's resolved member inside the rule handler.
    const ResolvedEnumMember* member = nullptr;
    for (const auto& decl : analyzed.program.declarations) {
        const auto* sys = std::get_if<RuleNode>(&decl);
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
        "rule S:\n"
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

// ── M2 — expression uniformity (tasks 2.2 / 2.3) ────────────────────────────

/// Minimal std.math pub-func surface, registered under alias "m".
static ModuleImports std_math_imports() {
    ImportedSymbols syms;
    syms.module_name = "std.math";
    ResolvedFunc fn;
    fn.name          = "clamp";
    fn.module_name   = "std.math";
    fn.symbol_id     = make_symbol_id(SymbolKind::Func, "std.math", "clamp");
    fn.canonical_id  = make_canonical_id(*fn.symbol_id);
    syms.funcs["clamp"] = std::move(fn);
    ModuleImports imports;
    imports.add("m", std::move(syms));
    return imports;
}

/// Collect the value expressions of let-statements in the first rule handler.
static std::vector<const ExprNode*> handler_let_values(const ProgramNode& program) {
    std::vector<const ExprNode*> values;
    for (const auto& decl : program.declarations) {
        const auto* sys = std::get_if<RuleNode>(&decl);
        if (sys == nullptr) {
            continue;
        }
        for (const auto& handler : sys->handlers) {
            for (const auto& stmt : handler.body) {
                if (const auto* let_stmt = std::get_if<LetStmt>(&stmt->stmt)) {
                    values.push_back(let_stmt->value.get());
                }
            }
        }
    }
    return values;
}

TEST_CASE("name resolution: callee resolves identically via alias and canonical path",
          "[resolution][callee][canonical]") {
    auto analyzed = analyze_source(
        "module test\n"
        "use std.math as m\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Paint:\n"
        "    var c: float\n"
        "rule S:\n"
        "    filter:\n"
        "        Paint\n"
        "    on tick:\n"
        "        let a = m.clamp(c, 0.0, 1.0)\n"
        "        let b = std.math.clamp(c, 0.0, 1.0)\n",
        std_math_imports());
    CHECK(analyzed.errors.empty());
    auto lets = handler_let_values(analyzed.program);
    REQUIRE(lets.size() == 2);
    for (const auto* value : lets) {
        const auto* call = std::get_if<CallExpr>(&value->expr);
        REQUIRE(call != nullptr);
        REQUIRE(call->resolved_callee_id.has_value());
        CHECK(make_canonical_id(*call->resolved_callee_id) == "std.math.clamp");
    }
}

TEST_CASE("name resolution: enum member resolves in general expression positions",
          "[resolution][enum-member][expression]") {
    auto analyzed = analyze_source(
        "module test\n"
        "use std.input as inp\n"
        "pub event tick:\n"
        "    dt: float\n"
        "enum Color:\n"
        "    Red\n"
        "    Green\n"
        "trait Paint:\n"
        "    var c: int\n"
        "rule S:\n"
        "    filter:\n"
        "        Paint\n"
        "    on tick:\n"
        "        let chosen = Color.Green\n"
        "        let is_green = chosen == Color.Green\n"
        "        let key = inp.Key.Space\n",
        std_input_imports());
    CHECK(analyzed.errors.empty());
    auto lets = handler_let_values(analyzed.program);
    REQUIRE(lets.size() == 3);
    const auto* comparison = std::get_if<BinaryExpr>(&lets[1]->expr);
    REQUIRE(comparison != nullptr);
    const auto* right = std::get_if<MemberExpr>(&comparison->right->expr);
    REQUIRE(right != nullptr);
    REQUIRE(right->resolved_enum_member.has_value());
    CHECK(make_canonical_id(right->resolved_enum_member->enum_id) == "test.Color");
    const auto* key = std::get_if<MemberExpr>(&lets[2]->expr);
    REQUIRE(key != nullptr);
    REQUIRE(key->resolved_enum_member.has_value());
    CHECK(make_canonical_id(key->resolved_enum_member->enum_id) == "std.input.Key");
}

TEST_CASE("name resolution: enum typing flows into add-arg type checks", "[resolution][enum-member][typing]") {
    auto mismatch = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "enum Color:\n"
        "    Red\n"
        "    Green\n"
        "trait Paint:\n"
        "    var c: int\n"
        "trait Tint:\n"
        "    var shade: int\n"
        "rule S:\n"
        "    filter:\n"
        "        Paint\n"
        "    on tick:\n"
        "        add Tint:\n"
        "            shade = Color.Green\n");
    CHECK(any_error_contains(mismatch, "type mismatch for field 'shade'"));

    auto ok = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "enum Color:\n"
        "    Red\n"
        "    Green\n"
        "trait Paint:\n"
        "    var c: int\n"
        "trait Tint:\n"
        "    var shade: Color\n"
        "rule S:\n"
        "    filter:\n"
        "        Paint\n"
        "    on tick:\n"
        "        add Tint:\n"
        "            shade = Color.Green\n");
    CHECK(ok.errors.empty());
}

TEST_CASE("name resolution: match expression over enum member subject keeps working",
          "[resolution][enum-member][match]") {
    auto analyzed = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "enum Color:\n"
        "    Red\n"
        "    Green\n"
        "trait Paint:\n"
        "    var c: int\n"
        "rule S:\n"
        "    filter:\n"
        "        Paint\n"
        "    on tick:\n"
        "        let chosen = Color.Green\n"
        "        let v = match chosen:\n"
        "            Color.Red => 0\n"
        "            Color.Green => 1\n");
    CHECK(analyzed.errors.empty());
    auto lets = handler_let_values(analyzed.program);
    REQUIRE(lets.size() == 2);
    const auto* match = std::get_if<MatchExpr>(&lets[1]->expr);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);
    for (const auto& arm : match->arms) {
        const auto* pattern = std::get_if<MemberExpr>(&arm.pattern->expr);
        REQUIRE(pattern != nullptr);
        CHECK(pattern->resolved_enum_member.has_value());
    }
}

// ── M3 — canonical qualifiers across all reference forms (task 3.3) ─────────

/// Module "game.stuff" (traits, struct, event, rule, template) under alias
/// "gs"; canonical-path spellings must resolve identically to the alias.
static ModuleImports game_stuff_imports() {
    ImportedSymbols syms;
    syms.module_name = "game.stuff";

    const auto add_trait = [&syms](const std::string& name, bool with_field) {
        ResolvedTrait rt;
        rt.name         = name;
        rt.is_pub       = true;
        rt.module_name  = "game.stuff";
        rt.symbol_id    = make_symbol_id(SymbolKind::Trait, "game.stuff", name);
        rt.canonical_id = make_canonical_id(*rt.symbol_id);
        if (with_field) {
            ResolvedField field;
            field.name        = "vx";
            field.type.kind   = TypeKind::Float;
            field.type.name   = "float";
            field.is_var      = true;
            field.has_default = true;
            rt.fields.push_back(std::move(field));
        }
        syms.traits[name] = std::move(rt);
    };
    add_trait("Velocity", true);
    add_trait("Frozen", false);

    ResolvedStruct vec;
    vec.name            = "Vec";
    vec.module_name     = "game.stuff";
    vec.symbol_id       = make_symbol_id(SymbolKind::Struct, "game.stuff", "Vec");
    vec.canonical_id    = make_canonical_id(*vec.symbol_id);
    syms.structs["Vec"] = std::move(vec);

    ImportedRule move;
    move.name             = "Move";
    move.module_name      = "game.stuff";
    move.symbol_id        = make_symbol_id(SymbolKind::Rule, "game.stuff", "Move");
    move.canonical_id     = make_canonical_id(*move.symbol_id);
    syms.rules["Move"]  = std::move(move);

    ImportedTemplate rock;
    rock.name               = "Rock";
    rock.module_name        = "game.stuff";
    rock.symbol_id          = make_symbol_id(SymbolKind::Template, "game.stuff", "Rock");
    rock.canonical_id       = make_canonical_id(*rock.symbol_id);
    syms.templates["Rock"]  = std::move(rock);

    ModuleImports imports;
    imports.add("gs", std::move(syms));
    return imports;
}

/// One source referencing every qualifier-capable reference form through the
/// given spelling. (Event references — `on`/`emit` — take bare identifiers
/// only in the grammar, so they have no qualified spelling to compare; their
/// resolver is the same resolve_name wrapper the other forms use.)
static std::string all_reference_forms_source(const std::string& q) {
    return "module test\n"
           "use game.stuff as gs\n"
           "pub event tick:\n"
           "    dt: float\n"
           "trait Wearing:\n"
           "    var p: " + q + ".Vec\n"
           "rule S:\n"
           "    filter:\n"
           "        " + q + ".Velocity\n"
           "    exclude:\n"
           "        " + q + ".Frozen\n"
           "    after:\n"
           "        " + q + ".Move\n"
           "    on tick:\n"
           "        spawn " + q + ".Rock:\n"
           "            " + q + ".Velocity:\n"
           "                vx = 1.0\n";
}

struct ReferenceFormIds {
    std::string filter_trait;
    std::string exclude_trait;
    std::string spawn_template;
    std::string spawn_override_trait;
    std::string field_type;
};

static ReferenceFormIds collect_reference_ids(const AnalyzedProgram& analyzed) {
    ReferenceFormIds ids;
    for (const auto& decl : analyzed.program.declarations) {
        if (const auto* sys = std::get_if<RuleNode>(&decl)) {
            REQUIRE(sys->filter.entries.size() == 1);
            REQUIRE(sys->filter.entries[0].resolved_trait_id.has_value());
            ids.filter_trait = make_canonical_id(*sys->filter.entries[0].resolved_trait_id);
            REQUIRE(sys->exclude.entries.size() == 1);
            REQUIRE(sys->exclude.entries[0].resolved_trait_id.has_value());
            ids.exclude_trait = make_canonical_id(*sys->exclude.entries[0].resolved_trait_id);
            for (const auto& handler : sys->handlers) {
                for (const auto& stmt : handler.body) {
                    if (const auto* spawn = std::get_if<SpawnStmt>(&stmt->stmt)) {
                        REQUIRE(spawn->resolved_template_id.has_value());
                        ids.spawn_template = make_canonical_id(*spawn->resolved_template_id);
                        REQUIRE(spawn->overrides.size() == 1);
                        REQUIRE(spawn->overrides[0].resolved_trait_id.has_value());
                        ids.spawn_override_trait = make_canonical_id(*spawn->overrides[0].resolved_trait_id);
                    }
                }
            }
        }
    }
    const auto trait_it = analyzed.decorated.traits.find("Wearing");
    REQUIRE(trait_it != analyzed.decorated.traits.end());
    REQUIRE(trait_it->second.fields.size() == 1);
    REQUIRE(trait_it->second.fields[0].type.symbol_id.has_value());
    ids.field_type = make_canonical_id(*trait_it->second.fields[0].type.symbol_id);
    return ids;
}

TEST_CASE("name resolution: alias and canonical qualifiers equivalent across reference forms",
          "[resolution][canonical][all-forms]") {
    auto aliased   = analyze_source(all_reference_forms_source("gs"), game_stuff_imports());
    auto canonical = analyze_source(all_reference_forms_source("game.stuff"), game_stuff_imports());
    CHECK(aliased.errors.empty());
    CHECK(canonical.errors.empty());

    auto alias_ids     = collect_reference_ids(aliased);
    auto canonical_ids = collect_reference_ids(canonical);
    CHECK(alias_ids.filter_trait == "game.stuff.Velocity");
    CHECK(alias_ids.exclude_trait == "game.stuff.Frozen");
    CHECK(alias_ids.spawn_template == "game.stuff.Rock");
    CHECK(alias_ids.spawn_override_trait == "game.stuff.Velocity");
    CHECK(alias_ids.field_type == "game.stuff.Vec");
    CHECK(canonical_ids.filter_trait == alias_ids.filter_trait);
    CHECK(canonical_ids.exclude_trait == alias_ids.exclude_trait);
    CHECK(canonical_ids.spawn_template == alias_ids.spawn_template);
    CHECK(canonical_ids.spawn_override_trait == alias_ids.spawn_override_trait);
    CHECK(canonical_ids.field_type == alias_ids.field_type);
}

// ── M4 — post-analysis resolution invariant (tasks 4.1 / 4.2) ───────────────

TEST_CASE("name resolution: dynamic-ECS references all carry resolved symbols after clean analysis",
          "[resolution][invariant]") {
    // analyze_source itself asserts the task 4.2 invariant on error-free
    // analyses; this program exercises the decl-level forms task 4.1 flips
    // to required mode: filter entries, spawn templates/overrides, add/remove
    // traits, and trait-match arms.
    auto analyzed = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "trait Frozen:\n"
        "    var f: int\n"
        "template Rock:\n"
        "    Pos:\n"
        "        x = 0.0\n"
        "rule S:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        add Frozen:\n"
        "            f = 1\n"
        "        remove Frozen\n"
        "        match self:\n"
        "            Frozen as fz =>\n"
        "                x = 1.0\n"
        "        spawn Rock:\n"
        "            Pos:\n"
        "                x = 2.0\n");
    CHECK(analyzed.errors.empty());
}

TEST_CASE("name resolution: unresolvable decl-level references are diagnostics, not silent",
          "[resolution][invariant][required]") {
    auto bad_filter = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "rule S:\n"
        "    filter:\n"
        "        Ghost\n"
        "    on tick:\n"
        "        x = 1.0\n");
    CHECK(any_error_contains(bad_filter, "unknown trait 'Ghost' in filter"));

    auto bad_spawn = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "rule S:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        spawn Missing:\n"
        "            Pos:\n"
        "                x = 1.0\n");
    CHECK(any_error_contains(bad_spawn, "undefined template 'Missing'"));

    auto bad_emit = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "rule S:\n"
        "    filter:\n"
        "        Pos\n"
        "    on tick:\n"
        "        emit Nothing:\n"
        "            a = 1\n");
    CHECK(any_error_contains(bad_emit, "undeclared event 'Nothing'"));

    auto bad_after = analyze_source(
        "module test\n"
        "pub event tick:\n"
        "    dt: float\n"
        "trait Pos:\n"
        "    var x: float\n"
        "rule S:\n"
        "    filter:\n"
        "        Pos\n"
        "    after:\n"
        "        NoSuchSystem\n"
        "    on tick:\n"
        "        x = 1.0\n");
    CHECK(any_error_contains(bad_after, "unknown rule 'NoSuchSystem' in after clause"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
