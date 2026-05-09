#pragma once

#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <string>

namespace cactus {

class EnttComponentEmitter {
public:
    static std::string emit_component(const ResolvedTrait& trait);
    static std::string emit_pod_struct(const ResolvedStruct& s);
    static std::string emit_enum(const ResolvedEnum& e);
};

}  // namespace cactus
