#include "backends/cpp-entt/type_utils.hpp"
#include "common/ast_expressions.hpp"
#include "common/codegen_error.hpp"

#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cactus {

namespace {

// dsl-vector-expressions "Color float component access": `.r/.g/.b/.a` are
// the only member names this language ever resolves against a color-typed
// expression (no other type has a field with any of these names, and
// semantic analysis has already validated the program by the time codegen
// runs) — the raw C++ field is a byte in [0,255] (raylib's Color), so every
// such access must normalize to a float in [0,1] the same way regardless of
// which emit_expr overload reaches it, mirroring rewrite_expr's handling of
// the same access on a trait field (system_emitter.cpp).

template <typename ResolvedDecl>
std::string resolved_decl_cpp_name(const ResolvedDecl& decl) {
    const auto kind = decl.symbol_id.has_value() ? decl.symbol_id->kind : SymbolKind::Trait;
    // Prefer module_name: allows tests to override canonical identity after analysis.
    if (decl.module_name.empty() && decl.symbol_id.has_value()) {
        return cpp_type_ref(*decl.symbol_id);
    }
    return cpp_type_ref(kind, decl.module_name, decl.name);
}

template <typename Map>
const Map::mapped_type* find_decl_by_symbol(const Map& map, const SymbolId& symbol) {
    const auto canonical = make_canonical_id(symbol);
    if (auto it = map.find(canonical); it != map.end()) {
        return &it->second;
    }
    if (auto it = map.find(symbol.local_name); it != map.end()) {
        const auto& decl = it->second;
        if ((decl.symbol_id.has_value() && *decl.symbol_id == symbol) || decl.canonical_id == canonical ||
            (!decl.symbol_id.has_value() && decl.module_name == symbol.module.name && decl.name == symbol.local_name)) {
            return &decl;
        }
    }
    for (const auto& [_, decl] : map) {
        if ((decl.symbol_id.has_value() && *decl.symbol_id == symbol) || decl.canonical_id == canonical ||
            (!decl.symbol_id.has_value() && decl.module_name == symbol.module.name && decl.name == symbol.local_name)) {
            return &decl;
        }
    }
    return nullptr;
}

template <typename Map>
std::string symbol_cpp_name_from_map(const SymbolId& symbol, const Map& map) {
    if (const auto* decl = find_decl_by_symbol(map, symbol); decl != nullptr) {
        return resolved_decl_cpp_name(*decl);
    }
    return cpp_type_ref(symbol);
}

template <typename Map>
std::string resolved_or_fallback_cpp_name(const std::optional<SymbolId>& symbol,
                                          const std::string& fallback_source_name,
                                          const Map& map) {
    if (symbol.has_value()) {
        return symbol_cpp_name_from_map(*symbol, map);
    }
    auto direct = map.find(fallback_source_name);
    if (direct != map.end()) {
        return resolved_decl_cpp_name(direct->second);
    }
    const auto dot = fallback_source_name.rfind('.');
    if (dot != std::string::npos) {
        for (const auto& [_, decl] : map) {
            if (decl.canonical_id == fallback_source_name) {
                return resolved_decl_cpp_name(decl);
            }
        }
        // Canonical id with no canonical match (single-module programs without
        // linker metadata): resolve by the simple suffix when it is unique.
        auto simple                             = fallback_source_name.substr(dot + 1U);
        const typename Map::mapped_type* unique = nullptr;
        for (const auto& [_, decl] : map) {
            if (decl.name != simple) {
                continue;
            }
            if (unique != nullptr) {
                unique = nullptr;
                break;
            }
            unique = &decl;
        }
        if (unique != nullptr) {
            return resolved_decl_cpp_name(*unique);
        }
        return simple;
    }
    // No dot — scan by local name for canonical-keyed maps (multi-module mode).
    for (const auto& [_, decl] : map) {
        if (decl.name == fallback_source_name) {
            return resolved_decl_cpp_name(decl);
        }
    }
    return fallback_source_name;
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

std::string namespace_block(const std::string& ns, const std::string& body) {
    return "namespace " + ns + " {\n" + body + "}  // namespace " + ns + "\n";
}

std::string detail_namespace_path(const std::string& module_name) {
    std::string result = "cactus_gen::detail";
    for (const auto& segment : cpp_namespace_segments(module_name)) {
        result += "::" + segment;
    }
    return result;
}

std::string join_strings(const std::vector<std::string>& items, std::string_view separator) {
    std::string joined;
    for (const auto& item : items) {
        if (!joined.empty()) {
            joined += separator;
        }
        joined += item;
    }
    return joined;
}

std::string component_local(const std::string& type_ref) {
    std::vector<std::string> segments;
    for (std::size_t begin = 0; begin <= type_ref.size();) {
        const auto end     = std::min(type_ref.find("::", begin), type_ref.size());
        auto segment       = type_ref.substr(begin, end - begin);
        const auto trimmed = segment.find_last_not_of('_');
        segment.erase(trimmed == std::string::npos ? 0 : trimmed + 1);
        if (!segment.empty() && segment != "cactus_gen") {
            segments.push_back(snake_case(segment));
        }
        begin = end + 2;
    }
    const auto first = segments.size() > 2 ? segments.end() - 2 : segments.begin();
    std::string local;
    for (auto it = first; it != segments.end(); ++it) {
        local += *it + "_";
    }
    return local + "comp";
}

DetailName detail_name(const std::string& module_name, const std::string& name) {
    return DetailName{.ns = detail_namespace_path(module_name), .name = name};
}

DetailName system_function_name(const std::string& module_name,
                                const std::string& system_name,
                                const std::string& suffix) {
    return detail_name(module_name, snake_case(system_name) + "_" + suffix);
}

DetailName archetype_create_function_name(const std::string& module_name, const std::string& archetype_name) {
    return detail_name(module_name, "create_" + snake_case(archetype_name));
}

DetailName archetype_create_at_function_name(const std::string& module_name, const std::string& archetype_name) {
    return detail_name(module_name, "create_" + snake_case(archetype_name) + "_at");
}

// Per-node helpers live in `<archetype>_nodes`: create_node for the root, create_node_<role path> for
// descendants. These are not registered in the editor template palette.
DetailName archetype_node_create_function_name(const std::string& module_name,
                                               const std::string& archetype_name,
                                               const std::vector<std::string>& role_path) {
    std::string name = "create_node";
    for (const auto& role : role_path) {
        name += "_" + snake_case(role);
    }
    return DetailName{.ns   = detail_namespace_path(module_name) + "::" + snake_case(archetype_name) + "_nodes",
                      .name = name};
}

DetailName range_check_function_name(const std::string& module_name, const std::string& trait_name) {
    return detail_name(module_name, "check_ranges_" + snake_case(trait_name));
}

DetailName range_value_function_name(const std::string& module_name,
                                     const std::string& archetype_name,
                                     const std::vector<std::string>& role_path,
                                     const std::string& trait_name) {
    auto name = archetype_node_create_function_name(module_name, archetype_name, role_path);
    name.name = "range_value_" + name.name.substr(std::string_view{"create_"}.size()) + "_" + snake_case(trait_name);
    return name;
}

DetailName archetype_range_precheck_function_name(const std::string& module_name, const std::string& archetype_name) {
    auto name = archetype_node_create_function_name(module_name, archetype_name, {});
    name.name = "passes_range_checks";
    return name;
}

DetailName archetype_node_create_at_function_name(const std::string& module_name,
                                                  const std::string& archetype_name,
                                                  const std::vector<std::string>& role_path) {
    auto name = archetype_node_create_function_name(module_name, archetype_name, role_path);
    name.name += "_at";
    return name;
}

std::string event_cpp_type_name(const SymbolId& event_id) {
    return cpp_type_ref(SymbolKind::Event, event_id.module.name, event_id.local_name);
}

std::string EnttCodegenUtils::value_type_to_cpp(const TypeInfo& type) {
    if (type.kind == TypeKind::EntityId) {
        return "entt::entity";
    }
    if (type.kind == TypeKind::List && type.element != nullptr) {
        return "std::vector<" + value_type_to_cpp(*type.element) + ">";
    }
    return type_to_cpp(type);
}

std::string EnttCodegenUtils::value_initializer(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::EntityId:
            return "{entt::null}";
        case TypeKind::String:
        case TypeKind::List:
            return "";
        default:
            return "{}";
    }
}

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
        case TypeKind::ModelId:
        case TypeKind::TextureId:
        case TypeKind::MaterialId:
            return "std::uint32_t";
        case TypeKind::InputButton:
        case TypeKind::InputAxis:
            return "std::uint8_t";
        case TypeKind::Struct:
        case TypeKind::Enum: {
            if (type.symbol_id.has_value()) {
                return cpp_type_ref(*type.symbol_id);
            }
            // Strip module-alias qualifier (e.g. "rand.Rng" → "Rng").
            const auto dot = type.name.rfind('.');
            return dot != std::string::npos ? type.name.substr(dot + 1U) : type.name;
        }
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
    const auto& module_name =
        e.module_name.empty() && e.symbol_id.has_value() ? e.symbol_id->module.name : e.module_name;
    const auto cpp_name = cpp_decl_name(SymbolKind::Enum, e.name);
    std::ostringstream out;
    if (e.variants.size() == 1) {
        out << "enum class " << cpp_name << " : std::uint8_t { " << cpp_escaped(e.variants.front()) << " };\n";
        return namespace_block(cpp_namespace_path(module_name), out.str());
    }
    out << "enum class " << cpp_name << " : std::uint8_t {\n";
    for (size_t i = 0; i < e.variants.size(); ++i) {
        out << "    " << cpp_escaped(e.variants[i]);
        if (i + 1 < e.variants.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "};\n";
    return namespace_block(cpp_namespace_path(module_name), out.str());
}

static std::string cpp_operator(const std::string& op) {
    if (op == "and") {
        return "&&";
    }
    if (op == "or") {
        return "||";
    }
    return op == "not" ? "!" : op;
}

static bool is_arithmetic_operator(std::string_view op) {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
}

static std::string total_int_helper(std::string_view op) {
    if (op == "+") {
        return "cactus::num::add";
    }
    if (op == "-") {
        return "cactus::num::sub";
    }
    if (op == "*") {
        return "cactus::num::mul";
    }
    if (op == "/") {
        return "cactus::num::div";
    }
    return "cactus::num::mod";
}

// Unchecked operands come from passes that typed no scope; they keep the int helpers.
static bool int_operands(ArithmeticOperands operands) {
    return operands == ArithmeticOperands::Int || operands == ArithmeticOperands::Unchecked;
}

static std::string as_float(const std::string& value) {
    return "static_cast<float>(" + value + ")";
}

std::string EnttCodegenUtils::emit_binary(const BinaryExpr& binary, const std::string& left, const std::string& right) {
    if (!is_arithmetic_operator(binary.op)) {
        return "(" + left + " " + cpp_operator(binary.op) + " " + right + ")";
    }
    if (int_operands(binary.operands)) {
        return total_int_helper(binary.op) + "(" + left + ", " + right + ")";
    }
    const auto& lhs = binary.operands == ArithmeticOperands::IntFloat ? as_float(left) : left;
    const auto& rhs = binary.operands == ArithmeticOperands::FloatInt ? as_float(right) : right;
    return "(" + lhs + " " + cpp_operator(binary.op) + " " + rhs + ")";
}

std::string EnttCodegenUtils::emit_unary(const UnaryExpr& unary, const std::string& operand) {
    // A negated literal is in range; INT_MIN is spelled so that C++ types it as `int`, not `long long`.
    const auto* literal = std::get_if<LiteralExpr>(&unary.operand->expr);
    if (unary.op == "-" && literal != nullptr && literal->kind == LiteralExpr::Kind::Int &&
        literal->value == "2147483648") {
        return "(-2147483647 - 1)";
    }
    if (unary.op == "-" && literal == nullptr && int_operands(unary.operands)) {
        return "cactus::num::neg(" + operand + ")";
    }
    return cpp_operator(unary.op) + operand;
}

std::string EnttCodegenUtils::emit_compound_assignment(const VarAssign& assign,
                                                       const std::string& target,
                                                       const std::string& value) {
    if (assign.op != "=" && int_operands(assign.operands)) {
        return target + " = " + total_int_helper(std::string_view(assign.op).substr(0, 1)) + "(" + target + ", " +
               value + ")";
    }
    if (assign.op != "=" && assign.operands == ArithmeticOperands::FloatInt) {
        return target + " " + assign.op + " " + as_float(value);
    }
    return target + " " + assign.op + " " + value;
}

// Exhaustive per-ExprNode-kind C++ text emission; self-recursive by
// construction (an expr tree emits its own subexpressions).
// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
std::string EnttCodegenUtils::emit_expr(const ExprNode& expr, const ProgramNode* ast) {
    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity) -- per-ExprNode-kind emission
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
                    return float_literal_cpp(e.value);
                }
                return e.value;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                if (e.template_slot.has_value()) {
                    return initializer_slot_name(*e.template_slot);
                }
                if (e.resolved_entity_id.has_value()) {
                    return named_slot_name(*e.resolved_entity_id);
                }
                if (e.resolved_const_id.has_value()) {
                    return symbol_cpp_name(*e.resolved_const_id);
                }
                if (is_input_action_name(ast, e.name)) {
                    return "::cactus_gen::detail::" + input_action_constant_name(e.name);
                }
                return e.name;
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return emit_binary(e, emit_expr(*e.left, ast), emit_expr(*e.right, ast));
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return emit_unary(e, emit_expr(*e.operand, ast));
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                // color(...) constructor (dsl-vector-expressions "Color
                // constructor"): route through the shared runtime
                // clamp-then-quantize helper rather than emitting a raw
                // `color(...)` call, so every color-constructing call site
                // funnels through one packing implementation (design.md
                // Decision 2; CLAUDE.md "Avoid duplication").
                const auto* callee_ident = std::get_if<IdentExpr>(&e.callee->expr);
                const bool is_color_ctor = callee_ident != nullptr && callee_ident->name == "color";
                std::string result       = (is_color_ctor ? std::string("color_from_components") : emit_expr(*e.callee, ast)) + "(";
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += emit_expr(*e.args[i], ast);
                }
                return result + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (e.resolved_entity_id.has_value()) {
                    return named_slot_name(*e.resolved_entity_id);
                }
                if (e.resolved_const_id.has_value()) {
                    return symbol_cpp_name(*e.resolved_const_id);
                }
                if (e.resolved_asset_id.has_value()) {
                    return e.resolved_asset_id->local_name;
                }
                if (e.resolved_phase_dt.has_value()) {
                    return phase_dt_cpp_name(*e.resolved_phase_dt);
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr); ident != nullptr && !ident->resolved_const_id) {
                    if (!ident->name.empty() && std::isupper(static_cast<unsigned char>(ident->name[0])) != 0) {
                        return ident->name + "::" + e.member;
                    }
                }
                if (EnttCodegenUtils::is_color_component_member(e.member)) {
                    return "(static_cast<float>(" + emit_expr(*e.object, ast) + "." + e.member + ") / 255.0F)";
                }
                return emit_expr(*e.object, ast) + "." + cpp_escaped(e.member);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                std::string result = "{";
                for (size_t i = 0; i < e.elements.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += emit_expr(*e.elements[i], ast);
                }
                return result + "}";
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                return emit_if_ternary(e, [ast](const ExprNode& child) { return emit_expr(child, ast); });
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                return emit_match_ternary(e, [ast](const ExprNode& child) { return emit_expr(child, ast); });
            } else {
                throw CodegenError(expr.location, "this expression form cannot be lowered to C++ here");
            }
        },
        expr.expr);
}

