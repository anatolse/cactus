#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

class EnttSystemEmitter {
public:
    static std::string emit_system(const RuleNode& sys, const DecoratedProgram& program);
    static std::string emit_extern_system(const ExternRuleNode& sys, const DecoratedProgram& program);
    static bool requires_entt_hierarchy_helpers(const DecoratedProgram& program);
    static std::string emit_entt_hierarchy_helpers(const DecoratedProgram& program);
};

}  // namespace cactus
