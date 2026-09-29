#include "backends/cpp-entt/event_emitter.hpp"

#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <sstream>

namespace cactus {

std::string EnttEventEmitter::emit_event(const ResolvedEvent& event, const std::string& cpp_name) {
    std::ostringstream out;
    out << "struct " << cpp_name << " {\n";
    for (const auto& field : event.fields) {
        out << "    " << EnttCodegenUtils::value_type_to_cpp(field.type) << " " << field.name << "{};\n";
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
