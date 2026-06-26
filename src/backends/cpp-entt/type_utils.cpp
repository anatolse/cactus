#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
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
        return func_name == "length" || func_name == "normalize" || func_name == "dot" || func_name == "lerp" ||
               func_name == "distance" || func_name == "angle";
    }
    if (module_name == "std.math.vec3") {
        return func_name == "length" || func_name == "normalize" || func_name == "dot" || func_name == "cross" ||
               func_name == "lerp" || func_name == "distance" || func_name == "reflect";
    }
    if (module_name == "std.math.quat") {
        return func_name == "identity" || func_name == "from_euler" || func_name == "from_axis_angle" ||
               func_name == "forward" || func_name == "right" || func_name == "up" || func_name == "rotate" ||
               func_name == "slerp" || func_name == "multiply" || func_name == "inverse";
    }
    if (module_name == "std.input") {
        return func_name == "pressed" || func_name == "down" || func_name == "released" || func_name == "axis" ||
               func_name == "axis2" || func_name == "mouse_position";
    }
    return false;
}

std::string lower_unqualified_stdlib_func(const ProgramNode* ast,
                                          const std::string& func_name,
                                          const auto& emit_args,
                                          const std::vector<std::unique_ptr<ExprNode>>& args) {
    if (ast == nullptr) {
        return {};
    }
    if (func_name == "format") {
        for (const auto& decl : ast->declarations) {
            const auto* use = std::get_if<UseNode>(&decl);
            if (use != nullptr && !use->alias.has_value() && use->module_name == "std.text") {
                std::string result = "std::format(";
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += emit_args(*args[i]);
                }
                return result + ")";
            }
        }
    }
    for (const auto& decl : ast->declarations) {
        const auto* use = std::get_if<UseNode>(&decl);
        if (use == nullptr || use->alias.has_value()) {
            continue;
        }
        if (!module_exports_stdlib_func(use->module_name, func_name)) {
            continue;
        }
        const std::string prefix = stdlib_runtime_prefix(ast, use->module_name);
        if (prefix.empty()) {
            continue;
        }
        std::string result;
        result.reserve(prefix.size() + func_name.size() + 3U);
        result.append(prefix).append("::").append(func_name).push_back('(');
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            result += emit_args(*args[i]);
        }
        result += ")";
        return result;
    }
    return {};
}

std::string lower_stdlib_member_call(const MemberExpr& member,
                                     const std::vector<std::unique_ptr<ExprNode>>& args,
                                     const ProgramNode* ast,
                                     const auto& emit_arg) {
    const auto* object_ident = std::get_if<IdentExpr>(&member.object->expr);
    if (object_ident == nullptr) {
        return {};
    }
    if (member.member == "format" && imported_module_name(ast, object_ident->name) == "std.text") {
        std::string result = "std::format(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            result += emit_arg(*args[i]);
        }
        return result + ")";
    }
    if (object_ident->name == "input" && member.member == "mouse_position" && args.empty()) {
        return "cactus::runtime::entt_backend::mouse_position()";
    }
    const std::string prefix = stdlib_runtime_prefix(ast, object_ident->name);
    if (prefix.empty()) {
        return {};
    }

    std::string result;
    result.reserve(prefix.size() + member.member.size() + 3U);
    result.append(prefix).append("::").append(member.member).push_back('(');
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += emit_arg(*args[i]);
    }
    result += ")";
    return result;
}

std::string upper_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string snake_case(const std::string& value) {
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

std::string input_action_constant_name(const std::string& input_name) {
    return "K_" + upper_copy(snake_case(input_name));
}

bool is_input_action_name(const ProgramNode* ast, const std::string& name) {
    if (ast == nullptr) {
        return false;
    }
    for (const auto& decl : ast->declarations) {
        if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
            if (input->name == name) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::string EnttCodegenUtils::type_to_cpp(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Int:
            return "int";
        case TypeKind::Float:
            return "float";
        case TypeKind::Bool:
            return "bool";
        case TypeKind::String:
            return "std::string";
        case TypeKind::Vec2:
            return "Vector2";
        case TypeKind::Vec3:
            return "Vector3";
        case TypeKind::Quat:
            return "Quat";
        case TypeKind::Color:
            return "Color";
        case TypeKind::EntityId:
            return "uint32_t";
        case TypeKind::MeshId:
        case TypeKind::TextureId:
        case TypeKind::MaterialId:
            return "std::uint32_t";
        case TypeKind::InputButton:
        case TypeKind::InputAxis:
            return "std::uint8_t";
        case TypeKind::Struct:
        case TypeKind::Enum:
            return type.name;
        case TypeKind::List:
            if (type.element) {
                return "std::vector<" + type_to_cpp(*type.element) + ">";
            }
            return "std::vector<int>";
        case TypeKind::Void:
            return "void";
        default:
            return "/* unknown */";
    }
}

std::string EnttCodegenUtils::emit_enum(const ResolvedEnum& e) {
    std::ostringstream out;
    if (e.variants.size() == 1) {
        out << "enum class " << e.name << " : std::uint8_t { " << e.variants.front() << " };\n";
        return out.str();
    }
    out << "enum class " << e.name << " : std::uint8_t {\n";
    for (size_t i = 0; i < e.variants.size(); ++i) {
        out << "    " << e.variants[i];
        if (i + 1 < e.variants.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "};\n";
    return out.str();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
std::string EnttCodegenUtils::emit_expr(const ExprNode& expr, const ProgramNode* ast) {
    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [&](auto& e) -> std::string {
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
                        return "Color{.r = " + byte(0) + ", .g = " + byte(2) + ", .b = " + byte(4) +
                               ", .a = " + byte(6) + "}";
                    }
                }
                if (e.kind == LiteralExpr::Kind::Float) {
                    return e.value + "F";
                }
                return e.value;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                if (is_input_action_name(ast, e.name)) {
                    return input_action_constant_name(e.name);
                }
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                std::string op = e.op;
                if (op == "and") {
                    op = "&&";
                } else if (op == "or") {
                    op = "||";
                }
                return "(" + emit_expr(*e.left, ast) + " " + op + " " + emit_expr(*e.right, ast) + ")";
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                std::string op = e.op;
                if (op == "not") {
                    op = "!";
                }
                return op + emit_expr(*e.operand, ast);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                if (const auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
                    if (const auto lowered = lower_unqualified_stdlib_func(
                            ast, ident->name, [&](const ExprNode& arg) { return emit_expr(arg, ast); }, e.args);
                        !lowered.empty()) {
                        return lowered;
                    }
                }
                if (const auto* member = std::get_if<MemberExpr>(&e.callee->expr)) {
                    if (const auto lowered = lower_stdlib_member_call(
                            *member, e.args, ast, [&](const ExprNode& arg) { return emit_expr(arg, ast); });
                        !lowered.empty()) {
                        return lowered;
                    }
                }
                std::string result = emit_expr(*e.callee, ast) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += emit_expr(*e.args[i], ast);
                }
                return result + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (!ident->name.empty() && std::isupper(static_cast<unsigned char>(ident->name[0])) != 0) {
                        return ident->name + "::" + e.member;
                    }
                }
                return emit_expr(*e.object, ast) + "." + e.member;
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                return "/* spawn expr */";
            } else {
                return "/* unsupported expr */";
            }
        },
        expr.expr);
}

}  // namespace cactus