#pragma once

#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <string>

namespace cactus {

class EnttComponentEmitter {
public:
    static std::string emit_component(const ResolvedTrait& trait, const DecoratedProgram& program = {});
    // One `remove_state_variant` specialization per state, emitted after every component.
    static std::string emit_state_helpers(const DecoratedProgram& program);
    static std::string emit_pod_struct(const ResolvedStruct& s);
    static std::string emit_enum(const ResolvedEnum& e);

private:
    static std::string emit_state_slot(const std::string& cpp_name,
                                       const SymbolId& state,
                                       const DecoratedProgram& program);
};

}  // namespace cactus