std::string EnttCodegenUtils::match_ternary_chain(const std::string& subject,
                                                  const std::vector<std::pair<std::string, std::string>>& tested_arms,
                                                  const std::string& last_value,
                                                  bool wrap_nested) {
    std::string chain = last_value;
    for (auto arm = tested_arms.rbegin(); arm != tested_arms.rend(); ++arm) {
        const bool nested = wrap_nested && arm != tested_arms.rbegin();
        if (nested) {
            chain.insert(0, "cactus_chosen(");
            chain += ")";
        }
        chain.insert(0, "((" + subject + " == " + arm->first + ") ? " + arm->second + " : ");
        chain += ")";
    }
    return chain;
}

namespace {

std::string conditional_operand(const ExprNode& operand, const std::function<std::string(const ExprNode&)>& emit) {
    auto text = emit(operand);
    if (std::holds_alternative<IfExpr>(operand.expr) || std::holds_alternative<MatchExpr>(operand.expr)) {
        return "cactus_chosen(" + text + ")";
    }
    return text;
}

}  // namespace

std::string EnttCodegenUtils::emit_if_ternary(const IfExpr& if_expr,
                                              const std::function<std::string(const ExprNode&)>& emit) {
    return "(" + conditional_operand(*if_expr.condition, emit) + " ? " + conditional_operand(*if_expr.then_expr, emit) +
           " : " + conditional_operand(*if_expr.else_expr, emit) + ")";
}

