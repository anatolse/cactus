#include "backends/cpp-entt/system_emitter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

namespace {

std::string imported_module_name(const ProgramNode* ast, const std::string& qualifier) {
    if (ast == nullptr) {
        return qualifier;
    }
    for (const auto& decl : ast->declarations) {
        if (const auto* use = std::get_if<UseNode>(&decl)) {
            if ((use->alias.has_value() && *use->alias == qualifier) || use->module_name == qualifier) {
                return use->module_name;
            }
        }
    }
    return qualifier;
}

std::string stdlib_runtime_prefix(const ProgramNode* ast, const std::string& qualifier) {
    const std::string module_name = imported_module_name(ast, qualifier);
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
    if (module_name == "std.random") {
        return "cactus::runtime::stdlib::random";
    }
    if (module_name == "std.render.models") {
        return "cactus::runtime::entt_backend";
    }
    return {};
}

// std.render.models extern funcs bind to the model_-prefixed runtime bridges
// (the bare names would collide with other asset kinds' introspection).
std::string stdlib_runtime_func_name(const std::string& module_name, const std::string& func_name) {
    if (module_name == "std.render.models" &&
        (func_name == "animation_count" || func_name == "animation_name" || func_name == "bounds_size")) {
        return "model_" + func_name;
    }
    return func_name;
}

bool module_exports_stdlib_func(const std::string& module_name, const std::string& func_name) {
    if (module_name == "std.math") {
        return func_name == "lerp" || func_name == "clamp" || func_name == "abs" || func_name == "min" ||
               func_name == "max" || func_name == "sqrt" || func_name == "sin" || func_name == "cos" ||
               func_name == "atan2" || func_name == "floor" || func_name == "ceil" || func_name == "round" ||
               func_name == "pow";
    }
    if (module_name == "std.math.vec2") {
        return func_name == "length" || func_name == "normalize" || func_name == "dot" || func_name == "lerp" ||
               func_name == "distance" || func_name == "angle";
    }
    if (module_name == "std.math.vec3") {
        return func_name == "length" || func_name == "normalize" || func_name == "dot" || func_name == "cross" ||
               func_name == "lerp" || func_name == "distance" || func_name == "reflect";
    }
    if (module_name == "std.math.quat") {
        return func_name == "identity" || func_name == "from_euler" || func_name == "from_axis_angle" ||
               func_name == "forward" || func_name == "right" || func_name == "up" || func_name == "rotate" ||
               func_name == "slerp" || func_name == "multiply" || func_name == "inverse";
    }
    if (module_name == "std.input") {
        return func_name == "pressed" || func_name == "down" || func_name == "released" || func_name == "axis" ||
               func_name == "axis2" || func_name == "mouse_position";
    }
    if (module_name == "std.physics.flat") {
        return func_name == "query_cast_nearest" || func_name == "query_overlap_deepest" ||
               func_name == "query_overlap_all";
    }
    if (module_name == "std.editor") {
        return func_name == "editor_spawn_template" || func_name == "editor_hit_test_2d" ||
               func_name == "editor_raycast_3d" || func_name == "editor_screen_to_world_2d" ||
               func_name == "editor_mouse_delta_2d" || func_name == "editor_plane_project_3d" ||
               func_name == "editor_mouse_delta_3d";
    }
    if (module_name == "std.random") {
        return func_name == "seeded" || func_name == "uniform" || func_name == "uniform_int" ||
               func_name == "normal" || func_name == "advance" || func_name == "sample" ||
               func_name == "sample_int" || func_name == "sample_normal" || func_name == "chance";
    }
    if (module_name == "std.render.models") {
        return func_name == "animation_count" || func_name == "animation_name" || func_name == "bounds_size";
    }
    return false;
}

bool is_stdlib_physics_flat_query(const std::string& func_name) {
    return func_name == "query_cast_nearest" || func_name == "query_overlap_deepest" ||
           func_name == "query_overlap_all";
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

std::string lower_unqualified_stdlib_func(const DecoratedProgram& program,
                                          const std::string& func_name,
                                          const auto& emit_arg) {
    (void)emit_arg;
    if (program.ast == nullptr) {
        return {};
    }
    if (func_name == "format") {
        for (const auto& decl : program.ast->declarations) {
            const auto* use = std::get_if<UseNode>(&decl);
            if (use != nullptr && !use->alias.has_value() && use->module_name == "std.text") {
                return "std::format";
            }
        }
    }
    for (const auto& decl : program.ast->declarations) {
        const auto* use = std::get_if<UseNode>(&decl);
        if (use == nullptr || use->alias.has_value()) {
            continue;
        }
        if (!module_exports_stdlib_func(use->module_name, func_name)) {
            continue;
        }
        const std::string prefix = stdlib_runtime_prefix(program.ast, use->module_name);
        if (prefix.empty()) {
            continue;
        }
        if (use->module_name == "std.physics.flat" && is_stdlib_physics_flat_query(func_name)) {
            continue;
        }
        const std::string runtime_name = stdlib_runtime_func_name(use->module_name, func_name);
        std::string result;
        result.reserve(prefix.size() + runtime_name.size() + 2U);
        result.append(prefix).append("::").append(runtime_name);
        return result;
    }
    return {};
}

std::string lower_stdlib_member_call(const MemberExpr& member,
                                     const std::vector<std::unique_ptr<ExprNode>>& args,
                                     const DecoratedProgram& program,
                                     const std::unordered_set<std::string>& pointer_aliases,
                                     const auto& emit_arg,
                                     const std::vector<std::string>& trait_names) {
    (void)pointer_aliases;
    (void)trait_names;
    const auto* object = std::get_if<IdentExpr>(&member.object->expr);
    if (object == nullptr) {
        return {};
    }
    if (member.member == "format" && imported_module_name(program.ast, object->name) == "std.text") {
        std::string result = "std::format(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            result += emit_arg(*args[i]);
        }
        return result + ")";
    }
    const std::string prefix = stdlib_runtime_prefix(program.ast, object->name);
    if (prefix.empty()) {
        return {};
    }
    if (imported_module_name(program.ast, object->name) == "std.physics.flat" &&
        is_stdlib_physics_flat_query(member.member)) {
        return stdlib_physics_flat_query_call(member.member, args, emit_arg);
    }
    const std::string runtime_name =
        stdlib_runtime_func_name(imported_module_name(program.ast, object->name), member.member);
    std::string result;
    result.reserve(prefix.size() + runtime_name.size() + 3U);
    result.append(prefix).append("::").append(runtime_name).push_back('(');
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
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

std::string system_function_name(const std::string& system_name, const std::string& suffix) {
    return snake_case(system_name) + "_" + suffix;
}

std::string archetype_create_function_name(const std::string& archetype_name) {
    return "create_" + snake_case(archetype_name);
}

std::string event_cpp_type(const std::string& event_name) {
    if (event_name == "tick") {
        return "TickEvent";
    }
    if (event_name == "input") {
        return "InputEvent";
    }
    return event_name + "Event";
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

struct FilterBinding {
    std::string trait_name;
    std::string binding_name;
};

std::vector<FilterBinding> filter_bindings(const FilterClause& filter) {
    std::vector<FilterBinding> result;
    std::unordered_set<std::string> seen_traits;
    for (const auto& entry : filter.entries) {
        const auto trait_name = filter_simple_name(entry);
        if (seen_traits.insert(trait_name).second) {
            result.push_back(FilterBinding{.trait_name = trait_name, .binding_name = trait_name + "_comp"});
        }
        if (entry.alias.has_value()) {
            result.push_back(FilterBinding{.trait_name = trait_name, .binding_name = *entry.alias});
        }
    }
    for (const auto& trait_name : filter.trait_names) {
        if (seen_traits.insert(trait_name).second) {
            result.push_back(FilterBinding{.trait_name = trait_name, .binding_name = trait_name + "_comp"});
        }
    }
    return result;
}

std::vector<std::string> filter_trait_names(const FilterClause& filter) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& binding : filter_bindings(filter)) {
        if (seen.insert(binding.trait_name).second) {
            result.push_back(binding.trait_name);
        }
    }
    return result;
}

template <typename FilterLike>
bool filter_has_trait(const FilterLike& filter, const std::string& qualified, const std::string& simple) {
    for (const auto& entry : filter.entries) {
        if (entry.qualified_name == qualified || filter_simple_name(entry) == simple) {
            return true;
        }
    }
    return std::any_of(
        filter.trait_names.begin(), filter.trait_names.end(), [&](const auto& name) { return name == simple; });
}

template <typename FilterLike>
bool filter_has_exact_trait(const FilterLike& filter, const std::string& qualified) {
    return std::any_of(filter.entries.begin(), filter.entries.end(), [&](const auto& entry) {
        return entry.qualified_name == qualified;
    });
}

template <typename FilterLike>
bool filter_has_simple_trait(const FilterLike& filter, const std::string& simple) {
    return std::any_of(filter.entries.begin(),
                       filter.entries.end(),
                       [&](const auto& entry) { return filter_simple_name(entry) == simple; }) ||
           std::any_of(
               filter.trait_names.begin(), filter.trait_names.end(), [&](const auto& name) { return name == simple; });
}

enum class TransformFlavor : std::uint8_t {
    Unknown,
    Flat,
    Volume,
};

const ResolvedTrait* find_trait(const DecoratedProgram& program, const std::string& name) {
    auto it = program.traits.find(name);
    if (it == program.traits.end()) {
        return nullptr;
    }
    return &it->second;
}

const ResolvedField* find_field(const ResolvedTrait* trait, const std::string& field_name) {
    if (trait == nullptr) {
        return nullptr;
    }
    auto it = std::ranges::find_if(trait->fields, [&](const auto& field) { return field.name == field_name; });
    if (it == trait->fields.end()) {
        return nullptr;
    }
    return &*it;
}

TransformFlavor transform_flavor_for_trait(const ResolvedTrait* trait) {
    const auto* position = find_field(trait, "position");
    const auto* rotation = find_field(trait, "rotation");
    const auto* scale    = find_field(trait, "scale");
    if (position == nullptr || rotation == nullptr || scale == nullptr) {
        return TransformFlavor::Unknown;
    }

    if (position->type.kind == TypeKind::Vec2 && rotation->type.kind == TypeKind::Float &&
        scale->type.kind == TypeKind::Vec2) {
        return TransformFlavor::Flat;
    }
    if (position->type.kind == TypeKind::Vec3 && rotation->type.kind == TypeKind::Quat &&
        scale->type.kind == TypeKind::Vec3) {
        return TransformFlavor::Volume;
    }
    return TransformFlavor::Unknown;
}

TransformFlavor infer_transform_propagation_flavor(const ExternSystemNode& sys, const DecoratedProgram& program) {
    const bool has_flat_qualified   = filter_has_exact_trait(sys.filter, "std.transform.flat.LocalTransform") ||
                                      filter_has_exact_trait(sys.filter, "std.transform.flat.WorldTransform");
    const bool has_volume_qualified = filter_has_exact_trait(sys.filter, "std.transform.volume.LocalTransform") ||
                                      filter_has_exact_trait(sys.filter, "std.transform.volume.WorldTransform");

    if (has_flat_qualified != has_volume_qualified) {
        return has_flat_qualified ? TransformFlavor::Flat : TransformFlavor::Volume;
    }

    if (!filter_has_simple_trait(sys.filter, "LocalTransform") ||
        !filter_has_simple_trait(sys.filter, "WorldTransform")) {
        return TransformFlavor::Unknown;
    }

    const auto local_flavor = transform_flavor_for_trait(find_trait(program, "LocalTransform"));
    const auto world_flavor = transform_flavor_for_trait(find_trait(program, "WorldTransform"));

    if (local_flavor != TransformFlavor::Unknown) {
        return local_flavor;
    }
    return world_flavor;
}

bool uses_stdlib_extern_contract(const ExternSystemNode& sys) {
    if (sys.is_stdlib) {
        return true;
    }
    if (sys.name == "TransformPropagation" || sys.name == "ShapeRenderer" || sys.name == "SpriteRenderer" ||
        sys.name == "AnimatedSpriteSystem" || sys.name == "MeshRenderer" || sys.name == "ModelRendererSystem" ||
        sys.name == "ModelAnimationSystem" || sys.name == "BillboardRenderer" || sys.name == "PointLightSystem" ||
        sys.name == "DirectionalLightSystem" || sys.name == "TextRenderer2D" || sys.name == "TextRenderer3D" ||
        sys.name == "ScreenLabelSystem" || sys.name == "GizmoRenderer2D" || sys.name == "GizmoRenderer3D" ||
        sys.name == "EditorTemplatePalette" || sys.name == "EditorPropertyPanel") {
        return true;
    }
    return std::ranges::any_of(sys.filter.entries,
                               [](const auto& entry) { return entry.qualified_name.rfind("std.", 0) == 0; });
}

bool is_flat_transform_propagation(const ExternSystemNode& sys, const DecoratedProgram& program) {
    return uses_stdlib_extern_contract(sys) && sys.name == "TransformPropagation" &&
           infer_transform_propagation_flavor(sys, program) == TransformFlavor::Flat;
}

bool is_volume_transform_propagation(const ExternSystemNode& sys, const DecoratedProgram& program) {
    return uses_stdlib_extern_contract(sys) && sys.name == "TransformPropagation" &&
           infer_transform_propagation_flavor(sys, program) == TransformFlavor::Volume;
}

bool is_shape_renderer(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "ShapeRenderer" &&
           filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "std.render.shapes.Shape", "Shape");
}

