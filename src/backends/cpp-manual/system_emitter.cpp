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

// ── Legacy emit_stmt (indexed model) ──────────────────────────────────────

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

// ── Legacy emit_system ─────────────────────────────────────────────────────

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

// ── Dynamic ECS helpers ────────────────────────────────────────────────────

std::string ManualSystemEmitter::compute_mask_expr(const FilterClause& clause,
                                                    const CodegenContext& /*ctx*/) {
    std::string result;
    // Prefer the simple trait_names list (populated by both old and new parsers)
    if (!clause.trait_names.empty()) {
        for (const auto& name : clause.trait_names) {
            if (!result.empty()) result += " | ";
            result += "TraitBits::" + name;
        }
    } else if (!clause.entries.empty()) {
        // Fall back to entries (extract simple name from qualified name)
        for (const auto& entry : clause.entries) {
            auto dot = entry.qualified_name.find('.');
            std::string simple_name = (dot != std::string::npos)
                                          ? entry.qualified_name.substr(dot + 1)
                                          : entry.qualified_name;
            if (!result.empty()) result += " | ";
            result += "TraitBits::" + simple_name;
        }
    }
    return result.empty() ? "0ULL" : result;
}

std::string ManualSystemEmitter::emit_spawn_call(const SpawnStmt& s,
                                                   const CodegenContext& ctx) {
    auto tmpl_it = ctx.template_ast.find(s.template_name);
    if (tmpl_it == ctx.template_ast.end()) {
        return "/* spawn " + s.template_name + " — template not found */";
    }
    const TemplateNode* tmpl = tmpl_it->second;

    // Build override map: field_name → expr_string
    std::unordered_map<std::string, std::string> overrides;
    for (const auto& [fname, expr] : s.overrides) {
        overrides[fname] = emit_expr(*expr);
    }

    // Build argument list in field declaration order across applied traits
    std::ostringstream args;
    bool first = true;
    for (const auto& entry : tmpl->apply.entries) {
        auto tit = ctx.traits.find(entry.trait_name);
        if (tit == ctx.traits.end()) continue;
        for (const auto& field : tit->second.fields) {
            if (!first) args << ", ";
            first = false;

            if (overrides.count(field.name)) {
                args << overrides[field.name];
            } else {
                // Check template config default
                auto tc_it = ctx.template_config.find(s.template_name);
                if (tc_it != ctx.template_config.end()) {
                    auto fi = tc_it->second.find(field.name);
                    if (fi != tc_it->second.end()) {
                        args << fi->second;
                        continue;
                    }
                }
                // Check trait field default
                auto td_it = ctx.trait_defaults.find(entry.trait_name);
                if (td_it != ctx.trait_defaults.end()) {
                    auto fi = td_it->second.find(field.name);
                    if (fi != td_it->second.end()) {
                        args << fi->second;
                        continue;
                    }
                }
                // Type default
                args << SoaEmitter::default_cpp_value(field.type);
            }
        }
    }

    return "spawn_" + s.template_name + "(" + args.str() + ");";
}

// ── emit_stmt_dynamic ──────────────────────────────────────────────────────

std::string ManualSystemEmitter::emit_stmt_dynamic(const StmtNode& stmt, int indent,
                                                     const CodegenContext& ctx,
                                                     const std::string& entity_index_var,
                                                     bool in_loop) {
    return std::visit(
        [indent, &ctx, &entity_index_var, in_loop](auto& s) -> std::string {
            using S = std::decay_t<decltype(s)>;
            std::string ind = indent_str(indent);

            if constexpr (std::is_same_v<S, VarAssign>) {
                // In the dynamic model, fields are local references — no [i] suffix
                std::string op = s.op;
                return ind + s.name + " " + op + " " + emit_expr(*s.value) + ";\n";

            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                std::string result = ind + s.event_name + "_buffer.push_back({";
                for (size_t i = 0; i < s.args.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += emit_expr(*s.args[i]);
                }
                return result + "});\n";

            } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                // task 7.9: swap-and-delete
                if (in_loop) {
                    return ind + "entity_remove(" + entity_index_var + "); __destroyed = true;\n";
                } else {
                    return ind + "entity_remove(" + entity_index_var + ");\n";
                }

            } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                // task 7.8
                return ind + emit_spawn_call(s, ctx) + "\n";

            } else if constexpr (std::is_same_v<S, LoadStmt>) {
                // task 7.10: deferred load
                return ind + "if (g_load_pending) { g_load_multi_error = true; }\n" +
                       ind + "g_pending_load = \"" + s.module_name + "\";\n" +
                       ind + "g_load_pending = true;\n";

            } else if constexpr (std::is_same_v<S, EnableStmt>) {
                // task 7.5
                return ind + "g_trait_mask[" + entity_index_var + "] |= TraitBits::" +
                       s.trait_name + ";\n";

            } else if constexpr (std::is_same_v<S, DisableStmt>) {
                // task 7.6
                return ind + "g_trait_mask[" + entity_index_var + "] &= ~TraitBits::" +
                       s.trait_name + ";\n";

            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) return ind + "return " + emit_expr(**s.value) + ";\n";
                return ind + "return;\n";

            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind + emit_expr(*s.expr) + ";\n";

            } else if constexpr (std::is_same_v<S, IfStmt>) {
                std::string result = ind + "if (" + emit_expr(*s.condition) + ") {\n";
                for (auto& inner : s.then_body) {
                    result += emit_stmt_dynamic(*inner, indent + 1, ctx, entity_index_var, in_loop);
                }
                result += ind + "}";
                if (!s.else_body.empty()) {
                    result += " else {\n";
                    for (auto& inner : s.else_body) {
                        result += emit_stmt_dynamic(*inner, indent + 1, ctx, entity_index_var,
                                                    in_loop);
                    }
                    result += ind + "}";
                }
                return result + "\n";

            } else {
                return ind + "/* unsupported stmt */\n";
            }
        },
        stmt.stmt);
}