std::string EnttCodegenUtils::emit_match_ternary(const MatchExpr& match_expr,
                                                 const std::function<std::string(const ExprNode&)>& emit) {
    std::vector<std::pair<std::string, std::string>> tested_arms;
    for (std::size_t index = 0; index + 1 < match_expr.arms.size(); ++index) {
        const auto& arm = match_expr.arms[index];
        tested_arms.emplace_back(emit(*arm.pattern), conditional_operand(*arm.body, emit));
    }
    return match_ternary_chain(conditional_operand(*match_expr.subject, emit),
                               tested_arms,
                               conditional_operand(*match_expr.arms.back().body, emit),
                               true);
}

std::string EnttCodegenUtils::join_emitted_args(const std::vector<std::unique_ptr<ExprNode>>& args,
                                                const DecoratedProgram& program) {
    std::string result;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += EnttCodegenUtils::emit_expr(*args[i], program);
    }
    return result;
}

// DecoratedProgram overload: same exhaustive per-ExprNode-kind emission, plus
// stdlib call-site rewriting that needs the linked program's resolved symbols.
// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
std::string EnttCodegenUtils::emit_expr(const ExprNode& expr, const DecoratedProgram& program) {
    return std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity) -- per-ExprNode-kind emission
        [&](auto& e) -> std::string {
            using E = std::decay_t<decltype(e)>;
            const auto emit = [&](const ExprNode& child) { return EnttCodegenUtils::emit_expr(child, program); };
            if constexpr (std::is_same_v<E, BinaryExpr>) {
                return emit_binary(e, emit(*e.left), emit(*e.right));
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return emit_unary(e, emit(*e.operand));
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                return "{" + join_emitted_args(e.elements, program) + "}";
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                if (e.resolved_struct_id.has_value()) {
                    return emit_struct_construction(e, program, emit);
                }
                // color(...) constructor: see the ProgramNode overload above
                // for why this routes through color_from_components instead
                // of a raw `color(...)` call.
                if (const auto* color_ident = std::get_if<IdentExpr>(&e.callee->expr);
                    color_ident != nullptr && color_ident->name == "color") {
                    return "color_from_components(" + EnttCodegenUtils::join_emitted_args(e.args, program) + ")";
                }
                auto stdlib_prefix_for_module = [](const std::string& mod) -> std::string {
                    if (mod == "std.math") {
                        return "cactus::runtime::stdlib::math";
                    }
                    if (mod == "std.math.vec2") {
                        return "cactus::runtime::stdlib::math::vec2";
                    }
                    if (mod == "std.math.vec3") {
                        return "cactus::runtime::stdlib::math::vec3";
                    }
                    if (mod == "std.math.quat") {
                        return "cactus::runtime::stdlib::math::quat";
                    }
                    if (mod == "std.random") {
                        return "cactus::runtime::stdlib::random";
                    }
                    return {};
                };
                if (e.resolved_callee_id.has_value() && e.resolved_callee_id->kind == SymbolKind::Func) {
                    const auto prefix = stdlib_prefix_for_module(e.resolved_callee_id->module.name);
                    if (!prefix.empty()) {
                        return prefix + "::" + e.resolved_callee_id->local_name + "(" +
                               EnttCodegenUtils::join_emitted_args(e.args, program) + ")";
                    }
                    if (const auto* func = find_func(program, *e.resolved_callee_id); func != nullptr && !func->is_extern) {
                        return symbol_cpp_name(*e.resolved_callee_id) + "(" +
                               EnttCodegenUtils::join_emitted_args(e.args, program) + ")";
                    }
                }
                // Fallback: scan UseNode aliases when resolved_callee_id is not set
                if (program.ast != nullptr) {
                    if (const auto* member_callee = std::get_if<MemberExpr>(&e.callee->expr)) {
                        if (const auto* alias_ident = std::get_if<IdentExpr>(&member_callee->object->expr)) {
                            for (const auto& decl : program.ast->declarations) {
                                if (const auto* use_node = std::get_if<UseNode>(&decl)) {
                                    const bool alias_matches = use_node->alias.has_value()
                                                                   ? (*use_node->alias == alias_ident->name)
                                                                   : (use_node->module_name == alias_ident->name);
                                    if (alias_matches) {
                                        const auto prefix = stdlib_prefix_for_module(use_node->module_name);
                                        if (!prefix.empty()) {
                                            return prefix + "::" + member_callee->member + "(" +
                                                   EnttCodegenUtils::join_emitted_args(e.args, program) + ")";
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                return EnttCodegenUtils::emit_expr(*e.callee, program) + "(" +
                       EnttCodegenUtils::join_emitted_args(e.args, program) + ")";
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (auto named = named_member_cpp(e)) {
                    return *std::move(named);
                }
                if (e.resolved_const_id.has_value()) {
                    return const_reference_cpp(program, *e.resolved_const_id);
                }
                if (e.resolved_asset_id.has_value()) {
                    return e.resolved_asset_id->local_name;
                }
                if (e.resolved_phase_dt.has_value()) {
                    return phase_dt_cpp_name(*e.resolved_phase_dt);
                }
                // Three-level: alias.EnumType.Variant → canonical_EnumType::Variant
                if (const auto* inner_member = std::get_if<MemberExpr>(&e.object->expr)) {
                    if (EnttCodegenUtils::find_enum(program, inner_member->member) != nullptr) {
                        return EnttCodegenUtils::enum_cpp_name(inner_member->member, program) +
                               "::" + cpp_escaped(e.member);
                    }
                }
                // Two-level: EnumType.Variant → canonical_EnumType::Variant
                if (const auto* ident = std::get_if<IdentExpr>(&e.object->expr);
                    ident != nullptr && !ident->resolved_const_id.has_value()) {
                    if (!ident->name.empty() && std::isupper(static_cast<unsigned char>(ident->name[0])) != 0) {
                        return EnttCodegenUtils::enum_cpp_name(ident->name, program) + "::" + cpp_escaped(e.member);
                    }
                }
                if (EnttCodegenUtils::is_color_component_member(e.member)) {
                    return "(static_cast<float>(" + EnttCodegenUtils::emit_expr(*e.object, program) + "." + e.member +
                          ") / 255.0F)";
                }
                return EnttCodegenUtils::emit_expr(*e.object, program) + "." + cpp_escaped(e.member);
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                return emit_if_ternary(e, emit);
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                return emit_match_ternary(e, emit);
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                if (!e.template_slot.has_value() && !e.resolved_entity_id.has_value() && e.resolved_const_id.has_value()) {
                    return const_reference_cpp(program, *e.resolved_const_id);
                }
                return emit_expr(expr, program.ast);
            } else {
                return emit_expr(expr, program.ast);
            }
        },
        expr.expr);
}

std::string EnttCodegenUtils::symbol_cpp_name(const SymbolId& symbol) {
    return cpp_type_ref(symbol);
}

DetailName EnttCodegenUtils::phase_dt_name(const SymbolId& phase) {
    return detail_name(phase.module.name, phase.local_name + "_phase_dt");
}

std::string EnttCodegenUtils::phase_dt_cpp_name(const SymbolId& phase) {
    return phase_dt_name(phase).ref();
}

std::string EnttCodegenUtils::collider_shape_builder(std::optional<std::string_view> shape_trait) {
    std::string name = "cactus_collider_shape_of";
    if (shape_trait.has_value()) {
        name += "_";
        name += *shape_trait;
    }
    return name;
}

std::vector<std::string> EnttCodegenUtils::named_slot_path(const SymbolId& entity) {
    auto path = cpp_namespace_segments(entity.module.name);
    path.push_back(cpp_escaped(entity.local_name));
    return path;
}

std::string EnttCodegenUtils::named_slot_name(const SymbolId& entity) {
    std::string name = "::cactus_gen::detail::generated_named_slots()";
    for (const auto& segment : named_slot_path(entity)) {
        name += "." + segment;
    }
    return name;
}

std::string EnttCodegenUtils::named_trait_ref_name(const NamedTraitRef& ref) {
    return "named_" + snake_case(ref.entity.local_name) + "_" + snake_case(ref.trait.local_name);
}

bool EnttCodegenUtils::is_color_component_member(const std::string& member) {
    return member == "r" || member == "g" || member == "b" || member == "a";
}

std::optional<std::string> EnttCodegenUtils::named_member_cpp(const MemberExpr& member) {
    if (member.resolved_entity_id.has_value()) {
        return named_slot_name(*member.resolved_entity_id);
    }
    if (const auto path = trait_field_path(member); path.has_value()) {
        if (const auto named = path->first->named_ref(); named.has_value()) {
            auto lowered = named_trait_ref_name(*named);
            for (const auto& field : path->second) {
                lowered += "." + cpp_escaped(field);
            }
            return lowered;
        }
    }
    return std::nullopt;
}

std::vector<const EntityNode*> EnttCodegenUtils::declared_entities(const DecoratedProgram& program) {
    std::vector<const EntityNode*> entities;
    if (program.ast == nullptr) {
        return entities;
    }
    for (const auto& decl : program.ast->declarations) {
        if (const auto* entity = std::get_if<EntityNode>(&decl)) {
            entities.push_back(entity);
        }
    }
    return entities;
}

std::string EnttCodegenUtils::hierarchy_index_expr(const DecoratedProgram& program) {
    return "cactus::runtime::entt_backend::hierarchy_index<" + trait_cpp_name("Parent", program) + ">(registry)";
}

bool EnttCodegenUtils::checks_reparent(const DecoratedProgram& program) {
    return !program.hierarchy.derived || program.hierarchy.has_reparent;
}

std::string EnttCodegenUtils::hierarchy_strategy_cpp(const DecoratedProgram& program) {
    return std::string("cactus::runtime::entt_backend::HierarchyStrategy::") +
           (checks_reparent(program) ? "DepthOrder" : "CreationSweep");
}

std::string EnttCodegenUtils::trait_cpp_name(const SymbolId& symbol) {
    return cpp_type_ref(symbol);
}

std::string EnttCodegenUtils::struct_cpp_name(const SymbolId& symbol) {
    return cpp_type_ref(symbol);
}

std::string EnttCodegenUtils::enum_cpp_name(const SymbolId& symbol) {
    return cpp_type_ref(symbol);
}

std::string EnttCodegenUtils::relation_cpp_name(const SymbolId& symbol) {
    return cpp_type_ref(symbol);
}

std::string EnttCodegenUtils::relation_store_cpp(const SymbolId& symbol) {
    return "cactus::runtime::entt_backend::relation_store<" + relation_cpp_name(symbol) + ">(registry)";
}

std::string EnttCodegenUtils::uncompacted_relation_store_cpp(const SymbolId& symbol) {
    return "cactus::runtime::entt_backend::uncompacted_relation_store<" + relation_cpp_name(symbol) + ">(registry)";
}

std::string EnttCodegenUtils::trait_cpp_name(const std::optional<SymbolId>& symbol,
                                             const std::string& fallback_source_name,
                                             const DecoratedProgram& program) {
    return resolved_or_fallback_cpp_name(symbol, fallback_source_name, program.traits);
}

std::string EnttCodegenUtils::struct_cpp_name(const std::optional<SymbolId>& symbol,
                                              const std::string& fallback_source_name,
                                              const DecoratedProgram& program) {
    return resolved_or_fallback_cpp_name(symbol, fallback_source_name, program.structs);
}

std::string EnttCodegenUtils::enum_cpp_name(const std::optional<SymbolId>& symbol,
                                            const std::string& fallback_source_name,
                                            const DecoratedProgram& program) {
    return resolved_or_fallback_cpp_name(symbol, fallback_source_name, program.enums);
}

std::string EnttCodegenUtils::trait_cpp_name(const std::string& source_name, const DecoratedProgram& program) {
    return resolved_or_fallback_cpp_name(std::nullopt, source_name, program.traits);
}

std::optional<std::string> EnttCodegenUtils::lifecycle_occurrence_cpp_type(const ResolvedHandlerTrigger& trigger,
                                                                           const DecoratedProgram& program) {
    if (!trigger.is_lifecycle()) {
        return std::nullopt;
    }
    const auto* occurrence = trigger.kind == HandlerTriggerKind::TraitAdded ? "TraitAdded" : "TraitRemoved";
    return std::string("cactus::runtime::entt_backend::") + occurrence + "<" +
           trait_cpp_name(trigger.symbol, trigger.symbol.local_name, program) + ">";
}

std::vector<SymbolId> EnttCodegenUtils::watched_lifecycle_traits(const DecoratedProgram& program) {
    std::vector<SymbolId> traits;
    for (const auto& handler : program.execution_graph.handlers) {
        const auto& trigger = handler.identity.trigger;
        if (trigger.is_lifecycle() && !std::ranges::contains(traits, trigger.symbol)) {
            traits.push_back(trigger.symbol);
        }
    }
    std::ranges::sort(traits, {}, [](const SymbolId& trait) { return make_canonical_id(trait); });
    return traits;
}

bool EnttCodegenUtils::tracks_lifecycle(const DecoratedProgram& program) {
    return !program.execution_graph.phases.empty() && !watched_lifecycle_traits(program).empty();
}

std::string EnttCodegenUtils::struct_cpp_name(const std::string& source_name, const DecoratedProgram& program) {
    return resolved_or_fallback_cpp_name(std::nullopt, source_name, program.structs);
}

std::string EnttCodegenUtils::enum_cpp_name(const std::string& source_name, const DecoratedProgram& program) {
    return resolved_or_fallback_cpp_name(std::nullopt, source_name, program.enums);
}

const ResolvedTrait* EnttCodegenUtils::find_trait(const DecoratedProgram& program, const std::string& name) {
    auto it = program.traits.find(name);
    if (it != program.traits.end()) {
        return &it->second;
    }
    // Canonical-id request: exact canonical match wins; otherwise fall through
    // to the simple-name scan so metadata-less single-module programs resolve.
    std::string simple = name;
    if (const auto dot = name.rfind('.'); dot != std::string::npos) {
        for (const auto& [_, t] : program.traits) {
            if (t.canonical_id == name) {
                return &t;
            }
        }
        simple = name.substr(dot + 1U);
    }
    const ResolvedTrait* match = nullptr;
    for (const auto& [_, t] : program.traits) {
        if (t.name != simple) {
            continue;
        }
        if (match != nullptr && match->canonical_id != t.canonical_id) {
            // Picking either variant would depend on map iteration order; the
            // silent wrong pick is exactly what disabled the editor glue.
            throw std::runtime_error(
                "internal error: trait lookup '" + name + "' is ambiguous in the merged program (candidates include '" +
                (match->canonical_id.empty() ? match->name : match->canonical_id) + "' and '" +
                (t.canonical_id.empty() ? t.name : t.canonical_id) + "'); codegen must look it up by canonical id");
        }
        if (match == nullptr) {
            match = &t;
        }
    }
    return match;
}

const ResolvedState* EnttCodegenUtils::find_state(const DecoratedProgram& program, const SymbolId& state) {
    return find_decl_by_symbol(program.states, state);
}

std::string EnttCodegenUtils::state_slot_cpp_name(const DecoratedProgram& program, const SymbolId& state) {
    return symbol_cpp_name_from_map(state_slot_trait(state), program.traits);
}

std::optional<EnttCodegenUtils::StateVariant> EnttCodegenUtils::state_variant(const DecoratedProgram& program,
                                                                              const std::optional<SymbolId>& trait) {
    const auto state_id = trait.and_then(state_of_variant_trait);
    const auto* state   = state_id.has_value() ? find_state(program, *state_id) : nullptr;
    if (state == nullptr) {
        return std::nullopt;
    }
    const auto index = state->variant_index(variant_name_of_trait(*trait));
    if (!index.has_value()) {
        return std::nullopt;
    }
    return StateVariant{.slot_cpp = state_slot_cpp_name(program, *state_id), .index = *index};
}

std::string EnttCodegenUtils::enter_state_call(const StateVariant& variant,
                                               const std::string& entity_expr,
                                               const std::string& value) {
    return "cactus::runtime::entt_backend::enter_state<" + variant.slot_cpp + ", " + std::to_string(variant.index) +
           ">(registry, " + entity_expr + ", " + value + ");\n";
}

bool EnttCodegenUtils::has_trait(const DecoratedProgram& program, const std::string& name) {
    return find_trait(program, name) != nullptr;
}

const ResolvedEnum* EnttCodegenUtils::find_enum(const DecoratedProgram& program, const std::string& simple_name) {
    auto it = program.enums.find(simple_name);
    if (it != program.enums.end()) {
        return &it->second;
    }
    for (const auto& [_, e] : program.enums) {
        if (e.name == simple_name) {
            return &e;
        }
    }
    return nullptr;
}

const ResolvedStruct* EnttCodegenUtils::find_struct(const DecoratedProgram& program, const std::string& simple_name) {
    auto it = program.structs.find(simple_name);
    if (it != program.structs.end()) {
        return &it->second;
    }
    for (const auto& [_, s] : program.structs) {
        if (s.name == simple_name) {
            return &s;
        }
    }
    return nullptr;
}

const ResolvedStruct* EnttCodegenUtils::find_struct(const DecoratedProgram& program, const SymbolId& symbol) {
    if (const auto found = program.structs.find(make_canonical_id(symbol)); found != program.structs.end()) {
        return &found->second;
    }
    for (const auto& [_, structure] : program.structs) {
        if (structure.symbol_id == symbol ||
            (!structure.symbol_id.has_value() && structure.name == symbol.local_name)) {
            return &structure;
        }
    }
    return nullptr;
}

const ResolvedFunc* EnttCodegenUtils::find_func(const DecoratedProgram& program, const SymbolId& symbol) {
    const auto canonical = make_canonical_id(symbol);
    if (const auto found = program.funcs.find(canonical); found != program.funcs.end()) {
        return &found->second;
    }
    for (const auto& [_, func] : program.funcs) {
        if ((func.symbol_id.has_value() && *func.symbol_id == symbol) || func.canonical_id == canonical) {
            return &func;
        }
    }
    return nullptr;
}

const ResolvedConst* EnttCodegenUtils::find_const(const DecoratedProgram& program, const SymbolId& symbol) {
    for (const auto& [_, constant] : program.consts) {
        if (constant.symbol_id == symbol) {
            return &constant;
        }
    }
    return nullptr;
}

const ExprNode* EnttCodegenUtils::find_const_initializer(const ProgramNode& ast, const SymbolId& symbol) {
    for (const auto& decl : ast.declarations) {
        const auto* block = std::get_if<ConstBlockNode>(&decl);
        if (block == nullptr) {
            continue;
        }
        for (const auto& assignment : block->assignments) {
            if (assignment.resolved_const_id == symbol) {
                return assignment.value.get();
            }
        }
    }
    return nullptr;
}

namespace {

bool is_literal_type(const TypeInfo& type, const DecoratedProgram& program, bool in_struct) {
    switch (type.kind) {
        case TypeKind::Int:
        case TypeKind::Float:
        case TypeKind::Bool:
        case TypeKind::Vec2:
        case TypeKind::Vec3:
        case TypeKind::Quat:
        case TypeKind::Color:
        case TypeKind::Enum:
            return true;
        case TypeKind::String:
            return !in_struct;
        case TypeKind::Struct: {
            const auto* resolved =
                type.symbol_id.has_value() ? EnttCodegenUtils::find_struct(program, *type.symbol_id) : nullptr;
            return resolved != nullptr && std::ranges::all_of(resolved->fields, [&](const ResolvedField& field) {
                       return is_literal_type(field.type, program, true);
                   });
        }
        default:
            return false;
    }
}

bool is_constant_expression(const ExprNode& expr, const DecoratedProgram& program) {
    const auto constant = [&](const std::unique_ptr<ExprNode>& child) {
        return child != nullptr && is_constant_expression(*child, program);
    };
    return std::visit(
        [&](const auto& e) -> bool {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                return true;
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return e.resolved_const_id.has_value() && EnttCodegenUtils::is_constexpr_const(program, *e.resolved_const_id);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                if (e.resolved_const_id.has_value()) {
                    return EnttCodegenUtils::is_constexpr_const(program, *e.resolved_const_id);
                }
                return e.resolved_enum_member.has_value() || e.resolved_phase_dt.has_value();
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return constant(e.left) && constant(e.right);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return constant(e.operand);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                const auto* callee = std::get_if<IdentExpr>(&e.callee->expr);
                const bool constructor =
                    e.resolved_struct_id.has_value() ||
                    (callee != nullptr && !e.resolved_callee_id.has_value() &&
                     (callee->name == "vec2" || callee->name == "vec3" || callee->name == "color"));
                return constructor && std::ranges::all_of(e.args, constant);
            } else {
                return false;
            }
        },
        expr.expr);
}

}  // namespace

bool EnttCodegenUtils::is_constexpr_const(const DecoratedProgram& program, const SymbolId& symbol) {
    const auto* constant    = find_const(program, symbol);
    const auto* initializer = program.ast != nullptr ? find_const_initializer(*program.ast, symbol) : nullptr;
    if (constant == nullptr || initializer == nullptr || !is_literal_type(constant->type, program, false)) {
        return false;
    }
    if (constant->type.kind != TypeKind::String) {
        return is_constant_expression(*initializer, program);
    }
    // A view needs a literal it can point at for the whole run.
    if (const auto* literal = std::get_if<LiteralExpr>(&initializer->expr)) {
        return literal->kind == LiteralExpr::Kind::String;
    }
    const auto* alias = named_const(*initializer);
    return alias != nullptr && is_constexpr_const(program, *alias);
}

const SymbolId* EnttCodegenUtils::named_const(const ExprNode& expr) {
    const std::optional<SymbolId>* resolved = nullptr;
    if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
        resolved = &ident->resolved_const_id;
    } else if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        resolved = &member->resolved_const_id;
    }
    return resolved != nullptr && resolved->has_value() ? &**resolved : nullptr;
}

std::string EnttCodegenUtils::float_literal_cpp(const std::string& value) {
    struct NamedConstant {
        float value;
        std::string_view name;
    };
    static constexpr std::array<NamedConstant, 13> kNumbers = {{
        {.value = std::numbers::e_v<float>, .name = "e_v"},
        {.value = std::numbers::log2e_v<float>, .name = "log2e_v"},
        {.value = std::numbers::log10e_v<float>, .name = "log10e_v"},
        {.value = std::numbers::pi_v<float>, .name = "pi_v"},
        {.value = std::numbers::inv_pi_v<float>, .name = "inv_pi_v"},
        {.value = std::numbers::inv_sqrtpi_v<float>, .name = "inv_sqrtpi_v"},
        {.value = std::numbers::ln2_v<float>, .name = "ln2_v"},
        {.value = std::numbers::ln10_v<float>, .name = "ln10_v"},
        {.value = std::numbers::sqrt2_v<float>, .name = "sqrt2_v"},
        {.value = std::numbers::sqrt3_v<float>, .name = "sqrt3_v"},
        {.value = std::numbers::inv_sqrt3_v<float>, .name = "inv_sqrt3_v"},
        {.value = std::numbers::egamma_v<float>, .name = "egamma_v"},
        {.value = std::numbers::phi_v<float>, .name = "phi_v"},
    }};
    float parsed = 0.0F;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error == std::errc{} && end == value.data() + value.size()) {
        const auto bits  = std::bit_cast<std::uint32_t>(parsed);
        const auto match = std::ranges::find_if(
            kNumbers, [&](const NamedConstant& constant) { return std::bit_cast<std::uint32_t>(constant.value) == bits; });
        if (match != kNumbers.end()) {
            return "std::numbers::" + std::string(match->name) + "<float>";
        }
    }
    return value + "F";
}

bool EnttCodegenUtils::is_trivially_copyable(const TypeInfo& type, const DecoratedProgram& program) {
    if (type.kind == TypeKind::String || type.kind == TypeKind::List) {
        return false;
    }
    if (type.kind != TypeKind::Struct) {
        return true;
    }
    const auto* resolved = type.symbol_id.has_value() ? find_struct(program, *type.symbol_id) : nullptr;
    return resolved != nullptr && std::ranges::all_of(resolved->fields, [&](const ResolvedField& field) {
               return is_trivially_copyable(field.type, program);
           });
}

std::string EnttCodegenUtils::const_reference_cpp(const DecoratedProgram& program, const SymbolId& symbol) {
    const auto* constant = find_const(program, symbol);
    if (constant != nullptr && constant->type.kind == TypeKind::String && is_constexpr_const(program, symbol)) {
        return "std::string{" + symbol_cpp_name(symbol) + "}";
    }
    return symbol_cpp_name(symbol);
}

bool EnttCodegenUtils::has_effects(const ExprNode& expr, const DecoratedProgram& program) {
    bool effects = false;
    visit_expression(expr, [&](const ExprNode& node) {
        if (std::holds_alternative<QueryCallExpr>(node.expr) || std::holds_alternative<SpawnExpr>(node.expr)) {
            effects = true;
            return;
        }
        const auto* call = std::get_if<CallExpr>(&node.expr);
        if (call == nullptr || call->resolved_struct_id.has_value()) {
            return;
        }
        if (is_builtin_value_constructor(*call)) {
            return;
        }
        const auto* func = call->resolved_callee_id.has_value() ? find_func(program, *call->resolved_callee_id) : nullptr;
        effects = effects || func == nullptr || !func->effect_summary.has_value() || !func->effect_summary->empty();
    });
    return effects;
}

std::string EnttCodegenUtils::emit_struct_construction(
    const CallExpr& call,
    const DecoratedProgram& program,
    const std::function<std::string(const ExprNode&)>& emit_argument) {
    const auto& symbol    = *call.resolved_struct_id;
    const auto* structure = find_struct(program, symbol);
    if (structure == nullptr) {
        throw std::runtime_error("cpp-entt backend: missing struct '" + make_canonical_id(symbol) + "'");
    }
    std::vector<std::size_t> argument_of_field;
    for (const auto& field : structure->fields) {
        const auto named = std::ranges::find(call.arg_names, field.name);
        argument_of_field.push_back(static_cast<std::size_t>(named - call.arg_names.begin()));
    }
    const bool source_order = std::ranges::is_sorted(argument_of_field);
    const bool temporaries  = !source_order && std::ranges::any_of(call.args, [&](const auto& argument) {
        return has_effects(*argument, program);
    });

    std::string prefix;
    std::string initializer = struct_cpp_name(symbol) + "{";
    for (std::size_t index = 0; index < structure->fields.size(); ++index) {
        const auto argument = argument_of_field[index];
        initializer += (index > 0 ? ", ." : ".") + structure->fields[index].name + " = ";
        initializer += temporaries ? "cactus_gen_field_" + std::to_string(argument) : emit_argument(*call.args[argument]);
    }
    initializer += "}";
    if (!temporaries) {
        return initializer;
    }
    for (std::size_t index = 0; index < call.args.size(); ++index) {
        prefix += "auto cactus_gen_field_" + std::to_string(index) + " = " + emit_argument(*call.args[index]) + "; ";
    }
    return "[&] { " + prefix + "return " + initializer + "; }()";
}

// ── WorldTransform usage scan (D2) ──────────────────────────────────────────

namespace {

constexpr std::string_view kFlatTransformModule = "std.transform.flat";

void note_world_transform_ref(const SymbolId& id, WorldTransformUsage& usage) {
    if (id.local_name != "WorldTransform") {
        return;
    }
    if (id.module.name == kFlatTransformModule) {
        usage.flat = true;
    } else if (id.module.name == kTransformVolumeModule) {
        usage.volume = true;
    }
}

void note_world_transform_ref(const std::optional<SymbolId>& id, WorldTransformUsage& usage) {
    if (id.has_value()) {
        note_world_transform_ref(*id, usage);
    }
}

// The merged codegen AST contains every module's declarations; only the root
// module's references decide dimensionality. Declarations without a resolved
// id (single-module pipeline) are root by construction.
bool is_root_decl(const std::optional<SymbolId>& id, const std::string& root_module) {
    return !id.has_value() || root_module.empty() || id->module.name == root_module;
}

void scan_trait_entries(const std::vector<ArchetypeTraitEntry>& traits, WorldTransformUsage& usage) {
    for (const auto& entry : traits) {
        note_world_transform_ref(entry.resolved_trait_id, usage);
    }
}

void scan_child_overrides(const std::vector<ChildOverrideNode>& overrides, WorldTransformUsage& usage) {
    for (const auto& node : overrides) {
        scan_trait_entries(node.traits, usage);
        scan_child_overrides(node.children, usage);
    }
}

void scan_children(const std::vector<ChildArchetypeNode>& children, WorldTransformUsage& usage) {
    for (const auto& child : children) {
        scan_trait_entries(child.traits, usage);
        scan_children(child.children, usage);
        scan_child_overrides(child.child_overrides, usage);
    }
}

void scan_filter(const FilterClause& filter, WorldTransformUsage& usage) {
    for (const auto& entry : filter.entries) {
        note_world_transform_ref(entry.resolved_trait_id, usage);
    }
    for (const auto& id : filter.resolved_trait_ids) {
        note_world_transform_ref(id, usage);
    }
}

// NOLINTNEXTLINE(bugprone-branch-clone) -- rule/extern-rule arms differ by node type
void scan_root_declaration(const Declaration& decl, const std::string& root_module, WorldTransformUsage& usage) {
    if (const auto* entity = std::get_if<EntityNode>(&decl)) {
        if (is_root_decl(entity->resolved_entity_id, root_module)) {
            scan_trait_entries(entity->traits, usage);
            scan_children(entity->children, usage);
            scan_child_overrides(entity->child_overrides, usage);
        }
    } else if (const auto* tmpl = std::get_if<TemplateNode>(&decl)) {
        if (is_root_decl(tmpl->resolved_template_id, root_module)) {
            scan_trait_entries(tmpl->traits, usage);
            scan_children(tmpl->children, usage);
        }
    } else if (const auto* rule = std::get_if<RuleNode>(&decl)) {
        if (is_root_decl(rule->resolved_rule_id, root_module)) {
            scan_filter(rule->filter, usage);
            scan_filter(rule->exclude, usage);
        }
    } else if (const auto* ext = std::get_if<ExternRuleNode>(&decl)) {
        if (is_root_decl(ext->resolved_rule_id, root_module)) {
            scan_filter(ext->filter, usage);
            scan_filter(ext->exclude, usage);
        }
    }
}

// Fallback: when the merged program holds exactly one WorldTransform variant,
// derive dimensionality from its position field type. With both variants
// present and no root reference, neither rig is emitted.
WorldTransformUsage usage_from_unique_variant(const DecoratedProgram& program) {
    WorldTransformUsage usage;
    const ResolvedTrait* only = nullptr;
    for (const auto& [_, t] : program.traits) {
        if (t.name != "WorldTransform") {
            continue;
        }
        if (only != nullptr && only->canonical_id != t.canonical_id) {
            return usage;
        }
        only = &t;
    }
    if (only == nullptr) {
        return usage;
    }
    for (const auto& field : only->fields) {
        if (field.name == "position") {
            usage.flat   = field.type.kind == TypeKind::Vec2;
            usage.volume = field.type.kind == TypeKind::Vec3;
        }
    }
    return usage;
}

}  // namespace

WorldTransformUsage EnttCodegenUtils::world_transform_usage(const DecoratedProgram& program) {
    WorldTransformUsage usage;
    if (program.ast != nullptr) {
        for (const auto& decl : program.ast->declarations) {
            scan_root_declaration(decl, program.module_name, usage);
        }
    }
    if (usage.flat || usage.volume) {
        return usage;
    }
    return usage_from_unique_variant(program);
}

const ExprNode* EnttCodegenUtils::find_trait_field_default(const DecoratedProgram& program,
                                                           const std::string& module_name,
                                                           const std::string& trait_name,
                                                           const std::string& field_name) {
    if (program.ast == nullptr) {
        return nullptr;
    }
    std::string current_module;
    for (const auto& decl : program.ast->declarations) {
        if (const auto* mod = std::get_if<ModuleNode>(&decl)) {
            current_module = mod->name;
            continue;
        }
        const std::vector<FieldNode>* fields = nullptr;
        if (const auto* trait_node = std::get_if<TraitNode>(&decl); trait_node != nullptr && trait_node->name == trait_name) {
            fields = &trait_node->fields;
        } else if (const auto* relation_node = std::get_if<RelationNode>(&decl);
                   relation_node != nullptr && relation_node->name == trait_name) {
            fields = &relation_node->fields;
        }
        if (fields == nullptr || current_module != module_name) {
            continue;
        }
        for (const auto& field : *fields) {
            if (field.name == field_name) {
                return field.default_value.has_value() ? field.default_value->get() : nullptr;
            }
        }
    }
    return nullptr;
}

}  // namespace cactus
