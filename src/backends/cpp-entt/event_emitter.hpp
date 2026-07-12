#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <string>

namespace cactus {

class EnttEventEmitter {
public:
    static std::string emit_event(const EventNode& event, const DecoratedProgram& program);
    static std::string emit_sink_connection(const EventNode& event, const DecoratedProgram& program);
};

}  // namespace cactus
