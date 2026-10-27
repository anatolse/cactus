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
    if constexpr (requires { node.left; node.right; }) {
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
    if constexpr (requires { node.condition; node.then_expr; node.else_expr; }) {
        visit(node.condition);
        visit(node.then_expr);
        visit(node.else_expr);
    }
    // A relation test's null end is the `_` wildcard.
    if constexpr (requires { node.from; node.to; }) {
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
    if constexpr (requires { node.source; node.operations; }) {
        visit(node.source);
        for (auto& operation : node.operations) {
            for (auto& argument : operation.args) {
                visit(argument);
            }
        }
    }
    if constexpr (requires { node.subject; node.arms; }) {
        visit(node.subject);
        for (auto& arm : node.arms) {
            visit(arm.pattern);
            visit(arm.body);
        }
    }
    if constexpr (requires { node.overrides; node.arguments; }) {
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

// A `where:` conjunct `r.from == <key>` or `r.to == <key>` of a rule with
// row binding `r`, whose key reads no row: the pass looks rows up by key.
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

[[nodiscard]] inline std::optional<RowEndJoin> find_row_end_join(const RuleNode& rule) {
    const auto* row = row_binding_name(rule);
    if (row == nullptr || !rule.where_clause.has_value()) {
        return std::nullopt;
    }
    const auto end_of = [&](const ExprNode& side) -> std::optional<bool> {
        const auto* member = std::get_if<MemberExpr>(&side.expr);
        const auto* owner  = member == nullptr ? nullptr : std::get_if<IdentExpr>(&member->object->expr);
        if (owner == nullptr || owner->name != *row || (member->member != "from" && member->member != "to")) {
            return std::nullopt;
        }
        return member->member == "from";
    };
    const auto reads_row = [&](const ExprNode& side) {
        bool found = false;
        visit_expression(side, [&](const ExprNode& node) {
            const auto* ident = std::get_if<IdentExpr>(&node.expr);
            found             = found || (ident != nullptr && ident->name == *row);
        });
        return found;
    };
    const auto& predicates = rule.where_clause->predicates;
    for (std::size_t index = 0; index < predicates.size(); ++index) {
        const auto* equality = std::get_if<BinaryExpr>(&predicates[index]->expr);
        if (equality == nullptr || equality->op != "==") {
            continue;
        }
        for (const auto& [end_side, key_side] : {std::pair{equality->left.get(), equality->right.get()},
                                                 std::pair{equality->right.get(), equality->left.get()}}) {
            if (const auto end = end_of(*end_side); end.has_value() && !reads_row(*key_side)) {
                return RowEndJoin{.predicate = index, .from_end = *end, .key = key_side};
            }
        }
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
