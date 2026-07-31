#include "backends/cpp-entt/system_emitter.hpp"

#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <cctype>
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
    return {};
}

// Table mapping (module_name, dsl_func_name) → runtime_func_name for cases
// where the names differ. Functions absent from the table keep their DSL name.
using FuncRenameMap                                                    = std::unordered_map<std::string, std::string>;
const std::unordered_map<std::string, FuncRenameMap> kRuntimeFuncNames = {
    {"std.render.models",
     {{"animation_count", "model_animation_count"},
      {"animation_name", "model_animation_name"},
      {"bounds_size", "model_bounds_size"}}},
    {"std.editor",
     {{"spawn_template", "editor_spawn_template"},
      {"hit_test_2d", "editor_hit_test_2d"},
      {"raycast_3d", "editor_raycast_3d"},
      {"camera_enter", "editor_camera_enter"},
      {"camera_exit", "editor_camera_exit"},
      {"apply_camera_2d", "editor_apply_camera_2d"},
      {"apply_camera_3d", "editor_apply_camera_3d"}}},
    {"std.input",
     {{"mouse_delta", "editor_mouse_delta_screen"},
      {"wheel_delta", "editor_wheel_delta"},
      {"consume", "editor_consume"}}},
    {"std.camera.flat",
     {{"screen_to_world", "editor_screen_to_world_2d"}, {"screen_delta_to_world", "screen_delta_to_world_2d"}}},
    {"std.camera.volume",
     {{"screen_to_plane", "editor_plane_project_3d"}, {"screen_delta_on_plane", "screen_delta_on_plane_3d"}}},
    {"std.transform.flat", {{"world_position", "editor_entity_position_2d"}}},
    {"std.transform.volume", {{"world_position", "editor_entity_position_3d"}}},
};

