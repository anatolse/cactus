#include "backends/cpp-entt/system_emitter.hpp"

#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

namespace {

bool symbol_is(const SymbolId& symbol, SymbolKind kind, std::string_view module_name, std::string_view local_name) {
    return symbol.kind == kind && symbol.module.name == module_name && symbol.local_name == local_name;
}

bool symbol_is(const std::optional<SymbolId>& symbol,
               SymbolKind kind,
               std::string_view module_name,
               std::string_view local_name) {
    return symbol.has_value() && symbol_is(*symbol, kind, module_name, local_name);
}

const ResolvedFunc* find_resolved_func(const DecoratedProgram& program, const SymbolId& symbol) {
    const auto canonical = make_canonical_id(symbol);
    if (const auto found = program.funcs.find(canonical); found != program.funcs.end()) {
        return &found->second;
    }
    for (const auto& [_, func] : program.funcs) {
        if ((func.symbol_id.has_value() && *func.symbol_id == symbol) || func.canonical_id == canonical) {
            return &func;
        }
    }
    return nullptr;
}

std::string stdlib_runtime_prefix(const SymbolId& func_id) {
    if (func_id.kind != SymbolKind::Func) {
        return {};
    }
    const std::string& module_name = func_id.module.name;
    if (module_name == "std.math") {
        return "cactus::runtime::stdlib::math";
    }
    if (module_name == "std.math.vec2") {
        return "cactus::runtime::stdlib::math::vec2";
    }
    if (module_name == "std.math.vec3") {
        return "cactus::runtime::stdlib::math::vec3";
    }
    if (module_name == "std.math.quat") {
        return "cactus::runtime::stdlib::math::quat";
    }
    if (module_name == "std.input") {
        return "cactus::runtime::entt_backend";
    }
    if (module_name == "std.physics.flat") {
        return "::";
    }
    if (module_name == "std.editor") {
        return "cactus::runtime::entt_backend";
    }
    if (module_name == "std.camera.flat") {
        return "cactus::runtime::entt_backend";
    }
    if (module_name == "std.camera.volume") {
        return "cactus::runtime::entt_backend";
    }
    if (module_name == "std.transform.flat") {
        return "cactus::runtime::entt_backend";
    }
    if (module_name == "std.transform.volume") {
        return "cactus::runtime::entt_backend";
    }
    if (module_name == "std.random") {
        return "cactus::runtime::stdlib::random";
    }
    if (module_name == "std.render.models") {
        return "cactus::runtime::entt_backend";
    }
    if (module_name == "std.pointer") {
        return "cactus::runtime::entt_backend";
    }
    return {};
}

// Per-function codegen metadata for a stdlib call: the runtime symbol to call
// (defaults to the DSL name when unset) and whether the call needs `registry`
// injected as its first C++ argument. Both facts live on the same entry so
// adding a new stdlib function only means touching one place, not two
// separately-maintained tables that can silently drift out of sync.
struct StdlibFuncInfo {
    std::string runtime_name;
    bool needs_registry = false;
};
using FuncRenameMap = std::unordered_map<std::string, StdlibFuncInfo>;
const std::unordered_map<std::string, FuncRenameMap> kRuntimeFuncNames = {
    {"std.render.models",
     {{"animation_count", {.runtime_name = "model_animation_count"}},
      {"animation_name", {.runtime_name = "model_animation_name"}},
      {"bounds_size", {.runtime_name = "model_bounds_size"}},
      {"bounds_center", {.runtime_name = "model_bounds_center"}}}},
    {"std.editor",
     {{"spawn_template", {.runtime_name = "editor_spawn_template", .needs_registry = true}},
      {"hit_test_2d", {.runtime_name = "editor_hit_test_2d", .needs_registry = true}},
      {"raycast_3d", {.runtime_name = "editor_raycast_3d", .needs_registry = true}},
      {"camera_enter", {.runtime_name = "editor_camera_enter", .needs_registry = true}},
      {"camera_exit", {.runtime_name = "editor_camera_exit", .needs_registry = true}},
      {"apply_camera_2d", {.runtime_name = "editor_apply_camera_2d", .needs_registry = true}},
      {"apply_camera_3d", {.runtime_name = "editor_apply_camera_3d", .needs_registry = true}},
      {"active_mode", {.runtime_name = "editor_active_mode", .needs_registry = true}},
      {"is_editor_active", {.runtime_name = "editor_is_active", .needs_registry = true}},
      {"template_names", {.runtime_name = "editor_template_names"}},
      {"template_index", {.runtime_name = "editor_template_index"}},
      {"screen_size", {.runtime_name = "editor_screen_size"}},
      {"palette_label_slot", {.runtime_name = "editor_palette_label_slot", .needs_registry = true}},
      {"palette_color", {.runtime_name = "editor_palette_color"}},
      {"mode_label", {.runtime_name = "editor_mode_label"}},
      {"palette_button_y", {.runtime_name = "editor_palette_button_y"}}}},
    {"std.input",
     {{"mouse_delta", {.runtime_name = "editor_mouse_delta_screen"}},
      {"wheel_delta", {.runtime_name = "editor_wheel_delta"}},
      {"consume", {.runtime_name = "editor_consume"}}}},
    {"std.camera.flat",
     {{"screen_to_world", {.runtime_name = "editor_screen_to_world_2d"}},
      {"screen_delta_to_world", {.runtime_name = "screen_delta_to_world_2d"}}}},
    {"std.camera.volume",
     {{"screen_to_plane", {.runtime_name = "editor_plane_project_3d"}},
      {"screen_delta_on_plane", {.runtime_name = "screen_delta_on_plane_3d"}}}},
    {"std.transform.flat", {{"world_position", {.runtime_name = "editor_entity_position_2d", .needs_registry = true}}}},
    {"std.transform.volume",
     {{"world_position", {.runtime_name = "editor_entity_position_3d", .needs_registry = true}}}},
    {"std.pointer", {{"top_target", {.runtime_name = "pointer_top_target", .needs_registry = true}}}},
};

// std.ui mixes a handful of backend-owned metric facts (window_size/text_size/
// texture_size — platform state per design.md decision #10) with ordinary
// authored `func` helpers (nonnegative, max_size, ...) in the same module, so
// it cannot use the blanket per-module prefix above: that would route every
// authored helper call through a nonexistent cactus::runtime::entt_backend
// symbol of the same name. Name the three metric facts explicitly instead.
const std::unordered_map<std::string, std::string> kUiMetricFuncNames = {
    {"window_size", "ui_window_size"},
    {"text_size", "ui_text_size"},
    {"texture_size", "ui_texture_size"},
};

bool is_stdlib_ui_metric_func(const SymbolId& func_id) {
    return func_id.kind == SymbolKind::Func && func_id.module.name == "std.ui" &&
           kUiMetricFuncNames.contains(func_id.local_name);
}

std::string stdlib_ui_metric_func_call(const SymbolId& func_id,
                                       const std::vector<std::unique_ptr<ExprNode>>& args,
                                       const auto& emit_arg) {
    std::string result = "cactus::runtime::entt_backend::" + kUiMetricFuncNames.at(func_id.local_name) + "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += emit_arg(*args[i]);
    }
    result += ")";
    return result;
}

std::string stdlib_runtime_func_name(const std::string& module_name, const std::string& func_name) {
    const auto module_it = kRuntimeFuncNames.find(module_name);
    if (module_it != kRuntimeFuncNames.end()) {
        const auto func_it = module_it->second.find(func_name);
        if (func_it != module_it->second.end()) {
            return func_it->second.runtime_name;
        }
    }
    return func_name;
}

bool is_stdlib_physics_flat_query(const std::string& func_name) {
    return func_name == "query_cast_nearest" || func_name == "query_overlap_deepest" ||
           func_name == "query_overlap_all";
}

bool is_stdlib_physics_flat_query(const SymbolId& func_id) {
    return symbol_is(func_id, SymbolKind::Func, "std.physics.flat", func_id.local_name) &&
           is_stdlib_physics_flat_query(func_id.local_name);
}

std::string stdlib_physics_flat_query_call(const std::string& func_name,
                                           const std::vector<std::unique_ptr<ExprNode>>& args,
                                           const auto& emit_arg) {
    std::string result = "cactus_" + func_name + "(registry";
    for (const auto& arg : args) {
        result += ", ";
        result += emit_arg(*arg);
    }
    result += ")";
    return result;
}

// Returns "prefix::runtime_name" for a resolved stdlib function identity, or empty if not applicable.
std::string stdlib_runtime_call_name(const SymbolId& func_id) {
    if (func_id.kind != SymbolKind::Func) {
        return {};
    }
    if (is_stdlib_physics_flat_query(func_id)) {
        return {};
    }
    const std::string prefix = stdlib_runtime_prefix(func_id);
    if (prefix.empty()) {
        return {};
    }
    const std::string runtime_name = stdlib_runtime_func_name(func_id.module.name, func_id.local_name);
    std::string result;
    result.reserve(prefix.size() + runtime_name.size() + 2U);
    result.append(prefix).append("::").append(runtime_name);
    return result;
}

bool is_stdlib_editor_gizmo_mode_call(const SymbolId& func_id) {
    return symbol_is(func_id, SymbolKind::Func, "std.editor", "active_mode") ||
           symbol_is(func_id, SymbolKind::Func, "std.editor", "mode_label");
}

// runtime.hpp's editor_active_mode/editor_mode_label are precompiled and program-independent,
// so they stay int-based internally rather than naming a per-program generated enum type (see
// editor-gizmo-mode-enum's design.md decision 1). Bridge the DSL's GizmoMode-typed signature at
// the call site instead: cast the runtime's int return/param to/from the program's generated
// GizmoMode enum class.
std::string stdlib_editor_gizmo_mode_call(const SymbolId& func_id,
                                          const std::vector<std::unique_ptr<ExprNode>>& args,
                                          const DecoratedProgram& program,
                                          const auto& emit_arg) {
    const std::string runtime_name = stdlib_runtime_call_name(func_id);
    if (func_id.local_name == "active_mode") {
        const std::string gizmo_mode_cpp = EnttCodegenUtils::enum_cpp_name("GizmoMode", program);
        return "static_cast<" + gizmo_mode_cpp + ">(" + runtime_name + "(registry))";
    }
    // mode_label(mode: GizmoMode) string
    return runtime_name + "(static_cast<int>(" + emit_arg(*args[0]) + "))";
}

bool stdlib_call_needs_registry(const SymbolId& func_id) {
    if (func_id.kind != SymbolKind::Func) {
        return false;
    }
    const auto module_it = kRuntimeFuncNames.find(func_id.module.name);
    if (module_it == kRuntimeFuncNames.end()) {
        return false;
    }
    const auto func_it = module_it->second.find(func_id.local_name);
    return func_it != module_it->second.end() && func_it->second.needs_registry;
}

std::string lower_resolved_stdlib_call(const SymbolId& func_id,
                                       const std::vector<std::unique_ptr<ExprNode>>& args,
                                       const DecoratedProgram& program,
                                       const auto& emit_arg) {
    if (symbol_is(func_id, SymbolKind::Func, "std.text", "format")) {
        std::string result = "std::format(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            result += emit_arg(*args[i]);
        }
        return result + ")";
    }
    if (is_stdlib_physics_flat_query(func_id)) {
        return stdlib_physics_flat_query_call(func_id.local_name, args, emit_arg);
    }
    if (is_stdlib_ui_metric_func(func_id)) {
        return stdlib_ui_metric_func_call(func_id, args, emit_arg);
    }
    if (is_stdlib_editor_gizmo_mode_call(func_id)) {
        return stdlib_editor_gizmo_mode_call(func_id, args, program, emit_arg);
    }
    const std::string runtime_name = stdlib_runtime_call_name(func_id);
    if (runtime_name.empty()) {
        return {};
    }
    const bool needs_registry = stdlib_call_needs_registry(func_id);
    std::string result        = runtime_name + "(";
    if (needs_registry) {
        result += "registry";
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0 || needs_registry) {
            result += ", ";
        }
        result += emit_arg(*args[i]);
    }
    result += ")";
    return result;
}

std::string snake_case(const std::string& value) {
    std::string result;
    for (const char ch : value) {
        if (std::isupper(static_cast<unsigned char>(ch)) != 0) {
            if (!result.empty() && result.back() != '_') {
                result += '_';
            }
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else {
            result += ch;
        }
    }
    return result;
}

std::string upper_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string archetype_create_function_name(const SymbolId& template_id, const DecoratedProgram& program) {
    const bool is_local       = program.pub_templates.contains(template_id.local_name) ||
                                program.non_pub_templates.contains(template_id.local_name);
    const std::string& module = is_local ? program.module_name : template_id.module.name;
    return "create_" + canonical_to_cpp_name(module, snake_case(template_id.local_name));
}

std::string archetype_create_at_function_name(const SymbolId& template_id, const DecoratedProgram& program) {
    return archetype_create_function_name(template_id, program) + "_at";
}

std::string event_cpp_type(const std::string& event_name, const DecoratedProgram& program) {
    if (program.ast != nullptr) {
        for (const auto& decl : program.ast->declarations) {
            if (const auto* ev = std::get_if<EventNode>(&decl)) {
                if (ev->name == event_name) {
                    const std::string& mod = ev->module_name.empty() ? program.module_name : ev->module_name;
                    return canonical_to_cpp_name(mod, event_name) + "Event";
                }
            }
        }
    }
    return event_name + "Event";
}

std::string event_cpp_type(const SymbolId& event_id, const DecoratedProgram& program) {
    if (program.ast != nullptr) {
        for (const auto& decl : program.ast->declarations) {
            if (const auto* event = std::get_if<EventNode>(&decl);
                event != nullptr && event->resolved_event_id.has_value() && *event->resolved_event_id == event_id) {
                const auto& module = event->module_name.empty() ? program.module_name : event->module_name;
                return canonical_to_cpp_name(module, event->name) + "Event";
            }
        }
    }

    const auto canonical = make_canonical_id(event_id);
    for (const auto& [_, event] : program.events) {
        if ((event.symbol_id.has_value() && *event.symbol_id == event_id) || event.canonical_id == canonical) {
            return canonical_to_cpp_name(event.module_name, event.name) + "Event";
        }
    }
    return event_cpp_type_name(event_id);
}

std::string handler_trigger_cpp_type(const EventHandlerNode& handler, const DecoratedProgram& program) {
    if (handler.resolved_trigger.has_value() && handler.resolved_trigger->kind == HandlerTriggerKind::Phase) {
        return "cactus::runtime::entt_backend::" + canonical_to_cpp_name(handler.resolved_trigger->symbol) +
               "PhaseRuntimeState";
    }
    if (handler.resolved_trigger.has_value()) {
        return event_cpp_type(handler.resolved_trigger->symbol, program);
    }
    return event_cpp_type(handler.event_name, program);
}

std::string handler_trigger_suffix(const EventHandlerNode& handler) {
    if (!handler.event_name.contains('.') || !handler.resolved_trigger.has_value()) {
        return handler.event_name;
    }
    return canonical_to_cpp_name(handler.resolved_trigger->symbol);
}

std::string handler_trigger_binding(const EventHandlerNode& handler) {
    return handler.alias.value_or(snake_case(handler_trigger_suffix(handler)));
}

const HandlerContract* graph_handler_contract(const RuleNode& rule,
                                              const EventHandlerNode& handler,
                                              const DecoratedProgram& program) {
    if (!rule.resolved_rule_id.has_value() || !handler.resolved_trigger.has_value()) {
        return nullptr;
    }
    const HandlerIdentity identity{.rule = *rule.resolved_rule_id, .trigger = *handler.resolved_trigger};
    const auto found = std::ranges::find_if(program.execution_graph.handlers,
                                            [&](const auto& node) { return node.identity == identity; });
    return found == program.execution_graph.handlers.end() ? nullptr : &found->contract;
}

std::string input_action_constant_name(const std::string& input_name) {
    return "K_" + upper_copy(snake_case(input_name));
}

bool is_input_action_name(const DecoratedProgram& program, const std::string& name) {
    if (program.ast == nullptr) {
        return false;
    }
    for (const auto& decl : program.ast->declarations) {
        if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
            if (input->name == name) {
                return true;
            }
        }
    }
    return false;
}

std::string filter_simple_name(const FilterEntry& entry) {
    auto dot = entry.qualified_name.rfind('.');
    return (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
}

bool world_transform_is_volume(const DecoratedProgram& program) {
    // Root-program resolved usage — deterministic even when std.editor links
    // both WorldTransform variants into the merged trait map.
    return EnttCodegenUtils::world_transform_usage(program).volume;
}

struct FilterBinding {
    std::string trait_name;     // simple name (for alias/dedupe bookkeeping)
    std::string lookup_name;    // canonical id when resolved, else simple name (for find_trait)
    std::string cpp_type_name;  // canonical C++ type name (for code emission)
    std::string binding_name;   // variable name suffix (e.g. "WorldTransform_comp" or alias)
};

// ── Pair relation codegen scope (dsl-pair-relations) ─────────────────────────

// Resolved binding-relative trait namespace for one pair binding, mirroring
// the frontend's PairBindingScope (semantic_analyzer.hpp) but carrying the
// generated C++ component type name instead of a canonical SymbolId — codegen
// consumes the AST's already-resolved trait identities (via
// EnttCodegenUtils::trait_cpp_name) and never re-derives them from source
// dotted spelling.
struct PairCodegenTraitAccess {
    std::string cpp_type;
    bool is_marker = false;
};

struct PairCodegenBinding {
    std::string binding_name;
    std::unordered_map<std::string, PairCodegenTraitAccess> traits;  // access key -> resolved trait
};

struct PairCodegenScope {
    std::vector<PairCodegenBinding> bindings;  // exactly two, source order

    [[nodiscard]] const PairCodegenBinding* find(const std::string& name) const {
        for (const auto& binding : bindings) {
            if (binding.binding_name == name) {
                return &binding;
            }
        }
        return nullptr;
    }
};

using LexicalLocalBindings = std::unordered_set<std::string>;

// Coarse scalar-numeric classification, tracked per lexical local alongside
// LexicalLocalBindings, used solely to decide where the C++ emitter must
// insert an explicit static_cast<float> to keep mixed int/float DSL
// arithmetic — which Cactus's own type checker permits freely — from
// tripping clang-tidy's bugprone-narrowing-conversions on the generated
// output (see stdlib std.ui's Grid/Stack measurement formulas, the first DSL
// code to mix a GridItem `int` field with `vec2` float components).
enum class NumericKind : std::uint8_t { Unknown, Int, Float };

using LocalNumericKinds = std::unordered_map<std::string, NumericKind>;

// rewrite_stmt threads the enclosing lexical scope down as nullable pointers
// (nullptr at the handler's outermost statements); every nested block needs
// its own owned copy to extend before recursing.
LexicalLocalBindings clone_or_empty(const LexicalLocalBindings* locals) {
    return locals != nullptr ? *locals : LexicalLocalBindings{};
}

LocalNumericKinds clone_or_empty(const LocalNumericKinds* kinds) {
    return kinds != nullptr ? *kinds : LocalNumericKinds{};
}

// Every handler body starts a fresh lexical scope seeded with just the
// trigger binding (and its `as` alias, if any) — shared by all three
// handler-body emission shapes (selectionless/filtered/fallback).
LexicalLocalBindings handler_lexical_locals(const EventHandlerNode& handler) {
    LexicalLocalBindings lexical_locals;
    lexical_locals.insert(handler.event_name);
    if (handler.alias.has_value()) {
        lexical_locals.insert(*handler.alias);
    }
    return lexical_locals;
}

std::vector<FilterBinding> filter_bindings(const FilterClause& filter, const DecoratedProgram& program) {
    std::vector<FilterBinding> result;
    std::unordered_set<std::string> seen_traits;
    for (const auto& entry : filter.entries) {
        const auto trait_name = filter_simple_name(entry);
        const auto lookup_name =
            entry.resolved_trait_id.has_value() ? make_canonical_id(*entry.resolved_trait_id) : trait_name;
        const auto cpp_type_name =
            EnttCodegenUtils::trait_cpp_name(entry.resolved_trait_id, entry.qualified_name, program);
        if (seen_traits.insert(trait_name).second) {
            result.push_back(FilterBinding{.trait_name    = trait_name,
                                           .lookup_name   = lookup_name,
                                           .cpp_type_name = cpp_type_name,
                                           .binding_name  = cpp_type_name + "_comp"});
        }
        if (entry.alias.has_value()) {
            result.push_back(FilterBinding{.trait_name    = trait_name,
                                           .lookup_name   = lookup_name,
                                           .cpp_type_name = cpp_type_name,
                                           .binding_name  = *entry.alias});
        }
    }
    for (const auto& trait_name : filter.trait_names) {
        const auto cpp_type_name = EnttCodegenUtils::trait_cpp_name(trait_name, program);
        if (seen_traits.insert(trait_name).second) {
            result.push_back(FilterBinding{.trait_name    = trait_name,
                                           .lookup_name   = trait_name,
                                           .cpp_type_name = cpp_type_name,
                                           .binding_name  = cpp_type_name + "_comp"});
        }
    }
    return result;
}

// Returns trait lookup names (canonical id when resolved) for semantic lookups
// in program.traits — simple names alone are ambiguous once both stdlib
// transform variants are linked.
std::vector<std::string> filter_trait_names(const FilterClause& filter, const DecoratedProgram& program) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& binding : filter_bindings(filter, program)) {
        if (seen.insert(binding.lookup_name).second) {
            result.push_back(binding.lookup_name);
        }
    }
    return result;
}

