#include "backends/cpp-entt/event_emitter.h"

#include <sstream>

namespace cactus {

std::string EnttEventEmitter::emit_event(const EventNode& event, const DecoratedProgram& program) {
    std::ostringstream out;
    out << "struct " << event.name << "Event {\n";
    for (auto& field : event.fields) {
        TypeInfo type;
        if (field.type.name == "int") type = {TypeKind::Int, "int"};
        else if (field.type.name == "float") type = {TypeKind::Float, "float"};
        else if (field.type.name == "bool") type = {TypeKind::Bool, "bool"};
        else if (field.type.name == "string") type = {TypeKind::String, "string"};
        else if (program.structs.count(field.type.name)) type = {TypeKind::Struct, field.type.name};
        else type = {TypeKind::Int, "int"};

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
