#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

class CppEnttCodegen {
public:
    static std::string generate(const DecoratedProgram& program);
};

}  // namespace cactus
