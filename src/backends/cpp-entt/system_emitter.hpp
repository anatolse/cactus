#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

class EnttSystemEmitter {
public:
    static std::string emit_func(const FuncNode& func, const DecoratedProgram& program);
    // Only funcs whose trait or event parameters match `reference_params`: those need the
    // component and event structs declared first.
    static std::string emit_func_declaration(const FuncNode& func, const DecoratedProgram& program, bool reference_params);
    static std::string emit_system(const RuleNode& sys, const DecoratedProgram& program);
    static std::string emit_extern_system(const ExternRuleNode& sys, const DecoratedProgram& program);
    static bool requires_entt_hierarchy_helpers(const DecoratedProgram& program);
    static std::string emit_entt_hierarchy_helpers(const DecoratedProgram& program);
    // Names each spatial index's static store; same bounds code and overlapping archetypes share one.
    static void assign_static_stores(std::string& code);
};

}  // namespace cactus