// Returns canonical C++ type names for view<> template arguments.
std::vector<std::string> filter_cpp_type_names(const FilterClause& filter, const DecoratedProgram& program) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& binding : filter_bindings(filter, program)) {
        if (seen.insert(binding.cpp_type_name).second) {
            result.push_back(binding.cpp_type_name);
        }
    }
    return result;
}

// ── Pair binding codegen (dsl-pair-relations) ────────────────────────────────

struct PairBindingCodegen {
    std::vector<std::string> cpp_types;  // dedup'd view<> template args, source order
    PairCodegenBinding scope;            // access-key -> resolved trait, for rewrite_expr; scope.binding_name
                                         // is also this binding's source-level name
};

// Mirrors the frontend's build_pair_scope (semantic_analyzer.cpp): consumes
// each already-resolved trait entry (resolved_trait_id) into an access-key ->
// cpp-type map, keyed by qualified spelling and binding-local alias, exactly
// as the semantic analyzer's resolve_pair_member_chain expects to look up.
PairBindingCodegen build_pair_binding_codegen(const PairBindingNode& binding, const DecoratedProgram& program) {
    PairBindingCodegen result;
    result.scope.binding_name = binding.name;
    std::unordered_set<std::string> seen_cpp_types;
    for (const auto& entry : binding.traits) {
        const auto cpp_type_name =
            EnttCodegenUtils::trait_cpp_name(entry.resolved_trait_id, entry.qualified_name, program);
        const auto lookup_name =
            entry.resolved_trait_id.has_value() ? make_canonical_id(*entry.resolved_trait_id) : entry.qualified_name;
        const auto* trait    = EnttCodegenUtils::find_trait(program, lookup_name);
        const bool is_marker = trait == nullptr || trait->fields.empty();
        if (seen_cpp_types.insert(cpp_type_name).second) {
            result.cpp_types.push_back(cpp_type_name);
        }
        const PairCodegenTraitAccess access{.cpp_type = cpp_type_name, .is_marker = is_marker};
        result.scope.traits[entry.qualified_name] = access;
        if (entry.alias.has_value()) {
            result.scope.traits[*entry.alias] = access;
        }
    }
    return result;
}

// Emits the deterministic snapshot-and-sort prologue for one pair binding:
// collect matching entity handles via a typed view (finite, not materialized
// as tuples), pairing each with its creation ordinal in the same pass so the
// sort below reads every ordinal exactly once (registry.get<> is a sparse-set
// lookup; re-fetching it per comparison would cost O(n log n) lookups instead
// of O(n)). Sorting by creation ordinal keeps tuple/event order stable and
// backend-independent (spec: "cpp-entt maintains stable entity creation
// ordinals").
void emit_pair_binding_snapshot(std::ostringstream& out, const PairBindingCodegen& binding, int indent) {
    const std::string ind(static_cast<std::size_t>(indent) * 4U, ' ');
    const std::string& name = binding.scope.binding_name;
    out << ind << "std::vector<entt::entity> " << name << "_snapshot;\n";
    out << ind << "{\n";
    out << ind << "    auto " << name << "_view = registry.view<";
    for (std::size_t i = 0; i < binding.cpp_types.size(); ++i) {
        out << (i == 0 ? "" : ", ") << binding.cpp_types[i];
    }
    out << ">();\n";
    out << ind << "    std::vector<std::pair<std::uint64_t, entt::entity>> " << name << "_ordered;\n";
    out << ind << "    for (auto pair_entity : " << name << "_view) {\n";
    out << ind << "        " << name
        << "_ordered.emplace_back(registry.get<cactus::runtime::entt_backend::CreationOrdinal>(pair_entity)"
           ".value,\n";
    out << ind << "                               pair_entity);\n";
    out << ind << "    }\n";
    out << ind << "    std::ranges::sort(" << name << "_ordered);\n";
    out << ind << "    " << name << "_snapshot.reserve(" << name << "_ordered.size());\n";
    out << ind << "    for (const auto& [ordinal, entity] : " << name << "_ordered) {\n";
    out << ind << "        " << name << "_snapshot.push_back(entity);\n";
    out << ind << "    }\n";
    out << ind << "}\n";
}

bool is_flat_transform_propagation(const ExternRuleNode& sys, const DecoratedProgram& program) {
    // When std.editor is used it transitively imports both transform modules,
    // so both flat and volume TransformPropagation can appear in the merged AST.
    // Only emit the variant that matches the program's actual WorldTransform dimensionality.
    if (symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.transform.flat", "TransformPropagation")) {
        return !world_transform_is_volume(program);
    }
    if (symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.transform.volume", "TransformPropagation")) {
        return false;
    }
    return false;
}

bool is_volume_transform_propagation(const ExternRuleNode& sys, const DecoratedProgram& program) {
    if (symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.transform.volume", "TransformPropagation")) {
        return world_transform_is_volume(program);
    }
    if (symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.transform.flat", "TransformPropagation")) {
        return false;
    }
    return false;
}

bool is_shape_renderer(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.shapes", "ShapeRenderer");
}

bool is_sprite_renderer(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.sprites", "SpriteRenderer");
}

bool is_sprite_animation(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.sprites", "SpriteAnimation");
}

bool is_mesh_renderer(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.meshes", "MeshRenderer");
}

bool is_model_render(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.models", "ModelRender");
}

bool is_model_animation(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.models", "ModelAnimation");
}

bool is_billboard_renderer(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.meshes", "BillboardRenderer");
}

bool is_point_light_render(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.meshes", "PointLightRender");
}

bool is_directional_light_render(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.meshes", "DirectionalLightRender");
}

bool is_any_text_renderer_2d(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.text", "TextRenderer2D");
}

bool is_any_text_renderer_3d(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.text", "TextRenderer3D");
}

bool is_screen_label_render(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.render.text", "ScreenLabelRender");
}

// EditorTemplatePalette, GizmoRenderer2D, and GizmoRenderer3D are no longer compiler-owned
// extern rules (editor-declarative-rendering) — their geometry/layout decisions moved into
// DSL rules in std.editor.cactus (EditorTemplatePalette, EditorGizmoRenderer2D/3D) that emit
// the generic std.debug/std.ui primitive events instead. EditorPropertyPanel remains the sole
// extern rule here — still an unimplemented stub.
bool is_editor_extern_system(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.editor", "EditorPropertyPanel");
}

bool is_standard_ui_render(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.ui", "RenderUi");
}

bool is_pointer_router(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.ui", "RoutePointer");
}

bool is_debug_grid_3d(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.editor", "DebugGrid3D");
}

// Generic, non-branching event-payload renderers (editor-debug-draw / editor-screen-ui):
// exactly one compiler-owned handler per event, triggered by `on <Event>:` rather than a
// filter+phase view. Each reads only its event's own payload fields (delivered as the
// `occurrence` parameter — see generated_dispatch_event's HandlerImplementationKind::External
// + HandlerTriggerKind::Event branch in cpp_entt_codegen.cpp, which now passes the occurrence
// through instead of dropping it) and issues one raylib call. Matched and dispatched via the
// data-driven kDebugDrawRendererSpecs table below rather than one predicate + one dispatch
// `if` per primitive, so adding the next primitive is one table row, not two new functions
// that can silently fall out of sync (e.g. a forgotten EndMode call).

// Shared shape for every one-event-one-handler generic renderer above: resolve the
// triggering event's runtime type from the (sole) handler's resolved trigger, emit a
// `(registry, occurrence)` function (unlike the `(registry)`-only shape every filter/phase
// extern rule above uses, since these have no view to iterate — event dispatch already
// delivers exactly one invocation per occurrence), and wrap `draw_call_body` in the given
// BeginMode/EndMode pair for world-space primitives (empty strings for screen-space ones).
std::string emit_event_renderer_body(const ExternRuleNode& sys,
                                     const DecoratedProgram& program,
                                     const std::string& mode_begin,
                                     const std::string& mode_end,
                                     const std::string& draw_call_body) {
    if (sys.handlers.empty() || !sys.handlers.front().resolved_trigger.has_value()) {
        throw std::runtime_error("cpp-entt backend: extern rule '" + sys.name +
                                 "' has no resolved event handler for emit_event_renderer_body");
    }
    const auto& handler   = sys.handlers.front();
    const auto event_type = event_cpp_type(handler.resolved_trigger->symbol, program);
    std::ostringstream out;
    out << "void " << system_function_name(program.module_name, sys.name, "tick") << "(entt::registry& registry, const "
        << event_type << "& occurrence) {\n";
    out << "    (void)registry;\n";
    if (!mode_begin.empty()) {
        out << "    " << mode_begin << "\n";
    }
    out << draw_call_body;
    if (!mode_end.empty()) {
        out << "    " << mode_end << "\n";
    }
    out << "}\n\n";
    return out.str();
}

const std::string kBeginMode2D =
    "cactus::runtime::raylib::BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());";
const std::string kEndMode2D = "cactus::runtime::raylib::EndMode2D();";
const std::string kBeginMode3D =
    "cactus::runtime::raylib::BeginMode3D(cactus::runtime::entt_backend::get_active_camera_3d());";
const std::string kEndMode3D = "cactus::runtime::raylib::EndMode3D();";

// One row per generic event-payload renderer (see the comment above); (module_name, rule_name)
// identifies the extern rule to match, mode_begin/mode_end wrap world-space primitives in a
// raylib BeginMode/EndMode pair (empty for screen-space ones), and draw_call_body is the exact
// statement(s) emit_event_renderer_body splices into the generated handler.
struct DebugDrawRendererSpec {
    const char* module_name;
    const char* rule_name;
    std::string mode_begin;
    std::string mode_end;
    std::string draw_call_body;
};

const std::vector<DebugDrawRendererSpec>& debug_draw_renderer_specs() {
    static const std::vector<DebugDrawRendererSpec> specs = {
        {.module_name    = "std.debug",
         .rule_name      = "DrawDebugLine2DRenderer",
         .mode_begin     = kBeginMode2D,
         .mode_end       = kEndMode2D,
         .draw_call_body = "    cactus::runtime::raylib::DrawLineEx(occurrence.start, occurrence.end, "
                           "occurrence.thickness, occurrence.color);\n"},
        {.module_name    = "std.debug",
         .rule_name      = "DrawDebugTriangle2DRenderer",
         .mode_begin     = kBeginMode2D,
         .mode_end       = kEndMode2D,
         .draw_call_body = "    cactus::runtime::raylib::DrawTriangle(occurrence.a, occurrence.b, occurrence.c, "
                           "occurrence.color);\n"},
        {.module_name    = "std.debug",
         .rule_name      = "DrawDebugRingOutline2DRenderer",
         .mode_begin     = kBeginMode2D,
         .mode_end       = kEndMode2D,
         .draw_call_body = "    cactus::runtime::raylib::DrawRing(occurrence.center, "
                           "occurrence.inner_radius, occurrence.outer_radius, 0.0F, 360.0F, 32, "
                           "occurrence.color);\n"},
        {.module_name    = "std.debug",
         .rule_name      = "DrawDebugRectOutline2DRenderer",
         .mode_begin     = kBeginMode2D,
         .mode_end       = kEndMode2D,
         .draw_call_body = "    const Rectangle __rect{.x = occurrence.position.x, .y = occurrence.position.y,\n"
                           "                          .width = occurrence.size.x, .height = occurrence.size.y};\n"
                           "    cactus::runtime::raylib::DrawRectangleLinesEx(__rect, occurrence.thickness, "
                           "occurrence.color);\n"},
        {.module_name = "std.debug",
         .rule_name   = "DrawDebugLine3DRenderer",
         .mode_begin  = kBeginMode3D,
         .mode_end    = kEndMode3D,
         .draw_call_body =
             "    cactus::runtime::raylib::DrawLine3D(occurrence.start, occurrence.end, occurrence.color);\n"},
        {.module_name = "std.debug",
         .rule_name   = "DrawDebugWireBox3DRenderer",
         .mode_begin  = kBeginMode3D,
         .mode_end    = kEndMode3D,
         .draw_call_body =
             "    cactus::runtime::raylib::DrawCubeWiresV(occurrence.center, occurrence.size, occurrence.color);\n"},
        // raylib's DrawCircle3D takes a rotation axis + angle, not a plane normal; the event's
        // `normal` field maps to rotationAxis with a fixed 90-degree angle, matching the exact
        // hardcoded call this replaces (Rotate gizmo always passed axis (1,0,0), angle 90) —
        // task 5.3 visual-equivalence requirement, not a general normal-to-rotation solver.
        {.module_name    = "std.debug",
         .rule_name      = "DrawDebugCircle3DRenderer",
         .mode_begin     = kBeginMode3D,
         .mode_end       = kEndMode3D,
         .draw_call_body = "    cactus::runtime::raylib::DrawCircle3D(occurrence.center, occurrence.radius, "
                           "occurrence.normal, 90.0F, occurrence.color);\n"},
        {.module_name = "std.debug",
         .rule_name   = "DrawDebugCube3DRenderer",
         .mode_begin  = kBeginMode3D,
         .mode_end    = kEndMode3D,
         .draw_call_body =
             "    cactus::runtime::raylib::DrawCubeV(occurrence.center, occurrence.size, occurrence.color);\n"},
        {.module_name    = "std.ui",
         .rule_name      = "DrawScreenRectRenderer",
         .mode_begin     = "",
         .mode_end       = "",
         .draw_call_body = "    const Rectangle __rect{.x = occurrence.position.x, .y = occurrence.position.y,\n"
                           "                          .width = occurrence.size.x, .height = occurrence.size.y};\n"
                           "    if (occurrence.filled) {\n"
                           "        cactus::runtime::raylib::DrawRectangleRec(__rect, occurrence.color);\n"
                           "    } else {\n"
                           "        cactus::runtime::raylib::DrawRectangleLinesEx(__rect, occurrence.thickness, "
                           "occurrence.color);\n"
                           "    }\n"},
    };
    return specs;
}

std::string sort_key_expr(const SortKey& key, const std::string& entity_name, const RuleNode& sys) {
    auto alias_to_trait = [&]() -> std::string {
        for (const auto& entry : sys.filter.entries) {
            auto dot    = entry.qualified_name.rfind('.');
            auto simple = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
            if (entry.alias.has_value() && *entry.alias == key.alias) {
                return simple;
            }
            if (simple == key.alias) {
                return simple;
            }
        }
        for (const auto& trait : sys.filter.trait_names) {
            if (trait == key.alias) {
                return trait;
            }
        }
        return "";
    };

    std::string trait_name = alias_to_trait();
    std::ostringstream expr;
    expr << "registry.get<" << trait_name << ">(" << entity_name << ")." << key.field;
    return expr.str();
}

std::string primary_sort_trait(const SortKey& key, const RuleNode& sys) {
    for (const auto& entry : sys.filter.entries) {
        auto dot    = entry.qualified_name.rfind('.');
        auto simple = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
        if ((entry.alias.has_value() && *entry.alias == key.alias) || simple == key.alias) {
            return simple;
        }
    }
    for (const auto& trait : sys.filter.trait_names) {
        if (trait == key.alias) {
            return trait;
        }
    }
    return key.alias;
}

void emit_sort_call(std::ostringstream& out, const RuleNode& sys, int indent = 1) {
    if (sys.order_by.empty()) {
        return;
    }

    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    out << ind << "registry.sort<" << primary_sort_trait(sys.order_by.front(), sys)
        << ">([&](entt::entity a, entt::entity b) {\n";
    for (const auto& key : sys.order_by) {
        auto left  = sort_key_expr(key, "a", sys);
        auto right = sort_key_expr(key, "b", sys);
        out << ind << "    if (" << left << " != " << right << ") return " << left << (key.descending ? " > " : " < ")
            << right << ";\n";
    }
    out << ind << "    return false;\n";
    out << ind << "});\n";
}

std::string foreach_temp_name(const ForeachStmt& stmt) {
    return "foreach_snapshot_" + std::to_string(std::max(stmt.location.line, 0)) + "_" +
           std::to_string(std::max(stmt.location.column, 0));
}

// Generates a compiler-internal temporary name that cannot collide with a user-visible DSL
// identifier spliced into the same or an enclosing C++ scope. Reserved double-underscore
// prefixes (e.g. "__target") are avoided because they are reserved for the implementation by
// the C++ standard in any scope; a bare unprefixed name (e.g. "target", "existing") is avoided
// because generated code frequently splices a raw, unqualified DSL identifier into the same
// block, and a name like `target` or `existing` is a completely ordinary thing for a DSL author
// to bind. The "cactus_gen_" namespace prefix plus the source location suffix (mirroring
// foreach_temp_name's per-call-site uniqueness) makes an accidental collision with authored DSL
// source effectively impossible while remaining well-formed, non-reserved C++.
std::string gen_temp_name(const std::string& base, const SourceLocation& location) {
    return "cactus_gen_" + base + "_" + std::to_string(std::max(location.line, 0)) + "_" +
           std::to_string(std::max(location.column, 0));
}

void emit_storage_filter_skip(std::ostringstream& out,
                              const FilterClause& filter,
                              const FilterClause& exclude,
                              const DecoratedProgram& program,
                              int indent) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    for (const auto& cpp_name : filter_cpp_type_names(filter, program)) {
        out << ind << "if (!registry.all_of<" << cpp_name << ">(entity)) {\n";
        out << ind << "    continue;\n";
        out << ind << "}\n";
    }
    for (const auto& cpp_name : filter_cpp_type_names(exclude, program)) {
        out << ind << "if (registry.all_of<" << cpp_name << ">(entity)) {\n";
        out << ind << "    continue;\n";
        out << ind << "}\n";
    }
}

// Equivalent of emit_view_each_header's component bindings, but fetched from a
// single known-valid entity via registry.get<> instead of a view.each() lambda
// parameter — used for recipient-targeted unary dispatch (targeted-event-delivery),
// where the handler runs for exactly one entity instead of iterating a view.
void emit_component_bindings_from_entity(std::ostringstream& out,
                                         const std::vector<FilterBinding>& bindings,
                                         const std::string& entity_name,
                                         int indent,
                                         const DecoratedProgram& program) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::unordered_set<std::string> seen_cpp;
    for (const auto& binding : bindings) {
        if (!seen_cpp.insert(binding.cpp_type_name).second) {
            continue;
        }
        const auto* trait   = EnttCodegenUtils::find_trait(program, binding.lookup_name);
        const bool is_empty = trait == nullptr || trait->fields.empty();
        if (!is_empty) {
            out << ind << "[[maybe_unused]] auto& " << binding.cpp_type_name << "_comp = registry.get<"
                << binding.cpp_type_name << ">(" << entity_name << ");\n";
        }
    }
}

void emit_filter_alias_bindings(std::ostringstream& out,
                                const FilterClause& filter,
                                const DecoratedProgram& program,
                                int indent) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    for (const auto& binding : filter_bindings(filter, program)) {
        if (binding.binding_name == binding.cpp_type_name + "_comp") {
            continue;
        }
        out << ind << "[[maybe_unused]] auto& " << binding.binding_name << " = " << binding.cpp_type_name << "_comp;\n";
    }
}

