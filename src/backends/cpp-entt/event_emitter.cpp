#include "backends/cpp-entt/event_emitter.hpp"

#include "frontend/symbol_identity.hpp"

#include <sstream>

namespace cactus {

namespace {

std::string entt_type_to_cpp(const TypeInfo& type) {
    if (type.kind == TypeKind::EntityId) {
        return "entt::entity";
    }
    return EnttCodegenUtils::type_to_cpp(type);
}

}  // namespace

std::string EnttEventEmitter::emit_event(const EventNode& event, const DecoratedProgram& program) {
    const std::string& mod     = event.module_name.empty() ? program.module_name : event.module_name;
    const std::string cpp_name = canonical_to_cpp_name(mod, event.name) + "Event";
    std::ostringstream out;
    out << "struct " << cpp_name << " {\n";
    for (const auto& field : event.fields) {
        TypeInfo type;
        if (field.type.name == "int") {  // NOLINT(bugprone-branch-clone)
            type = {.kind = TypeKind::Int, .name = "int"};
        } else if (field.type.name == "float") {
            type = {.kind = TypeKind::Float, .name = "float"};
        } else if (field.type.name == "bool") {
            type = {.kind = TypeKind::Bool, .name = "bool"};
        } else if (field.type.name == "string") {
            type = {.kind = TypeKind::String, .name = "string"};
        } else if (field.type.name == "vec2") {
            type = {.kind = TypeKind::Vec2, .name = "vec2"};
        } else if (field.type.name == "vec3") {
            type = {.kind = TypeKind::Vec3, .name = "vec3"};
        } else if (field.type.name == "color") {
            type = {.kind = TypeKind::Color, .name = "color"};
        } else if (field.type.name == "entity_id") {
            type = {.kind = TypeKind::EntityId, .name = "entity_id"};
        } else if (program.structs.contains(field.type.name)) {
            type = {.kind = TypeKind::Struct, .name = field.type.name};
        } else {
            type = {.kind = TypeKind::Int, .name = "int"};
        }

        out << "    " << entt_type_to_cpp(type) << " " << field.name << ";\n";
    }
    out << "};\n";
    return out.str();
}

std::string EnttEventEmitter::emit_sink_connection(const EventNode& event, const DecoratedProgram& program) {
    const std::string& mod     = event.module_name.empty() ? program.module_name : event.module_name;
    const std::string cpp_name = canonical_to_cpp_name(mod, event.name) + "Event";
    std::ostringstream out;
    out << "// dispatcher.sink<" << cpp_name << ">().connect<&on_" << event.name << ">();\n";
    return out.str();
}

}  // namespace cactus
