#include "backends/cpp-entt/type_utils.hpp"

#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
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
bool is_color_component_member(const std::string& member) {
    return member == "r" || member == "g" || member == "b" || member == "a";
}

template <typename ResolvedDecl>
std::string resolved_decl_cpp_name(const ResolvedDecl& decl) {
    // Prefer module_name: allows tests to override canonical identity after analysis.
    if (!decl.module_name.empty()) {
        return canonical_to_cpp_name(decl.module_name, decl.name);
    }
    if (decl.symbol_id.has_value()) {
        return canonical_to_cpp_name(*decl.symbol_id);
    }
    return decl.name;
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
    return canonical_to_cpp_name(symbol);
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

std::string system_function_name(const std::string& module_name,
                                 const std::string& system_name,
                                 const std::string& suffix) {
    return snake_case(canonical_to_cpp_name(module_name, system_name)) + "_" + suffix;
}

std::string system_function_name(const SymbolId& system_id, const std::string& suffix) {
    return snake_case(canonical_to_cpp_name(system_id)) + "_" + suffix;
}

std::string event_cpp_type_name(const SymbolId& event_id) {
    return canonical_to_cpp_name(event_id) + "Event";
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
                return canonical_to_cpp_name(*type.symbol_id);
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
    const std::string cpp_name = resolved_decl_cpp_name(e);
    std::ostringstream out;
    if (e.variants.size() == 1) {
        out << "enum class " << cpp_name << " : std::uint8_t { " << e.variants.front() << " };\n";
        return out.str();
    }
    out << "enum class " << cpp_name << " : std::uint8_t {\n";
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
                if (auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (!ident->name.empty() && std::isupper(static_cast<unsigned char>(ident->name[0])) != 0) {
                        return ident->name + "::" + e.member;
                    }
                }
                if (is_color_component_member(e.member)) {
                    return "(static_cast<float>(" + emit_expr(*e.object, ast) + "." + e.member + ") / 255.0F)";
                }
                return emit_expr(*e.object, ast) + "." + e.member;
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                std::string result = "{";
                for (size_t i = 0; i < e.elements.size(); ++i) {
                    if (i > 0) {
                        result += ", ";
                    }
                    result += emit_expr(*e.elements[i], ast);
                }
                return result + "}";
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                return "/* spawn expr */";
            } else {
                return "/* unsupported expr */";
            }
        },
        expr.expr);
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
            if constexpr (std::is_same_v<E, CallExpr>) {
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
                // Three-level: alias.EnumType.Variant → canonical_EnumType::Variant
                if (const auto* inner_member = std::get_if<MemberExpr>(&e.object->expr)) {
                    if (EnttCodegenUtils::find_enum(program, inner_member->member) != nullptr) {
                        return EnttCodegenUtils::enum_cpp_name(inner_member->member, program) + "::" + e.member;
                    }
                }
                // Two-level: EnumType.Variant → canonical_EnumType::Variant
                if (const auto* ident = std::get_if<IdentExpr>(&e.object->expr)) {
                    if (!ident->name.empty() && std::isupper(static_cast<unsigned char>(ident->name[0])) != 0) {
                        return EnttCodegenUtils::enum_cpp_name(ident->name, program) + "::" + e.member;
                    }
                }
                if (is_color_component_member(e.member)) {
                    return "(static_cast<float>(" + EnttCodegenUtils::emit_expr(*e.object, program) + "." + e.member +
                          ") / 255.0F)";
                }
                return EnttCodegenUtils::emit_expr(*e.object, program) + "." + e.member;
            } else {
                return emit_expr(expr, program.ast);
            }
        },
        expr.expr);
}

std::string EnttCodegenUtils::symbol_cpp_name(const SymbolId& symbol) {
    return canonical_to_cpp_name(symbol);
}

std::string EnttCodegenUtils::trait_cpp_name(const SymbolId& symbol) {
    return canonical_to_cpp_name(symbol);
}

std::string EnttCodegenUtils::struct_cpp_name(const SymbolId& symbol) {
    return canonical_to_cpp_name(symbol);
}

std::string EnttCodegenUtils::enum_cpp_name(const SymbolId& symbol) {
    return canonical_to_cpp_name(symbol);
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

// ── WorldTransform usage scan (D2) ──────────────────────────────────────────

namespace {

constexpr std::string_view kFlatTransformModule   = "std.transform.flat";
constexpr std::string_view kVolumeTransformModule = "std.transform.volume";

void note_world_transform_ref(const SymbolId& id, WorldTransformUsage& usage) {
    if (id.local_name != "WorldTransform") {
        return;
    }
    if (id.module.name == kFlatTransformModule) {
        usage.flat = true;
    } else if (id.module.name == kVolumeTransformModule) {
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

}  // namespace cactus