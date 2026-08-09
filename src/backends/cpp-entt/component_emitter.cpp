#include "backends/cpp-entt/component_emitter.hpp"

#include "frontend/symbol_identity.hpp"

#include <sstream>
#include <unordered_map>

namespace cactus {

namespace {

std::string entt_type_to_cpp(const TypeInfo& type) {
    if (type.kind == TypeKind::EntityId) {
        return "entt::entity";
    }
    if (type.kind == TypeKind::List && type.element != nullptr) {
        return "std::vector<" + entt_type_to_cpp(*type.element) + ">";
    }
    return EnttCodegenUtils::type_to_cpp(type);
}

bool should_defer_to_raylib_enum(const std::string& name) {
    return name == "MouseButton" || name == "GamepadButton" || name == "GamepadAxis";
}

std::string stdlib_trait_default(const ResolvedTrait& trait, const ResolvedField& field) {
    if (!trait.is_stdlib) {
        return {};
    }
    // BoxCollider.size's default depends on the field's own resolved type
    // (Vec2 vs Vec3), so it can't be a flat (trait, field) -> value lookup.
    if (trait.name == "BoxCollider" && field.name == "size") {
        if (field.type.kind == TypeKind::Vec2) {
            return "{.x = 1.0F, .y = 1.0F}";
        }
        if (field.type.kind == TypeKind::Vec3) {
            return "{.x = 1.0F, .y = 1.0F, .z = 1.0F}";
        }
        return {};
    }
    static const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> defaults{
        {"Collider", {{"layer", "{1}"}, {"mask", "{1}"}}},
        {"CircleCollider", {{"radius", "{0.5F}"}}},
        {"SphereCollider", {{"radius", "{0.5F}"}}},
        {"CapsuleCollider", {{"radius", "{0.5F}"}, {"height", "{1.0F}"}}},
        {"Viewport",
         {{"width", "{1.0F}"},
          {"height", "{1.0F}"},
          {"clear", "{true}"},
          {"active", "{true}"},
          {"clear_color", "{.r = 0, .g = 0, .b = 0, .a = 255}"}}},
    };
    const auto trait_it = defaults.find(trait.name);
    if (trait_it == defaults.end()) {
        return {};
    }
    const auto field_it = trait_it->second.find(field.name);
    return field_it == trait_it->second.end() ? std::string{} : field_it->second;
}

}  // namespace

std::string EnttComponentEmitter::emit_component(const ResolvedTrait& trait) {
    const std::string cpp_name = canonical_to_cpp_name(trait.module_name, trait.name);
    std::ostringstream out;
    if (trait.fields.empty()) {
        out << "struct " << cpp_name << " {};\n";
        return out.str();
    }

    out << "struct " << cpp_name << " {\n";
    for (const auto& field : trait.fields) {
        out << "    " << entt_type_to_cpp(field.type) << " " << field.name;
        const auto default_value = stdlib_trait_default(trait, field);
        out << (default_value.empty() ? "{}" : default_value) << ";\n";
    }
    out << "};\n";
    return out.str();
}

std::string EnttComponentEmitter::emit_pod_struct(const ResolvedStruct& s) {
    const std::string cpp_name = canonical_to_cpp_name(s.module_name, s.name);
    std::ostringstream out;
    out << "struct " << cpp_name << " {\n";
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
