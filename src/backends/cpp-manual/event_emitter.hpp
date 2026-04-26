#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-manual/soa_emitter.hpp"

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
