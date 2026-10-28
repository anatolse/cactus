#pragma once

#include "frontend/ast.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace cactus {

// Child nodes are matched by member name rather than by ExprNode alternative so
// one traversal covers every alternative that shares a shape; a new alternative
// needs a matching clause here or its children go unvisited.
namespace expr_children {

template <typename Node, typename Visit>
void fixed(Node& node, const Visit& visit) {
    if constexpr (requires { node.operand; }) {
        visit(node.operand);
    }
    if constexpr (requires {
                      node.left;
                      node.right;
                  }) {
        visit(node.left);
        visit(node.right);
    }
    if constexpr (requires { node.callee; }) {
        visit(node.callee);
    }
    if constexpr (requires { node.object; }) {
        visit(node.object);
    }
    if constexpr (requires { node.body; }) {
        visit(node.body);
    }
    if constexpr (requires {
                      node.condition;
                      node.then_expr;
                      node.else_expr;
                  }) {
        visit(node.condition);
        visit(node.then_expr);
        visit(node.else_expr);
    }
    // A relation test's null end is the `_` wildcard.
    if constexpr (requires {
                      node.from;
                      node.to;
                  }) {
        if (node.from != nullptr) {
            visit(node.from);
        }
        if (node.to != nullptr) {
            visit(node.to);
        }
    }
}

template <typename Node, typename Visit, typename Fields>
void lists(Node& node, const Visit& visit, const Fields& fields) {
    if constexpr (requires { node.args; }) {
        for (auto& argument : node.args) {
            visit(argument);
        }
    }
    if constexpr (requires { node.elements; }) {
        for (auto& element : node.elements) {
            visit(element);
        }
    }
    if constexpr (requires { node.named_args; }) {
        fields(node.named_args);
    }
}

template <typename Node, typename Visit, typename Fields>
void compounds(Node& node, const Visit& visit, const Fields& fields) {
    if constexpr (requires {
                      node.source;
                      node.operations;
                  }) {
        visit(node.source);
        for (auto& operation : node.operations) {
            for (auto& argument : operation.args) {
                visit(argument);
            }
        }
    }
    if constexpr (requires {
                      node.subject;
                      node.arms;
                  }) {
        visit(node.subject);
        for (auto& arm : node.arms) {
            visit(arm.pattern);
            visit(arm.body);
        }
    }
    if constexpr (requires {
                      node.overrides;
                      node.arguments;
                  }) {
        fields(node.arguments.values);
        for (auto& trait : node.overrides) {
            fields(trait.assignments);
        }
    }
}

}  // namespace expr_children

template <typename Expression, typename Visitor>
void visit_expression(Expression& expression, const Visitor& visitor) {
    visitor(expression);
    auto visit  = [&](auto& child) { visit_expression(*child, visitor); };
    auto fields = [&](auto& assignments) {
        for (auto& assignment : assignments) {
            visit(assignment.value);
        }
    };
    std::visit(
        [&](auto& node) {
            expr_children::fixed(node, visit);
            expr_children::lists(node, visit, fields);
            expr_children::compounds(node, visit, fields);
        },
        expression.expr);
}

inline void collect_chain_paths(const ExprNode& expr, std::string_view op, ExprPath& path, std::vector<ExprPath>& out) {
    const auto* binary = std::get_if<BinaryExpr>(&expr.expr);
    if (binary == nullptr || binary->op != op) {
        out.push_back(path);
        return;
    }
    for (std::uint8_t side = 0; side < 2; ++side) {
        path.push_back(side);
        collect_chain_paths(side == 0 ? *binary->left : *binary->right, op, path, out);
        path.pop_back();
    }
}

[[nodiscard]] inline std::vector<ExprPath> chain_paths(const ExprNode& root, const ExprPath& at, std::string_view op) {
    std::vector<ExprPath> out;
    auto path = at;
    collect_chain_paths(*resolve_expr_path(root, at), op, path, out);
    return out;
}

struct RowEndJoin {
    std::size_t predicate = 0;
    bool from_end         = true;
    const ExprNode* key   = nullptr;
};

// The name of a rule's row binding: a `pairs:` row entry or `each: R as r`.
[[nodiscard]] inline const std::string* row_binding_name(const RuleNode& rule) {
    if (rule.rows.has_value()) {
        return &rule.rows->alias;
    }
    if (!rule.pairs.has_value()) {
        return nullptr;
    }
    const auto row = std::ranges::find_if(rule.pairs->bindings, &PairBindingNode::is_row);
    return row == rule.pairs->bindings.end() ? nullptr : &row->name;
}

[[nodiscard]] inline std::optional<bool> row_endpoint(const ExprNode& expr, std::string_view binding) {
    const auto* member = std::get_if<MemberExpr>(&expr.expr);
    const auto* owner  = member == nullptr ? nullptr : std::get_if<IdentExpr>(&member->object->expr);
    if (owner == nullptr || owner->name != binding || (member->member != "from" && member->member != "to")) {
        return std::nullopt;
    }
    return member->member == "from";
}

