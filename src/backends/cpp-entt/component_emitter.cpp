#include "backends/cpp-entt/component_emitter.h"

#include <sstream>

namespace cactus {

namespace {

std::string entt_type_to_cpp(const TypeInfo& type) {
    if (type.kind == TypeKind::EntityId) {
        return "entt::entity";
    }
    return SoaEmitter::type_to_cpp(type);
}

bool should_defer_to_raylib_enum(const std::string& name) {
    return name == "MouseButton" || name == "GamepadButton" || name == "GamepadAxis";
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
        out << "{};\n";
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
    return SoaEmitter::emit_enum(e);
}

}  // namespace cactus
