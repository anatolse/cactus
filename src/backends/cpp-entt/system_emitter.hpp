#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <functional>
#include <string>

namespace cactus {

class EnttSystemEmitter {
public:
    static std::string emit_func(const FuncNode& func, const DecoratedProgram& program);
    // Only funcs whose trait or event parameters match `reference_params`: those need the
    // component and event structs declared first.
    static std::string emit_func_declaration(const FuncNode& func, const DecoratedProgram& program, bool reference_params);
    static std::string emit_system(const RuleNode& sys, const DecoratedProgram& program);
    // Lowers an expression of the row the domain loop is at.
    using RowValue = std::function<std::string(const ExprNode&)>;
    // A loop over `rule`'s unary domain: every `each:` trait, no `without:` trait, in `order by:`
    // order, else oldest first. Per row, with each trait's component and alias bound, `on_row` runs,
    // then rows failing `where:` are skipped, then the code `body` returns runs.
    static std::string emit_domain_rows(const RuleNode& rule,
                                        const DecoratedProgram& program,
                                        const std::string& on_row,
                                        const std::function<std::string(const RowValue&)>& body);
    // A drawing rule's guards before it draws: its named entities alive and bound, and its `when:`.
    static std::string emit_draw_prelude(const RuleNode& rule, const DecoratedProgram& program);
    // Lowers an expression that reads no row, such as a named entity's field.
    static std::string emit_world_value(const ExprNode& expr, const DecoratedProgram& program);
    static std::string emit_extern_system(const ExternRuleNode& sys, const DecoratedProgram& program);
    static bool requires_entt_hierarchy_helpers(const DecoratedProgram& program);
    static std::string emit_entt_hierarchy_helpers(const DecoratedProgram& program);
    // Names each spatial index's static store; same bounds code and overlapping archetypes share one.
    static void assign_static_stores(std::string& code);
};

}  // namespace cactus
