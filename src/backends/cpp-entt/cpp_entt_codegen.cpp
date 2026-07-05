#include "backends/cpp-entt/cpp_entt_codegen.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/event_emitter.hpp"
#include "backends/cpp-entt/system_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace cactus {

namespace {

bool program_uses_module(const DecoratedProgram& program, std::string_view module_name) {
    if (program.ast == nullptr) { return false; }
    for (const auto& decl : program.ast->declarations) {
        if (const auto* use = std::get_if<UseNode>(&decl)) {
            if (use->module_name == module_name) { return true; }
        }
    }
    return false;
}

bool uses_text_format(const DecoratedProgram& program)            { return program_uses_module(program, "std.text"); }
bool module_uses_camera_flat(const DecoratedProgram& program)     { return program_uses_module(program, "std.camera.flat"); }
bool module_uses_camera_viewport(const DecoratedProgram& program) { return program_uses_module(program, "std.camera.viewport"); }
bool module_uses_camera_volume(const DecoratedProgram& program)   { return program_uses_module(program, "std.camera.volume"); }
bool module_uses_editor(const DecoratedProgram& program)          { return program_uses_module(program, "std.editor"); }

// Task 6.1: Check if the program has any extern funcs requiring the runtime header
bool has_extern_funcs(const DecoratedProgram& program) {
    for (const auto& [name, func] : program.funcs) {  // NOLINT(readability-use-anyofallof)
        if (func.is_extern) {
            return true;
        }
    }
    return false;
}

std::string emit_projected_trait_registry_helpers(const DecoratedProgram& program) {
    std::ostringstream out;
    out << "// ── Projected Trait Registry Tracking ────────────────────────────────\n\n";
    out << "namespace {\n\n";
    for (const auto& [name, trait] : program.traits) {
        out << "std::vector<entt::entity> projected_" << name
            << "_entities;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)\n";
        if (trait.fields.empty()) {
            out << "std::unordered_map<entt::entity, bool> projected_" << name
                << "_previous;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)\n\n";
        } else {
            out << "std::unordered_map<entt::entity, std::optional<" << name << ">> projected_" << name
                << "_previous;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)\n\n";
        }
        out << "[[maybe_unused]] void remember_projected_" << name
            << "(entt::registry& registry, entt::entity entity) {\n";
        out << "    if (projected_" << name << "_previous.contains(entity)) {\n";
        out << "        return;\n";
        out << "    }\n";
        out << "    projected_" << name << "_entities.push_back(entity);\n";
        if (trait.fields.empty()) {
            out << "    projected_" << name << "_previous.emplace(entity, registry.all_of<" << name << ">(entity));\n";
        } else {
            out << "    if (auto* previous = registry.try_get<" << name << ">(entity); previous != nullptr) {\n";
            out << "        projected_" << name << "_previous.emplace(entity, *previous);\n";
            out << "    } else {\n";
            out << "        projected_" << name << "_previous.emplace(entity, std::nullopt);\n";
            out << "    }\n";
        }
        out << "}\n\n";
        if (trait.fields.empty()) {
            out << "[[maybe_unused]] void project_" << name << "(entt::registry& registry, entt::entity entity) {\n";
            out << "    remember_projected_" << name << "(registry, entity);\n";
            out << "    registry.emplace_or_replace<" << name << ">(entity);\n";
            out << "}\n\n";
        } else {
            out << "[[maybe_unused]] " << name << "& project_" << name
                << "(entt::registry& registry, entt::entity entity) {\n";
            out << "    remember_projected_" << name << "(registry, entity);\n";
            out << "    if (auto* current = registry.try_get<" << name << ">(entity); current != nullptr) {\n";
            out << "        return *current;\n";
            out << "    }\n";
            out << "    return registry.emplace<" << name << ">(entity);\n";
            out << "}\n\n";
        }
        out << "[[maybe_unused]] void cancel_projected_" << name << "(entt::entity entity) {\n";
        out << "    projected_" << name << "_previous.erase(entity);\n";
        out << "}\n\n";
    }
    out << "void clear_projected_traits(entt::registry& registry) {\n";
    for (const auto& [name, trait] : program.traits) {
        (void)trait;
        out << "    for (const auto entity : projected_" << name << "_entities) {\n";
        out << "        auto previous_it = projected_" << name << "_previous.find(entity);\n";
        out << "        if (previous_it == projected_" << name << "_previous.end()) {\n";
        out << "            continue;\n";
        out << "        }\n";
        out << "        if (!registry.valid(entity)) {\n";
        out << "            continue;\n";
        out << "        }\n";
        if (trait.fields.empty()) {
            out << "        if (previous_it->second) {\n";
            out << "            registry.emplace_or_replace<" << name << ">(entity);\n";
            out << "        } else if (registry.all_of<" << name << ">(entity)) {\n";
            out << "            registry.remove<" << name << ">(entity);\n";
            out << "        }\n";
        } else {
            out << "        if (previous_it->second.has_value()) {\n";
            out << "            registry.emplace_or_replace<" << name << ">(entity, *previous_it->second);\n";
            out << "        } else if (registry.all_of<" << name << ">(entity)) {\n";
            out << "            registry.remove<" << name << ">(entity);\n";
            out << "        }\n";
        }
        out << "    }\n";
        out << "    projected_" << name << "_entities.clear();\n";
        out << "    projected_" << name << "_previous.clear();\n";
    }
    out << "}\n\n";
    out << "}  // namespace\n\n";
    return out.str();
}

std::string upper_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string snake_case(const std::string& value) {
    std::string result;
    for (char ch : value) {
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

std::string system_function_name(const std::string& system_name, const std::string& suffix) {
    return snake_case(system_name) + "_" + suffix;
}

std::string input_action_constant_name(const std::string& input_name) {
    return "K_" + upper_copy(snake_case(input_name));
}

std::string cpp_string_literal(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string asset_register_call(const AssetDeclNode& asset) {
    const auto path_literal = cpp_string_literal(asset.path);
    switch (asset.asset_kind) {
        case AssetKind::Mesh:
            return "shared_asset_registry().register_mesh(" + asset.name + ", " + path_literal + ", static_cast<int>(" +
                   asset.name + "));";
        case AssetKind::Texture:
            return "shared_asset_registry().register_texture(" + asset.name + ", " + path_literal +
                   ", static_cast<int>(" + asset.name + "));";
        case AssetKind::Material:
            return "shared_asset_registry().register_material(" + asset.name + ", " + path_literal +
                   ", static_cast<int>(" + asset.name + "));";
        case AssetKind::Sound:
        case AssetKind::Music:
        case AssetKind::Font:
            return {};
    }
    return {};
}

std::string filter_simple_name(const FilterEntry& entry) {
    auto dot = entry.qualified_name.rfind('.');
    return (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
}

bool filter_has_trait(const FilterClause& filter, const std::string& qualified, const std::string& simple) {
    for (const auto& entry : filter.entries) {
        if (entry.qualified_name == qualified || filter_simple_name(entry) == simple) {
            return true;
        }
    }
    return std::ranges::find(filter.trait_names, simple) != filter.trait_names.end();
}

bool uses_stdlib_extern_contract(const ExternSystemNode& sys) {
    if (sys.is_stdlib) {
        return true;
    }
    if (sys.name == "TransformPropagation" || sys.name == "ShapeRenderer" || sys.name == "SpriteRenderer" ||
        sys.name == "AnimatedSpriteSystem" || sys.name == "MeshRenderer" || sys.name == "BillboardRenderer" ||
        sys.name == "PointLightSystem" || sys.name == "DirectionalLightSystem" || sys.name == "TextRenderer2D" ||
        sys.name == "TextRenderer3D" || sys.name == "GizmoRenderer2D" || sys.name == "GizmoRenderer3D" ||
        sys.name == "EditorTemplatePalette" || sys.name == "EditorPropertyPanel") {
        return true;
    }
    return std::ranges::any_of(sys.filter.entries,
                               [](const auto& entry) { return entry.qualified_name.rfind("std.", 0) == 0; });
}

bool is_render_phase_extern(const ExternSystemNode& sys, const DecoratedProgram& program) {
    (void)program;
    if (!uses_stdlib_extern_contract(sys)) {
        return false;
    }
    if (sys.name == "ShapeRenderer") {
        return filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.shapes.Shape", "Shape");
    }
    if (sys.name == "SpriteRenderer") {
        return filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.sprites.Renderer", "Renderer");
    }
    if (sys.name == "MeshRenderer") {
        return filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.meshes.Renderer", "Renderer");
    }
    if (sys.name == "BillboardRenderer") {
        return filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.meshes.BillboardRenderer", "BillboardRenderer");
    }
    if (sys.name == "PointLightSystem") {
        return filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
               filter_has_trait(sys.filter, "std.render.meshes.PointLight", "PointLight");
    }
    if (sys.name == "DirectionalLightSystem") {
        return filter_has_trait(sys.filter, "std.render.meshes.DirectionalLight", "DirectionalLight");
    }
    if (sys.name == "TextRenderer2D") {
        return filter_has_trait(sys.filter, "std.render.text.TextLabel", "TextLabel");
    }
    if (sys.name == "TextRenderer3D") {
        return filter_has_trait(sys.filter, "std.render.text.TextLabel", "TextLabel");
    }
    if (sys.name == "GizmoRenderer2D" || sys.name == "GizmoRenderer3D" ||
        sys.name == "EditorTemplatePalette" || sys.name == "EditorPropertyPanel") {
        return uses_stdlib_extern_contract(sys);
    }
    return false;
}

bool is_update_phase_extern(const ExternSystemNode& sys, const DecoratedProgram& program) {
    return !is_render_phase_extern(sys, program);
}

std::optional<std::string> raylib_key_constant(const ExprNode& expr) {
    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        if (const auto* ident = std::get_if<IdentExpr>(&member->object->expr)) {
            if (ident->name == "Key") {
                return "KEY_" + upper_copy(member->member);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> raylib_mouse_constant(const ExprNode& expr) {
    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        if (const auto* ident = std::get_if<IdentExpr>(&member->object->expr)) {
            if (ident->name == "MouseButton") {
                return "MOUSE_BUTTON_" + upper_copy(snake_case(member->member));
            }
        }
    }
    return std::nullopt;
}

std::string pad_to_width(const std::string& value, std::size_t width) {
    if (value.size() >= width) {
        return value;
    }
    return value + std::string(width - value.size(), ' ');
}

std::string archetype_create_function_name(const std::string& archetype_name) {
    return "create_" + snake_case(archetype_name);
}

void emit_archetype_trait_initializers(std::ostringstream& out,
                                       const std::vector<ArchetypeTraitEntry>& traits,
                                       const DecoratedProgram& program,
                                       const std::string& entity_name,
                                       int indent) {
    const std::string ind(static_cast<std::size_t>(indent) * 4U, ' ');
    for (const auto& trait : traits) {
        auto resolved_trait = program.traits.find(trait.trait_name);
        if (resolved_trait != program.traits.end() && resolved_trait->second.fields.empty()) {
            out << ind << "registry.emplace<" << trait.trait_name << ">(" << entity_name << ");\n";
            continue;
        }

        out << ind << "{\n";
        std::size_t widest = std::string("auto component").size();
        for (const auto& assignment : trait.assignments) {
            widest = std::max(widest, std::string("component.").size() + assignment.name.size());
        }
        out << ind << "    " << pad_to_width("auto component", widest) << " = " << trait.trait_name << "{};\n";
        for (const auto& assignment : trait.assignments) {
            out << ind << "    " << pad_to_width("component." + assignment.name, widest) << " = "
                << EnttCodegenUtils::emit_expr(*assignment.value, program.ast) << ";\n";
        }
        out << ind << "    registry.emplace<" << trait.trait_name << ">(" << entity_name << ", component);\n";
        out << ind << "}\n";
    }
}

std::string emit_archetype_creation_function(const std::string& archetype_name,
                                             const std::vector<ArchetypeTraitEntry>& traits,
                                             const DecoratedProgram& program) {
    std::ostringstream out;
    out << "entt::entity " << archetype_create_function_name(archetype_name) << "(entt::registry& registry) {\n";
    out << "    auto entity = registry.create();\n";
    emit_archetype_trait_initializers(out, traits, program, "entity", 1);
    out << "    return entity;\n";
    out << "}\n\n";
    return out.str();
}

const ResolvedTrait* find_trait(const DecoratedProgram& program, const std::string& name) {
    auto it = program.traits.find(name);
    return it == program.traits.end() ? nullptr : &it->second;
}

const ResolvedField* find_field(const ResolvedTrait* trait, const std::string& field_name) {
    if (trait == nullptr) {
        return nullptr;
    }
    auto it = std::ranges::find_if(trait->fields, [&](const auto& field) { return field.name == field_name; });
    return it == trait->fields.end() ? nullptr : &*it;
}

bool trait_field_is(const DecoratedProgram& program,
                    const std::string& trait_name,
                    const std::string& field_name,
                    TypeKind kind) {
    const auto* field = find_field(find_trait(program, trait_name), field_name);
    return field != nullptr && field->type.kind == kind;
}

bool has_collision_event_decl(const ProgramNode* ast) {
    if (ast == nullptr) {
        return false;
    }
    return std::ranges::any_of(ast->declarations, [](const auto& decl) {
        const auto* event = std::get_if<EventNode>(&decl);
        return event != nullptr && event->name == "CollisionEnter";
    });
}

bool has_flat_collider_support(const DecoratedProgram& program) {
    const auto* collider = find_trait(program, "Collider");
    return collider != nullptr && collider->is_stdlib && find_field(collider, "layer") != nullptr &&
           find_field(collider, "mask") != nullptr &&
           trait_field_is(program, "WorldTransform", "position", TypeKind::Vec2) &&
           trait_field_is(program, "WorldTransform", "rotation", TypeKind::Float) &&
           trait_field_is(program, "WorldTransform", "scale", TypeKind::Vec2) &&
           trait_field_is(program, "BoxCollider", "size", TypeKind::Vec2) &&
           trait_field_is(program, "CircleCollider", "radius", TypeKind::Float) &&
           trait_field_is(program, "CapsuleCollider", "height", TypeKind::Float);
}

bool has_volume_collider_support(const DecoratedProgram& program) {
    const auto* collider = find_trait(program, "Collider");
    return collider != nullptr && collider->is_stdlib && find_field(collider, "layer") != nullptr &&
           find_field(collider, "mask") != nullptr &&
           trait_field_is(program, "WorldTransform", "position", TypeKind::Vec3) &&
           trait_field_is(program, "WorldTransform", "rotation", TypeKind::Quat) &&
           trait_field_is(program, "WorldTransform", "scale", TypeKind::Vec3) &&
           trait_field_is(program, "BoxCollider", "size", TypeKind::Vec3) &&
           trait_field_is(program, "SphereCollider", "radius", TypeKind::Float) &&
           trait_field_is(program, "CapsuleCollider", "height", TypeKind::Float);
}

bool has_flat_physics_query_api(const DecoratedProgram& program) {
    return program.enums.contains("QueryResultKind") && program.structs.contains("QueryContact2D") &&
           program.structs.contains("QueryResult2D");
}

std::string emit_flat_query_fallback_helpers() {
    return R"(
// ── Stdlib 2D Query Fallbacks ────────────────────────────────────────────────

namespace {

QueryContact2D cactus_flat_contact(entt::entity entity, Vector2 normal, float distance, Vector2 overlap) noexcept {
    return QueryContact2D{.other = entity, .normal = normal, .distance = distance, .overlap = overlap};
}

QueryResult2D cactus_empty_query_result() noexcept {
    return QueryResult2D{.kind = QueryResultKind::Empty,
                         .contact = cactus_flat_contact(entt::entity{entt::null},
                                                        Vector2{.x = 0.0F, .y = 0.0F},
                                                        0.0F,
                                                        Vector2{.x = 0.0F, .y = 0.0F})};
}

}  // namespace

QueryResult2D cactus_query_cast_nearest(entt::registry& registry,
                                        entt::entity subject_entity,
                                        Vector2 delta,
                                        int mask,
                                        entt::entity exclude) {
    (void)registry;
    (void)subject_entity;
    (void)delta;
    (void)mask;
    (void)exclude;
    return cactus_empty_query_result();
}

QueryResult2D cactus_query_overlap_deepest(entt::registry& registry,
                                           entt::entity subject_entity,
                                           int mask,
                                           entt::entity exclude) {
    (void)registry;
    (void)subject_entity;
    (void)mask;
    (void)exclude;
    return cactus_empty_query_result();
}

std::vector<QueryContact2D> cactus_query_overlap_all(entt::registry& registry,
                                                     entt::entity subject_entity,
                                                     int mask,
                                                     entt::entity exclude) {
    (void)registry;
    (void)subject_entity;
    (void)mask;
    (void)exclude;
    return {};
}

)";
}

std::string emit_flat_collision_helpers() {
    return R"(
// ── Stdlib 2D Collider Runtime ───────────────────────────────────────────────

namespace {

struct CactusFlatColliderRef {
    entt::entity entity{entt::null};
    Vector2 position{};
    Collider collider{};
    Vector2 half_extents{};
};

bool cactus_collision_masks_allow(const Collider& lhs, const Collider& rhs) noexcept {
    return ((lhs.mask & rhs.layer) != 0) && ((rhs.mask & lhs.layer) != 0);
}

Vector2 cactus_flat_box_overlap(const CactusFlatColliderRef& lhs, const CactusFlatColliderRef& rhs) noexcept {
    const float lhs_center_x = lhs.position.x + lhs.half_extents.x;
    const float lhs_center_y = lhs.position.y + lhs.half_extents.y;
    const float rhs_center_x = rhs.position.x + rhs.half_extents.x;
    const float rhs_center_y = rhs.position.y + rhs.half_extents.y;
    const float delta_x = rhs_center_x - lhs_center_x;
    const float delta_y = rhs_center_y - lhs_center_y;
    const float overlap_x = (lhs.half_extents.x + rhs.half_extents.x) - std::abs(delta_x);
    const float overlap_y = (lhs.half_extents.y + rhs.half_extents.y) - std::abs(delta_y);
    if (overlap_x <= 0.0F || overlap_y <= 0.0F) {
        return Vector2{.x = 0.0F, .y = 0.0F};
    }
    if (overlap_x < overlap_y) {
        return Vector2{.x = delta_x < 0.0F ? -overlap_x : overlap_x, .y = 0.0F};
    }
    return Vector2{.x = 0.0F, .y = delta_y < 0.0F ? -overlap_y : overlap_y};
}

void cactus_collect_flat_colliders(entt::registry& registry, std::vector<CactusFlatColliderRef>& colliders) {
    auto boxes = registry.view<WorldTransform, Collider, BoxCollider>();
    boxes.each([&](entt::entity entity,
                   const WorldTransform& transform,
                   const Collider& collider,
                   const BoxCollider& box) {
        colliders.push_back(CactusFlatColliderRef{.entity       = entity,
                                                  .position     = transform.position,
                                                  .collider     = collider,
                                                  .half_extents = Vector2{.x = box.size.x * 0.5F,
                                                                          .y = box.size.y * 0.5F}});
    });
    auto circles = registry.view<WorldTransform, Collider, CircleCollider>();
    circles.each([&](entt::entity entity,
                     const WorldTransform& transform,
                     const Collider& collider,
                     const CircleCollider& circle) {
        colliders.push_back(CactusFlatColliderRef{.entity       = entity,
                                                  .position     = Vector2{.x = transform.position.x - circle.radius,
                                                                          .y = transform.position.y - circle.radius},
                                                  .collider     = collider,
                                                  .half_extents = Vector2{.x = circle.radius,
                                                                          .y = circle.radius}});
    });
    auto capsules = registry.view<WorldTransform, Collider, CapsuleCollider>();
    capsules.each([&](entt::entity entity,
                      const WorldTransform& transform,
                      const Collider& collider,
                      const CapsuleCollider& capsule) {
        colliders.push_back(CactusFlatColliderRef{.entity       = entity,
                                                  .position     = Vector2{.x = transform.position.x - capsule.radius,
                                                                          .y = transform.position.y - (capsule.height * 0.5F)},
                                                  .collider     = collider,
                                                  .half_extents = Vector2{.x = capsule.radius,
                                                                          .y = capsule.height * 0.5F}});
    });
}

Vector2 cactus_flat_overlap_normal(Vector2 overlap) noexcept {
    if (overlap.x != 0.0F) {
        return Vector2{.x = overlap.x < 0.0F ? -1.0F : 1.0F, .y = 0.0F};
    }
    if (overlap.y != 0.0F) {
        return Vector2{.x = 0.0F, .y = overlap.y < 0.0F ? -1.0F : 1.0F};
    }
    return Vector2{.x = 0.0F, .y = 0.0F};
}

float cactus_flat_length(Vector2 value) noexcept {
    return std::sqrt((value.x * value.x) + (value.y * value.y));
}

QueryContact2D cactus_flat_contact(entt::entity entity, Vector2 normal, float distance, Vector2 overlap) noexcept {
    return QueryContact2D{.other = entity, .normal = normal, .distance = distance, .overlap = overlap};
}

QueryResult2D cactus_empty_query_result() noexcept {
    return QueryResult2D{.kind = QueryResultKind::Empty,
                         .contact = cactus_flat_contact(entt::entity{entt::null},
                                                        Vector2{.x = 0.0F, .y = 0.0F},
                                                        0.0F,
                                                        Vector2{.x = 0.0F, .y = 0.0F})};
}

QueryResult2D cactus_hit_query_result(QueryContact2D contact) noexcept {
    return QueryResult2D{.kind = QueryResultKind::Hit, .contact = contact};
}

bool cactus_query_mask_allows(const CactusFlatColliderRef& candidate, int mask) noexcept {
    return (candidate.collider.layer & mask) != 0;
}

bool cactus_find_flat_collider(entt::registry& registry, entt::entity entity, CactusFlatColliderRef& result) {
    std::vector<CactusFlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    for (const auto& collider : colliders) {
        if (collider.entity == entity) {
            result = collider;
            return true;
        }
    }
    return false;
}

std::optional<QueryContact2D> cactus_flat_overlap_contact(const CactusFlatColliderRef& subject,
                                                          const CactusFlatColliderRef& candidate) noexcept {
    const auto overlap = cactus_flat_box_overlap(candidate, subject);
    if (overlap.x == 0.0F && overlap.y == 0.0F) {
        return std::nullopt;
    }
    return cactus_flat_contact(candidate.entity, cactus_flat_overlap_normal(overlap), 0.0F, overlap);
}

std::optional<QueryContact2D> cactus_flat_cast_contact(const CactusFlatColliderRef& subject,
                                                       const CactusFlatColliderRef& candidate,
                                                       Vector2 delta) noexcept {
    if (auto overlap = cactus_flat_overlap_contact(subject, candidate)) {
        return overlap;
    }

    const float subject_min_x = subject.position.x;
    const float subject_max_x = subject.position.x + (subject.half_extents.x * 2.0F);
    const float subject_min_y = subject.position.y;
    const float subject_max_y = subject.position.y + (subject.half_extents.y * 2.0F);
    const float candidate_min_x = candidate.position.x;
    const float candidate_max_x = candidate.position.x + (candidate.half_extents.x * 2.0F);
    const float candidate_min_y = candidate.position.y;
    const float candidate_max_y = candidate.position.y + (candidate.half_extents.y * 2.0F);

    constexpr float NEG_INF = -std::numeric_limits<float>::infinity();
    constexpr float POS_INF = std::numeric_limits<float>::infinity();
    float x_entry = NEG_INF;
    float x_exit = POS_INF;
    if (delta.x > 0.0F) {
        x_entry = (candidate_min_x - subject_max_x) / delta.x;
        x_exit = (candidate_max_x - subject_min_x) / delta.x;
    } else if (delta.x < 0.0F) {
        x_entry = (candidate_max_x - subject_min_x) / delta.x;
        x_exit = (candidate_min_x - subject_max_x) / delta.x;
    } else if (subject_max_x <= candidate_min_x || subject_min_x >= candidate_max_x) {
        return std::nullopt;
    }

    float y_entry = NEG_INF;
    float y_exit = POS_INF;
    if (delta.y > 0.0F) {
        y_entry = (candidate_min_y - subject_max_y) / delta.y;
        y_exit = (candidate_max_y - subject_min_y) / delta.y;
    } else if (delta.y < 0.0F) {
        y_entry = (candidate_max_y - subject_min_y) / delta.y;
        y_exit = (candidate_min_y - subject_max_y) / delta.y;
    } else if (subject_max_y <= candidate_min_y || subject_min_y >= candidate_max_y) {
        return std::nullopt;
    }

    const float entry = std::max(x_entry, y_entry);
    const float exit = std::min(x_exit, y_exit);
    if (entry > exit || entry < 0.0F || entry > 1.0F) {
        return std::nullopt;
    }

    Vector2 normal{.x = 0.0F, .y = 0.0F};
    if (x_entry > y_entry) {
        normal.x = delta.x > 0.0F ? -1.0F : 1.0F;
    } else {
        normal.y = delta.y > 0.0F ? -1.0F : 1.0F;
    }
    const float distance = cactus_flat_length(delta) * std::max(0.0F, entry);
    return cactus_flat_contact(candidate.entity, normal, distance, Vector2{.x = 0.0F, .y = 0.0F});
}

}  // namespace

QueryResult2D cactus_query_cast_nearest(entt::registry& registry,
                                        entt::entity subject_entity,
                                        Vector2 delta,
                                        int mask,
                                        entt::entity exclude) {
    if (delta.x == 0.0F && delta.y == 0.0F) {
        return cactus_empty_query_result();
    }

    CactusFlatColliderRef subject;
    if (!cactus_find_flat_collider(registry, subject_entity, subject)) {
        return cactus_empty_query_result();
    }

    std::vector<CactusFlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    std::optional<QueryContact2D> nearest;
    for (const auto& candidate : colliders) {
        if (candidate.entity == exclude || !cactus_query_mask_allows(candidate, mask)) {
            continue;
        }
        const auto contact = cactus_flat_cast_contact(subject, candidate, delta);
        if (!contact.has_value()) {
            continue;
        }
        if (!nearest.has_value() || contact->distance < nearest->distance) {
            nearest = *contact;
        }
    }
    return nearest.has_value() ? cactus_hit_query_result(*nearest) : cactus_empty_query_result();
}

QueryResult2D cactus_query_overlap_deepest(entt::registry& registry,
                                           entt::entity subject_entity,
                                           int mask,
                                           entt::entity exclude) {
    CactusFlatColliderRef subject;
    if (!cactus_find_flat_collider(registry, subject_entity, subject)) {
        return cactus_empty_query_result();
    }

    std::vector<CactusFlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    std::optional<QueryContact2D> deepest;
    float deepest_amount = 0.0F;
    for (const auto& candidate : colliders) {
        if (candidate.entity == exclude || !cactus_query_mask_allows(candidate, mask)) {
            continue;
        }
        const auto contact = cactus_flat_overlap_contact(subject, candidate);
        if (!contact.has_value()) {
            continue;
        }
        const float amount = std::abs(contact->overlap.x) + std::abs(contact->overlap.y);
        if (!deepest.has_value() || amount > deepest_amount) {
            deepest = *contact;
            deepest_amount = amount;
        }
    }
    return deepest.has_value() ? cactus_hit_query_result(*deepest) : cactus_empty_query_result();
}

std::vector<QueryContact2D> cactus_query_overlap_all(entt::registry& registry,
                                                     entt::entity subject_entity,
                                                     int mask,
                                                     entt::entity exclude) {
    CactusFlatColliderRef subject;
    if (!cactus_find_flat_collider(registry, subject_entity, subject)) {
        return {};
    }

    std::vector<CactusFlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    std::vector<QueryContact2D> contacts;
    for (const auto& candidate : colliders) {
        if (candidate.entity == exclude || !cactus_query_mask_allows(candidate, mask)) {
            continue;
        }
        if (const auto contact = cactus_flat_overlap_contact(subject, candidate)) {
            contacts.push_back(*contact);
        }
    }
    return contacts;
}

void cactus_dispatch_stdlib_flat_collisions(entt::registry& registry, entt::dispatcher& dispatcher) {
    std::vector<CactusFlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    for (std::size_t i = 0; i < colliders.size(); ++i) {
        for (std::size_t j = i + 1; j < colliders.size(); ++j) {
            const auto& lhs = colliders[i];
            const auto& rhs = colliders[j];
            if (!cactus_collision_masks_allow(lhs.collider, rhs.collider)) {
                continue;
            }
            const auto overlap = cactus_flat_box_overlap(lhs, rhs);
            if (overlap.x == 0.0F && overlap.y == 0.0F) {
                continue;
            }
            dispatcher.trigger(CollisionEnterEvent{.other = rhs.entity, .overlap = overlap});
            dispatcher.trigger(CollisionEnterEvent{.other = lhs.entity,
                                                   .overlap = Vector2{.x = -overlap.x, .y = -overlap.y}});
        }
    }
}

)";
}

std::string emit_volume_collision_helpers() {
    return R"(
// ── Stdlib 3D Collider Runtime ───────────────────────────────────────────────

namespace {

struct CactusVolumeColliderRef {
    entt::entity entity{entt::null};
    Vector3 position{};
    Collider collider{};
    Vector3 half_extents{};
};

bool cactus_collision_masks_allow(const Collider& lhs, const Collider& rhs) noexcept {
    return ((lhs.mask & rhs.layer) != 0) && ((rhs.mask & lhs.layer) != 0);
}

Vector3 cactus_volume_box_overlap(const CactusVolumeColliderRef& lhs, const CactusVolumeColliderRef& rhs) noexcept {
    const float lhs_center_x = lhs.position.x + lhs.half_extents.x;
    const float lhs_center_y = lhs.position.y + lhs.half_extents.y;
    const float lhs_center_z = lhs.position.z + lhs.half_extents.z;
    const float rhs_center_x = rhs.position.x + rhs.half_extents.x;
    const float rhs_center_y = rhs.position.y + rhs.half_extents.y;
    const float rhs_center_z = rhs.position.z + rhs.half_extents.z;
    const float delta_x = rhs_center_x - lhs_center_x;
    const float delta_y = rhs_center_y - lhs_center_y;
    const float delta_z = rhs_center_z - lhs_center_z;
    const float overlap_x = (lhs.half_extents.x + rhs.half_extents.x) - std::abs(delta_x);
    const float overlap_y = (lhs.half_extents.y + rhs.half_extents.y) - std::abs(delta_y);
    const float overlap_z = (lhs.half_extents.z + rhs.half_extents.z) - std::abs(delta_z);
    if (overlap_x <= 0.0F || overlap_y <= 0.0F || overlap_z <= 0.0F) {
        return Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    }
    if (overlap_x <= overlap_y && overlap_x <= overlap_z) {
        return Vector3{.x = delta_x < 0.0F ? -overlap_x : overlap_x, .y = 0.0F, .z = 0.0F};
    }
    if (overlap_y <= overlap_z) {
        return Vector3{.x = 0.0F, .y = delta_y < 0.0F ? -overlap_y : overlap_y, .z = 0.0F};
    }
    return Vector3{.x = 0.0F, .y = 0.0F, .z = delta_z < 0.0F ? -overlap_z : overlap_z};
}

Vector3 cactus_volume_normal(Vector3 overlap) noexcept {
    if (overlap.x != 0.0F) {
        return Vector3{.x = overlap.x < 0.0F ? -1.0F : 1.0F, .y = 0.0F, .z = 0.0F};
    }
    if (overlap.y != 0.0F) {
        return Vector3{.x = 0.0F, .y = overlap.y < 0.0F ? -1.0F : 1.0F, .z = 0.0F};
    }
    return Vector3{.x = 0.0F, .y = 0.0F, .z = overlap.z < 0.0F ? -1.0F : 1.0F};
}

void cactus_collect_volume_colliders(entt::registry& registry, std::vector<CactusVolumeColliderRef>& colliders) {
    auto boxes = registry.view<WorldTransform, Collider, BoxCollider>();
    boxes.each([&](entt::entity entity,
                   const WorldTransform& transform,
                   const Collider& collider,
                   const BoxCollider& box) {
        colliders.push_back(CactusVolumeColliderRef{.entity       = entity,
                                                    .position     = transform.position,
                                                    .collider     = collider,
                                                    .half_extents = Vector3{.x = box.size.x * 0.5F,
                                                                            .y = box.size.y * 0.5F,
                                                                            .z = box.size.z * 0.5F}});
    });
    auto spheres = registry.view<WorldTransform, Collider, SphereCollider>();
    spheres.each([&](entt::entity entity,
                     const WorldTransform& transform,
                     const Collider& collider,
                     const SphereCollider& sphere) {
        colliders.push_back(CactusVolumeColliderRef{.entity   = entity,
                                                    .position = Vector3{.x = transform.position.x - sphere.radius,
                                                                        .y = transform.position.y - sphere.radius,
                                                                        .z = transform.position.z - sphere.radius},
                                                    .collider = collider,
                                                    .half_extents = Vector3{.x = sphere.radius,
                                                                            .y = sphere.radius,
                                                                            .z = sphere.radius}});
    });
    auto capsules = registry.view<WorldTransform, Collider, CapsuleCollider>();
    capsules.each([&](entt::entity entity,
                      const WorldTransform& transform,
                      const Collider& collider,
                      const CapsuleCollider& capsule) {
        colliders.push_back(CactusVolumeColliderRef{.entity   = entity,
                                                    .position = Vector3{.x = transform.position.x - capsule.radius,
                                                                        .y = transform.position.y - (capsule.height * 0.5F),
                                                                        .z = transform.position.z - capsule.radius},
                                                    .collider = collider,
                                                    .half_extents = Vector3{.x = capsule.radius,
                                                                            .y = capsule.height * 0.5F,
                                                                            .z = capsule.radius}});
    });
}

}  // namespace

void cactus_dispatch_stdlib_volume_collisions(entt::registry& registry, entt::dispatcher& dispatcher) {
    std::vector<CactusVolumeColliderRef> colliders;
    cactus_collect_volume_colliders(registry, colliders);
    for (std::size_t i = 0; i < colliders.size(); ++i) {
        for (std::size_t j = i + 1; j < colliders.size(); ++j) {
            const auto& lhs = colliders[i];
            const auto& rhs = colliders[j];
            if (!cactus_collision_masks_allow(lhs.collider, rhs.collider)) {
                continue;
            }
            const auto overlap = cactus_volume_box_overlap(lhs, rhs);
            if (overlap.x == 0.0F && overlap.y == 0.0F && overlap.z == 0.0F) {
                continue;
            }
            const auto normal = cactus_volume_normal(overlap);
            dispatcher.trigger(CollisionEnterEvent{.other = rhs.entity, .point = lhs.position, .normal = normal});
            dispatcher.trigger(CollisionEnterEvent{.other = lhs.entity,
                                                   .point = rhs.position,
                                                   .normal = Vector3{.x = -normal.x,
                                                                      .y = -normal.y,
                                                                      .z = -normal.z}});
        }
    }
}

)";
}

std::string emit_backend_main() {
    std::ostringstream out;
    out << "\n// ── Backend Entry Point ───────────────────────────────────────────────\n\n";
    out << "#ifndef CACTUS_GENERATED_NO_MAIN\n";
    out << "int main() try {\n";
    out << "    const auto config = cactus::runtime::entt_backend::generated_project_config();\n";
    out << "    InitWindow(config.window_width, config.window_height, config.window_title);\n";
    out << "    SetTargetFPS(config.target_fps);\n\n";
    out << "    entt::registry registry;\n";
    out << "    entt::dispatcher dispatcher;\n";
    out << "    cactus::runtime::entt_backend::generated_setup_dispatcher(dispatcher);\n";
    out << "    cactus::runtime::entt_backend::generated_init_project(registry);\n";
    out << "    while (!WindowShouldClose()) {\n";
    out << "        const float dt = GetFrameTime();\n";
    out << "        cactus::runtime::entt_backend::generated_update_project(registry, dispatcher, dt);\n";
    out << "        BeginDrawing();\n";
    out << "        ClearBackground(RAYWHITE);\n";
    out << "        cactus::runtime::entt_backend::generated_render_project(registry, dispatcher);\n";
    out << "        EndDrawing();\n";
    out << "    }\n\n";
    out << "    CloseWindow();\n";
    out << "    return 0;\n";
    out << "} catch (...) {\n";
    out << "    return 1;\n";
    out << "}\n";
    out << "#endif  // CACTUS_GENERATED_NO_MAIN\n";
    return out.str();
}
}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string CppEnttCodegen::generate(const DecoratedProgram& program) {
    std::ostringstream out;

    const bool uses_flat     = module_uses_camera_flat(program);
    const bool uses_viewport = module_uses_camera_viewport(program);
    const bool uses_volume   = module_uses_camera_volume(program);
    const bool uses_text     = uses_text_format(program);
    const bool uses_editor   = module_uses_editor(program);

    // Header
    out << "// Generated by Cactus DSL Compiler (cpp-entt backend)\n\n";
    out << "// NOLINTBEGIN(modernize-use-std-numbers,readability-function-cognitive-complexity,"
           "bugprone-branch-clone,bugprone-reserved-identifier,bugprone-throwing-static-initialization,"
           "cppcoreguidelines-init-variables,cppcoreguidelines-pro-type-member-init,"
           "readability-redundant-member-init,readability-simplify-boolean-expr,"
           "readability-braces-around-statements,readability-isolate-declaration,"
           "readability-math-missing-parentheses,readability-qualified-auto,readability-redundant-parentheses)\n";
    out << "// Generated C++ mirrors authored DSL constants, declarations, and system control flow.\n\n";
    out << "#include \"backends/cpp-entt/runtime.hpp\"\n";
    out << "\n";
    out << "#include <entt/entt.hpp>\n";
    out << "#include <raylib.h>\n";
    out << "\n";
    out << "#ifdef PI\n";
    out << "#undef PI\n";
    out << "#endif\n";
    // raylib defines `typedef Camera3D Camera;` which conflicts with the Camera DSL
    // component struct. Redirect the token so struct Camera becomes struct CactusCamera.
    // Use Camera3D directly for any raylib 3D-camera uses.
    if (uses_flat || uses_volume) {
        out << "// Suppress raylib Camera typedef; DSL Camera struct takes this name\n";
        out << "#define Camera CactusCamera\n";
    }
    out << "\n";
    if (uses_viewport) {
        out << "#include <algorithm>\n";
    }
    out << "#include <array>\n";
    out << "#include <cmath>\n";
    out << "#include <cstdint>\n";
    out << "#include <limits>\n";
    out << "#include <optional>\n";
    out << "#include <string>\n";
    out << "#include <unordered_map>\n";
    out << "#include <unordered_set>\n";
    out << "#include <vector>\n";
    if (uses_text) {
        out << "#include <format>\n";
    }
    // Task 6.2: Include runtime header when extern funcs are present
    if (has_extern_funcs(program)) {
        out << "#include \"cactus_runtime.hpp\"\n";
    }
    out << "\n";

    out << "using Quat = cactus::runtime::Quat;\n";
    out << "\n";

    // Helper constructors for generated DSL built-ins
    out << "inline Vector2 vec2(float x, float y) {\n";
    out << "    return Vector2{.x = x, .y = y};\n";
    out << "}\n\n";

    out << "inline Vector3 vec3(float x, float y, float z) {\n";
    out << "    return Vector3{.x = x, .y = y, .z = z};\n";
    out << "}\n\n";

    out << "inline cactus::runtime::Quat quat(float x, float y, float z, float w) {\n";
    out << "    return cactus::runtime::Quat{.x = x, .y = y, .z = z, .w = w};\n";
    out << "}\n\n";

    // Built-in runtime helpers for generated examples
    out << "struct TickEvent {\n";
    out << "    float dt;\n";
    out << "};\n";
    out << "\n";

    if (program.ast != nullptr) {
        bool has_axis_input   = false;
        bool has_button_input = false;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                has_axis_input   = has_axis_input || input->input_kind == InputKind::Axis;
                has_button_input = has_button_input || input->input_kind == InputKind::Button;
            }
        }

        if (has_button_input) {
            out << "using InputButton = std::uint8_t;\n";
            std::uint8_t button_index = 0;
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Button) {
                        continue;
                    }
                    out << "[[maybe_unused]] constexpr InputButton " << input_action_constant_name(input->name)
                        << " = static_cast<InputButton>(" << static_cast<int>(button_index++) << ");\n";
                }
            }
            out << "\n";