// ── emit_system_forward_decls ──────────────────────────────────────────────

std::string ManualSystemEmitter::emit_system_forward_decls(const SystemNode& sys) {
    std::ostringstream out;
    for (const auto& handler : sys.handlers) {
        bool is_per_entity = (handler.event_name == "spawn" ||
                              handler.event_name == "destroy");
        if (is_per_entity) {
            out << "static void " << sys.name << "_" << handler.event_name
                << "(size_t _idx);\n";
        } else if (handler.event_name == "tick") {
            out << "static void " << sys.name << "_tick(float dt);\n";
        } else {
            out << "static void " << sys.name << "_" << handler.event_name << "();\n";
        }
    }
    return out.str();
}

// ── emit_system_dynamic ────────────────────────────────────────────────────

std::string ManualSystemEmitter::emit_system_dynamic(const SystemNode& sys,
                                                      const CodegenContext& ctx) {
    std::ostringstream out;

    const std::string filter_mask = compute_mask_expr(sys.filter, ctx);
    const std::string exclude_mask = compute_mask_expr(sys.exclude, ctx);

    for (const auto& handler : sys.handlers) {
        bool is_per_entity = (handler.event_name == "spawn" ||
                              handler.event_name == "destroy");

        if (is_per_entity) {
            // ── Per-entity handler: called with a specific entity index ──────
            out << "static void " << sys.name << "_" << handler.event_name
                << "(size_t _idx) {\n";

            // Local references for filter trait fields
            for (const auto& trait_name : sys.filter.trait_names) {
                auto tit = ctx.traits.find(trait_name);
                if (tit == ctx.traits.end()) continue;
                for (const auto& field : tit->second.fields) {
                    out << "    " << SoaEmitter::type_to_cpp(field.type) << "& " << field.name
                        << " = g_" << trait_name << "_" << field.name << "[_idx];\n";
                }
            }

            for (const auto& stmt : handler.body) {
                out << emit_stmt_dynamic(*stmt, 1, ctx, "_idx", /*in_loop=*/false);
            }
            out << "}\n\n";

        } else {
            // ── Loop-based handler (tick, load, unload, or custom event) ─────
            std::string params;
            if (handler.event_name == "tick") {
                params = "float dt";
            } else if (!handler.params.empty()) {
                // Custom event parameters
                for (size_t i = 0; i < handler.params.size(); ++i) {
                    if (i > 0) params += ", ";
                    params += "float " + handler.params[i].name;  // simplified: float for all
                }
            }

            out << "static void " << sys.name << "_" << handler.event_name
                << "(" << params << ") {\n";
            out << "    uint64_t _filter_mask = " << filter_mask << ";\n";
            out << "    uint64_t _exclude_mask = " << exclude_mask << ";\n";

            if (handler.event_name == "load") {
                // tasks 7.14 / 8.4: snapshot entity_count to avoid processing newly spawned
                out << "    size_t _loop_count = entity_count;\n";
                out << "    for (size_t i = 0; i < _loop_count; ++i) {\n";
                out << "        if ((g_trait_mask[i] & _filter_mask) == _filter_mask &&\n";
                out << "            (g_trait_mask[i] & _exclude_mask) == 0) {\n";
                // Local refs for filter trait fields
                for (const auto& trait_name : sys.filter.trait_names) {
                    auto tit = ctx.traits.find(trait_name);
                    if (tit == ctx.traits.end()) continue;
                    for (const auto& field : tit->second.fields) {
                        out << "            " << SoaEmitter::type_to_cpp(field.type) << "& "
                            << field.name << " = g_" << trait_name << "_" << field.name
                            << "[i];\n";
                    }
                }
                for (const auto& stmt : handler.body) {
                    out << emit_stmt_dynamic(*stmt, 3, ctx, "i", /*in_loop=*/false);
                }
                out << "        }\n";
                out << "    }\n";
            } else {
                // tasks 7.3, 7.4, 7.13: while loop (supports destroy via __destroyed flag)
                out << "    size_t i = 0;\n";
                out << "    while (i < entity_count) {\n";
                out << "        if ((g_trait_mask[i] & _filter_mask) == _filter_mask &&\n";
                out << "            (g_trait_mask[i] & _exclude_mask) == 0) {\n";
                // Local refs for filter trait fields
                for (const auto& trait_name : sys.filter.trait_names) {
                    auto tit = ctx.traits.find(trait_name);
                    if (tit == ctx.traits.end()) continue;
                    for (const auto& field : tit->second.fields) {
                        out << "            " << SoaEmitter::type_to_cpp(field.type) << "& "
                            << field.name << " = g_" << trait_name << "_" << field.name
                            << "[i];\n";
                    }
                }
                out << "            bool __destroyed = false;\n";
                for (const auto& stmt : handler.body) {
                    out << emit_stmt_dynamic(*stmt, 3, ctx, "i", /*in_loop=*/true);
                }
                out << "            if (!__destroyed) ++i;\n";
                out << "        } else {\n";
                out << "            ++i;\n";
                out << "        }\n";
                out << "    }\n";
            }

            out << "}\n\n";
        }
    }

    return out.str();
}

}  // namespace cactus
