#include "backends/cpp-manual/system_emitter.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace cactus {

namespace {

bool expr_is_self(const ExprNode& expr) {
    return std::holds_alternative<SelfExpr>(expr.expr);
}

std::string filter_simple_name(const FilterEntry& entry) {
    auto dot = entry.qualified_name.rfind('.');
    return (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
}

bool filter_has_trait(const FilterClause& filter, const std::string& qualified, const std::string& simple) {
    return std::ranges::any_of(filter.entries,
                               [&](const auto& entry) {
                                   return entry.qualified_name == qualified || filter_simple_name(entry) == simple;
                               }) ||
           std::ranges::find(filter.trait_names, simple) != filter.trait_names.end();
}

bool is_flat_transform_propagation(const ExternSystemNode& sys) {
    return filter_has_trait(sys.filter, "std.transform.flat.LocalTransform", "LocalTransform") &&
           filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform");
}

bool is_volume_transform_propagation(const ExternSystemNode& sys) {
    return filter_has_trait(sys.filter, "std.transform.volume.LocalTransform", "LocalTransform") &&
           filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform");
}

std::string emit_expr_dynamic_impl(const ExprNode& expr,
                                   const std::string& entity_index_var); // NOLINT(misc-no-recursion)

// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
std::string emit_expr_dynamic_impl(const ExprNode& expr,
                                   const std::string& entity_index_var) {
    return std::visit(
        [&](auto& e) -> std::string { // NOLINT(readability-function-cognitive-complexity)
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, SelfExpr>) {
                return entity_index_var;
            } else if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::String) {
                    return "\"" + e.value + "\"";
                }
                if (e.kind == LiteralExpr::Kind::Float) {
                    return e.value + "f";
                }
                return e.value;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                std::string op = e.op;
                if (op == "and") {
                    op = "&&";
                } else if (op == "or") {
                    op = "||";
                }
                return "(" + emit_expr_dynamic_impl(*e.left, entity_index_var) + " " + op + " " +
                       emit_expr_dynamic_impl(*e.right, entity_index_var) + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") {
                    op = "!";
                }
                return op + emit_expr_dynamic_impl(*e.operand, entity_index_var);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                std::string result = emit_expr_dynamic_impl(*e.callee, entity_index_var) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += emit_expr_dynamic_impl(*e.args[i], entity_index_var);
                }
                return result + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (!ident->name.empty() && std::isupper(static_cast<unsigned char>(ident->name[0])) != 0) {
                        return ident->name + "::" + e.member;
                    }
                }
                return emit_expr_dynamic_impl(*e.object, entity_index_var) + "." + e.member;
            } else {
                return ManualSystemEmitter::emit_expr(expr);
            }
        },
        expr.expr);
}

}  // namespace

std::string ManualSystemEmitter::indent_str(int level) {
    return std::string(static_cast<size_t>(level) * 4, ' '); // NOLINT(modernize-return-braced-init-list)
}

std::string ManualSystemEmitter::emit_expr(const ExprNode& expr) { // NOLINT(readability-function-cognitive-complexity)
    return std::visit(
        [](auto& e) -> std::string { // NOLINT(readability-function-cognitive-complexity)
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::String) {
                    return "\"" + e.value + "\"";
                }
                if (e.kind == LiteralExpr::Kind::HexColor) {
                    std::string hex = e.value;
                    if (hex.size() == 6) {
                        hex += "FF";
                    }
                    if (hex.size() == 8) {
                        auto byte = [&](size_t offset) {
                            return std::to_string(std::stoi(hex.substr(offset, 2), nullptr, 16));
                        };
                        return "Color{.r = " + byte(0) + ", .g = " + byte(2) + ", .b = " + byte(4) + ", .a = " + byte(6) + "}";
                    }
                }
                if (e.kind == LiteralExpr::Kind::Float) {
                    return e.value + "F";
                }
                return e.value;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                std::string op = e.op;
                if (op == "and") {
                    op = "&&";
                } else if (op == "or") {
                    op = "||";
                }
                return "(" + emit_expr(*e.left) + " " + op + " " + emit_expr(*e.right) + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") {
                    op = "!";
                }
                return op + emit_expr(*e.operand);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                std::string result = emit_expr(*e.callee) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
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
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                return "/* spawn expr */";
            } else {
                return "/* unsupported expr */";
            }
        },
        expr.expr);
}

