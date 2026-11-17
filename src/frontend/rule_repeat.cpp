#include "common/ast_expressions.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace cactus {

namespace {

constexpr const char* kOnlyFields = "a repeating rule may only write fields";

// The keyword of a statement that changes more than field values, if `stmt` is one.
std::optional<const char*> command_keyword(const StmtNode& stmt) {
    return std::visit(
        [](const auto& node) -> std::optional<const char*> {
            using S = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<S, EmitStmt>) {
                return "emit";
            } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                return "spawn";
            } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                return "destroy";
            } else if constexpr (std::is_same_v<S, LoadStmt>) {
                return "load";
            } else if constexpr (std::is_same_v<S, AddTraitStmt> || std::is_same_v<S, AddRelationStmt>) {
                return "add";
            } else if constexpr (std::is_same_v<S, RemoveTraitStmt> || std::is_same_v<S, RemoveRelationStmt>) {
                return "remove";
            } else if constexpr (std::is_same_v<S, SetTraitStmt>) {
                return "set";
            } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                return "project";
            } else {
                return std::nullopt;
            }
        },
        stmt.stmt);
}

std::string sorted_join(const std::unordered_set<std::string>& names) {
    std::vector<std::string> sorted(names.begin(), names.end());
    std::ranges::sort(sorted);
    std::string text;
    for (const auto& name : sorted) {
        text += (text.empty() ? "'" : ", '") + name + "'";
    }
    return text;
}

}  // namespace

void SemanticAnalyzer::fold_repeat_count(RepeatClause& repeat) {
    const auto& count = *repeat.count;
    if (!validate_const_expression(count, "repeat: the count must be a constant", true)) {
        return;
    }
    const auto type = infer_expr_type(count, {}, {}, nullptr);
    if (type.kind != TypeKind::Int) {
        errors_.error(count.location, "repeat: the count must be of type 'int', got '" + type.name + "'");
        return;
    }
    const auto passes = limit_constant_count(count);
    if (!passes.has_value()) {
        errors_.error(count.location, "repeat: the count must be a constant");
        return;
    }
    if (*passes < 2) {
        errors_.error(count.location,
                      "repeat: the count must be at least 2, got " + std::to_string(*passes) +
                          "; a rule without `repeat:` runs its pass once");
        return;
    }
    repeat.passes = *passes;
}

void SemanticAnalyzer::reject_repeating_commands(const RuleNode& rule, const EventHandlerNode& handler) {
    const auto check_expression = [&](const ExprNode& root) {
        visit_expression(root, [&](const ExprNode& node) {
            if (std::holds_alternative<SpawnExpr>(node.expr)) {
                errors_.error(node.location, "`spawn` in rule '" + rule.name + "': " + kOnlyFields);
            }
        });
    };
    for_each_nested_statement(handler.body, [&](const StmtNode& stmt) {
        if (const auto keyword = command_keyword(stmt)) {
            errors_.error(stmt.location, std::string("`") + *keyword + "` in rule '" + rule.name + "': " + kOnlyFields);
        }
    });
    for_each_statement_expression(handler.body, check_expression);
}

void SemanticAnalyzer::validate_repeat_clause(RuleNode& rule) {
    if (!rule.repeat.has_value()) {
        return;
    }
    const auto prefix = "rule '" + rule.name + "': ";
    if (rule.keep.has_value()) {
        errors_.error(rule.repeat->location,
                      prefix + "a keep rule can't have `repeat:`; `keep` is a definition, not a pass");
        return;
    }
    fold_repeat_count(*rule.repeat);
    const auto reject_clause = [&](const char* clause, const SourceLocation& location) {
        errors_.error(location, std::string("`") + clause + "` on rule '" + rule.name + "': " + kOnlyFields +
                                    " and runs every row in every pass");
    };
    if (rule.limit.has_value()) {
        reject_clause("limit:", rule.limit->location);
    }
    if (!rule.order_by.empty()) {
        reject_clause("order by:", rule.order_by.front().location);
    }
    if (rule.handlers.empty()) {
        errors_.error(rule.repeat->location, prefix + "a repeating rule needs one handler triggered by a phase");
        return;
    }
    for (std::size_t index = 1; index < rule.handlers.size(); ++index) {
        errors_.error(rule.handlers[index].trigger_location, prefix + "a repeating rule has exactly one handler");
    }
    const auto& handler = rule.handlers.front();
    if (handler.resolved_trigger.has_value() && handler.resolved_trigger->kind != HandlerTriggerKind::Phase) {
        errors_.error(handler.trigger_location, prefix + "a repeating rule's handler must be triggered by a phase");
    }
    reject_repeating_commands(rule, handler);
}

void SemanticAnalyzer::reject_repeating_effects(const RuleNode& rule,
                                                const EventHandlerNode& handler,
                                                const HandlerContract& contract) {
    if (!rule.repeat.has_value() || (contract.effect_acts.empty() && contract.native_spawns.empty())) {
        return;
    }
    const auto acts = [&](const SymbolId& callee) {
        if (native_creation(make_canonical_id(callee)) != nullptr) {
            return true;
        }
        const auto* function = find_resolved_func(callee);
        return function != nullptr && function->is_extern && !extern_call_effects(callee, *function).acts.empty();
    };
    std::optional<SourceLocation> call_location;
    for_each_statement_expression(handler.body, [&](const ExprNode& root) {
        visit_expression(root, [&](const ExprNode& node) {
            const auto* call = std::get_if<CallExpr>(&node.expr);
            if (!call_location.has_value() && call != nullptr && call->resolved_callee_id.has_value() &&
                acts(*call->resolved_callee_id)) {
                call_location = node.location;
            }
        });
    });
    const auto what = contract.effect_acts.empty() ? std::string{"creates entities"}
                                                   : "acts on " + sorted_join(contract.effect_acts);
    errors_.error(call_location.value_or(handler.trigger_location),
                  "rule '" + rule.name + "' calls a function that " + what + ": " + kOnlyFields);
}

}  // namespace cactus