bool is_sprite_renderer(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "SpriteRenderer" &&
           filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "std.render.sprites.Renderer", "Renderer");
}

bool is_animated_sprite_system(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "AnimatedSpriteSystem" &&
           filter_has_trait(sys.filter, "std.render.sprites.AnimatedSprite", "AnimatedSprite");
}

bool is_mesh_renderer(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "MeshRenderer" &&
           filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "std.render.meshes.Renderer", "Renderer");
}

bool is_model_renderer_system(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "ModelRendererSystem" &&
           filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "std.render.models.ModelRenderer", "ModelRenderer");
}

bool is_model_animation_system(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "ModelAnimationSystem" &&
           filter_has_trait(sys.filter, "std.render.models.ModelRenderer", "ModelRenderer") &&
           filter_has_trait(sys.filter, "std.render.models.ModelAnimator", "ModelAnimator");
}

bool is_billboard_renderer(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "BillboardRenderer" &&
           filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "std.render.meshes.BillboardRenderer", "BillboardRenderer");
}

bool is_point_light_system(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "PointLightSystem" &&
           filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "std.render.meshes.PointLight", "PointLight");
}

bool is_directional_light_system(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "DirectionalLightSystem" &&
           filter_has_trait(sys.filter, "std.render.meshes.DirectionalLight", "DirectionalLight");
}

bool world_transform_is_volume(const DecoratedProgram& program) {
    auto it = program.traits.find("WorldTransform");
    if (it == program.traits.end()) {
        return false;
    }
    return std::any_of(it->second.fields.begin(), it->second.fields.end(), [](const auto& f) {
        return f.name == "rotation" && f.type.kind == TypeKind::Quat;
    });
}

bool is_any_text_renderer_2d(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "TextRenderer2D" &&
           filter_has_trait(sys.filter, "std.render.text.TextLabel", "TextLabel");
}

bool is_any_text_renderer_3d(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "TextRenderer3D" &&
           filter_has_trait(sys.filter, "std.render.text.TextLabel", "TextLabel");
}

bool is_screen_label_system(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) && sys.name == "ScreenLabelSystem" &&
           filter_has_trait(sys.filter, "std.render.text.ScreenLabel", "ScreenLabel");
}