void emit_view_declaration(std::ostringstream& out,
                           const std::vector<std::string>& cpp_type_names,
                           const std::vector<std::string>& exclude_cpp_type_names,
                           int indent) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    out << ind << "auto view = registry.view<";
    for (size_t i = 0; i < cpp_type_names.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << cpp_type_names[i];
    }
    out << ">(";
    if (!exclude_cpp_type_names.empty()) {
        out << "entt::exclude<";
        for (size_t i = 0; i < exclude_cpp_type_names.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << exclude_cpp_type_names[i];
        }
        out << ">";
    }
    out << ");\n";
}

void emit_view_each_header(std::ostringstream& out,
                           const std::vector<FilterBinding>& bindings,
                           int indent,
                           const DecoratedProgram& program) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    out << ind << "view.each([&](entt::entity entity";
    std::unordered_set<std::string> seen_cpp;
    for (const auto& binding : bindings) {
        if (!seen_cpp.insert(binding.cpp_type_name).second) {
            continue;
        }
        // EnTT does not pass empty (marker) components to view.each lambdas.
        const auto* trait   = EnttCodegenUtils::find_trait(program, binding.lookup_name);
        const bool is_empty = trait == nullptr || trait->fields.empty();
        if (!is_empty) {
            out << ", [[maybe_unused]] " << binding.cpp_type_name << "& " << binding.cpp_type_name << "_comp";
        }
    }
    out << ") {\n";
}

}  // namespace

// ── Helper: resolve which component a field belongs to ──────────────────────

static std::string find_comp_for_field(const std::string& field_name,
                                       const std::vector<std::string>& trait_names,
                                       const DecoratedProgram& program) {
    for (const auto& tn : trait_names) {
        const auto* trait = EnttCodegenUtils::find_trait(program, tn);
        if (trait != nullptr) {
            for (const auto& f : trait->fields) {
                if (f.name == field_name) {
                    return tn;
                }
            }
        }
    }
    return "";
}

// A module-level `const:` block declaration's own value expression, or
// nullptr if no const with this name exists. Mirrors find_comp_for_field's
// shape for resolving a bare identifier's meaning outside local/trait-field
// scope.
static const ExprNode* find_const_value_expr(const std::string& name, const DecoratedProgram& program) {
    if (program.ast == nullptr) {
        return nullptr;
    }
    for (const auto& decl : program.ast->declarations) {
        const auto* const_block = std::get_if<ConstBlockNode>(&decl);
        if (const_block == nullptr) {
            continue;
        }
        for (const auto& assignment : const_block->assignments) {
            if (assignment.name == name) {
                return assignment.value.get();
            }
        }
    }
    return nullptr;
}

// ── Helper: collect all field names from filter traits ───────────────────────

static std::unordered_set<std::string> collect_trait_fields(const std::vector<std::string>& trait_names,
                                                            const DecoratedProgram& program) {
    std::unordered_set<std::string> fields;
    for (const auto& tn : trait_names) {
        const auto* trait = EnttCodegenUtils::find_trait(program, tn);
        if (trait != nullptr) {
            for (const auto& f : trait->fields) {
                fields.insert(f.name);
            }
        }
    }
    return fields;
}

// ── Helper: best-effort int-vs-float classification for cast insertion ──────
//
// Conservative by construction: anything it can't positively resolve comes
// back Unknown, and BinaryExpr only inserts a cast when BOTH operands
// resolve (one Int, one Float) — a missed cast opportunity is safe, a wrong
// one (e.g. silently turning an all-int `%`/`/` computation into float
// arithmetic because the other operand's type was merely unresolved) is not.
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- exhaustive AST-dispatch arms, same
// inherent shape as rewrite_expr/rewrite_stmt below.
static NumericKind infer_numeric_kind(const ExprNode& expr,
                                      const std::vector<std::string>& trait_names,
                                      const DecoratedProgram& program,
                                      const LocalNumericKinds* local_kinds) {
    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity) -- same reason as the outer function
        [&](const auto& e) -> NumericKind {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::Float) {
                    return NumericKind::Float;
                }
                if (e.kind == LiteralExpr::Kind::Int) {
                    return NumericKind::Int;
                }
                return NumericKind::Unknown;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                if (local_kinds != nullptr) {
                    if (const auto found = local_kinds->find(e.name); found != local_kinds->end()) {
                        return found->second;
                    }
                }
                // Trait fields take priority, matching rewrite_expr's own IdentExpr
                // precedence (a known trait field is qualified as `comp.field`; anything
                // else, including a const, is emitted as a bare name) — checking fields
                // first here keeps this resolution order consistent with that one.
                const auto comp = find_comp_for_field(e.name, trait_names, program);
                if (comp.empty()) {
                    // Module-level `const:` block declarations (e.g. `PARTICLE_COUNT`) aren't
                    // trait fields or lexical locals; resolve their kind from their own
                    // declared value expression so mixed int/float arithmetic involving a
                    // const (e.g. a range loop variable times a const-derived angle step)
                    // still gets the right static_cast.
                    if (const auto* const_value = find_const_value_expr(e.name, program)) {
                        return infer_numeric_kind(*const_value, trait_names, program, local_kinds);
                    }
                    return NumericKind::Unknown;
                }
                const auto* trait = EnttCodegenUtils::find_trait(program, comp);
                if (trait == nullptr) {
                    return NumericKind::Unknown;
                }
                for (const auto& f : trait->fields) {
                    if (f.name != e.name) {
                        continue;
                    }
                    if (f.type.kind == TypeKind::Int) {
                        return NumericKind::Int;
                    }
                    if (f.type.kind == TypeKind::Float) {
                        return NumericKind::Float;
                    }
                    return NumericKind::Unknown;
                }
                return NumericKind::Unknown;
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                // vec2/vec3 components are always float in this type system —
                // covers the overwhelmingly common operand shape (`.x`/`.y`/`.z`)
                // without needing to resolve the object's own declared type.
                if (e.member == "x" || e.member == "y" || e.member == "z") {
                    return NumericKind::Float;
                }
                return NumericKind::Unknown;
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return infer_numeric_kind(*e.operand, trait_names, program, local_kinds);
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                static const std::unordered_set<std::string> kArithmeticOps{"+", "-", "*", "/", "%"};
                if (!kArithmeticOps.contains(e.op)) {
                    return NumericKind::Unknown;
                }
                const auto left  = infer_numeric_kind(*e.left, trait_names, program, local_kinds);
                const auto right = infer_numeric_kind(*e.right, trait_names, program, local_kinds);
                if (left == NumericKind::Unknown || right == NumericKind::Unknown) {
                    return NumericKind::Unknown;
                }
                return (left == NumericKind::Float || right == NumericKind::Float) ? NumericKind::Float
                                                                                   : NumericKind::Int;
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                const ResolvedFunc* func = nullptr;
                if (e.resolved_callee_id.has_value()) {
                    func = find_resolved_func(program, *e.resolved_callee_id);
                } else if (const auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
                    if (const auto found = program.funcs.find(ident->name); found != program.funcs.end()) {
                        func = &found->second;
                    }
                }
                if (func == nullptr || !func->return_type.has_value()) {
                    return NumericKind::Unknown;
                }
                if (func->return_type->kind == TypeKind::Int) {
                    return NumericKind::Int;
                }
                if (func->return_type->kind == TypeKind::Float) {
                    return NumericKind::Float;
                }
                return NumericKind::Unknown;
            } else {
                return NumericKind::Unknown;
            }
        },
        expr.expr);
}

static std::string rewrite_expr(const ExprNode& expr,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases            = {},
                                const std::unordered_map<std::string, std::string>& cpp_overrides = {},
                                const PairCodegenScope* pair_scope                                = nullptr,
                                const LocalNumericKinds* local_kinds                              = nullptr);

// Rewrites a vec2/vec3 constructor argument, promoting an `int`-kind result
// to `float` via the same infer_numeric_kind machinery mixed int/float binary
// arithmetic already uses (dsl-vector-expressions). Shared by the generic
// CallExpr fallback (in rewrite_expr below) and the VarAssign-specific vec2
// pretty-printer (in rewrite_stmt) — the two codegen sites that construct
// vec2/vec3 argument lists.
static std::string rewrite_vec_arg(const ExprNode& arg,
                                   const std::vector<std::string>& trait_names,
                                   const DecoratedProgram& program,
                                   const std::unordered_set<std::string>& pointer_aliases,
                                   const std::unordered_map<std::string, std::string>& cpp_overrides,
                                   const PairCodegenScope* pair_scope,
                                   const LocalNumericKinds* local_kinds) {
    auto text = rewrite_expr(arg, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
    if (infer_numeric_kind(arg, trait_names, program, local_kinds) == NumericKind::Int) {
        return "static_cast<float>(" + text + ")";
    }
    return text;
}

// ── Rewrite expression: replace bare field names with comp.field ─────────────

// Comma-joined rewrite_expr(*args[i], ...) for a call/list argument list.
static std::string join_rewritten_args(const std::vector<std::unique_ptr<ExprNode>>& args,
                                       const std::vector<std::string>& trait_names,
                                       const DecoratedProgram& program,
                                       const std::unordered_set<std::string>& pointer_aliases,
                                       const std::unordered_map<std::string, std::string>& cpp_overrides,
                                       const PairCodegenScope* pair_scope,
                                       const LocalNumericKinds* local_kinds = nullptr) {
    std::string result;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += rewrite_expr(*args[i], trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
    }
    return result;
}

// (object, member) -> runtime-call dispatch table for rewrite_expr's
// input/camera2d/camera3d/transform2d/transform3d builtin call rewriting
// (camera2d/camera3d/transform2d/transform3d are aliases declared in
// std.editor and are invisible from the root program's AST, so
// imported_module_name cannot resolve them and they need hardcoded
// dispatch). required_arg_count mirrors each original site's own
// e.args.size() check (nullopt for the input.axis/pressed/down/released
// group, which never checked arg count); call_body receives the args
// already rewritten and comma-joined (join_rewritten_args on a single-arg
// list is exactly rewrite_expr(*args[0], ...), so this covers the 1-arg
// sites too without a separate code path).
struct BuiltinMemberCallSpec {
    const char* object_name;
    const char* member_name;
    std::optional<std::size_t> required_arg_count;
    std::function<std::string(const std::string& joined_args)> call_body;
};

const std::array<BuiltinMemberCallSpec, 14>& builtin_member_call_specs() {
    static const std::array<BuiltinMemberCallSpec, 14> specs{{
        {.object_name        = "input",
         .member_name        = "mouse_position",
         .required_arg_count = 0,
         .call_body          = [](const std::string&) { return "cactus::runtime::entt_backend::mouse_position()"; }},
        {.object_name        = "input",
         .member_name        = "axis",
         .required_arg_count = std::nullopt,
         .call_body          = [](const std::string& args) { return "InputEvent::axis(" + args + ")"; }},
        {.object_name        = "input",
         .member_name        = "pressed",
         .required_arg_count = std::nullopt,
         .call_body          = [](const std::string& args) { return "InputEvent::pressed(" + args + ")"; }},
        {.object_name        = "input",
         .member_name        = "down",
         .required_arg_count = std::nullopt,
         .call_body          = [](const std::string& args) { return "InputEvent::down(" + args + ")"; }},
        {.object_name        = "input",
         .member_name        = "released",
         .required_arg_count = std::nullopt,
         .call_body          = [](const std::string& args) { return "InputEvent::released(" + args + ")"; }},
        {.object_name        = "input",
         .member_name        = "mouse_delta",
         .required_arg_count = 0,
         .call_body = [](const std::string&) { return "cactus::runtime::entt_backend::editor_mouse_delta_screen()"; }},
        {.object_name        = "input",
         .member_name        = "wheel_delta",
         .required_arg_count = 0,
         .call_body = [](const std::string&) { return "cactus::runtime::entt_backend::editor_wheel_delta()"; }},
        {.object_name        = "input",
         .member_name        = "consume",
         .required_arg_count = 1,
         .call_body =
             [](const std::string& args) { return "cactus::runtime::entt_backend::editor_consume(" + args + ")"; }},
        {.object_name        = "camera2d",
         .member_name        = "screen_to_world",
         .required_arg_count = 1,
         .call_body =
             [](const std::string& args) {
                 return "cactus::runtime::entt_backend::editor_screen_to_world_2d(" + args + ")";
             }},
        {.object_name        = "camera2d",
         .member_name        = "screen_delta_to_world",
         .required_arg_count = 1,
         .call_body =
             [](const std::string& args) {
                 return "cactus::runtime::entt_backend::screen_delta_to_world_2d(" + args + ")";
             }},
        {.object_name        = "camera3d",
         .member_name        = "screen_to_plane",
         .required_arg_count = 3,
         .call_body =
             [](const std::string& args) {
                 return "cactus::runtime::entt_backend::editor_plane_project_3d(" + args + ")";
             }},
        {.object_name        = "camera3d",
         .member_name        = "screen_delta_on_plane",
         .required_arg_count = 4,
         .call_body =
             [](const std::string& args) {
                 return "cactus::runtime::entt_backend::screen_delta_on_plane_3d(" + args + ")";
             }},
        {.object_name        = "transform2d",
         .member_name        = "world_position",
         .required_arg_count = 1,
         .call_body =
             [](const std::string& args) {
                 return "cactus::runtime::entt_backend::editor_entity_position_2d(registry, " + args + ")";
             }},
        {.object_name        = "transform3d",
         .member_name        = "world_position",
         .required_arg_count = 1,
         .call_body =
             [](const std::string& args) {
                 return "cactus::runtime::entt_backend::editor_entity_position_3d(registry, " + args + ")";
             }},
    }};
    return specs;
}

static std::string rewrite_stmt(const StmtNode& stmt,
                                int indent,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases            = {},
                                bool dispatcher_available                                         = false,
                                const std::unordered_map<std::string, std::string>& cpp_overrides = {},
                                const PairCodegenScope* pair_scope                                = nullptr,
                                LexicalLocalBindings* lexical_locals                              = nullptr,
                                LocalNumericKinds* local_kinds                                    = nullptr);

static std::string rewrite_stmt_block(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                      int indent,
                                      const std::vector<std::string>& trait_names,
                                      const DecoratedProgram& program,
                                      const std::unordered_set<std::string>& pointer_aliases,
                                      bool dispatcher_available,
                                      const std::unordered_map<std::string, std::string>& cpp_overrides,
                                      const PairCodegenScope* pair_scope,
                                      LexicalLocalBindings& lexical_locals,
                                      LocalNumericKinds& local_kinds);

static std::string trait_cpp_from_entry(const ArchetypeTraitEntry& entry, const DecoratedProgram& program) {
    return EnttCodegenUtils::trait_cpp_name(entry.resolved_trait_id, entry.trait_name, program);
}

static std::string emit_spawn_overrides(const std::string& entity_name,
                                        const std::vector<ArchetypeTraitEntry>& overrides,
                                        int indent,
                                        const std::vector<std::string>& trait_names,
                                        const DecoratedProgram& program,
                                        const std::unordered_set<std::string>& pointer_aliases,
                                        const PairCodegenScope* pair_scope = nullptr) {
    const std::string ind(static_cast<std::size_t>(indent) * 4U, ' ');
    std::ostringstream out;
    for (const auto& override_entry : overrides) {
        if (override_entry.assignments.empty()) {
            continue;
        }

        const std::string cpp_name      = trait_cpp_from_entry(override_entry, program);
        const std::string existing_name = gen_temp_name("existing", override_entry.location);
        const std::string value_name    = gen_temp_name("override_value", override_entry.location);
        out << ind << "{\n";
        out << ind << "    auto " << existing_name << " = registry.try_get<" << cpp_name << ">(" << entity_name
            << ");\n";
        out << ind << "    auto " << value_name << " = " << existing_name << " ? *" << existing_name << " : "
            << cpp_name << "{};\n";
        for (const auto& assignment : override_entry.assignments) {
            out << ind << "    " << value_name << "." << assignment.name << " = "
                << rewrite_expr(*assignment.value, trait_names, program, pointer_aliases, {}, pair_scope) << ";\n";
        }
        out << ind << "    registry.emplace_or_replace<" << cpp_name << ">(" << entity_name << ", " << value_name
            << ");\n";
        out << ind << "}\n";
    }
    return out.str();
}

// ── Hierarchical spawn expansion (dsl-hierarchical-entity-templates D9) ─────

// Per-node creation helper names — must match the names generated by cpp_entt_codegen.cpp.
static std::string archetype_node_create_function_name(const std::string& module_name,
                                                       const std::string& archetype_name,
                                                       const std::vector<std::string>& role_path) {
    std::string name = "create_" + canonical_to_cpp_name(module_name, snake_case(archetype_name)) + "__node";
    for (const auto& role : role_path) {
        name += "__" + snake_case(role);
    }
    return name;
}

static std::string archetype_node_create_at_function_name(const std::string& module_name,
                                                          const std::string& archetype_name,
                                                          const std::vector<std::string>& role_path) {
    return archetype_node_create_function_name(module_name, archetype_name, role_path) + "_at";
}

static const std::vector<ChildArchetypeNode>* find_template_children(const DecoratedProgram& program,
                                                                     const SymbolId& template_id) {
    if (program.ast == nullptr) {
        return nullptr;
    }
    for (const auto& decl : program.ast->declarations) {
        const auto* tmpl = std::get_if<TemplateNode>(&decl);
        if (tmpl != nullptr && tmpl->resolved_template_id.has_value() && *tmpl->resolved_template_id == template_id &&
            !tmpl->children.empty()) {
            return &tmpl->children;
        }
    }
    return nullptr;
}

// Spawn sites with child overrides expand the tree inline: create each node
// via its per-node helper in parent-first preorder, emplace Parent on non-root
// nodes, and apply that node's overrides in handler scope.
static void emit_spawn_child_expansion(std::ostringstream& out,
                                       const std::string& template_module,
                                       const std::string& template_local_name,
                                       const std::vector<ChildArchetypeNode>& children,
                                       const std::vector<ChildOverrideNode>& overrides,
                                       const std::string& parent_var,
                                       const std::string& var_prefix,
                                       std::vector<std::string>& role_path,
                                       const std::vector<std::string>& trait_names,
                                       const DecoratedProgram& program,
                                       const std::unordered_set<std::string>& pointer_aliases,
                                       const PairCodegenScope* pair_scope = nullptr) {
    static const std::vector<ChildOverrideNode> NO_OVERRIDES;
    std::size_t index = 0;
    for (const auto& child : children) {
        const std::string var = var_prefix + "_" + std::to_string(index);
        role_path.push_back(child.role);
        out << "    auto " << var << " = "
            << archetype_node_create_function_name(template_module, template_local_name, role_path) << "(registry);\n";
        {
            const std::string parent_cpp = EnttCodegenUtils::trait_cpp_name("Parent", program);
            out << "    registry.emplace_or_replace<" << parent_cpp << ">(" << var << ", " << parent_cpp
                << "{.parent = " << parent_var << "});\n";
        }

        const ChildOverrideNode* override_node = nullptr;
        for (const auto& candidate : overrides) {
            if (candidate.role == child.role) {
                override_node = &candidate;
                break;
            }
        }
        if (override_node != nullptr) {
            out << emit_spawn_overrides(
                var, override_node->traits, 1, trait_names, program, pointer_aliases, pair_scope);
        }

        emit_spawn_child_expansion(out,
                                   template_module,
                                   template_local_name,
                                   child.children,
                                   override_node != nullptr ? override_node->children : NO_OVERRIDES,
                                   var,
                                   var,
                                   role_path,
                                   trait_names,
                                   program,
                                   pointer_aliases,
                                   pair_scope);
        role_path.pop_back();
        ++index;
    }
}

static std::string emit_hierarchical_spawn_expansion(const SymbolId& template_id,
                                                     const std::vector<ArchetypeTraitEntry>& root_overrides,
                                                     const std::vector<ChildOverrideNode>& child_overrides,
                                                     const std::vector<ChildArchetypeNode>& children,
                                                     const std::vector<std::string>& trait_names,
                                                     const DecoratedProgram& program,
                                                     const std::unordered_set<std::string>& pointer_aliases,
                                                     const SourceLocation& location,
                                                     const PairCodegenScope* pair_scope = nullptr) {
    std::ostringstream out;
    std::vector<std::string> role_path;
    const bool is_local_tmpl      = program.pub_templates.contains(template_id.local_name) ||
                                    program.non_pub_templates.contains(template_id.local_name);
    const std::string tmpl_module = is_local_tmpl ? program.module_name : template_id.module.name;
    const std::string& tmpl_local = template_id.local_name;
    out << "([&]() {\n";
    const bool graph_runtime = !program.execution_graph.phases.empty();
    // "spawned"/"committed" are arbitrary-looking, ordinary-sounding names a DSL author could
    // plausibly bind via `let`; mangle them so an override expression referencing such a
    // variable can't be captured by these compiler-internal declarations (same class of bug as
    // the target/parent self-reference cases above).
    const std::string spawned_name   = gen_temp_name("spawned", location);
    const std::string committed_name = gen_temp_name("committed", location);
    if (graph_runtime) {
        out << "    auto " << spawned_name << " = cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        out << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        out << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Spawn,\n";
        out << "        [=](entt::registry& registry) mutable {\n";
        out << "            auto " << committed_name << " = "
            << archetype_node_create_at_function_name(tmpl_module, tmpl_local, role_path) << "(registry, "
            << spawned_name << ");\n";
        out << emit_spawn_overrides(
            committed_name, root_overrides, 3, trait_names, program, pointer_aliases, pair_scope);
    } else {
        out << "    auto " << spawned_name << " = "
            << archetype_node_create_function_name(tmpl_module, tmpl_local, role_path) << "(registry);\n";
        out << emit_spawn_overrides(spawned_name, root_overrides, 1, trait_names, program, pointer_aliases, pair_scope);
    }
    emit_spawn_child_expansion(out,
                               tmpl_module,
                               tmpl_local,
                               children,
                               child_overrides,
                               graph_runtime ? committed_name : spawned_name,
                               "child",
                               role_path,
                               trait_names,
                               program,
                               pointer_aliases,
                               pair_scope);
    if (graph_runtime) {
        out << "        });\n";
    }
    out << "    return " << spawned_name << ";\n";
    out << "})()";
    return out.str();
}

static std::string emit_spawn_expression(const SpawnExpr& spawn,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases,
                                         const PairCodegenScope* pair_scope = nullptr) {
    const SymbolId tmpl_id = spawn.resolved_template_id.has_value()
                                 ? *spawn.resolved_template_id
                                 : make_symbol_id(SymbolKind::Template, program.module_name, spawn.template_name);
    if (!spawn.child_overrides.empty()) {
        if (const auto* children = find_template_children(program, tmpl_id)) {
            return emit_hierarchical_spawn_expansion(tmpl_id,
                                                     spawn.overrides,
                                                     spawn.child_overrides,
                                                     *children,
                                                     trait_names,
                                                     program,
                                                     pointer_aliases,
                                                     spawn.location,
                                                     pair_scope);
        }
    }
    std::ostringstream out;
    const std::string spawned_name = gen_temp_name("spawned", spawn.location);
    out << "([&]() {\n";
    if (!program.execution_graph.phases.empty()) {
        out << "    auto " << spawned_name << " = cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        out << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        out << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Spawn,\n";
        out << "        [=](entt::registry& registry) mutable {\n";
        out << "            " << archetype_create_at_function_name(tmpl_id, program) << "(registry, " << spawned_name
            << ");\n";
        out << emit_spawn_overrides(
            spawned_name, spawn.overrides, 3, trait_names, program, pointer_aliases, pair_scope);
        out << "        });\n";
    } else {
        out << "    auto " << spawned_name << " = " << archetype_create_function_name(tmpl_id, program)
            << "(registry);\n";
        out << emit_spawn_overrides(
            spawned_name, spawn.overrides, 1, trait_names, program, pointer_aliases, pair_scope);
    }
    out << "    return " << spawned_name << ";\n";
    out << "})()";
    return out.str();
}

// ── Query call lowering ─────────────────────────────────────────────────────

// Build "<T1, T2>(entt::exclude<N1, N2>)" from filter predicates.
// prepend_include must already be the canonical C++ name; predicate names are
// resolved to canonical C++ names when program is provided.
static std::string build_view_suffix(const std::vector<QueryFilterPredicate>& filters,
                                     const std::string& prepend_include = {},
                                     const DecoratedProgram* program    = nullptr) {
    auto to_cpp = [&](const QueryFilterPredicate& f) -> std::string {
        if (program != nullptr) {
            return EnttCodegenUtils::trait_cpp_name(f.resolved_trait_id, f.trait_name, *program);
        }
        if (f.resolved_trait_id.has_value()) {
            return EnttCodegenUtils::trait_cpp_name(*f.resolved_trait_id);
        }
        return f.trait_name;
    };
    std::string include_list = prepend_include;
    std::string exclude_list;
    for (const auto& f : filters) {
        const std::string cpp_name = to_cpp(f);
        if (f.negated) {
            if (!exclude_list.empty()) {
                exclude_list += ", ";
            }
            exclude_list += cpp_name;
        } else if (cpp_name != prepend_include) {
            if (!include_list.empty()) {
                include_list += ", ";
            }
            include_list += cpp_name;
        }
    }
    std::string result = "<" + include_list + ">(";
    if (!exclude_list.empty()) {
        result += "entt::exclude<" + exclude_list + ">";
    }
    result += ")";
    return result;
}

static std::string find_named_arg_value(const std::vector<FieldAssignment>& named_args,
                                        const std::string& name,
                                        const auto& emit_arg) {
    for (const auto& a : named_args) {
        if (a.name == name) {
            return emit_arg(*a.value);
        }
    }
    return "/* missing:" + name + " */";
}

static std::string lower_ecs_query_call(const QueryCallExpr& qcall,
                                        const std::string& func_name,
                                        const auto& emit_arg,
                                        const DecoratedProgram& program) {
    const std::string view = "registry.view" + build_view_suffix(qcall.filters, {}, &program);
    if (func_name == "exists") {
        return "[&]{ auto view = " + view + "; return view.begin() != view.end(); }()";
    }
    if (func_name == "count") {
        return "[&]{ return static_cast<int>(std::ranges::distance(" + view + ")); }()";
    }
    if (func_name == "first") {
        return "[&]{ auto view = " + view +
               "; auto it = view.begin(); return it != view.end() ? "
               "static_cast<entt::entity>(*it) : entt::entity{entt::null}; }()";
    }
    if (func_name == "all") {
        return "[&]{ std::vector<entt::entity> result; for (auto e : " + view +
               ") result.push_back(e); return result; }()";
    }
    if (func_name == "children") {
        const std::string of_expr          = find_named_arg_value(qcall.named_args, "of", emit_arg);
        const std::string parent_cpp       = EnttCodegenUtils::trait_cpp_name("Parent", program);
        const std::string child_view       = "registry.view" + build_view_suffix(qcall.filters, parent_cpp, &program);
        const std::string requested_parent = gen_temp_name("requested_parent", qcall.location);
        return "[&]{ const auto " + requested_parent + " = (" + of_expr + "); if (!registry.valid(" + requested_parent +
               ")) return std::vector<entt::entity>{}; "
               "std::vector<std::pair<std::uint64_t, entt::entity>> ordered; for (auto e : " +
               child_view + ") { if (registry.get<" + parent_cpp + ">(e).parent == " + requested_parent +
               ") ordered.emplace_back(registry.get<"
               "cactus::runtime::entt_backend::CreationOrdinal>(e).value, e); } std::ranges::sort(ordered); "
               "std::vector<entt::entity> result; result.reserve(ordered.size()); for (const auto& [ordinal, e] : "
               "ordered) { (void)ordinal; result.push_back(e); } return result; }()";
    }
    if (func_name == "hierarchy_preorder" || func_name == "hierarchy_postorder") {
        const std::string parent_cpp = EnttCodegenUtils::trait_cpp_name("Parent", program);
        const bool postorder         = func_name == "hierarchy_postorder";
        return "[&]{ std::vector<std::pair<std::uint64_t, entt::entity>> ordered; for (auto e : " + view +
               ") ordered.emplace_back(registry.get<"
               "cactus::runtime::entt_backend::CreationOrdinal>(e).value, e); std::ranges::sort(ordered); "
               "std::unordered_set<entt::entity> matching; matching.reserve(ordered.size()); for (const auto& "
               "[ordinal, e] : ordered) { (void)ordinal; matching.insert(e); } "
               "std::unordered_map<entt::entity, std::vector<entt::entity>> children; "
               "std::vector<entt::entity> roots; roots.reserve(ordered.size()); for (const auto& [ordinal, e] : "
               "ordered) { (void)ordinal; const auto* relation = registry.try_get<" +
               parent_cpp +
               ">(e); if (relation != nullptr && registry.valid(relation->parent) && "
               "matching.contains(relation->parent)) "
               "children[relation->parent].push_back(e); else roots.push_back(e); } "
               "std::vector<entt::entity> result; result.reserve(ordered.size()); std::unordered_set<entt::entity> "
               "visited; visited.reserve(ordered.size()); auto visit = [&](auto&& self, entt::entity e) -> void { if "
               "(!visited.insert(e).second) return; " +
               std::string(postorder ? "" : "result.push_back(e); ") +
               "if (const auto it = children.find(e); it != children.end()) for (const auto child : it->second) "
               "self(self, child); " +
               std::string(postorder ? "result.push_back(e); " : "") +
               "}; for (const auto root : roots) visit(visit, root); for (const auto& [ordinal, e] : ordered) { "
               "(void)ordinal; visit(visit, e); } return result; }()";
    }
    if (func_name == "parent") {
        // of_expr is arbitrary rewritten DSL text (very plausibly the bare identifier `parent`,
        // given the traversal this query performs) — the Parent-component pointer must be bound
        // to a name that cannot collide with it, or the declaration below becomes
        // self-referential.
        const std::string of_expr     = find_named_arg_value(qcall.named_args, "of", emit_arg);
        const std::string parent_cpp  = EnttCodegenUtils::trait_cpp_name("Parent", program);
        const std::string parent_comp = gen_temp_name("parent_component", qcall.location);
        return "[&]{ if (auto* " + parent_comp + " = registry.try_get<" + parent_cpp + ">(" + of_expr + "); " +
               parent_comp + " != nullptr) return " + parent_comp + "->parent; return entt::entity{entt::null}; }()";
    }
    return "/* unsupported std.query func: " + func_name + " */";
}

static std::string lower_flat_spatial_query(const QueryCallExpr& qcall,
                                            const std::string& func_name,
                                            const auto& emit_arg,
                                            const DecoratedProgram& program) {
    const std::string wt          = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
    const std::string view        = "registry.view" + build_view_suffix(qcall.filters, wt, &program);
    const std::string position_of = "[&](entt::entity e) { return registry.get<" + wt + ">(e).position; }";
    if (func_name == "nearest") {
        const std::string from = find_named_arg_value(qcall.named_args, "from", emit_arg);
        return "cactus::runtime::entt_backend::query_nearest(" + view + ", (" + from + "), " + position_of + ")";
    }
    if (func_name == "overlap_box") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string size   = find_named_arg_value(qcall.named_args, "size", emit_arg);
        return "cactus::runtime::entt_backend::query_overlap_box(" + view + ", (" + center + "), (" + size + "), " +
               position_of + ")";
    }
    if (func_name == "overlap_circle") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string radius = find_named_arg_value(qcall.named_args, "radius", emit_arg);
        return "cactus::runtime::entt_backend::query_overlap_circle(" + view + ", (" + center + "), (" + radius +
               "), " + position_of + ")";
    }
    if (func_name == "raycast") {
        const std::string origin   = find_named_arg_value(qcall.named_args, "origin", emit_arg);
        const std::string dir      = find_named_arg_value(qcall.named_args, "dir", emit_arg);
        const std::string max_dist = find_named_arg_value(qcall.named_args, "max_dist", emit_arg);
        return "cactus::runtime::entt_backend::query_raycast(" + view + ", (" + origin + "), (" + dir + "), (" +
               max_dist + "), " + position_of + ")";
    }
    return "/* unsupported std.physics.flat.query func: " + func_name + " */";
}

