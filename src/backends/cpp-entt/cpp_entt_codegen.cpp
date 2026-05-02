#include "backends/cpp-entt/cpp_entt_codegen.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/event_emitter.hpp"
#include "backends/cpp-entt/system_emitter.hpp"
#include "backends/cpp-manual/soa_emitter.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace cactus {

namespace {
// Task 6.1: Check if the program has any extern funcs requiring the runtime header
bool has_extern_funcs(const DecoratedProgram& program) {
    for (const auto& [name, func] : program.funcs) {  // NOLINT(readability-use-anyofallof)
        if (func.is_extern) {
            return true;
        }
    }
    return false;
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
        sys.name == "PointLightSystem" || sys.name == "DirectionalLightSystem") {
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
           find_field(collider, "mask") != nullptr && trait_field_is(program, "BoxCollider", "size", TypeKind::Vec2) &&
           trait_field_is(program, "CircleCollider", "radius", TypeKind::Float) &&
           trait_field_is(program, "CapsuleCollider", "height", TypeKind::Float);
}

bool has_volume_collider_support(const DecoratedProgram& program) {
    const auto* collider = find_trait(program, "Collider");
    return collider != nullptr && collider->is_stdlib && find_field(collider, "layer") != nullptr &&
           find_field(collider, "mask") != nullptr && trait_field_is(program, "BoxCollider", "size", TypeKind::Vec3) &&
           trait_field_is(program, "SphereCollider", "radius", TypeKind::Float) &&
           trait_field_is(program, "CapsuleCollider", "height", TypeKind::Float);
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

}  // namespace

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

    // Header
    out << "// Generated by Cactus DSL Compiler (cpp-entt backend)\n\n";
    out << "// NOLINTBEGIN(modernize-use-std-numbers,readability-function-cognitive-complexity)\n";
    out << "// Generated C++ mirrors authored DSL constants and system control flow.\n\n";
    out << "#include \"backends/cpp-entt/runtime.hpp\"\n";
    out << "\n";
    out << "#include <entt/entt.hpp>\n";
    out << "#include <raylib.h>\n";
    out << "\n";
    out << "#ifdef PI\n";
    out << "#undef PI\n";
    out << "#endif\n";
    out << "\n";
    out << "#include <cmath>\n";
    out << "#include <cstdint>\n";
    out << "#include <string>\n";
    out << "#include <unordered_set>\n";
    out << "#include <vector>\n";
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
                        << ManualSystemEmitter::emit_expr(*ca.value, program.ast) << ";\n";
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
        out << "    entt::entity other;\n";
        if (has_volume_colliders && !has_flat_colliders) {
            out << "    Vector3 point;\n";
            out << "    Vector3 normal;\n";
        } else {
            out << "    Vector2 overlap;\n";
        }
        out << "};\n\n";
    }

    if (has_flat_colliders) {
        out << emit_flat_collision_helpers();
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

    // Entity creation from units
    if (program.ast != nullptr) {
        out << "// ── Entity Creation ─────────────────────────────────────────────────\n\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* unit = std::get_if<UnitNode>(&decl)) {
                out << "entt::entity create_" << snake_case(unit->name) << "(entt::registry& registry) {\n";
                out << "    auto entity = registry.create();\n";
                for (const auto& trait : unit->traits) {
                    auto resolved_trait = program.traits.find(trait.trait_name);
                    if (resolved_trait != program.traits.end() && resolved_trait->second.fields.empty()) {
                        out << "    registry.emplace<" << trait.trait_name << ">(entity);\n";
                        continue;
                    }
                    out << "    {\n";
                    std::size_t widest = std::string("auto component").size();
                    for (const auto& assignment : trait.assignments) {
                        widest = std::max(widest, std::string("component.").size() + assignment.name.size());
                    }
                    out << "        " << pad_to_width("auto component", widest) << " = " << trait.trait_name << "{};\n";
                    for (const auto& assignment : trait.assignments) {
                        out << "        " << pad_to_width("component." + assignment.name, widest) << " = "
                            << ManualSystemEmitter::emit_expr(*assignment.value, program.ast) << ";\n";
                    }
                    out << "        registry.emplace<" << trait.trait_name << ">(entity, component);\n";
                    out << "    }\n";
                }
                out << "    return entity;\n";
                out << "}\n\n";
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
                        win_width = ManualSystemEmitter::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "WINDOW_HEIGHT") {
                        win_height = ManualSystemEmitter::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "WINDOW_TITLE") {
                        win_title = ManualSystemEmitter::emit_expr(*ca.value, program.ast);
                    } else if (ca.name == "TARGET_FPS") {
                        win_fps = ManualSystemEmitter::emit_expr(*ca.value, program.ast);
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
            if (auto* unit = std::get_if<UnitNode>(&decl)) {
                out << "    create_" << snake_case(unit->name) << "(registry);\n";
            }
        }
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
                            out << "    " << system_function_name(sys->name, "input") << "(registry, input);\n";
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
                        out << "    " << system_function_name(sys->name, "tick") << "(registry, TickEvent{dt});\n";
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

    out << "void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher) {\n";
    out << "    (void)registry;\n";
    out << "    (void)dispatcher;\n";
    out << "    cactus::runtime::entt_backend::begin_render_frame();\n";
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
                if (is_render_phase_extern(*sys, program)) {
                    out << "    " << system_function_name(sys->name, "tick") << "(registry);\n";
                }
            }
        }
    }
    out << "    cactus::runtime::entt_backend::end_render_frame();\n";
    out << "}\n";
    out << "\n}  // namespace cactus::runtime::entt_backend\n";

    out << emit_backend_main();

    out << "\n// NOLINTEND(modernize-use-std-numbers,readability-function-cognitive-complexity)\n";

    return out.str();
}

}  // namespace cactus
