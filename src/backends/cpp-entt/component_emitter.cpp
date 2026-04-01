#include "backends/cpp-entt/component_emitter.h"

#include <sstream>

namespace cactus {

std::string EnttComponentEmitter::emit_component(const ResolvedTrait& trait) {
    std::ostringstream out;
    out << "struct " << trait.name << " {\n";
    for (const auto& field : trait.fields) {
        out << "    " << SoaEmitter::type_to_cpp(field.type) << " " << field.name;
        out << "{};\n";
    }
    out << "};\n";
    return out.str();
}

std::string EnttComponentEmitter::emit_pod_struct(const ResolvedStruct& s) {
    return SoaEmitter::emit_pod_struct(s);
}

std::string EnttComponentEmitter::emit_enum(const ResolvedEnum& e) {
    return SoaEmitter::emit_enum(e);
}

}  // namespace cactus
