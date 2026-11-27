#include "frontend/stage_translation.hpp"

#include "common/ast_expressions.hpp"
#include "common/drawing_rule_builtin_fields.hpp"
#include "common/drawing_rule_intrinsics.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace cactus {

namespace {

std::string_view callee_name(const CallExpr& call) {
    const auto* ident = std::get_if<IdentExpr>(&call.callee->expr);
    return ident != nullptr ? std::string_view(ident->name) : std::string_view();
}

bool is_constant_operand(const ExprNode& expr) {
    bool constant = true;
    visit_expression(expr, [&](const ExprNode& node) {
        std::visit(
            [&](const auto& e) {
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, LiteralExpr>) {
                    constant = constant && (e.kind == LiteralExpr::Kind::Int || e.kind == LiteralExpr::Kind::Float);
                } else if constexpr (std::is_same_v<E, IdentExpr> || std::is_same_v<E, MemberExpr>) {
                    constant = constant && e.resolved_const_id.has_value();
                } else if constexpr (!std::is_same_v<E, UnaryExpr> && !std::is_same_v<E, BinaryExpr>) {
                    constant = false;
                }
            },
            node.expr);
    });
    return constant;
}

class StageFuncCheck {
public:
    StageFuncCheck(const ResolvedFunc& func, const StageTranslationLookups& lookups)
        : func_(&func)
        , self_("'" + func.canonical_id + "'")
        , lookups_(&lookups) {}

    std::string run(const FuncNode& node) {
        for (const auto& param : func_->params) {
            if (is_reference_type(param.type)) {
                block(self_ + " takes a trait or event parameter");
            } else if (!holds(param.type)) {
                block(self_ + " takes a parameter of type '" + param.type.name + "'");
            }
        }
        if (func_->return_type.has_value() && !holds(*func_->return_type)) {
            block(self_ + " returns a value of type '" + func_->return_type->name + "'");
        }
        for_each_nested_statement(node.body, [&](const StmtNode& stmt) { check_statement(stmt); });
        for_each_statement_expression(node.body, [&](const ExprNode& top) {
            visit_expression(top, [&](const ExprNode& expr) {
                std::visit([&](const auto& e) { check_expression(e, expr); }, expr.expr);
            });
        });
        return blocker_;
    }

private:
    const ResolvedFunc* func_;
    std::string self_;
    const StageTranslationLookups* lookups_;
    std::string blocker_;

    void block(std::string reason) {
        if (blocker_.empty()) {
            blocker_ = std::move(reason);
        }
    }

    // A value a shader holds: a scalar, vector, color or enum, or a struct of such values.
    bool holds(const TypeInfo& type) {
        if (is_glsl_value_type(type.kind)) {
            return true;
        }
        if (type.kind != TypeKind::Struct || !type.symbol_id.has_value()) {
            return false;
        }
        const auto* structure = lookups_->structure(*type.symbol_id);
        return structure != nullptr && std::ranges::all_of(structure->fields, [&](const ResolvedField& field) {
                   return holds(field.type);
               });
    }

    void check_statement(const StmtNode& stmt) {
        std::visit(
            [&](const auto& node) {
                using S = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<S, ForeachStmt>) {
                    if (!is_constant_range(*node.iterable)) {
                        block(self_ + " loops over a range, and the loop bound is not constant");
                    }
                } else if constexpr (std::is_same_v<S, VarAssign>) {
                    if (node.target != nullptr) {
                        block(self_ + " writes a trait field");
                    }
                } else if constexpr (!std::is_same_v<S, LetStmt> && !std::is_same_v<S, ReturnStmt> &&
                                     !std::is_same_v<S, ExprStmt> && !std::is_same_v<S, IfStmt> &&
                                     !std::is_same_v<S, MatchStmt>) {
                    block(self_ + " uses a statement a shader can't run");
                }
            },
            stmt.stmt);
    }

    template <typename E>
    void check_expression(const E& e, const ExprNode& node) {
        if constexpr (std::is_same_v<E, LiteralExpr>) {
            if (e.kind == LiteralExpr::Kind::String) {
                block(self_ + " uses a string");
            } else if (e.kind == LiteralExpr::Kind::Duration) {
                block(self_ + " uses a duration");
            }
        } else if constexpr (std::is_same_v<E, IdentExpr> || std::is_same_v<E, MemberExpr>) {
            const auto* constant = e.resolved_const_id.has_value() ? lookups_->constant(*e.resolved_const_id) : nullptr;
            if (constant != nullptr && !constant->glsl_portable) {
                block(self_ + " reads constant '" + constant->canonical_id + "', which a shader can't hold");
            }
        } else if constexpr (std::is_same_v<E, CallExpr>) {
            check_call(e);
        } else if constexpr (!std::is_same_v<E, UnaryExpr> && !std::is_same_v<E, BinaryExpr> &&
                             !std::is_same_v<E, MatchExpr> && !std::is_same_v<E, IfExpr>) {
            block(self_ + " uses '" + expression_spelling(node, "an expression") + "', which a shader can't run");
        }
    }

    void check_call(const CallExpr& call) {
        if (is_shader_value_constructor(call) || callee_name(call) == "range") {
            return;
        }
        if (call.resolved_struct_id.has_value()) {
            TypeInfo type;
            type.kind      = TypeKind::Struct;
            type.name      = make_canonical_id(*call.resolved_struct_id);
            type.symbol_id = call.resolved_struct_id;
            if (!holds(type)) {
                block(self_ + " builds struct '" + type.name + "', which a shader can't hold");
            }
            return;
        }
        const auto* callee = call.resolved_callee_id.has_value() ? lookups_->func(*call.resolved_callee_id) : nullptr;
        if (callee == nullptr) {
            block(self_ + " calls '" + expression_spelling(*call.callee, "?") + "', which has no shader translation");
            return;
        }
        if (stage_may_call(*callee, *call.resolved_callee_id)) {
            return;
        }
        if (callee->is_extern) {
            block(self_ + " calls extern func '" + make_canonical_id(*call.resolved_callee_id) +
                  "', which has no shader translation");
            return;
        }
        block(callee->stage_blocker.empty() ? self_ + " calls '" + callee->canonical_id + "', which a shader can't run"
                                            : callee->stage_blocker);
    }
};

}  // namespace

bool is_shader_value_constructor(const CallExpr& call) {
    const auto name = callee_name(call);
    return name == "vec2" || name == "vec3" || name == "color";
}

bool stage_may_call(const ResolvedFunc& func, const SymbolId& symbol) {
    return func.is_extern ? is_portable_glsl_intrinsic(symbol) : func.stage_translatable;
}

bool is_constant_range(const ExprNode& iterable) {
    const auto* call = std::get_if<CallExpr>(&iterable.expr);
    return call != nullptr && callee_name(*call) == "range" &&
           std::ranges::all_of(call->args, [](const auto& arg) { return is_constant_operand(*arg); });
}

std::string stage_func_blocker(const FuncNode& node, const ResolvedFunc& func, const StageTranslationLookups& lookups) {
    return StageFuncCheck(func, lookups).run(node);
}

}  // namespace cactus
