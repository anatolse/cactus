#include "backends/cpp-entt/system_emitter.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

namespace {

std::string imported_module_name(const ProgramNode* ast, const std::string& qualifier) {
    if (ast == nullptr) {
        return qualifier;
    }
    for (const auto& decl : ast->declarations) {
        if (const auto* use = std::get_if<UseNode>(&decl)) {
            if ((use->alias.has_value() && *use->alias == qualifier) || use->module_name == qualifier) {
                return use->module_name;
            }
        }
    }
    return qualifier;
}

std::string stdlib_runtime_prefix(const ProgramNode* ast, const std::string& qualifier) {
    const std::string module_name = imported_module_name(ast, qualifier);
    if (module_name == "std.math") {
        return "cactus::runtime::stdlib::math";
    }
    if (module_name == "std.math.vec2") {
        return "cactus::runtime::stdlib::math::vec2";
    }
    if (module_name == "std.math.vec3") {
        return "cactus::runtime::stdlib::math::vec3";
    }
    if (module_name == "std.math.quat") {
        return "cactus::runtime::stdlib::math::quat";
    }
    if (module_name == "std.input") {
        return "cactus::runtime::entt_backend";
    }
    return {};
}

bool module_exports_stdlib_func(const std::string& module_name, const std::string& func_name) {
    if (module_name == "std.math") {
        return func_name == "lerp" || func_name == "clamp" || func_name == "abs" || func_name == "min" ||
               func_name == "max" || func_name == "sqrt" || func_name == "sin" || func_name == "cos" ||
               func_name == "atan2" || func_name == "floor" || func_name == "ceil" || func_name == "round" ||
               func_name == "pow";
    }
    if (module_name == "std.math.vec2") {
        return func_name == "length" || func_name == "normalize" || func_name == "dot" ||
               func_name == "lerp" || func_name == "distance" || func_name == "angle";
    }
    if (module_name == "std.math.vec3") {
        return func_name == "length" || func_name == "normalize" || func_name == "dot" ||
               func_name == "cross" || func_name == "lerp" || func_name == "distance" ||
               func_name == "reflect";
    }
    if (module_name == "std.math.quat") {
        return func_name == "identity" || func_name == "from_euler" || func_name == "from_axis_angle" ||
               func_name == "forward" || func_name == "right" || func_name == "up" || func_name == "rotate" ||
               func_name == "slerp" || func_name == "multiply" || func_name == "inverse";
    }
    if (module_name == "std.input") {
        return func_name == "pressed" || func_name == "down" || func_name == "released" ||
               func_name == "axis" || func_name == "axis2";
    }
    return false;
}

std::string lower_unqualified_stdlib_func(const DecoratedProgram& program,
                                          const std::string& func_name,
                                          const auto& emit_arg) {
    (void)emit_arg;
    if (program.ast == nullptr) {
        return {};
    }
    for (const auto& decl : program.ast->declarations) {
        const auto* use = std::get_if<UseNode>(&decl);
        if (use == nullptr || use->alias.has_value()) {
            continue;
        }
        if (!module_exports_stdlib_func(use->module_name, func_name)) {
            continue;
        }
        const std::string prefix = stdlib_runtime_prefix(program.ast, use->module_name);
        if (prefix.empty()) {
            continue;
        }
        return prefix + "::" + func_name;
    }
    return {};
}

std::string lower_stdlib_member_call(const MemberExpr& member,
                                     const std::vector<std::unique_ptr<ExprNode>>& args,
                                     const DecoratedProgram& program,
                                     const std::unordered_set<std::string>& pointer_aliases,
                                     const auto& emit_arg,
                                     const std::vector<std::string>& trait_names) {
    (void)pointer_aliases;
    (void)trait_names;
    const auto* object = std::get_if<IdentExpr>(&member.object->expr);
    if (object == nullptr) {
        return {};
    }
    const std::string prefix = stdlib_runtime_prefix(program.ast, object->name);
    if (prefix.empty()) {
        return {};
    }
    std::string result = prefix + "::" + member.member + "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += emit_arg(*args[i]);
    }
    result += ")";
    return result;
}