std::string ManualSystemEmitter::emit_expr_dynamic(const ExprNode& expr,
                                                   const std::string& entity_index_var) {
    return emit_expr_dynamic_impl(expr, entity_index_var);
}

// ── Legacy emit_stmt (indexed model) ──────────────────────────────────────

std::string ManualSystemEmitter::emit_stmt(const StmtNode& stmt, int indent) { // NOLINT(readability-function-cognitive-complexity)
    return std::visit(
        [indent](auto& s) -> std::string {
            using S = std::decay_t<decltype(s)>;
            std::string ind = indent_str(indent);
            if constexpr (std::is_same_v<S, VarAssign>) {
                std::string op = s.op;
                return ind + s.name + "[i] " + op + " " + emit_expr(*s.value) + ";\n";
            } else if constexpr (std::is_same_v<S, LetStmt>) {
                return ind + "auto " + s.name + " = " + emit_expr(*s.value) + ";\n";
            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                std::string result = ind + s.event_name + "_buffer.push_back({";
                for (size_t i = 0; i < s.payload.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += "." + s.payload[i].name + " = " + emit_expr(*s.payload[i].value);
                }
                return result + "});\n";
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) {
                    return ind + "return " + emit_expr(**s.value) + ";\n";
                }
                return ind + "return;\n";
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind + emit_expr(*s.expr) + ";\n";
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                std::string result = ind + "if (" + emit_expr(*s.condition) + ") {\n";
                for (auto& inner : s.then_body) {
                    result += emit_stmt(*inner, indent + 1);
                }
                result += ind + "}";
                if (!s.else_body.empty()) {
                    result += " else {\n";
                    for (auto& inner : s.else_body) {
                        result += emit_stmt(*inner, indent + 1);
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

// ── Legacy emit_system ─────────────────────────────────────────────────────

std::string ManualSystemEmitter::emit_system(const SystemNode& sys, const DecoratedProgram& program) {
    (void)program;  // legacy SoA emitter doesn't use program — suppress C4100
    std::ostringstream out;

    // Collect storage parameter names from filter
    std::vector<std::string> storage_params;
    storage_params.reserve(sys.filter.trait_names.size());
    for (const auto& trait_name : sys.filter.trait_names) {
        std::string param = trait_name;
        param += "Storage& ";
        param += trait_name;
        param += "_store";
        storage_params.push_back(std::move(param));
    }

    for (const auto& handler : sys.handlers) {
        out << "void " << sys.name << "_" << handler.event_name << "(";

        // Storage params
        for (size_t i = 0; i < storage_params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << storage_params[i];
        }

        out << ") {\n";

        // Use first filter trait for count
        if (!sys.filter.trait_names.empty()) {
            const auto& first = sys.filter.trait_names[0];
            out << "    for (size_t i = 0; i < " << first << "_store.count; ++i) {\n";

            // Emit body with SoA array access
            for (const auto& stmt : handler.body) {
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
                                                   const CodegenContext& ctx) {
    (void)ctx;
    std::string result;
    // Prefer the simple trait_names list (populated by both old and new parsers)
    if (!clause.trait_names.empty()) {
        for (const auto& name : clause.trait_names) {
            if (!result.empty()) {
                result += " | ";
            }
            result += "TraitBits::" + name;
        }
    } else if (!clause.entries.empty()) {
        // Fall back to entries (extract simple name from qualified name)
        for (const auto& entry : clause.entries) {
            auto dot = entry.qualified_name.find('.');
            std::string simple_name = (dot != std::string::npos)
                                          ? entry.qualified_name.substr(dot + 1)
                                          : entry.qualified_name;
            if (!result.empty()) {
                result += " | ";
            }
            result += "TraitBits::" + simple_name;
        }
    }
    return result.empty() ? "0ULL" : result;
}

std::string ManualSystemEmitter::emit_spawn_call(const SpawnStmt& s, // NOLINT(readability-function-cognitive-complexity)
                                                   const CodegenContext& ctx) {
    auto tmpl_it = ctx.template_ast.find(s.template_name);
    if (tmpl_it == ctx.template_ast.end()) {
        return "/* spawn " + s.template_name + " — template not found */";
    }
    const TemplateNode* tmpl = tmpl_it->second;

    // Build override map: field_name → expr_string
    std::unordered_map<std::string, std::string> overrides;
    for (const auto& trait : s.overrides) {
        for (const auto& arg : trait.assignments) {
            overrides[arg.name] = emit_expr(*arg.value);
        }
    }

    // Build argument list in field declaration order across declared traits
    std::ostringstream args;
    bool first = true;
    for (const auto& entry : tmpl->traits) {
        auto tit = ctx.traits.find(entry.trait_name);
        if (tit == ctx.traits.end()) {
            continue;
        }
        for (const auto& field : tit->second.fields) {
            if (!first) {
                args << ", ";
            }
            first = false;

            if (static_cast<unsigned int>(overrides.contains(field.name)) != 0U) {
                args << overrides[field.name];
            } else {
                bool found_default = false;
                for (const auto& assign : entry.assignments) {
                    if (assign.name == field.name) {
                        args << ManualSystemEmitter::emit_expr(*assign.value);
                        found_default = true;
                        break;
                    }
                }
                if (found_default) {
                    continue;
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

std::string ManualSystemEmitter::emit_stmt_dynamic(const StmtNode& stmt, int indent, // NOLINT(readability-function-cognitive-complexity)
                                                     const CodegenContext& ctx,
                                                     const std::string& entity_index_var,
                                                     bool in_loop) {
    return std::visit(
        [indent, &ctx, &entity_index_var, in_loop](auto& s) -> std::string { // NOLINT(readability-function-cognitive-complexity)
            using S = std::decay_t<decltype(s)>;
            std::string ind = indent_str(indent);

            if constexpr (std::is_same_v<S, VarAssign>) {
                // In the dynamic model, fields are local references — no [i] suffix
                std::string op = s.op;
                return ind + s.name + " " + op + " " + emit_expr_dynamic(*s.value, entity_index_var) + ";\n";

            } else if constexpr (std::is_same_v<S, LetStmt>) {
                return ind + "auto " + s.name + " = " + emit_expr_dynamic(*s.value, entity_index_var) + ";\n";

            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                std::string result = ind + s.event_name + "_buffer.push_back({";
                for (size_t i = 0; i < s.payload.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    const auto& payload = s.payload.at(i);
                    result += "." + payload.name + " = " + emit_expr_dynamic(*payload.value, entity_index_var);
                }
                return result + "});\n";

            } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                // task 7.9: swap-and-delete
                if (s.target_expr.has_value()) {
                    return ind + "cactus_entity_remove_recursive(" + emit_expr_dynamic(**s.target_expr, entity_index_var) + ");\n";
                }
                if (in_loop) {
                    return ind + "cactus_entity_remove_recursive(" + entity_index_var + "); __destroyed = true;\n";
                }
                return ind + "cactus_entity_remove_recursive(" + entity_index_var + ");\n";

            } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                // task 7.8
                return ind + emit_spawn_call(s, ctx) + "\n";

            } else if constexpr (std::is_same_v<S, LoadStmt>) {
                // task 7.10: deferred load
                return ind + "if (g_load_pending) { g_load_multi_error = true; }\n" +
                       ind + "g_pending_load = \"" + s.module_name + "\";\n" +
                       ind + "g_load_pending = true;\n";

            } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                std::string target = s.target_expr.has_value() ? emit_expr_dynamic(**s.target_expr, entity_index_var) : entity_index_var;
                std::ostringstream result;
                result << ind << "if ((g_trait_mask[" << target << "] & TraitBits::" << s.trait_name << ") == 0) {\n";
                auto td_it = ctx.trait_defaults.find(s.trait_name);
                auto trait_it = ctx.traits.find(s.trait_name);
                if (trait_it != ctx.traits.end()) {
                    for (const auto& field : trait_it->second.fields) {
                        std::string init_value = SoaEmitter::default_cpp_value(field.type);
                        if (td_it != ctx.trait_defaults.end()) {
                            auto def_it = td_it->second.find(field.name);
                            if (def_it != td_it->second.end()) {
                                init_value = def_it->second;
                            }
                        }
                        result << ind << "    g_" << s.trait_name << "_" << field.name << "[" << target << "] = "
                               << init_value << ";\n";
                    }
                }
                result << ind << "}\n";
                for (const auto& arg : s.args) {
                    result << ind << "g_" << s.trait_name << "_" << arg.name << "[" << target << "] = "
                           << emit_expr_dynamic(*arg.value, entity_index_var) << ";\n";
                }
                result << ind << "g_trait_mask[" << target << "] |= TraitBits::" << s.trait_name << ";\n";
                return result.str();

            } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                std::string target = s.target_expr.has_value() ? emit_expr_dynamic(**s.target_expr, entity_index_var) : entity_index_var;
                return ind + "g_trait_mask[" + target + "] &= ~TraitBits::" + s.trait_name + ";\n";

            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) {
                    return ind + "return " + emit_expr_dynamic(**s.value, entity_index_var) + ";\n";
                }
                return ind + "return;\n";

            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind + emit_expr_dynamic(*s.expr, entity_index_var) + ";\n";

            } else if constexpr (std::is_same_v<S, IfStmt>) {
                std::string result = ind + "if (" + emit_expr_dynamic(*s.condition, entity_index_var) + ") {\n";
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
        const std::string EVENT_VAR = handler.alias.value_or(handler.event_name);
        bool is_per_entity = (handler.event_name == "spawn" ||
                              handler.event_name == "destroy");
        if (is_per_entity) {
            out << "static void " << sys.name << "_" << handler.event_name
                << "(size_t _idx, const " << handler.event_name << "Event& " << EVENT_VAR << ");\n";
        } else {
            out << "static void " << sys.name << "_" << handler.event_name
                << "(const " << handler.event_name << "Event& " << EVENT_VAR << ");\n";
        }
    }
    return out.str();
}

// ── emit_system_dynamic ────────────────────────────────────────────────────

std::string ManualSystemEmitter::emit_system_dynamic(const SystemNode& sys, // NOLINT(readability-function-cognitive-complexity)
                                                      const CodegenContext& ctx) {
    std::ostringstream out;

    const std::string FILTER_MASK  = compute_mask_expr(sys.filter, ctx);
    const std::string EXCLUDE_MASK = compute_mask_expr(sys.exclude, ctx);

    for (const auto& handler : sys.handlers) {
        bool is_per_entity = (handler.event_name == "spawn" ||
                              handler.event_name == "destroy");
        const std::string EVENT_VAR = handler.alias.value_or(handler.event_name);

        if (is_per_entity) {
            // ── Per-entity handler: called with a specific entity index ──────
            out << "static void " << sys.name << "_" << handler.event_name
                << "(size_t _idx, const " << handler.event_name << "Event& " << EVENT_VAR << ") {\n";

            // Local references for filter trait fields
            for (const auto& trait_name : sys.filter.trait_names) {
                auto tit = ctx.traits.find(trait_name);
                if (tit == ctx.traits.end()) {
                    continue;
                }
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
            out << "static void " << sys.name << "_" << handler.event_name
                << "(const " << handler.event_name << "Event& " << EVENT_VAR << ") {\n";
            out << "    uint64_t _filter_mask = " << FILTER_MASK << ";\n";
            out << "    uint64_t _exclude_mask = " << EXCLUDE_MASK << ";\n";

            if (handler.event_name == "load") {
                // tasks 7.14 / 8.4: snapshot entity_count to avoid processing newly spawned
                out << "    size_t _loop_count = entity_count;\n";
                out << "    for (size_t i = 0; i < _loop_count; ++i) {\n";
                out << "        if ((g_trait_mask[i] & _filter_mask) == _filter_mask &&\n";
                out << "            (g_trait_mask[i] & _exclude_mask) == 0) {\n";
                // Local refs for filter trait fields
                for (const auto& trait_name : sys.filter.trait_names) {
                    auto tit = ctx.traits.find(trait_name);
                    if (tit == ctx.traits.end()) {
                        continue;
                    }
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
                    if (tit == ctx.traits.end()) {
                        continue;
                    }
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

std::string ManualSystemEmitter::emit_extern_system_dynamic(const ExternSystemNode& sys, const CodegenContext& ctx) {
    (void)ctx;
    std::ostringstream out;

    if (is_flat_transform_propagation(sys)) {
        out << "static void " << sys.name << "_tick() {\n";
        out << "    std::vector<uint8_t> _active(entity_count, 0);\n";
        out << "    auto _resolve = [&](auto&& self, size_t _idx) -> void {\n";
        out << "        if (_idx >= entity_count || _active[_idx]) return;\n";
        out << "        _active[_idx] = 1;\n";
        out << "        bool _copied_local = false;\n";
        out << "        if ((g_trait_mask[_idx] & TraitBits::Parent) != 0) {\n";
        out << "            uint32_t _parent = g_Parent_parent[_idx];\n";
        out << "            if (_parent < entity_count && (g_trait_mask[_parent] & TraitBits::WorldTransform) != 0) {\n";
        out << "                self(self, _parent);\n";
        out << "                g_WorldTransform_position[_idx] = {g_WorldTransform_position[_parent].x + g_LocalTransform_position[_idx].x, g_WorldTransform_position[_parent].y + g_LocalTransform_position[_idx].y};\n";
        out << "                g_WorldTransform_rotation[_idx] = g_WorldTransform_rotation[_parent] + g_LocalTransform_rotation[_idx];\n";
        out << "                g_WorldTransform_scale[_idx] = {g_WorldTransform_scale[_parent].x * g_LocalTransform_scale[_idx].x, g_WorldTransform_scale[_parent].y * g_LocalTransform_scale[_idx].y};\n";
        out << "                _copied_local = true;\n";
        out << "            }\n";
        out << "        }\n";
        out << "        if (!_copied_local) {\n";
        out << "            g_WorldTransform_position[_idx] = g_LocalTransform_position[_idx];\n";
        out << "            g_WorldTransform_rotation[_idx] = g_LocalTransform_rotation[_idx];\n";
        out << "            g_WorldTransform_scale[_idx] = g_LocalTransform_scale[_idx];\n";
        out << "        }\n";
        out << "        _active[_idx] = 0;\n";
        out << "    };\n";
        out << "    for (size_t i = 0; i < entity_count; ++i) {\n";
        out << "        if ((g_trait_mask[i] & (TraitBits::LocalTransform | TraitBits::WorldTransform)) == (TraitBits::LocalTransform | TraitBits::WorldTransform)) {\n";
        out << "            _resolve(_resolve, i);\n";
        out << "        }\n";
        out << "    }\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_volume_transform_propagation(sys)) {
        out << "static void " << sys.name << "_tick() {\n";
        out << "    std::vector<uint8_t> _active(entity_count, 0);\n";
        out << "    auto _resolve = [&](auto&& self, size_t _idx) -> void {\n";
        out << "        if (_idx >= entity_count || _active[_idx]) return;\n";
        out << "        _active[_idx] = 1;\n";
        out << "        bool _copied_local = false;\n";
        out << "        if ((g_trait_mask[_idx] & TraitBits::Parent) != 0) {\n";
        out << "            uint32_t _parent = g_Parent_parent[_idx];\n";
        out << "            if (_parent < entity_count && (g_trait_mask[_parent] & TraitBits::WorldTransform) != 0) {\n";
        out << "                self(self, _parent);\n";
        out << "                g_WorldTransform_position[_idx] = {g_WorldTransform_position[_parent].x + g_LocalTransform_position[_idx].x, g_WorldTransform_position[_parent].y + g_LocalTransform_position[_idx].y, g_WorldTransform_position[_parent].z + g_LocalTransform_position[_idx].z};\n";
        out << "                g_WorldTransform_rotation[_idx] = g_LocalTransform_rotation[_idx];\n";
        out << "                g_WorldTransform_scale[_idx] = {g_WorldTransform_scale[_parent].x * g_LocalTransform_scale[_idx].x, g_WorldTransform_scale[_parent].y * g_LocalTransform_scale[_idx].y, g_WorldTransform_scale[_parent].z * g_LocalTransform_scale[_idx].z};\n";
        out << "                _copied_local = true;\n";
        out << "            }\n";
        out << "        }\n";
        out << "        if (!_copied_local) {\n";
        out << "            g_WorldTransform_position[_idx] = g_LocalTransform_position[_idx];\n";
        out << "            g_WorldTransform_rotation[_idx] = g_LocalTransform_rotation[_idx];\n";
        out << "            g_WorldTransform_scale[_idx] = g_LocalTransform_scale[_idx];\n";
        out << "        }\n";
        out << "        _active[_idx] = 0;\n";
        out << "    };\n";
        out << "    for (size_t i = 0; i < entity_count; ++i) {\n";
        out << "        if ((g_trait_mask[i] & (TraitBits::LocalTransform | TraitBits::WorldTransform)) == (TraitBits::LocalTransform | TraitBits::WorldTransform)) {\n";
        out << "            _resolve(_resolve, i);\n";
        out << "        }\n";
        out << "    }\n";
        out << "}\n\n";
        return out.str();
    }

    out << "static void " << sys.name << "_tick() {\n";
    out << "    // generic extern system scaffold\n";
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
