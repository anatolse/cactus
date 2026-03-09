#include "backends/cpp-manual/system_emitter.h"

#include <sstream>

namespace cactus {

std::string ManualSystemEmitter::indent_str(int level) {
    return std::string(static_cast<size_t>(level) * 4, ' ');
}

std::string ManualSystemEmitter::emit_expr(const ExprNode& expr) {
    return std::visit(
        [](auto& e) -> std::string {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::String) return "\"" + e.value + "\"";
                if (e.kind == LiteralExpr::Kind::Float) return e.value + "f";
                return e.value;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                std::string op = e.op;
                if (op == "and") op = "&&";
                else if (op == "or") op = "||";
                return "(" + emit_expr(*e.left) + " " + op + " " + emit_expr(*e.right) + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") op = "!";
                return op + emit_expr(*e.operand);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                std::string result = emit_expr(*e.callee) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += emit_expr(*e.args[i]);
                }
                return result + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                // Check if object looks like an enum name (starts with uppercase)
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (!ident->name.empty() && std::isupper(ident->name[0])) {
                        return ident->name + "::" + e.member;
                    }
                }
                return emit_expr(*e.object) + "." + e.member;
            } else {
                return "/* unsupported expr */";
            }
        },
        expr.expr);
}

std::string ManualSystemEmitter::emit_stmt(const StmtNode& stmt, int indent) {
    return std::visit(
        [indent](auto& s) -> std::string {
            using S = std::decay_t<decltype(s)>;
            std::string ind = indent_str(indent);
            if constexpr (std::is_same_v<S, VarAssign>) {
                std::string op = s.op;
                return ind + s.name + "[i] " + op + " " + emit_expr(*s.value) + ";\n";
            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                std::string result = ind + s.event_name + "_buffer.push_back({";
                for (size_t i = 0; i < s.args.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += emit_expr(*s.args[i]);
                }
                return result + "});\n";
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) return ind + "return " + emit_expr(**s.value) + ";\n";
                return ind + "return;\n";
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind + emit_expr(*s.expr) + ";\n";
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                std::string result = ind + "if (" + emit_expr(*s.condition) + ") {\n";
                for (auto& inner : s.then_body) result += emit_stmt(*inner, indent + 1);
                result += ind + "}";
                if (!s.else_body.empty()) {
                    result += " else {\n";
                    for (auto& inner : s.else_body) result += emit_stmt(*inner, indent + 1);
                    result += ind + "}";
                }
                return result + "\n";
            } else {
                return ind + "/* unsupported stmt */\n";
            }
        },
        stmt.stmt);
}

std::string ManualSystemEmitter::emit_system(const SystemNode& sys, const DecoratedProgram& program) {
    std::ostringstream out;

    // Collect storage parameter names from filter
    std::vector<std::string> storage_params;
    for (auto& trait_name : sys.filter.trait_names) {
        storage_params.push_back(trait_name + "Storage& " + trait_name + "_store");
    }

    for (auto& handler : sys.handlers) {
        out << "void " << sys.name << "_" << handler.event_name << "(";

        // Storage params
        for (size_t i = 0; i < storage_params.size(); ++i) {
            if (i > 0) out << ", ";
            out << storage_params[i];
        }

        // Handler params
        for (auto& param : handler.params) {
            if (!storage_params.empty()) out << ", ";
            out << SoaEmitter::type_to_cpp(program.traits.count(param.type.name)
                                                ? program.traits.at(param.type.name).fields[0].type
                                                : TypeInfo{TypeKind::Float, "float"})
                << " " << param.name;
        }

        out << ") {\n";

        // Use first filter trait for count
        if (!sys.filter.trait_names.empty()) {
            auto& first = sys.filter.trait_names[0];
            out << "    for (size_t i = 0; i < " << first << "_store.count; ++i) {\n";

            // Emit body with SoA array access
            for (auto& stmt : handler.body) {
                out << emit_stmt(*stmt, 2);
            }

            out << "    }\n";
        }

        out << "}\n\n";
    }

    return out.str();
}

}  // namespace cactus
