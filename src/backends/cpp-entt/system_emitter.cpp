#include "backends/cpp-entt/system_emitter.hpp"
#include "common/ast_expressions.hpp"
#include "common/codegen_error.hpp"

#include "frontend/hierarchy_forest.hpp"
#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/accumulation_plan.hpp"
#include "backends/cpp-entt/persistence_emitter.hpp"
#include "backends/cpp-entt/index_cell_size.hpp"
#include "backends/cpp-entt/range_checks.hpp"
#include "backends/cpp-entt/trait_writers.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <functional>
#include <iterator>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
    if (module_name == "std.app") {
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
const std::unordered_map<std::string, FuncRenameMap>& runtime_func_names() {
    static const std::unordered_map<std::string, FuncRenameMap> value = {
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
          {"consume", {.runtime_name = "editor_consume"}},
          {"set_cursor_captured", {.runtime_name = "set_cursor_captured"}}}},
        {"std.camera.flat",
         {{"screen_to_world", {.runtime_name = "editor_screen_to_world_2d"}},
          {"screen_delta_to_world", {.runtime_name = "screen_delta_to_world_2d"}}}},
        {"std.camera.volume",
         {{"screen_to_plane", {.runtime_name = "editor_plane_project_3d"}},
          {"screen_delta_on_plane", {.runtime_name = "screen_delta_on_plane_3d"}}}},
        {"std.transform.flat",
         {{"world_position", {.runtime_name = "editor_entity_position_2d", .needs_registry = true}}}},
        {"std.transform.volume",
         {{"world_position", {.runtime_name = "editor_entity_position_3d", .needs_registry = true}}}},
        {"std.pointer", {{"top_target", {.runtime_name = "pointer_top_target", .needs_registry = true}}}},
    };
    return value;
}

// std.ui mixes a handful of backend-owned metric facts (window_size/text_size/
// texture_size — platform state per design.md decision #10) with ordinary
// authored `func` helpers (nonnegative, max_size, ...) in the same module, so
// it cannot use the blanket per-module prefix above: that would route every
// authored helper call through a nonexistent cactus::runtime::entt_backend
// symbol of the same name. Name the three metric facts explicitly instead.
const std::unordered_map<std::string, std::string>& ui_metric_func_names() {
    static const std::unordered_map<std::string, std::string> value = {
        {"window_size", "ui_window_size"},
        {"text_size", "ui_text_size"},
        {"texture_size", "ui_texture_size"},
    };
    return value;
}

bool is_stdlib_ui_metric_func(const SymbolId& func_id) {
    return func_id.kind == SymbolKind::Func && func_id.module.name == "std.ui" &&
           ui_metric_func_names().contains(func_id.local_name);
}

std::string stdlib_ui_metric_func_call(const SymbolId& func_id,
                                       const std::vector<std::unique_ptr<ExprNode>>& args,
                                       const auto& emit_arg) {
    std::string result = "cactus::runtime::entt_backend::" + ui_metric_func_names().at(func_id.local_name) + "(";
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
    const auto module_it = runtime_func_names().find(module_name);
    if (module_it != runtime_func_names().end()) {
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
    const auto module_it = runtime_func_names().find(func_id.module.name);
    if (module_it == runtime_func_names().end()) {
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

const std::string& template_creation_module(const SymbolId& template_id, const DecoratedProgram& program) {
    const bool is_local = program.pub_templates.contains(template_id.local_name) ||
                          program.non_pub_templates.contains(template_id.local_name);
    return is_local ? program.module_name : template_id.module.name;
}

std::string template_create_ref(const SymbolId& template_id, const DecoratedProgram& program) {
    return archetype_create_function_name(template_creation_module(template_id, program), template_id.local_name).ref();
}

std::string template_create_at_ref(const SymbolId& template_id, const DecoratedProgram& program) {
    return archetype_create_at_function_name(template_creation_module(template_id, program), template_id.local_name)
        .ref();
}

std::string event_cpp_type(const std::string& event_name, const DecoratedProgram& program) {
    if (program.ast != nullptr) {
        for (const auto& decl : program.ast->declarations) {
            if (const auto* ev = std::get_if<EventNode>(&decl)) {
                if (ev->name == event_name) {
                    const std::string& mod = ev->module_name.empty() ? program.module_name : ev->module_name;
                    return cpp_type_ref(SymbolKind::Event, mod, event_name);
                }
            }
        }
    }
    return cpp_type_ref(SymbolKind::Event, program.module_name, event_name);
}

std::string event_cpp_type(const SymbolId& event_id, const DecoratedProgram& program) {
    if (program.ast != nullptr) {
        for (const auto& decl : program.ast->declarations) {
            if (const auto* event = std::get_if<EventNode>(&decl);
                event != nullptr && event->resolved_event_id.has_value() && *event->resolved_event_id == event_id) {
                const auto& module = event->module_name.empty() ? program.module_name : event->module_name;
                return cpp_type_ref(SymbolKind::Event, module, event->name);
            }
        }
    }

    const auto canonical = make_canonical_id(event_id);
    for (const auto& [_, event] : program.events) {
        if ((event.symbol_id.has_value() && *event.symbol_id == event_id) || event.canonical_id == canonical) {
            return cpp_type_ref(SymbolKind::Event, event.module_name, event.name);
        }
    }
    return event_cpp_type_name(event_id);
}

std::string handler_trigger_cpp_type(const EventHandlerNode& handler, const DecoratedProgram& program) {
    if (handler.resolved_trigger.has_value() && handler.resolved_trigger->kind == HandlerTriggerKind::Phase) {
        return cpp_type_ref(handler.resolved_trigger->symbol);
    }
    if (!handler.resolved_trigger.has_value()) {
        return event_cpp_type(handler.event_name, program);
    }
    if (auto lifecycle = EnttCodegenUtils::lifecycle_occurrence_cpp_type(*handler.resolved_trigger, program)) {
        return *lifecycle;
    }
    return event_cpp_type(handler.resolved_trigger->symbol, program);
}

// A lifecycle trigger's alias names the trait (or its snapshot), never the occurrence itself.
std::string handler_trigger_binding(const EventHandlerNode& handler) {
    if (handler.has_lifecycle_trigger()) {
        return "cactus_trigger";
    }
    // A dotted trigger has no single-identifier spelling of its own.
    if (handler.alias.has_value() || handler.event_name.contains('.')) {
        return handler.alias.value_or("cactus_trigger");
    }
    return snake_case(handler.event_name);
}

// Scoped, not a parameter: rewrite_expr's recursive calls pass only partial context.
struct HandlerTriggerSpelling {
    std::string spelling;  // as written after `on`, e.g. "Turn" or "combat.Hit"
    std::string binding;   // the handler's payload parameter
};

// The value an enclosing ActiveScope<T> points at, or null.
template <typename T>
T*& active_scope() {
    thread_local T* active = nullptr;
    return active;
}

template <typename T>
class ActiveScope {
public:
    explicit ActiveScope(T& value) : previous_(std::exchange(active_scope<T>(), &value)) {}
    ActiveScope(const ActiveScope&)            = delete;
    ActiveScope& operator=(const ActiveScope&) = delete;
    ActiveScope(ActiveScope&&)                 = delete;
    ActiveScope& operator=(ActiveScope&&)      = delete;
    ~ActiveScope() { active_scope<T>() = previous_; }

private:
    T* previous_;
};

using HandlerTriggerScope = ActiveScope<const HandlerTriggerSpelling>;

// The handler whose body is being emitted.
struct HandlerBodyCodegen {
    const HandlerContract* contract = nullptr;
    bool structure_deferred         = false;  // structural commands wait for commit
    bool uses_paint_order           = false;  // the body reads the pass's paint-order cache
};

// The payload parameter when `expr` spells the active handler's trigger name.
std::optional<std::string> lowered_trigger_reference(const ExprNode& expr) {
    const auto* trigger = active_scope<const HandlerTriggerSpelling>();
    if (trigger == nullptr || trigger->spelling.empty()) {
        return std::nullopt;
    }
    if (dotted_name_of(expr) != trigger->spelling) {
        return std::nullopt;
    }
    return trigger->binding;
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

// The body being emitted, when its handler's contract is known, structural
// commands wait for commit and Parent isn't projected, so the forest can't
// change while the body runs.
HandlerBodyCodegen* settled_handler_body() {
    auto* body = active_scope<HandlerBodyCodegen>();
    if (body == nullptr || body->contract == nullptr || !body->structure_deferred) {
        return nullptr;
    }
    const bool projects_parent = std::ranges::any_of(
        body->contract->projects, [](const SymbolId& trait) { return trait.local_name == "Parent"; });
    return projects_parent ? nullptr : body;
}

// A child loop can follow the sibling links when the body projects no trait the
// loop's filter tests.
bool walks_child_links(const QueryCallExpr& qcall) {
    const auto* body = settled_handler_body();
    if (body == nullptr) {
        return false;
    }
    return std::ranges::all_of(qcall.filters, [&](const QueryFilterPredicate& filter) {
        return filter.resolved_trait_id.has_value() && !body->contract->projects.contains(*filter.resolved_trait_id);
    });
}

// Draw orders come from the pass's cache when the body can change neither the
// forest nor any z_index; reports the use so the pass declares the cache.
bool reads_paint_order_cache() {
    auto* body = settled_handler_body();
    if (body == nullptr) {
        return false;
    }
    const auto& contract = *body->contract;
    const auto is_node   = [](const SymbolId& trait) {
        return trait.module.name == "std.ui" && trait.local_name == "Node";
    };
    if (std::ranges::any_of(contract.projects, is_node) ||
        std::ranges::any_of(contract.writes, [&](const SymbolId& trait) {
            return is_node(trait) && field_access(contract.write_fields, trait).covers("z_index");
        })) {
        return false;
    }
    body->uses_paint_order = true;
    return true;
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

struct PairCodegenBinding {
    PairCodegenBinding()                                                = default;
    PairCodegenBinding(const PairCodegenBinding&)                       = default;
    PairCodegenBinding& operator=(const PairCodegenBinding&)            = default;
    PairCodegenBinding(PairCodegenBinding&&) noexcept(false)            = default;
    PairCodegenBinding& operator=(PairCodegenBinding&&) noexcept(false) = default;
    ~PairCodegenBinding()                                               = default;
    std::string binding_name;
    // dsl-rule-limit: the binding a provably-one `limit: ... per` names, whose
    // trait access is emitted mutable (registry.get<T>) instead of the
    // read-only registry.get<const T> every other pair binding gets. Mirrors
    // the frontend's PairBindingScope::writable.
    bool writable = false;
    // Builds this binding's collider descriptor: a specialized builder when the
    // selection names exactly one shape trait, the generic lookup otherwise.
    std::string collider_shape_builder = EnttCodegenUtils::collider_shape_builder();
    // Set inside a reduction fold, where descriptors come from the pass cache.
    bool collider_shapes_cached = false;
    // A row binding: a relation row, not an entity.
    bool row = false;
    std::optional<SymbolId> relation;
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
// Color isn't numeric for arithmetic-promotion purposes (this classification's
// original purpose) — it's tracked here too because it needs the same
// per-local "what Cactus type is this identifier" lookup rewrite_expr's
// MemberExpr case already threads through, to normalize a color-typed
// local/parameter's `.r`/`.g`/`.b`/`.a` reads the same way every other color
// access is normalized.
enum class NumericKind : std::uint8_t { Unknown, Int, Float, Color };

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
    // `on added T as x` binds x as a filter alias; only a removed snapshot is a plain local.
    if (!handler.has_lifecycle_trigger()) {
        lexical_locals.insert(handler.event_name);
    }
    const bool added = handler.resolved_trigger.has_value() && handler.resolved_trigger->kind == HandlerTriggerKind::TraitAdded;
    if (handler.alias.has_value() && !added) {
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
                                           .binding_name  = component_local(cpp_type_name)});
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
                                           .binding_name  = component_local(cpp_type_name)});
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
    PairBindingCodegen()                                                = default;
    PairBindingCodegen(const PairBindingCodegen&)                       = default;
    PairBindingCodegen& operator=(const PairBindingCodegen&)            = default;
    PairBindingCodegen(PairBindingCodegen&&) noexcept(false)            = default;
    PairBindingCodegen& operator=(PairBindingCodegen&&) noexcept(false) = default;
    ~PairBindingCodegen()                                               = default;
    std::vector<std::string> cpp_types;  // dedup'd view<> template args, source order
    PairCodegenBinding scope;            // scope.binding_name is also this binding's source-level name
};

std::string collider_shape_builder(const PairBindingNode& binding) {
    std::vector<std::string_view> shapes;
    for (const auto& entry : binding.traits) {
        if (const auto shape = collider_shape_trait(entry.resolved_trait_id); shape.has_value()) {
            shapes.push_back(*shape);
        }
    }
    return EnttCodegenUtils::collider_shape_builder(shapes.size() == 1 ? std::optional{shapes.front()} : std::nullopt);
}

PairBindingCodegen build_pair_binding_codegen(const PairBindingNode& binding, const DecoratedProgram& program) {
    PairBindingCodegen result;
    result.scope.binding_name = binding.name;
    result.scope.row          = binding.is_row();
    result.scope.relation     = binding.resolved_relation_id;
    std::unordered_set<std::string> seen_cpp_types;
    for (const auto& entry : binding.traits) {
        const auto cpp_type_name =
            EnttCodegenUtils::trait_cpp_name(entry.resolved_trait_id, entry.qualified_name, program);
        if (seen_cpp_types.insert(cpp_type_name).second) {
            result.cpp_types.push_back(cpp_type_name);
        }
    }
    result.scope.collider_shape_builder = collider_shape_builder(binding);
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

enum class TransformDimension : std::uint8_t { Flat, Volume };

// Emits `PROPAGATION`, the runtime callbacks over the program's transform traits.
void emit_hierarchy_propagation(std::ostringstream& out,
                                const DecoratedProgram& program,
                                TransformDimension dimension) {
    const bool volume        = dimension == TransformDimension::Volume;
    const std::string module = volume ? "std.transform.volume" : "std.transform.flat";
    const std::string lt     = EnttCodegenUtils::trait_cpp_name(module + ".LocalTransform", program);
    const std::string wt     = EnttCodegenUtils::trait_cpp_name(module + ".WorldTransform", program);
    const std::string vec    = volume ? "Vector3" : "Vector2";
    const auto per_axis      = [&](std::string_view op, std::string_view field) {
        std::ostringstream axes;
        for (const char axis : volume ? std::string_view{"xyz"} : std::string_view{"xy"}) {
            axes << "            ." << axis << " = parent_world." << field << "." << axis << " " << op << " local."
                 << field << "." << axis << ",\n";
        }
        return axes.str();
    };
    out << "    const cactus::runtime::entt_backend::HierarchyPropagation PROPAGATION{\n";
    out << "        .has_local_world = [&](entt::entity entity) {\n";
    out << "            return registry.all_of<" << lt << ", " << wt << ">(entity);\n";
    out << "        },\n";
    out << "        .has_world = [&](entt::entity entity) { return registry.all_of<" << wt << ">(entity); },\n";
    out << "        .copy_local = [&](entt::entity entity) {\n";
    out << "            auto& local = registry.get<" << lt << ">(entity);\n";
    out << "            auto& world = registry.get<" << wt << ">(entity);\n";
    out << "            world.position = local.position;\n";
    out << "            world.rotation = local.rotation;\n";
    out << "            world.scale = local.scale;\n";
    out << "        },\n";
    out << "        .accumulate_from_parent = [&](entt::entity parent_entity, entt::entity entity) {\n";
    out << "            auto& local = registry.get<" << lt << ">(entity);\n";
    out << "            auto& world = registry.get<" << wt << ">(entity);\n";
    out << "            const auto& parent_world = registry.get<" << wt << ">(parent_entity);\n";
    out << "            world.position = " << vec << "{\n" << per_axis("+", "position") << "            };\n";
    if (volume) {
        out << "            world.rotation = cactus::runtime::stdlib::math::quat::compose(parent_world.rotation, "
               "local.rotation);\n";
    } else {
        out << "            world.rotation = parent_world.rotation + local.rotation;\n";
    }
    out << "            world.scale = " << vec << "{\n" << per_axis("*", "scale") << "            };\n";
    out << "        },\n";
    out << "    };\n";
}

bool is_follow_bodies(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.physics.volume", "FollowBodies");
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

bool is_voxel_volume_renderer(const ExternRuleNode& sys) {
    return symbol_is(sys.resolved_rule_id, SymbolKind::Rule, "std.voxel.volume", "RenderVolumes");
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
    out << "void " << system_function_name(program.module_name, sys.name, "tick").name
        << "(entt::registry& registry, const " << event_type << "& occurrence) {\n";
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

constexpr const char* kBeginMode2D =
    "cactus::runtime::raylib::BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());";
constexpr const char* kEndMode2D = "cactus::runtime::raylib::EndMode2D();";
constexpr const char* kBeginMode3D =
    "cactus::runtime::raylib::BeginMode3D(cactus::runtime::entt_backend::get_active_camera_3d());";
constexpr const char* kEndMode3D = "cactus::runtime::raylib::EndMode3D();";

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
         .draw_call_body = "    const Rectangle cactus_gen_rect{.x = occurrence.position.x, .y = occurrence.position.y,\n"
                           "                          .width = occurrence.size.x, .height = occurrence.size.y};\n"
                           "    cactus::runtime::raylib::DrawRectangleLinesEx(cactus_gen_rect, occurrence.thickness, "
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
         .draw_call_body = "    const Rectangle cactus_gen_rect{.x = occurrence.position.x, .y = occurrence.position.y,\n"
                           "                          .width = occurrence.size.x, .height = occurrence.size.y};\n"
                           "    if (occurrence.filled) {\n"
                           "        cactus::runtime::raylib::DrawRectangleRec(cactus_gen_rect, occurrence.color);\n"
                           "    } else {\n"
                           "        cactus::runtime::raylib::DrawRectangleLinesEx(cactus_gen_rect, occurrence.thickness, "
                           "occurrence.color);\n"
                           "    }\n"},
    };
    return specs;
}

std::string foreach_temp_name(const ForeachStmt& stmt, const std::string& role = "snapshot") {
    return "foreach_" + role + "_" + std::to_string(std::max(stmt.location.line, 0)) + "_" +
           std::to_string(std::max(stmt.location.column, 0));
}

// Generates a compiler-internal temporary name that cannot collide with a user-visible DSL
// identifier spliced into the same or an enclosing C++ scope. Reserved double-underscore
// prefixes (e.g. "cactus_gen_target") are avoided because they are reserved for the implementation by
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

// Distinct cpp_type_names among filter bindings, in first-seen order,
// restricted to traits that carry field data. EnTT's view.each never passes
// an empty (marker) component to its lambda, and there is nothing for
// registry.get<> to bind either, so both binding emitters below (and the
// shared handler-body lambda's parameter list) need this exact dedup-and-skip-
// empty set.
std::vector<std::string> filter_bindings_with_data_cpp_types(const std::vector<FilterBinding>& bindings,
                                                             const DecoratedProgram& program) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen_cpp;
    for (const auto& binding : bindings) {
        if (!seen_cpp.insert(binding.cpp_type_name).second) {
            continue;
        }
        const auto* trait   = EnttCodegenUtils::find_trait(program, binding.lookup_name);
        const bool is_empty = trait == nullptr || trait->fields.empty();
        if (!is_empty) {
            result.push_back(binding.cpp_type_name);
        }
    }
    return result;
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
    for (const auto& cpp_name : filter_bindings_with_data_cpp_types(bindings, program)) {
        out << ind << "[[maybe_unused]] auto& " << component_local(cpp_name) << " = registry.get<" << cpp_name << ">("
            << entity_name << ");\n";
    }
}

void emit_filter_alias_bindings(std::ostringstream& out,
                                const FilterClause& filter,
                                const DecoratedProgram& program,
                                int indent) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    for (const auto& binding : filter_bindings(filter, program)) {
        if (binding.binding_name == component_local(binding.cpp_type_name)) {
            continue;
        }
        out << ind << "[[maybe_unused]] auto& " << binding.binding_name << " = "
            << component_local(binding.cpp_type_name) << ";\n";
    }
}

// `drive_from` pins which element drives iteration (EnTT's view.use<>). A
// multi-element view otherwise picks its own leading pool, which would ignore
// a registry.sort<> of any other pool — so an order-by'd rule must pin the
// same trait it sorted. Empty (the default) leaves EnTT's own choice alone.
void emit_view_declaration(std::ostringstream& out,
                           const std::vector<std::string>& cpp_type_names,
                           const std::vector<std::string>& exclude_cpp_type_names,
                           int indent,
                           const std::string& drive_from = {}) {
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
    // A single-storage view has no use<>: it always iterates the pool that was sorted.
    if (!drive_from.empty() && (cpp_type_names.size() > 1 || !exclude_cpp_type_names.empty())) {
        out << ind << "view.use<" << drive_from << ">();\n";
    }
}