std::string stdlib_runtime_func_name(const std::string& module_name, const std::string& func_name) {
    const auto module_it = kRuntimeFuncNames.find(module_name);
    if (module_it != kRuntimeFuncNames.end()) {
        const auto func_it = module_it->second.find(func_name);
        if (func_it != module_it->second.end()) {
            return func_it->second;
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

bool stdlib_call_needs_registry(const SymbolId& func_id) {
    if (func_id.kind != SymbolKind::Func) {
        return false;
    }
    if (func_id.module.name == "std.editor") {
        return func_id.local_name == "spawn_template" || func_id.local_name == "hit_test_2d" ||
               func_id.local_name == "raycast_3d" || func_id.local_name == "camera_enter" ||
               func_id.local_name == "camera_exit" || func_id.local_name == "apply_camera_2d" ||
               func_id.local_name == "apply_camera_3d";
    }
    return (func_id.module.name == "std.transform.flat" || func_id.module.name == "std.transform.volume") &&
           func_id.local_name == "world_position";
}

std::string lower_resolved_stdlib_call(const SymbolId& func_id,
                                       const std::vector<std::unique_ptr<ExprNode>>& args,
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
    if (handler.event_name.find('.') == std::string::npos || !handler.resolved_trigger.has_value()) {
        return handler.event_name;
    }
    return canonical_to_cpp_name(handler.resolved_trigger->symbol);
}

std::string handler_trigger_binding(const EventHandlerNode& handler) {
    return handler.alias.value_or(snake_case(handler_trigger_suffix(handler)));
}

const HandlerContract* graph_handler_contract(const SystemNode& system,
                                              const EventHandlerNode& handler,
                                              const DecoratedProgram& program) {
    if (!system.resolved_system_id.has_value() || !handler.resolved_trigger.has_value()) {
        return nullptr;
    }
    const HandlerIdentity identity{.system = *system.resolved_system_id, .trigger = *handler.resolved_trigger};
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

bool is_flat_transform_propagation(const ExternSystemNode& sys, const DecoratedProgram& program) {
    // When std.editor is used it transitively imports both transform modules,
    // so both flat and volume TransformPropagation can appear in the merged AST.
    // Only emit the variant that matches the program's actual WorldTransform dimensionality.
    if (symbol_is(sys.resolved_system_id, SymbolKind::System, "std.transform.flat", "TransformPropagation")) {
        return !world_transform_is_volume(program);
    }
    if (symbol_is(sys.resolved_system_id, SymbolKind::System, "std.transform.volume", "TransformPropagation")) {
        return false;
    }
    return false;
}

bool is_volume_transform_propagation(const ExternSystemNode& sys, const DecoratedProgram& program) {
    if (symbol_is(sys.resolved_system_id, SymbolKind::System, "std.transform.volume", "TransformPropagation")) {
        return world_transform_is_volume(program);
    }
    if (symbol_is(sys.resolved_system_id, SymbolKind::System, "std.transform.flat", "TransformPropagation")) {
        return false;
    }
    return false;
}

bool is_shape_renderer(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.shapes", "ShapeRenderer");
}

bool is_sprite_renderer(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.sprites", "SpriteRenderer");
}

bool is_animated_sprite_system(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.sprites", "AnimatedSpriteSystem");
}

bool is_mesh_renderer(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.meshes", "MeshRenderer");
}

bool is_model_renderer_system(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.models", "ModelRendererSystem");
}

bool is_model_animation_system(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.models", "ModelAnimationSystem");
}

bool is_billboard_renderer(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.meshes", "BillboardRenderer");
}

bool is_point_light_system(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.meshes", "PointLightSystem");
}

bool is_directional_light_system(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.meshes", "DirectionalLightSystem");
}

bool is_any_text_renderer_2d(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.text", "TextRenderer2D");
}

bool is_any_text_renderer_3d(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.text", "TextRenderer3D");
}

bool is_screen_label_system(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.render.text", "ScreenLabelSystem");
}

bool is_editor_extern_system(const ExternSystemNode& sys) {
    return symbol_is(sys.resolved_system_id, SymbolKind::System, "std.editor", "EditorTemplatePalette") ||
           symbol_is(sys.resolved_system_id, SymbolKind::System, "std.editor", "EditorPropertyPanel") ||
           symbol_is(sys.resolved_system_id, SymbolKind::System, "std.editor", "GizmoRenderer2D") ||
           symbol_is(sys.resolved_system_id, SymbolKind::System, "std.editor", "GizmoRenderer3D");
}

std::string sort_key_expr(const SortKey& key, const std::string& entity_name, const SystemNode& sys) {
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

std::string primary_sort_trait(const SortKey& key, const SystemNode& sys) {
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

void emit_sort_call(std::ostringstream& out, const SystemNode& sys) {
    if (sys.order_by.empty()) {
        return;
    }

    out << "    registry.sort<" << primary_sort_trait(sys.order_by.front(), sys)
        << ">([&](entt::entity a, entt::entity b) {\n";
    for (const auto& key : sys.order_by) {
        auto left  = sort_key_expr(key, "a", sys);
        auto right = sort_key_expr(key, "b", sys);
        out << "        if (" << left << " != " << right << ") return " << left << (key.descending ? " > " : " < ")
            << right << ";\n";
    }
    out << "        return false;\n";
    out << "    });\n";
}

std::string foreach_temp_name(const ForeachStmt& stmt) {
    return "__foreach_snapshot_" + std::to_string(std::max(stmt.location.line, 0)) + "_" +
           std::to_string(std::max(stmt.location.column, 0));
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

// ── Rewrite expression: replace bare field names with comp.field ─────────────

static std::string rewrite_expr(const ExprNode& expr,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases            = {},
                                const std::unordered_map<std::string, std::string>& cpp_overrides = {});

static std::string rewrite_stmt(const StmtNode& stmt,
                                int indent,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases            = {},
                                bool dispatcher_available                                         = false,
                                const std::unordered_map<std::string, std::string>& cpp_overrides = {});

static std::string trait_cpp_from_entry(const ArchetypeTraitEntry& entry, const DecoratedProgram& program) {
    return EnttCodegenUtils::trait_cpp_name(entry.resolved_trait_id, entry.trait_name, program);
}

static std::string emit_spawn_overrides(const std::string& entity_name,
                                        const std::vector<ArchetypeTraitEntry>& overrides,
                                        int indent,
                                        const std::vector<std::string>& trait_names,
                                        const DecoratedProgram& program,
                                        const std::unordered_set<std::string>& pointer_aliases) {
    const std::string ind(static_cast<std::size_t>(indent) * 4U, ' ');
    std::ostringstream out;
    for (const auto& override_entry : overrides) {
        if (override_entry.assignments.empty()) {
            continue;
        }

        const std::string cpp_name = trait_cpp_from_entry(override_entry, program);
        out << ind << "{\n";
        out << ind << "    auto __existing = registry.try_get<" << cpp_name << ">(" << entity_name << ");\n";
        out << ind << "    auto __value = __existing ? *__existing : " << cpp_name << "{};\n";
        for (const auto& assignment : override_entry.assignments) {
            out << ind << "    __value." << assignment.name << " = "
                << rewrite_expr(*assignment.value, trait_names, program, pointer_aliases) << ";\n";
        }
        out << ind << "    registry.emplace_or_replace<" << cpp_name << ">(" << entity_name << ", __value);\n";
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
                                       const std::unordered_set<std::string>& pointer_aliases) {
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
            out << emit_spawn_overrides(var, override_node->traits, 1, trait_names, program, pointer_aliases);
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
                                   pointer_aliases);
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
                                                     const std::unordered_set<std::string>& pointer_aliases) {
    std::ostringstream out;
    std::vector<std::string> role_path;
    const bool is_local_tmpl      = program.pub_templates.contains(template_id.local_name) ||
                                    program.non_pub_templates.contains(template_id.local_name);
    const std::string tmpl_module = is_local_tmpl ? program.module_name : template_id.module.name;
    const std::string& tmpl_local = template_id.local_name;
    out << "([&]() {\n";
    const bool graph_runtime = !program.execution_graph.phases.empty();
    if (graph_runtime) {
        out << "    auto __spawned = cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        out << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        out << "        cactus::runtime::entt_backend::CactusStructuralCommand::Kind::Spawn,\n";
        out << "        [=](entt::registry& registry) mutable {\n";
        out << "            auto __committed = "
            << archetype_node_create_at_function_name(tmpl_module, tmpl_local, role_path) << "(registry, __spawned);\n";
        out << emit_spawn_overrides("__committed", root_overrides, 3, trait_names, program, pointer_aliases);
    } else {
        out << "    auto __spawned = " << archetype_node_create_function_name(tmpl_module, tmpl_local, role_path)
            << "(registry);\n";
        out << emit_spawn_overrides("__spawned", root_overrides, 1, trait_names, program, pointer_aliases);
    }
    emit_spawn_child_expansion(out,
                               tmpl_module,
                               tmpl_local,
                               children,
                               child_overrides,
                               graph_runtime ? "__committed" : "__spawned",
                               "__child",
                               role_path,
                               trait_names,
                               program,
                               pointer_aliases);
    if (graph_runtime) {
        out << "        });\n";
    }
    out << "    return __spawned;\n";
    out << "})()";
    return out.str();
}

static std::string emit_spawn_expression(const SpawnExpr& spawn,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases) {
    const SymbolId tmpl_id = spawn.resolved_template_id.has_value()
                                 ? *spawn.resolved_template_id
                                 : make_symbol_id(SymbolKind::Template, program.module_name, spawn.template_name);
    if (!spawn.child_overrides.empty()) {
        if (const auto* children = find_template_children(program, tmpl_id)) {
            return emit_hierarchical_spawn_expansion(
                tmpl_id, spawn.overrides, spawn.child_overrides, *children, trait_names, program, pointer_aliases);
        }
    }
    std::ostringstream out;
    out << "([&]() {\n";
    if (!program.execution_graph.phases.empty()) {
        out << "    auto __spawned = cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
        out << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
        out << "        cactus::runtime::entt_backend::CactusStructuralCommand::Kind::Spawn,\n";
        out << "        [=](entt::registry& registry) mutable {\n";
        out << "            " << archetype_create_at_function_name(tmpl_id, program) << "(registry, __spawned);\n";
        out << emit_spawn_overrides("__spawned", spawn.overrides, 3, trait_names, program, pointer_aliases);
        out << "        });\n";
    } else {
        out << "    auto __spawned = " << archetype_create_function_name(tmpl_id, program) << "(registry);\n";
        out << emit_spawn_overrides("__spawned", spawn.overrides, 1, trait_names, program, pointer_aliases);
    }
    out << "    return __spawned;\n";
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
        return "[&]{ auto __v = " + view + "; return __v.begin() != __v.end(); }()";
    }
    if (func_name == "count") {
        return "[&]{ return static_cast<int>(std::ranges::distance(" + view + ")); }()";
    }
    if (func_name == "first") {
        return "[&]{ auto __v = " + view +
               "; auto __it = __v.begin(); return __it != __v.end() ? "
               "static_cast<entt::entity>(*__it) : entt::entity{entt::null}; }()";
    }
    if (func_name == "all") {
        return "[&]{ std::vector<entt::entity> __r; for (auto __e : " + view + ") __r.push_back(__e); return __r; }()";
    }
    if (func_name == "parent") {
        const std::string of_expr    = find_named_arg_value(qcall.named_args, "of", emit_arg);
        const std::string parent_cpp = EnttCodegenUtils::trait_cpp_name("Parent", program);
        return "[&]{ if (auto* __p = registry.try_get<" + parent_cpp + ">(" + of_expr +
               "); __p != nullptr) return __p->parent; return entt::entity{entt::null}; }()";
    }
    return "/* unsupported std.query func: " + func_name + " */";
}

static std::string lower_flat_spatial_query(const QueryCallExpr& qcall,
                                            const std::string& func_name,
                                            const auto& emit_arg,
                                            const DecoratedProgram& program) {
    const std::string wt   = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
    const std::string view = "registry.view" + build_view_suffix(qcall.filters, wt, &program);
    if (func_name == "nearest") {
        const std::string from = find_named_arg_value(qcall.named_args, "from", emit_arg);
        return "[&]{ const auto __from = (" + from +
               "); "
               "entt::entity __best{entt::null}; float __best_d = std::numeric_limits<float>::max(); "
               "for (auto __e : " +
               view +
               ") { "
               "const auto& __wt = registry.get<" +
               wt +
               ">(__e); "
               "float __dx = __wt.position.x - __from.x, "
               "__dy = __wt.position.y - __from.y; "
               "float __d = __dx * __dx + __dy * __dy; "
               "if (__d < __best_d) { __best_d = __d; __best = __e; } } return __best; }()";
    }
    if (func_name == "overlap_box") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string size   = find_named_arg_value(qcall.named_args, "size", emit_arg);
        return "[&]{ const auto __ct = (" + center + "); const auto __sz = (" + size +
               "); "
               "std::vector<entt::entity> __r; "
               "for (auto __e : " +
               view +
               ") { "
               "const auto& __wt = registry.get<" +
               wt +
               ">(__e); "
               "float __hx = __sz.x * 0.5F, __hy = __sz.y * 0.5F; "
               "if (std::abs(__wt.position.x - __ct.x) <= __hx && "
               "std::abs(__wt.position.y - __ct.y) <= __hy) "
               "__r.push_back(__e); } return __r; }()";
    }
    if (func_name == "overlap_circle") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string radius = find_named_arg_value(qcall.named_args, "radius", emit_arg);
        return "[&]{ const auto __ct = (" + center + "); const float __rad = (" + radius +
               "); "
               "std::vector<entt::entity> __r; "
               "for (auto __e : " +
               view +
               ") { "
               "const auto& __wt = registry.get<" +
               wt +
               ">(__e); "
               "float __dx = __wt.position.x - __ct.x, "
               "__dy = __wt.position.y - __ct.y; "
               "if ((__dx * __dx + __dy * __dy) <= __rad * __rad) "
               "__r.push_back(__e); } return __r; }()";
    }
    if (func_name == "raycast") {
        const std::string origin   = find_named_arg_value(qcall.named_args, "origin", emit_arg);
        const std::string dir      = find_named_arg_value(qcall.named_args, "dir", emit_arg);
        const std::string max_dist = find_named_arg_value(qcall.named_args, "max_dist", emit_arg);
        return "[&]{ const auto __org = (" + origin + "); const auto __dir = (" + dir + "); const float __md = (" +
               max_dist +
               "); "
               "entt::entity __best{entt::null}; float __best_d = std::numeric_limits<float>::max(); "
               "for (auto __e : " +
               view +
               ") { "
               "const auto& __wt = registry.get<" +
               wt +
               ">(__e); "
               "float __dx = __wt.position.x - __org.x, "
               "__dy = __wt.position.y - __org.y; "
               "float __proj = __dx * __dir.x + __dy * __dir.y; "
               "if (__proj >= 0.0F && __proj <= __md) { "
               "float __perp = __dx * __dir.y - __dy * __dir.x; "
               "if (std::abs(__perp) < 0.5F && __proj < __best_d) { __best_d = __proj; __best = __e; } } } "
               "return __best; }()";
    }
    return "/* unsupported std.physics.flat.query func: " + func_name + " */";
}

static std::string lower_volume_spatial_query(const QueryCallExpr& qcall,
                                              const std::string& func_name,
                                              const auto& emit_arg,
                                              const DecoratedProgram& program) {
    const std::string wt   = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
    const std::string view = "registry.view" + build_view_suffix(qcall.filters, wt, &program);
    if (func_name == "nearest") {
        const std::string from = find_named_arg_value(qcall.named_args, "from", emit_arg);
        return "[&]{ const auto __from = (" + from +
               "); "
               "entt::entity __best{entt::null}; float __best_d = std::numeric_limits<float>::max(); "
               "for (auto __e : " +
               view +
               ") { "
               "const auto& __wt = registry.get<" +
               wt +
               ">(__e); "
               "float __dx = __wt.position.x - __from.x, "
               "__dy = __wt.position.y - __from.y, "
               "__dz = __wt.position.z - __from.z; "
               "float __d = __dx * __dx + __dy * __dy + __dz * __dz; "
               "if (__d < __best_d) { __best_d = __d; __best = __e; } } return __best; }()";
    }
    if (func_name == "overlap_box") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string size   = find_named_arg_value(qcall.named_args, "size", emit_arg);
        return "[&]{ const auto __ct = (" + center + "); const auto __sz = (" + size +
               "); "
               "std::vector<entt::entity> __r; "
               "for (auto __e : " +
               view +
               ") { "
               "const auto& __wt = registry.get<" +
               wt +
               ">(__e); "
               "float __hx = __sz.x * 0.5F, __hy = __sz.y * 0.5F, __hz = __sz.z * 0.5F; "
               "if (std::abs(__wt.position.x - __ct.x) <= __hx && "
               "std::abs(__wt.position.y - __ct.y) <= __hy && "
               "std::abs(__wt.position.z - __ct.z) <= __hz) "
               "__r.push_back(__e); } return __r; }()";
    }
    if (func_name == "overlap_sphere") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string radius = find_named_arg_value(qcall.named_args, "radius", emit_arg);
        return "[&]{ const auto __ct = (" + center + "); const float __rad = (" + radius +
               "); "
               "std::vector<entt::entity> __r; "
               "for (auto __e : " +
               view +
               ") { "
               "const auto& __wt = registry.get<" +
               wt +
               ">(__e); "
               "float __dx = __wt.position.x - __ct.x, "
               "__dy = __wt.position.y - __ct.y, "
               "__dz = __wt.position.z - __ct.z; "
               "if ((__dx * __dx + __dy * __dy + __dz * __dz) <= __rad * __rad) "
               "__r.push_back(__e); } return __r; }()";
    }
    if (func_name == "raycast") {
        const std::string origin   = find_named_arg_value(qcall.named_args, "origin", emit_arg);
        const std::string dir      = find_named_arg_value(qcall.named_args, "dir", emit_arg);
        const std::string max_dist = find_named_arg_value(qcall.named_args, "max_dist", emit_arg);
        return "[&]{ const auto __org = (" + origin + "); const auto __dir = (" + dir + "); const float __md = (" +
               max_dist +
               "); "
               "entt::entity __best{entt::null}; float __best_d = std::numeric_limits<float>::max(); "
               "for (auto __e : " +
               view +
               ") { "
               "const auto& __wt = registry.get<" +
               wt +
               ">(__e); "
               "float __dx = __wt.position.x - __org.x, "
               "__dy = __wt.position.y - __org.y, "
               "__dz = __wt.position.z - __org.z; "
               "float __proj = __dx * __dir.x + __dy * __dir.y + __dz * __dir.z; "
               "if (__proj >= 0.0F && __proj <= __md) { "
               "float __perp_x = __dy * __dir.z - __dz * __dir.y, "
               "__perp_y = __dz * __dir.x - __dx * __dir.z, "
               "__perp_z = __dx * __dir.y - __dy * __dir.x; "
               "if ((__perp_x * __perp_x + __perp_y * __perp_y + __perp_z * __perp_z) < 0.25F "
               "&& __proj < __best_d) { __best_d = __proj; __best = __e; } } } return __best; }()";
    }
    return "/* unsupported std.physics.volume.query func: " + func_name + " */";
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

static std::string emit_trait_match_stmt(const TraitMatchStmt& match_stmt,
                                         int indent,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases            = {},
                                         bool dispatcher_available                                         = false,
                                         const std::unordered_map<std::string, std::string>& cpp_overrides = {});

static std::string rewrite_expr(const ExprNode& expr,  // NOLINT(readability-function-cognitive-complexity)
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases,
                                const std::unordered_map<std::string, std::string>& cpp_overrides) {
    auto known_fields = collect_trait_fields(trait_names, program);

    return std::visit(
        [&](auto& e) -> std::string {  // NOLINT(readability-function-cognitive-complexity)
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
                return "(" + rewrite_expr(*e.left, trait_names, program, pointer_aliases, cpp_overrides) + " " + op +
                       " " + rewrite_expr(*e.right, trait_names, program, pointer_aliases, cpp_overrides) + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") {
                    op = "!";
                }
                return op + rewrite_expr(*e.operand, trait_names, program, pointer_aliases, cpp_overrides);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                if (auto* ident = std::get_if<IdentExpr>(&e.callee->expr);
                    ident != nullptr && ident->name == "exists" && e.args.size() == 1) {
                    return "registry.valid(" +
                           rewrite_expr(*e.args[0], trait_names, program, pointer_aliases, cpp_overrides) + ")";
                }
                // Module-scope stdlib call: use resolved callee identity (preferred path).
                if (e.resolved_callee_id.has_value()) {
                    const auto lowered =
                        lower_resolved_stdlib_call(*e.resolved_callee_id, e.args, [&](const ExprNode& arg) {
                            return rewrite_expr(arg, trait_names, program, pointer_aliases, cpp_overrides);
                        });
                    if (!lowered.empty()) {
                        return lowered;
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
                        return lower_resolved_stdlib_call(func_id, e.args, [&](const ExprNode& arg) {
                            return rewrite_expr(arg, trait_names, program, pointer_aliases, cpp_overrides);
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
                        std::string result = "InputEvent::" + ident->name + "(";
                        for (size_t i = 0; i < e.args.size(); ++i) {
                            if (i > 0) {
                                result += ", ";
                            }
                            result += rewrite_expr(*e.args[i], trait_names, program, pointer_aliases, cpp_overrides);
                        }
                        return result + ")";
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
                                        const auto lowered =
                                            lower_resolved_stdlib_call(func_id, e.args, [&](const ExprNode& arg) {
                                                return rewrite_expr(
                                                    arg, trait_names, program, pointer_aliases, cpp_overrides);
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
                        if (object->name == "input" && member->member == "mouse_position" && e.args.empty()) {
                            return "cactus::runtime::entt_backend::mouse_position()";
                        }
                        if (object->name == "input" && (member->member == "axis" || member->member == "pressed" ||
                                                        member->member == "down" || member->member == "released")) {
                            std::string result = "InputEvent::" + member->member + "(";
                            for (size_t i = 0; i < e.args.size(); ++i) {
                                if (i > 0) {
                                    result += ", ";
                                }
                                result +=
                                    rewrite_expr(*e.args[i], trait_names, program, pointer_aliases, cpp_overrides);
                            }
                            return result + ")";
                        }
                        if (object->name == "input" && member->member == "mouse_delta" && e.args.empty()) {
                            return "cactus::runtime::entt_backend::editor_mouse_delta_screen()";
                        }
                        if (object->name == "input" && member->member == "wheel_delta" && e.args.empty()) {
                            return "cactus::runtime::entt_backend::editor_wheel_delta()";
                        }
                        if (object->name == "input" && member->member == "consume" && e.args.size() == 1) {
                            return "cactus::runtime::entt_backend::editor_consume(" +
                                   rewrite_expr(*e.args[0], trait_names, program, pointer_aliases, cpp_overrides) + ")";
                        }
                        // camera2d/camera3d/transform2d/transform3d are aliases declared in
                        // std.editor and are invisible from the root program's AST, so
                        // imported_module_name cannot resolve them and they need hardcoded dispatch.
                        if (object->name == "camera2d") {
                            if (member->member == "screen_to_world" && e.args.size() == 1) {
                                return "cactus::runtime::entt_backend::editor_screen_to_world_2d(" +
                                       rewrite_expr(*e.args[0], trait_names, program, pointer_aliases, cpp_overrides) +
                                       ")";
                            }
                            if (member->member == "screen_delta_to_world" && e.args.size() == 1) {
                                return "cactus::runtime::entt_backend::screen_delta_to_world_2d(" +
                                       rewrite_expr(*e.args[0], trait_names, program, pointer_aliases, cpp_overrides) +
                                       ")";
                            }
                        }
                        if (object->name == "camera3d") {
                            if (member->member == "screen_to_plane" && e.args.size() == 3) {
                                std::string result = "cactus::runtime::entt_backend::editor_plane_project_3d(";
                                for (size_t i = 0; i < e.args.size(); ++i) {
                                    if (i > 0) {
                                        result += ", ";
                                    }
                                    result +=
                                        rewrite_expr(*e.args[i], trait_names, program, pointer_aliases, cpp_overrides);
                                }
                                return result + ")";
                            }
                            if (member->member == "screen_delta_on_plane" && e.args.size() == 4) {
                                std::string result = "cactus::runtime::entt_backend::screen_delta_on_plane_3d(";
                                for (size_t i = 0; i < e.args.size(); ++i) {
                                    if (i > 0) {
                                        result += ", ";
                                    }
                                    result +=
                                        rewrite_expr(*e.args[i], trait_names, program, pointer_aliases, cpp_overrides);
                                }
                                return result + ")";
                            }
                        }
                        if (object->name == "transform2d" && member->member == "world_position" && e.args.size() == 1) {
                            return "cactus::runtime::entt_backend::editor_entity_position_2d(registry, " +
                                   rewrite_expr(*e.args[0], trait_names, program, pointer_aliases, cpp_overrides) + ")";
                        }
                        if (object->name == "transform3d" && member->member == "world_position" && e.args.size() == 1) {
                            return "cactus::runtime::entt_backend::editor_entity_position_3d(registry, " +
                                   rewrite_expr(*e.args[0], trait_names, program, pointer_aliases, cpp_overrides) + ")";
                        }
                    }
                }
                std::string result =
                    rewrite_expr(*e.callee, trait_names, program, pointer_aliases, cpp_overrides) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += rewrite_expr(*e.args[i], trait_names, program, pointer_aliases, cpp_overrides);
                }
                return result + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
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
                return rewrite_expr(*e.object, trait_names, program, pointer_aliases, cpp_overrides) + "." + e.member;
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                return emit_spawn_expression(e, trait_names, program, pointer_aliases);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                std::string result = "std::vector{";
                for (size_t i = 0; i < e.elements.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += rewrite_expr(*e.elements[i], trait_names, program, pointer_aliases, cpp_overrides);
                }
                result += "}";
                return result;
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                return lower_query_call_expr(e, program, [&](const ExprNode& arg) {
                    return rewrite_expr(arg, trait_names, program, pointer_aliases, cpp_overrides);
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
                                         const std::unordered_map<std::string, std::string>& cpp_overrides) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::ostringstream out;

    out << ind << "{\n";
    out << ind << "    auto __match_entity = "
        << rewrite_expr(*match_stmt.subject, trait_names, program, pointer_aliases, cpp_overrides) << ";\n";
    out << ind << "    if (registry.valid(__match_entity)) {\n";

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

        out << ind << "        " << (first ? "if" : "else if") << " (";
        if (IS_MARKER) {
            out << "registry.all_of<" << cpp_arm << ">(__match_entity)) {\n";
        } else {
            const std::string ALIAS = arm.alias.value_or("__match_" + cpp_arm);
            arm_aliases.insert(ALIAS);
            out << "auto* " << ALIAS << " = registry.try_get<" << cpp_arm << ">(__match_entity)) {\n";
        }

        for (const auto& stmt : arm.body) {
            out << rewrite_stmt(
                *stmt, indent + 3, trait_names, program, arm_aliases, dispatcher_available, cpp_overrides);
        }
        out << ind << "        }";
        first = false;
        if (!first || arm.location.line >= 0) {
            out << "\n";
        }
    }

    if (match_stmt.wildcard.has_value()) {
        out << ind << "        " << (first ? "if (true)" : "else") << " {\n";
        for (const auto& stmt : match_stmt.wildcard->body) {
            out << rewrite_stmt(
                *stmt, indent + 3, trait_names, program, pointer_aliases, dispatcher_available, cpp_overrides);
        }
        out << ind << "        }\n";
    }

    out << ind << "    }\n";
    out << ind << "}\n";
    return out.str();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static std::string rewrite_stmt(const StmtNode& stmt,
                                int indent,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases,
                                bool dispatcher_available,
                                const std::unordered_map<std::string, std::string>& cpp_overrides) {
    auto known_fields = collect_trait_fields(trait_names, program);
    std::string ind(static_cast<size_t>(indent) * 4, ' ');

    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [&](auto& s) -> std::string {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, LetStmt>) {
                return ind + "[[maybe_unused]] auto " + s.name + " = " +
                       rewrite_expr(*s.value, trait_names, program, pointer_aliases, cpp_overrides) + ";\n";
            } else if constexpr (std::is_same_v<S, VarAssign>) {
                std::string lhs;
                if (known_fields.contains(s.name)) {
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
                    // Local variable — use auto for declaration
                    lhs = "auto " + s.name;
                }
                if (const auto* call = std::get_if<CallExpr>(&s.value->expr)) {
                    if (const auto* ident = std::get_if<IdentExpr>(&call->callee->expr);
                        ident != nullptr && ident->name == "vec2" && call->args.size() == 2) {
                        const std::string prefix = ind + lhs + " " + s.op + " vec2(";
                        const std::string continuation(prefix.size(), ' ');
                        return prefix +
                               rewrite_expr(*call->args[0], trait_names, program, pointer_aliases, cpp_overrides) +
                               ",\n" + continuation +
                               rewrite_expr(*call->args[1], trait_names, program, pointer_aliases, cpp_overrides) +
                               ");\n";
                    }
                }
                return ind + lhs + " " + s.op + " " +
                       rewrite_expr(*s.value, trait_names, program, pointer_aliases, cpp_overrides) + ";\n";
            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                std::string emit_call;
                const bool graph_runtime = !program.execution_graph.phases.empty();
                if (graph_runtime) {
                    const auto event_type = event_cpp_type(s.event_name, program);
                    emit_call             = "cactus::runtime::entt_backend::generated_emit_event(" + event_type + "{";
                } else if (dispatcher_available) {
                    emit_call = "dispatcher.trigger(" + event_cpp_type(s.event_name, program) + "{";
                } else {
                    emit_call = s.event_name + "_buffer.push_back({";
                }
                for (size_t i = 0; i < s.payload.size(); ++i) {
                    if (i > 0) {
                        emit_call += ", ";
                    }
                    emit_call +=
                        "." + s.payload[i].name + " = " +
                        rewrite_expr(*s.payload[i].value, trait_names, program, pointer_aliases, cpp_overrides);
                }
                emit_call += "});";
                if (s.target.has_value()) {
                    const std::string TARGET =
                        rewrite_expr(**s.target, trait_names, program, pointer_aliases, cpp_overrides);
                    return ind + "if (registry.valid(" + TARGET + ")) {\n" + ind + "    " + emit_call + "\n" + ind +
                           "}\n";
                }
                return ind + emit_call + "\n";
            } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                const SymbolId tmpl_id =
                    s.resolved_template_id.has_value()
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
                                                                 pointer_aliases) +
                               ";\n";
                    }
                }
                std::ostringstream result;
                result << ind << "{\n";
                if (!program.execution_graph.phases.empty()) {
                    result << ind
                           << "    auto __spawned = "
                              "cactus::runtime::entt_backend::generated_reserve_entity(registry);\n";
                    result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
                    result << ind << "        cactus::runtime::entt_backend::CactusStructuralCommand::Kind::Spawn,\n";
                    result << ind << "        [=](entt::registry& registry) mutable {\n";
                    result << ind << "            " << archetype_create_at_function_name(tmpl_id, program)
                           << "(registry, __spawned);\n";
                    result << emit_spawn_overrides(
                        "__spawned", s.overrides, indent + 3, trait_names, program, pointer_aliases);
                    result << ind << "        });\n";
                } else {
                    result << ind << "    auto __spawned = " << archetype_create_function_name(tmpl_id, program)
                           << "(registry);\n";
                    result << emit_spawn_overrides(
                        "__spawned", s.overrides, indent + 1, trait_names, program, pointer_aliases);
                }
                result << ind << "}\n";
                return result.str();
            } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                std::string target =
                    s.target_expr.has_value()
                        ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases, cpp_overrides)
                        : "entity";
                const bool GUARDED    = s.target_expr.has_value();
                const std::string cpp = EnttCodegenUtils::trait_cpp_name(s.resolved_trait_id, s.trait_name, program);
                if (!program.execution_graph.phases.empty()) {
                    std::ostringstream result;
                    result << ind << "{\n";
                    result << ind << "    const auto __target = " << target << ";\n";
                    result << ind << "    cactus::runtime::entt_backend::generated_queue_structural_command(\n";
                    result << ind << "        cactus::runtime::entt_backend::CactusStructuralCommand::Kind::Add,\n";
                    result << ind << "        [=](entt::registry& registry) mutable {\n";
                    result << ind << "            if (!registry.valid(__target)) { return; }\n";
                    result << ind << "            cancel_projected_" << cpp << "(__target);\n";
                    if (s.args.empty()) {
                        result << ind << "            registry.emplace_or_replace<" << cpp << ">(__target);\n";
                    } else {
                        result << ind << "            auto __existing = registry.try_get<" << cpp << ">(__target);\n";
                        result << ind << "            auto __value = __existing ? *__existing : " << cpp << "{};\n";
                        for (const auto& arg : s.args) {
                            result << ind << "            __value." << arg.name << " = "
                                   << rewrite_expr(*arg.value, trait_names, program, pointer_aliases, cpp_overrides)
                                   << ";\n";
                        }
                        result << ind << "            registry.emplace_or_replace<" << cpp << ">(__target, __value);\n";
                    }
                    result << ind << "        });\n";
                    result << ind << "}\n";
                    return result.str();
                }
                if (s.args.empty()) {
                    if (GUARDED) {
                        return ind + "if (registry.valid(" + target + ")) {\n" + ind + "    cancel_projected_" + cpp +
                               "(" + target + ");\n" + ind + "    registry.emplace_or_replace<" + cpp + ">(" + target +
                               ");\n" + ind + "}\n";
                    }
                    return ind + "cancel_projected_" + cpp + "(" + target + ");\n" + ind +
                           "registry.emplace_or_replace<" + cpp + ">(" + target + ");\n";
                }

                std::ostringstream result;
                if (GUARDED) {
                    result << ind << "if (registry.valid(" << target << ")) {\n";
                }
                result << ind << (GUARDED ? "    " : "") << "{\n";
                result << ind << (GUARDED ? "        " : "    ") << "cancel_projected_" << cpp << "(" << target
                       << ");\n";
                result << ind << (GUARDED ? "        " : "    ") << "auto __existing = registry.try_get<" << cpp << ">("
                       << target << ");\n";
                result << ind << (GUARDED ? "        " : "    ") << "auto __value = __existing ? *__existing : " << cpp
                       << "{};\n";
                for (const auto& arg : s.args) {
                    result << ind << (GUARDED ? "        " : "    ") << "__value." << arg.name << " = "
                           << rewrite_expr(*arg.value, trait_names, program, pointer_aliases, cpp_overrides) << ";\n";
                }
                result << ind << (GUARDED ? "        " : "    ") << "registry.emplace_or_replace<" << cpp << ">("
                       << target << ", __value);\n";
                result << ind << (GUARDED ? "    " : "") << "}\n";
                if (GUARDED) {
                    result << ind << "}\n";
                }
                return result.str();
            } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                std::string target =
                    s.target_expr.has_value()
                        ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases, cpp_overrides)
                        : "entity";
                const std::string cpp = EnttCodegenUtils::trait_cpp_name(s.resolved_trait_id, s.trait_name, program);
                if (!program.execution_graph.phases.empty()) {
                    return ind + "{\n" + ind + "    const auto __target = " + target + ";\n" + ind +
                           "    cactus::runtime::entt_backend::generated_queue_structural_command(\n" + ind +
                           "        cactus::runtime::entt_backend::CactusStructuralCommand::Kind::Remove,\n" + ind +
                           "        [=](entt::registry& registry) {\n" + ind +
                           "            if (!registry.valid(__target)) { return; }\n" + ind +
                           "            cancel_projected_" + cpp + "(__target);\n" + ind +
                           "            if (registry.all_of<" + cpp + ">(__target)) {\n" + ind +
                           "                registry.remove<" + cpp + ">(__target);\n" + ind + "            }\n" + ind +
                           "        });\n" + ind + "}\n";
                }
                if (s.target_expr.has_value()) {
                    return ind + "if (registry.valid(" + target + ")) {\n" + ind + "    cancel_projected_" + cpp + "(" +
                           target + ");\n" + ind + "    if (registry.all_of<" + cpp + ">(" + target + ")) {\n" + ind +
                           "        registry.remove<" + cpp + ">(" + target + ");\n" + ind + "    }\n" + ind + "}\n";
                }
                return ind + "cancel_projected_" + cpp + "(" + target + ");\n" + ind + "if (registry.all_of<" + cpp +
                       ">(" + target + ")) {\n" + ind + "    registry.remove<" + cpp + ">(" + target + ");\n" + ind +
                       "}\n";
            } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                const std::string target =
                    s.target_expr.has_value()
                        ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases, cpp_overrides)
                        : "entity";
                const std::string cpp = EnttCodegenUtils::trait_cpp_name(s.resolved_trait_id, s.trait_name, program);
                const auto simple     = s.trait_name.rfind('.') != std::string::npos
                                            ? s.trait_name.substr(s.trait_name.rfind('.') + 1)
                                            : s.trait_name;
                const auto* resolved_pt =
                    s.resolved_trait_id.has_value()
                        ? EnttCodegenUtils::find_trait(program, make_canonical_id(*s.resolved_trait_id))
                        : EnttCodegenUtils::find_trait(program, simple);
                const bool is_marker = resolved_pt == nullptr || resolved_pt->fields.empty();
                std::ostringstream result;
                result << ind << "if (registry.valid(" << target << ")) {\n";
                if (is_marker) {
                    result << ind << "    project_" << cpp << "(registry, " << target << ");\n";
                } else {
                    result << ind << "    [[maybe_unused]] auto& __projected = project_" << cpp << "(registry, "
                           << target << ");\n";
                    for (const auto& arg : s.args) {
                        result << ind << "    __projected." << arg.name << " = "
                               << rewrite_expr(*arg.value, trait_names, program, pointer_aliases, cpp_overrides)
                               << ";\n";
                    }
                }
                result << ind << "}\n";
                return result.str();
            } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                if (!program.execution_graph.phases.empty()) {
                    const std::string target =
                        s.target_expr.has_value()
                            ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases, cpp_overrides)
                            : "entity";
                    return ind + "{\n" + ind + "    const auto __target = " + target + ";\n" + ind +
                           "    cactus::runtime::entt_backend::generated_queue_structural_command(\n" + ind +
                           "        cactus::runtime::entt_backend::CactusStructuralCommand::Kind::Destroy,\n" + ind +
                           "        [=](entt::registry& registry) {\n" + ind +
                           "            if (registry.valid(__target)) {\n" + ind +
                           "                cactus_destroy_entity_recursive(registry, __target);\n" + ind +
                           "            }\n" + ind + "        });\n" + ind + "}\n";
                }
                if (s.target_expr.has_value()) {
                    std::string target =
                        rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases, cpp_overrides);
                    return ind + "if (registry.valid(" + target + ")) {\n" + ind +
                           "    cactus_destroy_entity_recursive(registry, " + target + ");\n" + ind + "}\n";
                }
                return ind + "cactus_destroy_entity_recursive(registry, entity);\n";
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) {
                    return ind + "return " +
                           rewrite_expr(**s.value, trait_names, program, pointer_aliases, cpp_overrides) + ";\n";
                }
                return ind + "return;\n";
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind + rewrite_expr(*s.expr, trait_names, program, pointer_aliases, cpp_overrides) + ";\n";
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                const auto condition = rewrite_expr(*s.condition, trait_names, program, pointer_aliases, cpp_overrides);
                std::string result   = ind + "if ";
                if (!condition.empty() && condition.front() == '(' && condition.back() == ')') {
                    result += condition;
                } else {
                    result += "(" + condition + ")";
                }
                result += " {\n";
                for (auto& inner : s.then_body) {
                    result += rewrite_stmt(
                        *inner, indent + 1, trait_names, program, pointer_aliases, dispatcher_available, cpp_overrides);
                }
                result += ind + "}";
                if (!s.else_body.empty()) {
                    result += " else {\n";
                    for (auto& inner : s.else_body) {
                        result += rewrite_stmt(*inner,
                                               indent + 1,
                                               trait_names,
                                               program,
                                               pointer_aliases,
                                               dispatcher_available,
                                               cpp_overrides);
                    }
                    result += ind + "}";
                }
                return result + "\n";
            } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                return emit_trait_match_stmt(
                    s, indent, trait_names, program, pointer_aliases, dispatcher_available, cpp_overrides);
            } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                const auto temp    = foreach_temp_name(s);
                std::string result = ind + "auto " + temp + " = " +
                                     rewrite_expr(*s.iterable, trait_names, program, pointer_aliases, cpp_overrides) +
                                     ";\n";
                result += ind + "for (const auto& " + s.var_name + " : " + temp + ") {\n";
                for (const auto& inner : s.body) {
                    result += rewrite_stmt(
                        *inner, indent + 1, trait_names, program, pointer_aliases, dispatcher_available, cpp_overrides);
                }
                result += ind + "}\n";
                return result;
            } else {
                return ind + "/* unsupported stmt */\n";
            }
        },
        stmt.stmt);
}

