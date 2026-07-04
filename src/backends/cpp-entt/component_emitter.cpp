#include "backends/cpp-entt/component_emitter.hpp"

#include <sstream>

namespace cactus {

namespace {

std::string entt_type_to_cpp(const TypeInfo& type) {
    if (type.kind == TypeKind::EntityId) {
        return "entt::entity";
    }
    return EnttCodegenUtils::type_to_cpp(type);
}

bool should_defer_to_raylib_enum(const std::string& name) {
    return name == "MouseButton" || name == "GamepadButton" || name == "GamepadAxis";
}

std::string viewport_default(const ResolvedField& field) {
    if (field.name == "width" || field.name == "height") { return "{1.0F}"; }
    if (field.name == "clear" || field.name == "active") { return "{true}"; }
    if (field.name == "clear_color") { return "{.r = 0, .g = 0, .b = 0, .a = 255}"; }
    return {};
}

std::string stdlib_collider_default(const ResolvedTrait& trait, const ResolvedField& field) {
    if (!trait.is_stdlib) {
        return {};
    }
    if (trait.name == "Collider") {
        if (field.name == "layer" || field.name == "mask") {
            return "{1}";
        }
    }
    if (trait.name == "BoxCollider" && field.name == "size") {
        if (field.type.kind == TypeKind::Vec2) {
            return "{.x = 1.0F, .y = 1.0F}";
        }
        if (field.type.kind == TypeKind::Vec3) {
            return "{.x = 1.0F, .y = 1.0F, .z = 1.0F}";
        }
    }
    if ((trait.name == "CircleCollider" || trait.name == "SphereCollider") && field.name == "radius") {
        return "{0.5F}";
    }
    if (trait.name == "CapsuleCollider") {
        if (field.name == "radius") {
            return "{0.5F}";
        }
        if (field.name == "height") {
            return "{1.0F}";
        }
    }
    if (trait.name == "Viewport") { return viewport_default(field); }
    return {};
}

}  // namespace

std::string EnttComponentEmitter::emit_component(const ResolvedTrait& trait) {
    std::ostringstream out;
    if (trait.fields.empty()) {
        out << "struct " << trait.name << " {};\n";
        return out.str();
    }

    out << "struct " << trait.name << " {\n";
    for (const auto& field : trait.fields) {
        out << "    " << entt_type_to_cpp(field.type) << " " << field.name;
        const auto default_value = stdlib_collider_default(trait, field);
        out << (default_value.empty() ? "{}" : default_value) << ";\n";
    }
    out << "};\n";
    return out.str();
}

std::string EnttComponentEmitter::emit_pod_struct(const ResolvedStruct& s) {
    std::ostringstream out;
    out << "struct " << s.name << " {\n";
    for (const auto& field : s.fields) {
        out << "    " << entt_type_to_cpp(field.type) << " " << field.name << ";\n";
    }
    out << "};\n";
    return out.str();
}

std::string EnttComponentEmitter::emit_enum(const ResolvedEnum& e) {
    if (should_defer_to_raylib_enum(e.name)) {
        return "";
    }
    return EnttCodegenUtils::emit_enum(e);
}

}  // namespace cactus
