#include "backends/cpp-entt/system_emitter.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cactus {

// ── Helper: resolve which component a field belongs to ──────────────────────

static std::string find_comp_for_field(const std::string& field_name,
                                        const std::vector<std::string>& trait_names,
                                        const DecoratedProgram& program) {
    for (auto& tn : trait_names) {
        auto it = program.traits.find(tn);
        if (it != program.traits.end()) {
            for (auto& f : it->second.fields) {
                if (f.name == field_name) return tn;
            }
        }
    }
    return "";
}

// ── Helper: collect all field names from filter traits ───────────────────────

static std::unordered_set<std::string> collect_trait_fields(
    const std::vector<std::string>& trait_names,
    const DecoratedProgram& program) {
    std::unordered_set<std::string> fields;
    for (auto& tn : trait_names) {
        auto it = program.traits.find(tn);
        if (it != program.traits.end()) {
            for (auto& f : it->second.fields) {
                fields.insert(f.name);
            }
        }
    }
    return fields;
}

// ── Rewrite expression: replace bare field names with comp.field ─────────────

static std::string rewrite_expr(const ExprNode& expr,
                                 const std::vector<std::string>& trait_names,
                                 const DecoratedProgram& program);

static std::string rewrite_expr(const ExprNode& expr,
                                 const std::vector<std::string>& trait_names,
                                 const DecoratedProgram& program) {
    auto known_fields = collect_trait_fields(trait_names, program);

    return std::visit(
        [&](auto& e) -> std::string {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::String) return "\"" + e.value + "\"";
                if (e.kind == LiteralExpr::Kind::Float) return e.value + "f";
                return e.value;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                // If it's a known trait field, qualify it
                if (known_fields.count(e.name)) {
                    auto comp = find_comp_for_field(e.name, trait_names, program);
                    if (!comp.empty()) return comp + "_comp." + e.name;
                }
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                std::string op = e.op;
                if (op == "and") op = "&&";
                else if (op == "or") op = "||";
                return "(" + rewrite_expr(*e.left, trait_names, program) + " " + op + " " +
                       rewrite_expr(*e.right, trait_names, program) + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") op = "!";
                return op + rewrite_expr(*e.operand, trait_names, program);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                std::string result = rewrite_expr(*e.callee, trait_names, program) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += rewrite_expr(*e.args[i], trait_names, program);
                }
                return result + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    // Translate tick.dt / fixed_tick.dt / late_tick.dt → dt
                    if ((ident->name == "tick" || ident->name == "fixed_tick" ||
                         ident->name == "late_tick") && e.member == "dt") {
                        return "dt";
                    }
                    // Enum names — use :: notation
                    if (program.enums.count(ident->name)) {
                        return ident->name + "::" + e.member;
                    }
                }
                return rewrite_expr(*e.object, trait_names, program) + "." + e.member;
            } else {
                return "/* unsupported expr */";
            }
        },
        expr.expr);
}

// ── Rewrite statement: replace field[i] = with comp.field = ─────────────────

static std::string rewrite_stmt(const StmtNode& stmt, int indent,
                                 const std::vector<std::string>& trait_names,
                                 const DecoratedProgram& program) {
    auto known_fields = collect_trait_fields(trait_names, program);
    std::string ind(static_cast<size_t>(indent) * 4, ' ');

    return std::visit(
        [&](auto& s) -> std::string {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, VarAssign>) {
                std::string lhs;
                if (known_fields.count(s.name)) {
                    auto comp = find_comp_for_field(s.name, trait_names, program);
                    if (!comp.empty())
                        lhs = comp + "_comp." + s.name;
                    else
                        lhs = s.name;
                } else {
                    // Local variable — use auto for declaration
                    lhs = "auto " + s.name;
                }
                return ind + lhs + " " + s.op + " " +
                       rewrite_expr(*s.value, trait_names, program) + ";\n";
            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                std::string result = ind + s.event_name + "_buffer.push_back({";
                for (size_t i = 0; i < s.args.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += rewrite_expr(*s.args[i], trait_names, program);
                }
                return result + "});\n";
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) return ind + "return " + rewrite_expr(**s.value, trait_names, program) + ";\n";
                return ind + "return;\n";
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind + rewrite_expr(*s.expr, trait_names, program) + ";\n";
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                std::string result = ind + "if (" + rewrite_expr(*s.condition, trait_names, program) + ") {\n";
                for (auto& inner : s.then_body)
                    result += rewrite_stmt(*inner, indent + 1, trait_names, program);
                result += ind + "}";
                if (!s.else_body.empty()) {
                    result += " else {\n";
                    for (auto& inner : s.else_body)
                        result += rewrite_stmt(*inner, indent + 1, trait_names, program);
                    result += ind + "}";
                }
                return result + "\n";
            } else {
                return ind + "/* unsupported stmt */\n";
            }
        },
        stmt.stmt);
}

std::string EnttSystemEmitter::emit_system(const SystemNode& sys, const DecoratedProgram& program) {
    std::ostringstream out;

    for (auto& handler : sys.handlers) {
        out << "void " << sys.name << "_" << handler.event_name << "(entt::registry& registry";

        // tick/fixed_tick/late_tick handlers receive a float dt
        if (handler.event_name == "tick" || handler.event_name == "fixed_tick" ||
            handler.event_name == "late_tick") {
            out << ", float dt";
        }
        out << ") {\n";

        // Build view template args
        out << "    auto view = registry.view<";
        for (size_t i = 0; i < sys.filter.trait_names.size(); ++i) {
            if (i > 0) out << ", ";
            out << sys.filter.trait_names[i];
        }
        out << ">();\n";

        // each() lambda
        out << "    view.each([&](";
        for (size_t i = 0; i < sys.filter.trait_names.size(); ++i) {
            if (i > 0) out << ", ";
            out << "auto& " << sys.filter.trait_names[i] << "_comp";
        }
        out << ") {\n";

        // Emit body with proper component field access
        for (auto& stmt : handler.body) {
            out << rewrite_stmt(*stmt, 2, sys.filter.trait_names, program);
        }

        out << "    });\n";
        out << "}\n\n";
    }

    return out.str();
}

}  // namespace cactus
