#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

class EnttEventEmitter {
public:
    static std::string emit_event(const ResolvedEvent& event, const std::string& cpp_name);
    static std::string emit_sink_connection(const EventNode& event, const DecoratedProgram& program);
};

}  // namespace cactus
