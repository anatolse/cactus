#include "backends/cpp-manual/event_emitter.hpp"

#include <sstream>

namespace cactus {

std::string ManualEventEmitter::emit_event(const EventNode& event, const DecoratedProgram& program) {
    std::ostringstream out;

    // Event POD struct
    out << "struct " << event.name << "Event {\n";
    for (const auto& field : event.fields) {
        // Resolve field type from the decorated program
        TypeInfo type;
        type.kind = TypeKind::Int;  // default
        type.name = field.type.name;

        // Try to resolve from known types
        if (field.type.name == "int") {
            type = {.kind = TypeKind::Int, .name = "int"};
        } else if (field.type.name == "float") {
            type = {.kind = TypeKind::Float, .name = "float"};
        } else if (field.type.name == "bool") {
            type = {.kind = TypeKind::Bool, .name = "bool"};
        } else if (field.type.name == "string") {
            type = {.kind = TypeKind::String, .name = "string"};
        } else if (static_cast<unsigned int>(program.structs.contains(field.type.name)) != 0U) {
            type = {.kind = TypeKind::Struct, .name = field.type.name};
        }

        out << "    " << SoaEmitter::type_to_cpp(type) << " " << field.name << ";\n";
    }
    out << "};\n\n";

    // Event buffer
    out << "std::vector<" << event.name << "Event> " << event.name << "_buffer;\n\n";

    return out.str();
}

std::string ManualEventEmitter::emit_dispatch(const EventNode& event) {
    std::ostringstream out;
    out << "void dispatch_" << event.name << "() {\n";
    out << "    for (auto& evt : " << event.name << "_buffer) {\n";
    out << "        // TODO: invoke registered handlers for " << event.name << "\n";
    out << "    }\n";
    out << "    " << event.name << "_buffer.clear();\n";
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