static std::string lower_volume_spatial_query(const QueryCallExpr& qcall,
                                              const std::string& func_name,
                                              const auto& emit_arg,
                                              const DecoratedProgram& program) {
    const std::string wt          = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
    const std::string view        = "registry.view" + build_view_suffix(qcall.filters, wt, &program);
    const std::string position_of = "[&](entt::entity e) { return registry.get<" + wt + ">(e).position; }";
    if (func_name == "nearest") {
        const std::string from = find_named_arg_value(qcall.named_args, "from", emit_arg);
        return "cactus::runtime::entt_backend::query_nearest(" + view + ", (" + from + "), " + position_of + ")";
    }
    if (func_name == "overlap_box") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string size   = find_named_arg_value(qcall.named_args, "size", emit_arg);
        return "cactus::runtime::entt_backend::query_overlap_box(" + view + ", (" + center + "), (" + size + "), " +
               position_of + ")";
    }
    if (func_name == "overlap_sphere") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string radius = find_named_arg_value(qcall.named_args, "radius", emit_arg);
        return "cactus::runtime::entt_backend::query_overlap_sphere(" + view + ", (" + center + "), (" + radius +
               "), " + position_of + ")";
    }
    if (func_name == "raycast") {
        const std::string origin   = find_named_arg_value(qcall.named_args, "origin", emit_arg);
        const std::string dir      = find_named_arg_value(qcall.named_args, "dir", emit_arg);
        const std::string max_dist = find_named_arg_value(qcall.named_args, "max_dist", emit_arg);
        return "cactus::runtime::entt_backend::query_raycast(" + view + ", (" + origin + "), (" + dir + "), (" +
               max_dist + "), " + position_of + ")";
    }
    return "/* unsupported std.physics.volume.query func: " + func_name + " */";
}

// Computes the sibling-local (z_index, creation_ordinal) stacking order used by
// ComputedLayout.draw_order (design.md decision #7): the traversal is recomputed
// natively per call rather than exposed as a generic sortable hierarchy query, so
// std.query stays free of widget-specific z-index policy while the authored
// ArrangeUi rule still assigns the result through an ordinary project statement.
static std::string lower_ui_query_call(const QueryCallExpr& qcall,
                                       const std::string& func_name,
                                       const auto& emit_arg,
                                       const DecoratedProgram& program) {
    if (func_name == "stacking_order") {
        const std::string of_expr    = find_named_arg_value(qcall.named_args, "of", emit_arg);
        const std::string node_cpp   = EnttCodegenUtils::trait_cpp_name("std.ui.Node", program);
        const std::string parent_cpp = EnttCodegenUtils::trait_cpp_name("Parent", program);
        const std::string requested  = gen_temp_name("stacking_requested", qcall.location);
        return "[&]{ const auto " + requested + " = (" + of_expr +
               "); std::vector<std::pair<std::uint64_t, entt::entity>> creation_ordered; for (auto e : "
               "registry.view<" +
               node_cpp +
               ">()) creation_ordered.emplace_back(registry.get<"
               "cactus::runtime::entt_backend::CreationOrdinal>(e).value, e); "
               "std::ranges::sort(creation_ordered); std::unordered_set<entt::entity> matching; "
               "matching.reserve(creation_ordered.size()); for (const auto& [ordinal, e] : creation_ordered) { "
               "(void)ordinal; matching.insert(e); } std::unordered_map<entt::entity, std::vector<entt::entity>> "
               "children; std::vector<entt::entity> roots; roots.reserve(creation_ordered.size()); for (const auto& "
               "[ordinal, e] : creation_ordered) { (void)ordinal; const auto* relation = registry.try_get<" +
               parent_cpp +
               ">(e); if (relation != nullptr && registry.valid(relation->parent) && "
               "matching.contains(relation->parent)) children[relation->parent].push_back(e); else "
               "roots.push_back(e); } for (auto& [parent_entity, kids] : children) { (void)parent_entity; "
               "std::ranges::sort(kids, [&](entt::entity left, entt::entity right) { const auto left_z = "
               "registry.get<" +
               node_cpp + ">(left).z_index; const auto right_z = registry.get<" + node_cpp +
               ">(right).z_index; if (left_z != right_z) return left_z < right_z; return "
               "registry.get<cactus::runtime::entt_backend::CreationOrdinal>(left).value < "
               "registry.get<cactus::runtime::entt_backend::CreationOrdinal>(right).value; }); } "
               "std::vector<entt::entity> order; order.reserve(creation_ordered.size()); "
               "std::unordered_set<entt::entity> visited; visited.reserve(creation_ordered.size()); auto visit = "
               "[&](auto&& self, entt::entity e) -> void { if (!visited.insert(e).second) return; "
               "order.push_back(e); if (const auto it = children.find(e); it != children.end()) for (const auto "
               "child : it->second) self(self, child); }; for (const auto root : roots) visit(visit, root); int "
               "index = 0; for (const auto e : order) { if (e == " +
               requested + ") return index; ++index; } return 0; }()";
    }
    return "/* unsupported std.ui func: " + func_name + " */";
}

// Reconstructs "a.b.c" from nested MemberExpr/IdentExpr chains.
static std::string expr_to_dotted_path(const ExprNode& expr) {
    if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
        return ident->name;
    }
    if (const auto* mem = std::get_if<MemberExpr>(&expr.expr)) {
        const auto obj = expr_to_dotted_path(*mem->object);
        if (obj.empty()) {
            return "";
        }
        return obj + "." + mem->member;
    }
    return "";
}

static std::string lower_query_call_expr(const QueryCallExpr& qcall,
                                         const DecoratedProgram& program,
                                         const auto& emit_arg) {
    auto lower_by_module = [&](const std::string& module, const std::string& func_name) -> std::string {
        if (module == "std.query") {
            return lower_ecs_query_call(qcall, func_name, emit_arg, program);
        }
        if (module == "std.physics.flat.query") {
            return lower_flat_spatial_query(qcall, func_name, emit_arg, program);
        }
        if (module == "std.physics.volume.query") {
            return lower_volume_spatial_query(qcall, func_name, emit_arg, program);
        }
        if (module == "std.ui") {
            return lower_ui_query_call(qcall, func_name, emit_arg, program);
        }
        return {};
    };

    if (!qcall.resolved_callee_id.has_value()) {
        // Fallback: infer module from UseNode declarations when semantic resolution was unavailable.
        if (qcall.callee != nullptr && program.ast != nullptr) {
            if (const auto* member = std::get_if<MemberExpr>(&qcall.callee->expr)) {
                const std::string& func_name = member->member;
                for (const auto& decl : program.ast->declarations) {
                    const auto* use_node = std::get_if<UseNode>(&decl);
                    if (use_node == nullptr) {
                        continue;
                    }
                    bool matches = false;
                    if (use_node->alias.has_value()) {
                        if (const auto* ident = std::get_if<IdentExpr>(&member->object->expr)) {
                            matches = (ident->name == *use_node->alias);
                        }
                    } else {
                        matches = (expr_to_dotted_path(*member->object) == use_node->module_name);
                    }
                    if (matches) {
                        const auto result = lower_by_module(use_node->module_name, func_name);
                        if (!result.empty()) {
                            return result;
                        }
                    }
                }
            }
        }
        return "/* unresolved query call */";
    }
    const std::string& module    = qcall.resolved_callee_id->module.name;
    const std::string& func_name = qcall.resolved_callee_id->local_name;
    const auto result            = lower_by_module(module, func_name);
    if (!result.empty()) {
        return result;
    }
    return "/* unrecognized query module: " + module + " */";
}

