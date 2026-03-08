#pragma once

#include "backends/cpp-manual/soa_emitter.h"
#include "frontend/ast.h"
#include "frontend/semantic_analyzer.h"

#include <string>

namespace cactus {

class EnttEventEmitter {
public:
    static std::string emit_event(const EventNode& event, const DecoratedProgram& program);
    static std::string emit_sink_connection(const EventNode& event);
};

}  // namespace cactus