bool is_editor_extern_system(const ExternSystemNode& sys) {
    return uses_stdlib_extern_contract(sys) &&
           (sys.name == "EditorTemplatePalette" || sys.name == "EditorPropertyPanel" || sys.name == "GizmoRenderer2D" ||
            sys.name == "GizmoRenderer3D");
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
                              int indent) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    for (const auto& trait_name : filter_trait_names(filter)) {
        out << ind << "if (!registry.all_of<" << trait_name << ">(entity)) {\n";
        out << ind << "    continue;\n";
        out << ind << "}\n";
    }
    for (const auto& trait_name : filter_trait_names(exclude)) {
        out << ind << "if (registry.all_of<" << trait_name << ">(entity)) {\n";
        out << ind << "    continue;\n";
        out << ind << "}\n";
    }
}

void emit_filter_alias_bindings(std::ostringstream& out, const FilterClause& filter, int indent) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    for (const auto& binding : filter_bindings(filter)) {
        if (binding.binding_name == binding.trait_name + "_comp") {
            continue;
        }
        out << ind << "[[maybe_unused]] auto& " << binding.binding_name << " = " << binding.trait_name << "_comp;\n";
    }
}

void emit_view_declaration(std::ostringstream& out,
                           const std::vector<std::string>& filter_traits,
                           const std::vector<std::string>& exclude_traits,
                           int indent) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    out << ind << "auto view = registry.view<";
    for (size_t i = 0; i < filter_traits.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << filter_traits[i];
    }
    out << ">(";
    if (!exclude_traits.empty()) {
        out << "entt::exclude<";
        for (size_t i = 0; i < exclude_traits.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << exclude_traits[i];
        }
        out << ">";
    }
    out << ");\n";
}