std::string EnttSystemEmitter::emit_system(  // NOLINT(readability-function-cognitive-complexity)
    const SystemNode& sys,
    const DecoratedProgram& program) {
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

    for (const auto& handler : sys.handlers) {
        const auto* contract       = graph_handler_contract(sys, handler, program);
        const bool selectionless   = contract != nullptr && contract->is_selectionless();
        const auto trigger_binding = handler_trigger_binding(handler);
        out << "void " << system_function_name(program.module_name, sys.name, handler_trigger_suffix(handler))
            << "(entt::registry& registry";
        out << ", const " << handler_trigger_cpp_type(handler, program) << "& " << trigger_binding;
        out << ") {\n";
        out << "    (void)" << trigger_binding << ";\n";

        if (selectionless) {
            out << "    (void)registry;\n";
            for (const auto& stmt : handler.body) {
                out << rewrite_stmt(*stmt, 1, filter_traits, program, {}, false, filter_cpp_overrides);
            }
        } else {
            emit_sort_call(out, sys);
            if (!filter_traits.empty()) {
                emit_view_declaration(out, filter_cpp_types, exclude_cpp_types, 1);
                emit_view_each_header(out, filter_bindings_list, 1, program);
                out << "        (void)entity;\n";
                emit_filter_alias_bindings(out, sys.filter, program, 2);
            } else {
                out << "    for (auto entity : registry.storage<entt::entity>()) {\n";
                out << "        (void)entity;\n";
                emit_storage_filter_skip(out, sys.filter, sys.exclude, program, 2);
            }

            // Emit body with proper component field access
            for (const auto& stmt : handler.body) {
                out << rewrite_stmt(*stmt, 2, filter_traits, program, {}, false, filter_cpp_overrides);
            }

            out << (filter_traits.empty() ? "    }\n" : "    });\n");
        }
        out << "}\n\n";
    }

    return out.str();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string EnttSystemEmitter::emit_extern_system(const ExternSystemNode& sys, const DecoratedProgram& program) {
    std::ostringstream out;

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
        out << "        world.rotation = cactus::runtime::stdlib::math::quat::multiply(parent_world.rotation, "
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
        out << "    BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << shape << "& "
            << shape << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        if (!" << shape << "_comp.visible) {\n";
        out << "            return;\n";
        out << "        }\n";
        out << "        switch (" << shape << "_comp.type) {\n";
        out << "            case " << shape_type << "::Rectangle:\n";
        out << "                DrawRectangleV(" << wt << "_comp.position,\n";
        out << "                               " << shape << "_comp.size,\n";
        out << "                               " << shape << "_comp.color);\n";
        out << "                break;\n";
        out << "        }\n";
        out << "    });\n";
        out << "    EndMode2D();\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_sprite_renderer(sys)) {
        const std::string wt       = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
        const std::string renderer = EnttCodegenUtils::trait_cpp_name("Renderer", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << wt << ", " << renderer << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << renderer
            << "& " << renderer << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_sprite(" << wt << "_comp.position, " << renderer
            << "_comp.size, " << renderer << "_comp.color, " << renderer << "_comp.texture, " << renderer
            << "_comp.visible, " << renderer << "_comp.layer);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_animated_sprite_system(sys)) {
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

    if (is_model_animation_system(sys)) {
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

    if (is_mesh_renderer(sys)) {
        const std::string wt       = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
        const std::string renderer = EnttCodegenUtils::trait_cpp_name("Renderer", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << wt << ", " << renderer << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << renderer
            << "& " << renderer << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_mesh(" << wt << "_comp.position, " << wt
            << "_comp.rotation, " << wt << "_comp.scale, " << renderer << "_comp.mesh, " << renderer
            << "_comp.material, " << renderer << "_comp.visible, " << renderer << "_comp.cast_shadow);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_model_renderer_system(sys)) {
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

    if (is_billboard_renderer(sys)) {
        const std::string wt = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
        const std::string br = EnttCodegenUtils::trait_cpp_name("BillboardRenderer", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << wt << ", " << br << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << br << "& "
            << br << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_billboard(" << wt << "_comp.position, " << br
            << "_comp.size, " << br << "_comp.color, " << br << "_comp.texture, " << br << "_comp.visible);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_point_light_system(sys)) {
        const std::string wt = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
        const std::string pl = EnttCodegenUtils::trait_cpp_name("PointLight", program);
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<" << wt << ", " << pl << ">();\n";
        out << "    view.each([&](entt::entity entity, const " << wt << "& " << wt << "_comp, const " << pl << "& "
            << pl << "_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::register_point_light(" << wt << "_comp.position, " << pl
            << "_comp.color, " << pl << "_comp.intensity, " << pl << "_comp.range, " << pl << "_comp.enabled);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_directional_light_system(sys)) {
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

    if (is_screen_label_system(sys)) {
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

    if (is_editor_extern_system(sys)) {
        if (symbol_is(sys.resolved_system_id, SymbolKind::System, "std.editor", "GizmoRenderer2D") &&
            !world_transform_is_volume(program)) {
            const bool has_box_collider = EnttCodegenUtils::has_trait(program, "std.physics.flat.BoxCollider");
            const std::string es        = EnttCodegenUtils::trait_cpp_name("EditorState", program);
            const std::string eg2d      = EnttCodegenUtils::trait_cpp_name("EditorGizmo2D", program);
            const std::string wt = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
            const std::string bc = EnttCodegenUtils::trait_cpp_name("std.physics.flat.BoxCollider", program);
            out << "void " << system_function_name(program.module_name, sys.name, "tick")
                << "(entt::registry& registry) {\n";
            out << "    bool __editor_active = false;\n";
            out << "    auto __estate_view = registry.view<" << es << ">();\n";
            out << "    for (auto __e : __estate_view) {\n";
            out << "        if (__estate_view.get<" << es << ">(__e).active) { __editor_active = true; break; }\n";
            out << "    }\n";
            out << "    if (!__editor_active) { return; }\n";
            out << "    BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());\n";
            out << "    auto view = registry.view<" << eg2d << ", " << wt << ">();\n";
            out << "    view.each([&](entt::entity entity, const " << eg2d << "& gizmo, const " << wt << "& xform) {\n";
            out << "        (void)entity;\n";
            out << "        Rectangle rect{.x = xform.position.x - 0.5F, .y = xform.position.y - 0.5F,\n";
            out << "                       .width = 1.0F, .height = 1.0F};\n";
            if (has_box_collider) {
                out << "        if (const auto* box = registry.try_get<" << bc << ">(entity)) {\n";
                out << "            rect = {.x = xform.position.x, .y = xform.position.y,\n";
                out << "                    .width = box->size.x, .height = box->size.y};\n";
                out << "        }\n";
            }
            out << "        DrawRectangleLinesEx(rect, 0.05F, gizmo.color);\n";
            out << "        const float arrow_len   = gizmo.size;\n";
            out << "        const float arrow_thick = arrow_len * 0.05F;\n";
            out << "        const Vector2 center    = xform.position;\n";
            out << "        if (gizmo.mode == 1) {\n";
            out << "            DrawLineEx(center, {.x = center.x + arrow_len, .y = center.y}, arrow_thick, RED);\n";
            out << "            DrawTriangle(\n";
            out << "                {.x = center.x + arrow_len - (arrow_len * 0.2F), .y = center.y - (arrow_len * "
                   "0.1F)},\n";
            out << "                {.x = center.x + arrow_len - (arrow_len * 0.2F), .y = center.y + (arrow_len * "
                   "0.1F)},\n";
            out << "                {.x = center.x + arrow_len, .y = center.y}, RED);\n";
            out << "            DrawLineEx(center, {.x = center.x, .y = center.y + arrow_len}, arrow_thick, GREEN);\n";
            out << "            DrawTriangle(\n";
            out << "                {.x = center.x - (arrow_len * 0.1F), .y = center.y + arrow_len - (arrow_len * "
                   "0.2F)},\n";
            out << "                {.x = center.x + (arrow_len * 0.1F), .y = center.y + arrow_len - (arrow_len * "
                   "0.2F)},\n";
            out << "                {.x = center.x, .y = center.y + arrow_len}, GREEN);\n";
            out << "        } else if (gizmo.mode == 2) {\n";
            out << "            DrawRing(center, gizmo.size * 0.8F, gizmo.size, 0.0F, 360.0F, 32, SKYBLUE);\n";
            out << "        } else if (gizmo.mode == 3) {\n";
            out << "            const float sq = arrow_len * 0.15F;\n";
            out << "            DrawLineEx(center, {.x = center.x + arrow_len, .y = center.y}, arrow_thick, RED);\n";
            out << "            DrawRectangleV(\n";
            out << "                {.x = center.x + arrow_len - (sq * 0.5F), .y = center.y - (sq * 0.5F)},\n";
            out << "                {.x = sq, .y = sq}, RED);\n";
            out << "            DrawLineEx(center, {.x = center.x, .y = center.y + arrow_len}, arrow_thick, GREEN);\n";
            out << "            DrawRectangleV(\n";
            out << "                {.x = center.x - (sq * 0.5F), .y = center.y + arrow_len - (sq * 0.5F)},\n";
            out << "                {.x = sq, .y = sq}, GREEN);\n";
            out << "        }\n";
            out << "    });\n";
            out << "    EndMode2D();\n";
            out << "}\n\n";
            return out.str();
        }
        if (symbol_is(sys.resolved_system_id, SymbolKind::System, "std.editor", "EditorTemplatePalette")) {
            const std::string es = EnttCodegenUtils::trait_cpp_name("EditorState", program);
            out << "void " << system_function_name(program.module_name, sys.name, "tick")
                << "(entt::registry& registry) {\n";
            out << "    auto __estate_view = registry.view<" << es << ">();\n";
            out << "    for (auto __ed_ent : __estate_view) {\n";
            out << "        auto& __es = __estate_view.get<" << es << ">(__ed_ent);\n";
            out << "        if (!__es.active) { continue; }\n";
            out << "        static std::unordered_map<std::string, Color> __tint_cache;\n";
            out << "        int __idx = 0;\n";
            out << "        for (const auto& [__name, __factory] : cactus_template_registry) {\n";
            out << "            if (!__tint_cache.contains(__name)) {\n";
            out << "                const float __hue = static_cast<float>(std::hash<std::string>{}(__name) % 360);\n";
            out << "                const float __h   = __hue / 60.0F;\n";
            out << "                const float __s   = 0.70F;\n";
            out << "                const float __l   = 0.55F;\n";
            out << "                const float __c   = (1.0F - std::abs((2.0F * __l) - 1.0F)) * __s;\n";
            out << "                const float __x   = __c * (1.0F - std::abs(std::fmod(__h, 2.0F) - 1.0F));\n";
            out << "                const float __m   = __l - (__c * 0.5F);\n";
            out << "                float __r = 0.0F;\n";
            out << "                float __g = 0.0F;\n";
            out << "                float __b = 0.0F;\n";
            out << "                switch (static_cast<int>(__h) % 6) {\n";
            out << "                    case 0: __r = __c; __g = __x; break;\n";
            out << "                    case 1: __r = __x; __g = __c; break;\n";
            out << "                    case 2: __g = __c; __b = __x; break;\n";
            out << "                    case 3: __g = __x; __b = __c; break;\n";
            out << "                    case 4: __r = __x; __b = __c; break;\n";
            out << "                    case 5: __r = __c; __b = __x; break;\n";
            out << "                    default: break;\n";
            out << "                }\n";
            out << "                __tint_cache[__name] = Color{\n";
            out << "                    .r = static_cast<unsigned char>((__r + __m) * 255.0F),\n";
            out << "                    .g = static_cast<unsigned char>((__g + __m) * 255.0F),\n";
            out << "                    .b = static_cast<unsigned char>((__b + __m) * 255.0F),\n";
            out << "                    .a = 200};\n";
            out << "            }\n";
            out << "            const Rectangle __btn = {.x = 10.0F,\n";
            out << "                                     .y = 40.0F + (static_cast<float>(__idx) * 30.0F),\n";
            out << "                                     .width = 140.0F, .height = 26.0F};\n";
            out << "            DrawRectangleRec(__btn, __tint_cache[__name]);\n";
            out << "            DrawText(__name.c_str(), static_cast<int>(__btn.x) + 6,\n";
            out << "                     static_cast<int>(__btn.y) + 6, 14, WHITE);\n";
            out << "            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&\n";
            out << "                CheckCollisionPointRec(GetMousePosition(), __btn)) {\n";
            out << "                __es.active_template = __name;\n";
            out << "                __es.mode = 4;\n";
            out << "            }\n";
            out << "            ++__idx;\n";
            out << "        }\n";
            out << "        break;\n";
            out << "    }\n";
            out << "}\n\n";
            return out.str();
        }
        if (symbol_is(sys.resolved_system_id, SymbolKind::System, "std.editor", "GizmoRenderer3D") &&
            world_transform_is_volume(program)) {
            const bool has_model_renderer = EnttCodegenUtils::has_trait(program, "ModelRenderer");
            const std::string es          = EnttCodegenUtils::trait_cpp_name("EditorState", program);
            const std::string eg3d        = EnttCodegenUtils::trait_cpp_name("EditorGizmo3D", program);
            const std::string wt = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
            const std::string mr = EnttCodegenUtils::trait_cpp_name("ModelRenderer", program);
            out << "void " << system_function_name(program.module_name, sys.name, "tick")
                << "(entt::registry& registry) {\n";
            out << "    bool __editor_active = false;\n";
            out << "    auto __estate_view = registry.view<" << es << ">();\n";
            out << "    for (auto __e : __estate_view) {\n";
            out << "        if (__estate_view.get<" << es << ">(__e).active) { __editor_active = true; break; }\n";
            out << "    }\n";
            out << "    if (!__editor_active) { return; }\n";
            out << "    BeginMode3D(cactus::runtime::entt_backend::get_active_camera_3d());\n";
            out << "    DrawGrid(20, 1.0F);\n";
            out << "    auto view = registry.view<" << eg3d << ", " << wt << ">();\n";
            out << "    view.each([&](entt::entity entity, const " << eg3d << "& gizmo, const " << wt << "& xform) {\n";
            out << "        (void)entity;\n";
            out << "        Vector3 box_center = xform.position;\n";
            out << "        Vector3 box_size{.x = 1.0F, .y = 1.0F, .z = 1.0F};\n";
            if (has_model_renderer) {
                out << "        if (const auto* renderer = registry.try_get<" << mr << ">(entity)) {\n";
                out << "            const BoundingBox bounds = "
                       "cactus::runtime::entt_backend::model_bounds_box(renderer->model);\n";
                out << "            const Vector3 extents{.x = bounds.max.x - bounds.min.x,\n";
                out << "                                  .y = bounds.max.y - bounds.min.y,\n";
                out << "                                  .z = bounds.max.z - bounds.min.z};\n";
                out << "            if (extents.x > 0.0F || extents.y > 0.0F || extents.z > 0.0F) {\n";
                out << "                box_size = Vector3{.x = extents.x * xform.scale.x,\n";
                out << "                                   .y = extents.y * xform.scale.y,\n";
                out << "                                   .z = extents.z * xform.scale.z};\n";
                out << "                box_center = Vector3{\n";
                out << "                    .x = xform.position.x + ((bounds.min.x + bounds.max.x) * 0.5F * "
                       "xform.scale.x),\n";
                out << "                    .y = xform.position.y + ((bounds.min.y + bounds.max.y) * 0.5F * "
                       "xform.scale.y),\n";
                out << "                    .z = xform.position.z + ((bounds.min.z + bounds.max.z) * 0.5F * "
                       "xform.scale.z)};\n";
                out << "            }\n";
                out << "        }\n";
            }
            out << "        DrawCubeWiresV(box_center, box_size, gizmo.color);\n";
            out << "        const Vector3 origin = xform.position;\n";
            out << "        const float axis_len = gizmo.size;\n";
            out << "        if (gizmo.mode == 1 || gizmo.mode == 3) {\n";
            out << "            DrawLine3D(origin, Vector3{.x = origin.x + axis_len, .y = origin.y, .z = origin.z}, "
                   "RED);\n";
            out << "            DrawLine3D(origin, Vector3{.x = origin.x, .y = origin.y + axis_len, .z = origin.z}, "
                   "GREEN);\n";
            out << "            DrawLine3D(origin, Vector3{.x = origin.x, .y = origin.y, .z = origin.z + axis_len}, "
                   "BLUE);\n";
            out << "        }\n";
            out << "        if (gizmo.mode == 2) {\n";
            out << "            DrawCircle3D(origin, axis_len, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 90.0F, "
                   "SKYBLUE);\n";
            out << "        }\n";
            out << "        if (gizmo.mode == 3) {\n";
            out << "            const float tip = axis_len * 0.15F;\n";
            out << "            DrawCubeV(Vector3{.x = origin.x + axis_len, .y = origin.y, .z = origin.z},\n";
            out << "                      Vector3{.x = tip, .y = tip, .z = tip}, RED);\n";
            out << "            DrawCubeV(Vector3{.x = origin.x, .y = origin.y + axis_len, .z = origin.z},\n";
            out << "                      Vector3{.x = tip, .y = tip, .z = tip}, GREEN);\n";
            out << "            DrawCubeV(Vector3{.x = origin.x, .y = origin.y, .z = origin.z + axis_len},\n";
            out << "                      Vector3{.x = tip, .y = tip, .z = tip}, BLUE);\n";
            out << "        }\n";
            out << "    });\n";
            out << "    EndMode3D();\n";
            out << "}\n\n";
            return out.str();
        }
        // EditorPropertyPanel and the dimension-mismatched gizmo renderer
        // (GizmoRenderer3D in flat programs, GizmoRenderer2D in volume) — stub
        out << "void " << system_function_name(program.module_name, sys.name, "tick")
            << "(entt::registry& registry) {\n";
        out << "    (void)registry;\n";
        out << "}\n\n";
        return out.str();
    }

    // When std.editor is imported it pulls in both std.transform.flat and
    // std.transform.volume, adding both TransformPropagation ExternSystemNodes
    // to the merged AST. The dimension-matching variant is handled above (early
    // return). The non-matching variant falls here and must be dropped so that
    // the generic emit path does not produce a duplicate function definition.
    if (symbol_is(sys.resolved_system_id, SymbolKind::System, "std.transform.flat", "TransformPropagation") ||
        symbol_is(sys.resolved_system_id, SymbolKind::System, "std.transform.volume", "TransformPropagation")) {
        return "";
    }

    throw std::runtime_error(
        "cpp-entt has no compiler-owned implementation for external system '" +
        (sys.resolved_system_id.has_value() ? make_canonical_id(*sys.resolved_system_id) : sys.name) +
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