            out << "namespace cactus::runtime::entt_backend {\n";
            out << "int cactus_input_button_key(std::uint8_t button) noexcept {\n";
            out << "    switch (button) {\n";
            button_index = 0;
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Button) {
                        continue;
                    }
                    std::string key = "0";
                    for (const auto& prop : input->props) {
                        if (prop.key == "key") {
                            if (auto maybe_key = raylib_key_constant(*prop.value)) {
                                key = *maybe_key;
                            } else if (auto maybe_mouse = raylib_mouse_constant(*prop.value)) {
                                key = *maybe_mouse;
                            }
                        }
                    }
                    out << "        case static_cast<InputButton>(" << static_cast<int>(button_index++) << "): return "
                        << key << ";\n";
                }
            }
            out << "        default:\n";
            out << "            break;\n";
            out << "    }\n";
            out << "    return 0;\n";
            out << "}\n";
            out << "int cactus_input_button_mouse(std::uint8_t button) noexcept {\n";
            out << "    switch (button) {\n";
            button_index = 0;
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Button) {
                        continue;
                    }
                    std::string mouse = "-1";
                    for (const auto& prop : input->props) {
                        if (prop.key == "mouse") {
                            if (auto maybe_mouse = raylib_mouse_constant(*prop.value)) {
                                mouse = *maybe_mouse;
                            }
                        }
                    }
                    out << "        case static_cast<InputButton>(" << static_cast<int>(button_index++) << "): return "
                        << mouse << ";\n";
                }
            }
            out << "        default:\n";
            out << "            break;\n";
            out << "    }\n";
            out << "    return -1;\n";
            out << "}\n";
            out << "}  // namespace cactus::runtime::entt_backend\n\n";
        }

        if (has_axis_input) {
            out << "using InputAxis = std::uint8_t;\n";
            out << "enum class CactusInputAxisTag : std::uint8_t { ";
            bool first = true;
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Axis) {
                        continue;
                    }
                    out << (first ? "" : ", ") << input->name;
                    first = false;
                }
            }
            out << " };\n\n";

            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Axis) {
                        continue;
                    }
                    out << "[[maybe_unused]] constexpr InputAxis " << input_action_constant_name(input->name)
                        << " = static_cast<InputAxis>(CactusInputAxisTag::" << input->name << ");\n";
                }
            }
            out << "\n";

            out << "namespace cactus::runtime::entt_backend {\n";
            out << "float cactus_input_axis_value(std::uint8_t action) noexcept {\n";
            out << "    switch (action) {\n";
            for (const auto& decl : program.ast->declarations) {
                if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                    if (input->input_kind != InputKind::Axis) {
                        continue;
                    }
                    std::string negative = "0";
                    std::string positive = "0";
                    for (const auto& prop : input->props) {
                        if (prop.key == "negative") {
                            if (auto key = raylib_key_constant(*prop.value)) {
                                negative = "(IsKeyDown(" + *key + ") ? 1.0F : 0.0F)";
                            }
                        } else if (prop.key == "positive") {
                            if (auto key = raylib_key_constant(*prop.value)) {
                                positive = "(IsKeyDown(" + *key + ") ? 1.0F : 0.0F)";
                            }
                        }
                    }
                    out << "        case static_cast<InputAxis>(CactusInputAxisTag::" << input->name << "):\n";
                    out << "            return " << positive << " - " << negative << ";\n";
                }
            }
            out << "        default:\n";
            out << "            return 0.0F;\n";
            out << "    }\n";
            out << "}\n\n";
            out << "}  // namespace cactus::runtime::entt_backend\n\n";
        }

        if (has_axis_input || has_button_input) {
            out << "struct InputEvent {\n";
            if (has_axis_input) {
                out << "    [[nodiscard]] static float axis(InputAxis action) {\n";
                out << "        return cactus::runtime::entt_backend::axis(action);\n";
                out << "    }\n";
            }
            if (has_button_input) {
                out << "    [[nodiscard]] static bool pressed(InputButton action) {\n";
                out << "        return cactus::runtime::entt_backend::pressed(action);\n";
                out << "    }\n";
                out << "    [[nodiscard]] static bool down(InputButton action) {\n";
                out << "        return cactus::runtime::entt_backend::down(action);\n";
                out << "    }\n";
                out << "    [[nodiscard]] static bool released(InputButton action) {\n";
                out << "        return cactus::runtime::entt_backend::released(action);\n";
                out << "    }\n";
            }
            out << "};\n\n";
        }
    }

    if (program.ast != nullptr) {
        for (const auto& decl : program.ast->declarations) {
            if (const auto* cb = std::get_if<ConstBlockNode>(&decl)) {
                for (const auto& ca : cb->assignments) {
                    if (ca.name == "WINDOW_WIDTH" || ca.name == "WINDOW_HEIGHT" || ca.name == "WINDOW_TITLE" ||
                        ca.name == "TARGET_FPS") {
                        continue;
                    }
                    out << "[[maybe_unused]] constexpr auto " << upper_copy(ca.name) << " = "
                        << EnttCodegenUtils::emit_expr(*ca.value, program.ast) << ";\n";
                }
            }
        }
        out << "\n";
    }

    if (program.ast != nullptr) {
        std::uint32_t next_asset_handle = 1U;
        bool emitted_asset_constants    = false;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* asset = std::get_if<AssetDeclNode>(&decl)) {
                if (!emitted_asset_constants) {
                    out << "// ── Asset Handles ──────────────────────────────────────────────────\n\n";
                    emitted_asset_constants = true;
                }
                out << "constexpr cactus::runtime::AssetHandle " << asset->name << " = " << next_asset_handle++
                    << "U; // NOLINT(readability-identifier-naming)\n";
            }
        }
        if (emitted_asset_constants) {
            out << "\n";
        }
    }

    // Enums
    for (const auto& [name, e] : program.enums) {
        auto emitted_code = EnttComponentEmitter::emit_enum(e);
        if (!emitted_code.empty()) {
            out << emitted_code << "\n";
        }
    }

    // POD structs
    for (const auto& [name, s] : program.structs) {
        out << EnttComponentEmitter::emit_pod_struct(s) << "\n";
    }

    // Component structs (from traits)
    for (const auto& [name, t] : program.traits) {
        out << EnttComponentEmitter::emit_component(t) << "\n";
    }

    out << emit_projected_trait_registry_helpers(program);

    // Events
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* event = std::get_if<EventNode>(&decl)) {
                out << EnttEventEmitter::emit_event(*event, program) << "\n";
            }
        }
    }

    const bool has_flat_colliders   = has_flat_collider_support(program);
    const bool has_volume_colliders = has_volume_collider_support(program);
    if ((has_flat_colliders || has_volume_colliders) && !has_collision_event_decl(program.ast)) {
        out << "struct CollisionEnterEvent {\n";
        out << "    entt::entity other{};\n";
        if (has_volume_colliders && !has_flat_colliders) {
            out << "    Vector3 point{};\n";
            out << "    Vector3 normal{};\n";
        } else {
            out << "    Vector2 overlap{};\n";
        }
        out << "};\n\n";
    }

    if (has_flat_colliders) {
        out << emit_flat_collision_helpers();
    } else if (has_flat_physics_query_api(program)) {
        out << emit_flat_query_fallback_helpers();
    }
    if (has_volume_colliders) {
        out << emit_volume_collision_helpers();
    }

    out << EnttSystemEmitter::emit_entt_hierarchy_helpers(program);

    // Persist serialization stubs
    out << "// ── Persist Serialization ────────────────────────────────────────────\n\n";
    for (const auto& [name, t] : program.traits) {
        bool has_persist = false;
        for (const auto& f : t.fields) {
            if (f.is_persist) {
                has_persist = true;
                break;
            }
        }
        if (has_persist) {
            out << "void save_" << name << "(const " << name << "& comp) {\n";
            out << "    (void)comp;\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    out << "    // serialize comp." << f.name << "\n";
                }
            }
            out << "}\n\n";
            out << "void load_" << name << "(" << name << "& comp) {\n";
            out << "    (void)comp;\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    out << "    // deserialize into comp." << f.name << "\n";
                }
            }
            out << "}\n\n";
        }
    }

    // Sync replication stubs
    out << "// ── Sync Replication ─────────────────────────────────────────────────\n\n";
    for (const auto& [name, t] : program.traits) {
        bool has_sync = false;
        for (const auto& f : t.fields) {
            if (f.is_sync) {
                has_sync = true;
                break;
            }
        }
        if (has_sync) {
            out << "void replicate_" << name << "(const " << name << "& comp) {\n";
            out << "    (void)comp;\n";
            for (const auto& f : t.fields) {
                if (f.is_sync) {
                    out << "    // send delta for comp." << f.name << "\n";
                }
            }
            out << "}\n\n";
        }
    }

    // Entity creation from flattened templates and entities (inline and template-backed)
    if (program.ast != nullptr) {
        out << "// ── Entity Creation ─────────────────────────────────────────────────\n\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* tmpl = std::get_if<TemplateNode>(&decl)) {
                out << emit_archetype_creation_function(tmpl->name, tmpl->traits, program);
            }
        }
        for (auto& decl : program.ast->declarations) {
            if (auto* entity = std::get_if<EntityNode>(&decl)) {
                out << emit_archetype_creation_function(entity->name, entity->traits, program);
            }
        }
    }

    // Template registry (used by editor_spawn_template and EditorTemplatePalette)
    if (uses_editor && program.ast != nullptr) {
        out << "// ── Template Registry ───────────────────────────────────────────────\n\n";
        out << "using CactusTemplateFactory = entt::entity(*)(entt::registry&);\n";
        out << "static const std::unordered_map<std::string, CactusTemplateFactory> cactus_template_registry = {\n";
        for (const auto& decl : program.ast->declarations) {
            if (const auto* tmpl = std::get_if<TemplateNode>(&decl)) {
                if (tmpl->is_pub) {
                    out << "    {\"" << tmpl->name << "\", " << archetype_create_function_name(tmpl->name) << "},\n";
                }
            }
        }
        out << "};\n\n";
    }

    // System functions
    if (program.ast != nullptr) {
        out << "// ── Systems ─────────────────────────────────────────────────────────\n\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* sys = std::get_if<SystemNode>(&decl)) {
                out << EnttSystemEmitter::emit_system(*sys, program);
            }
            if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
                out << EnttSystemEmitter::emit_extern_system(*sys, program);
            }
        }
    }

    // Dispatcher setup
    out << "// ── Event Dispatcher ────────────────────────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";
    out << "void generated_setup_dispatcher(entt::dispatcher& dispatcher) {\n";
    if (program.ast != nullptr) {
        out << "    (void)dispatcher;\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* event = std::get_if<EventNode>(&decl)) {
                out << "    " << EnttEventEmitter::emit_sink_connection(*event);
            }
        }
    }
    out << "}\n\n";

    // Main game loop — extract window constants from const block if available
    std::string win_width  = "800";
    std::string win_height = "600";
    std::string win_title  = "\"Cactus Game\"";
    std::string win_fps    = "60";
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* cb = std::get_if<ConstBlockNode>(&decl)) {
                for (auto& ca : cb->assignments) {
                    if (ca.name == "WINDOW_WIDTH") {
                        win_width = EnttCodegenUtils::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "WINDOW_HEIGHT") {
                        win_height = EnttCodegenUtils::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "WINDOW_TITLE") {
                        win_title = EnttCodegenUtils::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "TARGET_FPS") {
                        win_fps = EnttCodegenUtils::emit_expr(*ca.value, program.ast);
                    }
                }
            }
        }
    }

    out << "// ── Runtime Glue ────────────────────────────────────────────────────\n\n";
    out << "ProjectConfig generated_project_config() noexcept {\n";
    out << "    return {.window_width = " << win_width << ", .window_height = " << win_height
        << ", .window_title = " << win_title << ", .target_fps = " << win_fps << "};\n";
    out << "}\n\n";

    out << "void generated_init_project(entt::registry& registry) {\n";
    out << "    (void)registry;\n";
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* asset = std::get_if<AssetDeclNode>(&decl)) {
                const auto registration = asset_register_call(*asset);
                if (!registration.empty()) {
                    out << "    " << registration << "\n";
                }
            }
        }
        for (auto& decl : program.ast->declarations) {
            if (auto* entity = std::get_if<EntityNode>(&decl)) {
                out << "    create_" << snake_case(entity->name) << "(registry);\n";
            }
        }
    }
    if (uses_editor && program.traits.contains("WorldTransform") &&
        program.traits.contains("BoxCollider")) {
        out << "    cactus::runtime::entt_backend::register_editor_hit_test_impl(\n";
        out << "        [](entt::registry& reg, Vector2 world_pos, int /*mask*/) -> entt::entity {\n";
        out << "            auto view = reg.view<WorldTransform, BoxCollider>(entt::exclude<EditorLocked>);\n";
        out << "            for (auto entity : view) {\n";
        out << "                const auto& xform = reg.get<WorldTransform>(entity);\n";
        out << "                const auto& box   = reg.get<BoxCollider>(entity);\n";
        out << "                if (world_pos.x < xform.position.x || world_pos.x > xform.position.x + box.size.x) { continue; }\n";
        out << "                if (world_pos.y < xform.position.y || world_pos.y > xform.position.y + box.size.y) { continue; }\n";
        out << "                return entity;\n";
        out << "            }\n";
        out << "            return entt::entity{entt::null};\n";
        out << "        });\n";
    }
    if (uses_editor && program.traits.contains("LocalTransform") &&
        program.traits.contains("WorldTransform")) {
        out << "    cactus::runtime::entt_backend::register_editor_spawn_impl(\n";
        out << "        [](entt::registry& reg, const std::string& name, Vector2 pos2d, Vector3 /*pos3d*/) -> entt::entity {\n";
        out << "            auto it = cactus_template_registry.find(name);\n";
        out << "            if (it == cactus_template_registry.end()) { return entt::entity{entt::null}; }\n";
        out << "            auto entity = it->second(reg);\n";
        out << "            if (auto* lt = reg.try_get<LocalTransform>(entity)) { lt->position = pos2d; }\n";
        out << "            if (auto* wt = reg.try_get<WorldTransform>(entity)) { wt->position = pos2d; }\n";
        out << "            return entity;\n";
        out << "        });\n";
    }
    out << "}\n\n";

    out << "void generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt) {\n";

    out << "    (void)dt;\n\n";

    // Call system input handlers
    if (program.ast != nullptr) {
        bool emits_input_handler = false;
        for (auto& decl : program.ast->declarations) {
            if (auto* sys = std::get_if<SystemNode>(&decl)) {
                for (auto& handler : sys->handlers) {
                    if (handler.event_name == "input") {
                        emits_input_handler = true;
                        break;
                    }
                }
            }
            if (emits_input_handler) {
                break;
            }
        }
        if (emits_input_handler) {
            out << "    auto input = InputEvent{};\n";
            for (auto& decl : program.ast->declarations) {
                if (auto* sys = std::get_if<SystemNode>(&decl)) {
                    for (auto& handler : sys->handlers) {
                        if (handler.event_name == "input") {
                            out << "    " << system_function_name(sys->name, "input") << "(registry, dispatcher, input);\n";
                        }
                    }
                }
            }
            out << "\n";
        }
    }

    // Call non-render system tick handlers
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* sys = std::get_if<SystemNode>(&decl)) {
                for (auto& handler : sys->handlers) {
                    if (handler.event_name == "tick") {
                        out << "    " << system_function_name(sys->name, "tick") << "(registry, dispatcher, TickEvent{dt});\n";
                    }
                }
            }
            if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
                if (!is_update_phase_extern(*sys, program)) {
                    continue;
                }
                out << "    " << system_function_name(sys->name, "tick") << "(registry);\n";
            }
        }
    }

    if (has_flat_colliders) {
        out << "    ::cactus_dispatch_stdlib_flat_collisions(registry, dispatcher);\n";
    }
    if (has_volume_colliders) {
        out << "    ::cactus_dispatch_stdlib_volume_collisions(registry, dispatcher);\n";
    }

    out << "    dispatcher.update();\n";
    out << "}\n\n";

    // ── translate_camera helpers (emitted when viewport loop is active) ──────
    const bool emit_2d_helper = uses_viewport && uses_flat;
    const bool emit_3d_helper = uses_viewport && uses_volume &&
        trait_field_is(program, "WorldTransform", "position", TypeKind::Vec3) &&
        trait_field_is(program, "WorldTransform", "rotation", TypeKind::Quat);
    if (emit_2d_helper || emit_3d_helper) {
        out << "namespace {\n";
        if (emit_2d_helper) {
            out << "Camera2D __translate_camera_2d(const Camera& cam, int sw, int sh) noexcept {\n";
            out << "    Camera2D cam2d{};\n";
            out << "    cam2d.target   = cam.offset;\n";
            out << "    cam2d.zoom     = cam.zoom;\n";
            out << "    cam2d.rotation = cam.rotation * (180.0F / 3.14159265F);\n";
            out << "    cam2d.offset   = {.x = static_cast<float>(sw) * 0.5F,\n";
            out << "                      .y = static_cast<float>(sh) * 0.5F};\n";
            out << "    return cam2d;\n";
            out << "}\n";
        }
        if (emit_3d_helper) {
            out << "Camera3D __translate_camera_3d(entt::entity entity, const Camera& cam, entt::registry& registry) {\n";
            out << "    Camera3D cam3d{};\n";
            out << "    cam3d.fovy       = cam.fov_y;\n";
            out << "    cam3d.projection = CAMERA_PERSPECTIVE;\n";
            out << "    cam3d.up         = {.x = 0.0F, .y = 1.0F, .z = 0.0F};\n";
            out << "    if (registry.all_of<WorldTransform>(entity)) {\n";
            out << "        const auto& xform = registry.get<WorldTransform>(entity);\n";
            out << "        cam3d.position = xform.position;\n";
            out << "        const auto& q  = xform.rotation;\n";
            out << "        cam3d.target   = {.x = xform.position.x + (-(2.0F * ((q.x * q.z) + (q.w * q.y)))),\n";
            out << "                          .y = xform.position.y + (2.0F * ((q.w * q.x) - (q.y * q.z))),\n";
            out << "                          .z = xform.position.z + (-(1.0F - (2.0F * ((q.x * q.x) + (q.y * q.y)))))};\n";
            out << "    }\n";
            out << "    return cam3d;\n";
            out << "}\n";
        }
        out << "}  // namespace\n\n";
    }

    auto emit_render_phase_calls = [&](std::string_view indent) {
        if (program.ast == nullptr) { return; }
        for (const auto& decl : program.ast->declarations) {
            if (const auto* sys = std::get_if<ExternSystemNode>(&decl)) {
                if (is_render_phase_extern(*sys, program)) {
                    out << indent << system_function_name(sys->name, "tick") << "(registry);\n";
                }
            }
        }
    };

    out << "void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher) {\n";
    out << "    (void)dispatcher;\n";
    if (!uses_viewport) {
        out << "    (void)registry;\n";
    }
    out << "    cactus::runtime::entt_backend::begin_render_frame();\n";

    if (uses_viewport) {
        out << "    {\n";
        out << "        const int __sw = GetScreenWidth();\n";
        out << "        const int __sh = GetScreenHeight();\n";
        out << "        static std::vector<std::pair<int,entt::entity>> __vps;\n";
        out << "        __vps.clear();\n";
        out << "        for (const auto& [__vp_e, __vp] : registry.view<Viewport>().each()) {\n";
        out << "            if (__vp.active) { __vps.emplace_back(__vp.depth, __vp_e); }\n";
        out << "        }\n";
        out << "        std::ranges::sort(__vps);\n";
        out << "        for (auto& [__depth, __vp_ent] : __vps) {\n";
        out << "            (void)__depth;\n";
        out << "            const auto& __vp = registry.get<Viewport>(__vp_ent);\n";
        out << "            BeginScissorMode(\n";
        out << "                static_cast<int>(__vp.x * static_cast<float>(__sw)),\n";
        out << "                static_cast<int>(__vp.y * static_cast<float>(__sh)),\n";
        out << "                static_cast<int>(__vp.width * static_cast<float>(__sw)),\n";
        out << "                static_cast<int>(__vp.height * static_cast<float>(__sh)));\n";
        out << "            if (__vp.clear) { ClearBackground(__vp.clear_color); }\n";
        if (emit_2d_helper) {
            out << "            if (registry.all_of<Camera>(__vp_ent)) {\n";
            out << "                const auto& __cam = registry.get<Camera>(__vp_ent);\n";
            out << "                cactus::runtime::entt_backend::set_active_camera_2d(\n";
            out << "                    __translate_camera_2d(__cam, __sw, __sh));\n";
            out << "            }\n";
        }
        if (emit_3d_helper) {
            if (emit_2d_helper) {
                out << "            else if (registry.all_of<Camera>(__vp_ent)) {\n";
            } else {
                out << "            if (registry.all_of<Camera>(__vp_ent)) {\n";
            }
            out << "                const auto& __cam = registry.get<Camera>(__vp_ent);\n";
            out << "                cactus::runtime::entt_backend::set_active_camera_3d(\n";
            out << "                    __translate_camera_3d(__vp_ent, __cam, registry));\n";
            out << "            }\n";
        }
        emit_render_phase_calls("            ");
        if (emit_3d_helper) {
            // Draw this viewport's 3D meshes now (inside its scissor + camera) so
            // each split-screen region renders the world from its own camera.
            out << "            cactus::runtime::entt_backend::flush_viewport_3d();\n";
        }
        out << "            EndScissorMode();\n";
        out << "        }\n";
        out << "    }\n";
    } else {
        emit_render_phase_calls("    ");
    }

    out << "    cactus::runtime::entt_backend::end_render_frame();\n";

    // Edit-mode overlay drawn after end_render_frame (screen-space, no camera transform)
    if (uses_editor && program.traits.contains("EditorState")) {
        out << "    {\n";
        out << "        bool __editor_active = false;\n";
        out << "        int  __editor_mode   = 0;\n";
        out << "        auto __ed_view = registry.view<EditorState>();\n";
        out << "        for (auto __ed_ent : __ed_view) {\n";
        out << "            const auto& __es = __ed_view.get<EditorState>(__ed_ent);\n";
        out << "            if (__es.active) { __editor_active = true; __editor_mode = __es.mode; break; }\n";
        out << "        }\n";
        out << "        if (__editor_active) {\n";
        out << "            DrawRectangleLinesEx({.x = 0.0F, .y = 0.0F,\n";
        out << "                                  .width  = static_cast<float>(GetScreenWidth()),\n";
        out << "                                  .height = static_cast<float>(GetScreenHeight())}, 3, YELLOW);\n";
        out << "            const std::array<const char*, 5> __mode_names = {\"SELECT\", \"TRANSLATE\", \"ROTATE\", \"SCALE\", \"PLACE\"};\n";
        out << "            const char* __mode_str = (__editor_mode >= 0 && __editor_mode < 5)\n";
        out << "                                         ? __mode_names[static_cast<std::size_t>(__editor_mode)] : \"SELECT\";\n";
        out << "            std::string __hud = std::string(\"EDIT [\") + __mode_str +\n";
        out << "                                \"]  F1:toggle  W:trans  E:rot  R:scale  T:place\";\n";
        out << "            DrawText(__hud.c_str(), 10, 10, 14, YELLOW);\n";
        out << "        }\n";
        out << "    }\n";
    }

    out << "    clear_projected_traits(registry);\n";
    out << "}\n";
    out << "\n}  // namespace cactus::runtime::entt_backend\n";

    out << emit_backend_main();

    out << "\n// NOLINTEND(modernize-use-std-numbers,readability-function-cognitive-complexity,"
           "bugprone-branch-clone,bugprone-reserved-identifier,bugprone-throwing-static-initialization,"
           "cppcoreguidelines-init-variables,cppcoreguidelines-pro-type-member-init,"
           "readability-redundant-member-init,readability-simplify-boolean-expr,"
           "readability-braces-around-statements,readability-isolate-declaration,"
           "readability-math-missing-parentheses,readability-qualified-auto,readability-redundant-parentheses)\n";

    return out.str();
}

}  // namespace cactus
