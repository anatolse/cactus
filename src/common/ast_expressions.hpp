#pragma once

#include "frontend/ast.hpp"

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

inline std::string initializer_slot_name(std::size_t index) {
    return "__cactus_template_slot_" + std::to_string(index);
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
