#include "backends/cpp-entt/system_emitter.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cactus {

namespace {

std::string sort_key_expr(const SortKey& key, const std::string& entity_name,
                         const SystemNode& sys) {
    auto alias_to_trait = [&]() -> std::string {
        for (const auto& entry : sys.filter.entries) {
            auto dot = entry.qualified_name.rfind('.');
            auto simple = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
            if (entry.alias.has_value() && *entry.alias == key.alias) {
                return simple;
            }
            if (simple == key.alias) {
                return simple;
            }
        }
        for (const auto& trait : sys.filter.trait_names) {
            if (trait == key.alias) {
                return trait;
            }
        }
        return "";
    };

    std::string trait_name = alias_to_trait();
    std::ostringstream expr;
    expr << "registry.get<" << trait_name << ">(" << entity_name << ")." << key.field;
    return expr.str();
}

std::string primary_sort_trait(const SortKey& key, const SystemNode& sys) {
    for (const auto& entry : sys.filter.entries) {
        auto dot = entry.qualified_name.rfind('.');
        auto simple = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
        if ((entry.alias.has_value() && *entry.alias == key.alias) || simple == key.alias) {
            return simple;
        }
    }
    for (const auto& trait : sys.filter.trait_names) {
        if (trait == key.alias) {
            return trait;
        }
    }
    return key.alias;
}

void emit_sort_call(std::ostringstream& out, const SystemNode& sys) {
    if (sys.order_by.empty()) {
        return;
    }

    out << "    registry.sort<" << primary_sort_trait(sys.order_by.front(), sys) << ">([&](entt::entity a, entt::entity b) {\n";
    for (const auto& key : sys.order_by) {
        auto left = sort_key_expr(key, "a", sys);
        auto right = sort_key_expr(key, "b", sys);
        out << "        if (" << left << " != " << right << ") return "
            << left << (key.descending ? " > " : " < ") << right << ";\n";
    }
    out << "        return false;\n";
    out << "    });\n";
}

}  // namespace

// ── Helper: resolve which component a field belongs to ──────────────────────

static std::string find_comp_for_field(const std::string& field_name,
                                        const std::vector<std::string>& trait_names,
                                        const DecoratedProgram& program) {
    for (const auto& tn : trait_names) {
        auto it = program.traits.find(tn);
        if (it != program.traits.end()) {
            for (const auto& f : it->second.fields) {
                if (f.name == field_name) {
                    return tn;
                }
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
    for (const auto& tn : trait_names) {
        auto it = program.traits.find(tn);
        if (it != program.traits.end()) {
            for (const auto& f : it->second.fields) {
                fields.insert(f.name);
            }
        }
    }
    return fields;
}

// ── Rewrite expression: replace bare field names with comp.field ─────────────

static std::string rewrite_expr(const ExprNode& expr,
                                 const std::vector<std::string>& trait_names,
                                 const DecoratedProgram& program,
                                 const std::unordered_set<std::string>& pointer_aliases = {});

static std::string rewrite_stmt(const StmtNode& stmt,
                                 int indent,
                                 const std::vector<std::string>& trait_names,
                                 const DecoratedProgram& program,
                                 const std::unordered_set<std::string>& pointer_aliases = {});

static std::string emit_trait_match_stmt(const TraitMatchStmt& match_stmt,
                                         int indent,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases = {});

static std::string rewrite_expr(const ExprNode& expr, // NOLINT(readability-function-cognitive-complexity)
                                 const std::vector<std::string>& trait_names,
                                  const DecoratedProgram& program,
                                  const std::unordered_set<std::string>& pointer_aliases) {
    auto known_fields = collect_trait_fields(trait_names, program);

    return std::visit(
        [&](auto& e) -> std::string { // NOLINT(readability-function-cognitive-complexity)
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::String) {
                    return "\"" + e.value + "\"";
                }
                if (e.kind == LiteralExpr::Kind::Float) {
                    return e.value + "f";
                }
                return e.value;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                // If it's a known trait field, qualify it
                if (known_fields.contains(e.name)) {
                    auto comp = find_comp_for_field(e.name, trait_names, program);
                    if (!comp.empty()) {
                        return comp + "_comp." + e.name;
                    }
                }
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                std::string op = e.op;
                if (op == "and") {
                    op = "&&";
                } else if (op == "or") {
                    op = "||";
                }
                return "(" + rewrite_expr(*e.left, trait_names, program, pointer_aliases) + " " + op + " " +
                       rewrite_expr(*e.right, trait_names, program, pointer_aliases) + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") {
                    op = "!";
                }
                return op + rewrite_expr(*e.operand, trait_names, program, pointer_aliases);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                if (auto* ident = std::get_if<IdentExpr>(&e.callee->expr);
                    ident != nullptr && ident->name == "exists" && e.args.size() == 1) {
                    return "registry.valid(" +
                           rewrite_expr(*e.args[0], trait_names, program, pointer_aliases) + ")";
                }
                std::string result = rewrite_expr(*e.callee, trait_names, program, pointer_aliases) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += rewrite_expr(*e.args[i], trait_names, program, pointer_aliases);
                }
                return result + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    // Enum names — use :: notation
                    if (program.enums.contains(ident->name)) {
                        return ident->name + "::" + e.member;
                    }
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (pointer_aliases.contains(ident->name)) {
                        return ident->name + "->" + e.member;
                    }
                }
                return rewrite_expr(*e.object, trait_names, program, pointer_aliases) + "." + e.member;
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                return "/* spawn expr */";
            } else {
                return "/* unsupported expr */";
            }
        },
        expr.expr);
}

