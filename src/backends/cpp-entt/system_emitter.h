#pragma once

#include "backends/cpp-manual/system_emitter.h"
#include "frontend/ast.h"
#include "frontend/semantic_analyzer.h"

#include <string>

namespace cactus {

class EnttSystemEmitter {
public:
    static std::string emit_system(const SystemNode& sys, const DecoratedProgram& program);
    static std::string emit_extern_system(const ExternSystemNode& sys, const DecoratedProgram& program);
};

}  // namespace cactus