static std::string rewrite_expr(  // NOLINT(readability-function-cognitive-complexity) -- still 250 after table-driving
                                  // the input/camera/transform builtin dispatch (task 5.4); this is the full
                                  // expression-rewrite dispatch, dozens of unrelated arms remain
    const ExprNode& expr,
    const std::vector<std::string>& trait_names,
    const DecoratedProgram& program,
    const std::unordered_set<std::string>& pointer_aliases,
    const std::unordered_map<std::string, std::string>& cpp_overrides,
    const PairCodegenScope* pair_scope,
    const LocalNumericKinds* local_kinds) {
    auto known_fields = collect_trait_fields(trait_names, program);

    return std::visit(
        [&](auto& e) -> std::string {  // NOLINT(readability-function-cognitive-complexity) -- still 198, same reason
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::String) {
                    return "\"" + e.value + "\"";
                }
                if (e.kind == LiteralExpr::Kind::Float) {
                    return e.value + "F";
                }
                if (e.kind == LiteralExpr::Kind::HexColor) {
                    std::string hex = e.value;
                    if (hex.size() == 6) {
                        hex += "FF";
                    }
                    if (hex.size() == 8) {
                        auto byte = [&](size_t offset) {
                            return std::to_string(std::stoi(hex.substr(offset, 2), nullptr, 16));
                        };
                        return "Color{.r = " + byte(0) + ", .g = " + byte(2) + ", .b = " + byte(4) +
                               ", .a = " + byte(6) + "}";
                    }
                }
                return e.value;
            } else if constexpr (std::is_same_v<E, SelfExpr>) {
                return "entity";
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                if (is_input_action_name(program, e.name)) {
                    return input_action_constant_name(e.name);
                }
                // If it's a known trait field, qualify it with the canonical variable name
                if (known_fields.contains(e.name)) {
                    auto comp = find_comp_for_field(e.name, trait_names, program);
                    if (!comp.empty()) {
                        const auto ovr = cpp_overrides.find(comp);
                        const auto& cpp =
                            ovr != cpp_overrides.end() ? ovr->second : EnttCodegenUtils::trait_cpp_name(comp, program);
                        return cpp + "_comp." + e.name;
                    }
                }
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                std::string op = e.op;
                if (op == "and") {
                    op = "&&";
                } else if (op == "or") {
                    op = "||";
                }
                std::string left_text = rewrite_expr(
                    *e.left, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                std::string right_text = rewrite_expr(
                    *e.right, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                // Cactus's type checker allows mixed int/float arithmetic freely,
                // but the emitted C++ must not — an unwrapped int operand next to
                // a float one trips clang-tidy's bugprone-narrowing-conversions.
                // Only cast when BOTH sides positively resolve to differing
                // numeric kinds; an Unknown side never triggers a cast, so an
                // all-int computation the inference merely failed to fully
                // resolve is never silently turned into float arithmetic.
                static const std::unordered_set<std::string> kArithmeticOps{"+", "-", "*", "/", "%"};
                if (kArithmeticOps.contains(op)) {
                    const auto left_kind  = infer_numeric_kind(*e.left, trait_names, program, local_kinds);
                    const auto right_kind = infer_numeric_kind(*e.right, trait_names, program, local_kinds);
                    if (left_kind == NumericKind::Int && right_kind == NumericKind::Float) {
                        left_text = "static_cast<float>(" + left_text + ")";
                    } else if (right_kind == NumericKind::Int && left_kind == NumericKind::Float) {
                        right_text = "static_cast<float>(" + right_text + ")";
                    }
                }
                return "(" + left_text + " " + op + " " + right_text + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") {
                    op = "!";
                }
                return op +
                       rewrite_expr(
                           *e.operand, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                if (auto* ident = std::get_if<IdentExpr>(&e.callee->expr);
                    ident != nullptr && ident->name == "exists" && e.args.size() == 1) {
                    return "registry.valid(" +
                           rewrite_expr(*e.args[0],
                                        trait_names,
                                        program,
                                        pointer_aliases,
                                        cpp_overrides,
                                        pair_scope,
                                        local_kinds) +
                           ")";
                }
                // Module-scope stdlib call: use resolved callee identity (preferred path).
                if (e.resolved_callee_id.has_value()) {
                    auto lowered =
                        lower_resolved_stdlib_call(*e.resolved_callee_id, e.args, program, [&](const ExprNode& arg) {
                            return rewrite_expr(
                                arg, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                        });
                    if (!lowered.empty()) {
                        return lowered;
                    }
                    if (const auto* func = find_resolved_func(program, *e.resolved_callee_id);
                        func != nullptr && !func->is_extern) {
                        return canonical_to_cpp_name(*e.resolved_callee_id) + "(" +
                               join_rewritten_args(e.args,
                                                   trait_names,
                                                   program,
                                                   pointer_aliases,
                                                   cpp_overrides,
                                                   pair_scope,
                                                   local_kinds) +
                               ")";
                    }
                }
                // Bare ident call — check program.funcs module identity, then scan unaliased UseNodes.
                if (const auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
                    auto emit_stdlib = [&](const std::string& module_name) -> std::string {
                        if (module_name.empty()) {
                            return {};
                        }
                        const auto func_id =
                            make_symbol_id(SymbolKind::Func, ModuleId{.name = module_name}, ident->name);
                        return lower_resolved_stdlib_call(func_id, e.args, program, [&](const ExprNode& arg) {
                            return rewrite_expr(
                                arg, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                        });
                    };
                    const auto func_it = program.funcs.find(ident->name);
                    if (func_it != program.funcs.end()) {
                        const auto lowered = emit_stdlib(func_it->second.module_name);
                        if (!lowered.empty()) {
                            return lowered;
                        }
                    }
                    if (program.ast != nullptr) {
                        for (const auto& decl : program.ast->declarations) {
                            if (const auto* use_node = std::get_if<UseNode>(&decl);
                                use_node != nullptr && !use_node->alias.has_value()) {
                                const auto lowered = emit_stdlib(use_node->module_name);
                                if (!lowered.empty()) {
                                    return lowered;
                                }
                            }
                        }
                    }
                    // Bare DSL input-state builtins: fallback when no module resolved.
                    if (ident->name == "axis" || ident->name == "pressed" || ident->name == "down" ||
                        ident->name == "released") {
                        return "InputEvent::" + ident->name + "(" +
                               join_rewritten_args(e.args,
                                                   trait_names,
                                                   program,
                                                   pointer_aliases,
                                                   cpp_overrides,
                                                   pair_scope,
                                                   local_kinds) +
                               ")";
                    }
                }
                // Disambiguate module-alias.func() when the alias shadows a known field name
                // (e.g. `text.format(...)` where `text` is both a UseNode alias and a trait field).
                if (const auto* member_callee = std::get_if<MemberExpr>(&e.callee->expr)) {
                    if (const auto* alias_ident = std::get_if<IdentExpr>(&member_callee->object->expr)) {
                        if (program.ast != nullptr) {
                            for (const auto& decl : program.ast->declarations) {
                                if (const auto* use_node = std::get_if<UseNode>(&decl)) {
                                    if (use_node->alias.has_value() && *use_node->alias == alias_ident->name) {
                                        const auto func_id = make_symbol_id(SymbolKind::Func,
                                                                            ModuleId{.name = use_node->module_name},
                                                                            member_callee->member);
                                        const auto lowered = lower_resolved_stdlib_call(
                                            func_id, e.args, program, [&](const ExprNode& arg) {
                                                return rewrite_expr(arg,
                                                                    trait_names,
                                                                    program,
                                                                    pointer_aliases,
                                                                    cpp_overrides,
                                                                    pair_scope,
                                                                    local_kinds);
                                            });
                                        if (!lowered.empty()) {
                                            return lowered;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (const auto* member = std::get_if<MemberExpr>(&e.callee->expr)) {
                    if (const auto* object = std::get_if<IdentExpr>(&member->object->expr)) {
                        for (const auto& spec : builtin_member_call_specs()) {
                            if (object->name == spec.object_name && member->member == spec.member_name &&
                                (!spec.required_arg_count.has_value() || e.args.size() == *spec.required_arg_count)) {
                                return spec.call_body(join_rewritten_args(e.args,
                                                                          trait_names,
                                                                          program,
                                                                          pointer_aliases,
                                                                          cpp_overrides,
                                                                          pair_scope,
                                                                          local_kinds));
                            }
                        }
                    }
                }
                if (const auto* ident = std::get_if<IdentExpr>(&e.callee->expr);
                    ident != nullptr && (ident->name == "vec2" || ident->name == "vec3")) {
                    std::string args;
                    for (size_t i = 0; i < e.args.size(); ++i) {
                        if (i > 0) {
                            args += ", ";
                        }
                        args += rewrite_vec_arg(
                            *e.args[i], trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                    }
                    return ident->name + "(" + args + ")";
                }
                return rewrite_expr(
                           *e.callee, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds) +
                       "(" +
                       join_rewritten_args(
                           e.args, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds) +
                       ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                // Pair-bound member chain (e.g. `body.tf.WorldTransform.position`):
                // consume leading segments against the binding's resolved trait
                // namespace using the same longest-prefix rule as the semantic
                // analyzer's resolve_pair_member_chain, then fetch the trait as
                // const (pair-bound traits are read-only) and append the
                // remaining field path unchanged.
                if (pair_scope != nullptr) {
                    std::vector<std::string> segments{e.member};
                    const ExprNode* cursor = e.object.get();
                    while (const auto* chain_member = std::get_if<MemberExpr>(&cursor->expr)) {
                        segments.push_back(chain_member->member);
                        cursor = chain_member->object.get();
                    }
                    std::ranges::reverse(segments);
                    if (const auto* root = std::get_if<IdentExpr>(&cursor->expr)) {
                        if (const auto* binding = pair_scope->find(root->name)) {
                            for (std::size_t len = std::min<std::size_t>(2, segments.size()); len >= 1; --len) {
                                std::string key;
                                for (std::size_t i = 0; i < len; ++i) {
                                    if (i != 0) {
                                        key += '.';
                                    }
                                    key += segments[i];
                                }
                                auto found = binding->traits.find(key);
                                if (found == binding->traits.end()) {
                                    continue;
                                }
                                std::string access =
                                    "registry.get<const " + found->second.cpp_type + ">(" + root->name + ")";
                                for (std::size_t i = len; i < segments.size(); ++i) {
                                    access += "." + segments[i];
                                }
                                return access;
                            }
                        }
                    }
                }
                // Three-level access module.EnumName.Variant: emit CanonicalEnum::Variant.
                if (const auto* enum_member = std::get_if<MemberExpr>(&e.object->expr)) {
                    if (std::get_if<IdentExpr>(&enum_member->object->expr) != nullptr &&
                        EnttCodegenUtils::find_enum(program, enum_member->member) != nullptr) {
                        return EnttCodegenUtils::enum_cpp_name(enum_member->member, program) + "::" + e.member;
                    }
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    // Enum names — use :: notation with canonical C++ name
                    if (EnttCodegenUtils::find_enum(program, ident->name) != nullptr) {
                        return EnttCodegenUtils::enum_cpp_name(ident->name, program) + "::" + e.member;
                    }
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (pointer_aliases.contains(ident->name)) {
                        return ident->name + "->" + e.member;
                    }
                }
                return rewrite_expr(*e.object, trait_names, program, pointer_aliases, cpp_overrides, pair_scope) + "." +
                       e.member;
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                return emit_spawn_expression(e, trait_names, program, pointer_aliases, pair_scope);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                std::string result = "std::vector{";
                for (size_t i = 0; i < e.elements.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result +=
                        rewrite_expr(*e.elements[i], trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
                }
                result += "}";
                return result;
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                return lower_query_call_expr(e, program, [&](const ExprNode& arg) {
                    return rewrite_expr(arg, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
                });
            } else {
                return "/* unsupported expr */";
            }
        },
        expr.expr);
}

// ── Rewrite statement: replace field[i] = with comp.field = ─────────────────

static std::string emit_trait_match_stmt(const TraitMatchStmt& match_stmt,
                                         int indent,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases,
                                         bool dispatcher_available,
                                         const std::unordered_map<std::string, std::string>& cpp_overrides,
                                         const PairCodegenScope* pair_scope,
                                         const LexicalLocalBindings& lexical_locals,
                                         const LocalNumericKinds& local_kinds) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::ostringstream out;

    // A user's DSL identifiers can never contain the "cactus_gen_" namespace prefix used by
    // gen_temp_name, so the entity temporary below cannot collide with a subject expression that
    // happens to be a bare identifier (see the target/parent self-reference class of bug).
    const std::string match_entity = gen_temp_name("match_entity", match_stmt.location);

    out << ind << "{\n";
    out << ind << "    auto " << match_entity << " = "
        << rewrite_expr(
               *match_stmt.subject, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, &local_kinds)
        << ";\n";
    out << ind << "    if (registry.valid(" << match_entity << ")) {\n";

    bool first = true;
    for (const auto& arm : match_stmt.arms) {
        const std::string cpp_arm = EnttCodegenUtils::trait_cpp_name(arm.resolved_trait_id, arm.trait_name, program);
        const auto simple_name    = arm.trait_name.rfind('.') != std::string::npos
                                        ? arm.trait_name.substr(arm.trait_name.rfind('.') + 1)
                                        : arm.trait_name;
        const auto* TRAIT_INFO    = EnttCodegenUtils::find_trait(
            program, arm.resolved_trait_id.has_value() ? make_canonical_id(*arm.resolved_trait_id) : simple_name);
        const bool IS_MARKER                        = TRAIT_INFO == nullptr || TRAIT_INFO->fields.empty();
        std::unordered_set<std::string> arm_aliases = pointer_aliases;
        auto arm_locals                             = lexical_locals;
        auto arm_kinds                              = local_kinds;

        out << ind << "        " << (first ? "if" : "else if") << " (";
        if (IS_MARKER) {
            out << "registry.all_of<" << cpp_arm << ">(" << match_entity << ")) {\n";
        } else {
            const std::string ALIAS = arm.alias.value_or(gen_temp_name("match_" + cpp_arm, arm.location));
            arm_aliases.insert(ALIAS);
            arm_locals.insert(ALIAS);
            out << "auto* " << ALIAS << " = registry.try_get<" << cpp_arm << ">(" << match_entity << ")) {\n";
        }

        out << rewrite_stmt_block(arm.body,
                                  indent + 3,
                                  trait_names,
                                  program,
                                  arm_aliases,
                                  dispatcher_available,
                                  cpp_overrides,
                                  pair_scope,
                                  arm_locals,
                                  arm_kinds);
        out << ind << "        }";
        first = false;
        if (!first || arm.location.line >= 0) {
            out << "\n";
        }
    }

    if (match_stmt.wildcard.has_value()) {
        out << ind << "        " << (first ? "if (true)" : "else") << " {\n";
        auto wildcard_locals = lexical_locals;
        auto wildcard_kinds  = local_kinds;
        out << rewrite_stmt_block(match_stmt.wildcard->body,
                                  indent + 3,
                                  trait_names,
                                  program,
                                  pointer_aliases,
                                  dispatcher_available,
                                  cpp_overrides,
                                  pair_scope,
                                  wildcard_locals,
                                  wildcard_kinds);
        out << ind << "        }\n";
    }

    out << ind << "    }\n";
    out << ind << "}\n";
    return out.str();
}

static std::string emit_emit_stmt(const EmitStmt& s,
                                  int indent,
                                  const std::vector<std::string>& trait_names,
                                  const DecoratedProgram& program,
                                  const std::unordered_set<std::string>& pointer_aliases,
                                  bool dispatcher_available,
                                  const std::unordered_map<std::string, std::string>& cpp_overrides,
                                  const PairCodegenScope* pair_scope) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const bool graph_runtime = !program.execution_graph.phases.empty();
    // event_name is now dotted for cross-module events (e.g. "std.debug.DrawDebugLine2D"),
    // so the resolved symbol must drive the lookup — the string overload only matches a
    // bare declared name and would silently fall through to an invalid C++ type name.
    const auto event_type = s.resolved_event_id.has_value() ? event_cpp_type(*s.resolved_event_id, program)
                                                            : event_cpp_type(s.event_name, program);
    std::string payload;
    for (size_t i = 0; i < s.payload.size(); ++i) {
        if (i > 0) {
            payload += ", ";
        }
        payload += "." + s.payload[i].name + " = " +
                   rewrite_expr(*s.payload[i].value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
    }

    if (!s.target.has_value()) {
        std::string emit_call;
        if (graph_runtime) {
            emit_call = "cactus::runtime::entt_backend::generated_emit_event(" + event_type + "{";
        } else if (dispatcher_available) {
            emit_call = "dispatcher.trigger(" + event_type + "{";
        } else {
            // event_type (already computed above) is a valid, collision-resistant C++
            // identifier; event_name may be dotted for cross-module events and cannot be
            // used directly to build an identifier.
            emit_call = event_type + "_buffer.push_back({";
        }
        return ind + emit_call + payload + "});\n";
    }

    // Targeted emit: evaluate the target exactly once and, on the
    // graph-runtime path, carry its identity into the queued
    // occurrence (targeted-event-delivery) rather than lowering to
    // a validity guard around broadcast dispatch. The legacy
    // dispatcher/buffer paths (unused by the modern runtime) keep
    // their existing validity-guarded shape, since they have no
    // recipient-aware delivery mechanism to carry a target through.
    const std::string TARGET =
        rewrite_expr(**s.target, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
    if (graph_runtime) {
        // TARGET is arbitrary, rewritten DSL text (e.g. a bare identifier such as
        // `target`) — the recipient temporary must use a name that can never collide
        // with it, or the declaration below becomes self-referential.
        const std::string RECIPIENT = gen_temp_name("emit_target", s.location);
        return ind + "{\n" + ind + "    const auto " + RECIPIENT + " = " + TARGET + ";\n" + ind +
               "    if (registry.valid(" + RECIPIENT + ")) {\n" + ind +
               "        cactus::runtime::entt_backend::generated_emit_targeted_event(" + event_type + "{" + payload +
               "}, " + RECIPIENT + ");\n" + ind + "    }\n" + ind + "}\n";
    }
    std::string emit_call =
        dispatcher_available ? "dispatcher.trigger(" + event_type + "{" : event_type + "_buffer.push_back({";
    return ind + "if (registry.valid(" + TARGET + ")) {\n" + ind + "    " + emit_call + payload + "});\n" + ind + "}\n";
}

static std::string emit_spawn_stmt(const SpawnStmt& s,
                                   int indent,
                                   const std::vector<std::string>& trait_names,
                                   const DecoratedProgram& program,
                                   const std::unordered_set<std::string>& pointer_aliases,
                                   const PairCodegenScope* pair_scope) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const SymbolId tmpl_id = s.resolved_template_id.has_value()
                                 ? *s.resolved_template_id
                                 : make_symbol_id(SymbolKind::Template, program.module_name, s.template_name);
    if (!s.child_overrides.empty()) {
        if (const auto* children = find_template_children(program, tmpl_id)) {
            return ind +
                   emit_hierarchical_spawn_expansion(tmpl_id,
                                                     s.overrides,
                                                     s.child_overrides,
                                                     *children,
                                                     trait_names,
                                                     program,
                                                     pointer_aliases,
                                                     s.location,
                                                     pair_scope) +
                   ";\n";
        }
    }
    std::ostringstream result;
    const std::string spawned_name = gen_temp_name("spawned", s.location);
    result << ind << "{\n";
    if (!program.execution_graph.phases.empty()) {
        result << ind << "    auto " << spawned_name
               << " = cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        result << ind << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Spawn,\n";
        result << ind << "        [=](entt::registry& registry) mutable {\n";
        result << ind << "            " << archetype_create_at_function_name(tmpl_id, program) << "(registry, "
               << spawned_name << ");\n";
        result << emit_spawn_overrides(
            spawned_name, s.overrides, indent + 3, trait_names, program, pointer_aliases, pair_scope);
        result << ind << "        });\n";
    } else {
        result << ind << "    auto " << spawned_name << " = " << archetype_create_function_name(tmpl_id, program)
               << "(registry);\n";
        result << emit_spawn_overrides(
            spawned_name, s.overrides, indent + 1, trait_names, program, pointer_aliases, pair_scope);
    }
    result << ind << "}\n";
    return result.str();
}

static std::string emit_add_trait_stmt(const AddTraitStmt& s,
                                       int indent,
                                       const std::vector<std::string>& trait_names,
                                       const DecoratedProgram& program,
                                       const std::unordered_set<std::string>& pointer_aliases,
                                       const std::unordered_map<std::string, std::string>& cpp_overrides,
                                       const PairCodegenScope* pair_scope) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::string target =
        s.target_expr.has_value()
            ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope)
            : "entity";
    const bool GUARDED    = s.target_expr.has_value();
    const std::string cpp = EnttCodegenUtils::trait_cpp_name(s.resolved_trait_id, s.trait_name, program);
    // `target` above is arbitrary rewritten DSL text; the generated temporaries must
    // use names that cannot collide with it or with each other (same class of bug as
    // the reserved-double-underscore names these replace).
    const std::string target_name   = gen_temp_name("target", s.location);
    const std::string existing_name = gen_temp_name("existing", s.location);
    const std::string value_name    = gen_temp_name("value", s.location);
    if (!program.execution_graph.phases.empty()) {
        std::ostringstream result;
        result << ind << "{\n";
        result << ind << "    const auto " << target_name << " = " << target << ";\n";
        result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        result << ind << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Add,\n";
        result << ind << "        [=](entt::registry& registry) mutable {\n";
        result << ind << "            if (!registry.valid(" << target_name << ")) { return; }\n";
        result << ind << "            cancel_projected_" << cpp << "(" << target_name << ");\n";
        if (s.args.empty()) {
            result << ind << "            registry.emplace_or_replace<" << cpp << ">(" << target_name << ");\n";
        } else {
            result << ind << "            auto " << existing_name << " = registry.try_get<" << cpp << ">("
                   << target_name << ");\n";
            result << ind << "            auto " << value_name << " = " << existing_name << " ? *" << existing_name
                   << " : " << cpp << "{};\n";
            for (const auto& arg : s.args) {
                result << ind << "            " << value_name << "." << arg.name << " = "
                       << rewrite_expr(*arg.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope)
                       << ";\n";
            }
            result << ind << "            registry.emplace_or_replace<" << cpp << ">(" << target_name << ", "
                   << value_name << ");\n";
        }
        result << ind << "        });\n";
        result << ind << "}\n";
        return result.str();
    }
    if (s.args.empty()) {
        if (GUARDED) {
            return ind + "if (registry.valid(" + target + ")) {\n" + ind + "    cancel_projected_" + cpp + "(" +
                   target + ");\n" + ind + "    registry.emplace_or_replace<" + cpp + ">(" + target + ");\n" + ind +
                   "}\n";
        }
        return ind + "cancel_projected_" + cpp + "(" + target + ");\n" + ind + "registry.emplace_or_replace<" + cpp +
               ">(" + target + ");\n";
    }

    std::ostringstream result;
    if (GUARDED) {
        result << ind << "if (registry.valid(" << target << ")) {\n";
    }
    result << ind << (GUARDED ? "    " : "") << "{\n";
    result << ind << (GUARDED ? "        " : "    ") << "cancel_projected_" << cpp << "(" << target << ");\n";
    result << ind << (GUARDED ? "        " : "    ") << "auto " << existing_name << " = registry.try_get<" << cpp
           << ">(" << target << ");\n";
    result << ind << (GUARDED ? "        " : "    ") << "auto " << value_name << " = " << existing_name << " ? *"
           << existing_name << " : " << cpp << "{};\n";
    for (const auto& arg : s.args) {
        result << ind << (GUARDED ? "        " : "    ") << value_name << "." << arg.name << " = "
               << rewrite_expr(*arg.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope) << ";\n";
    }
    result << ind << (GUARDED ? "        " : "    ") << "registry.emplace_or_replace<" << cpp << ">(" << target << ", "
           << value_name << ");\n";
    result << ind << (GUARDED ? "    " : "") << "}\n";
    if (GUARDED) {
        result << ind << "}\n";
    }
    return result.str();
}

static std::string emit_remove_trait_stmt(const RemoveTraitStmt& s,
                                          int indent,
                                          const std::vector<std::string>& trait_names,
                                          const DecoratedProgram& program,
                                          const std::unordered_set<std::string>& pointer_aliases,
                                          const std::unordered_map<std::string, std::string>& cpp_overrides,
                                          const PairCodegenScope* pair_scope) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::string target =
        s.target_expr.has_value()
            ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope)
            : "entity";
    const std::string cpp = EnttCodegenUtils::trait_cpp_name(s.resolved_trait_id, s.trait_name, program);
    if (!program.execution_graph.phases.empty()) {
        const std::string target_name = gen_temp_name("target", s.location);
        return ind + "{\n" + ind + "    const auto " + target_name + " = " + target + ";\n" + ind +
               "    cactus::runtime::entt_backend::generated_queue_structural_command(\n" + ind +
               "        cactus::runtime::entt_backend::StructuralCommand::Kind::Remove,\n" + ind +
               "        [=](entt::registry& registry) {\n" + ind + "            if (!registry.valid(" + target_name +
               ")) { return; }\n" + ind + "            cancel_projected_" + cpp + "(" + target_name + ");\n" + ind +
               "            if (registry.all_of<" + cpp + ">(" + target_name + ")) {\n" + ind +
               "                registry.remove<" + cpp + ">(" + target_name + ");\n" + ind + "            }\n" + ind +
               "        });\n" + ind + "}\n";
    }
    if (s.target_expr.has_value()) {
        return ind + "if (registry.valid(" + target + ")) {\n" + ind + "    cancel_projected_" + cpp + "(" + target +
               ");\n" + ind + "    if (registry.all_of<" + cpp + ">(" + target + ")) {\n" + ind +
               "        registry.remove<" + cpp + ">(" + target + ");\n" + ind + "    }\n" + ind + "}\n";
    }
    return ind + "cancel_projected_" + cpp + "(" + target + ");\n" + ind + "if (registry.all_of<" + cpp + ">(" +
           target + ")) {\n" + ind + "    registry.remove<" + cpp + ">(" + target + ");\n" + ind + "}\n";
}

static std::string emit_project_trait_stmt(const ProjectTraitStmt& s,
                                           int indent,
                                           const std::vector<std::string>& trait_names,
                                           const DecoratedProgram& program,
                                           const std::unordered_set<std::string>& pointer_aliases,
                                           const std::unordered_map<std::string, std::string>& cpp_overrides,
                                           const PairCodegenScope* pair_scope) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const std::string target =
        s.target_expr.has_value()
            ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope)
            : "entity";
    const std::string cpp = EnttCodegenUtils::trait_cpp_name(s.resolved_trait_id, s.trait_name, program);
    const auto simple =
        s.trait_name.rfind('.') != std::string::npos ? s.trait_name.substr(s.trait_name.rfind('.') + 1) : s.trait_name;
    const auto* resolved_pt = s.resolved_trait_id.has_value()
                                  ? EnttCodegenUtils::find_trait(program, make_canonical_id(*s.resolved_trait_id))
                                  : EnttCodegenUtils::find_trait(program, simple);
    const bool is_marker    = resolved_pt == nullptr || resolved_pt->fields.empty();
    std::ostringstream result;
    result << ind << "if (registry.valid(" << target << ")) {\n";
    if (is_marker) {
        result << ind << "    project_" << cpp << "(registry, " << target << ");\n";
    } else {
        const std::string projected_name = gen_temp_name("projected", s.location);
        result << ind << "    [[maybe_unused]] auto& " << projected_name << " = project_" << cpp << "(registry, "
               << target << ");\n";
        for (const auto& arg : s.args) {
            result << ind << "    " << projected_name << "." << arg.name << " = "
                   << rewrite_expr(*arg.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope)
                   << ";\n";
        }
    }
    result << ind << "}\n";
    return result.str();
}

// range(begin, end, step=1): the sole sanctioned counted-iteration form
// (dsl-bounded-foreach). Lowers to a native counting loop instead of the
// generic ForeachStmt path's `auto temp = <iterable>; for (const auto& v :
// temp)` — no list[T]/std::vector snapshot is constructed. begin/end/step are
// each emitted into a `const` local exactly once, giving "evaluated once" for
// free from ordinary C++ scoping. The `if (step > 0) {...} else if (step < 0)
// {...}` shape (no unconditional trailing branch) makes a runtime step of 0
// total: it falls through both arms and the loop body runs zero times,
// regardless of begin/end, rather than hanging or needing a separate
// zero-step check.
static std::string emit_range_foreach_stmt(const ForeachStmt& s,
                                           const CallExpr& range_call,
                                           int indent,
                                           const std::vector<std::string>& trait_names,
                                           const DecoratedProgram& program,
                                           const std::unordered_set<std::string>& pointer_aliases,
                                           bool dispatcher_available,
                                           const std::unordered_map<std::string, std::string>& cpp_overrides,
                                           const PairCodegenScope* pair_scope,
                                           LexicalLocalBindings* lexical_locals,
                                           LocalNumericKinds* local_kinds) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const std::string ind1(static_cast<size_t>(indent + 1) * 4, ' ');
    const std::string ind2(static_cast<size_t>(indent + 2) * 4, ' ');

    const auto begin_name = gen_temp_name("range_begin", s.location);
    const auto end_name   = gen_temp_name("range_end", s.location);
    const auto step_name  = gen_temp_name("range_step", s.location);
    const auto begin_text = rewrite_expr(
        *range_call.args[0], trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
    const auto end_text = rewrite_expr(
        *range_call.args[1], trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
    const auto step_text =
        range_call.args.size() == 3
            ? rewrite_expr(
                  *range_call.args[2], trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds)
            : "1";

    std::string result = ind + "{\n";
    result += ind1 + "const int " + begin_name + " = " + begin_text + ";\n";
    result += ind1 + "const int " + end_name + " = " + end_text + ";\n";
    result += ind1 + "const int " + step_name + " = " + step_text + ";\n";

    const auto emit_direction = [&](const std::string& compare_op) {
        auto range_locals = clone_or_empty(lexical_locals);
        auto range_kinds  = clone_or_empty(local_kinds);
        range_locals.insert(s.var_name);
        range_kinds[s.var_name] = NumericKind::Int;
        std::string branch      = ind2 + "for (int " + s.var_name + " = " + begin_name + "; " + s.var_name + " " +
                                  compare_op + " " + end_name + "; " + s.var_name + " += " + step_name + ") {\n";
        branch += rewrite_stmt_block(s.body,
                                     indent + 3,
                                     trait_names,
                                     program,
                                     pointer_aliases,
                                     dispatcher_available,
                                     cpp_overrides,
                                     pair_scope,
                                     range_locals,
                                     range_kinds);
        branch += ind2 + "}\n";
        return branch;
    };

    result += ind1 + "if (" + step_name + " > 0) {\n";
    result += emit_direction("<");
    result += ind1 + "} else if (" + step_name + " < 0) {\n";
    result += emit_direction(">");
    result += ind1 + "}\n";
    result += ind + "}\n";
    return result;
}

// Still 73 after extracting emit_emit_stmt/emit_spawn_stmt/emit_add_trait_stmt/
// emit_remove_trait_stmt/emit_project_trait_stmt (task 6.7); the visit lambda's
// own nesting is counted into the enclosing function too.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static std::string rewrite_stmt(const StmtNode& stmt,
                                int indent,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases,
                                bool dispatcher_available,
                                const std::unordered_map<std::string, std::string>& cpp_overrides,
                                const PairCodegenScope* pair_scope,
                                LexicalLocalBindings* lexical_locals,
                                LocalNumericKinds* local_kinds) {
    auto known_fields = collect_trait_fields(trait_names, program);
    std::string ind(static_cast<size_t>(indent) * 4, ' ');

    return std::visit(
        // Still 57 after task 6.7's extraction; remaining branches (LetStmt/VarAssign/
        // DestroyStmt/ReturnStmt/ExprStmt/IfStmt/TraitMatchStmt/ForeachStmt) are the
        // exhaustive AST-dispatch arms, an inherent shape.
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [&](auto& s) -> std::string {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, LetStmt>) {
                const auto result =
                    ind + "[[maybe_unused]] auto " + s.name + " = " +
                    rewrite_expr(
                        *s.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds) +
                    ";\n";
                if (lexical_locals != nullptr) {
                    lexical_locals->insert(s.name);
                }
                if (local_kinds != nullptr) {
                    (*local_kinds)[s.name] = infer_numeric_kind(*s.value, trait_names, program, local_kinds);
                }
                return result;
            } else if constexpr (std::is_same_v<S, VarAssign>) {
                std::string lhs;
                if (!s.path.empty()) {
                    // Dotted assignment target (`alias.field...`): reconstruct the
                    // equivalent member-access chain and lower it through the same
                    // path ordinary reads use, so `hp.health = x` resolves `hp` as
                    // the already-in-scope filter-alias/field reference instead of
                    // falling into the bare-identifier "new local" branch below,
                    // which would shadow-redeclare it.
                    ExprNode chain(ExprNode::Variant{IdentExpr{s.name, s.location}}, s.location);
                    for (const auto& segment : s.path) {
                        chain = ExprNode(
                            ExprNode::Variant{MemberExpr{
                                std::make_unique<ExprNode>(std::move(chain)), segment, std::nullopt, s.location}},
                            s.location);
                    }
                    lhs = rewrite_expr(
                        chain, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                } else if (lexical_locals != nullptr && lexical_locals->contains(s.name)) {
                    lhs = s.name;
                } else if (known_fields.contains(s.name)) {
                    auto comp = find_comp_for_field(s.name, trait_names, program);
                    if (!comp.empty()) {
                        const auto ovr = cpp_overrides.find(comp);
                        const auto& cpp =
                            ovr != cpp_overrides.end() ? ovr->second : EnttCodegenUtils::trait_cpp_name(comp, program);
                        lhs = cpp + "_comp." + s.name;
                    } else {
                        lhs = s.name;
                    }
                } else {
                    // A first bare `=` preserves the backend's existing mutable-local
                    // declaration behavior. Compound assignment is only meaningful for
                    // an already-visible lexical binding and therefore never declares.
                    if (s.op == "=") {
                        lhs = "auto " + s.name;
                        if (lexical_locals != nullptr) {
                            lexical_locals->insert(s.name);
                        }
                    } else {
                        lhs = s.name;
                    }
                }
                if (const auto* call = std::get_if<CallExpr>(&s.value->expr)) {
                    if (const auto* ident = std::get_if<IdentExpr>(&call->callee->expr);
                        ident != nullptr && ident->name == "vec2" && call->args.size() == 2) {
                        const std::string prefix = ind + lhs + " " + s.op + " vec2(";
                        const std::string continuation(prefix.size(), ' ');
                        return prefix +
                               rewrite_vec_arg(*call->args[0],
                                               trait_names,
                                               program,
                                               pointer_aliases,
                                               cpp_overrides,
                                               pair_scope,
                                               local_kinds) +
                               ",\n" + continuation +
                               rewrite_vec_arg(*call->args[1],
                                               trait_names,
                                               program,
                                               pointer_aliases,
                                               cpp_overrides,
                                               pair_scope,
                                               local_kinds) +
                               ");\n";
                    }
                }
                return ind + lhs + " " + s.op + " " +
                       rewrite_expr(
                           *s.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds) +
                       ";\n";
            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                return emit_emit_stmt(
                    s, indent, trait_names, program, pointer_aliases, dispatcher_available, cpp_overrides, pair_scope);
            } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                return emit_spawn_stmt(s, indent, trait_names, program, pointer_aliases, pair_scope);
            } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                return emit_add_trait_stmt(s, indent, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
            } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                return emit_remove_trait_stmt(
                    s, indent, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
            } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                return emit_project_trait_stmt(
                    s, indent, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
            } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                if (!program.execution_graph.phases.empty()) {
                    const std::string target      = s.target_expr.has_value() ? rewrite_expr(**s.target_expr,
                                                                                             trait_names,
                                                                                             program,
                                                                                             pointer_aliases,
                                                                                             cpp_overrides,
                                                                                             pair_scope,
                                                                                             local_kinds)
                                                                              : "entity";
                    const std::string target_name = gen_temp_name("target", s.location);
                    return ind + "{\n" + ind + "    const auto " + target_name + " = " + target + ";\n" + ind +
                           "    cactus::runtime::entt_backend::generated_queue_structural_command(\n" + ind +
                           "        cactus::runtime::entt_backend::StructuralCommand::Kind::Destroy,\n" + ind +
                           "        [=](entt::registry& registry) {\n" + ind + "            if (registry.valid(" +
                           target_name + ")) {\n" + ind + "                cactus_destroy_entity_recursive(registry, " +
                           target_name + ");\n" + ind + "            }\n" + ind + "        });\n" + ind + "}\n";
                }
                if (s.target_expr.has_value()) {
                    std::string target = rewrite_expr(
                        **s.target_expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                    return ind + "if (registry.valid(" + target + ")) {\n" + ind +
                           "    cactus_destroy_entity_recursive(registry, " + target + ");\n" + ind + "}\n";
                }
                return ind + "cactus_destroy_entity_recursive(registry, entity);\n";
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) {
                    return ind + "return " +
                           rewrite_expr(**s.value,
                                        trait_names,
                                        program,
                                        pointer_aliases,
                                        cpp_overrides,
                                        pair_scope,
                                        local_kinds) +
                           ";\n";
                }
                return ind + "return;\n";
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind +
                       rewrite_expr(
                           *s.expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds) +
                       ";\n";
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                const auto parenthesize_condition = [](const std::string& condition) {
                    if (!condition.empty() && condition.front() == '(' && condition.back() == ')') {
                        return condition;
                    }
                    return "(" + condition + ")";
                };
                const auto condition = rewrite_expr(
                    *s.condition, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                std::string result = ind + "if " + parenthesize_condition(condition) + " {\n";
                auto then_locals   = clone_or_empty(lexical_locals);
                auto then_kinds    = clone_or_empty(local_kinds);
                result += rewrite_stmt_block(s.then_body,
                                             indent + 1,
                                             trait_names,
                                             program,
                                             pointer_aliases,
                                             dispatcher_available,
                                             cpp_overrides,
                                             pair_scope,
                                             then_locals,
                                             then_kinds);
                result += ind + "}";
                for (const auto& branch : s.else_if_branches) {
                    const auto branch_condition = rewrite_expr(*branch.condition,
                                                               trait_names,
                                                               program,
                                                               pointer_aliases,
                                                               cpp_overrides,
                                                               pair_scope,
                                                               local_kinds);
                    result += " else if " + parenthesize_condition(branch_condition) + " {\n";
                    auto branch_locals = clone_or_empty(lexical_locals);
                    auto branch_kinds  = clone_or_empty(local_kinds);
                    result += rewrite_stmt_block(branch.body,
                                                 indent + 1,
                                                 trait_names,
                                                 program,
                                                 pointer_aliases,
                                                 dispatcher_available,
                                                 cpp_overrides,
                                                 pair_scope,
                                                 branch_locals,
                                                 branch_kinds);
                    result += ind + "}";
                }
                if (!s.else_body.empty()) {
                    result += " else {\n";
                    auto else_locals = clone_or_empty(lexical_locals);
                    auto else_kinds  = clone_or_empty(local_kinds);
                    result += rewrite_stmt_block(s.else_body,
                                                 indent + 1,
                                                 trait_names,
                                                 program,
                                                 pointer_aliases,
                                                 dispatcher_available,
                                                 cpp_overrides,
                                                 pair_scope,
                                                 else_locals,
                                                 else_kinds);
                    result += ind + "}";
                }
                return result + "\n";
            } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                return emit_trait_match_stmt(s,
                                             indent,
                                             trait_names,
                                             program,
                                             pointer_aliases,
                                             dispatcher_available,
                                             cpp_overrides,
                                             pair_scope,
                                             clone_or_empty(lexical_locals),
                                             clone_or_empty(local_kinds));
            } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                if (const auto* range_call = std::get_if<CallExpr>(&s.iterable->expr)) {
                    const auto* range_ident = std::get_if<IdentExpr>(&range_call->callee->expr);
                    if (range_ident != nullptr && range_ident->name == "range" &&
                        (range_call->args.size() == 2 || range_call->args.size() == 3)) {
                        return emit_range_foreach_stmt(s,
                                                       *range_call,
                                                       indent,
                                                       trait_names,
                                                       program,
                                                       pointer_aliases,
                                                       dispatcher_available,
                                                       cpp_overrides,
                                                       pair_scope,
                                                       lexical_locals,
                                                       local_kinds);
                    }
                }
                const auto temp = foreach_temp_name(s);
                std::string result =
                    ind + "auto " + temp + " = " +
                    rewrite_expr(
                        *s.iterable, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds) +
                    ";\n";
                result += ind + "for (const auto& " + s.var_name + " : " + temp + ") {\n";
                auto loop_locals = clone_or_empty(lexical_locals);
                auto loop_kinds  = clone_or_empty(local_kinds);
                loop_locals.insert(s.var_name);
                result += rewrite_stmt_block(s.body,
                                             indent + 1,
                                             trait_names,
                                             program,
                                             pointer_aliases,
                                             dispatcher_available,
                                             cpp_overrides,
                                             pair_scope,
                                             loop_locals,
                                             loop_kinds);
                result += ind + "}\n";
                return result;
            } else {
                return ind + "/* unsupported stmt */\n";
            }
        },
        stmt.stmt);
}

static std::string rewrite_stmt_block(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                      int indent,
                                      const std::vector<std::string>& trait_names,
                                      const DecoratedProgram& program,
                                      const std::unordered_set<std::string>& pointer_aliases,
                                      bool dispatcher_available,
                                      const std::unordered_map<std::string, std::string>& cpp_overrides,
                                      const PairCodegenScope* pair_scope,
                                      LexicalLocalBindings& lexical_locals,
                                      LocalNumericKinds& local_kinds) {
    std::string result;
    for (const auto& stmt : stmts) {
        result += rewrite_stmt(*stmt,
                               indent,
                               trait_names,
                               program,
                               pointer_aliases,
                               dispatcher_available,
                               cpp_overrides,
                               pair_scope,
                               &lexical_locals,
                               &local_kinds);
    }
    return result;
}

std::string EnttSystemEmitter::emit_func(const FuncNode& func, const DecoratedProgram& program) {
    if (func.is_extern || !func.resolved_func_id.has_value()) {
        return {};
    }
    const auto* resolved = find_resolved_func(program, *func.resolved_func_id);
    if (resolved == nullptr) {
        throw std::runtime_error("cpp-entt backend: missing resolved authored func '" +
                                 make_canonical_id(*func.resolved_func_id) + "'");
    }

    std::ostringstream out;
    out << (resolved->return_type.has_value() ? EnttCodegenUtils::type_to_cpp(*resolved->return_type) : "void") << " "
        << canonical_to_cpp_name(*func.resolved_func_id) << "(";
    for (std::size_t index = 0; index < resolved->params.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << EnttCodegenUtils::type_to_cpp(resolved->params[index].type) << " " << resolved->params[index].name;
    }
    out << ") {\n";
    LexicalLocalBindings locals;
    LocalNumericKinds param_kinds;
    for (const auto& param : resolved->params) {
        locals.insert(param.name);
        if (param.type.kind == TypeKind::Int) {
            param_kinds[param.name] = NumericKind::Int;
        } else if (param.type.kind == TypeKind::Float) {
            param_kinds[param.name] = NumericKind::Float;
        }
    }
    out << rewrite_stmt_block(func.body, 1, {}, program, {}, false, {}, nullptr, locals, param_kinds);
    out << "}\n\n";
    return out.str();
}

// A pair handler snapshots both bindings' live memberships up
// front (deterministic, creation-ordinal order), then iterates
// their directed Cartesian product left-binding-major without
// ever materializing the full tuple list. Component access inside
// the body is read-only (const), matching the read-only pair
// trait rule enforced by semantic analysis.
static void emit_pair_handler_body(std::ostringstream& out,
                                   const EventHandlerNode& handler,
                                   const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                   const PairCodegenScope& pair_codegen_scope,
                                   const DecoratedProgram& program) {
    for (const auto& binding : pair_binding_codegens) {
        emit_pair_binding_snapshot(out, binding, 1);
    }
    const auto& left  = pair_binding_codegens[0];
    const auto& right = pair_binding_codegens[1];
    LexicalLocalBindings pair_locals;
    for (const auto& binding : pair_binding_codegens) {
        pair_locals.insert(binding.scope.binding_name);
    }
    LocalNumericKinds pair_kinds;
    // Rendered one level deeper (5) than the innermost loop body (4) because
    // every call site below wraps it in a per-invocation lambda: a bare
    // `return` spliced directly into a raw loop body would exit this whole
    // generated function on the first tuple that hits it (see
    // dsl-pair-relations, "Early return rejects only the current tuple")
    // instead of ending just that tuple's invocation.
    const auto pair_body =
        rewrite_stmt_block(handler.body, 5, {}, program, {}, false, {}, &pair_codegen_scope, pair_locals, pair_kinds);
    const auto emit_tuple_invocation = [&out, &pair_body]() {
        out << "                [&]() {\n";
        out << pair_body;
        out << "                }();\n";
    };
    // Recipient-targeted delivery: only tuples incident to the
    // recipient run (targeted-event-delivery, "Pair target routes to
    // incident tuples"). A tuple where both bindings equal the
    // recipient is covered exactly once, by the first block below.
    // Membership is tested with all_of<> against the binding's own
    // component set (an O(1) sparse-set check) rather than scanning
    // the snapshot vector, since the snapshot was built moments
    // earlier from that same view and cannot have changed since.
    out << "    if (cactus_recipient.has_value()) {\n";
    out << "        const auto target = *cactus_recipient;\n";
    out << "        if (registry.all_of<";
    for (std::size_t i = 0; i < left.cpp_types.size(); ++i) {
        out << (i == 0 ? "" : ", ") << left.cpp_types[i];
    }
    out << ">(target)) {\n";
    out << "            auto " << left.scope.binding_name << " = target;\n";
    out << "            for (auto " << right.scope.binding_name << " : " << right.scope.binding_name
        << "_snapshot) {\n";
    emit_tuple_invocation();
    out << "            }\n";
    out << "        }\n";
    out << "        if (registry.all_of<";
    for (std::size_t i = 0; i < right.cpp_types.size(); ++i) {
        out << (i == 0 ? "" : ", ") << right.cpp_types[i];
    }
    out << ">(target)) {\n";
    out << "            for (auto " << left.scope.binding_name << " : " << left.scope.binding_name << "_snapshot) {\n";
    out << "                if (" << left.scope.binding_name << " == target) { continue; }\n";
    out << "                auto " << right.scope.binding_name << " = target;\n";
    emit_tuple_invocation();
    out << "            }\n";
    out << "        }\n";
    out << "    } else {\n";
    out << "        for (auto " << left.scope.binding_name << " : " << left.scope.binding_name << "_snapshot) {\n";
    out << "            for (auto " << right.scope.binding_name << " : " << right.scope.binding_name
        << "_snapshot) {\n";
    emit_tuple_invocation();
    out << "            }\n";
    out << "        }\n";
    out << "    }\n";
}

static void emit_selectionless_handler_body(std::ostringstream& out,
                                            const EventHandlerNode& handler,
                                            const std::vector<std::string>& filter_traits,
                                            const DecoratedProgram& program,
                                            const std::unordered_map<std::string, std::string>& filter_cpp_overrides) {
    out << "    (void)registry;\n";
    auto lexical_locals = handler_lexical_locals(handler);
    LocalNumericKinds local_kinds;
    out << rewrite_stmt_block(
        handler.body, 1, filter_traits, program, {}, false, filter_cpp_overrides, nullptr, lexical_locals, local_kinds);
}

static void emit_filtered_handler_body(std::ostringstream& out,
                                       const RuleNode& sys,
                                       const EventHandlerNode& handler,
                                       const std::vector<FilterBinding>& filter_bindings_list,
                                       const std::vector<std::string>& filter_traits,
                                       const std::vector<std::string>& filter_cpp_types,
                                       const std::vector<std::string>& exclude_cpp_types,
                                       const DecoratedProgram& program,
                                       const std::unordered_map<std::string, std::string>& filter_cpp_overrides) {
    auto lexical_locals = handler_lexical_locals(handler);
    LocalNumericKinds local_kinds;
    const auto filtered_body = rewrite_stmt_block(
        handler.body, 3, filter_traits, program, {}, false, filter_cpp_overrides, nullptr, lexical_locals, local_kinds);
    // Recipient-targeted delivery: run at most once, for the
    // recipient, and only if it satisfies this handler's selection
    // (targeted-event-delivery, "Unary target ... only if it
    // satisfies that consumer's selection").
    out << "    if (cactus_recipient.has_value()) {\n";
    out << "        entt::entity entity = *cactus_recipient;\n";
    out << "        if (registry.all_of<";
    for (std::size_t i = 0; i < filter_cpp_types.size(); ++i) {
        out << (i == 0 ? "" : ", ") << filter_cpp_types[i];
    }
    out << ">(entity)";
    if (!exclude_cpp_types.empty()) {
        out << " && !registry.any_of<";
        for (std::size_t i = 0; i < exclude_cpp_types.size(); ++i) {
            out << (i == 0 ? "" : ", ") << exclude_cpp_types[i];
        }
        out << ">(entity)";
    }
    out << ") {\n";
    out << "            (void)entity;\n";
    emit_component_bindings_from_entity(out, filter_bindings_list, "entity", 3, program);
    emit_filter_alias_bindings(out, sys.filter, program, 3);
    out << filtered_body;
    out << "        }\n";
    out << "    } else {\n";
    emit_sort_call(out, sys, 2);
    emit_view_declaration(out, filter_cpp_types, exclude_cpp_types, 2);
    emit_view_each_header(out, filter_bindings_list, 2, program);
    out << "        (void)entity;\n";
    emit_filter_alias_bindings(out, sys.filter, program, 3);
    out << filtered_body;
    out << "        });\n";
    out << "    }\n";
}

// Defensive fallback (should not occur for a well-formed contract lookup):
// no filter clause, contract not classified selectionless.
static void emit_fallback_handler_body(std::ostringstream& out,
                                       const RuleNode& sys,
                                       const EventHandlerNode& handler,
                                       const std::vector<std::string>& filter_traits,
                                       const DecoratedProgram& program,
                                       const std::unordered_map<std::string, std::string>& filter_cpp_overrides) {
    emit_sort_call(out, sys);
    out << "    for (auto entity : registry.storage<entt::entity>()) {\n";
    out << "        (void)entity;\n";
    emit_storage_filter_skip(out, sys.filter, sys.exclude, program, 2);
    auto lexical_locals = handler_lexical_locals(handler);
    LocalNumericKinds local_kinds;
    // Wrapped in a per-invocation lambda for the same reason as
    // emit_pair_handler_body's tuple bodies: a bare `return` spliced
    // directly into this raw loop would exit the whole generated function
    // on the first matching entity instead of ending just that invocation.
    out << "        [&]() {\n";
    out << rewrite_stmt_block(
        handler.body, 3, filter_traits, program, {}, false, filter_cpp_overrides, nullptr, lexical_locals, local_kinds);
    out << "        }();\n";
    out << "    }\n";
}

std::string EnttSystemEmitter::emit_system(const RuleNode& sys, const DecoratedProgram& program) {
    std::ostringstream out;
    const auto filter_bindings_list = filter_bindings(sys.filter, program);
    const auto filter_traits        = filter_trait_names(sys.filter, program);
    const auto filter_cpp_types     = filter_cpp_type_names(sys.filter, program);
    const auto exclude_cpp_types    = filter_cpp_type_names(sys.exclude, program);

    // Build lookup_name/simple_name → canonical_cpp_name map so rewrite_expr and
    // rewrite_stmt can resolve ambiguous traits (e.g. WorldTransform in both
    // flat and volume modules) without another map scan.
    std::unordered_map<std::string, std::string> filter_cpp_overrides;
    for (const auto& b : filter_bindings_list) {
        filter_cpp_overrides.emplace(b.trait_name, b.cpp_type_name);
        filter_cpp_overrides.emplace(b.lookup_name, b.cpp_type_name);
    }

    const bool is_pair_system = sys.pairs.has_value();
    std::vector<PairBindingCodegen> pair_binding_codegens;
    PairCodegenScope pair_codegen_scope;
    if (is_pair_system) {
        for (const auto& binding : sys.pairs->bindings) {
            auto codegen = build_pair_binding_codegen(binding, program);
            pair_codegen_scope.bindings.push_back(codegen.scope);
            pair_binding_codegens.push_back(std::move(codegen));
        }
    }

    for (const auto& handler : sys.handlers) {
        const auto* contract = graph_handler_contract(sys, handler, program);
        const bool is_pair = is_pair_system && contract != nullptr && contract->domain_kind == HandlerDomainKind::Pair;
        const bool selectionless   = !is_pair && contract != nullptr && contract->is_selectionless();
        const auto trigger_binding = handler_trigger_binding(handler);
        out << "void " << system_function_name(program.module_name, sys.name, handler_trigger_suffix(handler))
            << "(entt::registry& registry";
        out << ", const " << handler_trigger_cpp_type(handler, program) << "& " << trigger_binding;
        // Recipient is meaningful only for event-triggered handlers dispatched
        // through generated_dispatch_event; phase dispatch always calls with
        // the default (targeted-event-delivery: routing has no meaning for a
        // phase activation).
        out << ", std::optional<entt::entity> cactus_recipient = std::nullopt";
        out << ") {\n";
        out << "    (void)" << trigger_binding << ";\n";
        out << "    (void)cactus_recipient;\n";

        if (is_pair) {
            emit_pair_handler_body(out, handler, pair_binding_codegens, pair_codegen_scope, program);
        } else if (selectionless) {
            emit_selectionless_handler_body(out, handler, filter_traits, program, filter_cpp_overrides);
        } else if (!filter_traits.empty()) {
            emit_filtered_handler_body(out,
                                       sys,
                                       handler,
                                       filter_bindings_list,
                                       filter_traits,
                                       filter_cpp_types,
                                       exclude_cpp_types,
                                       program,
                                       filter_cpp_overrides);
        } else {
            emit_fallback_handler_body(out, sys, handler, filter_traits, program, filter_cpp_overrides);
        }
        out << "}\n\n";
    }

    return out.str();
}

// Shared shape for single-view, single-call component renderers: one runtime
// call per matching entity, no per-entity branching, no program-level
// gating. Renderers with a switch/conditional/extra prelude beyond the one
// call (shape_renderer, sprite_animation, model_animation, model_render,
// the flavor-gated text renderers) don't fit this shape and stay
// hand-written, same as the transform-propagation/shape-renderer pair task
// 5.3 explicitly carves out. call_body builds the exact call expression from
// the two resolved cpp component variable names — the argument lists differ
// too much between renderers (which transform fields, which data fields, in
// what order) to also templatize as a string.
struct SingleViewRendererSpec {
    bool (*matches)(const ExternRuleNode&);
    const char* transform_trait;
    const char* data_trait;
    std::function<std::string(const std::string& transform_var, const std::string& data_var)> call_body;
};

const std::array<SingleViewRendererSpec, 4>& single_view_renderer_specs() {
    static const std::array<SingleViewRendererSpec, 4> specs{{
        {.matches         = is_sprite_renderer,
         .transform_trait = "std.transform.flat.WorldTransform",
         .data_trait      = "Renderer",
         .call_body =
             [](const std::string& t, const std::string& d) {
                 return "cactus::runtime::entt_backend::submit_sprite(" + t + ".position, " + d + ".size, " + d +
                        ".color, " + d + ".texture, " + d + ".visible, " + d + ".layer)";
             }},
        {.matches         = is_mesh_renderer,
         .transform_trait = "std.transform.volume.WorldTransform",
         .data_trait      = "Renderer",
         .call_body =
             [](const std::string& t, const std::string& d) {
                 return "cactus::runtime::entt_backend::submit_mesh(" + t + ".position, " + t + ".rotation, " + t +
                        ".scale, " + d + ".mesh, " + d + ".material, " + d + ".visible, " + d + ".cast_shadow, " + d +
                        ".color)";
             }},
        {.matches         = is_billboard_renderer,
         .transform_trait = "std.transform.volume.WorldTransform",
         .data_trait      = "BillboardRenderer",
         .call_body =
             [](const std::string& t, const std::string& d) {
                 return "cactus::runtime::entt_backend::submit_billboard(" + t + ".position, " + d + ".size, " + d +
                        ".color, " + d + ".texture, " + d + ".visible)";
             }},
        {.matches         = is_point_light_render,
         .transform_trait = "std.transform.volume.WorldTransform",
         .data_trait      = "PointLight",
         .call_body =
             [](const std::string& t, const std::string& d) {
                 return "cactus::runtime::entt_backend::register_point_light(" + t + ".position, " + d + ".color, " +
                        d + ".intensity, " + d + ".range, " + d + ".enabled)";
             }},
    }};
    return specs;
}

std::string emit_single_view_renderer_body(const ExternRuleNode& sys,
                                           const DecoratedProgram& program,
                                           const SingleViewRendererSpec& spec) {
    const std::string transform     = EnttCodegenUtils::trait_cpp_name(spec.transform_trait, program);
    const std::string data          = EnttCodegenUtils::trait_cpp_name(spec.data_trait, program);
    const std::string transform_var = transform + "_comp";
    const std::string data_var      = data + "_comp";
    std::ostringstream out;
    out << "void " << system_function_name(program.module_name, sys.name, "tick") << "(entt::registry& registry) {\n";
    out << "    auto view = registry.view<" << transform << ", " << data << ">();\n";
    out << "    view.each([&](entt::entity entity, const " << transform << "& " << transform_var << ", const " << data
        << "& " << data_var << ") {\n";
    out << "        (void)entity;\n";
    out << "        " << spec.call_body(transform_var, data_var) << ";\n";
    out << "    });\n";
    out << "}\n\n";
    return out.str();
}

std::string
EnttSystemEmitter::emit_extern_system(  // NOLINT(readability-function-cognitive-complexity) -- still 29 after
                                        // single-view-renderer table extraction (many hand-written kinds remain:
                                        // transform-propagation x2, shape_renderer, sprite_animation, model_animation,
                                        // model_render, directional_light, text_2d/3d, debug-grid, editor-stub); not
                                        // further in scope here
    const ExternRuleNode& sys,
    const DecoratedProgram& program) {
    std::ostringstream out;

    for (const auto& spec : single_view_renderer_specs()) {
        if (spec.matches(sys)) {
            return emit_single_view_renderer_body(sys, program, spec);
        }
    }

    if (is_flat_transform_propagation(sys, program)) {
        const std::string lt     = EnttCodegenUtils::trait_cpp_name("std.transform.flat.LocalTransform", program);
        const std::string wt     = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
        const std::string parent = EnttCodegenUtils::trait_cpp_name("Parent", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    const auto HAS_LOCAL_WORLD = [&](entt::entity entity) {\n";
        out << "        return registry.all_of<" << lt << ", " << wt << ">(entity);\n";
        out << "    };\n";
        out << "    const auto GET_PARENT = [&](entt::entity entity) {\n";
        out << "        if (auto* parent = registry.try_get<" << parent << ">(entity); parent != nullptr) {\n";
        out << "            return parent->parent;\n";
        out << "        }\n";
        out << "        return entt::entity{entt::null};\n";
        out << "    };\n";
        out << "    const auto COPY_LOCAL = [&](entt::entity entity) {\n";
        out << "        auto& local = registry.get<" << lt << ">(entity);\n";
        out << "        auto& world = registry.get<" << wt << ">(entity);\n";
        out << "        world.position = local.position;\n";
        out << "        world.rotation = local.rotation;\n";
        out << "        world.scale = local.scale;\n";
        out << "    };\n";
        out << "    const auto ACCUMULATE_FROM_PARENT = [&](entt::entity parent_entity, entt::entity entity) {\n";
        out << "        auto& local = registry.get<" << lt << ">(entity);\n";
        out << "        auto& world = registry.get<" << wt << ">(entity);\n";
        out << "        const auto& parent_world = registry.get<" << wt << ">(parent_entity);\n";
        out << "        world.position = Vector2{\n";
        out << "            .x = parent_world.position.x + local.position.x,\n";
        out << "            .y = parent_world.position.y + local.position.y,\n";
        out << "        };\n";
        out << "        world.rotation = parent_world.rotation + local.rotation;\n";
        out << "        world.scale = Vector2{\n";
        out << "            .x = parent_world.scale.x * local.scale.x,\n";
        out << "            .y = parent_world.scale.y * local.scale.y,\n";
        out << "        };\n";
        out << "    };\n";
        out << "    cactus::runtime::entt_backend::propagate_hierarchy(\n";
        out << "        registry, HAS_LOCAL_WORLD, GET_PARENT, COPY_LOCAL, ACCUMULATE_FROM_PARENT);\n";
        out << "}\n\n";

        return out.str();
    }

    if (is_volume_transform_propagation(sys, program)) {
        const std::string lt     = EnttCodegenUtils::trait_cpp_name("std.transform.volume.LocalTransform", program);
        const std::string wt     = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
        const std::string parent = EnttCodegenUtils::trait_cpp_name("Parent", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    const auto HAS_LOCAL_WORLD = [&](entt::entity entity) {\n";
        out << "        return registry.all_of<" << lt << ", " << wt << ">(entity);\n";
        out << "    };\n";
        out << "    const auto GET_PARENT = [&](entt::entity entity) {\n";
        out << "        if (auto* parent = registry.try_get<" << parent << ">(entity); parent != nullptr) {\n";
        out << "            return parent->parent;\n";
        out << "        }\n";
        out << "        return entt::entity{entt::null};\n";
        out << "    };\n";
        out << "    const auto COPY_LOCAL = [&](entt::entity entity) {\n";
        out << "        auto& local = registry.get<" << lt << ">(entity);\n";
        out << "        auto& world = registry.get<" << wt << ">(entity);\n";
        out << "        world.position = local.position;\n";
        out << "        world.rotation = local.rotation;\n";
        out << "        world.scale = local.scale;\n";
        out << "    };\n";
        out << "    const auto ACCUMULATE_FROM_PARENT = [&](entt::entity parent_entity, entt::entity entity) {\n";
        out << "        auto& local = registry.get<" << lt << ">(entity);\n";
        out << "        auto& world = registry.get<" << wt << ">(entity);\n";
        out << "        const auto& parent_world = registry.get<" << wt << ">(parent_entity);\n";
        out << "        world.position = Vector3{\n";
        out << "            .x = parent_world.position.x + local.position.x,\n";
        out << "            .y = parent_world.position.y + local.position.y,\n";
        out << "            .z = parent_world.position.z + local.position.z,\n";
        out << "        };\n";
        out << "        world.rotation = cactus::runtime::stdlib::math::quat::compose(parent_world.rotation, "
               "local.rotation);\n";
        out << "        world.scale = Vector3{\n";
        out << "            .x = parent_world.scale.x * local.scale.x,\n";
        out << "            .y = parent_world.scale.y * local.scale.y,\n";
        out << "            .z = parent_world.scale.z * local.scale.z,\n";
        out << "        };\n";
        out << "    };\n";
        out << "    cactus::runtime::entt_backend::propagate_hierarchy(\n";
        out << "        registry, HAS_LOCAL_WORLD, GET_PARENT, COPY_LOCAL, ACCUMULATE_FROM_PARENT);\n";
        out << "}\n\n";

        return out.str();
    }

    if (is_shape_renderer(sys)) {
        const std::string wt         = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
        const std::string shape      = EnttCodegenUtils::trait_cpp_name("Shape", program);
        const std::string shape_type = EnttCodegenUtils::enum_cpp_name("ShapeType", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << wt << ", " << shape << ">();\n";
        out << "    cactus::runtime::raylib::BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << shape << "& "
            << shape << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        if (!" << shape << "_comp.visible) {\n";
        out << "            return;\n";
        out << "        }\n";
        out << "        switch (" << shape << "_comp.type) {\n";
        out << "            case " << shape_type << "::Rectangle:\n";
        out << "                cactus::runtime::entt_backend::draw_shape_rectangle(" << wt << "_comp.position,\n";
        out << "                               " << shape << "_comp.size,\n";
        out << "                               " << shape << "_comp.origin,\n";
        out << "                               " << wt << "_comp.rotation,\n";
        out << "                               " << shape << "_comp.color);\n";
        out << "                break;\n";
        out << "            case " << shape_type << "::Circle:\n";
        out << "                cactus::runtime::entt_backend::draw_shape_circle(" << wt << "_comp.position,\n";
        out << "                            " << shape << "_comp.size.x,\n";
        out << "                            " << shape << "_comp.origin,\n";
        out << "                            " << shape << "_comp.color);\n";
        out << "                break;\n";
        out << "        }\n";
        out << "    });\n";
        out << "    cactus::runtime::raylib::EndMode2D();\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_sprite_animation(sys)) {
        const std::string anim = EnttCodegenUtils::trait_cpp_name("AnimatedSprite", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << anim << ">();\n";
        out << "    view.each([&](entt::entity entity, " << anim << "& " << anim << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        constexpr float kFixedDt = 1.0F / 60.0F;\n";
        out << "        cactus::runtime::entt_backend::advance_animated_sprite(" << anim << "_comp.texture, " << anim
            << "_comp.frame, " << anim << "_comp.frame_count, " << anim << "_comp.fps, " << anim
            << "_comp.playing, kFixedDt);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_model_animation(sys)) {
        const std::string mr    = EnttCodegenUtils::trait_cpp_name("ModelRenderer", program);
        const std::string manim = EnttCodegenUtils::trait_cpp_name("ModelAnimator", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << mr << ", " << manim << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << mr << "& " << mr << "_comp, " << manim << "& "
            << manim << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        if (!" << manim << "_comp.playing) {\n";
        out << "            return;\n";
        out << "        }\n";
        out << "        constexpr float kFixedDt = 1.0F / 60.0F;\n";
        out << "        " << manim << "_comp.time += kFixedDt * " << manim << "_comp.speed;\n";
        out << "        const float duration = cactus::runtime::entt_backend::model_animation_duration(" << mr
            << "_comp.model, " << manim << "_comp.clip);\n";
        out << "        if (duration > 0.0F) {\n";
        out << "            " << manim << "_comp.time = std::fmod(" << manim << "_comp.time, duration);\n";
        out << "            if (" << manim << "_comp.time < 0.0F) {\n";
        out << "                " << manim << "_comp.time += duration;\n";
        out << "            }\n";
        out << "        }\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_model_render(sys)) {
        const std::string wt    = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
        const std::string mr    = EnttCodegenUtils::trait_cpp_name("ModelRenderer", program);
        const std::string manim = EnttCodegenUtils::trait_cpp_name("ModelAnimator", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << wt << ", " << mr << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << mr << "& "
            << mr << "_comp) {\n";
        out << "        (void)entity;\n";
        if (EnttCodegenUtils::find_trait(program, "ModelAnimator") != nullptr) {
            out << "        if (const auto* animator = registry.try_get<" << manim << ">(entity)) {\n";
            out << "            cactus::runtime::entt_backend::submit_model(" << wt << "_comp.position, " << wt
                << "_comp.rotation, " << wt << "_comp.scale, " << mr << "_comp.model, " << mr << "_comp.visible, " << mr
                << "_comp.cast_shadow, animator->clip, animator->time);\n";
            out << "            return;\n";
            out << "        }\n";
        }
        out << "        cactus::runtime::entt_backend::submit_model(" << wt << "_comp.position, " << wt
            << "_comp.rotation, " << wt << "_comp.scale, " << mr << "_comp.model, " << mr << "_comp.visible, " << mr
            << "_comp.cast_shadow);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_directional_light_render(sys)) {
        const std::string dl = EnttCodegenUtils::trait_cpp_name("DirectionalLight", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << dl << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << dl << "& " << dl << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::register_directional_light(" << dl << "_comp.direction, " << dl
            << "_comp.color, " << dl << "_comp.intensity, " << dl << "_comp.enabled);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_any_text_renderer_2d(sys)) {
        const std::string wt = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
        const std::string tl = EnttCodegenUtils::trait_cpp_name("TextLabel", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        if (!world_transform_is_volume(program)) {
            out << "    auto view = registry.view<" << wt << ", " << tl << ">();\n";
            out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << tl << "& "
                << tl << "_comp) {\n";
            out << "        (void)entity;\n";
            out << "        cactus::runtime::entt_backend::submit_text_2d(" << wt << "_comp.position, " << wt
                << "_comp.rotation, " << tl << "_comp.font_size, " << tl << "_comp.color, " << tl << "_comp.text, "
                << tl << "_comp.visible);\n";
            out << "    });\n";
        } else {
            out << "    (void)registry;\n";
        }
        out << "}\n\n";
        return out.str();
    }

    if (is_any_text_renderer_3d(sys)) {
        const std::string wt = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
        const std::string tl = EnttCodegenUtils::trait_cpp_name("TextLabel", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        if (world_transform_is_volume(program)) {
            out << "    auto view = registry.view<" << wt << ", " << tl << ">();\n";
            out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << tl << "& "
                << tl << "_comp) {\n";
            out << "        cactus::runtime::entt_backend::submit_text_3d(static_cast<uint32_t>(entity), " << wt
                << "_comp.position, " << wt << "_comp.rotation, " << wt << "_comp.scale, " << tl << "_comp.font_size, "
                << tl << "_comp.color, " << tl << "_comp.text, " << tl << "_comp.visible);\n";
            out << "    });\n";
        } else {
            out << "    (void)registry;\n";
        }
        out << "}\n\n";
        return out.str();
    }

    if (is_screen_label_render(sys)) {
        // Window-space HUD text: no WorldTransform and no flavor gating — the
        // same emission serves flat and volume programs (dsl-model-animation D5).
        const std::string sl = EnttCodegenUtils::trait_cpp_name("ScreenLabel", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << sl << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << sl << "& " << sl << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_screen_label(" << sl << "_comp.position, " << sl
            << "_comp.font_size, " << sl << "_comp.color, " << sl << "_comp.text, " << sl << "_comp.visible);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    for (const auto& spec : debug_draw_renderer_specs()) {
        if (symbol_is(sys.resolved_rule_id, SymbolKind::Rule, spec.module_name, spec.rule_name)) {
            out << emit_event_renderer_body(sys, program, spec.mode_begin, spec.mode_end, spec.draw_call_body);
            return out.str();
        }
    }

    if (is_debug_grid_3d(sys)) {
        // Filter-gated existence check (no per-entity data needed) — same
        // existence-scan shape the now-removed hardcoded GizmoRenderer2D/3D
        // bodies used for their __editor_active check.
        const std::string cam3d = EnttCodegenUtils::trait_cpp_name("EditorCamera3D", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    bool __has_3d_rig = false;\n";
        out << "    auto __cam3d_view = registry.view<" << cam3d << ">();\n";
        out << "    for (auto __e : __cam3d_view) { (void)__e; __has_3d_rig = true; break; }\n";
        out << "    if (!__has_3d_rig) { return; }\n";
        out << "    " << kBeginMode3D << "\n";
        out << "    cactus::runtime::raylib::DrawGrid(20, 1.0F);\n";
        out << "    " << kEndMode3D << "\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_editor_extern_system(sys)) {
        // EditorPropertyPanel — unimplemented stub.
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    (void)registry;\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_standard_ui_render(sys)) {
        // ComputedLayout.draw_order (design decision #7) is already the
        // complete window-space painter order across the whole UI forest, so
        // rendering only needs a flat sort-and-emit — no recursive traversal
        // or clip stack, since ComputedLayout.clip_min/max already holds each
        // entity's fully intersected ancestor clip rectangle. Each entity's
        // gathered data is handed to one runtime bridge (render_ui_primitive)
        // that owns primitive order/opacity/fit/clip drawing (design decision
        // #10: the backend owns drawing operations, not authored Cactus).
        const std::string cl_cpp     = EnttCodegenUtils::trait_cpp_name("ComputedLayout", program);
        const std::string visual_cpp = EnttCodegenUtils::trait_cpp_name("Visual", program);
        const std::string panel_cpp  = EnttCodegenUtils::trait_cpp_name("Panel", program);
        const std::string text_cpp   = EnttCodegenUtils::trait_cpp_name("Text", program);
        const std::string image_cpp  = EnttCodegenUtils::trait_cpp_name("Image", program);
        const std::string button_cpp = EnttCodegenUtils::trait_cpp_name("Button", program);
        const std::string anim_cpp   = EnttCodegenUtils::trait_cpp_name("FrameAnimation", program);
        const std::string pstate_cpp = EnttCodegenUtils::trait_cpp_name("std.pointer.PointerState", program);

        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    std::vector<entt::entity> __ui_paint_order;\n";
        out << "    for (auto __e : registry.view<" << cl_cpp << ">()) { __ui_paint_order.push_back(__e); }\n";
        out << "    std::ranges::sort(__ui_paint_order, [&](entt::entity __a, entt::entity __b) {\n";
        out << "        return registry.get<" << cl_cpp << ">(__a).draw_order < registry.get<" << cl_cpp
            << ">(__b).draw_order;\n";
        out << "    });\n";
        out << "    for (auto __e : __ui_paint_order) {\n";
        out << "        const auto& __cl = registry.get<" << cl_cpp << ">(__e);\n";
        out << "        cactus::runtime::entt_backend::UiPrimitive __prim{};\n";
        out << "        __prim.position = __cl.position;\n";
        out << "        __prim.size = __cl.size;\n";
        out << "        __prim.effective_visible = __cl.effective_visible;\n";
        out << "        __prim.effective_enabled = __cl.effective_enabled;\n";
        out << "        __prim.effective_opacity = __cl.effective_opacity;\n";
        out << "        __prim.clip_min = __cl.clip_min;\n";
        out << "        __prim.clip_max = __cl.clip_max;\n";
        out << "        if (const auto* __visual = registry.try_get<" << visual_cpp
            << ">(__e)) { __prim.visual_scale = __visual->scale; }\n";
        out << "        if (const auto* __panel = registry.try_get<" << panel_cpp << ">(__e)) {\n";
        out << "            __prim.has_panel = true;\n";
        out << "            __prim.panel_background = __panel->background;\n";
        out << "            __prim.panel_border_color = __panel->border_color;\n";
        out << "            __prim.panel_border_width = __panel->border_width;\n";
        out << "        }\n";
        out << "        if (const auto* __text = registry.try_get<" << text_cpp << ">(__e)) {\n";
        out << "            __prim.has_text = true;\n";
        out << "            __prim.text_value = __text->value;\n";
        out << "            __prim.text_font_size = __text->font_size;\n";
        out << "            __prim.text_color = __text->color;\n";
        out << "            __prim.text_align = static_cast<int>(__text->align);\n";
        out << "        }\n";
        out << "        if (const auto* __image = registry.try_get<" << image_cpp << ">(__e)) {\n";
        out << "            __prim.has_image = true;\n";
        out << "            __prim.image_texture = __image->texture;\n";
        out << "            __prim.image_tint = __image->tint;\n";
        out << "            __prim.image_fit = static_cast<int>(__image->fit);\n";
        out << "        }\n";
        out << "        if (const auto* __anim = registry.try_get<" << anim_cpp << ">(__e)) {\n";
        out << "            __prim.has_frame_animation = true;\n";
        out << "            __prim.frame_count = __anim->frame_count;\n";
        out << "            __prim.frame = __anim->frame;\n";
        out << "        }\n";
        out << "        if (const auto* __button = registry.try_get<" << button_cpp << ">(__e)) {\n";
        out << "            __prim.has_button = true;\n";
        out << "            __prim.button_label = __button->label;\n";
        out << "            __prim.button_normal_color = __button->normal_color;\n";
        out << "            __prim.button_hover_color = __button->hover_color;\n";
        out << "            __prim.button_pressed_color = __button->pressed_color;\n";
        out << "            __prim.button_disabled_color = __button->disabled_color;\n";
        out << "            __prim.button_text_color = __button->text_color;\n";
        out << "        }\n";
        out << "        if (const auto* __pstate = registry.try_get<" << pstate_cpp << ">(__e)) {\n";
        out << "            __prim.pointer_hovered = __pstate->hovered;\n";
        out << "            __prim.pointer_pressed = __pstate->pressed;\n";
        out << "        }\n";
        out << "        cactus::runtime::entt_backend::render_ui_primitive(__prim);\n";
        out << "    }\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_pointer_router(sys)) {
        // Turns compute_pointer_frame_transitions' decisions into real
        // component writes and typed targeted events — the part that needs
        // this program's real generated types, which that shared, pure
        // runtime.cpp function cannot reference (design decisions #8/#9).
        // registry.valid() guards mirror ordinary DSL `emit ... to ...`
        // lowering (emit_emit_stmt above) even though the struct's targets
        // are already known-live at the moment this function receives them.
        const std::string pstate_cpp  = EnttCodegenUtils::trait_cpp_name("std.pointer.PointerState", program);
        const std::string enter_evt   = event_cpp_type("PointerEnter", program);
        const std::string leave_evt   = event_cpp_type("PointerLeave", program);
        const std::string press_evt   = event_cpp_type("PointerPress", program);
        const std::string release_evt = event_cpp_type("PointerRelease", program);
        const std::string click_evt   = event_cpp_type("Click", program);

        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    const auto __t = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);\n";
        out << "    const Vector2 __pos = cactus::runtime::entt_backend::mouse_position();\n";
        out << "    if (__t.hover_changed) {\n";
        out << "        if (registry.valid(__t.leave_target)) {\n";
        out << "            if (auto* __ps = registry.try_get<" << pstate_cpp
            << ">(__t.leave_target)) { __ps->hovered = false; }\n";
        out << "            cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "                " << leave_evt << "{.position = __pos}, __t.leave_target);\n";
        out << "        }\n";
        out << "        if (registry.valid(__t.enter_target)) {\n";
        out << "            if (auto* __ps = registry.try_get<" << pstate_cpp
            << ">(__t.enter_target)) { __ps->hovered = true; }\n";
        out << "            cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "                " << enter_evt << "{.position = __pos}, __t.enter_target);\n";
        out << "        }\n";
        out << "    }\n";
        out << "    if (__t.press_occurred && registry.valid(__t.press_target)) {\n";
        out << "        if (auto* __ps = registry.try_get<" << pstate_cpp
            << ">(__t.press_target)) { __ps->pressed = true; }\n";
        out << "        cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "            " << press_evt << "{.position = __pos}, __t.press_target);\n";
        out << "    }\n";
        out << "    if (__t.release_occurred && registry.valid(__t.release_target)) {\n";
        out << "        if (auto* __ps = registry.try_get<" << pstate_cpp
            << ">(__t.release_target)) { __ps->pressed = false; }\n";
        out << "        cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "            " << release_evt << "{.position = __pos}, __t.release_target);\n";
        out << "        if (__t.release_is_click) {\n";
        out << "            cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "                " << click_evt << "{.position = __pos}, __t.release_target);\n";
        out << "        }\n";
        out << "    }\n";
        out << "    if (__t.should_consume_primary) {\n";
        out << "        cactus::runtime::entt_backend::mark_input_mouse_consumed(MOUSE_BUTTON_LEFT);\n";
        out << "    }\n";
        out << "}\n\n";
        return out.str();
    }

    // When std.editor is imported it pulls in both std.transform.flat and
    // std.transform.volume, adding both TransformPropagation ExternRuleNodes
    // to the merged AST. The dimension-matching variant is handled above (early
    // return). The non-matching variant falls here and must be dropped so that
    // the generic emit path does not produce a duplicate function definition.
    if (symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.transform.flat", "TransformPropagation") ||
        symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.transform.volume", "TransformPropagation")) {
        return "";
    }

    throw std::runtime_error("cpp-entt has no compiler-owned implementation for external rule '" +
                             (sys.resolved_rule_id.has_value() ? make_canonical_id(*sys.resolved_rule_id) : sys.name) +
                             "'; user external handlers must be lowered through their per-handler callback ABI");
}

bool EnttSystemEmitter::requires_entt_hierarchy_helpers(const DecoratedProgram& program) {
    return EnttCodegenUtils::find_trait(program, "Parent") != nullptr;
}

std::string EnttSystemEmitter::emit_entt_hierarchy_helpers(const DecoratedProgram& program) {
    if (!requires_entt_hierarchy_helpers(program)) {
        return "";
    }

    const std::string parent_cpp = EnttCodegenUtils::trait_cpp_name("Parent", program);
    std::ostringstream out;
    out << "[[maybe_unused]] static void cactus_destroy_entity_recursive(entt::registry& registry, entt::entity "
           "entity) {\n";
    out << "    cactus::runtime::entt_backend::destroy_entity_recursive(\n";
    out << "        registry, entity, [&](entt::entity parent, const auto& visitor) {\n";
    out << "            auto parent_view = registry.view<" << parent_cpp << ">();\n";
    out << "            parent_view.each([&](entt::entity child, const " << parent_cpp << "& rel) {\n";
    out << "                if (rel.parent == parent) {\n";
    out << "                    visitor(child);\n";
    out << "                }\n";
    out << "            });\n";
    out << "        });\n";
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
