#include "backends/cpp-entt/event_emitter.hpp"

#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <sstream>

namespace cactus {

std::string EnttEventEmitter::emit_event(const ResolvedEvent& event, const std::string& module_name) {
    std::ostringstream out;
    out << "struct " << cpp_decl_name(SymbolKind::Event, event.name) << " {\n";
    for (const auto& field : event.fields) {
        out << "    " << EnttCodegenUtils::value_type_to_cpp(field.type) << " " << cpp_escaped(field.name)
            << EnttCodegenUtils::value_initializer(field.type) << ";\n";
    }
    out << "};\n";
    return namespace_block(cpp_namespace_path(module_name), out.str());
}

std::string EnttEventEmitter::emit_sink_connection(const EventNode& event, const DecoratedProgram& program) {
    const std::string& mod     = event.module_name.empty() ? program.module_name : event.module_name;
    const std::string cpp_name = cpp_type_ref(SymbolKind::Event, mod, event.name);
    std::ostringstream out;
    out << "// dispatcher.sink<" << cpp_name << ">().connect<&on_" << event.name << ">();\n";
    return out.str();
}

}  // namespace cactus
