#include "backends/cpp-entt/system_emitter.h"

#include <sstream>

namespace cactus {

std::string EnttSystemEmitter::emit_system(const SystemNode& sys, const DecoratedProgram& /*program*/) {
    std::ostringstream out;

    for (auto& handler : sys.handlers) {
        out << "void " << sys.name << "_" << handler.event_name << "(entt::registry& registry";

        // Handler params
        for (auto& param : handler.params) {
            out << ", float " << param.name;
        }
        out << ") {\n";

        // Build view template args
        out << "    auto view = registry.view<";
        for (size_t i = 0; i < sys.filter.trait_names.size(); ++i) {
            if (i > 0) out << ", ";
            out << sys.filter.trait_names[i];
        }
        out << ">();\n";

        // each() lambda
        out << "    view.each([&](";
        for (size_t i = 0; i < sys.filter.trait_names.size(); ++i) {
            if (i > 0) out << ", ";
            out << "auto& " << sys.filter.trait_names[i] << "_comp";
        }
        out << ") {\n";

        // Emit body
        for (auto& stmt : handler.body) {
            out << ManualSystemEmitter::emit_stmt(*stmt, 2);
        }

        out << "    });\n";
        out << "}\n\n";
    }

    return out.str();
}

}  // namespace cactus
