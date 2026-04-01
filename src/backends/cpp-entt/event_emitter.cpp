#include "backends/cpp-entt/event_emitter.h"

#include <sstream>

namespace cactus {

std::string EnttEventEmitter::emit_event(const EventNode& event, const DecoratedProgram& program) {
    std::ostringstream out;
    out << "struct " << event.name << "Event {\n";
    for (const auto& field : event.fields) {
        TypeInfo type;
        if (field.type.name == "int") { // NOLINT(bugprone-branch-clone)
            type = {.kind = TypeKind::Int, .name = "int"};
        } else if (field.type.name == "float") {
            type = {.kind = TypeKind::Float, .name = "float"};
        } else if (field.type.name == "bool") {
            type = {.kind = TypeKind::Bool, .name = "bool"};
        } else if (field.type.name == "string") {
            type = {.kind = TypeKind::String, .name = "string"};
        } else if (program.structs.contains(field.type.name)) {
            type = {.kind = TypeKind::Struct, .name = field.type.name};
        } else {
            type = {.kind = TypeKind::Int, .name = "int"};
        }

        out << "    " << SoaEmitter::type_to_cpp(type) << " " << field.name << ";\n";
    }
    out << "};\n";
    return out.str();
}

std::string EnttEventEmitter::emit_sink_connection(const EventNode& event) {
    std::ostringstream out;
    out << "// dispatcher.sink<" << event.name << "Event>().connect<&on_" << event.name << ">();\n";
    return out.str();
}

}  // namespace cactus