void emit_view_each_header(std::ostringstream& out,
                           const std::vector<std::string>& filter_traits,
                           int indent,
                           const DecoratedProgram& program) {
    const std::string ind(static_cast<size_t>(indent) * 4, ' ');
    out << ind << "view.each([&](entt::entity entity";
    for (const auto& trait_name : filter_traits) {
        // EnTT does not pass empty (marker) components to view.each lambdas.
        auto it = program.traits.find(trait_name);
        const bool is_empty = it == program.traits.end() || it->second.fields.empty();
        if (!is_empty) {
            out << ", [[maybe_unused]] " << trait_name << "& " << trait_name << "_comp";
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
        auto it = program.traits.find(tn);
        if (it != program.traits.end()) {
            for (const auto& f : it->second.fields) {
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
        auto it = program.traits.find(tn);
        if (it != program.traits.end()) {
            for (const auto& f : it->second.fields) {
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
                                const std::unordered_set<std::string>& pointer_aliases = {});

static std::string rewrite_stmt(const StmtNode& stmt,
                                int indent,
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases = {},
                                bool dispatcher_available                               = false);

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

        out << ind << "{\n";
        out << ind << "    auto __existing = registry.try_get<" << override_entry.trait_name << ">(" << entity_name
            << ");\n";
        out << ind << "    auto __value = __existing ? *__existing : " << override_entry.trait_name << "{};\n";
        for (const auto& assignment : override_entry.assignments) {
            out << ind << "    __value." << assignment.name << " = "
                << rewrite_expr(*assignment.value, trait_names, program, pointer_aliases) << ";\n";
        }
        out << ind << "    registry.emplace_or_replace<" << override_entry.trait_name << ">(" << entity_name
            << ", __value);\n";
        out << ind << "}\n";
    }
    return out.str();
}

// ── Hierarchical spawn expansion (dsl-hierarchical-entity-templates D9) ─────

// Per-node creation helper names emitted by the codegen for hierarchical
// archetypes (create_<t>__node, create_<t>__node__<role path>).
static std::string archetype_node_create_function_name(const std::string& archetype_name,
                                                       const std::vector<std::string>& role_path) {
    std::string name = "create_" + snake_case(archetype_name) + "__node";
    for (const auto& role : role_path) {
        name += "__" + snake_case(role);
    }
    return name;
}

static const std::vector<ChildArchetypeNode>* find_template_children(const DecoratedProgram& program,
                                                                     const std::string& template_name) {
    if (program.ast == nullptr) {
        return nullptr;
    }
    for (const auto& decl : program.ast->declarations) {
        const auto* tmpl = std::get_if<TemplateNode>(&decl);
        if (tmpl != nullptr && tmpl->name == template_name && !tmpl->children.empty()) {
            return &tmpl->children;
        }
    }
    return nullptr;
}

// Spawn sites with child overrides expand the tree inline: create each node
// via its per-node helper in parent-first preorder, emplace Parent on non-root
// nodes, and apply that node's overrides in handler scope.
static void emit_spawn_child_expansion(std::ostringstream& out,
                                       const std::string& template_name,
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
        out << "    auto " << var << " = " << archetype_node_create_function_name(template_name, role_path)
            << "(registry);\n";
        out << "    registry.emplace_or_replace<Parent>(" << var << ", Parent{.parent = " << parent_var << "});\n";

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
                                   template_name,
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

static std::string emit_hierarchical_spawn_expansion(const std::string& template_name,
                                                     const std::vector<ArchetypeTraitEntry>& root_overrides,
                                                     const std::vector<ChildOverrideNode>& child_overrides,
                                                     const std::vector<ChildArchetypeNode>& children,
                                                     const std::vector<std::string>& trait_names,
                                                     const DecoratedProgram& program,
                                                     const std::unordered_set<std::string>& pointer_aliases) {
    std::ostringstream out;
    std::vector<std::string> role_path;
    out << "([&]() {\n";
    out << "    auto __spawned = " << archetype_node_create_function_name(template_name, role_path)
        << "(registry);\n";
    out << emit_spawn_overrides("__spawned", root_overrides, 1, trait_names, program, pointer_aliases);
    emit_spawn_child_expansion(out,
                               template_name,
                               children,
                               child_overrides,
                               "__spawned",
                               "__child",
                               role_path,
                               trait_names,
                               program,
                               pointer_aliases);
    out << "    return __spawned;\n";
    out << "})()";
    return out.str();
}

static std::string emit_spawn_expression(const SpawnExpr& spawn,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases) {
    if (!spawn.child_overrides.empty()) {
        if (const auto* children = find_template_children(program, spawn.template_name)) {
            return emit_hierarchical_spawn_expansion(
                spawn.template_name, spawn.overrides, spawn.child_overrides, *children, trait_names, program,
                pointer_aliases);
        }
    }
    std::ostringstream out;
    out << "([&]() {\n";
    out << "    auto __spawned = " << archetype_create_function_name(spawn.template_name) << "(registry);\n";
    out << emit_spawn_overrides("__spawned", spawn.overrides, 1, trait_names, program, pointer_aliases);
    out << "    return __spawned;\n";
    out << "})()";
    return out.str();
}

// ── Query call lowering ─────────────────────────────────────────────────────

static std::optional<std::string> extract_dotted_qualifier(const ExprNode& expr) {
    if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
        return ident->name;
    }
    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        if (auto prefix = extract_dotted_qualifier(*member->object)) {
            return *prefix + "." + member->member;
        }
    }
    return std::nullopt;
}

static bool is_known_query_module_name(const std::string& name) {
    return name == "std.query" || name == "std.physics.flat.query" || name == "std.physics.volume.query";
}

// Returns the full module name if `use` declares a known query module whose qualifier or alias
// matches `qualifier`; otherwise returns empty.
static std::string match_query_use_node(const UseNode& use, const std::string& qualifier) {
    if (!is_known_query_module_name(use.module_name)) {
        return {};
    }
    if (use.alias.has_value() && *use.alias == qualifier) {
        return use.module_name;
    }
    if (!use.alias.has_value()) {
        const auto last_dot      = use.module_name.rfind('.');
        const std::string last_comp = (last_dot != std::string::npos)
                                          ? use.module_name.substr(last_dot + 1)
                                          : use.module_name;
        if (last_comp == qualifier) {
            return use.module_name;
        }
    }
    return {};
}

static std::string resolve_query_module(const QueryCallExpr& qcall, const ProgramNode* ast) {
    const auto* member = std::get_if<MemberExpr>(&qcall.callee->expr);
    if (member == nullptr) {
        return {};
    }
    const auto qualifier = extract_dotted_qualifier(*member->object);
    if (!qualifier.has_value()) {
        return {};
    }
    if (is_known_query_module_name(*qualifier)) {
        return *qualifier;
    }
    if (ast == nullptr) {
        return {};
    }
    for (const auto& decl : ast->declarations) {
        const auto* use = std::get_if<UseNode>(&decl);
        if (use == nullptr) {
            continue;
        }
        if (auto matched = match_query_use_node(*use, *qualifier); !matched.empty()) {
            return matched;
        }
    }
    return {};
}

static std::string get_query_func_name_from_call(const QueryCallExpr& qcall) {
    const auto* member = std::get_if<MemberExpr>(&qcall.callee->expr);
    return member != nullptr ? member->member : std::string{};
}

// Build "<T1, T2>(entt::exclude<N1, N2>)" from filter predicates.
// prepend_include is added first and deduplicated against positive filters.
static std::string build_view_suffix(const std::vector<QueryFilterPredicate>& filters,
                                     const std::string& prepend_include = {}) {
    std::string include_list = prepend_include;
    std::string exclude_list;
    for (const auto& f : filters) {
        if (f.negated) {
            if (!exclude_list.empty()) {
                exclude_list += ", ";
            }
            exclude_list += f.trait_name;
        } else if (f.trait_name != prepend_include) {
            if (!include_list.empty()) {
                include_list += ", ";
            }
            include_list += f.trait_name;
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
                                        const auto& emit_arg) {
    const std::string view = "registry.view" + build_view_suffix(qcall.filters);
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
        return "[&]{ std::vector<entt::entity> __r; for (auto __e : " + view +
               ") __r.push_back(__e); return __r; }()";
    }
    if (func_name == "parent") {
        const std::string of_expr = find_named_arg_value(qcall.named_args, "of", emit_arg);
        return "[&]{ if (auto* __p = registry.try_get<Parent>(" + of_expr +
               "); __p != nullptr) return __p->parent; return entt::entity{entt::null}; }()";
    }
    return "/* unsupported std.query func: " + func_name + " */";
}

static std::string lower_flat_spatial_query(const QueryCallExpr& qcall,
                                            const std::string& func_name,
                                            const auto& emit_arg) {
    const std::string view = "registry.view" + build_view_suffix(qcall.filters, "WorldTransform");
    if (func_name == "nearest") {
        const std::string from = find_named_arg_value(qcall.named_args, "from", emit_arg);
        return "[&]{ const auto __from = (" + from + "); "
               "entt::entity __best{entt::null}; float __best_d = std::numeric_limits<float>::max(); "
               "for (auto __e : " + view + ") { "
               "const auto& __wt = registry.get<WorldTransform>(__e); "
               "float __dx = __wt.position.x - __from.x, "
               "__dy = __wt.position.y - __from.y; "
               "float __d = __dx * __dx + __dy * __dy; "
               "if (__d < __best_d) { __best_d = __d; __best = __e; } } return __best; }()";
    }
    if (func_name == "overlap_box") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string size   = find_named_arg_value(qcall.named_args, "size", emit_arg);
        return "[&]{ const auto __ct = (" + center + "); const auto __sz = (" + size + "); "
               "std::vector<entt::entity> __r; "
               "for (auto __e : " + view + ") { "
               "const auto& __wt = registry.get<WorldTransform>(__e); "
               "float __hx = __sz.x * 0.5F, __hy = __sz.y * 0.5F; "
               "if (std::abs(__wt.position.x - __ct.x) <= __hx && "
               "std::abs(__wt.position.y - __ct.y) <= __hy) "
               "__r.push_back(__e); } return __r; }()";
    }
    if (func_name == "overlap_circle") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string radius = find_named_arg_value(qcall.named_args, "radius", emit_arg);
        return "[&]{ const auto __ct = (" + center + "); const float __rad = (" + radius + "); "
               "std::vector<entt::entity> __r; "
               "for (auto __e : " + view + ") { "
               "const auto& __wt = registry.get<WorldTransform>(__e); "
               "float __dx = __wt.position.x - __ct.x, "
               "__dy = __wt.position.y - __ct.y; "
               "if ((__dx * __dx + __dy * __dy) <= __rad * __rad) "
               "__r.push_back(__e); } return __r; }()";
    }
    if (func_name == "raycast") {
        const std::string origin   = find_named_arg_value(qcall.named_args, "origin", emit_arg);
        const std::string dir      = find_named_arg_value(qcall.named_args, "dir", emit_arg);
        const std::string max_dist = find_named_arg_value(qcall.named_args, "max_dist", emit_arg);
        return "[&]{ const auto __org = (" + origin + "); const auto __dir = (" + dir + "); const float __md = (" + max_dist + "); "
               "entt::entity __best{entt::null}; float __best_d = std::numeric_limits<float>::max(); "
               "for (auto __e : " + view + ") { "
               "const auto& __wt = registry.get<WorldTransform>(__e); "
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
                                              const auto& emit_arg) {
    const std::string view = "registry.view" + build_view_suffix(qcall.filters, "WorldTransform");
    if (func_name == "nearest") {
        const std::string from = find_named_arg_value(qcall.named_args, "from", emit_arg);
        return "[&]{ const auto __from = (" + from + "); "
               "entt::entity __best{entt::null}; float __best_d = std::numeric_limits<float>::max(); "
               "for (auto __e : " + view + ") { "
               "const auto& __wt = registry.get<WorldTransform>(__e); "
               "float __dx = __wt.position.x - __from.x, "
               "__dy = __wt.position.y - __from.y, "
               "__dz = __wt.position.z - __from.z; "
               "float __d = __dx * __dx + __dy * __dy + __dz * __dz; "
               "if (__d < __best_d) { __best_d = __d; __best = __e; } } return __best; }()";
    }
    if (func_name == "overlap_box") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string size   = find_named_arg_value(qcall.named_args, "size", emit_arg);
        return "[&]{ const auto __ct = (" + center + "); const auto __sz = (" + size + "); "
               "std::vector<entt::entity> __r; "
               "for (auto __e : " + view + ") { "
               "const auto& __wt = registry.get<WorldTransform>(__e); "
               "float __hx = __sz.x * 0.5F, __hy = __sz.y * 0.5F, __hz = __sz.z * 0.5F; "
               "if (std::abs(__wt.position.x - __ct.x) <= __hx && "
               "std::abs(__wt.position.y - __ct.y) <= __hy && "
               "std::abs(__wt.position.z - __ct.z) <= __hz) "
               "__r.push_back(__e); } return __r; }()";
    }
    if (func_name == "overlap_sphere") {
        const std::string center = find_named_arg_value(qcall.named_args, "center", emit_arg);
        const std::string radius = find_named_arg_value(qcall.named_args, "radius", emit_arg);
        return "[&]{ const auto __ct = (" + center + "); const float __rad = (" + radius + "); "
               "std::vector<entt::entity> __r; "
               "for (auto __e : " + view + ") { "
               "const auto& __wt = registry.get<WorldTransform>(__e); "
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
        return "[&]{ const auto __org = (" + origin + "); const auto __dir = (" + dir + "); const float __md = (" + max_dist + "); "
               "entt::entity __best{entt::null}; float __best_d = std::numeric_limits<float>::max(); "
               "for (auto __e : " + view + ") { "
               "const auto& __wt = registry.get<WorldTransform>(__e); "
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

static std::string lower_query_call_expr(const QueryCallExpr& qcall,
                                         const DecoratedProgram& program,
                                         const auto& emit_arg) {
    const std::string module    = resolve_query_module(qcall, program.ast);
    const std::string func_name = get_query_func_name_from_call(qcall);
    if (module == "std.query") {
        return lower_ecs_query_call(qcall, func_name, emit_arg);
    }
    if (module == "std.physics.flat.query") {
        return lower_flat_spatial_query(qcall, func_name, emit_arg);
    }
    if (module == "std.physics.volume.query") {
        return lower_volume_spatial_query(qcall, func_name, emit_arg);
    }
    return "/* unresolved query module */";
}

static std::string emit_trait_match_stmt(const TraitMatchStmt& match_stmt,
                                         int indent,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases = {},
                                         bool dispatcher_available = false);

static std::string rewrite_expr(const ExprNode& expr,  // NOLINT(readability-function-cognitive-complexity)
                                const std::vector<std::string>& trait_names,
                                const DecoratedProgram& program,
                                const std::unordered_set<std::string>& pointer_aliases) {
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
                // If it's a known trait field, qualify it
                if (known_fields.contains(e.name)) {
                    auto comp = find_comp_for_field(e.name, trait_names, program);
                    if (!comp.empty()) {
                        return comp + "_comp." + e.name;
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
                return "(" + rewrite_expr(*e.left, trait_names, program, pointer_aliases) + " " + op + " " +
                       rewrite_expr(*e.right, trait_names, program, pointer_aliases) + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") {
                    op = "!";
                }
                return op + rewrite_expr(*e.operand, trait_names, program, pointer_aliases);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                if (auto* ident = std::get_if<IdentExpr>(&e.callee->expr);
                    ident != nullptr && ident->name == "exists" && e.args.size() == 1) {
                    return "registry.valid(" + rewrite_expr(*e.args[0], trait_names, program, pointer_aliases) + ")";
                }
                if (const auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
                    if (program.ast != nullptr) {
                        for (const auto& decl : program.ast->declarations) {
                            const auto* use = std::get_if<UseNode>(&decl);
                            if (use != nullptr && !use->alias.has_value() && use->module_name == "std.physics.flat" &&
                                is_stdlib_physics_flat_query(ident->name)) {
                                return stdlib_physics_flat_query_call(ident->name, e.args, [&](const ExprNode& arg) {
                                    return rewrite_expr(arg, trait_names, program, pointer_aliases);
                                });
                            }
                        }
                    }
                    if (const auto lowered_name = lower_unqualified_stdlib_func(
                            program,
                            ident->name,
                            [&](const ExprNode& arg) {
                                return rewrite_expr(arg, trait_names, program, pointer_aliases);
                            });
                        !lowered_name.empty()) {
                        // editor_spawn_template and editor_hit_test_2d need registry as first arg
                        const bool needs_registry = (ident->name == "editor_spawn_template" ||
                                                     ident->name == "editor_hit_test_2d");
                        std::string result = lowered_name + "(";
                        if (needs_registry) {
                            result += "registry";
                        }
                        for (size_t i = 0; i < e.args.size(); ++i) {
                            if (i > 0 || needs_registry) {
                                result += ", ";
                            }
                            result += rewrite_expr(*e.args[i], trait_names, program, pointer_aliases);
                        }
                        return result + ")";
                    }
                }
                if (const auto* member = std::get_if<MemberExpr>(&e.callee->expr)) {
                    if (const auto lowered = lower_stdlib_member_call(
                            *member,
                            e.args,
                            program,
                            pointer_aliases,
                            [&](const ExprNode& arg) {
                                return rewrite_expr(arg, trait_names, program, pointer_aliases);
                            },
                            trait_names);
                        !lowered.empty()) {
                        return lowered;
                    }
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
                                result += rewrite_expr(*e.args[i], trait_names, program, pointer_aliases);
                            }
                            return result + ")";
                        }
                    }
                }
                std::string result = rewrite_expr(*e.callee, trait_names, program, pointer_aliases) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += rewrite_expr(*e.args[i], trait_names, program, pointer_aliases);
                }
                return result + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (const auto* enum_member = std::get_if<MemberExpr>(&e.object->expr)) {
                    if (const auto* module_ident = std::get_if<IdentExpr>(&enum_member->object->expr)) {
                        if (imported_module_name(program.ast, module_ident->name) == "std.physics.flat" &&
                            program.enums.contains(enum_member->member)) {
                            return enum_member->member + "::" + e.member;
                        }
                    }
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    // Enum names — use :: notation
                    if (program.enums.contains(ident->name)) {
                        return ident->name + "::" + e.member;
                    }
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (pointer_aliases.contains(ident->name)) {
                        return ident->name + "->" + e.member;
                    }
                }
                return rewrite_expr(*e.object, trait_names, program, pointer_aliases) + "." + e.member;
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                return emit_spawn_expression(e, trait_names, program, pointer_aliases);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                std::string result = "std::vector{";
                for (size_t i = 0; i < e.elements.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += rewrite_expr(*e.elements[i], trait_names, program, pointer_aliases);
                }
                result += "}";
                return result;
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                return lower_query_call_expr(e, program, [&](const ExprNode& arg) {
                    return rewrite_expr(arg, trait_names, program, pointer_aliases);
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
                                         bool dispatcher_available) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::ostringstream out;

    out << ind << "{\n";
    out << ind
        << "    auto __match_entity = " << rewrite_expr(*match_stmt.subject, trait_names, program, pointer_aliases)
        << ";\n";
    out << ind << "    if (registry.valid(__match_entity)) {\n";

    bool first = true;
    for (const auto& arm : match_stmt.arms) {
        const auto TRAIT_IT  = program.traits.find(arm.trait_name);
        const bool IS_MARKER = TRAIT_IT == program.traits.end() || TRAIT_IT->second.fields.empty();
        std::unordered_set<std::string> arm_aliases = pointer_aliases;

        out << ind << "        " << (first ? "if" : "else if") << " (";
        if (IS_MARKER) {
            out << "registry.all_of<" << arm.trait_name << ">(__match_entity)) {\n";
        } else {
            const std::string ALIAS = arm.alias.value_or("__match_" + arm.trait_name);
            arm_aliases.insert(ALIAS);
            out << "auto* " << ALIAS << " = registry.try_get<" << arm.trait_name << ">(__match_entity)) {\n";
        }

        for (const auto& stmt : arm.body) {
            out << rewrite_stmt(*stmt, indent + 3, trait_names, program, arm_aliases, dispatcher_available);
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
            out << rewrite_stmt(*stmt, indent + 3, trait_names, program, pointer_aliases, dispatcher_available);
        }
        out << ind << "        }\n";
    }

    out << ind << "    }\n";
    out << ind << "}\n";
    return out.str();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static std::string rewrite_stmt(
    const StmtNode& stmt,
    int indent,
    const std::vector<std::string>& trait_names,
    const DecoratedProgram& program,
    const std::unordered_set<std::string>& pointer_aliases,
    bool dispatcher_available) {
    auto known_fields = collect_trait_fields(trait_names, program);
    std::string ind(static_cast<size_t>(indent) * 4, ' ');

    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [&](auto& s) -> std::string {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, LetStmt>) {
                return ind + "[[maybe_unused]] auto " + s.name + " = " +
                       rewrite_expr(*s.value, trait_names, program, pointer_aliases) + ";\n";
            } else if constexpr (std::is_same_v<S, VarAssign>) {
                std::string lhs;
                if (known_fields.contains(s.name)) {
                    auto comp = find_comp_for_field(s.name, trait_names, program);
                    if (!comp.empty()) {
                        lhs = comp + "_comp." + s.name;
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
                        return prefix + rewrite_expr(*call->args[0], trait_names, program, pointer_aliases) + ",\n" +
                               continuation + rewrite_expr(*call->args[1], trait_names, program, pointer_aliases) +
                               ");\n";
                    }
                }
                return ind + lhs + " " + s.op + " " + rewrite_expr(*s.value, trait_names, program, pointer_aliases) +
                       ";\n";
            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                std::string emit_call;
                if (dispatcher_available) {
                    emit_call = "dispatcher.trigger(" + event_cpp_type(s.event_name) + "{";
                } else {
                    emit_call = s.event_name + "_buffer.push_back({";
                }
                for (size_t i = 0; i < s.payload.size(); ++i) {
                    if (i > 0) {
                        emit_call += ", ";
                    }
                    emit_call += "." + s.payload[i].name + " = " +
                                 rewrite_expr(*s.payload[i].value, trait_names, program, pointer_aliases);
                }
                emit_call += "});";
                if (s.target.has_value()) {
                    const std::string TARGET = rewrite_expr(**s.target, trait_names, program, pointer_aliases);
                    return ind + "if (registry.valid(" + TARGET + ")) {\n" + ind + "    " + emit_call + "\n" + ind +
                           "}\n";
                }
                return ind + emit_call + "\n";
            } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                if (!s.child_overrides.empty()) {
                    if (const auto* children = find_template_children(program, s.template_name)) {
                        return ind +
                               emit_hierarchical_spawn_expansion(s.template_name,
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
                result << ind << "    auto __spawned = " << archetype_create_function_name(s.template_name)
                       << "(registry);\n";
                result << emit_spawn_overrides(
                    "__spawned", s.overrides, indent + 1, trait_names, program, pointer_aliases);
                result << ind << "}\n";
                return result.str();
            } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                std::string target = s.target_expr.has_value()
                                         ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases)
                                         : "entity";
                const bool GUARDED = s.target_expr.has_value();
                if (s.args.empty()) {
                    if (GUARDED) {
                        return ind + "if (registry.valid(" + target + ")) {\n" + ind + "    cancel_projected_" +
                               s.trait_name + "(" + target + ");\n" + ind + "    registry.emplace_or_replace<" +
                               s.trait_name + ">(" + target + ");\n" + ind + "}\n";
                    }
                    return ind + "cancel_projected_" + s.trait_name + "(" + target + ");\n" + ind +
                           "registry.emplace_or_replace<" + s.trait_name + ">(" + target + ");\n";
                }

                std::ostringstream result;
                if (GUARDED) {
                    result << ind << "if (registry.valid(" << target << ")) {\n";
                }
                result << ind << (GUARDED ? "    " : "") << "{\n";
                result << ind << (GUARDED ? "        " : "    ") << "cancel_projected_" << s.trait_name << "(" << target
                       << ");\n";
                result << ind << (GUARDED ? "        " : "    ") << "auto __existing = registry.try_get<"
                       << s.trait_name << ">(" << target << ");\n";
                result << ind << (GUARDED ? "        " : "    ")
                       << "auto __value = __existing ? *__existing : " << s.trait_name << "{};\n";
                for (const auto& arg : s.args) {
                    result << ind << (GUARDED ? "        " : "    ") << "__value." << arg.name << " = "
                           << rewrite_expr(*arg.value, trait_names, program, pointer_aliases) << ";\n";
                }
                result << ind << (GUARDED ? "        " : "    ") << "registry.emplace_or_replace<" << s.trait_name
                       << ">(" << target << ", __value);\n";
                result << ind << (GUARDED ? "    " : "") << "}\n";
                if (GUARDED) {
                    result << ind << "}\n";
                }
                return result.str();
            } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                std::string target = s.target_expr.has_value()
                                         ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases)
                                         : "entity";
                if (s.target_expr.has_value()) {
                    return ind + "if (registry.valid(" + target + ")) {\n" + ind + "    cancel_projected_" +
                           s.trait_name + "(" + target + ");\n" + ind + "    if (registry.all_of<" + s.trait_name +
                           ">(" + target + ")) {\n" + ind + "        registry.remove<" + s.trait_name + ">(" + target +
                           ");\n" + ind + "    }\n" + ind + "}\n";
                }
                return ind + "cancel_projected_" + s.trait_name + "(" + target + ");\n" + ind + "if (registry.all_of<" +
                       s.trait_name + ">(" + target + ")) {\n" + ind + "    registry.remove<" + s.trait_name + ">(" +
                       target + ");\n" + ind + "}\n";
            } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                const std::string target = s.target_expr.has_value()
                                               ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases)
                                               : "entity";
                const auto trait_it      = program.traits.find(s.trait_name);
                const bool is_marker     = trait_it == program.traits.end() || trait_it->second.fields.empty();
                std::ostringstream result;
                result << ind << "if (registry.valid(" << target << ")) {\n";
                if (is_marker) {
                    result << ind << "    project_" << s.trait_name << "(registry, " << target << ");\n";
                } else {
                    result << ind << "    [[maybe_unused]] auto& __projected = project_" << s.trait_name
                           << "(registry, " << target << ");\n";
                    for (const auto& arg : s.args) {
                        result << ind << "    __projected." << arg.name << " = "
                               << rewrite_expr(*arg.value, trait_names, program, pointer_aliases) << ";\n";
                    }
                }
                result << ind << "}\n";
                return result.str();
            } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                if (s.target_expr.has_value()) {
                    std::string target = rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases);
                    return ind + "if (registry.valid(" + target + ")) {\n" + ind +
                           "    cactus_destroy_entity_recursive(registry, " + target + ");\n" + ind + "}\n";
                }
                return ind + "cactus_destroy_entity_recursive(registry, entity);\n";
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) {
                    return ind + "return " + rewrite_expr(**s.value, trait_names, program, pointer_aliases) + ";\n";
                }
                return ind + "return;\n";
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind + rewrite_expr(*s.expr, trait_names, program, pointer_aliases) + ";\n";
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                const auto condition = rewrite_expr(*s.condition, trait_names, program, pointer_aliases);
                std::string result   = ind + "if ";
                if (!condition.empty() && condition.front() == '(' && condition.back() == ')') {
                    result += condition;
                } else {
                    result += "(" + condition + ")";
                }
                result += " {\n";
                for (auto& inner : s.then_body) {
                    result += rewrite_stmt(*inner, indent + 1, trait_names, program, pointer_aliases, dispatcher_available);
                }
                result += ind + "}";
                if (!s.else_body.empty()) {
                    result += " else {\n";
                    for (auto& inner : s.else_body) {
                        result += rewrite_stmt(*inner, indent + 1, trait_names, program, pointer_aliases, dispatcher_available);
                    }
                    result += ind + "}";
                }
                return result + "\n";
            } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                return emit_trait_match_stmt(s, indent, trait_names, program, pointer_aliases, dispatcher_available);
            } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                const auto temp    = foreach_temp_name(s);
                std::string result = ind + "auto " + temp + " = " +
                                     rewrite_expr(*s.iterable, trait_names, program, pointer_aliases) + ";\n";
                result += ind + "for (const auto& " + s.var_name + " : " + temp + ") {\n";
                for (const auto& inner : s.body) {
                    result += rewrite_stmt(*inner, indent + 1, trait_names, program, pointer_aliases, dispatcher_available);
                }
                result += ind + "}\n";
                return result;
            } else {
                return ind + "/* unsupported stmt */\n";
            }
        },
        stmt.stmt);
}

