#pragma once

#include "frontend/semantic_analyzer.h"

#include <string>

namespace cactus {

class CppManualCodegen {
public:
    static std::string generate(const DecoratedProgram& program);
};

}  // namespace cactus
