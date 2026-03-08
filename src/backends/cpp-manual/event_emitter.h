#pragma once

#include "backends/cpp-manual/soa_emitter.h"
#include "frontend/ast.h"
#include "frontend/semantic_analyzer.h"

#include <string>

namespace cactus {

class ManualEventEmitter {
public:
    // Generate event POD struct and buffer
    static std::string emit_event(const EventNode& event, const DecoratedProgram& program);

    // Generate dispatch function
    static std::string emit_dispatch(const EventNode& event);
};

}  // namespace cactus