// ── Rewrite statement: replace field[i] = with comp.field = ─────────────────

static std::string emit_trait_match_stmt(const TraitMatchStmt& match_stmt,
                                         int indent,
                                         const std::vector<std::string>& trait_names,
                                         const DecoratedProgram& program,
                                         const std::unordered_set<std::string>& pointer_aliases) {
    std::string ind(static_cast<size_t>(indent) * 4, ' ');
    std::ostringstream out;

    out << ind << "{\n";
    out << ind << "    auto __match_entity = "
        << rewrite_expr(*match_stmt.subject, trait_names, program, pointer_aliases) << ";\n";
    out << ind << "    if (registry.valid(__match_entity)) {\n";

    bool first = true;
    for (const auto& arm : match_stmt.arms) {
        const auto TRAIT_IT  = program.traits.find(arm.trait_name);
        const bool IS_MARKER = TRAIT_IT == program.traits.end() || TRAIT_IT->second.fields.empty();
        std::unordered_set<std::string> arm_aliases = pointer_aliases;

        out << ind << "        " << (first ? "if" : "else if") << " (";
        if (IS_MARKER) {
            out << "registry.all_of<" << arm.trait_name << ">(__match_entity)) {\n";
        } else {
            const std::string ALIAS = arm.alias.value_or("__match_" + arm.trait_name);
            arm_aliases.insert(ALIAS);
            out << "auto* " << ALIAS << " = registry.try_get<" << arm.trait_name << ">(__match_entity)) {\n";
        }

        for (const auto& stmt : arm.body) {
            out << rewrite_stmt(*stmt, indent + 3, trait_names, program, arm_aliases);
        }
        out << ind << "        }";
        first = false;
        if (!first || arm.location.line >= 0) {
            out << "\n";
        }
    }

    if (match_stmt.wildcard.has_value()) {
        out << ind << "        " << (first ? "if (true)" : "else") << " {\n";
        for (const auto& stmt : match_stmt.wildcard->body) {
            out << rewrite_stmt(*stmt, indent + 3, trait_names, program, pointer_aliases);
        }
        out << ind << "        }\n";
    }

    out << ind << "    }\n";
    out << ind << "}\n";
    return out.str();
}