void emit_view_each_header(std::ostringstream& out,
                           const std::vector<FilterBinding>& bindings,
                           int indent,
                           const DecoratedProgram& program) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    out << ind << "view.each([&](entt::entity entity";
    for (const auto& cpp_name : filter_bindings_with_data_cpp_types(bindings, program)) {
        out << ", [[maybe_unused]] " << cpp_name << "& " << component_local(cpp_name);
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

static NumericKind const_numeric_kind(const SymbolId& symbol, const DecoratedProgram& program) {
    const auto* constant = EnttCodegenUtils::find_const(program, symbol);
    if (constant == nullptr) {
        return NumericKind::Unknown;
    }
    if (constant->type.kind == TypeKind::Int) {
        return NumericKind::Int;
    }
    return constant->type.kind == TypeKind::Float ? NumericKind::Float : NumericKind::Unknown;
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
static NumericKind infer_numeric_kind(const ExprNode& expr,
                                      const std::vector<std::string>& trait_names,
                                      const DecoratedProgram& program,
                                      const LocalNumericKinds* local_kinds);

struct NumericKindResolver {
    const std::vector<std::string>* trait_names;
    const DecoratedProgram* program;
    const LocalNumericKinds* local_kinds;

    NumericKind operator()(const LiteralExpr& e) const {
        if (e.kind == LiteralExpr::Kind::Float) {
            return NumericKind::Float;
        }
        if (e.kind == LiteralExpr::Kind::Int) {
            return NumericKind::Int;
        }
        return NumericKind::Unknown;
    }

    NumericKind operator()(const IdentExpr& e) const {
        if (local_kinds != nullptr) {
            if (const auto found = local_kinds->find(e.name); found != local_kinds->end()) {
                return found->second;
            }
        }
        // Trait fields take priority, matching rewrite_expr's own IdentExpr
        // precedence (a known trait field is qualified as `comp.field`; anything
        // else, including a const, is emitted as a bare name) — checking fields
        // first here keeps this resolution order consistent with that one.
        if (e.resolved_const_id.has_value()) {
            return const_numeric_kind(*e.resolved_const_id, *program);
        }
        const auto comp = find_comp_for_field(e.name, *trait_names, *program);
        if (comp.empty()) {
            return NumericKind::Unknown;
        }
        const auto* trait = EnttCodegenUtils::find_trait(*program, comp);
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
    }

    NumericKind operator()(const MemberExpr& e) const {
        // vec2/vec3 components are always float in this type system —
        // covers the overwhelmingly common operand shape (`.x`/`.y`/`.z`)
        // without needing to resolve the object's own declared type.
        if (e.member == "x" || e.member == "y" || e.member == "z" || e.resolved_phase_dt.has_value()) {
            return NumericKind::Float;
        }
        if (e.resolved_const_id.has_value()) {
            return const_numeric_kind(*e.resolved_const_id, *program);
        }
        return NumericKind::Unknown;
    }

    NumericKind operator()(const UnaryExpr& e) const {
        return infer_numeric_kind(*e.operand, *trait_names, *program, local_kinds);
    }

    NumericKind operator()(const BinaryExpr& e) const {
        static const std::unordered_set<std::string> kArithmeticOps{"+", "-", "*", "/", "%"};
        if (!kArithmeticOps.contains(e.op)) {
            return NumericKind::Unknown;
        }
        const auto left  = infer_numeric_kind(*e.left, *trait_names, *program, local_kinds);
        const auto right = infer_numeric_kind(*e.right, *trait_names, *program, local_kinds);
        if (left == NumericKind::Unknown || right == NumericKind::Unknown) {
            return NumericKind::Unknown;
        }
        return (left == NumericKind::Float || right == NumericKind::Float) ? NumericKind::Float : NumericKind::Int;
    }

    NumericKind operator()(const CallExpr& e) const {
        const ResolvedFunc* func = nullptr;
        if (e.resolved_callee_id.has_value()) {
            func = EnttCodegenUtils::find_func(*program, *e.resolved_callee_id);
        } else if (const auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
            if (const auto found = program->funcs.find(ident->name); found != program->funcs.end()) {
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
    }

    NumericKind operator()(const IfExpr& e) const {
        return infer_numeric_kind(*e.then_expr, *trait_names, *program, local_kinds);
    }

    NumericKind operator()(const MatchExpr& e) const {
        return e.arms.empty() ? NumericKind::Unknown
                              : infer_numeric_kind(*e.arms.front().body, *trait_names, *program, local_kinds);
    }

    NumericKind operator()(const auto& /*expression*/) const {
        return NumericKind::Unknown;
    }
};

static NumericKind infer_numeric_kind(const ExprNode& expr,
                                      const std::vector<std::string>& trait_names,
                                      const DecoratedProgram& program,
                                      const LocalNumericKinds* local_kinds) {
    return std::visit(NumericKindResolver{.trait_names = &trait_names, .program = &program, .local_kinds = local_kinds},
                      expr.expr);
}

static std::string rewrite_expr(const ExprNode& expr,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases            = {},
                                const std::unordered_map<std::string, std::string>& cpp_overrides = {},
                                const PairCodegenScope* pair_scope                                = nullptr,
                                const LocalNumericKinds* local_kinds                              = nullptr);

// One collider argument's descriptor: from the pass cache inside a reduction
// fold, otherwise built fresh from the registry.
static std::string collider_shape_cpp(const PairCodegenBinding& binding, const std::string& entity) {
    if (binding.collider_shapes_cached) {
        return "cactus_shape_of_" + binding.binding_name + "(" + entity + ")";
    }
    return binding.collider_shape_builder + "(registry, " + entity + ")";
}

// physics.sweep / touching / push_out over two rule bindings, which semantic
// analysis guarantees.
static std::string lower_collider_query(ColliderQueryKind kind,
                                        const CallExpr& call,
                                        const PairCodegenScope* pair_scope,
                                        const std::function<std::string(const ExprNode&)>& rewrite) {
    const auto [subject_arg, target_arg] = collider_query_entity_args(kind);
    const auto entity_shape              = [&](std::size_t index) {
        const auto entity   = rewrite(*call.args[index]);
        const auto* ident   = std::get_if<IdentExpr>(&call.args[index]->expr);
        const auto* binding = pair_scope == nullptr || ident == nullptr ? nullptr : pair_scope->find(ident->name);
        return std::pair{entity,
                         binding == nullptr ? EnttCodegenUtils::collider_shape_builder() + "(registry, " + entity + ")"
                                            : collider_shape_cpp(*binding, entity)};
    };
    const auto [subject, subject_shape] = entity_shape(subject_arg);
    const auto [target, target_shape]   = entity_shape(target_arg);
    if (kind == ColliderQueryKind::Touching) {
        return "cactus_collider_touching(" + subject_shape + ", " + target_shape + ", " + subject + ", " + target + ")";
    }
    if (kind == ColliderQueryKind::PushOut) {
        return "cactus_collider_push_out(registry, " + subject_shape + ", " + target_shape + ", " + subject + ", " +
               target + ")";
    }
    return "cactus_collider_sweep(" + subject_shape + ", " + rewrite(*call.args[1]) + ", " + target_shape + ", " +
           subject + ", " + target + ")";
}

// Which component pool `registry.sort<T>()` physically permutes, and which
// element the sorted view is then pinned to drive iteration from. Any
// positively-filtered trait works: the comparator reads whatever traits the
// sort-key expressions name via registry.get<>, independently of which pool is
// being reordered, so this choice is arbitrary and carries no meaning beyond
// picking one pool. It is load-bearing only in that the same trait must be
// used for both the sort and the view's use<> pin — an unpinned multi-trait
// view is free to iterate from a different (unsorted) pool and would silently
// ignore the sort.
std::string primary_sort_trait(const RuleNode& sys, const DecoratedProgram& program) {
    const auto cpp_types = filter_cpp_type_names(sys.filter, program);
    return cpp_types.empty() ? std::string{} : cpp_types.front();
}

// One `[&](entt::entity) { ... }` key extractor per sort key, binding the
// rule's filter components off the passed entity exactly as a handler body
// would, so the sort-key expression lowers through the ordinary expression
// emitter (rewrite_expr) with the same names in scope it would see in the
// handler. Returns the emitted lambda names, positionally matching
// sys.order_by.
std::vector<std::string> emit_sort_key_extractors(std::ostringstream& out,
                                                  const RuleNode& sys,
                                                  const std::vector<FilterBinding>& bindings,
                                                  const std::vector<std::string>& trait_names,
                                                  const DecoratedProgram& program,
                                                  const std::unordered_map<std::string, std::string>& cpp_overrides,
                                                  int indent) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::vector<std::string> names;
    names.reserve(sys.order_by.size());
    for (const auto& key : sys.order_by) {
        auto name = gen_temp_name("sort_key", key.location);
        out << ind << "auto " << name << " = [&](entt::entity entity) {\n";
        out << ind << "    (void)entity;\n";
        emit_component_bindings_from_entity(out, bindings, "entity", indent + 1, program);
        emit_filter_alias_bindings(out, sys.filter, program, indent + 1);
        out << ind << "    return "
            << rewrite_expr(*key.expression, trait_names, program, {}, cpp_overrides) << ";\n";
        out << ind << "};\n";
        names.push_back(std::move(name));
    }
    return names;
}

// Stable-sorts the vector `rows` by keys evaluated once per row, using the
// runtime's total key order: later keys break earlier ties, and `tie_break`
// (over row indices `cactus_lhs`/`cactus_rhs`) decides rows equal on every key.
// `row_keys[i]` lowers key i for the row named `cactus_row`.
void emit_keyed_sort(std::ostringstream& out,
                     const RuleNode& sys,
                     const std::string& rows,
                     const std::vector<std::string>& row_keys,
                     int indent,
                     const std::string& tie_break = "false") {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    out << ind << "{\n";
    for (std::size_t i = 0; i < row_keys.size(); ++i) {
        const auto key_of = "cactus_key_of_" + std::to_string(i);
        const auto keys   = "cactus_keys_" + std::to_string(i);
        out << ind << "    auto " << key_of << " = [&](const auto& cactus_row) { return " << row_keys[i] << "; };\n";
        out << ind << "    std::vector<std::decay_t<decltype(" << key_of << "(" << rows << ".front()))>> " << keys
            << ";\n";
        out << ind << "    " << keys << ".reserve(" << rows << ".size());\n";
        out << ind << "    for (const auto& cactus_row : " << rows << ") {\n";
        out << ind << "        " << keys << ".push_back(" << key_of << "(cactus_row));\n";
        out << ind << "    }\n";
    }
    out << ind << "    std::vector<std::size_t> cactus_order(" << rows << ".size());\n";
    out << ind << "    for (std::size_t cactus_i = 0; cactus_i < cactus_order.size(); ++cactus_i) {\n";
    out << ind << "        cactus_order[cactus_i] = cactus_i;\n";
    out << ind << "    }\n";
    out << ind << "    std::ranges::stable_sort(cactus_order, [&](std::size_t cactus_lhs, std::size_t cactus_rhs) {\n";
    for (std::size_t i = 0; i < row_keys.size(); ++i) {
        const auto keys = "cactus_keys_" + std::to_string(i);
        out << ind << "        if (const int cactus_cmp = cactus::num::key_order(" << keys << "[cactus_lhs], " << keys
            << "[cactus_rhs], " << (sys.order_by[i].descending ? "true" : "false") << ");\n";
        out << ind << "            cactus_cmp != 0) {\n";
        out << ind << "            return cactus_cmp < 0;\n";
        out << ind << "        }\n";
    }
    out << ind << "        return " << tie_break << ";\n";
    out << ind << "    });\n";
    out << ind << "    auto cactus_sorted = " << rows << ";\n";
    out << ind << "    for (std::size_t cactus_i = 0; cactus_i < cactus_order.size(); ++cactus_i) {\n";
    out << ind << "        cactus_sorted[cactus_i] = " << rows << "[cactus_order[cactus_i]];\n";
    out << ind << "    }\n";
    out << ind << "    " << rows << " = std::move(cactus_sorted);\n";
    out << ind << "}\n";
}

// Returns the anchor trait it sorted by (see primary_sort_trait), or empty
// when the rule isn't ordered — the caller reuses this to pin the matching
// view's drive_from instead of re-deriving the same anchor a second time.
//
// The key extractors read every filter trait unconditionally (see
// emit_sort_key_extractors), so they are only safe to call on an entity that
// satisfies the rule's *full* filter/exclude — not merely one that belongs to
// the anchor trait's own pool. registry.sort<Anchor>(comparator) sorts the
// anchor's raw pool, which can contain entities missing one of the other
// filter traits (e.g. an anchor-pool entity with Ranked but not Marker), so
// calling the extractor from inside that comparator would call registry.get<>
// on a component the entity doesn't have. Instead, snapshot exactly the
// entities the real handler view will visit, sort that snapshot with the
// extractors (safe: every entity in it has every filter trait), and use
// EnTT storage's sort_as(first, last) to permute the anchor pool to match —
// it reorders entities present in both the pool and the given range to agree
// with that range's order, skipping anything else in the pool, without
// invoking a comparator over the pool's other entities.
std::string emit_sort_call(std::ostringstream& out,
                           const RuleNode& sys,
                           const std::vector<FilterBinding>& bindings,
                           const std::vector<std::string>& trait_names,
                           const DecoratedProgram& program,
                           const std::unordered_map<std::string, std::string>& cpp_overrides,
                           int indent = 1) {
    auto anchor = primary_sort_trait(sys, program);
    if (sys.order_by.empty() || anchor.empty()) {
        return {};
    }

    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const auto key_names = emit_sort_key_extractors(out, sys, bindings, trait_names, program, cpp_overrides, indent);

    const auto filter_cpp_types  = filter_cpp_type_names(sys.filter, program);
    const auto exclude_cpp_types = filter_cpp_type_names(sys.exclude, program);
    const auto snapshot_name     = gen_temp_name("sort_entities", sys.location);
    out << ind << "std::vector<entt::entity> " << snapshot_name << ";\n";
    out << ind << "for (auto entity : registry.view<";
    for (size_t i = 0; i < filter_cpp_types.size(); ++i) {
        out << (i == 0 ? "" : ", ") << filter_cpp_types[i];
    }
    out << ">(";
    if (!exclude_cpp_types.empty()) {
        out << "entt::exclude<";
        for (size_t i = 0; i < exclude_cpp_types.size(); ++i) {
            out << (i == 0 ? "" : ", ") << exclude_cpp_types[i];
        }
        out << ">";
    }
    out << ")) {\n";
    out << ind << "    " << snapshot_name << ".push_back(entity);\n";
    out << ind << "}\n";
    std::vector<std::string> row_keys;
    row_keys.reserve(key_names.size());
    for (const auto& key_name : key_names) {
        row_keys.push_back(key_name + "(cactus_row)");
    }
    // The snapshot is in pool order, so equal keys fall back to creation order.
    emit_keyed_sort(out,
                    sys,
                    snapshot_name,
                    row_keys,
                    indent,
                    "registry.get<cactus::runtime::entt_backend::CreationOrdinal>(" + snapshot_name +
                        "[cactus_lhs]).value < registry.get<cactus::runtime::entt_backend::CreationOrdinal>(" +
                        snapshot_name + "[cactus_rhs]).value");
    out << ind << "registry.storage<" << anchor << ">().sort_as(" << snapshot_name << ".begin(), " << snapshot_name
        << ".end());\n";
    return anchor;
}

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
         .call_body = [](const std::string& args) { return "::cactus_gen::detail::InputEvent::axis(" + args + ")"; }},
        {.object_name        = "input",
         .member_name        = "pressed",
         .required_arg_count = std::nullopt,
         .call_body =
             [](const std::string& args) { return "::cactus_gen::detail::InputEvent::pressed(" + args + ")"; }},
        {.object_name        = "input",
         .member_name        = "down",
         .required_arg_count = std::nullopt,
         .call_body = [](const std::string& args) { return "::cactus_gen::detail::InputEvent::down(" + args + ")"; }},
        {.object_name        = "input",
         .member_name        = "released",
         .required_arg_count = std::nullopt,
         .call_body =
             [](const std::string& args) { return "::cactus_gen::detail::InputEvent::released(" + args + ")"; }},
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

static std::string deferred_field_value_name(const FieldAssignment& assignment) {
    return gen_temp_name("field", assignment.location);
}

// Deferred field blocks take their values when the statement runs, so a
// registry-reading call sees the same moment as a trait read.
static void emit_deferred_field_values(std::ostringstream& out,
                                       const std::string& ind,
                                       const std::string& cpp_name,
                                       const std::vector<FieldAssignment>& assignments,
                                       const std::vector<std::string>& trait_names,
                                       const DecoratedProgram& program,
                                       const std::unordered_set<std::string>& pointer_aliases,
                                       const std::unordered_map<std::string, std::string>& cpp_overrides,
                                       const PairCodegenScope* pair_scope) {
    for (const auto& assignment : assignments) {
        out << ind << "const decltype(" << cpp_name << "::" << assignment.name << ") "
            << deferred_field_value_name(assignment) << " = "
            << rewrite_expr(*assignment.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope)
            << ";\n";
    }
}

static void emit_deferred_override_values(std::ostringstream& out,
                                          const std::string& ind,
                                          const std::vector<ArchetypeTraitEntry>& overrides,
                                          const std::vector<ChildOverrideNode>& child_overrides,
                                          const std::vector<std::string>& trait_names,
                                          const DecoratedProgram& program,
                                          const std::unordered_set<std::string>& pointer_aliases,
                                          const PairCodegenScope* pair_scope) {
    for (const auto& entry : overrides) {
        emit_deferred_field_values(out,
                                   ind,
                                   trait_cpp_from_entry(entry, program),
                                   entry.assignments,
                                   trait_names,
                                   program,
                                   pointer_aliases,
                                   {},
                                   pair_scope);
    }
    for (const auto& child : child_overrides) {
        emit_deferred_override_values(
            out, ind, child.traits, child.children, trait_names, program, pointer_aliases, pair_scope);
    }
}

// An override is evaluated once at construction, so when the spawned node's
// creation path retains provenance the override belongs to the construction
// data rather than counting as a later mutation. `deferred` overrides read the
// values emit_deferred_override_values hoisted before the command was queued.
static std::string emit_spawn_overrides(const std::string& entity_name,
                                        const std::vector<ArchetypeTraitEntry>& overrides,
                                        int indent,
                                        const std::vector<std::string>& trait_names,
                                        const DecoratedProgram& program,
                                        const std::unordered_set<std::string>& pointer_aliases,
                                        const PairCodegenScope* pair_scope,
                                        std::optional<std::uint32_t> origin,
                                        bool deferred) {
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
        out << ind << "    auto* " << existing_name << " = registry.try_get<" << cpp_name << ">(" << entity_name
            << ");\n";
        out << ind << "    auto " << value_name << " = " << existing_name << " ? *" << existing_name << " : "
            << cpp_name << "{};\n";
        for (const auto& assignment : override_entry.assignments) {
            out << ind << "    " << value_name << "." << assignment.name << " = "
                << (deferred ? deferred_field_value_name(assignment)
                             : rewrite_expr(*assignment.value, trait_names, program, pointer_aliases, {}, pair_scope))
                << ";\n";
        }
        out << ind << "    registry.emplace_or_replace<" << cpp_name << ">(" << entity_name << ", " << value_name
            << ");\n";
        if (EnttPersistenceEmitter::node_keeps_construction_copy(
                program, origin, override_entry.resolved_trait_id, override_entry.trait_name)) {
            out << EnttPersistenceEmitter::emit_retain_construction(cpp_name, entity_name, ind + "    ");
        }
        out << ind << "}\n";
    }
    return out.str();
}

// ── Range pre-checks of a spawn ─────────────────────────────────────────────

// The text an override assigns to a field at the check: its hoisted value, or the expression itself.
using OverrideValue = std::function<std::string(const FieldAssignment&)>;

static const TemplateNode* find_template_definition(const DecoratedProgram& program, const SymbolId& template_id) {
    if (program.ast == nullptr) {
        return nullptr;
    }
    for (const auto& decl : program.ast->declarations) {
        const auto* tmpl = std::get_if<TemplateNode>(&decl);
        if (tmpl != nullptr && tmpl->resolved_template_id == template_id) {
            return tmpl;
        }
    }
    return nullptr;
}

struct NodeRangeChecks {
    std::string module;
    std::string archetype;
    std::string arguments;
    std::string ind;
    const OverrideValue* override_value = nullptr;
    const DecoratedProgram* program     = nullptr;
    std::vector<std::string> conditions;  // one passes_range_check call per checked value
};

static void emit_node_range_check(std::ostringstream& out,
                                  NodeRangeChecks& context,
                                  const std::vector<std::string>& role_path,
                                  const ResolvedTrait& trait,
                                  const ArchetypeTraitEntry& entry,
                                  bool carried,
                                  const ArchetypeTraitEntry* override_entry) {
    const auto cpp = EnttCodegenUtils::trait_cpp_name(entry.resolved_trait_id, entry.trait_name, *context.program);
    const auto initial =
        carried ? range_value_function_name(context.module, context.archetype, role_path, trait.name).ref() + "(" +
                      context.arguments + ")"
                : cpp + "{}";
    const auto value = "cactus_gen_range_value_" + std::to_string(context.conditions.size());
    out << context.ind << "    auto " << value << " = " << initial << ";\n";
    if (override_entry != nullptr) {
        for (const auto& assignment : override_entry->assignments) {
            out << context.ind << "    " << value << "." << cpp_escaped(assignment.name) << " = "
                << (*context.override_value)(assignment) << ";\n";
        }
    }
    context.conditions.push_back(emit_range_check_passes(trait, value));
}

// NOLINTNEXTLINE(misc-no-recursion) -- walks the spawned template's child tree
static void emit_node_range_checks(std::ostringstream& out,
                                   NodeRangeChecks& context,
                                   const std::vector<ArchetypeTraitEntry>& traits,
                                   const std::vector<ChildArchetypeNode>& children,
                                   const std::vector<ArchetypeTraitEntry>& overrides,
                                   const std::vector<ChildOverrideNode>& child_overrides,
                                   std::vector<std::string>& role_path) {
    const auto ranged = [&](const ArchetypeTraitEntry& entry) {
        return find_ranged_trait(*context.program, entry.resolved_trait_id, entry.trait_name);
    };
    const auto override_of = [&](const ResolvedTrait* trait) -> const ArchetypeTraitEntry* {
        const auto found = std::ranges::find_if(overrides, [&](const auto& entry) { return ranged(entry) == trait; });
        return found == overrides.end() ? nullptr : &*found;
    };
    std::vector<const ResolvedTrait*> carried;
    for (const auto& entry : traits) {
        if (const auto* trait = ranged(entry)) {
            carried.push_back(trait);
            emit_node_range_check(out, context, role_path, *trait, entry, true, override_of(trait));
        }
    }
    for (const auto& entry : overrides) {
        if (const auto* trait = ranged(entry); trait != nullptr && !std::ranges::contains(carried, trait)) {
            emit_node_range_check(out, context, role_path, *trait, entry, false, &entry);
        }
    }
    static const std::vector<ArchetypeTraitEntry> NO_TRAITS;
    static const std::vector<ChildOverrideNode> NO_CHILDREN;
    for (const auto& child : children) {
        const auto found = std::ranges::find(child_overrides, child.role, &ChildOverrideNode::role);
        const bool has   = found != child_overrides.end();
        role_path.push_back(child.role);
        emit_node_range_checks(out,
                               context,
                               child.traits,
                               child.children,
                               has ? found->traits : NO_TRAITS,
                               has ? found->children : NO_CHILDREN,
                               role_path);
        role_path.pop_back();
    }
}

// `const bool <ok> = ...;`: whether every ranged trait the spawned tree would carry, with the
// site's overrides applied, passes its check. Empty when the spawn makes no ranged value.
static std::string emit_spawn_range_precheck(const SymbolId& template_id,
                                             const std::vector<ArchetypeTraitEntry>& root_overrides,
                                             const std::vector<ChildOverrideNode>& child_overrides,
                                             const std::string& ok_name,
                                             const std::string& ind,
                                             const OverrideValue& override_value,
                                             const DecoratedProgram& program) {
    const auto* tmpl = find_template_definition(program, template_id);
    if (tmpl == nullptr) {
        return {};
    }
    std::string arguments;
    for (const auto& slot : tmpl->initializers) {
        if (slot.value == nullptr) {
            arguments += (arguments.empty() ? "" : ", ") + initializer_slot_name(slot.index);
        }
    }
    NodeRangeChecks context{.module         = template_creation_module(template_id, program),
                            .archetype      = template_id.local_name,
                            .arguments      = std::move(arguments),
                            .ind            = ind,
                            .override_value = &override_value,
                            .program        = &program,
                            .conditions     = {}};
    std::ostringstream values;
    std::vector<std::string> role_path;
    emit_node_range_checks(values, context, tmpl->traits, tmpl->children, root_overrides, child_overrides, role_path);
    if (context.conditions.empty()) {
        return {};
    }
    // `&&` stops at the first failure, so one spawn reports one fault.
    return ind + "const bool " + ok_name + " = [&]() {\n" + values.str() + ind + "    return " +
           join_strings(context.conditions, " && ") + ";\n" + ind + "}();\n";
}

// `precheck`, then `on_fail` when it failed. Empty when the spawn makes no ranged value.
static std::string emit_spawn_range_guard(const std::string& precheck,
                                          const std::string& ok_name,
                                          const std::string& ind,
                                          const std::string& on_fail) {
    if (precheck.empty()) {
        return {};
    }
    return precheck + ind + "if (!" + ok_name + ") { " + on_fail + " }\n";
}

static OverrideValue spawn_override_value(bool deferred,
                                          const std::vector<std::string>& trait_names,
                                          const DecoratedProgram& program,
                                          const std::unordered_set<std::string>& pointer_aliases,
                                          const PairCodegenScope* pair_scope) {
    return [=, &trait_names, &program, &pointer_aliases](const FieldAssignment& assignment) {
        return deferred ? deferred_field_value_name(assignment)
                        : rewrite_expr(*assignment.value, trait_names, program, pointer_aliases, {}, pair_scope);
    };
}

// ── Hierarchical spawn expansion (dsl-hierarchical-entity-templates D9) ─────

static const TemplateNode* find_template_node(const DecoratedProgram& program,
                                                                     const SymbolId& template_id) {
    const auto* tmpl = find_template_definition(program, template_id);
    return tmpl != nullptr && !tmpl->children.empty() ? tmpl : nullptr;
}

// Spawn sites with child overrides expand the tree inline: create each node
// via its per-node helper in parent-first preorder, emplace Parent on non-root
// nodes, and apply that node's overrides in handler scope.
static void emit_spawn_child_expansion(std::ostringstream& out,
                                       const std::string& template_module,
                                       const std::string& template_local_name,
                                       const SymbolId& archetype,
                                       const std::vector<ChildArchetypeNode>& children,
                                       const std::vector<ChildOverrideNode>& overrides,
                                       const std::string& parent_var,
                                       const std::string& var_prefix,
                                       std::vector<std::string>& role_path,
                                       const std::vector<std::string>& trait_names,
                                       const DecoratedProgram& program,
                                       const std::unordered_set<std::string>& pointer_aliases,
                                       const PairCodegenScope* pair_scope,
                                       const std::string& arguments,
                                       bool deferred) {
    static const std::vector<ChildOverrideNode> NO_OVERRIDES;
    std::size_t index = 0;
    for (const auto& child : children) {
        const std::string var = var_prefix + "_" + std::to_string(index);
        role_path.push_back(child.role);
        out << "    auto " << var << " = "
            << archetype_node_create_function_name(template_module, template_local_name, role_path).ref() << "(registry"
            << arguments << ");\n";
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
                var,
                override_node->traits,
                1,
                trait_names,
                program,
                pointer_aliases,
                pair_scope,
                EnttPersistenceEmitter::archetype_origin_index(program, archetype, role_path),
                deferred);
        }

        emit_spawn_child_expansion(out,
                                   template_module,
                                   template_local_name,
                                   archetype,
                                   child.children,
                                   override_node != nullptr ? override_node->children : NO_OVERRIDES,
                                   var,
                                   var,
                                   role_path,
                                   trait_names,
                                   program,
                                   pointer_aliases,
                                   pair_scope,
                                   arguments,
                                   deferred);
        role_path.pop_back();
        ++index;
    }
}

static std::string emit_template_bindings(std::ostringstream& out,
                                          const TemplateArguments& arguments,
                                          const std::string& indent,
                                          const std::vector<std::string>& trait_names,
                                          const DecoratedProgram& program,
                                          const std::unordered_set<std::string>& pointer_aliases,
                                          const PairCodegenScope* pair_scope);

static std::string emit_hierarchical_spawn_expansion(const SymbolId& template_id,
                                                     const std::vector<ArchetypeTraitEntry>& root_overrides,
                                                     const std::vector<ChildOverrideNode>& child_overrides,
                                                     const TemplateNode& template_node,
                                                     const TemplateArguments& bindings,
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
    emit_template_bindings(out, bindings, "    ", trait_names, program, pointer_aliases, pair_scope);
    std::string arguments;
    for (const auto& slot : template_node.initializers) {
        arguments += ", " + initializer_slot_name(slot.index);
        if (slot.value != nullptr) {
            out << "    [[maybe_unused]] const " << EnttCodegenUtils::value_type_to_cpp(slot.type) << " "
                << initializer_slot_name(slot.index) << " = "
                << rewrite_expr(*slot.value, trait_names, program, pointer_aliases, {}, pair_scope) << ";\n";
        }
    }
    const bool graph_runtime = !program.execution_graph.phases.empty();
    // "spawned"/"committed" are arbitrary-looking, ordinary-sounding names a DSL author could
    // plausibly bind via `let`; mangle them so an override expression referencing such a
    // variable can't be captured by these compiler-internal declarations (same class of bug as
    // the target/parent self-reference cases above).
    const std::string spawned_name   = gen_temp_name("spawned", location);
    const std::string committed_name = gen_temp_name("committed", location);
    const auto overrides             = [&](const std::string& entity_name, int indent) {
        return emit_spawn_overrides(entity_name,
                                    root_overrides,
                                    indent,
                                    trait_names,
                                    program,
                                    pointer_aliases,
                                    pair_scope,
                                    EnttPersistenceEmitter::archetype_origin_index(program, template_id),
                                    graph_runtime);
    };
    const std::string ranges_ok = gen_temp_name("ranges_ok", location);
    const auto precheck         = [&](const std::string& ind) {
        return emit_spawn_range_precheck(template_id,
                                         root_overrides,
                                         child_overrides,
                                         ranges_ok,
                                         ind,
                                         spawn_override_value(graph_runtime, trait_names, program, pointer_aliases, pair_scope),
                                         program);
    };
    if (graph_runtime) {
        emit_deferred_override_values(
            out, "    ", root_overrides, child_overrides, trait_names, program, pointer_aliases, pair_scope);
        out << "    auto " << spawned_name << " = cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        out << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        out << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Spawn,\n";
        out << "        [=](entt::registry& registry) mutable {\n";
        out << emit_spawn_range_guard(precheck("            "), ranges_ok, "            ", "return;");
        out << "            auto " << committed_name << " = "
            << archetype_node_create_at_function_name(tmpl_module, tmpl_local, role_path).ref() << "(registry, "
            << spawned_name << arguments << ");\n";
        out << overrides(committed_name, 3);
    } else {
        out << emit_spawn_range_guard(precheck("    "), ranges_ok, "    ", "return entt::entity{entt::null};");
        out << "    auto " << spawned_name << " = "
            << archetype_node_create_function_name(tmpl_module, tmpl_local, role_path).ref() << "(registry" << arguments
            << ");\n";
        out << overrides(spawned_name, 1);
    }
    emit_spawn_child_expansion(out,
                               tmpl_module,
                               tmpl_local,
                               template_id,
                               template_node.children,
                               child_overrides,
                               graph_runtime ? committed_name : spawned_name,
                               "child",
                               role_path,
                               trait_names,
                               program,
                               pointer_aliases,
                               pair_scope,
                               arguments,
                               graph_runtime);
    if (graph_runtime) {
        out << "        });\n";
    }
    out << "    return " << spawned_name << ";\n";
    out << "})()";
    return out.str();
}

static std::string emit_template_bindings(std::ostringstream& out,
                                          const TemplateArguments& arguments,
                                          const std::string& indent,
                                          const std::vector<std::string>& trait_names,
                                          const DecoratedProgram& program,
                                          const std::unordered_set<std::string>& pointer_aliases,
                                          const PairCodegenScope* pair_scope) {
    for (const auto& binding : arguments.bindings) {
        out << indent << "const " << EnttCodegenUtils::value_type_to_cpp(binding.type) << " "
            << initializer_slot_name(binding.index) << " = "
            << rewrite_expr(*binding.value, trait_names, program, pointer_aliases, {}, pair_scope) << ";\n";
    }
    // Bindings are written in evaluation order (explicit arguments first, then
    // defaults); the callee declares its slots in parameter order, so the call
    // must pass them sorted by slot index rather than by binding position.
    std::vector<std::size_t> indices;
    indices.reserve(arguments.bindings.size());
    for (const auto& binding : arguments.bindings) {
        indices.push_back(binding.index);
    }
    std::ranges::sort(indices);
    std::string values;
    for (const auto index : indices) {
        values += ", " + initializer_slot_name(index);
    }
    return values;
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
        if (const auto* node = find_template_node(program, tmpl_id)) {
            return emit_hierarchical_spawn_expansion(tmpl_id,
                                                     spawn.overrides,
                                                     spawn.child_overrides,
                                                     *node,
                                                     spawn.arguments,
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
    const auto arguments =
        emit_template_bindings(out, spawn.arguments, "    ", trait_names, program, pointer_aliases, pair_scope);
    const bool graph_runtime = !program.execution_graph.phases.empty();
    const auto overrides     = [&](int indent) {
        return emit_spawn_overrides(spawned_name,
                                    spawn.overrides,
                                    indent,
                                    trait_names,
                                    program,
                                    pointer_aliases,
                                    pair_scope,
                                    EnttPersistenceEmitter::archetype_origin_index(program, tmpl_id),
                                    graph_runtime);
    };
    const std::string ranges_ok = gen_temp_name("ranges_ok", spawn.location);
    const auto precheck         = [&](const std::string& ind) {
        return emit_spawn_range_precheck(tmpl_id,
                                         spawn.overrides,
                                         {},
                                         ranges_ok,
                                         ind,
                                         spawn_override_value(graph_runtime, trait_names, program, pointer_aliases, pair_scope),
                                         program);
    };
    if (graph_runtime) {
        emit_deferred_override_values(
            out, "    ", spawn.overrides, {}, trait_names, program, pointer_aliases, pair_scope);
        out << "    auto " << spawned_name << " = cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        out << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        out << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Spawn,\n";
        out << "        [=](entt::registry& registry) mutable {\n";
        out << emit_spawn_range_guard(precheck("            "), ranges_ok, "            ", "return;");
        out << "            " << template_create_at_ref(tmpl_id, program) << "(registry, " << spawned_name << arguments
            << ");\n";
        out << overrides(3);
        out << "        });\n";
    } else {
        out << emit_spawn_range_guard(precheck("    "), ranges_ok, "    ", "return entt::entity{entt::null};");
        out << "    auto " << spawned_name << " = " << template_create_ref(tmpl_id, program) << "(registry" << arguments
            << ");\n";
        out << overrides(1);
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

// The view of the children a query.children call can yield.
static std::string hierarchy_child_view(const QueryCallExpr& qcall, const DecoratedProgram& program) {
    return "registry.view" +
           build_view_suffix(qcall.filters, EnttCodegenUtils::trait_cpp_name("Parent", program), &program);
}

// The arguments a hierarchy_preorder or hierarchy_postorder call passes to hierarchy_order.
static std::string hierarchy_order_args(const QueryCallExpr& qcall,
                                        const std::string& func_name,
                                        const DecoratedProgram& program) {
    return "registry, " + EnttCodegenUtils::hierarchy_index_expr(program) + ", registry.view" +
           build_view_suffix(qcall.filters, {}, &program) + ", " +
           (func_name == "hierarchy_postorder" ? "true" : "false");
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
        return "cactus::runtime::entt_backend::oldest(registry, " + view + ")";
    }
    if (func_name == "all") {
        return "cactus::runtime::entt_backend::creation_ordered(registry, " + view + ")";
    }
    if (func_name == "children") {
        const std::string of_expr = find_named_arg_value(qcall.named_args, "of", emit_arg);
        return "cactus::runtime::entt_backend::hierarchy_children(registry, " +
               EnttCodegenUtils::hierarchy_index_expr(program) + ", " + of_expr + ", " +
               hierarchy_child_view(qcall, program) + ")";
    }
    if (func_name == "hierarchy_preorder" || func_name == "hierarchy_postorder") {
        return "cactus::runtime::entt_backend::hierarchy_order(" + hierarchy_order_args(qcall, func_name, program) +
               ")";
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
               parent_comp + " != nullptr) { return " + parent_comp + "->parent; } return entt::entity{entt::null}; }()";
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
        return "cactus::runtime::entt_backend::query_nearest(registry, " + view + ", " + from + ", " + position_of + ")";
    }
    if (func_name == "overlap_box") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string size   = find_named_arg_value(qcall.named_args, "size", emit_arg);
        return "cactus::runtime::entt_backend::query_overlap_box(registry, " + view + ", " + center + ", " + size + ", " +
               position_of + ")";
    }
    if (func_name == "overlap_circle") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string radius = find_named_arg_value(qcall.named_args, "radius", emit_arg);
        return "cactus::runtime::entt_backend::query_overlap_circle(registry, " + view + ", " + center + ", " + radius +
               ", " + position_of + ")";
    }
    if (func_name == "raycast") {
        const std::string origin   = find_named_arg_value(qcall.named_args, "origin", emit_arg);
        const std::string dir      = find_named_arg_value(qcall.named_args, "dir", emit_arg);
        const std::string max_dist = find_named_arg_value(qcall.named_args, "max_dist", emit_arg);
        return "cactus::runtime::entt_backend::query_raycast(registry, " + view + ", " + origin + ", " + dir + ", " +
               max_dist + ", " + position_of + ")";
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
        return "cactus::runtime::entt_backend::query_nearest(registry, " + view + ", " + from + ", " + position_of + ")";
    }
    if (func_name == "overlap_box") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string size   = find_named_arg_value(qcall.named_args, "size", emit_arg);
        return "cactus::runtime::entt_backend::query_overlap_box(registry, " + view + ", " + center + ", " + size + ", " +
               position_of + ")";
    }
    if (func_name == "overlap_sphere") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string radius = find_named_arg_value(qcall.named_args, "radius", emit_arg);
        return "cactus::runtime::entt_backend::query_overlap_sphere(registry, " + view + ", " + center + ", " + radius +
               ", " + position_of + ")";
    }
    if (func_name == "raycast") {
        const std::string origin   = find_named_arg_value(qcall.named_args, "origin", emit_arg);
        const std::string dir      = find_named_arg_value(qcall.named_args, "dir", emit_arg);
        const std::string max_dist = find_named_arg_value(qcall.named_args, "max_dist", emit_arg);
        return "cactus::runtime::entt_backend::query_raycast(registry, " + view + ", " + origin + ", " + dir + ", " +
               max_dist + ", " + position_of + ")";
    }
    return "/* unsupported std.physics.volume.query func: " + func_name + " */";
}

// The sibling-local (z_index, creation ordinal) stacking order behind
// ComputedLayout.draw_order. It is a std.ui function, not a sortable std.query,
// so std.query stays free of widget z-index policy.
static std::string lower_ui_query_call(const QueryCallExpr& qcall,
                                       const std::string& func_name,
                                       const auto& emit_arg,
                                       const DecoratedProgram& program) {
    if (func_name == "stacking_order") {
        const std::string of_expr  = find_named_arg_value(qcall.named_args, "of", emit_arg);
        const std::string node_cpp = EnttCodegenUtils::trait_cpp_name("std.ui.Node", program);
        const std::string callee   = reads_paint_order_cache()
                                         ? "cactus_paint_order.order_of("
                                         : "cactus::runtime::entt_backend::hierarchy_stacking_order(";
        return callee + "registry, " + EnttCodegenUtils::hierarchy_index_expr(program) + ", registry.view<" +
               node_cpp + ">(), " + of_expr + ", [&](entt::entity e) { return registry.get<" + node_cpp +
               ">(e).z_index; })";
    }
    return "/* unsupported std.ui func: " + func_name + " */";
}

struct QueryTarget {
    std::string module;
    std::string function;
};

// Without a resolved callee, the modules whose `use` the call's qualifier matches, in declaration order.
static std::vector<QueryTarget> unresolved_query_targets(const QueryCallExpr& qcall, const DecoratedProgram& program) {
    std::vector<QueryTarget> targets;
    const auto* member = qcall.callee == nullptr ? nullptr : std::get_if<MemberExpr>(&qcall.callee->expr);
    if (member == nullptr || program.ast == nullptr) {
        return targets;
    }
    for (const auto& decl : program.ast->declarations) {
        const auto* use_node = std::get_if<UseNode>(&decl);
        if (use_node == nullptr) {
            continue;
        }
        const auto* ident  = std::get_if<IdentExpr>(&member->object->expr);
        const bool matches = use_node->alias.has_value() ? ident != nullptr && ident->name == *use_node->alias
                                                         : dotted_name_of(*member->object) == use_node->module_name;
        if (matches) {
            targets.push_back({.module = use_node->module_name, .function = member->member});
        }
    }
    return targets;
}

// The std.query function a call names, if it names one.
static std::optional<std::string> std_query_function(const QueryCallExpr& qcall, const DecoratedProgram& program) {
    if (qcall.resolved_callee_id.has_value()) {
        return qcall.resolved_callee_id->module.name == "std.query"
                   ? std::optional{qcall.resolved_callee_id->local_name}
                   : std::nullopt;
    }
    for (const auto& target : unresolved_query_targets(qcall, program)) {
        if (target.module == "std.query") {
            return target.function;
        }
    }
    return std::nullopt;
}

static std::string lower_query_by_module(const auto& qcall,
                                         const DecoratedProgram& program,
                                         const auto& emit_arg,
                                         const std::string& module,
                                         const std::string& func_name) {
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
}

static std::string lower_query_call_expr(const QueryCallExpr& qcall,
                                         const DecoratedProgram& program,
                                         const auto& emit_arg) {
    if (!qcall.resolved_callee_id.has_value()) {
        for (const auto& target : unresolved_query_targets(qcall, program)) {
            auto result = lower_query_by_module(qcall, program, emit_arg, target.module, target.function);
            if (!result.empty()) {
                return result;
            }
        }
        return "/* unresolved query call */";
    }
    const std::string& module    = qcall.resolved_callee_id->module.name;
    const std::string& func_name = qcall.resolved_callee_id->local_name;
    auto result                  = lower_query_by_module(qcall, program, emit_arg, module, func_name);
    if (!result.empty()) {
        return result;
    }
    return "/* unrecognized query module: " + module + " */";
}

// `R(x, y)` is a pair lookup; `R(x, _)` and `R(_, y)` look up one end's index.
static std::string lower_relation_test(const RelationTestExpr& test,
                                       const std::function<std::string(const ExprNode&)>& rewrite) {
    const auto store = EnttCodegenUtils::relation_store_cpp(test.relation);
    if (test.from == nullptr) {
        return store + ".has_to(" + rewrite(*test.to) + ")";
    }
    if (test.to == nullptr) {
        return store + ".has_from(" + rewrite(*test.from) + ")";
    }
    return store + ".contains(" + rewrite(*test.from) + ", " + rewrite(*test.to) + ")";
}

// An immediately invoked lambda evaluates the subject once and only the selected arm.
// Semantic analysis guarantees exhaustiveness, so the last arm needs no test.
static std::string lower_match_expr(const MatchExpr& match_expr,
                                    const std::function<std::string(const ExprNode&)>& rewrite) {
    const auto subject = gen_temp_name("match_subject", match_expr.location);
    const auto subject_value = rewrite(*match_expr.subject) + (match_expr.on_state_slot ? ".index" : "");
    std::string lowered = "[&]() { const auto " + subject + " = " + subject_value + "; ";
    if (match_expr.on_state_slot) {
        lowered += "switch (" + subject + ") { ";
    }
    for (std::size_t index = 0; index < match_expr.arms.size(); ++index) {
        const auto& arm      = match_expr.arms[index];
        const auto selected = "return " + rewrite(*arm.body) + "; ";
        const bool last     = index + 1 == match_expr.arms.size() || is_wildcard_pattern(*arm.pattern);
        if (match_expr.on_state_slot) {
            lowered += last ? "default: { " : "case " + std::to_string(*arm.state_variant) + ": { ";
            lowered += selected;
            lowered += "} ";
        } else if (last) {
            lowered += selected;
        } else {
            lowered += "if (" + subject + " == " + rewrite(*arm.pattern) + ") { ";
            lowered += selected;
            lowered += "} ";
        }
        if (last) {
            break;
        }
    }
    return lowered + (match_expr.on_state_slot ? "} }()" : "}()");
}