std::string snake_case(std::string value) {
    std::string result;
    for (const char ch : value) {
        if (std::isupper(static_cast<unsigned char>(ch)) != 0) {
            if (!result.empty() && result.back() != '_') {
                result += '_';
            }
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else {
            result += ch;
        }
    }
    return result;
}

std::string upper_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string system_function_name(const std::string& system_name, const std::string& suffix) {
    return snake_case(system_name) + "_" + suffix;
}

std::string event_cpp_type(const std::string& event_name) {
    if (event_name == "tick") {
        return "TickEvent";
    }
    if (event_name == "input") {
        return "InputEvent";
    }
    return event_name + "Event";
}

std::string input_action_constant_name(const std::string& input_name) {
    return "K_" + upper_copy(snake_case(input_name));
}

bool is_input_action_name(const DecoratedProgram& program, const std::string& name) {
    if (program.ast == nullptr) {
        return false;
    }
    for (const auto& decl : program.ast->declarations) {
        if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
            if (input->name == name) {
                return true;
            }
        }
    }
    return false;
}

std::string filter_simple_name(const FilterEntry& entry) {
    auto dot = entry.qualified_name.rfind('.');
    return (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
}

template <typename FilterLike>
bool filter_has_trait(const FilterLike& filter, const std::string& qualified, const std::string& simple) {
    for (const auto& entry : filter.entries) {
        if (entry.qualified_name == qualified || filter_simple_name(entry) == simple) {
            return true;
        }
    }
    return std::any_of(filter.trait_names.begin(), filter.trait_names.end(),
                       [&](const auto& name) { return name == simple; });
}

bool is_flat_transform_propagation(const ExternSystemNode& sys) {
    return filter_has_trait(sys.filter, "std.transform.flat.LocalTransform", "LocalTransform") &&
           filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
           !filter_has_trait(sys.filter, "std.transform.volume.LocalTransform", "__never_match__") &&
           !filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "__never_match__");
}

bool is_volume_transform_propagation(const ExternSystemNode& sys) {
    return filter_has_trait(sys.filter, "std.transform.volume.LocalTransform", "LocalTransform") &&
           filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
           !filter_has_trait(sys.filter, "std.transform.flat.LocalTransform", "__never_match__") &&
           !filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "__never_match__");
}

bool is_shape_renderer(const ExternSystemNode& sys) {
    return filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "Shape", "Shape");
}

bool is_sprite_renderer(const ExternSystemNode& sys) {
    return sys.name == "SpriteRenderer" &&
           filter_has_trait(sys.filter, "std.transform.flat.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "Renderer", "Renderer");
}

bool is_animated_sprite_system(const ExternSystemNode& sys) {
    return sys.name == "AnimatedSpriteSystem" && filter_has_trait(sys.filter, "AnimatedSprite", "AnimatedSprite");
}

bool is_mesh_renderer(const ExternSystemNode& sys) {
    return sys.name == "MeshRenderer" &&
           filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "Renderer", "Renderer");
}

bool is_billboard_renderer(const ExternSystemNode& sys) {
    return sys.name == "BillboardRenderer" &&
           filter_has_trait(sys.filter, "std.transform.volume.WorldTransform", "WorldTransform") &&
           filter_has_trait(sys.filter, "BillboardRenderer", "BillboardRenderer");
}

bool is_point_light_system(const ExternSystemNode& sys) {
    return sys.name == "PointLightSystem" && filter_has_trait(sys.filter, "PointLight", "PointLight");
}