std::string EnttSystemEmitter::emit_system(const SystemNode& sys, const DecoratedProgram& program) {
    std::ostringstream out;
    const auto filter_traits  = filter_trait_names(sys.filter);
    const auto exclude_traits = filter_trait_names(sys.exclude);

    for (const auto& handler : sys.handlers) {
        // Frame events (tick, input, fixed_tick, late_tick) are called directly from
        // generated_update_project which has dispatcher; lifecycle events (spawn, destroy,
        // load, unload) are connected via dispatcher sinks and cannot take extra args.
        const bool is_frame_event = handler.event_name == "tick" || handler.event_name == "input" ||
                                    handler.event_name == "fixed_tick" || handler.event_name == "late_tick";
        out << "void " << system_function_name(sys.name, handler.event_name) << "(entt::registry& registry";
        if (is_frame_event) {
            out << ", entt::dispatcher& dispatcher";
        }
        out << ", const " << event_cpp_type(handler.event_name) << "& " << handler.alias.value_or(handler.event_name);
        out << ") {\n";
        if (is_frame_event) {
            out << "    (void)dispatcher;\n";
        }
        out << "    (void)" << handler.alias.value_or(handler.event_name) << ";\n";

        emit_sort_call(out, sys);
        if (!filter_traits.empty()) {
            emit_view_declaration(out, filter_traits, exclude_traits, 1);
            emit_view_each_header(out, filter_traits, 1, program);
            out << "        (void)entity;\n";
            emit_filter_alias_bindings(out, sys.filter, 2);
        } else {
            out << "    for (auto entity : registry.storage<entt::entity>()) {\n";
            out << "        (void)entity;\n";
            emit_storage_filter_skip(out, sys.filter, sys.exclude, 2);
        }

        // Emit body with proper component field access
        for (const auto& stmt : handler.body) {
            out << rewrite_stmt(*stmt, 2, filter_traits, program, {}, is_frame_event);
        }

        out << (filter_traits.empty() ? "    }\n" : "    });\n");
        out << "}\n\n";
    }

    return out.str();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string EnttSystemEmitter::emit_extern_system(const ExternSystemNode& sys, const DecoratedProgram& program) {
    std::ostringstream out;

    if (is_flat_transform_propagation(sys, program)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    const auto HAS_LOCAL_WORLD = [&](entt::entity entity) {\n";
        out << "        return registry.all_of<LocalTransform, WorldTransform>(entity);\n";
        out << "    };\n";
        out << "    const auto GET_PARENT = [&](entt::entity entity) {\n";
        out << "        if (auto* parent = registry.try_get<Parent>(entity); parent != nullptr) {\n";
        out << "            return parent->parent;\n";
        out << "        }\n";
        out << "        return entt::entity{entt::null};\n";
        out << "    };\n";
        out << "    const auto COPY_LOCAL = [&](entt::entity entity) {\n";
        out << "        auto& local = registry.get<LocalTransform>(entity);\n";
        out << "        auto& world = registry.get<WorldTransform>(entity);\n";
        out << "        world.position = local.position;\n";
        out << "        world.rotation = local.rotation;\n";
        out << "        world.scale = local.scale;\n";
        out << "    };\n";
        out << "    const auto ACCUMULATE_FROM_PARENT = [&](entt::entity parent_entity, entt::entity entity) {\n";
        out << "        auto& local = registry.get<LocalTransform>(entity);\n";
        out << "        auto& world = registry.get<WorldTransform>(entity);\n";
        out << "        const auto& parent_world = registry.get<WorldTransform>(parent_entity);\n";
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
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    const auto HAS_LOCAL_WORLD = [&](entt::entity entity) {\n";
        out << "        return registry.all_of<LocalTransform, WorldTransform>(entity);\n";
        out << "    };\n";
        out << "    const auto GET_PARENT = [&](entt::entity entity) {\n";
        out << "        if (auto* parent = registry.try_get<Parent>(entity); parent != nullptr) {\n";
        out << "            return parent->parent;\n";
        out << "        }\n";
        out << "        return entt::entity{entt::null};\n";
        out << "    };\n";
        out << "    const auto COPY_LOCAL = [&](entt::entity entity) {\n";
        out << "        auto& local = registry.get<LocalTransform>(entity);\n";
        out << "        auto& world = registry.get<WorldTransform>(entity);\n";
        out << "        world.position = local.position;\n";
        out << "        world.rotation = local.rotation;\n";
        out << "        world.scale = local.scale;\n";
        out << "    };\n";
        out << "    const auto ACCUMULATE_FROM_PARENT = [&](entt::entity parent_entity, entt::entity entity) {\n";
        out << "        auto& local = registry.get<LocalTransform>(entity);\n";
        out << "        auto& world = registry.get<WorldTransform>(entity);\n";
        out << "        const auto& parent_world = registry.get<WorldTransform>(parent_entity);\n";
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
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, Shape>();\n";
        out << "    BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const Shape& "
               "Shape_comp) {\n";
        out << "        (void)entity;\n";
        out << "        if (!Shape_comp.visible) {\n";
        out << "            return;\n";
        out << "        }\n";
        out << "        switch (Shape_comp.type) {\n";
        out << "            case ShapeType::Rectangle:\n";
        out << "                DrawRectangleV(WorldTransform_comp.position,\n";
        out << "                               Shape_comp.size,\n";
        out << "                               Shape_comp.color);\n";
        out << "                break;\n";
        out << "        }\n";
        out << "    });\n";
        out << "    EndMode2D();\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_sprite_renderer(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, Renderer>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const Renderer& "
               "Renderer_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_sprite(WorldTransform_comp.position, Renderer_comp.size, "
               "Renderer_comp.color, Renderer_comp.texture, Renderer_comp.visible, Renderer_comp.layer);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_animated_sprite_system(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<AnimatedSprite>();\n";
        out << "    view.each([&](entt::entity entity, AnimatedSprite& AnimatedSprite_comp) {\n";
        out << "        (void)entity;\n";
        out << "        constexpr float kFixedDt = 1.0F / 60.0F;\n";
        out << "        cactus::runtime::entt_backend::advance_animated_sprite(AnimatedSprite_comp.texture, "
               "AnimatedSprite_comp.frame, AnimatedSprite_comp.frame_count, AnimatedSprite_comp.fps, "
               "AnimatedSprite_comp.playing, kFixedDt);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_model_animation_system(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<ModelRenderer, ModelAnimator>();\n";
        out << "    view.each([&](entt::entity entity, const ModelRenderer& ModelRenderer_comp, ModelAnimator& "
               "ModelAnimator_comp) {\n";
        out << "        (void)entity;\n";
        out << "        if (!ModelAnimator_comp.playing) {\n";
        out << "            return;\n";
        out << "        }\n";
        out << "        constexpr float kFixedDt = 1.0F / 60.0F;\n";
        out << "        ModelAnimator_comp.time += kFixedDt * ModelAnimator_comp.speed;\n";
        out << "        const float duration = cactus::runtime::entt_backend::model_animation_duration("
               "ModelRenderer_comp.model, ModelAnimator_comp.clip);\n";
        out << "        if (duration > 0.0F) {\n";
        out << "            ModelAnimator_comp.time = std::fmod(ModelAnimator_comp.time, duration);\n";
        out << "            if (ModelAnimator_comp.time < 0.0F) {\n";
        out << "                ModelAnimator_comp.time += duration;\n";
        out << "            }\n";
        out << "        }\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_mesh_renderer(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, Renderer>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const Renderer& "
               "Renderer_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_mesh(WorldTransform_comp.position, "
               "WorldTransform_comp.rotation, WorldTransform_comp.scale, Renderer_comp.mesh, Renderer_comp.material, "
               "Renderer_comp.visible, Renderer_comp.cast_shadow);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_model_renderer_system(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, ModelRenderer>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const ModelRenderer& "
               "ModelRenderer_comp) {\n";
        out << "        (void)entity;\n";
        if (program.traits.contains("ModelAnimator")) {
            out << "        if (const auto* animator = registry.try_get<ModelAnimator>(entity)) {\n";
            out << "            cactus::runtime::entt_backend::submit_model(WorldTransform_comp.position, "
                   "WorldTransform_comp.rotation, WorldTransform_comp.scale, ModelRenderer_comp.model, "
                   "ModelRenderer_comp.visible, ModelRenderer_comp.cast_shadow, animator->clip, animator->time);\n";
            out << "            return;\n";
            out << "        }\n";
        }
        out << "        cactus::runtime::entt_backend::submit_model(WorldTransform_comp.position, "
               "WorldTransform_comp.rotation, WorldTransform_comp.scale, ModelRenderer_comp.model, "
               "ModelRenderer_comp.visible, ModelRenderer_comp.cast_shadow);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_billboard_renderer(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, BillboardRenderer>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const "
               "BillboardRenderer& BillboardRenderer_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_billboard(WorldTransform_comp.position, "
               "BillboardRenderer_comp.size, BillboardRenderer_comp.color, BillboardRenderer_comp.texture, "
               "BillboardRenderer_comp.visible);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_point_light_system(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, PointLight>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const PointLight& "
               "PointLight_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::register_point_light(WorldTransform_comp.position, "
               "PointLight_comp.color, PointLight_comp.intensity, PointLight_comp.range, PointLight_comp.enabled);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_directional_light_system(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<DirectionalLight>();\n";
        out << "    view.each([&](entt::entity entity, const DirectionalLight& DirectionalLight_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::register_directional_light(DirectionalLight_comp.direction, "
               "DirectionalLight_comp.color, DirectionalLight_comp.intensity, DirectionalLight_comp.enabled);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_any_text_renderer_2d(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        if (!world_transform_is_volume(program)) {
            out << "    auto view = registry.view<WorldTransform, TextLabel>();\n";
            out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const TextLabel& "
                   "TextLabel_comp) {\n";
            out << "        (void)entity;\n";
            out << "        cactus::runtime::entt_backend::submit_text_2d(WorldTransform_comp.position, "
                   "WorldTransform_comp.rotation, TextLabel_comp.font_size, TextLabel_comp.color, TextLabel_comp.text, "
                   "TextLabel_comp.visible);\n";
            out << "    });\n";
        } else {
            out << "    (void)registry;\n";
        }
        out << "}\n\n";
        return out.str();
    }

    if (is_any_text_renderer_3d(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        if (world_transform_is_volume(program)) {
            out << "    auto view = registry.view<WorldTransform, TextLabel>();\n";
            out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const TextLabel& "
                   "TextLabel_comp) {\n";
            out << "        cactus::runtime::entt_backend::submit_text_3d(static_cast<uint32_t>(entity), "
                   "WorldTransform_comp.position, WorldTransform_comp.rotation, WorldTransform_comp.scale, "
                   "TextLabel_comp.font_size, TextLabel_comp.color, TextLabel_comp.text, TextLabel_comp.visible);\n";
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
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<ScreenLabel>();\n";
        out << "    view.each([&](entt::entity entity, const ScreenLabel& ScreenLabel_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_screen_label(ScreenLabel_comp.position, "
               "ScreenLabel_comp.font_size, ScreenLabel_comp.color, ScreenLabel_comp.text, "
               "ScreenLabel_comp.visible);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_editor_extern_system(sys)) {
        if (sys.name == "GizmoRenderer2D") {
            out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
            out << "    bool __editor_active = false;\n";
            out << "    auto __estate_view = registry.view<EditorState>();\n";
            out << "    for (auto __e : __estate_view) {\n";
            out << "        if (__estate_view.get<EditorState>(__e).active) { __editor_active = true; break; }\n";
            out << "    }\n";
            out << "    if (!__editor_active) { return; }\n";
            out << "    BeginMode2D(cactus::runtime::entt_backend::get_active_camera_2d());\n";
            out << "    auto view = registry.view<EditorGizmo2D, WorldTransform>();\n";
            out << "    view.each([&](entt::entity entity, const EditorGizmo2D& gizmo, const WorldTransform& xform) {\n";
            out << "        Rectangle rect{.x = xform.position.x - 0.5F, .y = xform.position.y - 0.5F,\n";
            out << "                       .width = 1.0F, .height = 1.0F};\n";
            out << "        if (const auto* box = registry.try_get<BoxCollider>(entity)) {\n";
            out << "            rect = {.x = xform.position.x, .y = xform.position.y,\n";
            out << "                    .width = box->size.x, .height = box->size.y};\n";
            out << "        }\n";
            out << "        DrawRectangleLinesEx(rect, 0.05F, gizmo.color);\n";
            out << "        const float arrow_len   = gizmo.size;\n";
            out << "        const float arrow_thick = arrow_len * 0.05F;\n";
            out << "        const Vector2 center    = xform.position;\n";
            out << "        if (gizmo.mode == 1) {\n";
            out << "            DrawLineEx(center, {.x = center.x + arrow_len, .y = center.y}, arrow_thick, RED);\n";
            out << "            DrawTriangle(\n";
            out << "                {.x = center.x + arrow_len - (arrow_len * 0.2F), .y = center.y - (arrow_len * 0.1F)},\n";
            out << "                {.x = center.x + arrow_len - (arrow_len * 0.2F), .y = center.y + (arrow_len * 0.1F)},\n";
            out << "                {.x = center.x + arrow_len, .y = center.y}, RED);\n";
            out << "            DrawLineEx(center, {.x = center.x, .y = center.y + arrow_len}, arrow_thick, GREEN);\n";
            out << "            DrawTriangle(\n";
            out << "                {.x = center.x - (arrow_len * 0.1F), .y = center.y + arrow_len - (arrow_len * 0.2F)},\n";
            out << "                {.x = center.x + (arrow_len * 0.1F), .y = center.y + arrow_len - (arrow_len * 0.2F)},\n";
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
        if (sys.name == "EditorTemplatePalette") {
            out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
            out << "    auto __estate_view = registry.view<EditorState>();\n";
            out << "    for (auto __ed_ent : __estate_view) {\n";
            out << "        auto& __es = __estate_view.get<EditorState>(__ed_ent);\n";
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
        // EditorPropertyPanel and GizmoRenderer3D — stub for now
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    (void)registry;\n";
        out << "}\n\n";
        return out.str();
    }

    const auto filter_traits  = filter_trait_names(sys.filter);
    const auto exclude_traits = filter_trait_names(sys.exclude);

    out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";

    if (!sys.order_by.empty()) {
        out << "    // order by:\n";
        for (const auto& key : sys.order_by) {
            out << "    //   " << key.alias << "." << key.field << (key.descending ? " desc" : " asc") << "\n";
        }
    }

    if (!filter_traits.empty()) {
        emit_view_declaration(out, filter_traits, exclude_traits, 1);
        emit_view_each_header(out, filter_traits, 1, program);
        out << "        (void)entity;\n";
        emit_filter_alias_bindings(out, sys.filter, 2);
    } else {
        out << "    for (auto entity : registry.storage<entt::entity>()) {\n";
        out << "        (void)entity;\n";
        emit_storage_filter_skip(out, sys.filter, sys.exclude, 2);
    }
    out << "        " << system_function_name(sys.name, "update") << "(registry, entity";
    for (const auto& trait_name : filter_traits) {
        out << ", " << trait_name << "_comp";
    }
    out << ");\n";
    out << (filter_traits.empty() ? "    }\n" : "    });\n");
    out << "}\n\n";

    out << "void " << system_function_name(sys.name, "update") << "(entt::registry& registry, entt::entity entity";
    for (const auto& trait_name : filter_traits) {
        out << ", " << trait_name << "& " << trait_name << "_comp";
    }
    out << ");\n\n";

    return out.str();
}

bool EnttSystemEmitter::requires_entt_hierarchy_helpers(const DecoratedProgram& program) {
    return program.traits.contains("Parent");
}

std::string EnttSystemEmitter::emit_entt_hierarchy_helpers(const DecoratedProgram& program) {
    if (!requires_entt_hierarchy_helpers(program)) {
        return "";
    }

    std::ostringstream out;
    out << "[[maybe_unused]] static void cactus_destroy_entity_recursive(entt::registry& registry, entt::entity "
           "entity) {\n";
    out << "    cactus::runtime::entt_backend::destroy_entity_recursive(\n";
    out << "        registry, entity, [&](entt::entity parent, const auto& visitor) {\n";
    out << "            auto parent_view = registry.view<Parent>();\n";
    out << "            parent_view.each([&](entt::entity child, const Parent& rel) {\n";
    out << "                if (rel.parent == parent) {\n";
    out << "                    visitor(child);\n";
    out << "                }\n";
    out << "            });\n";
    out << "        });\n";
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