static std::string rewrite_stmt(const StmtNode& stmt, int indent, // NOLINT(readability-function-cognitive-complexity)
                                 const std::vector<std::string>& trait_names,
                                 const DecoratedProgram& program,
                                 const std::unordered_set<std::string>& pointer_aliases) {
    auto known_fields = collect_trait_fields(trait_names, program);
    std::string ind(static_cast<size_t>(indent) * 4, ' ');

    return std::visit(
        [&](auto& s) -> std::string { // NOLINT(readability-function-cognitive-complexity)
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, LetStmt>) {
                return ind + "auto " + s.name + " = " + rewrite_expr(*s.value, trait_names, program, pointer_aliases) + ";\n";
            } else if constexpr (std::is_same_v<S, VarAssign>) {
                std::string lhs;
                if (known_fields.contains(s.name)) {
                    auto comp = find_comp_for_field(s.name, trait_names, program);
                    if (!comp.empty()) {
                        lhs = comp + "_comp." + s.name;
                    } else {
                        lhs = s.name;
                    }
                } else {
                    // Local variable — use auto for declaration
                    lhs = "auto " + s.name;
                }
                return ind + lhs + " " + s.op + " " +
                       rewrite_expr(*s.value, trait_names, program, pointer_aliases) + ";\n";
            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                std::string emit_call = s.event_name + "_buffer.push_back({";
                for (size_t i = 0; i < s.payload.size(); ++i) {
                    if (i > 0) {
                        emit_call += ", ";
                    }
                    emit_call += "." + s.payload[i].name + " = " + rewrite_expr(*s.payload[i].value, trait_names, program, pointer_aliases);
                }
                emit_call += "});";
                if (s.target.has_value()) {
                    const std::string TARGET = rewrite_expr(**s.target, trait_names, program, pointer_aliases);
                    return ind + "if (registry.valid(" + TARGET + ")) {\n" + ind + "    " + emit_call + "\n" + ind +
                           "}\n";
                }
                return ind + emit_call + "\n";
            } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                std::string target = s.target_expr.has_value()
                    ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases)
                    : "entity";
                const bool GUARDED = s.target_expr.has_value();
                if (s.args.empty()) {
                    if (GUARDED) {
                        return ind + "if (registry.valid(" + target + ")) {\n" +
                               ind + "    registry.emplace_or_replace<" + s.trait_name + ">(" + target + ");\n" +
                               ind + "}\n";
                    }
                    return ind + "registry.emplace_or_replace<" + s.trait_name + ">(" + target + ");\n";
                }

                std::ostringstream result;
                if (GUARDED) {
                    result << ind << "if (registry.valid(" << target << ")) {\n";
                }
                result << ind << (GUARDED ? "    " : "") << "{\n";
                result << ind << (GUARDED ? "        " : "    ") << "auto __existing = registry.try_get<"
                       << s.trait_name << ">(" << target << ");\n";
                result << ind << (GUARDED ? "        " : "    ")
                       << "auto __value = __existing ? *__existing : " << s.trait_name << "{};\n";
                for (const auto& arg : s.args) {
                    result << ind << (GUARDED ? "        " : "    ") << "__value." << arg.name << " = "
                           << rewrite_expr(*arg.value, trait_names, program, pointer_aliases) << ";\n";
                }
                result << ind << (GUARDED ? "        " : "    ") << "registry.emplace_or_replace<" << s.trait_name
                       << ">(" << target << ", __value);\n";
                result << ind << (GUARDED ? "    " : "") << "}\n";
                if (GUARDED) {
                    result << ind << "}\n";
                }
                return result.str();
            } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                std::string target = s.target_expr.has_value()
                    ? rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases)
                    : "entity";
                if (s.target_expr.has_value()) {
                    return ind + "if (registry.valid(" + target + ")) {\n" +
                           ind + "    registry.remove<" + s.trait_name + ">(" + target + ");\n" +
                           ind + "}\n";
                }
                return ind + "registry.remove<" + s.trait_name + ">(" + target + ");\n";
            } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                if (s.target_expr.has_value()) {
                    std::string target = rewrite_expr(**s.target_expr, trait_names, program, pointer_aliases);
                    return ind + "if (registry.valid(" + target + ")) {\n" +
                           ind + "    registry.destroy(" + target + ");\n" +
                           ind + "}\n";
                }
                return ind + "registry.destroy(entity);\n";
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) {
                    return ind + "return " + rewrite_expr(**s.value, trait_names, program, pointer_aliases) + ";\n";
                }
                return ind + "return;\n";
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                return ind + rewrite_expr(*s.expr, trait_names, program, pointer_aliases) + ";\n";
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                std::string result = ind + "if (" + rewrite_expr(*s.condition, trait_names, program, pointer_aliases) + ") {\n";
                for (auto& inner : s.then_body) {
                    result += rewrite_stmt(*inner, indent + 1, trait_names, program, pointer_aliases);
                }
                result += ind + "}";
                if (!s.else_body.empty()) {
                    result += " else {\n";
                    for (auto& inner : s.else_body) {
                        result += rewrite_stmt(*inner, indent + 1, trait_names, program, pointer_aliases);
                    }
                    result += ind + "}";
                }
                return result + "\n";
            } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                return emit_trait_match_stmt(s, indent, trait_names, program, pointer_aliases);
            } else {
                return ind + "/* unsupported stmt */\n";
            }
        },
        stmt.stmt);
}