bool is_directional_light_system(const ExternSystemNode& sys) {
    return sys.name == "DirectionalLightSystem" && filter_has_trait(sys.filter, "DirectionalLight", "DirectionalLight");
}

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
            } else if constexpr (std::is_same_v<E, SelfExpr>) {
                return "entity";
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                if (is_input_action_name(program, e.name)) {
                    return input_action_constant_name(e.name);
                }
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
                if (const auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
                    if (const auto lowered_name = lower_unqualified_stdlib_func(
                            program, ident->name,
                            [&](const ExprNode& arg) { return rewrite_expr(arg, trait_names, program, pointer_aliases); });
                        !lowered_name.empty()) {
                        std::string result = lowered_name + "(";
                        for (size_t i = 0; i < e.args.size(); ++i) {
                            if (i > 0) {
                                result += ", ";
                            }
                            result += rewrite_expr(*e.args[i], trait_names, program, pointer_aliases);
                        }
                        return result + ")";
                    }
                }
                if (const auto* member = std::get_if<MemberExpr>(&e.callee->expr)) {
                    if (const auto lowered = lower_stdlib_member_call(
                            *member, e.args, program, pointer_aliases,
                            [&](const ExprNode& arg) { return rewrite_expr(arg, trait_names, program, pointer_aliases); },
                            trait_names);
                        !lowered.empty()) {
                        return lowered;
                    }
                    if (const auto* object = std::get_if<IdentExpr>(&member->object->expr)) {
                        if (object->name == "input" && member->member == "axis") {
                            std::string result = "InputEvent::axis(";
                            for (size_t i = 0; i < e.args.size(); ++i) {
                                if (i > 0) {
                                    result += ", ";
                                }
                                result += rewrite_expr(*e.args[i], trait_names, program, pointer_aliases);
                            }
                            return result + ")";
                        }
                    }
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
                if (const auto* call = std::get_if<CallExpr>(&s.value->expr)) {
                    if (const auto* ident = std::get_if<IdentExpr>(&call->callee->expr);
                        ident != nullptr && ident->name == "vec2" && call->args.size() == 2) {
                        const std::string prefix = ind + lhs + " " + s.op + " vec2(";
                        const std::string continuation(prefix.size(), ' ');
                        return prefix + rewrite_expr(*call->args[0], trait_names, program, pointer_aliases) + ",\n" +
                               continuation + rewrite_expr(*call->args[1], trait_names, program, pointer_aliases) + ");\n";
                    }
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
                           ind + "    cactus_destroy_entity_recursive(registry, " + target + ");\n" +
                           ind + "}\n";
                }
                return ind + "cactus_destroy_entity_recursive(registry, entity);\n";
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
        out << "void " << system_function_name(sys.name, handler.event_name) << "(entt::registry& registry";
        out << ", const " << event_cpp_type(handler.event_name) << "& "
            << handler.alias.value_or(handler.event_name);
        out << ") {\n";
        out << "    (void)" << handler.alias.value_or(handler.event_name) << ";\n";

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
        out << "        (void)entity;\n";

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

    if (is_flat_transform_propagation(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    const auto HAS_LOCAL_WORLD = [&](entt::entity entity) {\n";
        out << "        return registry.all_of<LocalTransform, WorldTransform>(entity);\n";
        out << "    };\n";
        out << "    const auto GET_PARENT = [&](entt::entity entity) {\n";
        out << "        if (auto* parent = registry.try_get<Parent>(entity); parent != nullptr) {\n";
        out << "            return parent->parent;\n";
        out << "        }\n";
        out << "        return entt::entity{entt::null};\n";
        out << "    };\n";
        out << "    const auto COPY_LOCAL = [&](entt::entity entity) {\n";
        out << "        auto& local = registry.get<LocalTransform>(entity);\n";
        out << "        auto& world = registry.get<WorldTransform>(entity);\n";
        out << "        world.position = local.position;\n";
        out << "        world.rotation = local.rotation;\n";
        out << "        world.scale = local.scale;\n";
        out << "    };\n";
        out << "    const auto ACCUMULATE_FROM_PARENT = [&](entt::entity parent_entity, entt::entity entity) {\n";
        out << "        auto& local = registry.get<LocalTransform>(entity);\n";
        out << "        auto& world = registry.get<WorldTransform>(entity);\n";
        out << "        const auto& parent_world = registry.get<WorldTransform>(parent_entity);\n";
        out << "        world.position = Vector2{\n";
        out << "            .x = parent_world.position.x + local.position.x,\n";
        out << "            .y = parent_world.position.y + local.position.y,\n";
        out << "        };\n";
        out << "        world.rotation = parent_world.rotation + local.rotation;\n";
        out << "        world.scale = Vector2{\n";
        out << "            .x = parent_world.scale.x * local.scale.x,\n";
        out << "            .y = parent_world.scale.y * local.scale.y,\n";
        out << "        };\n";
        out << "    };\n";
        out << "    cactus::runtime::entt_backend::propagate_hierarchy(\n";
        out << "        registry, HAS_LOCAL_WORLD, GET_PARENT, COPY_LOCAL, ACCUMULATE_FROM_PARENT);\n";
        out << "}\n\n";

        return out.str();
    }

    if (is_volume_transform_propagation(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    const auto HAS_LOCAL_WORLD = [&](entt::entity entity) {\n";
        out << "        return registry.all_of<LocalTransform, WorldTransform>(entity);\n";
        out << "    };\n";
        out << "    const auto GET_PARENT = [&](entt::entity entity) {\n";
        out << "        if (auto* parent = registry.try_get<Parent>(entity); parent != nullptr) {\n";
        out << "            return parent->parent;\n";
        out << "        }\n";
        out << "        return entt::entity{entt::null};\n";
        out << "    };\n";
        out << "    const auto COPY_LOCAL = [&](entt::entity entity) {\n";
        out << "        auto& local = registry.get<LocalTransform>(entity);\n";
        out << "        auto& world = registry.get<WorldTransform>(entity);\n";
        out << "        world.position = local.position;\n";
        out << "        world.rotation = local.rotation;\n";
        out << "        world.scale = local.scale;\n";
        out << "    };\n";
        out << "    const auto ACCUMULATE_FROM_PARENT = [&](entt::entity parent_entity, entt::entity entity) {\n";
        out << "        auto& local = registry.get<LocalTransform>(entity);\n";
        out << "        auto& world = registry.get<WorldTransform>(entity);\n";
        out << "        const auto& parent_world = registry.get<WorldTransform>(parent_entity);\n";
        out << "        world.position = Vector3{\n";
        out << "            .x = parent_world.position.x + local.position.x,\n";
        out << "            .y = parent_world.position.y + local.position.y,\n";
        out << "            .z = parent_world.position.z + local.position.z,\n";
        out << "        };\n";
        out << "        world.rotation = cactus::runtime::stdlib::math::quat::multiply(parent_world.rotation, local.rotation);\n";
        out << "        world.scale = Vector3{\n";
        out << "            .x = parent_world.scale.x * local.scale.x,\n";
        out << "            .y = parent_world.scale.y * local.scale.y,\n";
        out << "            .z = parent_world.scale.z * local.scale.z,\n";
        out << "        };\n";
        out << "    };\n";
        out << "    cactus::runtime::entt_backend::propagate_hierarchy(\n";
        out << "        registry, HAS_LOCAL_WORLD, GET_PARENT, COPY_LOCAL, ACCUMULATE_FROM_PARENT);\n";
        out << "}\n\n";

        return out.str();
    }

    if (is_shape_renderer(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, Shape>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const Shape& Shape_comp) {\n";
        out << "        (void)entity;\n";
        out << "        if (!Shape_comp.visible) {\n";
        out << "            return;\n";
        out << "        }\n";
        out << "        switch (Shape_comp.type) {\n";
        out << "            case ShapeType::Rectangle:\n";
        out << "                DrawRectangle(static_cast<int>(WorldTransform_comp.position.x),\n";
        out << "                              static_cast<int>(WorldTransform_comp.position.y),\n";
        out << "                              static_cast<int>(Shape_comp.size.x),\n";
        out << "                              static_cast<int>(Shape_comp.size.y),\n";
        out << "                              Shape_comp.color);\n";
        out << "                break;\n";
        out << "        }\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_sprite_renderer(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, Renderer>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const Renderer& Renderer_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_sprite(WorldTransform_comp.position, Renderer_comp.size, Renderer_comp.color, Renderer_comp.texture, Renderer_comp.visible);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_animated_sprite_system(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<AnimatedSprite>();\n";
        out << "    view.each([&](entt::entity entity, AnimatedSprite& AnimatedSprite_comp) {\n";
        out << "        (void)entity;\n";
        out << "        constexpr float kFixedDt = 1.0F / 60.0F;\n";
        out << "        cactus::runtime::entt_backend::advance_animated_sprite(AnimatedSprite_comp.texture, AnimatedSprite_comp.frame, AnimatedSprite_comp.frame_count, AnimatedSprite_comp.fps, AnimatedSprite_comp.playing, kFixedDt);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_mesh_renderer(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, Renderer>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const Renderer& Renderer_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_mesh(WorldTransform_comp.position, WorldTransform_comp.rotation, WorldTransform_comp.scale, Renderer_comp.mesh, Renderer_comp.material, Renderer_comp.visible, Renderer_comp.cast_shadow);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_billboard_renderer(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, BillboardRenderer>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const BillboardRenderer& BillboardRenderer_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::submit_billboard(WorldTransform_comp.position, BillboardRenderer_comp.size, BillboardRenderer_comp.color, BillboardRenderer_comp.texture, BillboardRenderer_comp.visible);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_point_light_system(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<WorldTransform, PointLight>();\n";
        out << "    view.each([&](entt::entity entity, const WorldTransform& WorldTransform_comp, const PointLight& PointLight_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::register_point_light(WorldTransform_comp.position, PointLight_comp.color, PointLight_comp.intensity, PointLight_comp.range, PointLight_comp.enabled);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    if (is_directional_light_system(sys)) {
        out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
        out << "    auto view = registry.view<DirectionalLight>();\n";
        out << "    view.each([&](entt::entity entity, const DirectionalLight& DirectionalLight_comp) {\n";
        out << "        (void)entity;\n";
        out << "        cactus::runtime::entt_backend::register_directional_light(DirectionalLight_comp.direction, DirectionalLight_comp.color, DirectionalLight_comp.intensity, DirectionalLight_comp.enabled);\n";
        out << "    });\n";
        out << "}\n\n";
        return out.str();
    }

    out << "void " << system_function_name(sys.name, "tick") << "(entt::registry& registry) {\n";
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
    out << "        (void)entity;\n";
    out << "        " << system_function_name(sys.name, "update") << "(registry, entity";
    for (const auto& trait_name : sys.filter.trait_names) {
        out << ", " << trait_name << "_comp";
    }
    out << ");\n";
    out << "    });\n";
    out << "}\n\n";

    out << "void " << system_function_name(sys.name, "update") << "(entt::registry& registry, entt::entity entity";
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
    out << "[[maybe_unused]] static void cactus_destroy_entity_recursive(entt::registry& registry, entt::entity entity) {\n";
    out << "    cactus::runtime::entt_backend::destroy_entity_recursive(\n";
    out << "        registry, entity, [&](entt::entity parent, const auto& visitor) {\n";
    out << "            auto parent_view = registry.view<Parent>();\n";
    out << "            parent_view.each([&](entt::entity child, const Parent& rel) {\n";
    out << "                if (rel.parent == parent) {\n";
    out << "                    visitor(child);\n";
    out << "                }\n";
    out << "            });\n";
    out << "        });\n";
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