[[nodiscard]] inline std::optional<LimitKey> resolve_limit_key(const RuleNode& rule,
                                                               std::string_view name,
                                                               std::optional<bool> from) {
    const auto endpoint =
        from.has_value() ? std::optional{*from ? RelationEndpoint::From : RelationEndpoint::To} : std::nullopt;
    if (rule.rows.has_value()) {
        return name == rule.rows->alias && endpoint.has_value() ? std::optional{LimitKey{.endpoint = endpoint}}
                                                                : std::nullopt;
    }
    if (!rule.pairs.has_value()) {
        return std::nullopt;
    }
    const auto& bindings = rule.pairs->bindings;
    const auto found     = std::ranges::find(bindings, name, &PairBindingNode::name);
    if (found == bindings.end() || found->is_row() != endpoint.has_value()) {
        return std::nullopt;
    }
    return LimitKey{.binding_index = static_cast<std::size_t>(found - bindings.begin()), .endpoint = endpoint};
}

[[nodiscard]] inline std::optional<LimitKey> row_endpoint_key(const RuleNode& rule, const ExprNode& expr) {
    const auto* member = std::get_if<MemberExpr>(&expr.expr);
    const auto* owner  = member == nullptr ? nullptr : std::get_if<IdentExpr>(&member->object->expr);
    if (owner == nullptr) {
        return std::nullopt;
    }
    const auto from = row_endpoint(expr, owner->name);
    return from.has_value() ? resolve_limit_key(rule, owner->name, from) : std::nullopt;
}

[[nodiscard]] inline bool is_bound_row_endpoint(const RuleNode& rule, const ExprNode& expr) {
    return row_endpoint_key(rule, expr).has_value();
}
[[nodiscard]] inline std::uint8_t row_key_dependencies(const ExprNode& expr,
                                                       const std::array<std::string_view, 2>& names) {
    std::uint8_t dependencies = 0;
    visit_expression(expr, [&](const ExprNode& node) {
        const auto* ident = std::get_if<IdentExpr>(&node.expr);
        if (ident == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (!names[index].empty() && ident->name == names[index]) {
                dependencies |= static_cast<std::uint8_t>(1U << index);
            }
        }
    });
    return dependencies;
}

inline void collect_row_end_keys(RowJoinPlan& plan,
                                 const ExprNode& root,
                                 const std::array<std::string_view, 2>& names,
                                 std::size_t predicate,
                                 const ExprPath& path) {
    const auto* equality = std::get_if<BinaryExpr>(&resolve_expr_path(root, path)->expr);
    if (equality == nullptr || equality->op != "==") {
        return;
    }
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index].empty()) {
            continue;
        }
        for (std::uint8_t side = 0; side < 2; ++side) {
            const auto& end_side    = side == 0 ? *equality->left : *equality->right;
            const auto& key_side    = side == 0 ? *equality->right : *equality->left;
            const auto end          = row_endpoint(end_side, names[index]);
            const auto dependencies = row_key_dependencies(key_side, names);
            if (!end.has_value() || (dependencies & (1U << index)) != 0) {
                continue;
            }
            auto key_path = path;
            key_path.push_back(static_cast<std::uint8_t>(1U - side));
            const bool joined = !names[1 - index].empty() && row_endpoint(key_side, names[1 - index]).has_value();
            plan.keys[index].push_back(RowEndKey{.predicate    = predicate,
                                                 .expression   = std::move(key_path),
                                                 .from_end     = *end,
                                                 .dependencies = dependencies});
            plan.linked = plan.linked || joined;
        }
    }
}

[[nodiscard]] inline RowJoinPlan analyze_row_ends(const RuleNode& rule) {
    RowJoinPlan plan;
    if (!rule.where_clause.has_value()) {
        return plan;
    }
    std::array<std::string_view, 2> names{};
    if (rule.rows.has_value()) {
        names[0] = rule.rows->alias;
    } else if (rule.pairs.has_value()) {
        for (std::size_t index = 0; index < rule.pairs->bindings.size(); ++index) {
            names[index] = rule.pairs->bindings[index].name;
        }
    }
    const auto& predicates = rule.where_clause->predicates;
    for (std::size_t index = 0; index < predicates.size(); ++index) {
        for (const auto& path : chain_paths(*predicates[index], {}, "and")) {
            collect_row_end_keys(plan, *predicates[index], names, index, path);
        }
    }
    return plan;
}

[[nodiscard]] inline std::optional<RowEndJoin> find_row_end_join(const RuleNode& rule) {
    const auto* row = row_binding_name(rule);
    if (row == nullptr || !rule.where_clause.has_value()) {
        return std::nullopt;
    }
    const auto index = rule.pairs.has_value() && !rule.pairs->bindings[0].is_row() ? std::size_t{1} : std::size_t{0};
    RowJoinPlan fallback;
    const auto& plan = rule.row_join.has_value() ? *rule.row_join : (fallback = analyze_row_ends(rule));
    if (!plan.keys[index].empty()) {
        const auto& key = plan.keys[index].front();
        return RowEndJoin{.predicate = key.predicate,
                          .from_end  = key.from_end,
                          .key = resolve_expr_path(*rule.where_clause->predicates[key.predicate], key.expression)};
    }
    return std::nullopt;
}

inline std::string initializer_slot_name(std::size_t index) {
    return "cactus_gen_slot_" + std::to_string(index);
}

inline void offset_initializer_slots(ExprNode& expression, std::size_t offset) {
    visit_expression(expression, [offset](ExprNode& node) {
        auto* ident = std::get_if<IdentExpr>(&node.expr);
        if (ident != nullptr && ident->template_slot.has_value()) {
            *ident->template_slot += offset;
        }
    });
}

}  // namespace cactus