std::string EnttSystemEmitter::emit_system(const SystemNode& sys, const DecoratedProgram& program) {
    std::ostringstream out;

    for (const auto& handler : sys.handlers) {
        out << "void " << sys.name << "_" << handler.event_name << "(entt::registry& registry";
        out << ", const " << handler.event_name << "Event& "
            << handler.alias.value_or(handler.event_name);
        out << ") {\n";

        // Build view template args
        emit_sort_call(out, sys);
        out << "    auto view = registry.view<";
        for (size_t i = 0; i < sys.filter.trait_names.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << sys.filter.trait_names[i];
        }
        out << ">();\n";

        // each() lambda
        out << "    view.each([&](";
        out << "entt::entity entity";
        for (const auto& trait_name : sys.filter.trait_names) {
            out << ", ";
            out << "auto& " << trait_name << "_comp";
        }
        out << ") {\n";

        // Emit body with proper component field access
        for (const auto& stmt : handler.body) {
            out << rewrite_stmt(*stmt, 2, sys.filter.trait_names, program);
        }

        out << "    });\n";
        out << "}\n\n";
    }

    return out.str();
}

std::string EnttSystemEmitter::emit_extern_system(const ExternSystemNode& sys,
                                                  const DecoratedProgram& program) {
    (void)program;
    std::ostringstream out;

    out << "void " << sys.name << "_tick(entt::registry& registry) {\n";
    out << "    auto view = registry.view<";
    for (size_t i = 0; i < sys.filter.trait_names.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << sys.filter.trait_names[i];
    }
    out << ">();\n";

    if (!sys.order_by.empty()) {
        out << "    // order by:\n";
        for (const auto& key : sys.order_by) {
            out << "    //   " << key.alias << "." << key.field << (key.descending ? " desc" : " asc") << "\n";
        }
    }

    out << "    view.each([&](entt::entity entity";
    for (const auto& trait_name : sys.filter.trait_names) {
        out << ", auto& " << trait_name << "_comp";
    }
    out << ") {\n";
    out << "        " << sys.name << "_update(registry, entity";
    for (const auto& trait_name : sys.filter.trait_names) {
        out << ", " << trait_name << "_comp";
    }
    out << ");\n";
    out << "    });\n";
    out << "}\n\n";

    out << "void " << sys.name << "_update(entt::registry& registry, entt::entity entity";
    for (const auto& trait_name : sys.filter.trait_names) {
        out << ", " << trait_name << "& " << trait_name << "_comp";
    }
    out << ");\n\n";

    return out.str();
}

bool EnttSystemEmitter::requires_entt_hierarchy_helpers(const DecoratedProgram& program) {
    return program.traits.contains("Parent");
}

std::string EnttSystemEmitter::emit_entt_hierarchy_helpers(const DecoratedProgram& program) {
    if (!requires_entt_hierarchy_helpers(program)) {
        return "";
    }

    std::ostringstream out;
    out << "static void cactus_destroy_entity_recursive(entt::registry& registry, entt::entity entity) {\n";
    out << "    static std::unordered_set<entt::entity> __destroying;\n";
    out << "    if (!registry.valid(entity) || __destroying.contains(entity)) {\n";
    out << "        return;\n";
    out << "    }\n";
    out << "    __destroying.insert(entity);\n";
    out << "    std::vector<entt::entity> __children;\n";
    out << "    auto __parent_view = registry.view<Parent>();\n";
    out << "    __parent_view.each([&](entt::entity child, const Parent& rel) {\n";
    out << "        if (rel.parent == entity) {\n";
    out << "            __children.push_back(child);\n";
    out << "        }\n";
    out << "    });\n";
    out << "    for (auto child : __children) {\n";
    out << "        cactus_destroy_entity_recursive(registry, child);\n";
    out << "    }\n";
    out << "    if (registry.valid(entity)) {\n";
    out << "        registry.destroy(entity);\n";
    out << "    }\n";
    out << "    __destroying.erase(entity);\n";
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