// `X[T].a.b`: the named slot reference or the pair binding's component, then
// the field path. A color field's channel reads as a normalized float.
static std::string lower_trait_field_path(const TraitAccessExpr& access,
                                          const std::vector<std::string>& fields,
                                          const DecoratedProgram& program,
                                          const PairCodegenScope* pair_scope) {
    if (!access.resolved_trait_id.has_value()) {
        throw CodegenError(access.location, "unresolved trait access '" + access.trait_name + "' reached code generation");
    }
    const auto& trait_id = *access.resolved_trait_id;
    std::string base;
    if (const auto named = access.named_ref(); named.has_value()) {
        base = EnttCodegenUtils::named_trait_ref_name(*named);
    } else {
        const auto* ident   = std::get_if<IdentExpr>(&access.object->expr);
        const auto* binding = ident != nullptr && pair_scope != nullptr ? pair_scope->find(ident->name) : nullptr;
        if (binding == nullptr) {
            throw CodegenError(access.location, "trait access '" + access.trait_name + "' has no named entity or pair binding");
        }
        base = "registry.get<" + std::string(binding->writable ? "" : "const ") +
               EnttCodegenUtils::trait_cpp_name(trait_id, access.trait_name, program) + ">(" + ident->name + ")";
    }
    const auto* trait = EnttCodegenUtils::find_trait(program, make_canonical_id(trait_id));
    if (fields.size() == 2 && trait != nullptr && EnttCodegenUtils::is_color_component_member(fields[1])) {
        const auto field = std::ranges::find(trait->fields, fields[0], &ResolvedField::name);
        if (field != trait->fields.end() && field->type.kind == TypeKind::Color) {
            return "(static_cast<float>(" + base + "." + fields[0] + "." + fields[1] + ") / 255.0F)";
        }
    }
    for (const auto& field : fields) {
        base += "." + cpp_escaped(field);
    }
    return base;
}

static std::string rewrite_expr_node(  // NOLINT(readability-function-cognitive-complexity) -- still 250 after
                                       // table-driving the input/camera/transform builtin dispatch (task 5.4);
                                       // this is the full expression-rewrite dispatch, dozens of unrelated arms remain
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
                    return EnttCodegenUtils::float_literal_cpp(e.value);
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
                if (e.template_slot.has_value()) {
                    return initializer_slot_name(*e.template_slot);
                }
                if (e.resolved_entity_id.has_value()) {
                    return EnttCodegenUtils::named_slot_name(*e.resolved_entity_id);
                }
                if (e.resolved_const_id.has_value()) {
                    return EnttCodegenUtils::const_reference_cpp(program, *e.resolved_const_id);
                }
                if (auto lowered = lowered_trigger_reference(expr)) {
                    return *std::move(lowered);
                }
                if (is_input_action_name(program, e.name)) {
                    return "::cactus_gen::detail::" + input_action_constant_name(e.name);
                }
                // If it's a known trait field, qualify it with the canonical variable name
                if (known_fields.contains(e.name)) {
                    auto comp = find_comp_for_field(e.name, trait_names, program);
                    if (!comp.empty()) {
                        const auto ovr = cpp_overrides.find(comp);
                        const auto& cpp =
                            ovr != cpp_overrides.end() ? ovr->second : EnttCodegenUtils::trait_cpp_name(comp, program);
                        return component_local(cpp) + "." + cpp_escaped(e.name);
                    }
                }
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return EnttCodegenUtils::emit_binary(
                    e,
                    rewrite_expr(*e.left, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds),
                    rewrite_expr(
                        *e.right, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds));
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return EnttCodegenUtils::emit_unary(
                    e,
                    rewrite_expr(
                        *e.operand, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds));
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                if (e.resolved_struct_id.has_value()) {
                    return EnttCodegenUtils::emit_struct_construction(e, program, [&](const ExprNode& argument) {
                        return rewrite_expr(
                            argument, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                    });
                }
                if (const auto kind = collider_query_kind(e); kind.has_value()) {
                    return lower_collider_query(*kind, e, pair_scope, [&](const ExprNode& arg) {
                        return rewrite_expr(
                            arg, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                    });
                }
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
                    if (const auto* func = EnttCodegenUtils::find_func(program, *e.resolved_callee_id);
                        func != nullptr && !func->is_extern) {
                        return cpp_type_ref(*e.resolved_callee_id) + "(" +
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
                        return "::cactus_gen::detail::InputEvent::" + ident->name + "(" +
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
                // color(...) constructor: route through the shared runtime
                // clamp-then-quantize helper (design.md Decision 2) instead of
                // a raw `color(...)` call; reuses rewrite_vec_arg's int->float
                // promotion, same as the vec2/vec3 case above.
                if (const auto* color_ident = std::get_if<IdentExpr>(&e.callee->expr);
                    color_ident != nullptr && color_ident->name == "color") {
                    std::string args;
                    for (size_t i = 0; i < e.args.size(); ++i) {
                        if (i > 0) {
                            args += ", ";
                        }
                        args += rewrite_vec_arg(
                            *e.args[i], trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                    }
                    return "color_from_components(" + args + ")";
                }
                return rewrite_expr(
                           *e.callee, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds) +
                       "(" +
                       join_rewritten_args(
                           e.args, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds) +
                       ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (e.resolved_phase_dt.has_value()) {
                    return EnttCodegenUtils::phase_dt_cpp_name(*e.resolved_phase_dt);
                }
                if (auto lowered = lowered_trigger_reference(expr)) {
                    return *std::move(lowered);
                }
                if (auto lowered = lowered_trigger_reference(*e.object)) {
                    return *std::move(lowered) + "." + cpp_escaped(e.member);
                }
                if (const auto path = trait_field_path(e); path.has_value()) {
                    return lower_trait_field_path(*path->first, path->second, program, pair_scope);
                }
                if (const auto* owner = std::get_if<IdentExpr>(&e.object->expr); owner != nullptr && pair_scope != nullptr) {
                    if (const auto* binding = pair_scope->find(owner->name); binding != nullptr && binding->row) {
                        const bool end = e.member == "from" || e.member == "to";
                        return owner->name + (end ? "." : ".row.") + cpp_escaped(e.member);
                    }
                }
                if (auto named = EnttCodegenUtils::named_member_cpp(e)) {
                    return *std::move(named);
                }
                if (e.resolved_const_id.has_value()) {
                    return EnttCodegenUtils::const_reference_cpp(program, *e.resolved_const_id);
                }
                if (e.resolved_asset_id.has_value()) {
                    return e.resolved_asset_id->local_name;
                }
                // Three-level access module.EnumName.Variant: emit CanonicalEnum::Variant.
                if (const auto* enum_member = std::get_if<MemberExpr>(&e.object->expr)) {
                    if (std::get_if<IdentExpr>(&enum_member->object->expr) != nullptr &&
                        EnttCodegenUtils::find_enum(program, enum_member->member) != nullptr) {
                        return EnttCodegenUtils::enum_cpp_name(enum_member->member, program) +
                               "::" + cpp_escaped(e.member);
                    }
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    // Enum names — use :: notation with canonical C++ name
                    if (EnttCodegenUtils::find_enum(program, ident->name) != nullptr) {
                        return EnttCodegenUtils::enum_cpp_name(ident->name, program) + "::" + cpp_escaped(e.member);
                    }
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (pointer_aliases.contains(ident->name)) {
                        return ident->name + "->" + e.member;
                    }
                    // A color-typed local/parameter's `.r`/`.g`/`.b`/`.a`
                    // needs the same normalized-float read every other color
                    // access uses (dsl-vector-expressions "Color float
                    // component access") — the trait-access branch above
                    // already handles this for a named or pair trait access;
                    // this is the same fix for a plain local value (e.g. a
                    // `func`'s `color`-typed parameter).
                    if (local_kinds != nullptr && EnttCodegenUtils::is_color_component_member(e.member)) {
                        if (const auto found = local_kinds->find(ident->name);
                            found != local_kinds->end() && found->second == NumericKind::Color) {
                            return "(static_cast<float>(" + ident->name + "." + e.member + ") / 255.0F)";
                        }
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
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                return EnttCodegenUtils::emit_if_ternary(e, [&](const ExprNode& child) {
                    return rewrite_expr(child, trait_names, program, pointer_aliases, cpp_overrides, pair_scope,
                                        local_kinds);
                });
            } else if constexpr (std::is_same_v<E, TraitAccessExpr>) {
                return lower_trait_field_path(e, {}, program, pair_scope);
            } else if constexpr (std::is_same_v<E, RelationTestExpr>) {
                return lower_relation_test(e, [&](const ExprNode& child) {
                    return rewrite_expr(child, trait_names, program, pointer_aliases, cpp_overrides, pair_scope,
                                        local_kinds);
                });
            } else {
                static_assert(std::is_same_v<E, MatchExpr>, "every expression form needs a C++ lowering");
                return lower_match_expr(e, [&](const ExprNode& child) {
                    return rewrite_expr(child, trait_names, program, pointer_aliases, cpp_overrides, pair_scope,
                                        local_kinds);
                });
            }
        },
        expr.expr);
}

// A non-accumulating access of a buffered field first applies that cell's pending deltas.
static std::string settled_access(const std::optional<TraitFieldRef>& access,
                                  std::string lowered,
                                  const DecoratedProgram& program) {
    const auto* plan = AccumulationScope::active();
    if (!access.has_value() || plan == nullptr || !plan->buffered(*access)) {
        return lowered;
    }
    return AccumulationPlan::table_name(access->trait, *access->field, program) + "().settle(" + lowered + ")";
}

static std::string rewrite_expr(const ExprNode& expr,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases,
                                const std::unordered_map<std::string, std::string>& cpp_overrides,
                                const PairCodegenScope* pair_scope,
                                const LocalNumericKinds* local_kinds) {
    return settled_access(
        expr.field_read,
        rewrite_expr_node(expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds),
        program);
}

// The entity an accumulating assignment adds to: a named or pair-bound trait's owner, else the row.
static std::string accumulation_entity(const VarAssign& assign,
                                       const std::vector<std::string>& trait_names,
                                       const DecoratedProgram& program,
                                       const PairCodegenScope* pair_scope) {
    const ExprNode* cursor = assign.target.get();
    while (cursor != nullptr) {
        if (const auto* access = std::get_if<TraitAccessExpr>(&cursor->expr)) {
            return access->resolved_entity_id.has_value()
                       ? EnttCodegenUtils::named_slot_name(*access->resolved_entity_id)
                       : rewrite_expr(*access->object, trait_names, program, {}, {}, pair_scope);
        }
        const auto* member = std::get_if<MemberExpr>(&cursor->expr);
        cursor             = member == nullptr ? nullptr : member->object.get();
    }
    return "entity";
}

// `, 1` or `, 2` when the assignment targets the y or z component of a vector field.
static std::string accumulation_component(const VarAssign& assign, const AccumulationTarget& target) {
    std::string last = assign.path.empty() ? assign.name : assign.path.back();
    if (const auto* member = assign.target == nullptr ? nullptr : std::get_if<MemberExpr>(&assign.target->expr)) {
        last = member->member;
    }
    if (last == target.field) {
        return {};
    }
    if (last == "y") {
        return ", 1";
    }
    return last == "z" ? ", 2" : std::string{};
}

// ── Rewrite statement: replace field[i] = with comp.field = ─────────────────

static std::string emit_trait_match_stmt(const MatchStmt& match_stmt,
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
        if (arm.is_wildcard()) {
            out << ind << "        " << (first ? "if (true)" : "else") << " {\n";
            auto wildcard_locals = lexical_locals;
            auto wildcard_kinds  = local_kinds;
            out << rewrite_stmt_block(arm.body,
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
            break;
        }
        const auto trait_name     = arm.trait_name();
        const std::string cpp_arm = EnttCodegenUtils::trait_cpp_name(arm.resolved_trait_id, trait_name, program);
        const auto simple_name    = trait_name.rfind('.') != std::string::npos
                                        ? trait_name.substr(trait_name.rfind('.') + 1)
                                        : trait_name;
        const auto* TRAIT_INFO    = EnttCodegenUtils::find_trait(
            program, arm.resolved_trait_id.has_value() ? make_canonical_id(*arm.resolved_trait_id) : simple_name);
        const bool IS_MARKER                        = TRAIT_INFO == nullptr || TRAIT_INFO->fields.empty();
        std::unordered_set<std::string> arm_aliases = pointer_aliases;
        auto arm_locals                             = lexical_locals;
        auto arm_kinds                              = local_kinds;

        out << ind << "        " << (first ? "if" : "else if") << " (";
        if (IS_MARKER || !arm.alias.has_value()) {
            out << "registry.all_of<" << cpp_arm << ">(" << match_entity << ")) {\n";
        } else {
            arm_aliases.insert(*arm.alias);
            arm_locals.insert(*arm.alias);
            out << "auto* " << *arm.alias << " = registry.try_get<" << cpp_arm << ">(" << match_entity << ")) {\n";
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

    out << ind << "    }\n";
    out << ind << "}\n";
    return out.str();
}

// A `switch` on the slot index; an arm alias binds the entity's own variant component.
static std::string emit_state_match_stmt(const MatchStmt& match_stmt,
                                         int indent,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases,
                                         bool dispatcher_available,
                                         const std::unordered_map<std::string, std::string>& cpp_overrides,
                                         const PairCodegenScope* pair_scope,
                                         const LexicalLocalBindings& lexical_locals,
                                         const LocalNumericKinds& local_kinds) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::ostringstream out;
    out << ind << "switch ("
        << rewrite_expr(
               *match_stmt.subject, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, &local_kinds)
        << ".index) {\n";
    for (const auto& arm : match_stmt.arms) {
        out << ind << "    "
            << (arm.state_variant.has_value() ? "case " + std::to_string(*arm.state_variant) + ":" : "default:")
            << " {\n";
        auto arm_locals = lexical_locals;
        auto arm_kinds  = local_kinds;
        if (arm.alias.has_value()) {
            out << ind << "        auto& " << *arm.alias << " = registry.get<"
                << EnttCodegenUtils::trait_cpp_name(arm.resolved_trait_id, arm.trait_name(), program) << ">(entity);\n";
            arm_locals.insert(*arm.alias);
        }
        out << rewrite_stmt_block(arm.body,
                                  indent + 2,
                                  trait_names,
                                  program,
                                  pointer_aliases,
                                  dispatcher_available,
                                  cpp_overrides,
                                  pair_scope,
                                  arm_locals,
                                  arm_kinds);
        out << ind << "        break;\n";
        out << ind << "    }\n";
    }
    out << ind << "}\n";
    return out.str();
}

// Semantic analysis guarantees exhaustiveness, so the last arm is a plain `else`.
static std::string emit_value_match_stmt(const MatchStmt& match_stmt,
                                         int indent,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases,
                                         bool dispatcher_available,
                                         const std::unordered_map<std::string, std::string>& cpp_overrides,
                                         const PairCodegenScope* pair_scope,
                                         const LexicalLocalBindings& lexical_locals,
                                         const LocalNumericKinds& local_kinds) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const auto subject = gen_temp_name("match_subject", match_stmt.location);
    const auto rewrite = [&](const ExprNode& expr) {
        return rewrite_expr(expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, &local_kinds);
    };
    std::ostringstream out;
    out << ind << "{\n";
    out << ind << "    [[maybe_unused]] const auto " << subject << " = " << rewrite(*match_stmt.subject) << ";\n";
    for (std::size_t index = 0; index < match_stmt.arms.size(); ++index) {
        const auto& arm = match_stmt.arms[index];
        const bool last = index + 1 == match_stmt.arms.size() || arm.is_wildcard();
        out << ind << "    ";
        if (!last) {
            out << (index == 0 ? "if (" : "else if (") << subject << " == " << rewrite(*arm.pattern) << ") ";
        } else if (index > 0) {
            out << "else ";
        }
        out << "{\n";
        auto arm_locals = lexical_locals;
        auto arm_kinds  = local_kinds;
        out << rewrite_stmt_block(arm.body,
                                  indent + 2,
                                  trait_names,
                                  program,
                                  pointer_aliases,
                                  dispatcher_available,
                                  cpp_overrides,
                                  pair_scope,
                                  arm_locals,
                                  arm_kinds);
        out << ind << "    }\n";
        if (last) {
            break;
        }
    }
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
        if (const auto* node = find_template_node(program, tmpl_id)) {
            return ind +
                   emit_hierarchical_spawn_expansion(tmpl_id,
                                                     s.overrides,
                                                     s.child_overrides,
                                                     *node,
                                                     s.arguments,
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
    const auto arguments =
        emit_template_bindings(result, s.arguments, ind + "    ", trait_names, program, pointer_aliases, pair_scope);
    const bool graph_runtime = !program.execution_graph.phases.empty();
    const auto overrides     = [&](int override_indent) {
        return emit_spawn_overrides(spawned_name,
                                    s.overrides,
                                    override_indent,
                                    trait_names,
                                    program,
                                    pointer_aliases,
                                    pair_scope,
                                    EnttPersistenceEmitter::archetype_origin_index(program, tmpl_id),
                                    graph_runtime);
    };
    const std::string ranges_ok = gen_temp_name("ranges_ok", s.location);
    const auto precheck         = [&](const std::string& check_indent) {
        return emit_spawn_range_precheck(tmpl_id,
                                         s.overrides,
                                         {},
                                         ranges_ok,
                                         check_indent,
                                         spawn_override_value(graph_runtime, trait_names, program, pointer_aliases, pair_scope),
                                         program);
    };
    if (graph_runtime) {
        emit_deferred_override_values(
            result, ind + "    ", s.overrides, {}, trait_names, program, pointer_aliases, pair_scope);
        result << ind << "    auto " << spawned_name
               << " = cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        result << ind << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Spawn,\n";
        result << ind << "        [=](entt::registry& registry) mutable {\n";
        result << emit_spawn_range_guard(precheck(ind + "            "), ranges_ok, ind + "            ", "return;");
        result << ind << "            " << template_create_at_ref(tmpl_id, program) << "(registry, " << spawned_name
               << arguments << ");\n";
        result << overrides(indent + 3);
        result << ind << "        });\n";
    } else {
        const auto check = precheck(ind + "    ");
        result << check;
        if (!check.empty()) {
            result << ind << "    if (" << ranges_ok << ") {\n";
        }
        result << ind << "    auto " << spawned_name << " = " << template_create_ref(tmpl_id, program) << "(registry"
               << arguments << ");\n";
        result << overrides(indent + 1);
        if (!check.empty()) {
            result << ind << "    }\n";
        }
    }
    result << ind << "}\n";
    return result.str();
}

// Attaches or replaces a trait; a state variant goes through enter_state, which swaps out the
// entity's current variant. An empty `value` writes the trait's defaults.
static std::string emit_trait_write(const std::string& cpp,
                                    const std::optional<EnttCodegenUtils::StateVariant>& variant,
                                    const std::string& entity_expr,
                                    const std::string& value) {
    if (variant.has_value()) {
        return EnttCodegenUtils::enter_state_call(*variant, entity_expr, value.empty() ? cpp + "{}" : value);
    }
    std::string write = "registry.emplace_or_replace<" + cpp + ">(" + entity_expr;
    if (!value.empty()) {
        write += ", " + value;
    }
    return write + ");\n";
}

// The `parent` value of an `add Parent` the commit must check for a cycle.
static const FieldAssignment* reparent_value(const AddTraitStmt& add, const DecoratedProgram& program) {
    if (!add.resolved_trait_id.has_value() || !is_hierarchy_trait(*add.resolved_trait_id) ||
        !EnttCodegenUtils::checks_reparent(program)) {
        return nullptr;
    }
    const auto parent = std::ranges::find(add.args, "parent", &FieldAssignment::name);
    return parent == add.args.end() ? nullptr : &*parent;
}

// A deferred `add` at commit: the value from the hoisted args, its range check, then the write.
static std::string emit_deferred_add_write(const AddTraitStmt& s,
                                           const std::string& cpp,
                                           const std::optional<EnttCodegenUtils::StateVariant>& variant,
                                           const ResolvedTrait* ranged,
                                           const std::string& target_name,
                                           const std::string& ind) {
    const std::string existing_name = gen_temp_name("existing", s.location);
    const std::string value_name    = gen_temp_name("value", s.location);
    std::ostringstream result;
    if (!s.args.empty()) {
        result << ind << "auto* " << existing_name << " = registry.try_get<" << cpp << ">(" << target_name << ");\n";
        result << ind << "auto " << value_name << " = " << existing_name << " ? *" << existing_name << " : " << cpp
               << "{};\n";
        for (const auto& arg : s.args) {
            result << ind << value_name << "." << cpp_escaped(arg.name) << " = " << deferred_field_value_name(arg)
                   << ";\n";
        }
    }
    const auto value = s.args.empty() ? std::string{} : value_name;
    if (ranged != nullptr) {
        result << emit_range_check_statement(*ranged, value.empty() ? cpp + "{}" : value, "return;", ind);
    }
    result << ind << "::cactus_gen::detail::cancel_projected<" << cpp << ">(" << target_name << ");\n";
    result << ind << emit_trait_write(cpp, variant, target_name, value);
    return result.str();
}

// `<value>`: the target's current `cpp` value, or a default one, with `args` written over it.
static void emit_candidate_value(std::ostringstream& out,
                                 const std::string& cpp,
                                 const std::string& target,
                                 const std::string& existing_name,
                                 const std::string& value_name,
                                 const std::vector<FieldAssignment>& args,
                                 const OverrideValue& value_of,
                                 const std::string& ind) {
    out << ind << "const auto* " << existing_name << " = registry.try_get<" << cpp << ">(" << target << ");\n";
    out << ind << "auto " << value_name << " = " << existing_name << " != nullptr ? *" << existing_name << " : "
        << cpp << "{};\n";
    for (const auto& arg : args) {
        out << ind << value_name << "." << cpp_escaped(arg.name) << " = " << value_of(arg) << ";\n";
    }
}

// An immediate `add` of a trait with ranged fields: the value is built first and written only when it passes.
static std::string emit_immediate_ranged_add(const AddTraitStmt& s,
                                             const ResolvedTrait& ranged,
                                             const std::string& target,
                                             const std::string& cpp,
                                             const std::string& retain,
                                             const std::string& ind,
                                             const OverrideValue& value_of) {
    const std::string existing_name = gen_temp_name("existing", s.location);
    const std::string value_name    = gen_temp_name("value", s.location);
    std::ostringstream result;
    result << ind << "if (registry.valid(" << target << ")) {\n";
    if (s.args.empty()) {
        result << ind << "    const auto " << value_name << " = " << cpp << "{};\n";
    } else {
        emit_candidate_value(result, cpp, target, existing_name, value_name, s.args, value_of, ind + "    ");
    }
    result << ind << "    if (" << emit_range_check_passes(ranged, value_name) << ") {\n";
    result << ind << "        ::cactus_gen::detail::cancel_projected<" << cpp << ">(" << target << ");\n";
    result << ind << "        registry.emplace_or_replace<" << cpp << ">(" << target << ", " << value_name << ");\n";
    result << retain;
    result << ind << "    }\n";
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
    // An add can land on an entity that is (or becomes) eligible, and the added
    // trait's initialization is the incarnation a save has to rebuild.
    const bool retain =
        EnttPersistenceEmitter::structural_site_tracks_construction(program, s.resolved_trait_id, s.trait_name);
    const auto retain_line = [&](const std::string& entity_expr, const std::string& indentation) {
        return retain ? EnttPersistenceEmitter::emit_retain_construction(cpp, entity_expr, indentation) : std::string{};
    };
    const auto variant     = EnttCodegenUtils::state_variant(program, s.resolved_trait_id);
    const auto write_trait = [&](const std::string& entity_expr, const std::string& value) {
        return emit_trait_write(cpp, variant, entity_expr, value);
    };
    const auto* ranged = find_ranged_trait(program, s.resolved_trait_id, s.trait_name);
    if (!program.execution_graph.phases.empty()) {
        std::ostringstream result;
        result << ind << "{\n";
        result << ind << "    const auto " << target_name << " = " << target << ";\n";
        emit_deferred_field_values(
            result, ind + "    ", cpp, s.args, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
        result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        result << ind << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Add,\n";
        result << ind << "        [=](entt::registry& registry) mutable {\n";
        result << ind << "            if (!registry.valid(" << target_name << ")) { return; }\n";
        if (const auto* parent = reparent_value(s, program); parent != nullptr) {
            result << ind << "            if (cactus::runtime::entt_backend::reparent_would_cycle("
                   << EnttCodegenUtils::hierarchy_index_expr(program) << ", " << target_name << ", "
                   << deferred_field_value_name(*parent) << ")) { return; }\n";
        }
        result << emit_deferred_add_write(s, cpp, variant, ranged, target_name, ind + "            ");
        result << retain_line(target_name, ind + "            ");
        result << ind << "        });\n";
        result << ind << "}\n";
        return result.str();
    }
    if (ranged != nullptr) {
        return emit_immediate_ranged_add(s, *ranged, target, cpp, retain_line(target, ind + "        "), ind, [&](const FieldAssignment& arg) {
            return rewrite_expr(*arg.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
        });
    }
    if (s.args.empty()) {
        if (GUARDED) {
            return ind + "if (registry.valid(" + target + ")) {\n" + ind +
                   "    ::cactus_gen::detail::cancel_projected<" + cpp + ">(" + target + ");\n" + ind + "    " +
                   write_trait(target, "") + retain_line(target, ind + "    ") + ind + "}\n";
        }
        return ind + "::cactus_gen::detail::cancel_projected<" + cpp + ">(" + target + ");\n" + ind +
               write_trait(target, "") + retain_line(target, ind);
    }

    std::ostringstream result;
    if (GUARDED) {
        result << ind << "if (registry.valid(" << target << ")) {\n";
    }
    result << ind << (GUARDED ? "    " : "") << "{\n";
    result << ind << (GUARDED ? "        " : "    ") << "::cactus_gen::detail::cancel_projected<" << cpp << ">("
           << target << ");\n";
    result << ind << (GUARDED ? "        " : "    ") << "auto* " << existing_name << " = registry.try_get<" << cpp
           << ">(" << target << ");\n";
    result << ind << (GUARDED ? "        " : "    ") << "auto " << value_name << " = " << existing_name << " ? *"
           << existing_name << " : " << cpp << "{};\n";
    for (const auto& arg : s.args) {
        result << ind << (GUARDED ? "        " : "    ") << value_name << "." << cpp_escaped(arg.name) << " = "
               << rewrite_expr(*arg.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope) << ";\n";
    }
    result << ind << (GUARDED ? "        " : "    ") << write_trait(target, value_name);
    result << retain_line(target, ind + (GUARDED ? "        " : "    "));
    result << ind << (GUARDED ? "    " : "") << "}\n";
    if (GUARDED) {
        result << ind << "}\n";
    }
    return result.str();
}

// `remove S` leaves the state through leave_state, which also removes the current variant.
static std::string emit_remove_state_stmt(const RemoveTraitStmt& s,
                                          const std::string& slot_cpp,
                                          const std::string& target,
                                          const std::string& ind,
                                          bool deferred) {
    const auto leave = [&](const std::string& entity_expr) {
        return "cactus::runtime::entt_backend::leave_state<" + slot_cpp + ">(registry, " + entity_expr + ");\n";
    };
    if (!deferred) {
        return ind + "if (registry.valid(" + target + ")) {\n" + ind + "    " + leave(target) + ind + "}\n";
    }
    const std::string target_name = gen_temp_name("target", s.location);
    return ind + "{\n" + ind + "    const auto " + target_name + " = " + target + ";\n" + ind +
           "    cactus::runtime::entt_backend::generated_queue_structural_command(\n" + ind +
           "        cactus::runtime::entt_backend::StructuralCommand::Kind::Remove,\n" + ind +
           "        [=](entt::registry& registry) {\n" + ind + "            if (!registry.valid(" + target_name +
           ")) { return; }\n" + ind + "            " + leave(target_name) + ind + "        });\n" + ind + "}\n";
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
    // A removed trait's construction copy would otherwise be mistaken for the
    // live one if the trait is added back.
    const bool discard =
        EnttPersistenceEmitter::structural_site_tracks_construction(program, s.resolved_trait_id, s.trait_name);
    const auto discard_line = [&](const std::string& entity_expr, const std::string& indentation) {
        return discard ? EnttPersistenceEmitter::emit_discard_construction(cpp, entity_expr, indentation)
                       : std::string{};
    };
    if (const auto* slot = EnttCodegenUtils::find_trait(
            program, s.resolved_trait_id.has_value() ? make_canonical_id(*s.resolved_trait_id) : s.trait_name);
        slot != nullptr && slot->is_state_slot()) {
        return emit_remove_state_stmt(s, cpp, target, ind, !program.execution_graph.phases.empty());
    }
    if (!program.execution_graph.phases.empty()) {
        const std::string target_name = gen_temp_name("target", s.location);
        return ind + "{\n" + ind + "    const auto " + target_name + " = " + target + ";\n" + ind +
               "    cactus::runtime::entt_backend::generated_queue_structural_command(\n" + ind +
               "        cactus::runtime::entt_backend::StructuralCommand::Kind::Remove,\n" + ind +
               "        [=](entt::registry& registry) {\n" + ind + "            if (!registry.valid(" + target_name +
               ")) { return; }\n" + ind + "            ::cactus_gen::detail::cancel_projected<" + cpp + ">(" +
               target_name + ");\n" + ind + "            if (registry.all_of<" + cpp + ">(" + target_name + ")) {\n" +
               ind + "                registry.remove<" + cpp + ">(" + target_name + ");\n" +
               discard_line(target_name, ind + "                ") + ind + "            }\n" + ind + "        });\n" +
               ind + "}\n";
    }
    if (s.target_expr.has_value()) {
        return ind + "if (registry.valid(" + target + ")) {\n" + ind + "    ::cactus_gen::detail::cancel_projected<" +
               cpp + ">(" + target + ");\n" + ind + "    if (registry.all_of<" + cpp + ">(" + target + ")) {\n" + ind +
               "        registry.remove<" + cpp + ">(" + target + ");\n" + discard_line(target, ind + "        ") +
               ind + "    }\n" + ind + "}\n";
    }
    return ind + "::cactus_gen::detail::cancel_projected<" + cpp + ">(" + target + ");\n" + ind +
           "if (registry.all_of<" + cpp + ">(" + target + ")) {\n" + ind + "    registry.remove<" + cpp + ">(" +
           target + ");\n" + discard_line(target, ind + "    ") + ind + "}\n";
}

// `add R(x, y)[: ...]`: ends and field values are taken in the handler; the
// row is created or replaced at the commit, if both ends still carry their traits.
static std::string emit_add_relation_stmt(const AddRelationStmt& s,
                                          int indent,
                                          const std::function<std::string(const ExprNode&)>& rewrite) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const auto row        = EnttCodegenUtils::relation_cpp_name(*s.resolved_relation_id);
    const auto from_name  = gen_temp_name("row_from", s.location);
    const auto to_name    = gen_temp_name("row_to", s.location);
    const auto value_name = gen_temp_name("row_value", s.location);
    std::ostringstream result;
    result << ind << "{\n";
    result << ind << "    const auto " << from_name << " = " << rewrite(*s.from) << ";\n";
    result << ind << "    const auto " << to_name << " = " << rewrite(*s.to) << ";\n";
    result << ind << "    " << row << " " << value_name << "{};\n";
    for (const auto& field : s.args) {
        result << ind << "    " << value_name << "." << cpp_escaped(field.name) << " = " << rewrite(*field.value)
               << ";\n";
    }
    result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    result << ind << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Add,\n";
    result << ind << "        [=](entt::registry& registry) {\n";
    result << ind << "            cactus::runtime::entt_backend::add_relation_row<" << row << ">(registry, " << from_name
           << ", " << to_name << ", " << value_name << ");\n";
    result << ind << "        });\n";
    result << ind << "}\n";
    return result.str();
}

// `remove R(x, y)`: a `_` end matches every row through the other end's index.
static std::string emit_remove_relation_stmt(const RemoveRelationStmt& s,
                                             int indent,
                                             const std::function<std::string(const ExprNode&)>& rewrite) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const bool any_from  = is_wildcard_pattern(*s.from);
    const bool any_to    = is_wildcard_pattern(*s.to);
    const auto from_name = gen_temp_name("row_from", s.location);
    const auto to_name   = gen_temp_name("row_to", s.location);
    std::string call;
    std::string captures;
    if (any_from && any_to) {
        call = "clear()";
    } else if (any_to) {
        call     = "erase_from(" + from_name + ")";
        captures = from_name;
    } else if (any_from) {
        call     = "erase_to(" + to_name + ")";
        captures = to_name;
    } else {
        call     = "erase(" + from_name + ", " + to_name + ")";
        captures = from_name + ", " + to_name;
    }
    std::ostringstream result;
    result << ind << "{\n";
    if (!any_from) {
        result << ind << "    const auto " << from_name << " = " << rewrite(*s.from) << ";\n";
    }
    if (!any_to) {
        result << ind << "    const auto " << to_name << " = " << rewrite(*s.to) << ";\n";
    }
    result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    result << ind << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Remove,\n";
    result << ind << "        [" << captures << "](entt::registry& registry) {\n";
    result << ind << "            " << EnttCodegenUtils::uncompacted_relation_store_cpp(*s.resolved_relation_id) << "." << call
           << ";\n";
    result << ind << "        });\n";
    result << ind << "}\n";
    return result.str();
}

// A pure patch: no attach, no projection cancel, no construction baseline.
static std::string emit_set_trait_stmt(const SetTraitStmt& s,
                                       int indent,
                                       const std::vector<std::string>& trait_names,
                                       const DecoratedProgram& program,
                                       const std::unordered_set<std::string>& pointer_aliases,
                                       const std::unordered_map<std::string, std::string>& cpp_overrides,
                                       const PairCodegenScope* pair_scope) {
    if (program.execution_graph.phases.empty()) {
        throw std::runtime_error("cpp-entt backend: `set` requires the graph-driven phase scheduler");
    }
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    const std::string cpp         = EnttCodegenUtils::trait_cpp_name(s.resolved_trait_id, s.trait_name, program);
    const std::string target_name = gen_temp_name("target", s.location);
    const std::string value_name  = gen_temp_name("value", s.location);
    std::ostringstream result;
    result << ind << "{\n";
    result << ind << "    const auto " << target_name << " = "
           << rewrite_expr(*s.target_expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope) << ";\n";
    emit_deferred_field_values(
        result, ind + "    ", cpp, s.args, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
    result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    result << ind << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Set,\n";
    result << ind << "        [=](entt::registry& registry) {\n";
    result << ind << "            auto* " << value_name << " = ::cactus_gen::detail::durable<" << cpp << ">(registry, "
           << target_name << ");\n";
    result << ind << "            if (" << value_name << " == nullptr) { return; }\n";
    for (const auto& arg : s.args) {
        result << ind << "            " << value_name << "->" << arg.name << " = " << deferred_field_value_name(arg)
               << ";\n";
    }
    result << ind << "        });\n";
    result << ind << "}\n";
    return result.str();
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
    const auto* ranged = find_ranged_trait(program, s.resolved_trait_id, s.trait_name);
    if (ranged != nullptr) {
        const std::string existing_name  = gen_temp_name("existing", s.location);
        const std::string candidate_name = gen_temp_name("candidate", s.location);
        emit_candidate_value(result,
                             cpp,
                             target,
                             existing_name,
                             candidate_name,
                             s.args,
                             [&](const FieldAssignment& arg) {
                                 return rewrite_expr(
                                     *arg.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
                             },
                             ind + "    ");
        result << ind << "    if (" << emit_range_check_passes(*ranged, candidate_name) << ") {\n";
        result << ind << "        ::cactus_gen::detail::project<" << cpp << ">(registry, " << target
               << ") = " << candidate_name << ";\n";
        result << ind << "    }\n";
    } else if (is_marker) {
        result << ind << "    ::cactus_gen::detail::project<" << cpp << ">(registry, " << target << ");\n";
    } else {
        const std::string projected_name = gen_temp_name("projected", s.location);
        result << ind << "    [[maybe_unused]] auto& " << projected_name << " = ::cactus_gen::detail::project<" << cpp
               << ">(registry, " << target << ");\n";
        for (const auto& arg : s.args) {
            result << ind << "    " << projected_name << "." << cpp_escaped(arg.name) << " = "
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
// zero-step check. The index counts in int64, so the step past the last value cannot overflow `int`.
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

    const auto index_name = gen_temp_name("range_index", s.location);
    const std::string ind3(static_cast<size_t>(indent + 3) * 4, ' ');
    const auto emit_direction = [&](const std::string& compare_op) {
        auto range_locals = clone_or_empty(lexical_locals);
        auto range_kinds  = clone_or_empty(local_kinds);
        range_locals.insert(s.var_name);
        range_kinds[s.var_name] = NumericKind::Int;
        std::string branch = ind2 + "for (std::int64_t " + index_name + " = " + begin_name + "; " + index_name + " " +
                             compare_op + " " + end_name + "; " + index_name + " += " + step_name + ") {\n";
        branch += ind3 + "[[maybe_unused]] const int " + s.var_name + " = static_cast<int>(" + index_name + ");\n";
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

// The loop header for a std.query hierarchy iterable that needs no fresh
// snapshot: a child walk over the sibling links, or a traversal into storage
// the call site keeps. Nullopt for any other iterable.
static std::optional<std::string> hierarchy_loop_header(const ForeachStmt& s,
                                                        const std::string& ind,
                                                        const DecoratedProgram& program,
                                                        const std::function<std::string(const ExprNode&)>& rewrite) {
    const auto* qcall = std::get_if<QueryCallExpr>(&s.iterable->expr);
    const auto function = qcall == nullptr ? std::nullopt : std_query_function(*qcall, program);
    if (!function.has_value()) {
        return std::nullopt;
    }
    const auto& func_name = *function;
    if (func_name == "children" && walks_child_links(*qcall)) {
        return ind + "for ([[maybe_unused]] const auto " + s.var_name +
               " : cactus::runtime::entt_backend::HierarchyChildren(registry, " +
               EnttCodegenUtils::hierarchy_index_expr(program) + ", " +
               find_named_arg_value(qcall->named_args, "of", rewrite) + ", " + hierarchy_child_view(*qcall, program) +
               ")) {\n";
    }
    if ((func_name != "hierarchy_preorder" && func_name != "hierarchy_postorder") ||
        active_scope<HandlerBodyCodegen>() == nullptr) {
        return std::nullopt;
    }
    const auto buffer    = foreach_temp_name(s, "buffer");
    const auto traversal = foreach_temp_name(s);
    return ind + "static thread_local cactus::runtime::entt_backend::HierarchyTraversalBuffer " + buffer + ";\n" + ind +
           "const auto " + traversal + " = cactus::runtime::entt_backend::hierarchy_order(" +
           hierarchy_order_args(*qcall, func_name, program) + ", " + buffer + ");\n" + ind +
           "for ([[maybe_unused]] const auto& " + s.var_name + " : " + traversal + ") {\n";
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
        // DestroyStmt/ReturnStmt/ExprStmt/IfStmt/MatchStmt/ForeachStmt) are the
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
                if (s.target != nullptr) {
                    lhs = rewrite_expr(
                        *s.target, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                } else if (!s.path.empty()) {
                    // Dotted assignment target (`alias.field...`): reconstruct the
                    // equivalent member-access chain and lower it through the same
                    // path ordinary reads use, so `hp.health = x` resolves `hp` as
                    // the already-in-scope filter-alias/field reference.
                    ExprNode chain(ExprNode::Variant{IdentExpr{s.name, s.location}}, s.location);
                    for (const auto& segment : s.path) {
                        chain = ExprNode(
                            ExprNode::Variant{MemberExpr{
                                std::make_unique<ExprNode>(std::move(chain)), segment, std::nullopt, s.location}},
                            s.location);
                    }
                    lhs = rewrite_expr(
                        chain, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                } else {
                    const bool is_field = known_fields.contains(s.name) &&
                                          (lexical_locals == nullptr || !lexical_locals->contains(s.name));
                    const auto comp     = is_field ? find_comp_for_field(s.name, trait_names, program) : std::string{};
                    if (comp.empty()) {
                        lhs = s.name;
                    } else {
                        const auto ovr = cpp_overrides.find(comp);
                        const auto& cpp =
                            ovr != cpp_overrides.end() ? ovr->second : EnttCodegenUtils::trait_cpp_name(comp, program);
                        lhs = component_local(cpp) + "." + cpp_escaped(s.name);
                    }
                }
                if (s.accumulation.has_value() && s.accumulation->buffered) {
                    return ind + AccumulationPlan::table_name(s.accumulation->trait, s.accumulation->field, program) +
                           "()." + (s.op == "-=" ? "subtract(" : "add(") +
                           accumulation_entity(s, trait_names, program, pair_scope) + ", " + lhs + ", " +
                           rewrite_expr(*s.value,
                                        trait_names,
                                        program,
                                        pointer_aliases,
                                        cpp_overrides,
                                        pair_scope,
                                        local_kinds) +
                           accumulation_component(s, *s.accumulation) + ");\n";
                }
                lhs = settled_access(s.field_write, std::move(lhs), program);
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
                return ind +
                       EnttCodegenUtils::emit_compound_assignment(
                           s,
                           lhs,
                           rewrite_expr(
                               *s.value, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds)) +
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
            } else if constexpr (std::is_same_v<S, SetTraitStmt>) {
                return emit_set_trait_stmt(s, indent, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
            } else if constexpr (std::is_same_v<S, AddRelationStmt> || std::is_same_v<S, RemoveRelationStmt>) {
                const auto rewrite = [&](const ExprNode& expr) {
                    return rewrite_expr(
                        expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                };
                if constexpr (std::is_same_v<S, AddRelationStmt>) {
                    return emit_add_relation_stmt(s, indent, rewrite);
                } else {
                    return emit_remove_relation_stmt(s, indent, rewrite);
                }
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
                           target_name + ")) {\n" + ind +
                           "                ::cactus_gen::detail::cactus_destroy_entity_recursive(registry, " +
                           target_name + ");\n" + ind + "            }\n" + ind + "        });\n" + ind + "}\n";
                }
                if (s.target_expr.has_value()) {
                    std::string target = rewrite_expr(
                        **s.target_expr, trait_names, program, pointer_aliases, cpp_overrides, pair_scope, local_kinds);
                    return ind + "if (registry.valid(" + target + ")) {\n" + ind +
                           "    ::cactus_gen::detail::cactus_destroy_entity_recursive(registry, " + target + ");\n" +
                           ind + "}\n";
                }
                return ind + "::cactus_gen::detail::cactus_destroy_entity_recursive(registry, entity);\n";
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
            } else if constexpr (std::is_same_v<S, MatchStmt>) {
                const auto emit_match = [&] {
                    switch (s.kind) {
                        case MatchKind::Value:
                            return emit_value_match_stmt;
                        case MatchKind::State:
                            return emit_state_match_stmt;
                        case MatchKind::Trait:
                            return emit_trait_match_stmt;
                    }
                    std::unreachable();
                }();
                return emit_match(s,
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
                const auto rewrite_arg = [&](const ExprNode& arg) {
                    return rewrite_expr(arg, trait_names, program, pointer_aliases, cpp_overrides, pair_scope);
                };
                auto result = hierarchy_loop_header(s, ind, program, rewrite_arg).value_or("");
                if (result.empty()) {
                    const auto temp = foreach_temp_name(s);
                    const bool constant_iterable =
                        std::visit([](const auto& node) {
                            if constexpr (requires { node.resolved_const_id; }) {
                                return node.resolved_const_id.has_value();
                            } else {
                                return false;
                            }
                        }, s.iterable->expr);
                    result = ind + (constant_iterable ? "const auto& " : "auto ") + temp + " = " +
                             rewrite_expr(*s.iterable,
                                          trait_names,
                                          program,
                                          pointer_aliases,
                                          cpp_overrides,
                                          pair_scope,
                                          local_kinds) +
                             ";\n";
                    result += ind + "for ([[maybe_unused]] const auto& " + s.var_name + " : " + temp + ") {\n";
                }
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

static std::string func_signature(const FuncNode& func, const ResolvedFunc& resolved) {
    std::string signature =
        (resolved.return_type.has_value() ? EnttCodegenUtils::value_type_to_cpp(*resolved.return_type) : "void") + " " +
        cpp_decl_name(*func.resolved_func_id) + "(";
    for (std::size_t index = 0; index < resolved.params.size(); ++index) {
        signature += index != 0U ? ", " : "";
        signature += EnttCodegenUtils::value_type_to_cpp(resolved.params[index].type) + " " + resolved.params[index].name;
    }
    return signature + ")";
}

// Lets constants and other code before the definitions call authored funcs.
std::string EnttSystemEmitter::emit_func_declaration(const FuncNode& func, const DecoratedProgram& program) {
    if (func.is_extern || !func.resolved_func_id.has_value()) {
        return {};
    }
    const auto* resolved = EnttCodegenUtils::find_func(program, *func.resolved_func_id);
    return resolved == nullptr ? std::string{}
                               : namespace_block(cpp_namespace_path(func.resolved_func_id->module),
                                                 func_signature(func, *resolved) + ";\n");
}

std::string EnttSystemEmitter::emit_func(const FuncNode& func, const DecoratedProgram& program) {
    if (func.is_extern || !func.resolved_func_id.has_value()) {
        return {};
    }
    const auto* resolved = EnttCodegenUtils::find_func(program, *func.resolved_func_id);
    if (resolved == nullptr) {
        throw std::runtime_error("cpp-entt backend: missing resolved authored func '" +
                                 make_canonical_id(*func.resolved_func_id) + "'");
    }

    std::ostringstream out;
    out << func_signature(func, *resolved) << " {\n";
    LexicalLocalBindings locals;
    LocalNumericKinds param_kinds;
    for (const auto& param : resolved->params) {
        locals.insert(param.name);
        if (param.type.kind == TypeKind::Int) {
            param_kinds[param.name] = NumericKind::Int;
        } else if (param.type.kind == TypeKind::Float) {
            param_kinds[param.name] = NumericKind::Float;
        } else if (param.type.kind == TypeKind::Color) {
            param_kinds[param.name] = NumericKind::Color;
        }
    }
    out << rewrite_stmt_block(func.body, 1, {}, program, {}, false, {}, nullptr, locals, param_kinds);
    out << "}\n\n";
    return namespace_block(cpp_namespace_path(func.resolved_func_id->module), out.str()) + "\n";
}

// One `[&](entt::entity <left>, entt::entity <right>) { ... }` extractor per
// sort key, with both pair bindings as parameters so a cross-binding key
// expression lowers through the ordinary pair-scope expression emitter with
// exactly the names it was authored against.
static std::vector<std::string> emit_pair_sort_key_extractors(
    std::ostringstream& out,
    const RuleNode& sys,
    const std::vector<PairBindingCodegen>& pair_binding_codegens,
    const PairCodegenScope& pair_codegen_scope,
    const DecoratedProgram& program) {
    std::vector<std::string> names;
    names.reserve(sys.order_by.size());
    for (const auto& key : sys.order_by) {
        auto name = gen_temp_name("pair_sort_key", key.location);
        out << "    auto " << name << " = [&]([[maybe_unused]] entt::entity " << pair_binding_codegens[0].scope.binding_name
            << ", [[maybe_unused]] entt::entity " << pair_binding_codegens[1].scope.binding_name << ") {\n";
        out << "        return " << rewrite_expr(*key.expression, {}, program, {}, {}, &pair_codegen_scope) << ";\n";
        out << "    };\n";
        names.push_back(std::move(name));
    }
    return names;
}

// Without a contract every clause expression is row stage.
static const RuleStaging& staging_of(const HandlerContract* contract) {
    static const RuleStaging unstaged;
    return contract == nullptr ? unstaged : contract->staging;
}

// The `where:` predicates recorded at `stage`, in source order.
static std::vector<const ExprNode*> where_at(const RuleNode& sys, const HandlerContract* contract, ClauseStage stage) {
    std::vector<const ExprNode*> predicates;
    if (!sys.where_clause.has_value()) {
        return predicates;
    }
    const auto& stages = staging_of(contract).where;
    for (std::size_t i = 0; i < sys.where_clause->predicates.size(); ++i) {
        if (stage_at(stages, i) == stage) {
            predicates.push_back(sys.where_clause->predicates[i].get());
        }
    }
    return predicates;
}

static ClauseStage binding_stage(std::size_t binding_index) {
    return binding_index == 0 ? ClauseStage::BindingLeft : ClauseStage::BindingRight;
}

static bool is_binding_stage(ClauseStage stage) {
    return stage == ClauseStage::BindingLeft || stage == ClauseStage::BindingRight;
}

// `values[i]` = `call` evaluated for member i of one binding's snapshot, where
// `call` reads the member as `cactus_member` and `prototype` names its type.
static void emit_member_table(std::ostringstream& out,
                              const std::string& values,
                              const std::string& prototype,
                              const std::string& call,
                              const std::string& binding_name) {
    out << "    std::vector<std::decay_t<decltype(" << prototype << ")>> " << values << ";\n";
    out << "    " << values << ".reserve(" << binding_name << "_snapshot.size());\n";
    out << "    for (const auto cactus_member : " << binding_name << "_snapshot) {\n";
    out << "        " << values << ".push_back(" << call << ");\n";
    out << "    }\n";
}

static std::size_t matching_paren(std::string_view cpp, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < cpp.size(); ++i) {
        const char c = cpp[i];
        if (c == '"' || c == '\'') {
            for (++i; i < cpp.size() && cpp[i] != c; ++i) {
                i += cpp[i] == '\\' ? 1 : 0;
            }
        } else if (c == '(') {
            ++depth;
        } else if (c == ')' && --depth == 0) {
            return i;
        }
    }
    return std::string_view::npos;
}

static bool is_name(std::string_view cpp) {
    return !cpp.empty() && std::ranges::all_of(cpp, [](const char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == ':';
    });
}

// Parenthesizes `cpp` only when it is not already one primary expression, so an operand of
// `&&` never gets a redundant pair.
static std::string as_operand(const std::string& cpp) {
    const bool parenthesized = cpp.starts_with('(') && matching_paren(cpp, 0) == cpp.size() - 1;
    return parenthesized || is_name(cpp) ? cpp : "(" + cpp + ")";
}

// An `if` condition that holds when `condition` does not. A compound condition is named first:
// negating it in place reads as a De Morgan candidate to clang-tidy.
static std::string rejects(const std::string& condition) {
    if (is_name(condition)) {
        return "!" + condition;
    }
    return "const bool cactus_admitted = " + condition + "; !cactus_admitted";
}

// `pred0 && pred1 && ...`, each predicate lowered through `rewrite`.
static std::string where_conjunction(const std::vector<const ExprNode*>& predicates,
                                     const std::function<std::string(const ExprNode&)>& rewrite) {
    if (predicates.size() == 1) {
        return rewrite(*predicates.front());
    }
    std::string conjunction;
    for (std::size_t i = 0; i < predicates.size(); ++i) {
        conjunction += (i == 0 ? "" : " && ") + as_operand(rewrite(*predicates[i]));
    }
    return conjunction;
}

static void emit_where_predicate_conjunction(std::ostringstream& out,
                                             const std::vector<const ExprNode*>& predicates,
                                             const std::function<std::string(const ExprNode&)>& rewrite) {
    out << "        return " << where_conjunction(predicates, rewrite) << ";\n";
}

// Checks `predicates` at the start of an invocation, against live values.
static void emit_where_guard(std::ostringstream& out,
                             const std::vector<const ExprNode*>& predicates,
                             const std::function<std::string(const ExprNode&)>& rewrite) {
    if (predicates.empty()) {
        return;
    }
    out << "        if (" << rejects(where_conjunction(predicates, rewrite)) << ") { return; }\n";
}

// The conjunction of `predicates`, lowered outside any entity binding.
static std::string unbound_conjunction(const std::vector<const ExprNode*>& predicates, const DecoratedProgram& program) {
    return where_conjunction(predicates, [&](const ExprNode& predicate) {
        return rewrite_expr(predicate, {}, program, {}, {}, nullptr, nullptr);
    });
}

// The conjunction of the activation-stage predicates, or empty when there are none.
static std::string activation_condition(const RuleNode& sys,
                                        const HandlerContract* contract,
                                        const DecoratedProgram& program) {
    return unbound_conjunction(where_at(sys, contract, ClauseStage::Activation), program);
}

// An unreduced pass whose activation-stage predicates fail runs no invocation.
static void emit_activation_gate(std::ostringstream& out,
                                 const RuleNode& sys,
                                 const HandlerContract* contract,
                                 const DecoratedProgram& program) {
    const auto condition = activation_condition(sys, contract, program);
    if (condition.empty()) {
        return;
    }
    out << "    if (" << rejects(condition) << ") {\n";
    out << "        return;\n";
    out << "    }\n";
}

// The snapshot position of each entity of one binding, for per-pass tables.
static void emit_snapshot_positions(std::ostringstream& out, const std::string& name) {
    out << "    const cactus::runtime::entt_backend::SnapshotPositions cactus_positions_" << name << "(" << name
        << "_snapshot);\n";
}

// Drops the members of one binding's snapshot that fail its binding-stage
// predicates, keeping snapshot order. Returns the member test's name, or empty.
static std::string emit_binding_filter(std::ostringstream& out,
                                       const RuleNode& sys,
                                       const HandlerContract* contract,
                                       std::size_t binding_index,
                                       const PairCodegenScope& pair_codegen_scope,
                                       const DecoratedProgram& program) {
    const auto predicates = where_at(sys, contract, binding_stage(binding_index));
    if (predicates.empty()) {
        return {};
    }
    const auto& name = pair_codegen_scope.bindings[binding_index].binding_name;
    auto keep        = "cactus_keep_" + name;
    out << "    auto " << keep << " = [&](entt::entity " << name << ") {\n";
    emit_where_predicate_conjunction(out, predicates, [&](const ExprNode& predicate) {
        return rewrite_expr(predicate, {}, program, {}, {}, &pair_codegen_scope);
    });
    out << "    };\n";
    out << "    std::erase_if(" << name << "_snapshot, [&](entt::entity cactus_member) { return !" << keep
        << "(cactus_member); });\n";
    return keep;
}

// One `[&](entt::entity <left>, entt::entity <right>) { return ...; }` filter
// for a limited rule's row predicates, checked at collection (dsl-rule-limit).
// Returns the lambda's name, or empty when there are none.
static std::string emit_pair_where_filter(std::ostringstream& out,
                                          const std::vector<const ExprNode*>& predicates,
                                          const PairCodegenScope& pair_codegen_scope,
                                          const DecoratedProgram& program) {
    if (predicates.empty()) {
        return {};
    }
    auto name = gen_temp_name("pair_where", predicates.front()->location);
    out << "    auto " << name << " = [&]([[maybe_unused]] entt::entity "
        << pair_codegen_scope.bindings[0].binding_name << ", [[maybe_unused]] entt::entity "
        << pair_codegen_scope.bindings[1].binding_name << ") {\n";
    emit_where_predicate_conjunction(out, predicates, [&](const ExprNode& predicate) {
        return rewrite_expr(predicate, {}, program, {}, {}, &pair_codegen_scope);
    });
    out << "    };\n";
    return name;
}

// Sorts a pair pass's tuple buffer. A binding-stage key is computed once per
// member of its binding, every other key once per buffered tuple.
static void emit_pair_sort(std::ostringstream& out,
                           const RuleNode& sys,
                           const HandlerContract* contract,
                           const std::string& tuple_buffer_name,
                           const std::vector<PairBindingCodegen>& pair_binding_codegens,
                           const PairCodegenScope& pair_codegen_scope,
                           const DecoratedProgram& program) {
    const auto key_names =
        emit_pair_sort_key_extractors(out, sys, pair_binding_codegens, pair_codegen_scope, program);
    std::vector<std::string> row_keys;
    std::array<bool, 2> positioned{};
    for (std::size_t i = 0; i < key_names.size(); ++i) {
        const auto stage = stage_at(staging_of(contract).order_by, i);
        if (!is_binding_stage(stage)) {
            row_keys.push_back(key_names[i] + "(cactus_row.first, cactus_row.second)");
            continue;
        }
        const bool left        = stage == ClauseStage::BindingLeft;
        const auto& name       = pair_codegen_scope.bindings[left ? 0 : 1].binding_name;
        const auto member_keys = "cactus_member_keys_" + std::to_string(i);
        const auto call        = key_names[i] + (left ? "(cactus_member, entt::null)" : "(entt::null, cactus_member)");
        emit_member_table(out, member_keys, key_names[i] + "(entt::null, entt::null)", call, name);
        if (!std::exchange(positioned[left ? 0 : 1], true)) {
            emit_snapshot_positions(out, name);
        }
        auto row_key = member_keys;
        row_key += "[cactus_positions_" + name;
        row_key += left ? ".find(cactus_row.first)]" : ".find(cactus_row.second)]";
        row_keys.push_back(std::move(row_key));
    }
    emit_keyed_sort(out, sys, tuple_buffer_name, row_keys, 1);
}

// The expression a spatial plan's slots are paths into: the matched `where:`
// predicate, or the input of the matched `reduce:` aggregate.
static const ExprNode* spatial_join_root(const RuleNode& sys, const HandlerContract* contract) {
    if (contract == nullptr || !contract->spatial_join.has_value()) {
        return nullptr;
    }
    const auto& plan = *contract->spatial_join;
    if (plan.source == SpatialJoinSource::Reduce) {
        return sys.reduce.has_value() ? sys.reduce->reducers[plan.matched_predicate_index].input.get() : nullptr;
    }
    return sys.where_clause.has_value() ? sys.where_clause->predicates[plan.matched_predicate_index].get() : nullptr;
}

static bool is_collider_shape(SpatialShapeKind kind) {
    return kind == SpatialShapeKind::Collider || kind == SpatialShapeKind::SweptCollider;
}

static const char* sap_proxy_function(SpatialShapeKind kind, SpatialJoinDimension dimension) {
    if (kind == SpatialShapeKind::Box) {
        return "box_proxy";
    }
    if (kind == SpatialShapeKind::AlignedBox) {
        return "aligned_box_proxy";
    }
    if (kind == SpatialShapeKind::RaySegment) {
        return "segment_proxy";
    }
    if (is_collider_shape(kind)) {
        return "collider_proxy";
    }
    return dimension == SpatialJoinDimension::Flat2D ? "circle_proxy" : "sphere_proxy";
}

// C++ for one binding's broad-phase proxy, built from that binding's shape
// slots in `predicate`. A collider proxy is an std::optional (no descriptor, no
// proxy); every other kind is a plain ProxyAabb.
struct ProxyExpression {
    std::string code;
    bool optional = false;
};

static ProxyExpression sap_proxy_expression(const SpatialShape& shape,
                                            SpatialJoinDimension dimension,
                                            const std::string& binding_name,
                                            const std::string& ordinal,
                                            const ExprNode& predicate,
                                            const PairCodegenScope& pair_codegen_scope,
                                            const DecoratedProgram& program) {
    const std::string ns = "cactus::runtime::entt_backend::";
    const bool collider  = is_collider_shape(shape.kind);
    std::ostringstream proxy;
    proxy << ns << sap_proxy_function(shape.kind, dimension) << "(" << binding_name << ", " << ordinal << ", " << ns
          << "SapSide::" << (shape.binding_index == 0 ? "Left" : "Right");
    if (collider) {
        proxy << ", " << collider_shape_cpp(*pair_codegen_scope.find(binding_name), binding_name);
    }
    for (const auto& slot : shape.slots) {
        proxy << ", " << rewrite_expr(*resolve_expr_path(predicate, slot), {}, program, {}, {}, &pair_codegen_scope);
    }
    proxy << ")";
    return ProxyExpression{.code = proxy.str(), .optional = collider};
}

static bool emit_indexed_pair_activation_if_planned(std::ostringstream& out,
                                                    const RuleNode& sys,
                                                    const HandlerContract* contract,
                                                    const ExprNode& predicate,
                                                    const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                                    const PairCodegenScope& pair_codegen_scope,
                                                    const DecoratedProgram& program,
                                                    const std::array<std::string, 2>& keeps,
                                                    const std::string& dispatch_name);

// SAP-eligible pair-rule activation: builds one
// proxy per entity of each binding's snapshot from that binding's shape slots,
// syncs the runtime broad phase, and invokes `pair_body_name` for every
// candidate in (left, right) snapshot order. `pair_body_name` re-checks the
// full `where:`, since the candidates are a conservative superset.
static void emit_sap_pair_activation(std::ostringstream& out,
                                     const SpatialJoinPlan& plan,
                                     const ExprNode& predicate,
                                     const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                     const PairCodegenScope& pair_codegen_scope,
                                     const DecoratedProgram& program,
                                     const std::string& pair_body_name) {
    const std::string ns   = "cactus::runtime::entt_backend::";
    const bool flat        = plan.dimension == SpatialJoinDimension::Flat2D;
    const auto& left_name  = pair_binding_codegens[0].scope.binding_name;
    const auto& right_name = pair_binding_codegens[1].scope.binding_name;

    out << "        {\n";
    out << "            std::vector<" << ns << (flat ? "ProxyAabb2D" : "ProxyAabb3D") << "> cactus_gen_sap_proxies;\n";
    out << "            cactus_gen_sap_proxies.reserve(" << left_name << "_snapshot.size() + " << right_name
        << "_snapshot.size());\n";
    for (const auto* shape : {&plan.left, &plan.right}) {
        const auto& binding_name = pair_binding_codegens[shape->binding_index].scope.binding_name;
        out << "            for (std::size_t cactus_gen_sap_i = 0; cactus_gen_sap_i < " << binding_name
            << "_snapshot.size(); ++cactus_gen_sap_i) {\n";
        out << "                const auto " << binding_name << " = " << binding_name << "_snapshot[cactus_gen_sap_i];\n";
        const auto proxy =
            sap_proxy_expression(*shape, plan.dimension, binding_name, "cactus_gen_sap_i", predicate, pair_codegen_scope, program);
        if (proxy.optional) {
            out << "                if (const auto cactus_gen_sap_proxy = " << proxy.code << ") {\n";
            out << "                    cactus_gen_sap_proxies.push_back(*cactus_gen_sap_proxy);\n";
            out << "                }\n";
        } else {
            out << "                cactus_gen_sap_proxies.push_back(" << proxy.code << ");\n";
        }
        out << "            }\n";
    }
    out << "            " << ns << (flat ? "SapBroadPhase2D" : "SapBroadPhase3D") << " cactus_gen_sap;\n";
    out << "            cactus_gen_sap.sync(cactus_gen_sap_proxies);\n";
    out << "            for (const auto& cactus_gen_sap_pair : cactus_gen_sap.candidate_pairs()) {\n";
    out << "                " << pair_body_name
        << "(cactus_gen_sap_proxies[cactus_gen_sap_pair.left].entity, cactus_gen_sap_proxies[cactus_gen_sap_pair.right].entity);\n";
    out << "            }\n";
    out << "        }\n";
}

// One `per` part's counters: a lambda computing the part's count for a binding
// value, and {taken, cap} per value, cap computed once on the value's first
// tuple: the count expression may read the binding's own trait fields, so
// re-running it every tuple would re-evaluate that read once per tuple sharing
// the value instead of once per distinct binding value.
static std::pair<std::string, std::string> emit_limit_part_counters(std::ostringstream& out,
                                                                    const LimitPart& part,
                                                                    const PairCodegenScope& pair_codegen_scope,
                                                                    const DecoratedProgram& program) {
    const auto count_expr  = rewrite_expr(*part.count, {}, program, {}, {}, &pair_codegen_scope);
    const auto count_name  = gen_temp_name("pair_limit_of", part.location);
    const auto counts_name = gen_temp_name("pair_partition", part.location);
    out << "    auto " << count_name << " = [&]([[maybe_unused]] entt::entity " << part.binding << ") {\n";
    out << "        return " << count_expr << ";\n";
    out << "    };\n";
    out << "    std::unordered_map<std::uint32_t, std::pair<int, int>> " << counts_name << ";\n";
    return {count_name, counts_name};
}

// The `{taken, cap}` slot of `member`'s value in one part's counters.
static void emit_limit_part_slot(std::ostringstream& out,
                                 const std::pair<std::string, std::string>& counters,
                                 const std::string& member,
                                 const std::string& slot) {
    out << "        auto [" << slot << "_it, " << slot << "_new] = " << counters.second
        << ".try_emplace(static_cast<std::uint32_t>(cactus_tuple." << member << "), 0, 0);\n";
    out << "        auto& " << slot << " = " << slot << "_it->second;\n";
    out << "        if (" << slot << "_new) { " << slot << ".second = " << counters.first << "(cactus_tuple." << member
        << "); }\n";
}

// The truncation replay for a limited pair rule (dsl-rule-limit): one linear
// pass over the already-filtered (and, when `order by:` is present, sorted)
// tuple buffer. A global limit counts into a single implicit partition; a
// `per <binding>` part keys a counter on that binding's entity handle and
// evaluates its own count expression once per distinct partition value. With
// one part, partitions resolve independently of each other; with two, a tuple
// is retained only while both of its values are under their caps, which walks
// the ordered buffer as a greedy matching.
static void emit_pair_limit_replay(std::ostringstream& out,
                                   const RuleNode& sys,
                                   const std::string& tuple_buffer_name,
                                   const std::string& pair_body_name,
                                   const PairCodegenScope& pair_codegen_scope,
                                   const DecoratedProgram& program) {
    const auto& limit = *sys.limit;
    if (limit.parts.empty()) {
        const auto count_expr = rewrite_expr(*limit.count, {}, program, {}, {}, &pair_codegen_scope);
        const auto limit_name = gen_temp_name("pair_limit", limit.location);
        const auto taken_name = gen_temp_name("pair_taken", limit.location);
        out << "    const int " << limit_name << " = " << count_expr << ";\n";
        out << "    int " << taken_name << " = 0;\n";
        out << "    for (const auto& cactus_tuple : " << tuple_buffer_name << ") {\n";
        out << "        if (" << taken_name << " >= " << limit_name << ") { break; }\n";
        out << "        ++" << taken_name << ";\n";
        out << "        " << pair_body_name << "(cactus_tuple.first, cactus_tuple.second);\n";
        out << "    }\n";
        return;
    }
    const auto member_of = [&](const LimitPart& part) {
        return std::string(pair_codegen_scope.bindings[0].binding_name == part.binding ? "first" : "second");
    };
    std::vector<std::pair<std::string, std::string>> counters;
    counters.reserve(limit.parts.size());
    for (const auto& part : limit.parts) {
        counters.push_back(emit_limit_part_counters(out, part, pair_codegen_scope, program));
    }
    out << "    for (const auto& cactus_tuple : " << tuple_buffer_name << ") {\n";
    std::string full;
    for (std::size_t index = 0; index < limit.parts.size(); ++index) {
        const auto slot = "cactus_part" + std::to_string(index);
        emit_limit_part_slot(out, counters[index], member_of(limit.parts[index]), slot);
        full += full.empty() ? "" : " || ";
        full += std::format("{0}.first >= {0}.second", slot);
    }
    out << "        if (" << full << ") { continue; }\n";
    for (std::size_t index = 0; index < limit.parts.size(); ++index) {
        out << "        ++cactus_part" << index << ".first;\n";
    }
    out << "        " << pair_body_name << "(cactus_tuple.first, cactus_tuple.second);\n";
    out << "    }\n";
}

// A pair handler snapshots both bindings' live memberships up
// front (deterministic, creation-ordinal order), then iterates
// their directed Cartesian product left-binding-major without
// ever materializing the full tuple list. Component access inside
// the body is read-only (const) unless a provably-one `limit: ... per`
// made one binding writable, matching what semantic analysis enforced.
static void emit_pair_handler_body(std::ostringstream& out,
                                   const RuleNode& sys,
                                   const EventHandlerNode& handler,
                                   const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                   const PairCodegenScope& pair_codegen_scope,
                                   const DecoratedProgram& program,
                                   const HandlerContract* contract,
                                   const std::string* tuple_body = nullptr) {
    for (const auto& binding : pair_binding_codegens) {
        emit_pair_binding_snapshot(out, binding, 1);
    }
    const std::array<std::string, 2> keeps{
        emit_binding_filter(out, sys, contract, 0, pair_codegen_scope, program),
        emit_binding_filter(out, sys, contract, 1, pair_codegen_scope, program)};
    const auto& left  = pair_binding_codegens[0];
    const auto& right = pair_binding_codegens[1];
    LexicalLocalBindings pair_locals;
    for (const auto& binding : pair_binding_codegens) {
        pair_locals.insert(binding.scope.binding_name);
    }
    LocalNumericKinds pair_kinds;
    // Rendered one level deeper (5) than the innermost loop body (4) because
    // the body is wrapped in a per-invocation lambda: a bare `return`
    // spliced directly into a raw loop body would exit this whole generated
    // function on the first tuple that hits it (see dsl-pair-relations,
    // "Early return rejects only the current tuple") instead of ending just
    // that tuple's invocation.
    const auto pair_body =
        tuple_body != nullptr
            ? *tuple_body
            : rewrite_stmt_block(handler.body, 5, {}, program, {}, false, {}, &pair_codegen_scope, pair_locals, pair_kinds);
    const auto pair_body_name = gen_temp_name("pair_body", handler.location);
    const bool limited        = sys.limit.has_value();
    // dsl-rule-order-by on a pair domain: a pair pass has no backing component
    // pool for registry.sort<> to permute, so an ordered rule buffers its
    // tuples and sorts the buffer instead, with the same keyed sort the unary
    // path uses. Every dispatch site below then feeds the collector rather than
    // invoking the body directly, and the sorted buffer is replayed once at
    // the end. A rule without `order by:` pays none of this and keeps
    // streaming exactly as before.
    // dsl-rule-limit reuses that same buffer: truncation has to see the whole
    // candidate sequence in order before it can decide which tuples occupy
    // slots, so a limited rule materializes even without `order by:`.
    const bool ordered           = !sys.order_by.empty();
    const bool buffered          = ordered || limited;
    const auto tuple_buffer_name = gen_temp_name("pair_tuples", handler.location);
    const auto dispatch_name     = buffered ? gen_temp_name("pair_collect", handler.location) : pair_body_name;
    // The tuple body is defined once as a named lambda and every dispatch
    // site below (and emit_sap_pair_activation, when spatial broadphase
    // applies) calls it by name instead of re-splicing its own copy. Every
    // call site already binds the pair to locals named exactly left/right's
    // binding names, so the lambda's parameters are those same two names —
    // no per-site parameter mapping is needed.
    const auto row_predicates = where_at(sys, contract, ClauseStage::Row);
    out << "    auto " << pair_body_name << " = [&]([[maybe_unused]] entt::entity " << left.scope.binding_name
        << ", [[maybe_unused]] entt::entity " << right.scope.binding_name << ") {\n";
    if (!limited) {
        emit_where_guard(out, row_predicates, [&](const ExprNode& predicate) {
            return rewrite_expr(predicate, {}, program, {}, {}, &pair_codegen_scope);
        });
    }
    out << pair_body;
    out << "    };\n";
    // A limited rule's row predicates run here, at collection, so a rejected
    // candidate never reaches the buffer and therefore can never occupy a
    // slot truncation would otherwise have given it (dsl-rule-limit).
    const auto where_filter_name =
        limited ? emit_pair_where_filter(out, row_predicates, pair_codegen_scope, program) : std::string{};
    if (buffered) {
        out << "    std::vector<std::pair<entt::entity, entt::entity>> " << tuple_buffer_name << ";\n";
        out << "    auto " << dispatch_name << " = [&](entt::entity cactus_left, entt::entity cactus_right) {\n";
        if (!where_filter_name.empty()) {
            out << "        if (!" << where_filter_name << "(cactus_left, cactus_right)) { return; }\n";
        }
        out << "        " << tuple_buffer_name << ".emplace_back(cactus_left, cactus_right);\n";
        out << "    };\n";
    }
    const auto emit_tuple_invocation = [&out, &dispatch_name, &left, &right]() {
        out << "                " << dispatch_name << "(" << left.scope.binding_name << ", "
            << right.scope.binding_name << ");\n";
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
    out << ">(target)" << (keeps[0].empty() ? "" : " && " + keeps[0] + "(target)") << ") {\n";
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
    out << ">(target)" << (keeps[1].empty() ? "" : " && " + keeps[1] + "(target)") << ") {\n";
    out << "            for (auto " << left.scope.binding_name << " : " << left.scope.binding_name << "_snapshot) {\n";
    out << "                if (" << left.scope.binding_name << " == target) { continue; }\n";
    out << "                auto " << right.scope.binding_name << " = target;\n";
    emit_tuple_invocation();
    out << "            }\n";
    out << "        }\n";
    out << "    } else {\n";
    const auto* spatial_root = spatial_join_root(sys, contract);
    if (spatial_root == nullptr) {
        out << "        for (auto " << left.scope.binding_name << " : " << left.scope.binding_name << "_snapshot) {\n";
        out << "            for (auto " << right.scope.binding_name << " : " << right.scope.binding_name
            << "_snapshot) {\n";
        emit_tuple_invocation();
        out << "            }\n";
        out << "        }\n";
    } else if (!emit_indexed_pair_activation_if_planned(
                   out, sys, contract, *spatial_root, pair_binding_codegens, pair_codegen_scope, program, keeps,
                   dispatch_name)) {
        emit_sap_pair_activation(out,
                                 *contract->spatial_join,
                                 *spatial_root,
                                 pair_binding_codegens,
                                 pair_codegen_scope,
                                 program,
                                 dispatch_name);
    }
    out << "    }\n";
    if (!buffered) {
        return;
    }
    if (ordered) {
        // Stable: tuples with equal keys keep the pass's own left-binding-major
        // collection order (dsl-rule-order-by).
        emit_pair_sort(out, sys, contract, tuple_buffer_name, pair_binding_codegens, pair_codegen_scope, program);
    }
    if (limited) {
        emit_pair_limit_replay(out, sys, tuple_buffer_name, pair_body_name, pair_codegen_scope, program);
        return;
    }
    out << "    for (const auto& cactus_tuple : " << tuple_buffer_name << ") {\n";
    out << "        " << pair_body_name << "(cactus_tuple.first, cactus_tuple.second);\n";
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

// Recipient-targeted delivery's call to the shared handler-body lambda, gated
// by whichever of the retained `where:` predicate and the domain `limit:`
// count apply (either, both, or neither) — extracted so the branching lives
// in one place instead of the enclosing function.
static void emit_recipient_handler_call(std::ostringstream& out,
                                        const std::string& handler_body_name,
                                        bool has_retained_where,
                                        bool limited,
                                        const std::string& where_filter_name,
                                        const std::string& limit_name,
                                        const std::function<void(std::ostringstream&)>& call_args) {
    const bool guarded = has_retained_where || limited;
    if (guarded) {
        out << "            if (";
        if (has_retained_where) {
            out << where_filter_name << "(entity";
            call_args(out);
            out << ")";
        }
        if (limited) {
            out << (has_retained_where ? " && " : "") << limit_name << " > 0";
        }
        out << ") {\n";
    }
    out << "            " << handler_body_name << "(entity";
    call_args(out);
    out << ");\n";
    if (guarded) {
        out << "            }\n";
    }
}

// `filter` is the rule's own clause, or its effective clause for a lifecycle
// trigger; `recipient_only` omits the broadcast pass for handlers that are only
// ever delivered to one entity; `prelude` is spliced into the body lambda.
struct HandlerDomainCodegen {
    HandlerDomainCodegen()                                                  = default;
    HandlerDomainCodegen(const HandlerDomainCodegen&)                       = default;
    HandlerDomainCodegen& operator=(const HandlerDomainCodegen&)            = default;
    HandlerDomainCodegen(HandlerDomainCodegen&&) noexcept(false)            = default;
    HandlerDomainCodegen& operator=(HandlerDomainCodegen&&) noexcept(false) = default;
    ~HandlerDomainCodegen()                                                 = default;
    FilterClause filter;
    std::vector<FilterBinding> filter_bindings_list;
    std::vector<std::string> filter_traits;
    std::vector<std::string> filter_cpp_types;
    std::vector<std::string> exclude_cpp_types;
    // lookup_name/simple_name → canonical cpp name, so rewrite_expr/rewrite_stmt
    // resolve ambiguous traits (e.g. WorldTransform in both flat and volume).
    std::unordered_map<std::string, std::string> filter_cpp_overrides;
    bool recipient_only = false;
    std::string prelude;
};

static HandlerDomainCodegen build_handler_domain(const FilterClause& filter,
                                                 const FilterClause& exclude,
                                                 const DecoratedProgram& program) {
    HandlerDomainCodegen domain;
    domain.filter               = filter;
    domain.filter_bindings_list = filter_bindings(filter, program);
    domain.filter_traits        = filter_trait_names(filter, program);
    domain.filter_cpp_types     = filter_cpp_type_names(filter, program);
    domain.exclude_cpp_types    = filter_cpp_type_names(exclude, program);
    for (const auto& binding : domain.filter_bindings_list) {
        domain.filter_cpp_overrides.emplace(binding.trait_name, binding.cpp_type_name);
        domain.filter_cpp_overrides.emplace(binding.lookup_name, binding.cpp_type_name);
    }
    return domain;
}

static void emit_filtered_handler_body(std::ostringstream& out,
                                       const RuleNode& sys,
                                       const EventHandlerNode& handler,
                                       const HandlerDomainCodegen& domain,
                                       const DecoratedProgram& program,
                                       const HandlerContract* contract,
                                       bool iterate_in_creation_order) {
    const auto& filter_bindings_list = domain.filter_bindings_list;
    const auto& filter_traits        = domain.filter_traits;
    const auto& filter_cpp_types     = domain.filter_cpp_types;
    const auto& exclude_cpp_types    = domain.exclude_cpp_types;
    const auto& filter_cpp_overrides = domain.filter_cpp_overrides;
    auto lexical_locals              = handler_lexical_locals(handler);
    LocalNumericKinds local_kinds;
    const auto filtered_body = rewrite_stmt_block(
        handler.body, 3, filter_traits, program, {}, false, filter_cpp_overrides, nullptr, lexical_locals, local_kinds);
    const auto data_cpp_types    = filter_bindings_with_data_cpp_types(filter_bindings_list, program);
    const auto handler_body_name = gen_temp_name("handler_body", handler.location);
    // The rewritten body is shared by the recipient-targeted branch and the
    // broadcast view.each
    // branch below via one local lambda, rather than being spliced into the
    // generated output twice. Each distinct data-carrying filter trait
    // becomes an explicit parameter because the two call sites bind their
    // entity/*_comp locals in separate (sibling) scopes — a zero-parameter
    // `[&]` capture defined once cannot see locals declared later at either
    // call site.
    out << "    auto " << handler_body_name << " = [&](entt::entity entity";
    for (const auto& cpp_name : data_cpp_types) {
        out << ", [[maybe_unused]] " << cpp_name << "& " << component_local(cpp_name);
    }
    out << ") {\n";
    out << "        (void)entity;\n";
    emit_filter_alias_bindings(out, domain.filter, program, 2);
    out << domain.prelude;
    const bool limited         = sys.limit.has_value();
    const auto row_predicates  = where_at(sys, contract, ClauseStage::Row);
    const auto rewrite_unary   = [&](const ExprNode& predicate) {
        return rewrite_expr(predicate, filter_traits, program, {}, filter_cpp_overrides);
    };
    if (!limited) {
        emit_where_guard(out, row_predicates, rewrite_unary);
    }
    out << filtered_body;
    out << "    };\n";
    // A limited rule checks its row predicates in a second lambda of exactly
    // the handler body's shape — same entity/component parameters, same
    // filter-alias bindings — before a candidate can be counted toward the
    // limit or delivered (dsl-rule-limit).
    const bool has_retained_where = limited && !row_predicates.empty();
    std::string where_filter_name;
    if (has_retained_where) {
        where_filter_name = gen_temp_name("where_filter", row_predicates.front()->location);
        out << "    auto " << where_filter_name << " = [&](entt::entity entity";
        for (const auto& cpp_name : data_cpp_types) {
            out << ", [[maybe_unused]] " << cpp_name << "& " << component_local(cpp_name);
        }
        out << ") {\n";
        out << "        (void)entity;\n";
        emit_filter_alias_bindings(out, domain.filter, program, 2);
        emit_where_predicate_conjunction(out, row_predicates, rewrite_unary);
        out << "    };\n";
    }
    const auto call_args = [&data_cpp_types](std::ostringstream& os) {
        for (const auto& cpp_name : data_cpp_types) {
            os << ", " << component_local(cpp_name);
        }
    };
    // Computed once, ahead of the recipient/broadcast split, so a targeted
    // delivery can be gated by the same count a broadcast pass truncates by
    // instead of ignoring it: a live recipient still counts as one candidate
    // of the domain `limit:` bounds (dsl-rule-limit), and `limit: 0` must
    // suppress it exactly as it would suppress the last slot of a broadcast
    // pass.
    std::string limit_name;
    if (limited) {
        limit_name = gen_temp_name("limit", sys.limit->location);
        out << "    const int " << limit_name << " = "
            << rewrite_expr(*sys.limit->count, filter_traits, program, {}, filter_cpp_overrides) << ";\n";
    }
    // Recipient-targeted delivery: run at most once, for the
    // recipient, and only if it satisfies this handler's selection
    // (targeted-event-delivery, "Unary target ... only if it
    // satisfies that consumer's selection").
    out << "    if (cactus_recipient.has_value()) {\n";
    out << "        entt::entity entity = *cactus_recipient;\n";
    std::vector<std::string> recipient_checks;
    if (!filter_cpp_types.empty()) {
        recipient_checks.push_back("registry.all_of<" + join_strings(filter_cpp_types, ", ") + ">(entity)");
    }
    if (!exclude_cpp_types.empty()) {
        recipient_checks.push_back("!registry.any_of<" + join_strings(exclude_cpp_types, ", ") + ">(entity)");
    }
    out << "        if (" << (recipient_checks.empty() ? "true" : join_strings(recipient_checks, " && ")) << ") {\n";
    emit_component_bindings_from_entity(out, filter_bindings_list, "entity", 3, program);
    emit_recipient_handler_call(out, handler_body_name, has_retained_where, limited, where_filter_name, limit_name, call_args);
    out << "        }\n";
    if (domain.recipient_only) {
        out << "    }\n";
        return;
    }
    out << "    } else {\n";
    const auto sort_anchor = emit_sort_call(out, sys, filter_bindings_list, filter_traits, program, filter_cpp_overrides, 2);
    emit_view_declaration(out, filter_cpp_types, exclude_cpp_types, 2, sort_anchor);
    const bool creation_ordered = iterate_in_creation_order && sort_anchor.empty();
    if (!limited && !creation_ordered) {
        emit_view_each_header(out, filter_bindings_list, 2, program);
        out << "        " << handler_body_name << "(entity";
        call_args(out);
        out << ");\n";
        out << "        });\n";
        out << "    }\n";
        return;
    }
    // A range-for rather than view.each so a limited pass can stop the moment
    // the limit is reached, and only where:-surviving candidates count toward it.
    std::string taken_name;
    if (limited) {
        taken_name = gen_temp_name("taken", sys.limit->location);
        out << "        int " << taken_name << " = 0;\n";
    }
    out << "        for (auto entity : "
        << (creation_ordered ? "cactus::runtime::entt_backend::creation_ordered(registry, view)" : "view") << ") {\n";
    if (limited) {
        out << "            if (" << taken_name << " >= " << limit_name << ") { break; }\n";
    }
    emit_component_bindings_from_entity(out, filter_bindings_list, "entity", 3, program);
    if (has_retained_where) {
        out << "            if (!" << where_filter_name << "(entity";
        call_args(out);
        out << ")) { continue; }\n";
    }
    if (limited) {
        out << "            ++" << taken_name << ";\n";
    }
    out << "            " << handler_body_name << "(entity";
    call_args(out);
    out << ");\n";
    out << "        }\n";
    out << "    }\n";
}

// A lifecycle trigger is delivered only to the entity whose trait set changed:
// `on added T` adds T to the selection (bound under its alias), `on removed T`
// adds it to the exclusion and binds the pre-removal snapshot.
static void emit_lifecycle_handler_body(std::ostringstream& out,
                                        const RuleNode& sys,
                                        const EventHandlerNode& handler,
                                        const std::string& trigger_binding,
                                        const DecoratedProgram& program,
                                        const HandlerContract* contract) {
    const auto& trigger = *handler.resolved_trigger;
    FilterClause filter  = sys.filter;
    FilterClause exclude = sys.exclude;
    const FilterEntry entry{.qualified_name    = handler.event_name,
                            .resolved_trait_id = trigger.symbol,
                            .alias             = trigger.kind == HandlerTriggerKind::TraitAdded ? handler.alias
                                                                                                : std::nullopt,
                            .location          = handler.trigger_location};
    auto& clause = trigger.kind == HandlerTriggerKind::TraitAdded ? filter : exclude;
    clause.entries.push_back(entry);
    clause.resolved_trait_ids.push_back(trigger.symbol);

    auto domain           = build_handler_domain(filter, exclude, program);
    domain.recipient_only = true;
    if (trigger.kind == HandlerTriggerKind::TraitRemoved && handler.alias.has_value()) {
        domain.prelude = "        [[maybe_unused]] const auto& " + *handler.alias + " = " + trigger_binding + ".old;\n";
    }
    emit_filtered_handler_body(out, sys, handler, domain, program, contract, false);
}

// Defensive fallback (should not occur for a well-formed contract lookup):
// no filter clause, contract not classified selectionless.
static void emit_fallback_handler_body(std::ostringstream& out,
                                       const RuleNode& sys,
                                       const EventHandlerNode& handler,
                                       const std::vector<FilterBinding>& filter_bindings_list,
                                       const std::vector<std::string>& filter_traits,
                                       const DecoratedProgram& program,
                                       const std::unordered_map<std::string, std::string>& filter_cpp_overrides) {
    emit_sort_call(out, sys, filter_bindings_list, filter_traits, program, filter_cpp_overrides);
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

// Structural changes are buffered to the commit, so the hoisted references stay valid all pass.
static void emit_named_requirements(std::ostringstream& out,
                                    const HandlerContract* contract,
                                    const DecoratedProgram& program) {
    if (contract == nullptr || contract->named_requirements.empty()) {
        return;
    }
    std::vector<std::pair<SymbolId, std::string>> trait_lists;
    for (const auto& requirement : contract->named_requirements) {
        const auto trait = EnttCodegenUtils::trait_cpp_name(requirement.trait, requirement.trait.local_name, program);
        auto found = std::ranges::find(trait_lists, requirement.entity, &std::pair<SymbolId, std::string>::first);
        if (found == trait_lists.end()) {
            trait_lists.emplace_back(requirement.entity, trait);
        } else {
            found->second += ", " + trait;
        }
    }
    for (const auto& [entity, traits] : trait_lists) {
        out << "    if (!cactus::runtime::entt_backend::named_alive<" << traits << ">(registry, "
            << EnttCodegenUtils::named_slot_name(entity) << ")) {\n";
        out << "        return;\n";
        out << "    }\n";
    }
    for (const auto& requirement : contract->named_requirements) {
        const NamedTraitRef ref{.entity = requirement.entity, .trait = requirement.trait};
        out << "    [[maybe_unused]] auto& " << EnttCodegenUtils::named_trait_ref_name(ref) << " = registry.get<"
            << EnttCodegenUtils::trait_cpp_name(requirement.trait, requirement.trait.local_name, program) << ">("
            << EnttCodegenUtils::named_slot_name(requirement.entity) << ");\n";
    }
}

static void emit_when_gate(std::ostringstream& out, const RuleNode& sys, const DecoratedProgram& program) {
    if (!sys.when_clause.has_value() || sys.when_clause->predicates.empty()) {
        return;
    }
    std::vector<const ExprNode*> predicates;
    for (const auto& predicate : sys.when_clause->predicates) {
        predicates.push_back(predicate.get());
    }
    out << "    if (" << rejects(unbound_conjunction(predicates, program)) << ") {\n";
    out << "        return;\n";
    out << "    }\n";
}

// ── Rule reductions ─────────────────────────────────────────────────────────

static std::string reducer_cpp_type(const ReducerDecl& reducer) {
    return EnttCodegenUtils::value_type_to_cpp(reducer.result_type);
}

// One accumulator per group, and the finished aggregate row the handler and
// sort keys read. `<name>_seen` marks a min/max/best that has folded a value,
// so a default applies only to empty input. `cactus_witnessed` marks a group
// with rows, for a keep without a condition.
static void emit_reduce_types(std::ostringstream& out, const ReduceClause& reduce, bool witness) {
    out << "    struct cactus_reduce_acc {\n";
    for (const auto& reducer : reduce.reducers) {
        const auto type = reducer_cpp_type(reducer);
        out << "        "
            << (reducer.kind == ReducerKind::Sum ? "cactus::runtime::reduce::Sum<" + type + ">" : type) << " "
            << reducer.name;
        switch (reducer.kind) {
            case ReducerKind::FirstHit:
                out << " = cactus_collider_sweep_miss();\n";
                break;
            case ReducerKind::Best:
                out << "{entt::null};\n";
                out << "        " << EnttCodegenUtils::value_type_to_cpp(reducer.key_type) << " " << reducer.name
                    << "_key{};\n";
                out << "        bool " << reducer.name << "_seen = false;\n";
                break;
            default:
                out << "{};\n";
                break;
        }
        if (reducer_has_default(reducer.kind)) {
            out << "        bool " << reducer.name << "_seen = false;\n";
        }
    }
    if (witness) {
        out << "        bool cactus_witnessed = false;\n";
    }
    out << "    };\n";
    out << "    struct cactus_reduce_row {\n";
    out << "        entt::entity cactus_group{entt::null};\n";
    for (const auto& reducer : reduce.reducers) {
        out << "        " << reducer_cpp_type(reducer) << " " << reducer.name << "{};\n";
    }
    if (witness) {
        out << "        bool cactus_witnessed = false;\n";
    }
    out << "    };\n";
}

// Folds one accepted row into `cactus_acc`.
static void emit_reducer_folds(std::ostringstream& out,
                               const ReduceClause& reduce,
                               const std::function<std::string(const ExprNode&)>& rewrite,
                               const std::string& ind,
                               bool witness) {
    const std::string ns = "cactus::runtime::reduce::";
    if (witness) {
        out << ind << "cactus_acc.cactus_witnessed = true;\n";
    }
    for (const auto& reducer : reduce.reducers) {
        const auto field = "cactus_acc." + reducer.name;
        switch (reducer.kind) {
            case ReducerKind::Count:
                out << ind << field << " = cactus::num::add(" << field << ", 1);\n";
                break;
            case ReducerKind::Sum:
                out << ind << field << ".add(" << rewrite(*reducer.input) << ");\n";
                break;
            case ReducerKind::Min:
            case ReducerKind::Max: {
                const char* step = reducer.kind == ReducerKind::Min ? "min" : "max";
                out << ind << "{\n";
                out << ind << "    const " << reducer_cpp_type(reducer) << " cactus_value = " << rewrite(*reducer.input) << ";\n";
                out << ind << "    " << field << " = " << field << "_seen ? " << ns << step << "(" << field
                    << ", cactus_value) : cactus_value;\n";
                out << ind << "    " << field << "_seen = true;\n";
                out << ind << "}\n";
                break;
            }
            case ReducerKind::Any:
                out << ind << "if (!" << field << ") { " << field << " = " << rewrite(*reducer.input) << "; }\n";
                break;
            case ReducerKind::FirstHit:
                // Strict `<`: on equal t the earlier row in fold order wins.
                out << ind << "{\n";
                out << ind << "    const auto cactus_value = " << rewrite(*reducer.input) << ";\n";
                out << ind << "    if (cactus_value.hit && (!" << field << ".hit || cactus_value.t < " << field
                    << ".t)) { " << field << " = cactus_value; }\n";
                out << ind << "}\n";
                break;
            case ReducerKind::Best:
                out << ind << "{\n";
                out << ind << "    const " << EnttCodegenUtils::value_type_to_cpp(reducer.key_type)
                    << " cactus_key = " << rewrite(*reducer.key) << ";\n";
                out << ind << "    if (" << ns << "improves(cactus_key, " << field << "_key, " << field << "_seen)) {\n";
                out << ind << "        " << field << " = " << rewrite(*reducer.input) << ";\n";
                out << ind << "        " << field << "_key = cactus_key;\n";
                out << ind << "        " << field << "_seen = true;\n";
                out << ind << "    }\n";
                out << ind << "}\n";
                break;
        }
    }
}

// Turns `cactus_acc` into a row appended to `cactus_rows`.
static void emit_reduce_row(std::ostringstream& out,
                            const ReduceClause& reduce,
                            const std::string& group,
                            const DecoratedProgram& program,
                            const std::string& ind,
                            bool witness = false) {
    out << ind << "cactus_reduce_row cactus_row;\n";
    out << ind << "cactus_row.cactus_group = " << group << ";\n";
    if (witness) {
        out << ind << "cactus_row.cactus_witnessed = cactus_acc.cactus_witnessed;\n";
    }
    for (const auto& reducer : reduce.reducers) {
        out << ind << "cactus_row." << reducer.name << " = ";
        if (reducer_has_default(reducer.kind)) {
            out << "cactus_acc." << reducer.name << "_seen ? cactus_acc." << reducer.name << " : "
                << rewrite_expr(*reducer.default_value, {}, program) << ";\n";
        } else if (reducer.kind == ReducerKind::Sum) {
            out << "cactus_acc." << reducer.name << ".value();\n";
        } else {
            out << "cactus_acc." << reducer.name << ";\n";
        }
    }
    out << ind << "cactus_rows.push_back(cactus_row);\n";
}

// Declares `cactus_rows` holding the single row of a global reduction.
static void emit_total_reduce_rows(std::ostringstream& out, const ReduceClause& reduce, const DecoratedProgram& program) {
    out << "    std::vector<cactus_reduce_row> cactus_rows;\n";
    out << "    {\n";
    out << "        const auto& cactus_acc = cactus_total;\n";
    emit_reduce_row(out, reduce, "entt::null", program, "        ");
    out << "    }\n";
}

// The names a reduced handler or sort key reads off `cactus_row`.
static void emit_reduce_row_bindings(std::ostringstream& out,
                                     const ReduceClause& reduce,
                                     const std::string& ind) {
    if (reduce.per_binding.has_value()) {
        out << ind << "[[maybe_unused]] const auto " << *reduce.per_binding << " = cactus_row.cactus_group;\n";
    }
    for (const auto& reducer : reduce.reducers) {
        out << ind << "[[maybe_unused]] const auto " << reducer.name << " = cactus_row." << reducer.name << ";\n";
    }
}

// order by → limit over the aggregate rows, then one handler call per row.
static void emit_reduced_dispatch(std::ostringstream& out,
                                  const RuleNode& sys,
                                  const EventHandlerNode& handler,
                                  const PairCodegenScope* group_scope,
                                  const DecoratedProgram& program) {
    const auto& reduce = *sys.reduce;
    if (!sys.order_by.empty()) {
        std::vector<std::string> key_names;
        for (const auto& key : sys.order_by) {
            auto name = gen_temp_name("reduce_sort_key", key.location);
            out << "    auto " << name << " = [&](const cactus_reduce_row& cactus_row) {\n";
            emit_reduce_row_bindings(out, reduce, "        ");
            out << "        return " << rewrite_expr(*key.expression, {}, program, {}, {}, group_scope) << ";\n";
            out << "    };\n";
            key_names.push_back(std::move(name));
        }
        std::vector<std::string> row_keys;
        row_keys.reserve(key_names.size());
        for (const auto& key_name : key_names) {
            row_keys.push_back(key_name + "(cactus_row)");
        }
        emit_keyed_sort(out, sys, "cactus_rows", row_keys, 1);
    }
    if (sys.limit.has_value()) {
        out << "    {\n";
        out << "        const int cactus_limit = " << rewrite_expr(*sys.limit->count, {}, program) << ";\n";
        out << "        const auto cactus_keep = static_cast<std::size_t>(std::max(cactus_limit, 0));\n";
        out << "        if (cactus_rows.size() > cactus_keep) {\n";
        out << "            cactus_rows.resize(cactus_keep);\n";
        out << "        }\n";
        out << "    }\n";
    }

    auto lexical_locals = handler_lexical_locals(handler);
    LocalNumericKinds local_kinds;
    for (const auto& reducer : reduce.reducers) {
        lexical_locals.insert(reducer.name);
        const auto kind = reducer.result_type.kind;
        if (kind == TypeKind::Int || kind == TypeKind::Float) {
            local_kinds[reducer.name] = kind == TypeKind::Int ? NumericKind::Int : NumericKind::Float;
        }
    }
    if (reduce.per_binding.has_value()) {
        lexical_locals.insert(*reduce.per_binding);
    }
    // A lambda, so a `return` in the body ends only this group's activation.
    const auto body = rewrite_stmt_block(
        handler.body, 2, {}, program, {}, false, {}, group_scope, lexical_locals, local_kinds);
    out << "    auto cactus_reduced_body = [&](const cactus_reduce_row& cactus_row) {\n";
    emit_reduce_row_bindings(out, reduce, "        ");
    out << body;
    out << "    };\n";
    out << "    for (const auto& cactus_row : cactus_rows) {\n";
    out << "        cactus_reduced_body(cactus_row);\n";
    out << "    }\n";
}

static bool rule_uses_collider_queries(const RuleNode& sys) {
    bool found = false;
    const auto check = [&](const ExprNode& root) {
        visit_expression(root, [&](const ExprNode& node) {
            const auto* call = std::get_if<CallExpr>(&node.expr);
            found            = found || (call != nullptr && collider_query_kind(*call).has_value());
        });
    };
    if (sys.where_clause.has_value()) {
        for (const auto& predicate : sys.where_clause->predicates) {
            check(*predicate);
        }
    }
    if (sys.reduce.has_value()) {
        for (const auto& reducer : sys.reduce->reducers) {
            if (reducer.input != nullptr) {
                check(*reducer.input);
            }
        }
    }
    return found;
}

// Descriptors for every entity of one binding's snapshot, in snapshot order,
// and the `cactus_shape_of_<binding>` accessor the fold reads them by.
static void emit_collider_shape_cache(std::ostringstream& out, const PairCodegenBinding& binding) {
    const auto& name = binding.binding_name;
    out << "    std::vector<std::optional<cactus::runtime::physics::ColliderShape>> cactus_shapes_" << name << ";\n";
    out << "    cactus_shapes_" << name << ".reserve(" << name << "_snapshot.size());\n";
    out << "    for (const auto " << name << " : " << name << "_snapshot) {\n";
    out << "        cactus_shapes_" << name << ".push_back(" << binding.collider_shape_builder << "(registry, " << name
        << "));\n";
    out << "    }\n";
    out << "    [[maybe_unused]] const auto cactus_shape_of_" << name
        << " = [&](entt::entity cactus_entity) -> const std::optional<cactus::runtime::physics::ColliderShape>& {\n";
    out << "        return cactus_shapes_" << name << "[cactus_positions_" << name << ".find(cactus_entity)];\n";
    out << "    };\n";
}

// `apply_kept<T, tracked>` or `apply_unkept<T, tracked>`, as the queued command calls it.
static std::string kept_command(const char* function, const KeepClause& keep, const DecoratedProgram& program) {
    const auto cpp = EnttCodegenUtils::trait_cpp_name(keep.resolved_target, keep.target_name, program);
    const bool tracked =
        EnttPersistenceEmitter::structural_site_tracks_construction(program, keep.resolved_target, keep.target_name);
    return std::string("cactus::runtime::entt_backend::") + function + "<" + cpp + ", " + (tracked ? "true" : "false") +
           ">";
}

// Queues `T` on `recipient` with this run's field values, at the activation's commit.
static void emit_keep_add(std::ostringstream& out,
                          const KeepClause& keep,
                          const std::string& recipient,
                          const std::function<std::string(const ExprNode&)>& rewrite,
                          const DecoratedProgram& program,
                          const std::string& ind) {
    // With no assigned fields the value is all defaults, so a present trait needs no patch.
    const bool patches = !keep.fields.empty();
    if (patches) {
        out << ind << EnttCodegenUtils::trait_cpp_name(keep.resolved_target, keep.target_name, program)
            << " cactus_value{};\n";
    }
    for (const auto& field : keep.fields) {
        out << ind << "cactus_value." << field.name << " = " << rewrite(*field.value) << ";\n";
    }
    out << ind << "cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    out << ind << "    cactus::runtime::entt_backend::StructuralCommand::Kind::Add,\n";
    out << ind << "    [cactus_target = " << recipient << (patches ? ", cactus_value" : "")
        << "](entt::registry& registry) {\n";
    out << ind << "        " << kept_command("apply_kept", keep, program) << "(registry, cactus_target"
        << (patches ? ", cactus_value" : "") << ");\n";
    out << ind << "    });\n";
}

// Queues the removal of `T` from every carrier `cactus_is_kept` rejects, oldest first.
static void emit_keep_removals(std::ostringstream& out, const KeepClause& keep, const DecoratedProgram& program) {
    const auto cpp = EnttCodegenUtils::trait_cpp_name(keep.resolved_target, keep.target_name, program);
    out << "    for (const auto cactus_entity : cactus::runtime::entt_backend::leaving_carriers<" << cpp
        << ">(registry, cactus_is_kept)) {\n";
    out << "        cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    out << "            cactus::runtime::entt_backend::StructuralCommand::Kind::Remove,\n";
    out << "            [cactus_target = cactus_entity](entt::registry& registry) {\n";
    out << "                " << kept_command("apply_unkept", keep, program) << "(registry, cactus_target);\n";
    out << "            });\n";
    out << "    }\n";
}

// `keep T on b [if c]`: every group with rows, or whose condition holds, gets
// `T`, and every other carrier of `T` loses it, at the activation's commit.
static void emit_keep_commands(std::ostringstream& out,
                               const KeepClause& keep,
                               const ReduceClause& reduce,
                               const PairCodegenScope& group_scope,
                               const DecoratedProgram& program) {
    const auto& group  = group_scope.bindings.front().binding_name;
    const auto rewrite = [&](const ExprNode& expr) {
        return rewrite_expr(expr, {}, program, {}, {}, &group_scope);
    };
    out << "    std::vector<char> cactus_kept(" << group << "_snapshot.size(), 0);\n";
    out << "    for (const auto& cactus_row : cactus_rows) {\n";
    emit_reduce_row_bindings(out, reduce, "        ");
    const auto present = keep.condition != nullptr ? rewrite(*keep.condition) : "cactus_row.cactus_witnessed";
    out << "        if (" << rejects(present) << ") { continue; }\n";
    out << "        cactus_kept[cactus_positions_" << group << ".find(cactus_row.cactus_group)] = 1;\n";
    emit_keep_add(out, keep, group, rewrite, program, "        ");
    out << "    }\n";
    out << "    auto cactus_is_kept = [&](entt::entity cactus_carrier) {\n";
    out << "        const auto cactus_slot = cactus_positions_" << group << ".find(cactus_carrier);\n";
    out << "        return cactus_slot != cactus::runtime::entt_backend::SnapshotPositions::kAbsent && "
           "cactus_kept[cactus_slot] != 0;\n";
    out << "    };\n";
    emit_keep_removals(out, keep, program);
}

// The group binding first: with no groups there is nothing to run, unless the
// rule keeps a trait and so must still remove every carrier.
static void emit_reduced_pair_snapshots(std::ostringstream& out,
                                        const ReduceClause& reduce,
                                        const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                        bool keeps,
                                        const std::optional<std::string>& indexed) {
    const auto is_group = [&](const PairBindingCodegen& binding) {
        return reduce.per_binding == binding.scope.binding_name;
    };
    for (const auto& binding : pair_binding_codegens | std::views::filter(is_group)) {
        emit_pair_binding_snapshot(out, binding, 1);
        if (!keeps) {
            out << "    if (" << binding.scope.binding_name << "_snapshot.empty()) {\n";
            out << "        return;\n";
            out << "    }\n";
        }
    }
    for (const auto& binding : pair_binding_codegens | std::views::filter(std::not_fn(is_group))) {
        if (binding.scope.binding_name != indexed) {
            emit_pair_binding_snapshot(out, binding, 1);
        }
    }
}

// Binding-stage reducer inputs, computed once per member of their binding.
// Returns, per input expression, how the fold reads it for the current row:
// through the group slot for the group binding, by snapshot position otherwise.
static std::unordered_map<const ExprNode*, std::string> emit_member_inputs(std::ostringstream& out,
                                                                          const ReduceClause& reduce,
                                                                          const HandlerContract* contract,
                                                                          const PairCodegenScope& fold_scope,
                                                                          const DecoratedProgram& program,
                                                                          const std::optional<std::string>& indexed) {
    std::unordered_map<const ExprNode*, std::string> member_inputs;
    for (std::size_t index = 0; index < reduce.reducers.size(); ++index) {
        const auto& reducer = reduce.reducers[index];
        const auto stage    = stage_at(staging_of(contract).reducers, index);
        if (reducer.kind == ReducerKind::Count || !is_binding_stage(stage)) {
            continue;
        }
        const auto& name = fold_scope.bindings[stage == ClauseStage::BindingLeft ? 0 : 1].binding_name;
        if (name == indexed) {
            continue;
        }
        auto slot = std::string("[cactus_slot]");
        if (reduce.per_binding != name) {
            slot = "[cactus_positions_" + name;
            slot += ".find(" + name + ")]";
        }
        for (const auto* expr : {reducer.input.get(), reducer.key.get()}) {
            if (expr == nullptr) {
                continue;
            }
            const auto values = "cactus_inputs_" + std::to_string(index) + (expr == reducer.key.get() ? "_key" : "");
            out << "    auto " << values << "_of = [&](entt::entity " << name << ") { return "
                << rewrite_expr(*expr, {}, program, {}, {}, &fold_scope) << "; };\n";
            emit_member_table(out, values, values + "_of(entt::null)", values + "_of(cactus_member)", name);
            member_inputs.emplace(expr, values + slot);
        }
    }
    return member_inputs;
}

// Filters every binding but the group binding, which keeps every member so
// its group filters decide which groups exist. Returns which were filtered.
static std::array<bool, 2> emit_reduced_binding_filters(std::ostringstream& out,
                                                        const RuleNode& sys,
                                                        const ReduceClause& reduce,
                                                        const HandlerContract* contract,
                                                        const PairCodegenScope& pair_codegen_scope,
                                                        const DecoratedProgram& program,
                                                        const std::optional<std::string>& indexed) {
    std::array<bool, 2> filtered{};
    for (std::size_t index = 0; index < filtered.size(); ++index) {
        const auto& name = pair_codegen_scope.bindings[index].binding_name;
        filtered[index]  = reduce.per_binding != name && name != indexed &&
                          !emit_binding_filter(out, sys, contract, index, pair_codegen_scope, program).empty();
    }
    return filtered;
}

// Snapshot positions and collider descriptors the fold reads, built once per pass.
static void emit_fold_tables(std::ostringstream& out,
                             const RuleNode& sys,
                             const ReduceClause& reduce,
                             const HandlerContract* contract,
                             PairCodegenScope& fold_scope,
                             const std::optional<std::string>& indexed) {
    const bool uses_collider_queries = rule_uses_collider_queries(sys);
    const auto& reducer_stages       = staging_of(contract).reducers;
    for (std::size_t index = 0; index < fold_scope.bindings.size(); ++index) {
        const auto& name = fold_scope.bindings[index].binding_name;
        if (name == indexed) {
            continue;
        }
        const bool member_inputs = std::ranges::contains(reducer_stages, binding_stage(index));
        if (uses_collider_queries || reduce.per_binding == name || member_inputs) {
            emit_snapshot_positions(out, name);
        }
    }
    if (!uses_collider_queries) {
        return;
    }
    for (auto& binding : fold_scope.bindings) {
        if (binding.binding_name != indexed) {
            emit_collider_shape_cache(out, binding);
            binding.collider_shapes_cached = true;
        }
    }
}

// One group per `per` entity that passes the group filters; a targeted event
// keeps only the recipient's group.
static void emit_group_table(std::ostringstream& out,
                             const ReduceClause& reduce,
                             const std::vector<std::unique_ptr<ExprNode>>& predicates,
                             const std::function<std::string(const ExprNode&)>& rewrite) {
    const auto& group = *reduce.per_binding;
    out << "    std::vector<cactus_reduce_acc> cactus_groups(" << group << "_snapshot.size());\n";
    out << "    std::vector<char> cactus_group_live(" << group << "_snapshot.size(), 0);\n";
    out << "    for (std::size_t cactus_i = 0; cactus_i < " << group << "_snapshot.size(); ++cactus_i) {\n";
    out << "        const auto " << group << " = " << group << "_snapshot[cactus_i];\n";
    out << "        if (cactus_recipient.has_value() && *cactus_recipient != " << group << ") { continue; }\n";
    for (const auto index : reduce.group_predicates) {
        out << "        if (" << rejects(rewrite(*predicates[index])) << ") { continue; }\n";
    }
    out << "        cactus_group_live[cactus_i] = 1;\n";
    out << "    }\n";
}

// Folds one accepted row into its group's (or the total's) accumulator.
static void emit_fold_lambda(std::ostringstream& out,
                             const ReduceClause& reduce,
                             bool witness,
                             const PairCodegenScope& fold_scope,
                             const std::vector<const ExprNode*>& row_predicates,
                             const std::function<std::string(const ExprNode&)>& rewrite) {
    out << "    auto cactus_fold = [&]([[maybe_unused]] entt::entity " << fold_scope.bindings[0].binding_name
        << ", [[maybe_unused]] entt::entity " << fold_scope.bindings[1].binding_name << ") {\n";
    if (reduce.per_binding.has_value()) {
        const auto& group = *reduce.per_binding;
        out << "        const auto cactus_slot = cactus_positions_" << group << ".find(" << group << ");\n";
        out << "        if (cactus_slot == cactus::runtime::entt_backend::SnapshotPositions::kAbsent || "
               "cactus_group_live[cactus_slot] == 0) { return; }\n";
        out << "        auto& cactus_acc = cactus_groups[cactus_slot];\n";
        if (witness && reduce.reducers.empty()) {
            out << "        if (cactus_acc.cactus_witnessed) { return; }\n";
        }
    } else {
        out << "        auto& cactus_acc = cactus_total;\n";
    }
    for (const auto* predicate : row_predicates) {
        out << "        if (" << rejects(rewrite(*predicate)) << ") { return; }\n";
    }
    emit_reducer_folds(out, reduce, rewrite, "        ", witness);
    out << "    };\n";
}

// Feeds every row, from the broad phase when one applies, to the fold; a false
// activation gate feeds none.
static void emit_fold_dispatch(std::ostringstream& out,
                               const RuleNode& sys,
                               const HandlerContract* contract,
                               bool gated,
                               const std::vector<PairBindingCodegen>& pair_binding_codegens,
                               const PairCodegenScope& fold_scope,
                               const DecoratedProgram& program) {
    if (gated) {
        out << "    if (cactus_active) {\n";
    }
    const auto* spatial_root = spatial_join_root(sys, contract);
    if (spatial_root != nullptr) {
        emit_sap_pair_activation(
            out, *contract->spatial_join, *spatial_root, pair_binding_codegens, fold_scope, program, "cactus_fold");
    } else {
        const auto& left  = fold_scope.bindings[0].binding_name;
        const auto& right = fold_scope.bindings[1].binding_name;
        out << "    for (auto " << left << " : " << left << "_snapshot) {\n";
        out << "        for (auto " << right << " : " << right << "_snapshot) {\n";
        out << "            cactus_fold(" << left << ", " << right << ");\n";
        out << "        }\n";
        out << "    }\n";
    }
    if (gated) {
        out << "    }\n";
    }
}

// ── Indexed folds ────────────────────────────────────────────────────────────
// A reduced pair pass whose spatial plan bounds the non-group binding by its
// own fixed values keeps that binding in a SpatialBindingIndex3D across passes.
// Each group entity then looks up the members its bounds overlap, oldest first,
// so every group folds the same rows in the same order as the full product.

struct IndexedFoldPlan {
    std::size_t indexed = 0;  // the binding kept in the index
    const SpatialShape* indexed_shape = nullptr;
    const SpatialShape* query_shape   = nullptr;
    std::vector<SymbolId> watched;   // traits whose construction or destruction can change a member
    std::vector<SymbolId> required;  // traits every member carries
    TraitWriters writers;
};

// Whether `expr` depends on nothing but constants and the values `binding` reads
// from its own entity.
static bool fixed_for_member(const ExprNode& expr, std::size_t binding, const std::unordered_set<std::string>& names) {
    const auto fixed = [&](const std::unique_ptr<ExprNode>& child) {
        return child != nullptr && fixed_for_member(*child, binding, names);
    };
    return std::visit(
        [&](const auto& node) -> bool {
            using E = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                return true;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return node.resolved_const_id.has_value() || names.contains(node.name);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (node.resolved_const_id.has_value() || node.resolved_enum_member.has_value()) {
                    return true;
                }
                return !node.resolved_entity_id.has_value() && !node.resolved_phase_dt.has_value() && fixed(node.object);
            } else if constexpr (std::is_same_v<E, TraitAccessExpr>) {
                return node.pair_binding_index == binding;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return fixed(node.left) && fixed(node.right);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return fixed(node.operand);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                return (node.resolved_callee_id.has_value() || node.resolved_struct_id.has_value()) &&
                       std::ranges::all_of(node.args, fixed);
            } else {
                return false;
            }
        },
        expr.expr);
}

static std::vector<SymbolId> collider_proxy_traits() {
    std::vector<SymbolId> traits{
        make_symbol_id(SymbolKind::Trait, ModuleId{.name = std::string(kTransformVolumeModule)}, "WorldTransform"),
        make_symbol_id(SymbolKind::Trait, ModuleId{.name = std::string(kPhysicsVolumeModule)}, "Collider")};
    std::ranges::move(collider_shape_traits(), std::back_inserter(traits));
    return traits;
}

static void add_unique(std::vector<SymbolId>& into, const SymbolId& trait) {
    if (!std::ranges::contains(into, trait)) {
        into.push_back(trait);
    }
}

// The traits a slot expression reads through `binding`, by bracket access or
// through one of `aliases` (alias name -> trait).
static void collect_slot_traits(const ExprNode& expr,
                                std::size_t binding,
                                const std::unordered_map<std::string, SymbolId>& aliases,
                                std::vector<SymbolId>& traits) {
    visit_expression(expr, [&](const ExprNode& node) {
        if (const auto* access = std::get_if<TraitAccessExpr>(&node.expr);
            access != nullptr && access->pair_binding_index == binding && access->resolved_trait_id.has_value()) {
            add_unique(traits, *access->resolved_trait_id);
        }
        if (const auto* ident = std::get_if<IdentExpr>(&node.expr); ident != nullptr) {
            if (const auto found = aliases.find(ident->name); found != aliases.end()) {
                add_unique(traits, found->second);
            }
        }
    });
}

// A spatial pair pass whose plan bounds the binding other than `group` by its
// own fixed values can keep that binding in a SpatialBindingIndex3D.
static std::optional<IndexedFoldPlan> indexed_join_plan(const RuleNode& sys,
                                                        const HandlerContract* contract,
                                                        const DecoratedProgram& program,
                                                        std::size_t group) {
    if (!program.index_pair_joins || contract == nullptr || !contract->spatial_join.has_value() ||
        !sys.pairs.has_value() || contract->pair_bindings.size() != 2) {
        return std::nullopt;
    }
    const auto& join = *contract->spatial_join;
    if (join.dimension != SpatialJoinDimension::Volume3D) {
        return std::nullopt;
    }
    IndexedFoldPlan plan;
    plan.indexed       = 1 - group;
    plan.indexed_shape = join.left.binding_index == plan.indexed ? &join.left : &join.right;
    plan.query_shape   = join.left.binding_index == plan.indexed ? &join.right : &join.left;
    if (plan.indexed_shape->binding_index != plan.indexed || plan.query_shape->binding_index != group ||
        plan.indexed_shape->kind == SpatialShapeKind::SweptCollider) {
        return std::nullopt;
    }

    const auto& binding_node = sys.pairs->bindings[plan.indexed];
    std::unordered_set<std::string> names{binding_node.name};
    std::unordered_map<std::string, SymbolId> aliases;
    for (const auto& entry : binding_node.traits) {
        if (entry.alias.has_value() && entry.resolved_trait_id.has_value()) {
            names.insert(*entry.alias);
            aliases.emplace(*entry.alias, *entry.resolved_trait_id);
        }
    }
    std::vector<SymbolId> proxy_traits;
    if (is_collider_shape(plan.indexed_shape->kind)) {
        proxy_traits = collider_proxy_traits();
    }
    const auto* root = spatial_join_root(sys, contract);
    for (const auto& slot : plan.indexed_shape->slots) {
        const auto* argument = root == nullptr ? nullptr : resolve_expr_path(*root, slot);
        if (argument == nullptr || !fixed_for_member(*argument, plan.indexed, names)) {
            return std::nullopt;
        }
        collect_slot_traits(*argument, plan.indexed, aliases, proxy_traits);
    }
    plan.writers         = trait_writers(program, proxy_traits);
    const auto& required = contract->pair_bindings[plan.indexed].required_traits;
    const auto covers_all = [&](const std::vector<SymbolId>& set) {
        return std::ranges::all_of(set, [&](const SymbolId& trait) { return std::ranges::contains(required, trait); });
    };
    if (std::ranges::any_of(plan.writers.target_sets, covers_all)) {
        return std::nullopt;
    }
    plan.watched  = required;
    plan.required = required;
    for (const auto& trait : proxy_traits) {
        add_unique(plan.watched, trait);
    }
    for (const auto& set : plan.writers.target_sets) {
        for (const auto& trait : set) {
            add_unique(plan.watched, trait);
        }
    }
    std::ranges::sort(plan.watched, std::less{}, [](const SymbolId& trait) { return make_canonical_id(trait); });
    return plan;
}

static std::optional<IndexedFoldPlan> indexed_fold_plan(const RuleNode& sys,
                                                        const HandlerContract* contract,
                                                        const DecoratedProgram& program) {
    if (contract == nullptr || !contract->reduction.has_value() || !contract->reduction->group_binding.has_value()) {
        return std::nullopt;
    }
    return indexed_join_plan(sys, contract, program, *contract->reduction->group_binding);
}

// A plain pair pass indexes its right binding, so walking the left snapshot and
// each left member's candidates oldest first keeps the full product's order.
static std::optional<IndexedFoldPlan> indexed_pair_plan(const RuleNode& sys,
                                                        const HandlerContract* contract,
                                                        const DecoratedProgram& program) {
    if (sys.reduce.has_value()) {
        return std::nullopt;
    }
    return indexed_join_plan(sys, contract, program, 0);
}

static std::string cpp_trait_list(const std::vector<SymbolId>& traits, const DecoratedProgram& program) {
    std::vector<std::string> names;
    names.reserve(traits.size());
    for (const auto& trait : traits) {
        names.push_back(EnttCodegenUtils::trait_cpp_name(trait, trait.local_name, program));
    }
    return join_strings(names, ", ");
}

// Whether a member's bounds may change after construction: it carries every
// trait of some writer's target set, or it is a named entity a handler writes.
static std::string member_may_change(const TraitWriters& writers,
                                     const std::string& entity,
                                     const DecoratedProgram& program) {
    std::vector<std::string> tests;
    tests.reserve(writers.target_sets.size() + writers.named_entities.size());
    for (const auto& set : writers.target_sets) {
        tests.push_back("registry.all_of<" + cpp_trait_list(set, program) + ">(" + entity + ")");
    }
    for (const auto& named : writers.named_entities) {
        tests.push_back(entity + " == " + EnttCodegenUtils::named_slot_name(named));
    }
    return tests.empty() ? "false" : join_strings(tests, " || ");
}

// A stable 64-bit hash, so a program's generated keys never vary between runs.
static std::uint64_t stable_hash(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : text) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
    }
    return hash;
}

// Declares `index` as the shared instance for the sync code that follows it.
static void emit_shared_index(std::ostringstream& out,
                              const std::string& index_type,
                              const std::string& index,
                              const std::string& sync) {
    out << "    auto& " << index << " = cactus::runtime::entt_backend::shared_binding_index<" << index_type << ", "
        << stable_hash(index_type + sync) << "ULL>();\n";
    out << sync;
}

// The grid cell size a spatial index starts from; 0 lets the index choose it when first built.
static std::string spatial_cell_size(const IndexedFoldPlan& plan,
                                     const std::vector<std::size_t>& origins,
                                     const DecoratedProgram& program) {
    if (program.grid_cell_size.has_value()) {
        return EnttCodegenUtils::float_literal_cpp(std::to_string(*program.grid_cell_size));
    }
    if (!is_collider_shape(plan.indexed_shape->kind)) {
        return "0.0F";
    }
    return constant_collider_cell_size(program, origins).value_or("0.0F");
}

constexpr std::string_view kStaticStoreMark = "@CACTUS_STATIC_STORE:";

// A static store, named once every index of the program is known (see
// assign_static_stores): `bounds` is the code that decides a member's bounds,
// `origins` the archetypes its members can come from.
static std::string static_store_placeholder(const std::string& bounds, const std::vector<std::size_t>& origins) {
    std::vector<std::string> names;
    names.reserve(origins.size());
    for (const auto origin : origins) {
        names.push_back(std::to_string(origin));
    }
    return "cactus::runtime::entt_backend::shared_static_store<" + std::string{kStaticStoreMark} +
           std::to_string(stable_hash(bounds)) + ":" + join_strings(names, ",") + "@>()";
}

void EnttSystemEmitter::assign_static_stores(std::string& code) {
    struct Placeholder {
        std::size_t at     = 0;
        std::size_t length = 0;
        std::string bounds;
        std::vector<std::string> origins;  // empty when unknown: then it may overlap any
    };
    std::vector<Placeholder> found;
    for (auto at = code.find(kStaticStoreMark); at != std::string::npos; at = code.find(kStaticStoreMark, at + 1)) {
        const auto start = at + kStaticStoreMark.size();
        const auto colon = code.find(':', start);
        const auto end   = code.find('@', colon);
        Placeholder placeholder{.at = at, .length = end + 1 - at, .bounds = code.substr(start, colon - start)};
        for (const auto part : std::views::split(std::string_view{code}.substr(colon + 1, end - colon - 1), ',')) {
            if (!part.empty()) {
                placeholder.origins.emplace_back(part.begin(), part.end());
            }
        }
        found.push_back(std::move(placeholder));
    }
    std::vector<std::size_t> group(found.size());
    std::ranges::iota(group, std::size_t{0});
    const auto root = [&](std::size_t index) {
        while (group[index] != index) {
            index = group[index];
        }
        return index;
    };
    for (std::size_t left = 0; left < found.size(); ++left) {
        for (std::size_t right = left + 1; right < found.size(); ++right) {
            const auto& a        = found[left];
            const auto& b        = found[right];
            const bool overlap   = a.origins.empty() || b.origins.empty() ||
                                 std::ranges::any_of(a.origins, [&](const std::string& origin) {
                                     return std::ranges::contains(b.origins, origin);
                                 });
            if (a.bounds == b.bounds && overlap) {
                group[root(right)] = root(left);
            }
        }
    }
    std::string named;
    named.reserve(code.size());
    std::size_t copied = 0;
    for (std::size_t index = 0; index < found.size(); ++index) {
        const auto& first = found[root(index)];
        named.append(code, copied, found[index].at - copied);
        named += std::to_string(stable_hash(first.bounds + ":" + join_strings(first.origins, ","))) + "ULL";
        copied = found[index].at + found[index].length;
    }
    named.append(code, copied);
    code = std::move(named);
}

// Rebuilds `index` from every current member when it was built for another
// world; the catch_up call that follows then reclassifies the marked entities.
static void emit_index_refresh(std::ostringstream& out,
                               const std::string& index,
                               const std::string& reset_args,
                               const std::string& watched,
                               const std::string& member_types) {
    out << "    if (" << index << ".tracker().stale(registry)) {\n";
    out << "        " << index << ".reset(registry" << reset_args << ");\n";
    out << "        cactus::runtime::entt_backend::watch_binding_traits<" << watched << ">(registry, " << index
        << ".tracker());\n";
    out << "        for (const auto cactus_member : registry.view<" << member_types << ">()) {\n";
    out << "            " << index << ".tracker().mark(cactus_member);\n";
    out << "        }\n";
    out << "    }\n";
}

static void emit_binding_index_sync(std::ostringstream& out,
                                    const IndexedFoldPlan& plan,
                                    const ExprNode& predicate,
                                    const PairBindingCodegen& binding,
                                    const PairCodegenScope& pair_codegen_scope,
                                    const DecoratedProgram& program) {
    const std::string ns    = "cactus::runtime::entt_backend::";
    const auto& name        = binding.scope.binding_name;
    const auto index        = "cactus_index_" + name;
    const auto member_types = join_strings(binding.cpp_types, ", ");
    PairCodegenScope scope  = pair_codegen_scope;
    for (auto& entry : scope.bindings) {
        entry.collider_shapes_cached = false;
    }
    const auto proxy = sap_proxy_expression(*plan.indexed_shape, SpatialJoinDimension::Volume3D, name, "0", predicate,
                                            scope, program);
    // What decides a member's stored bounds; indexes that agree on it can share static members.
    std::ostringstream bounds;
    bounds << "        cactus_found.is_static = !(" << member_may_change(plan.writers, name, program) << ");\n";
    bounds << "        const auto cactus_proxy = " << proxy.code << ";\n";
    if (proxy.optional) {
        bounds << "        if (cactus_proxy.has_value()) {\n";
        bounds << "            cactus_found.bounds = std::pair{cactus_proxy->min, cactus_proxy->max};\n";
        bounds << "        }\n";
    } else {
        bounds << "        cactus_found.bounds = std::pair{cactus_proxy.min, cactus_proxy.max};\n";
    }
    const auto watched   = cpp_trait_list(plan.watched, program);
    const auto origins   = member_origins(program, plan.required);
    const auto cell_size = spatial_cell_size(plan, origins, program);
    const auto store     = static_store_placeholder(
        program.share_static_stores ? bounds.str() : bounds.str() + member_types + watched + cell_size, origins);
    std::ostringstream sync;
    emit_index_refresh(sync, index, ", " + store + ", " + cell_size, watched, member_types);
    sync << "    " << index << ".catch_up(registry, [&](entt::entity " << name << ") {\n";
    sync << "        " << ns << "SpatialBindingIndex3D::Classified cactus_found;\n";
    sync << "        if (!registry.all_of<" << member_types << ">(" << name << ")) {\n";
    sync << "            return cactus_found;\n";
    sync << "        }\n";
    sync << "        cactus_found.member    = true;\n";
    sync << bounds.str();
    sync << "        return cactus_found;\n";
    sync << "    });\n";
    emit_shared_index(out, ns + "SpatialBindingIndex3D", index, sync.str());
}

// Emits a walk over every group entity bound by `group_loop` (which opens the
// loop at `indent`), calling `call` for each member whose bounds overlap the
// group's, oldest first: static members from the index, dynamic members bounded
// afresh this pass.
static void emit_indexed_candidates(std::ostringstream& out,
                                    const IndexedFoldPlan& plan,
                                    const ExprNode& predicate,
                                    const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                    const PairCodegenScope& scope,
                                    const DecoratedProgram& program,
                                    const std::string& indent,
                                    const std::string& group_loop,
                                    const std::string& call) {
    const std::string index_type = "cactus::runtime::entt_backend::SpatialBindingIndex3D";
    const auto& member           = pair_binding_codegens[plan.indexed].scope.binding_name;
    const auto& group            = pair_binding_codegens[1 - plan.indexed].scope.binding_name;
    const auto index             = "cactus_index_" + member;
    const auto member_proxy =
        sap_proxy_expression(*plan.indexed_shape, SpatialJoinDimension::Volume3D, member, "0", predicate, scope, program);
    const auto query_proxy =
        sap_proxy_expression(*plan.query_shape, SpatialJoinDimension::Volume3D, group, "0", predicate, scope, program);
    const auto bounds_of = [](const ProxyExpression& proxy, const std::string& value) {
        return proxy.optional ? value + "->" : value + ".";
    };
    const auto body = indent + "    ";
    out << indent << "std::vector<" << index_type << "::BoundedMember> cactus_dynamic;\n";
    out << indent << index << ".bound_dynamic([&](entt::entity " << member << ") -> std::optional<" << index_type
        << "::Bounds> {\n";
    out << body << "const auto cactus_proxy = " << member_proxy.code << ";\n";
    if (member_proxy.optional) {
        out << body << "if (!cactus_proxy.has_value()) { return std::nullopt; }\n";
    }
    out << body << "return std::pair{" << bounds_of(member_proxy, "cactus_proxy") << "min, "
        << bounds_of(member_proxy, "cactus_proxy") << "max};\n";
    out << indent << "}, cactus_dynamic);\n";
    out << indent << "std::vector<cactus::runtime::entt_backend::OrderedEntity> cactus_candidates;\n";
    out << group_loop;
    out << body << "const auto cactus_query = " << query_proxy.code << ";\n";
    if (query_proxy.optional) {
        out << body << "if (!cactus_query.has_value()) { continue; }\n";
    }
    out << body << index << ".candidates(registry, " << bounds_of(query_proxy, "cactus_query") << "min, "
        << bounds_of(query_proxy, "cactus_query") << "max, cactus_dynamic, cactus_candidates);\n";
    out << body << "for (const auto& [cactus_ordinal, " << member << "] : cactus_candidates) {\n";
    out << body << "    " << call << ";\n";
    out << body << "}\n";
    out << indent << "}\n";
}

static void emit_indexed_fold_dispatch(std::ostringstream& out,
                                       const IndexedFoldPlan& plan,
                                       const ExprNode& predicate,
                                       bool gated,
                                       const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                       const PairCodegenScope& fold_scope,
                                       const DecoratedProgram& program) {
    const auto& member = pair_binding_codegens[plan.indexed].scope.binding_name;
    const auto& group  = pair_binding_codegens[1 - plan.indexed].scope.binding_name;
    if (gated) {
        out << "    if (cactus_active) {\n";
    }
    const std::string group_loop = "    for (std::size_t cactus_i = 0; cactus_i < " + group +
                                   "_snapshot.size(); ++cactus_i) {\n" +
                                   "        if (cactus_group_live[cactus_i] == 0) { continue; }\n" +
                                   "        const auto " + group + " = " + group + "_snapshot[cactus_i];\n";
    const auto call = "cactus_fold(" + (plan.indexed == 0 ? member + ", " + group : group + ", " + member) + ")";
    emit_indexed_candidates(out, plan, predicate, pair_binding_codegens, fold_scope, program, "    ", group_loop, call);
    if (gated) {
        out << "    }\n";
    }
}

// Invokes `dispatch_name` for every candidate tuple of an indexed plain pair
// pass, in (left, right) snapshot order like the broad-phase activation. Like
// it, every candidate is found, and the indexed binding's own filter applied,
// before the first tuple runs.
static void emit_indexed_pair_activation(std::ostringstream& out,
                                         const IndexedFoldPlan& plan,
                                         const ExprNode& predicate,
                                         const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                         const PairCodegenScope& pair_codegen_scope,
                                         const DecoratedProgram& program,
                                         const std::string& member_keep,
                                         const std::string& dispatch_name) {
    const auto& member = pair_binding_codegens[plan.indexed].scope.binding_name;
    const auto& group  = pair_binding_codegens[1 - plan.indexed].scope.binding_name;
    out << "        {\n";
    emit_binding_index_sync(out, plan, predicate, pair_binding_codegens[plan.indexed], pair_codegen_scope, program);
    out << "        std::vector<std::pair<entt::entity, entt::entity>> cactus_indexed_tuples;\n";
    const auto collect = "cactus_indexed_tuples.emplace_back(" + group + ", " + member + ")";
    emit_indexed_candidates(out, plan, predicate, pair_binding_codegens, pair_codegen_scope, program, "        ",
                            "        for (const auto " + group + " : " + group + "_snapshot) {\n",
                            member_keep.empty() ? collect : "if (" + member_keep + "(" + member + ")) { " + collect + "; }");
    out << "        for (const auto& [cactus_left, cactus_right] : cactus_indexed_tuples) {\n";
    out << "            " << dispatch_name << "(cactus_left, cactus_right);\n";
    out << "        }\n";
    out << "        }\n";
}

// Emits the indexed activation when the pass has an index plan.
static bool emit_indexed_pair_activation_if_planned(std::ostringstream& out,
                                                    const RuleNode& sys,
                                                    const HandlerContract* contract,
                                                    const ExprNode& predicate,
                                                    const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                                    const PairCodegenScope& pair_codegen_scope,
                                                    const DecoratedProgram& program,
                                                    const std::array<std::string, 2>& keeps,
                                                    const std::string& dispatch_name) {
    const auto plan = indexed_pair_plan(sys, contract, program);
    if (!plan.has_value()) {
        return false;
    }
    emit_indexed_pair_activation(out, *plan, predicate, pair_binding_codegens, pair_codegen_scope, program,
                                 keeps.at(plan->indexed), dispatch_name);
    return true;
}

// ── Equality-indexed folds ───────────────────────────────────────────────────
// A reduced pair pass with an equality join keeps the non-group members in a
// DenseKeyIndex, or a FlatKeyIndex when a key part can only be hashed, keyed by
// values they never change. Each group entity folds exactly the members with
// its key, oldest first; the fold still checks
// every predicate. An identity key needs no index: the registry already finds
// the one member a group names.

struct EqualityKeyPart {
    const ExprNode* member     = nullptr;
    const ExprNode* query      = nullptr;
    const EqualityJoinKey* key = nullptr;
};

// A dense index's key: the parts addressed within a block, in key order, and
// what names each member's block: an entity part, or the container field the
// ranged parts read through. Neither means one block for every member.
struct DenseKeyShape {
    std::vector<std::size_t> values;
    std::optional<std::size_t> block_part;
    const EqualityJoinKey* ranged = nullptr;
};

struct EqualityFoldPlan {
    std::size_t indexed = 0;
    std::vector<EqualityKeyPart> parts;
    const ExprNode* identity = nullptr;
    std::optional<DenseKeyShape> dense;
};

// The dense layout of a key whose parts all address known ranges with at most
// one block entity; nullopt when the key must be hashed.
static std::optional<DenseKeyShape> dense_key_shape(const std::vector<EqualityKeyPart>& parts,
                                                    const DecoratedProgram& program) {
    if (program.force_hashed_keys) {
        return std::nullopt;
    }
    DenseKeyShape shape;
    std::vector<std::size_t> entities;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto& key = *parts[index].key;
        switch (key.layout) {
            case EqualityKeyLayout::Hashed:
                return std::nullopt;
            case EqualityKeyLayout::Entity:
                entities.push_back(index);
                break;
            case EqualityKeyLayout::Ranged:
                if (shape.ranged != nullptr &&
                    (shape.ranged->trait != key.trait || shape.ranged->container != key.container)) {
                    return std::nullopt;
                }
                shape.ranged = &key;
                shape.values.push_back(index);
                break;
            case EqualityKeyLayout::Direct:
                shape.values.push_back(index);
                break;
        }
    }
    for (const auto index : entities) {
        const auto& key            = *parts[index].key;
        const bool names_container = shape.ranged != nullptr && key.trait == shape.ranged->trait &&
                                     key.field == shape.ranged->container;
        if (shape.block_part.has_value() || (shape.ranged != nullptr && !names_container)) {
            return std::nullopt;
        }
        shape.block_part = index;
    }
    return shape;
}

static std::optional<EqualityFoldPlan> equality_fold_plan(const RuleNode& sys,
                                                          const HandlerContract* contract,
                                                          const DecoratedProgram& program) {
    if (!program.index_pair_joins || contract == nullptr || !contract->equality_join.has_value() ||
        !sys.where_clause.has_value()) {
        return std::nullopt;
    }
    EqualityFoldPlan plan;
    plan.indexed = contract->equality_join->indexed_binding;
    for (const auto& key : contract->equality_join->keys) {
        const auto& root = *sys.where_clause->predicates.at(key.predicate_index);
        plan.parts.push_back(EqualityKeyPart{
            .member = resolve_expr_path(root, key.member), .query = resolve_expr_path(root, key.query), .key = &key});
    }
    if (std::ranges::any_of(plan.parts, [](const EqualityKeyPart& part) {
            return part.member == nullptr || part.query == nullptr;
        })) {
        return std::nullopt;
    }
    const auto& indexed_name = sys.pairs->bindings.at(plan.indexed).name;
    const auto identity      = std::ranges::find_if(plan.parts, [&](const EqualityKeyPart& part) {
        const auto* ident = std::get_if<IdentExpr>(&part.member->expr);
        return ident != nullptr && ident->name == indexed_name;
    });
    if (identity != plan.parts.end()) {
        plan.identity = identity->query;
        return plan;
    }
    plan.dense = dense_key_shape(plan.parts, program);
    return plan;
}

static std::string equality_index_type(const EqualityFoldPlan& plan) {
    return plan.dense.has_value()
               ? "cactus::runtime::entt_backend::DenseKeyIndex<" + std::to_string(plan.dense->values.size()) + ">"
               : "cactus::runtime::entt_backend::FlatKeyIndex<" + std::to_string(plan.parts.size()) + ">";
}

static std::string key_side(const EqualityKeyPart& part,
                            bool member_side,
                            const PairCodegenScope& scope,
                            const DecoratedProgram& program) {
    return rewrite_expr(member_side ? *part.member : *part.query, {}, program, {}, {}, &scope);
}

// The comma-separated key values: dense parts, or every part hashed.
static std::string equality_key_values(const EqualityFoldPlan& plan,
                                       bool member_side,
                                       const PairCodegenScope& scope,
                                       const DecoratedProgram& program) {
    std::vector<std::string> values;
    if (plan.dense.has_value()) {
        for (const auto index : plan.dense->values) {
            values.push_back("cactus::runtime::entt_backend::dense_key_part(" +
                             key_side(plan.parts[index], member_side, scope, program) + ")");
        }
    } else {
        for (const auto& part : plan.parts) {
            values.push_back("cactus::runtime::entt_backend::equality_key_part(" +
                             key_side(part, member_side, scope, program) + ")");
        }
    }
    return join_strings(values, ", ");
}

static std::string dense_key(const EqualityFoldPlan& plan, const std::string& block, const std::string& values) {
    return equality_index_type(plan) + "::Key{.block = " + block + ", .values = {" + values + "}}";
}

// A member's key; `member` names the member entity.
static std::string equality_member_key(const EqualityFoldPlan& plan,
                                       const std::string& member,
                                       const PairCodegenScope& scope,
                                       const DecoratedProgram& program) {
    const auto values = equality_key_values(plan, true, scope, program);
    if (!plan.dense.has_value()) {
        return equality_index_type(plan) + "::Key{" + values + "}";
    }
    std::string block = "entt::null";
    if (plan.dense->block_part.has_value()) {
        block = key_side(plan.parts[*plan.dense->block_part], true, scope, program);
    } else if (const auto* ranged = plan.dense->ranged; ranged != nullptr && ranged->trait.has_value()) {
        block = "registry.get<const " + EnttCodegenUtils::trait_cpp_name(*ranged->trait, ranged->trait->local_name, program) +
                ">(" + member + ")." + cpp_escaped(ranged->container);
    }
    return dense_key(plan, block, values);
}

// Replaces `out` with the members whose key equals the group's.
static std::string equality_find(const EqualityFoldPlan& plan,
                                 const std::string& index,
                                 const std::string& out,
                                 const PairCodegenScope& scope,
                                 const DecoratedProgram& program) {
    const auto values = equality_key_values(plan, false, scope, program);
    if (!plan.dense.has_value()) {
        return index + ".find(registry, " + equality_index_type(plan) + "::Key{" + values + "}, " + out + ")";
    }
    if (plan.dense->ranged != nullptr && !plan.dense->block_part.has_value()) {
        return index + ".find_any_block(registry, {" + values + "}, " + out + ")";
    }
    const auto block = plan.dense->block_part.has_value()
                           ? key_side(plan.parts[*plan.dense->block_part], false, scope, program)
                           : std::string{"entt::null"};
    return index + ".find(registry, " + dense_key(plan, block, values) + ", " + out + ")";
}

// The [lo, hi) of each dense part, read through `block` for ranged parts.
static ContainerExtents dense_extents(const EqualityFoldPlan& plan,
                                      const DenseKeyShape& dense,
                                      const std::string& block,
                                      const std::string& indent,
                                      const DecoratedProgram& program) {
    std::vector<const ResolvedFieldRange*> ranges;
    std::vector<std::optional<std::pair<std::string, std::string>>> fixed;
    for (const auto index : dense.values) {
        const auto& key   = *plan.parts[index].key;
        const auto* trait =
            key.trait.has_value() ? EnttCodegenUtils::find_trait(program, make_canonical_id(*key.trait)) : nullptr;
        const auto fields = trait == nullptr ? std::span<const ResolvedField>{} : std::span{trait->fields};
        const auto field  = std::ranges::find(fields, key.field, &ResolvedField::name);
        if (field == fields.end()) {
            throw CodegenError(plan.parts[index].member->location, "unknown equality key field '" + key.field + "'");
        }
        if (field->type.kind == TypeKind::Bool) {
            fixed.emplace_back(std::pair<std::string, std::string>{"0", "2"});
        } else if (field->type.kind == TypeKind::Enum) {
            const auto* enum_decl = EnttCodegenUtils::find_enum(
                program, field->type.symbol_id.has_value() ? field->type.symbol_id->local_name : field->type.name);
            if (enum_decl == nullptr) {
                throw CodegenError(plan.parts[index].member->location, "unknown enum '" + field->type.name + "'");
            }
            fixed.emplace_back(std::pair<std::string, std::string>{"0", std::to_string(enum_decl->variants.size())});
        } else {
            ranges.push_back(&*field->range);
            fixed.emplace_back(std::nullopt);
        }
    }
    auto extents = emit_container_extents(ranges, block, "return std::nullopt;", indent, program);
    std::vector<std::pair<std::string, std::string>> bounds;
    bounds.reserve(fixed.size());
    auto ranged = extents.bounds.begin();
    for (auto& entry : fixed) {
        bounds.push_back(entry.has_value() ? *entry : *ranged++);
    }
    extents.bounds = std::move(bounds);
    return extents;
}

static void emit_equality_index_sync(std::ostringstream& out,
                                     const EqualityFoldPlan& plan,
                                     const PairBindingCodegen& binding,
                                     const PairCodegenScope& pair_codegen_scope,
                                     const DecoratedProgram& program) {
    const auto& name        = binding.scope.binding_name;
    const auto index        = "cactus_index_" + name;
    const auto index_type   = equality_index_type(plan);
    const auto member_types = join_strings(binding.cpp_types, ", ");
    std::ostringstream sync;
    emit_index_refresh(sync, index, "", member_types, member_types);
    sync << "    " << index << ".catch_up(\n";
    sync << "        registry,\n";
    sync << "        [&](entt::entity " << name << ") -> std::optional<" << index_type << "::Key> {\n";
    sync << "            if (!registry.all_of<" << member_types << ">(" << name << ")) {\n";
    sync << "                return std::nullopt;\n";
    sync << "            }\n";
    sync << "            return " << equality_member_key(plan, name, pair_codegen_scope, program) << ";\n";
    if (!plan.dense.has_value()) {
        sync << "        });\n";
        emit_shared_index(out, index_type, index, sync.str());
        return;
    }
    const auto extents = dense_extents(plan, *plan.dense, "cactus_block", "            ", program);
    std::vector<std::string> bounds;
    bounds.reserve(extents.bounds.size());
    for (const auto& [lo, hi] : extents.bounds) {
        std::string bound = "std::pair<std::int64_t, std::int64_t>{";
        bound += lo;
        bound += ", ";
        bound += hi;
        bound += "}";
        bounds.push_back(std::move(bound));
    }
    sync << "        },\n";
    sync << "        [&]([[maybe_unused]] entt::entity cactus_block) -> std::optional<" << index_type << "::Extents> {\n";
    sync << extents.lookups;
    sync << "            return " << index_type << "::Extents{" << join_strings(bounds, ", ") << "};\n";
    sync << "        });\n";
    emit_shared_index(out, index_type, index, sync.str());
}

static void emit_equality_fold_dispatch(std::ostringstream& out,
                                        const EqualityFoldPlan& plan,
                                        bool gated,
                                        const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                        const PairCodegenScope& fold_scope,
                                        const DecoratedProgram& program) {
    const auto& member = pair_binding_codegens[plan.indexed].scope.binding_name;
    const auto& group  = pair_binding_codegens[1 - plan.indexed].scope.binding_name;
    if (gated) {
        out << "    if (cactus_active) {\n";
    }
    if (plan.identity == nullptr) {
        out << "    std::vector<entt::entity> cactus_matches;\n";
    }
    out << "    for (std::size_t cactus_i = 0; cactus_i < " << group << "_snapshot.size(); ++cactus_i) {\n";
    out << "        if (cactus_group_live[cactus_i] == 0) { continue; }\n";
    out << "        const auto " << group << " = " << group << "_snapshot[cactus_i];\n";
    if (plan.identity != nullptr) {
        out << "        const entt::entity " << member << " = "
            << rewrite_expr(*plan.identity, {}, program, {}, {}, &fold_scope) << ";\n";
        out << "        if (cactus::runtime::entt_backend::named_alive<"
            << join_strings(pair_binding_codegens[plan.indexed].cpp_types, ", ") << ">(registry, " << member << ")) {\n";
    } else {
        out << "        " << equality_find(plan, "cactus_index_" + member, "cactus_matches", fold_scope, program)
            << ";\n";
        out << "        for (const auto " << member << " : cactus_matches) {\n";
    }
    out << "            cactus_fold(" << (plan.indexed == 0 ? member + ", " + group : group + ", " + member) << ");\n";
    out << "        }\n";
    out << "    }\n";
    if (gated) {
        out << "    }\n";
    }
}

// The `where:` predicates the fold checks per row: not group filters, not the
// gate, and not the binding filters that already ran.
static std::vector<const ExprNode*> fold_row_predicates(const ReduceClause& reduce,
                                                        const std::vector<std::unique_ptr<ExprNode>>& predicates,
                                                        const HandlerContract* contract,
                                                        const std::array<bool, 2>& filtered) {
    std::vector<const ExprNode*> row_predicates;
    for (std::size_t index = 0; index < predicates.size(); ++index) {
        const auto stage = stage_at(staging_of(contract).where, index);
        const bool filtered_out =
            (stage == ClauseStage::BindingLeft && filtered[0]) || (stage == ClauseStage::BindingRight && filtered[1]);
        if (!std::ranges::contains(reduce.group_predicates, index) && stage != ClauseStage::Activation &&
            !filtered_out) {
            row_predicates.push_back(predicates[index].get());
        }
    }
    return row_predicates;
}

// A reduced pair rule: snapshot both bindings, filter every binding but the
// group binding, build the groups, fold every accepted row in snapshot order,
// then dispatch per group. A false activation gate folds no row.
static void emit_reduced_pair_handler_body(std::ostringstream& out,
                                           const RuleNode& sys,
                                           const EventHandlerNode& handler,
                                           const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                           const PairCodegenScope& pair_codegen_scope,
                                           const DecoratedProgram& program,
                                           const HandlerContract* contract) {
    const auto& reduce  = *sys.reduce;
    const bool keeps    = sys.keep.has_value();
    const bool witness  = keeps && sys.keep->condition == nullptr;
    const auto index    = indexed_fold_plan(sys, contract, program);
    const auto equality = equality_fold_plan(sys, contract, program);
    std::optional<std::string> indexed;
    if (index.has_value()) {
        indexed = pair_binding_codegens[index->indexed].scope.binding_name;
    } else if (equality.has_value()) {
        indexed = pair_binding_codegens[equality->indexed].scope.binding_name;
    }
    emit_reduced_pair_snapshots(out, reduce, pair_binding_codegens, keeps, indexed);
    if (index.has_value()) {
        emit_binding_index_sync(out, *index, *spatial_join_root(sys, contract), pair_binding_codegens[index->indexed],
                                pair_codegen_scope, program);
    } else if (equality.has_value() && equality->identity == nullptr) {
        emit_equality_index_sync(out, *equality, pair_binding_codegens[equality->indexed], pair_codegen_scope, program);
    }
    const auto filtered =
        emit_reduced_binding_filters(out, sys, reduce, contract, pair_codegen_scope, program, indexed);
    const auto active   = activation_condition(sys, contract, program);
    if (!active.empty()) {
        out << "    const bool cactus_active = " << active << ";\n";
    }
    emit_reduce_types(out, reduce, witness);
    auto fold_scope = pair_codegen_scope;
    emit_fold_tables(out, sys, reduce, contract, fold_scope, indexed);
    const auto member_inputs = emit_member_inputs(out, reduce, contract, fold_scope, program, indexed);
    const auto rewrite       = [&](const ExprNode& expr) {
        const auto found = member_inputs.find(&expr);
        return found != member_inputs.end() ? found->second : rewrite_expr(expr, {}, program, {}, {}, &fold_scope);
    };

    static const std::vector<std::unique_ptr<ExprNode>> no_predicates;
    const auto& predicates = sys.where_clause.has_value() ? sys.where_clause->predicates : no_predicates;
    std::optional<PairCodegenScope> group_scope;
    if (reduce.per_binding.has_value()) {
        group_scope.emplace();
        group_scope->bindings.push_back(*pair_codegen_scope.find(*reduce.per_binding));
        group_scope->bindings.front().writable = true;
        emit_group_table(out, reduce, predicates, rewrite);
    } else {
        out << "    cactus_reduce_acc cactus_total{};\n";
    }
    emit_fold_lambda(out, reduce, witness, fold_scope, fold_row_predicates(reduce, predicates, contract, filtered), rewrite);
    if (index.has_value()) {
        emit_indexed_fold_dispatch(out, *index, *spatial_join_root(sys, contract), !active.empty(),
                                   pair_binding_codegens, fold_scope, program);
    } else if (equality.has_value()) {
        emit_equality_fold_dispatch(out, *equality, !active.empty(), pair_binding_codegens, fold_scope, program);
    } else {
        emit_fold_dispatch(out, sys, contract, !active.empty(), pair_binding_codegens, fold_scope, program);
    }

    if (reduce.per_binding.has_value()) {
        out << "    std::vector<cactus_reduce_row> cactus_rows;\n";
        out << "    for (std::size_t cactus_i = 0; cactus_i < cactus_groups.size(); ++cactus_i) {\n";
        out << "        if (cactus_group_live[cactus_i] == 0) { continue; }\n";
        out << "        const auto& cactus_acc = cactus_groups[cactus_i];\n";
        emit_reduce_row(out, reduce, *reduce.per_binding + "_snapshot[cactus_i]", program, "        ", witness);
        out << "    }\n";
    } else {
        emit_total_reduce_rows(out, reduce, program);
    }
    if (keeps && group_scope.has_value()) {
        emit_keep_commands(out, *sys.keep, reduce, *group_scope, program);
    }
    if (handler.body.empty()) {
        return;
    }
    emit_reduced_dispatch(out, sys, handler, group_scope.has_value() ? &*group_scope : nullptr, program);
}

// A reduced unary rule is always global: fold the filter domain in creation
// order into one row, then run the handler once. A false activation gate
// folds no row.
static void emit_reduced_unary_handler_body(std::ostringstream& out,
                                            const RuleNode& sys,
                                            const EventHandlerNode& handler,
                                            const HandlerDomainCodegen& domain,
                                            const DecoratedProgram& program,
                                            const HandlerContract* contract) {
    const auto& reduce = *sys.reduce;
    emit_reduce_types(out, reduce, false);
    out << "    cactus_reduce_acc cactus_total{};\n";
    const auto active = activation_condition(sys, contract, program);
    out << (active.empty() ? "    {\n" : "    if (" + active + ") {\n");
    emit_view_declaration(out, domain.filter_cpp_types, domain.exclude_cpp_types, 2);
    out << "        for (auto entity : cactus::runtime::entt_backend::creation_ordered(registry, view)) {\n";
    emit_component_bindings_from_entity(out, domain.filter_bindings_list, "entity", 3, program);
    emit_filter_alias_bindings(out, domain.filter, program, 3);
    out << "            auto& cactus_acc = cactus_total;\n";
    const auto rewrite = [&](const ExprNode& expr) {
        return rewrite_expr(expr, domain.filter_traits, program, {}, domain.filter_cpp_overrides);
    };
    for (const auto* predicate : where_at(sys, contract, ClauseStage::Row)) {
        out << "            if (" << rejects(rewrite(*predicate)) << ") { continue; }\n";
    }
    emit_reducer_folds(out, reduce, rewrite, "            ", false);
    out << "        }\n";
    out << "    }\n";
    emit_total_reduce_rows(out, reduce, program);
    emit_reduced_dispatch(out, sys, handler, nullptr, program);
}

// `keep T` on a unary rule: one pass over the filter domain in creation order
// keeps each entity that passes `where:`. A false activation gate keeps none.
static void emit_unary_keep_handler_body(std::ostringstream& out,
                                         const RuleNode& sys,
                                         const HandlerDomainCodegen& domain,
                                         const DecoratedProgram& program,
                                         const HandlerContract* contract) {
    const auto& keep = *sys.keep;
    out << "    std::vector<entt::entity> cactus_kept_entities;\n";
    const auto active = activation_condition(sys, contract, program);
    out << (active.empty() ? "    {\n" : "    if (" + active + ") {\n");
    emit_view_declaration(out, domain.filter_cpp_types, domain.exclude_cpp_types, 2);
    out << "        for (auto entity : cactus::runtime::entt_backend::creation_ordered(registry, view)) {\n";
    emit_component_bindings_from_entity(out, domain.filter_bindings_list, "entity", 3, program);
    emit_filter_alias_bindings(out, domain.filter, program, 3);
    const auto rewrite = [&](const ExprNode& expr) {
        return rewrite_expr(expr, domain.filter_traits, program, {}, domain.filter_cpp_overrides);
    };
    for (const auto* predicate : where_at(sys, contract, ClauseStage::Row)) {
        out << "            if (" << rejects(rewrite(*predicate)) << ") { continue; }\n";
    }
    out << "            cactus_kept_entities.push_back(entity);\n";
    emit_keep_add(out, keep, "entity", rewrite, program, "            ");
    out << "        }\n";
    out << "    }\n";
    out << "    const cactus::runtime::entt_backend::SnapshotPositions cactus_kept(cactus_kept_entities);\n";
    out << "    auto cactus_is_kept = [&](entt::entity cactus_carrier) {\n";
    out << "        return cactus_kept.find(cactus_carrier) != cactus::runtime::entt_backend::SnapshotPositions::kAbsent;\n";
    out << "    };\n";
    emit_keep_removals(out, keep, program);
}

// `keep R(a, b)`: one run stamps a row per tuple passing `where:`, then drops
// the rows it did not stamp, all at the activation's commit.
static void emit_relation_keep_handler_body(std::ostringstream& out,
                                            const RuleNode& sys,
                                            const EventHandlerNode& handler,
                                            const std::vector<PairBindingCodegen>& pair_binding_codegens,
                                            const PairCodegenScope& pair_codegen_scope,
                                            const DecoratedProgram& program,
                                            const HandlerContract* contract) {
    const auto& keep  = *sys.keep;
    const auto row    = EnttCodegenUtils::relation_cpp_name(*keep.resolved_target);
    const auto store  = EnttCodegenUtils::uncompacted_relation_store_cpp(*keep.resolved_target);
    const auto queue  = [&](const std::string& call) {
        out << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        out << "        cactus::runtime::entt_backend::StructuralCommand::Kind::Add,\n";
        out << "        [](entt::registry& registry) { " << store << "." << call << "; });\n";
    };
    queue("begin_run()");
    std::ostringstream body;
    body << "        " << row << " cactus_row_value{};\n";
    for (const auto& field : keep.fields) {
        body << "        cactus_row_value." << cpp_escaped(field.name) << " = "
             << rewrite_expr(*field.value, {}, program, {}, {}, &pair_codegen_scope) << ";\n";
    }
    const auto& from = keep.arguments[0].spelling;
    const auto& to   = keep.arguments[1].spelling;
    body << "        cactus::runtime::entt_backend::generated_queue_structural_command(\n";
    body << "            cactus::runtime::entt_backend::StructuralCommand::Kind::Add,\n";
    body << "            [" << from << ", " << to << ", cactus_row_value](entt::registry& registry) { " << store
         << ".keep(" << from << ", " << to << ", cactus_row_value); });\n";
    const auto tuple_body = body.str();
    emit_pair_handler_body(out, sys, handler, pair_binding_codegens, pair_codegen_scope, program, contract, &tuple_body);
    queue("drop_unstamped()");
}

// ── Relation row passes ─────────────────────────────────────

// Sort keys and limit of a pass over row tuples: `cactus_row_tuples` holds
// (entity, row) pairs, the entity null in a unary row domain.
static void emit_row_tuple_order_and_limit(std::ostringstream& out,
                                           const RuleNode& sys,
                                           const std::string& params,
                                           const std::string& args_a,
                                           const std::string& args_b,
                                           const PairCodegenScope& scope,
                                           const DecoratedProgram& program) {
    if (!sys.order_by.empty()) {
        for (std::size_t index = 0; index < sys.order_by.size(); ++index) {
            out << "    auto cactus_row_key" << index << " = [&](" << params << ") { return "
                << rewrite_expr(*sys.order_by[index].expression, {}, program, {}, {}, &scope) << "; };\n";
        }
        out << "    std::ranges::stable_sort(cactus_row_tuples, [&](const auto& cactus_a, const auto& cactus_b) {\n";
        for (std::size_t index = 0; index < sys.order_by.size(); ++index) {
            const auto key = "cactus_row_key" + std::to_string(index);
            const bool desc = sys.order_by[index].descending;
            out << "        if (const auto cactus_ka = " << key << "(" << args_a << "), cactus_kb = " << key << "("
                << args_b << "); cactus_ka != cactus_kb) {\n";
            out << "            return cactus_ka " << (desc ? ">" : "<") << " cactus_kb;\n";
            out << "        }\n";
        }
        out << "        return false;\n";
        out << "    });\n";
    }
    if (!sys.limit.has_value()) {
        return;
    }
    const auto& limit = *sys.limit;
    if (limit.parts.empty()) {
        out << "    const auto cactus_row_limit = static_cast<std::size_t>(std::max("
            << rewrite_expr(*limit.count, {}, program, {}, {}, &scope) << ", 0));\n";
        out << "    if (cactus_row_tuples.size() > cactus_row_limit) { cactus_row_tuples.resize(cactus_row_limit); }\n";
        return;
    }
    // A `per` part names the entity binding: the first tuples of each entity, up to its count.
    const auto counters = emit_limit_part_counters(out, limit.parts.front(), scope, program);
    out << "    std::erase_if(cactus_row_tuples, [&](const auto& cactus_tuple) {\n";
    emit_limit_part_slot(out, counters, "first", "cactus_row_slot");
    out << "        return cactus_row_slot.first++ >= cactus_row_slot.second;\n";
    out << "    });\n";
}

// The binding order of a row pass: a lone row, or an entity and a row.
struct RowPassShape {
    bool unary     = false;
    bool row_first = false;
    std::string entity_name;

    [[nodiscard]] std::string args(const std::string& entity, const std::string& row) const {
        if (unary) {
            return row;
        }
        return row_first ? row + ", " + entity : entity + ", " + row;
    }

    // Arguments of one buffered (entity, row pointer) tuple.
    [[nodiscard]] std::string tuple_args(const std::string& tuple) const {
        return args(tuple + ".first", "*" + tuple + ".second");
    }
};

// Buffers the tuples that pass `where:`: rows by end lookup under an end-key
// join with the entity binding outermost, else in binding-major order.
static void emit_row_tuple_collection(std::ostringstream& out,
                                      const RuleNode& sys,
                                      const RowPassShape& shape,
                                      const std::function<std::string(const ExprNode&)>& rewrite) {
    const auto collect = [&](const std::string& ind, const std::string& entity) {
        out << ind << "if (cactus_row_passes(" << shape.args(entity, "cactus_row")
            << ")) { cactus_row_tuples.emplace_back(" << entity << ", &cactus_row); }\n";
    };
    const auto& entity = shape.entity_name;
    if (shape.unary) {
        out << "    for (const auto& cactus_row : cactus_rows.rows()) {\n";
        collect("        ", "entt::null");
        out << "    }\n";
        return;
    }
    const auto join = find_row_end_join(sys);
    if (join.has_value() && !shape.row_first) {
        out << "    for (auto " << entity << " : " << entity << "_snapshot) {\n";
        out << "        cactus_rows.for_each_" << (join->from_end ? "from" : "to") << "(" << rewrite(*join->key)
            << ", [&](const cactus_row_entry& cactus_row) {\n";
        collect("            ", entity);
        out << "        });\n";
        out << "    }\n";
        return;
    }
    const auto rows_loop   = std::string("for (const auto& cactus_row : cactus_rows.rows())");
    const auto entity_loop = "for (auto " + entity + " : " + entity + "_snapshot)";
    out << "    " << (shape.row_first ? rows_loop : entity_loop) << " {\n";
    out << "        " << (shape.row_first ? entity_loop : rows_loop) << " {\n";
    collect("            ", entity);
    out << "        }\n";
    out << "    }\n";
}

// `each: R as r`, or a pair rule with one row binding. The rows are the
// store's live span, already in creation order.
static void emit_row_pass(std::ostringstream& out,
                          const RuleNode& sys,
                          const EventHandlerNode& handler,
                          const std::vector<PairBindingCodegen>& pair_binding_codegens,
                          const PairCodegenScope& scope,
                          const DecoratedProgram& program,
                          const HandlerContract* contract) {
    const bool unary         = pair_binding_codegens.size() == 1;
    const auto row_index     = unary || pair_binding_codegens[0].scope.row ? std::size_t{0} : std::size_t{1};
    const auto entity_index  = 1 - row_index;
    const auto& row_name     = pair_binding_codegens[row_index].scope.binding_name;
    const auto store         = EnttCodegenUtils::relation_store_cpp(*pair_binding_codegens[row_index].scope.relation);
    out << "    auto& cactus_rows = " << store << ";\n";
    out << "    using cactus_row_entry = std::remove_cvref_t<decltype(cactus_rows)>::Entry;\n";
    std::string entity_name;
    if (unary) {
        out << "    if (cactus_recipient.has_value()) { return; }\n";
    } else {
        entity_name = pair_binding_codegens[entity_index].scope.binding_name;
        emit_pair_binding_snapshot(out, pair_binding_codegens[entity_index], 1);
        (void)emit_binding_filter(out, sys, contract, entity_index, scope, program);
        out << "    if (cactus_recipient.has_value()) {\n";
        out << "        std::erase_if(" << entity_name << "_snapshot, [&](entt::entity cactus_member) { return "
               "cactus_member != *cactus_recipient; });\n";
        out << "    }\n";
    }
    // The handler's parameters, in binding order.
    const auto param = [&](std::size_t index) {
        return index == row_index ? "[[maybe_unused]] const cactus_row_entry& " + row_name
                                  : "[[maybe_unused]] entt::entity " + entity_name;
    };
    const auto params = unary ? param(0) : param(0) + ", " + param(1);
    std::vector<const ExprNode*> guards = where_at(sys, contract, ClauseStage::Row);
    std::ranges::copy(where_at(sys, contract, binding_stage(row_index)), std::back_inserter(guards));
    const auto rewrite = [&](const ExprNode& expr) { return rewrite_expr(expr, {}, program, {}, {}, &scope); };
    LexicalLocalBindings locals;
    for (const auto& binding : pair_binding_codegens) {
        locals.insert(binding.scope.binding_name);
    }
    LocalNumericKinds kinds;
    out << "    auto cactus_row_body = [&](" << params << ") {\n";
    out << rewrite_stmt_block(handler.body, 2, {}, program, {}, false, {}, &scope, locals, kinds);
    out << "    };\n";
    out << "    auto cactus_row_passes = [&](" << params << ") {\n";
    if (guards.empty()) {
        out << "        return true;\n";
    } else {
        emit_where_predicate_conjunction(out, guards, rewrite);
    }
    out << "    };\n";
    const RowPassShape shape{.unary = unary, .row_first = row_index == 0, .entity_name = entity_name};
    out << "    std::vector<std::pair<entt::entity, const cactus_row_entry*>> cactus_row_tuples;\n";
    emit_row_tuple_collection(out, sys, shape, rewrite);
    emit_row_tuple_order_and_limit(
        out, sys, params, shape.tuple_args("cactus_a"), shape.tuple_args("cactus_b"), scope, program);
    out << "    for (const auto& cactus_tuple : cactus_row_tuples) {\n";
    out << "        cactus_row_body(" << shape.tuple_args("cactus_tuple") << ");\n";
    out << "    }\n";
}

struct HandlerBodyInputs {
    const HandlerDomainCodegen* domain                          = nullptr;
    const std::vector<PairBindingCodegen>* pair_binding_codegens = nullptr;
    const PairCodegenScope* pair_codegen_scope                  = nullptr;
    const DecoratedProgram* program                             = nullptr;
    const HandlerContract* contract                             = nullptr;
    bool is_pair                                                = false;
    bool selectionless                                          = false;
    const std::string* trigger_binding                          = nullptr;
};

// One handler's body, by the rule's domain and clauses.
static void emit_handler_body(std::ostringstream& out,
                              const RuleNode& sys,
                              const EventHandlerNode& handler,
                              const HandlerBodyInputs& in) {
    const auto& domain   = *in.domain;
    const auto& program  = *in.program;
    const auto& pairs    = *in.pair_binding_codegens;
    const auto& scope    = *in.pair_codegen_scope;
    const auto* contract = in.contract;
    const bool row_pass =
        sys.rows.has_value() || std::ranges::any_of(pairs, [](const PairBindingCodegen& binding) { return binding.scope.row; });
    if (row_pass && !handler.has_lifecycle_trigger()) {
        emit_activation_gate(out, sys, contract, program);
        emit_row_pass(out, sys, handler, pairs, scope, program, contract);
        return;
    }
    if (sys.keep.has_value() && !sys.keep->arguments.empty()) {
        emit_relation_keep_handler_body(out, sys, handler, pairs, scope, program, contract);
        return;
    }
    if (sys.keep.has_value() && !sys.reduce.has_value()) {
        emit_unary_keep_handler_body(out, sys, domain, program, contract);
        return;
    }
    if (sys.reduce.has_value()) {
        if (sys.pairs.has_value()) {
            emit_reduced_pair_handler_body(out, sys, handler, pairs, scope, program, contract);
        } else {
            emit_reduced_unary_handler_body(out, sys, handler, domain, program, contract);
        }
        return;
    }
    emit_activation_gate(out, sys, contract, program);
    if (handler.has_lifecycle_trigger()) {
        emit_lifecycle_handler_body(out, sys, handler, *in.trigger_binding, program, contract);
    } else if (in.is_pair) {
        emit_pair_handler_body(out, sys, handler, pairs, scope, program, contract);
    } else if (in.selectionless) {
        emit_selectionless_handler_body(out, handler, domain.filter_traits, program, domain.filter_cpp_overrides);
    } else if (!domain.filter_traits.empty()) {
        emit_filtered_handler_body(out,
                                   sys,
                                   handler,
                                   domain,
                                   program,
                                   contract,
                                   contract != nullptr && contract->iteration_order == IterationOrder::Creation);
    } else {
        emit_fallback_handler_body(out,
                                   sys,
                                   handler,
                                   domain.filter_bindings_list,
                                   domain.filter_traits,
                                   program,
                                   domain.filter_cpp_overrides);
    }
}

std::string EnttSystemEmitter::emit_system(const RuleNode& sys, const DecoratedProgram& program) {
    std::ostringstream out;
    const auto domain = build_handler_domain(sys.filter, sys.exclude, program);

    const bool is_pair_system = sys.pairs.has_value();
    std::vector<PairBindingCodegen> pair_binding_codegens;
    PairCodegenScope pair_codegen_scope;
    if (is_pair_system) {
        // Semantic analysis already proved which binding — if any — a
        // `limit: 1 per <binding>` makes writable; codegen only consumes that
        // decision, it never re-derives it.
        for (const auto& binding : sys.pairs->bindings) {
            auto codegen            = build_pair_binding_codegen(binding, program);
            const auto* limit_part  = sys.limit.has_value() ? sys.limit->part_for(binding.name) : nullptr;
            codegen.scope.writable  = limit_part != nullptr && limit_part->provably_one;
            pair_codegen_scope.bindings.push_back(codegen.scope);
            pair_binding_codegens.push_back(std::move(codegen));
        }
    }
    if (sys.rows.has_value()) {
        PairBindingCodegen codegen;
        codegen.scope.binding_name = sys.rows->alias;
        codegen.scope.row          = true;
        codegen.scope.relation     = sys.rows->relation;
        pair_codegen_scope.bindings.push_back(codegen.scope);
        pair_binding_codegens.push_back(std::move(codegen));
    }

    for (const auto& handler : sys.handlers) {
        if (handler.resolved_trigger.has_value() && handler.resolved_trigger->kind == HandlerTriggerKind::RenderStage) {
            // dsl-render-passes: this handler's body is translated to GLSL by
            // render_pass_emitter.cpp, not emitted as an ordinary C++ system
            // function — it has no natural trigger C++ type (no phase
            // struct, no event struct) for this generic per-handler
            // emission to target.
            continue;
        }
        const auto* contract = graph_handler_contract(sys, handler, program);
        const bool is_pair = is_pair_system && contract != nullptr && contract->domain_kind == HandlerDomainKind::Pair;
        const bool selectionless   = !is_pair && contract != nullptr && contract->is_selectionless();
        const bool lifecycle       = handler.has_lifecycle_trigger();
        const auto trigger_binding = handler_trigger_binding(handler);
        const HandlerTriggerSpelling trigger{.spelling = lifecycle ? "" : handler.event_name,
                                             .binding  = trigger_binding};
        const HandlerTriggerScope trigger_scope(trigger);
        out << "void " << system_function_name(program.module_name, sys.name, "on").name << "(entt::registry& registry";
        out << ", const " << handler_trigger_cpp_type(handler, program) << "& " << trigger_binding;
        // Recipient is meaningful only for event-triggered handlers dispatched
        // through generated_dispatch_event; phase dispatch always calls with
        // the default (targeted-event-delivery: routing has no meaning for a
        // phase activation).
        out << ", std::optional<entt::entity> cactus_recipient = std::nullopt";
        out << ") {\n";
        out << "    (void)" << trigger_binding << ";\n";
        out << "    (void)cactus_recipient;\n";
        emit_named_requirements(out, contract, program);
        emit_when_gate(out, sys, program);
        HandlerBodyCodegen body_codegen{.contract           = contract,
                                        .structure_deferred = !program.execution_graph.phases.empty()};
        std::ostringstream body;
        {
            const ActiveScope<HandlerBodyCodegen> body_scope(body_codegen);
            emit_handler_body(body,
                              sys,
                              handler,
                              {.domain                = &domain,
                               .pair_binding_codegens = &pair_binding_codegens,
                               .pair_codegen_scope    = &pair_codegen_scope,
                               .program               = &program,
                               .contract              = contract,
                               .is_pair               = is_pair,
                               .selectionless         = selectionless,
                               .trigger_binding       = &trigger_binding});
        }
        if (body_codegen.uses_paint_order) {
            out << "    static thread_local cactus::runtime::entt_backend::HierarchyPaintOrder cactus_paint_order;\n";
            out << "    const auto cactus_paint_pass = cactus_paint_order.pass();\n";
        }
        out << body.str();
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
    const std::string transform_var = component_local(transform);
    const std::string data_var      = component_local(data);
    std::ostringstream out;
    out << "void " << system_function_name(program.module_name, sys.name, "tick").name
        << "(entt::registry& registry) {\n";
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
    for (const auto& spec : single_view_renderer_specs()) {
        if (spec.matches(sys)) {
            return emit_single_view_renderer_body(sys, program, spec);
        }
    }

    std::ostringstream out;
    const auto tick_signature =
        "void " + system_function_name(program.module_name, sys.name, "tick").name + "(entt::registry& registry) {\n";

    const bool flat_propagation = is_flat_transform_propagation(sys, program);
    if (flat_propagation || is_volume_transform_propagation(sys, program)) {
        out << tick_signature;
        emit_hierarchy_propagation(
            out, program, flat_propagation ? TransformDimension::Flat : TransformDimension::Volume);
        out << "    cactus::runtime::entt_backend::propagate_hierarchy(\n";
        out << "        registry, " << EnttCodegenUtils::hierarchy_index_expr(program) << ", PROPAGATION, "
            << EnttCodegenUtils::hierarchy_strategy_cpp(program) << ");\n";
        out << "}\n\n";

        return out.str();
    }

    if (is_voxel_volume_renderer(sys)) {
        const std::string ns       = "cactus::runtime::entt_backend::";
        const std::string volume   = EnttCodegenUtils::trait_cpp_name("std.voxel.volume.Volume", program);
        const std::string renderer = EnttCodegenUtils::trait_cpp_name("std.voxel.volume.VolumeRenderer", program);
        const std::string cell     = EnttCodegenUtils::trait_cpp_name("std.voxel.volume.Cell", program);
        const std::string cutaway  = EnttCodegenUtils::trait_cpp_name("std.voxel.volume.Cutaway", program);
        const std::string solid    = EnttCodegenUtils::trait_cpp_name("std.physics.volume.Solid", program);
        const std::string issue    = EnttCodegenUtils::enum_cpp_name("std.voxel.volume.VolumeIssue", program);
        out << tick_signature;
        out << "    " << ns << "render_voxel_volumes<" << volume << ", " << renderer << ", " << cell << ", " << cutaway
            << ", " << solid << ", " << issue << "::Valid>(registry);\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_follow_bodies(sys)) {
        const std::string lt   = EnttCodegenUtils::trait_cpp_name("std.transform.volume.LocalTransform", program);
        const std::string wt   = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
        const std::string body = EnttCodegenUtils::trait_cpp_name("std.physics.volume.CharacterBody", program);
        out << tick_signature;
        emit_hierarchy_propagation(out, program, TransformDimension::Volume);
        out << "    const auto bodies = cactus::runtime::entt_backend::creation_ordered(registry, registry.view<"
            << body << ">());\n";
        out << "    cactus::runtime::entt_backend::propagate_hierarchy_below(\n";
        out << "        " << EnttCodegenUtils::hierarchy_index_expr(program)
            << ", PROPAGATION, bodies, [&](entt::entity entity) { return registry.all_of<" << body
            << ">(entity); });\n";
        out << "}\n\n";

        return out.str();
    }

    if (is_shape_renderer(sys)) {
        const std::string wt         = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
        const std::string shape      = EnttCodegenUtils::trait_cpp_name("Shape", program);
        const std::string shape_type = EnttCodegenUtils::enum_cpp_name("ShapeType", program);
        const auto wt_var            = component_local(wt);
        const auto shape_var         = component_local(shape);
        out << tick_signature;
        out << "    auto view = registry.view<" << wt << ", " << shape << ">();\n";
        out << "    cactus::runtime::raylib::BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt_var << ", const " << shape << "& "
            << shape_var << ") {\n";
        out << "        (void)entity;\n";
        out << "        if (!" << shape_var << ".visible) {\n";
        out << "            return;\n";
        out << "        }\n";
        out << "        switch (" << shape_var << ".type) {\n";
        out << "            case " << shape_type << "::Rectangle:\n";
        out << "                cactus::runtime::entt_backend::draw_shape_rectangle(" << wt_var << ".position,\n";
        out << "                               " << shape_var << ".size,\n";
        out << "                               " << shape_var << ".origin,\n";
        out << "                               " << wt_var << ".rotation,\n";
        out << "                               " << shape_var << ".color);\n";
        out << "                break;\n";
        out << "            case " << shape_type << "::Circle:\n";
        out << "                cactus::runtime::entt_backend::draw_shape_circle(" << wt_var << ".position,\n";
        out << "                            " << shape_var << ".size.x,\n";
        out << "                            " << shape_var << ".origin,\n";
        out << "                            " << shape_var << ".color);\n";
        out << "                break;\n";
        out << "        }\n";
        out << "    });\n";
        out << "    cactus::runtime::raylib::EndMode2D();\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_sprite_animation(sys)) {
        const std::string anim = EnttCodegenUtils::trait_cpp_name("AnimatedSprite", program);
        const auto anim_var    = component_local(anim);
        out << tick_signature;
        out << "    auto view = registry.view<" << anim << ">();\n";
        out << "    view.each([&](entt::entity entity, " << anim << "& " << anim_var << ") {\n";
        out << "        (void)entity;\n";
        out << "        constexpr float kFixedDt = 1.0F / 60.0F;\n";
        out << "        cactus::runtime::entt_backend::advance_animated_sprite(" << anim_var << ".texture, " << anim_var
            << ".frame, " << anim_var << ".frame_count, " << anim_var << ".fps, " << anim_var
            << ".playing, kFixedDt);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_model_animation(sys)) {
        const std::string mr    = EnttCodegenUtils::trait_cpp_name("ModelRenderer", program);
        const std::string manim = EnttCodegenUtils::trait_cpp_name("ModelAnimator", program);
        const auto mr_var       = component_local(mr);
        const auto manim_var    = component_local(manim);
        out << tick_signature;
        // Playback emits events, so owners are visited oldest first, not in storage order.
        const bool emits = EnttCodegenUtils::find_trait(program, "ModelPlayback") != nullptr;
        out << "    auto view = registry.view<" << mr << ", " << manim << ">();\n";
        out << "    const auto tick_owner = [&](entt::entity entity, const " << mr << "& " << mr_var << ", " << manim
            << "& " << manim_var << ") {\n";
        out << "        (void)entity;\n";
        out << "        constexpr float kFixedDt = 1.0F / 60.0F;\n";

        // ModelPlayback is std.render.models' opt-in one-shot/cue/completion
        // companion (add-model-animation-lifecycle-events); entities without it
        // fall through to the legacy wrap-and-loop path below, unchanged. Guard
        // on find_trait (mirrors is_model_render's ModelAnimator guard above)
        // since a program can import std.render.models without ever declaring
        // ModelPlayback. All decision logic (baseline resolution, cue lookup,
        // interval advance, cap dedup) lives in tick_model_playback; this just
        // applies its result, the same division is_pointer_router below uses
        // for compute_pointer_frame_transitions.
        if (emits) {
            const std::string playback     = EnttCodegenUtils::trait_cpp_name("ModelPlayback", program);
            const std::string cue          = EnttCodegenUtils::trait_cpp_name("AnimationCue", program);
            const std::string parent_cpp   = EnttCodegenUtils::trait_cpp_name("Parent", program);
            const std::string mode_cpp     = EnttCodegenUtils::enum_cpp_name("PlaybackMode", program);
            const std::string marker_evt   = event_cpp_type("AnimationMarker", program);
            const std::string finished_evt = event_cpp_type("AnimationFinished", program);
            const std::string ns           = "cactus::runtime::entt_backend::";

            out << "        if (const auto* playback = registry.try_get<" << playback << ">(entity)) {\n";
            out << "            const auto result = " << ns << "tick_model_playback<" << parent_cpp << ", " << cue
                << ">(\n";
            out << "                registry, entity, " << mr_var << ".model, " << manim_var << ".clip, "
                << "playback->revision, " << manim_var << ".time,\n";
            out << "                kFixedDt * " << manim_var << ".speed, " << manim_var
                << ".playing, playback->mode == " << mode_cpp << "::Once);\n";
            out << "            " << manim_var << ".time    = result.new_time;\n";
            out << "            " << manim_var << ".playing = result.new_playing;\n";
            out << "            for (const auto& crossed_name : result.crossed_cue_names) {\n";
            out << "                " << ns << "generated_emit_targeted_event(\n";
            out << "                    " << marker_evt << "{.clip = " << manim_var
                << ".clip, .name = crossed_name}, entity);\n";
            out << "            }\n";
            out << "            if (result.completed) {\n";
            out << "                " << ns << "generated_emit_targeted_event(\n";
            out << "                    " << finished_evt << "{.clip = " << manim_var << ".clip}, entity);\n";
            out << "            }\n";
            out << "            return;\n";
            out << "        }\n";
        }

        out << "        if (!" << manim_var << ".playing) {\n";
        out << "            return;\n";
        out << "        }\n";
        out << "        " << manim_var << ".time += kFixedDt * " << manim_var << ".speed;\n";
        out << "        const float duration = cactus::runtime::entt_backend::model_animation_duration(" << mr_var
            << ".model, " << manim_var << ".clip);\n";
        out << "        if (duration > 0.0F) {\n";
        out << "            " << manim_var << ".time = std::fmod(" << manim_var << ".time, duration);\n";
        out << "            if (" << manim_var << ".time < 0.0F) {\n";
        out << "                " << manim_var << ".time += duration;\n";
        out << "            }\n";
        out << "        }\n";
        out << "    };\n";
        if (emits) {
            out << "    for (const auto entity : cactus::runtime::entt_backend::creation_ordered(registry, view)) {\n";
            out << "        tick_owner(entity, view.get<" << mr << ">(entity), view.get<" << manim << ">(entity));\n";
            out << "    }\n";
        } else {
            out << "    view.each(tick_owner);\n";
        }
        out << "}\n\n";
        return out.str();
    }

    if (is_model_render(sys)) {
        const std::string wt    = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
        const std::string mr    = EnttCodegenUtils::trait_cpp_name("ModelRenderer", program);
        const std::string manim = EnttCodegenUtils::trait_cpp_name("ModelAnimator", program);
        const auto wt_var       = component_local(wt);
        const auto mr_var       = component_local(mr);
        out << tick_signature;
        out << "    auto view = registry.view<" << wt << ", " << mr << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt_var << ", const " << mr << "& "
            << mr_var << ") {\n";
        out << "        (void)entity;\n";
        if (EnttCodegenUtils::find_trait(program, "ModelAnimator") != nullptr) {
            out << "        if (const auto* animator = registry.try_get<" << manim << ">(entity)) {\n";
            out << "            cactus::runtime::entt_backend::submit_model(" << wt_var << ".position, " << wt_var
                << ".rotation, " << wt_var << ".scale, " << mr_var << ".model, " << mr_var << ".visible, " << mr_var
                << ".cast_shadow, animator->clip, animator->time, " << mr_var << ".color);\n";
            out << "            return;\n";
            out << "        }\n";
        }
        out << "        cactus::runtime::entt_backend::submit_model(" << wt_var << ".position, " << wt_var
            << ".rotation, " << wt_var << ".scale, " << mr_var << ".model, " << mr_var << ".visible, " << mr_var
            << ".cast_shadow, " << mr_var << ".color);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_directional_light_render(sys)) {
        const std::string dl = EnttCodegenUtils::trait_cpp_name("DirectionalLight", program);
        const auto dl_var    = component_local(dl);
        out << tick_signature;
        out << "    auto view = registry.view<" << dl << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << dl << "& " << dl_var << ") {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::register_directional_light(" << dl_var << ".direction, "
            << dl_var << ".color, " << dl_var << ".intensity, " << dl_var << ".enabled);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_any_text_renderer_2d(sys)) {
        const std::string wt = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
        const std::string tl = EnttCodegenUtils::trait_cpp_name("TextLabel", program);
        const auto wt_var    = component_local(wt);
        const auto tl_var    = component_local(tl);
        out << tick_signature;
        if (!world_transform_is_volume(program)) {
            out << "    auto view = registry.view<" << wt << ", " << tl << ">();\n";
            out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt_var << ", const " << tl << "& "
                << tl_var << ") {\n";
            out << "        (void)entity;\n";
            out << "        cactus::runtime::entt_backend::submit_text_2d(" << wt_var << ".position, " << wt_var
                << ".rotation, " << tl_var << ".font_size, " << tl_var << ".color, " << tl_var << ".text, " << tl_var
                << ".visible);\n";
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
        const auto wt_var    = component_local(wt);
        const auto tl_var    = component_local(tl);
        out << tick_signature;
        if (world_transform_is_volume(program)) {
            out << "    auto view = registry.view<" << wt << ", " << tl << ">();\n";
            out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt_var << ", const " << tl << "& "
                << tl_var << ") {\n";
            out << "        cactus::runtime::entt_backend::submit_text_3d(static_cast<uint32_t>(entity), " << wt_var
                << ".position, " << wt_var << ".rotation, " << wt_var << ".scale, " << tl_var << ".font_size, "
                << tl_var << ".color, " << tl_var << ".text, " << tl_var << ".visible);\n";
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
        const auto sl_var    = component_local(sl);
        out << tick_signature;
        out << "    auto view = registry.view<" << sl << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << sl << "& " << sl_var << ") {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_screen_label(" << sl_var << ".position, " << sl_var
            << ".font_size, " << sl_var << ".color, " << sl_var << ".text, " << sl_var << ".visible);\n";
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
        // bodies used for their cactus_gen_editor_active check.
        const std::string cam3d = EnttCodegenUtils::trait_cpp_name("EditorCamera3D", program);
        out << tick_signature;
        out << "    bool cactus_gen_has_3d_rig = false;\n";
        out << "    auto cactus_gen_cam3d_view = registry.view<" << cam3d << ">();\n";
        out << "    for (auto cactus_gen_e : cactus_gen_cam3d_view) { (void)cactus_gen_e; cactus_gen_has_3d_rig = true; break; }\n";
        out << "    if (!cactus_gen_has_3d_rig) { return; }\n";
        out << "    " << kBeginMode3D << "\n";
        out << "    cactus::runtime::raylib::DrawGrid(20, 1.0F);\n";
        out << "    " << kEndMode3D << "\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_editor_extern_system(sys)) {
        // EditorPropertyPanel — unimplemented stub.
        out << tick_signature;
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

        out << tick_signature;
        out << "    std::vector<entt::entity> cactus_gen_ui_paint_order;\n";
        out << "    for (auto cactus_gen_e : registry.view<" << cl_cpp << ">()) { cactus_gen_ui_paint_order.push_back(cactus_gen_e); }\n";
        out << "    std::ranges::sort(cactus_gen_ui_paint_order, [&](entt::entity cactus_gen_a, entt::entity cactus_gen_b) {\n";
        out << "        return registry.get<" << cl_cpp << ">(cactus_gen_a).draw_order < registry.get<" << cl_cpp
            << ">(cactus_gen_b).draw_order;\n";
        out << "    });\n";
        out << "    for (auto cactus_gen_e : cactus_gen_ui_paint_order) {\n";
        out << "        const auto& cactus_gen_cl = registry.get<" << cl_cpp << ">(cactus_gen_e);\n";
        out << "        cactus::runtime::entt_backend::UiPrimitive cactus_gen_prim{};\n";
        out << "        cactus_gen_prim.position = cactus_gen_cl.position;\n";
        out << "        cactus_gen_prim.size = cactus_gen_cl.size;\n";
        out << "        cactus_gen_prim.effective_visible = cactus_gen_cl.effective_visible;\n";
        out << "        cactus_gen_prim.effective_enabled = cactus_gen_cl.effective_enabled;\n";
        out << "        cactus_gen_prim.effective_opacity = cactus_gen_cl.effective_opacity;\n";
        out << "        cactus_gen_prim.clip_min = cactus_gen_cl.clip_min;\n";
        out << "        cactus_gen_prim.clip_max = cactus_gen_cl.clip_max;\n";
        out << "        if (const auto* cactus_gen_visual = registry.try_get<" << visual_cpp
            << ">(cactus_gen_e)) { cactus_gen_prim.visual_scale = cactus_gen_visual->scale; }\n";
        out << "        if (const auto* cactus_gen_panel = registry.try_get<" << panel_cpp << ">(cactus_gen_e)) {\n";
        out << "            cactus_gen_prim.has_panel = true;\n";
        out << "            cactus_gen_prim.panel_background = cactus_gen_panel->background;\n";
        out << "            cactus_gen_prim.panel_border_color = cactus_gen_panel->border_color;\n";
        out << "            cactus_gen_prim.panel_border_width = cactus_gen_panel->border_width;\n";
        out << "        }\n";
        out << "        if (const auto* cactus_gen_text = registry.try_get<" << text_cpp << ">(cactus_gen_e)) {\n";
        out << "            cactus_gen_prim.has_text = true;\n";
        out << "            cactus_gen_prim.text_value = cactus_gen_text->value;\n";
        out << "            cactus_gen_prim.text_font_size = cactus_gen_text->font_size;\n";
        out << "            cactus_gen_prim.text_color = cactus_gen_text->color;\n";
        out << "            cactus_gen_prim.text_align = static_cast<int>(cactus_gen_text->align);\n";
        out << "        }\n";
        out << "        if (const auto* cactus_gen_image = registry.try_get<" << image_cpp << ">(cactus_gen_e)) {\n";
        out << "            cactus_gen_prim.has_image = true;\n";
        out << "            cactus_gen_prim.image_texture = cactus_gen_image->texture;\n";
        out << "            cactus_gen_prim.image_tint = cactus_gen_image->tint;\n";
        out << "            cactus_gen_prim.image_fit = static_cast<int>(cactus_gen_image->fit);\n";
        out << "        }\n";
        out << "        if (const auto* cactus_gen_anim = registry.try_get<" << anim_cpp << ">(cactus_gen_e)) {\n";
        out << "            cactus_gen_prim.has_frame_animation = true;\n";
        out << "            cactus_gen_prim.frame_count = cactus_gen_anim->frame_count;\n";
        out << "            cactus_gen_prim.frame = cactus_gen_anim->frame;\n";
        out << "        }\n";
        out << "        if (const auto* cactus_gen_button = registry.try_get<" << button_cpp << ">(cactus_gen_e)) {\n";
        out << "            cactus_gen_prim.has_button = true;\n";
        out << "            cactus_gen_prim.button_label = cactus_gen_button->label;\n";
        out << "            cactus_gen_prim.button_normal_color = cactus_gen_button->normal_color;\n";
        out << "            cactus_gen_prim.button_hover_color = cactus_gen_button->hover_color;\n";
        out << "            cactus_gen_prim.button_pressed_color = cactus_gen_button->pressed_color;\n";
        out << "            cactus_gen_prim.button_disabled_color = cactus_gen_button->disabled_color;\n";
        out << "            cactus_gen_prim.button_text_color = cactus_gen_button->text_color;\n";
        out << "        }\n";
        out << "        if (const auto* cactus_gen_pstate = registry.try_get<" << pstate_cpp << ">(cactus_gen_e)) {\n";
        out << "            cactus_gen_prim.pointer_hovered = cactus_gen_pstate->hovered;\n";
        out << "            cactus_gen_prim.pointer_pressed = cactus_gen_pstate->pressed;\n";
        out << "        }\n";
        out << "        cactus::runtime::entt_backend::render_ui_primitive(cactus_gen_prim);\n";
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

        out << tick_signature;
        out << "    const auto cactus_gen_t = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);\n";
        out << "    const Vector2 cactus_gen_pos = cactus::runtime::entt_backend::mouse_position();\n";
        out << "    if (cactus_gen_t.hover_changed) {\n";
        out << "        if (registry.valid(cactus_gen_t.leave_target)) {\n";
        out << "            if (auto* cactus_gen_ps = registry.try_get<" << pstate_cpp
            << ">(cactus_gen_t.leave_target)) { cactus_gen_ps->hovered = false; }\n";
        out << "            cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "                " << leave_evt << "{.position = cactus_gen_pos}, cactus_gen_t.leave_target);\n";
        out << "        }\n";
        out << "        if (registry.valid(cactus_gen_t.enter_target)) {\n";
        out << "            if (auto* cactus_gen_ps = registry.try_get<" << pstate_cpp
            << ">(cactus_gen_t.enter_target)) { cactus_gen_ps->hovered = true; }\n";
        out << "            cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "                " << enter_evt << "{.position = cactus_gen_pos}, cactus_gen_t.enter_target);\n";
        out << "        }\n";
        out << "    }\n";
        out << "    if (cactus_gen_t.press_occurred && registry.valid(cactus_gen_t.press_target)) {\n";
        out << "        if (auto* cactus_gen_ps = registry.try_get<" << pstate_cpp
            << ">(cactus_gen_t.press_target)) { cactus_gen_ps->pressed = true; }\n";
        out << "        cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "            " << press_evt << "{.position = cactus_gen_pos}, cactus_gen_t.press_target);\n";
        out << "    }\n";
        out << "    if (cactus_gen_t.release_occurred && registry.valid(cactus_gen_t.release_target)) {\n";
        out << "        if (auto* cactus_gen_ps = registry.try_get<" << pstate_cpp
            << ">(cactus_gen_t.release_target)) { cactus_gen_ps->pressed = false; }\n";
        out << "        cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "            " << release_evt << "{.position = cactus_gen_pos}, cactus_gen_t.release_target);\n";
        out << "        if (cactus_gen_t.release_is_click) {\n";
        out << "            cactus::runtime::entt_backend::generated_emit_targeted_event(\n";
        out << "                " << click_evt << "{.position = cactus_gen_pos}, cactus_gen_t.release_target);\n";
        out << "        }\n";
        out << "    }\n";
        out << "    if (cactus_gen_t.should_consume_primary) {\n";
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

    std::ostringstream out;
    out << "namespace cactus_gen::detail {\n";
    // What the hierarchy facts prove; a constant depth bound allows per-level passes.
    out << "[[maybe_unused]] constexpr auto kCactusHierarchyStrategy = "
        << EnttCodegenUtils::hierarchy_strategy_cpp(program) << ";\n";
    if (program.hierarchy.depth_bound.has_value()) {
        out << "[[maybe_unused]] constexpr std::uint32_t kCactusHierarchyDepthBound = "
            << *program.hierarchy.depth_bound << ";\n";
    }
    out << "\n";
    out << "[[maybe_unused]] static void cactus_destroy_entity_recursive(entt::registry& registry, entt::entity "
           "entity) {\n";
    out << "    cactus::runtime::entt_backend::destroy_entity_recursive(registry, "
        << EnttCodegenUtils::hierarchy_index_expr(program) << ", entity);\n";
    out << "}\n";
    out << "}  // namespace cactus_gen::detail\n\n";
    return out.str();
}

}  // namespace cactus
