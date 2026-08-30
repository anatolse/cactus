#include "frontend/semantic_analyzer.hpp"

#include "common/render_pass_builtin_fields.hpp"
#include "common/render_pass_intrinsics.hpp"
#include "common/vector_binary_ops.hpp"
#include "frontend/execution_graph_scheduler.hpp"
#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <ranges>
#include <sstream>
#include <string_view>

namespace cactus {

namespace {

bool module_name_is_stdlib(const std::string& module_name) {
    return module_name == "std" || module_name.starts_with("std.");
}

template <typename ResolvedDecl>
void assign_canonical_identity(ResolvedDecl& decl, const SymbolId& symbol) {
    decl.module_name  = symbol.module.name;
    decl.canonical_id = make_canonical_id(symbol);
    decl.symbol_id    = symbol;
}

template <typename Decl>
void ensure_symbol_identity(Decl& decl,
                            SymbolKind kind,
                            const std::string& fallback_module,
                            const std::string& fallback_name) {
    const std::string module_name = decl.module_name.empty() ? fallback_module : decl.module_name;
    const std::string local_name  = decl.name.empty() ? fallback_name : decl.name;
    if (module_name.empty() || local_name.empty()) {
        return;
    }

    const SymbolId symbol = decl.symbol_id.value_or(make_symbol_id(kind, module_name, local_name));
    decl.name             = symbol.local_name;
    assign_canonical_identity(decl, symbol);
}

bool provider_is_std_core(const ModuleImports& imports, const std::string& qualifier) {
    const auto module_it = imports.modules.find(qualifier);
    return module_it != imports.modules.end() && module_it->second.module_name == "std.core";
}

std::optional<std::string> find_std_core_provider(const ModuleImports& imports,
                                                  const std::vector<std::string>& qualifiers) {
    for (const auto& qualifier : qualifiers) {
        if (provider_is_std_core(imports, qualifier)) {
            return qualifier;
        }
    }
    return std::nullopt;
}

std::string provider_module_name(const ModuleImports& imports, const std::string& qualifier) {
    const auto module_it = imports.modules.find(qualifier);
    return module_it != imports.modules.end() ? module_it->second.module_name : qualifier;
}

std::vector<std::string> unique_provider_modules(const ModuleImports& imports,
                                                 const std::vector<std::string>& qualifiers) {
    std::vector<std::string> modules;
    for (const auto& qualifier : qualifiers) {
        const auto module_name = provider_module_name(imports, qualifier);
        if (std::ranges::find(modules, module_name) == modules.end()) {
            modules.push_back(module_name);
        }
    }
    return modules;
}

std::string imported_reference_diagnostic(const ModuleImports& imports,
                                          const char* kind,
                                          const std::string& name,
                                          const std::vector<std::string>& qualifiers) {
    if (qualifiers.empty()) {
        return std::string("unknown ") + kind + " '" + name + "'";
    }

    const auto modules = unique_provider_modules(imports, qualifiers);
    if (modules.size() == 1 && qualifiers.size() == 1) {
        const auto& qualifier  = qualifiers.front();
        const auto module_name = provider_module_name(imports, qualifier);
        std::ostringstream msg;
        msg << kind << " '" << name << "' is imported from module '" << module_name << "' as canonical symbol '"
            << make_canonical_id(module_name, name) << "' and must be referenced as '" << qualifier << "." << name
            << "'";
        return msg.str();
    }

    std::ostringstream msg;
    msg << "ambiguous reference '" << name << "': defined as";
    for (std::size_t i = 0; i < modules.size(); ++i) {
        msg << (i == 0 ? " '" : (i + 1 == modules.size() ? " and '" : ", '")) << make_canonical_id(modules[i], name)
            << "'";
    }
    msg << "; use qualified access to disambiguate";
    return msg.str();
}

std::vector<std::string> type_provider_qualifiers(const ModuleImports& imports, const std::string& name) {
    std::vector<std::string> qualifiers;
    if (const auto it = imports.struct_providers.find(name); it != imports.struct_providers.end()) {
        qualifiers.insert(qualifiers.end(), it->second.begin(), it->second.end());
    }
    if (const auto it = imports.enum_providers.find(name); it != imports.enum_providers.end()) {
        qualifiers.insert(qualifiers.end(), it->second.begin(), it->second.end());
    }
    return qualifiers;
}

std::optional<std::string> explicit_module_name(const ProgramNode& program, ErrorReporter& errors) {
    const ModuleNode* found = nullptr;
    for (const auto& decl : program.declarations) {
        if (const auto* module = std::get_if<ModuleNode>(&decl)) {
            if (found != nullptr) {
                errors.error(module->location, "semantic analysis requires exactly one explicit module declaration");
                continue;
            }
            found = module;
        }
    }

    if (found == nullptr) {
        errors.error(program.location, "semantic analysis requires an explicit module declaration");
        return std::nullopt;
    }
    if (found->name.empty() || found->name == "<error>") {
        errors.error(found->location, "semantic analysis requires a non-empty explicit module declaration");
        return std::nullopt;
    }
    return found->name;
}

const std::unordered_set<std::string>& builtin_types() {
    static const std::unordered_set<std::string> TYPES = {"int",
                                                          "float",
                                                          "bool",
                                                          "string",
                                                          "vec2",
                                                          "vec3",
                                                          "quat",
                                                          "color",
                                                          "entity_id",
                                                          "void",
                                                          "list",
                                                          // Asset opaque ID types (spec 5.1 - dsl-spec-new-features)
                                                          "mesh_id",
                                                          "model_id",
                                                          "texture_id",
                                                          "sound_id",
                                                          "music_id",
                                                          "font_id",
                                                          "material_id",
                                                          // Input handle types (spec 5.1 - dsl-spec-new-features)
                                                          "InputButton",
                                                          "InputAxis"};
    return TYPES;
}

TypeKind type_kind_from_name(const std::string& name) {
    if (name == "int") {
        return TypeKind::Int;
    }
    if (name == "float") {
        return TypeKind::Float;
    }
    if (name == "bool") {
        return TypeKind::Bool;
    }
    if (name == "string") {
        return TypeKind::String;
    }
    if (name == "vec2") {
        return TypeKind::Vec2;
    }
    if (name == "vec3") {
        return TypeKind::Vec3;
    }
    if (name == "quat") {
        return TypeKind::Quat;
    }
    if (name == "color") {
        return TypeKind::Color;
    }
    if (name == "entity_id") {
        return TypeKind::EntityId;
    }
    if (name == "void") {
        return TypeKind::Void;
    }
    // Asset opaque ID types
    if (name == "mesh_id") {
        return TypeKind::MeshId;
    }
    if (name == "model_id") {
        return TypeKind::ModelId;
    }
    if (name == "texture_id") {
        return TypeKind::TextureId;
    }
    if (name == "sound_id") {
        return TypeKind::SoundId;
    }
    if (name == "music_id") {
        return TypeKind::MusicId;
    }
    if (name == "font_id") {
        return TypeKind::FontId;
    }
    if (name == "material_id") {
        return TypeKind::MaterialId;
    }
    // Input handle types
    if (name == "InputButton") {
        return TypeKind::InputButton;
    }
    if (name == "InputAxis") {
        return TypeKind::InputAxis;
    }
    return TypeKind::Unknown;
}

// Every row in lookup_vector_binary_op_result's table results in Vec2, Vec3, or Color.
TypeInfo vector_result_type_info(TypeKind kind) {
    if (kind == TypeKind::Vec2) {
        return make_vec2_type();
    }
    if (kind == TypeKind::Vec3) {
        return make_vec3_type();
    }
    return make_color_type();
}

/// Map AssetKind → TypeKind for the resulting opaque ID
TypeKind asset_kind_to_type_kind(AssetKind ak) {
    switch (ak) {
        case AssetKind::Mesh:
            return TypeKind::MeshId;
        case AssetKind::Model:
            return TypeKind::ModelId;
        case AssetKind::Texture:
            return TypeKind::TextureId;
        case AssetKind::Sound:
            return TypeKind::SoundId;
        case AssetKind::Music:
            return TypeKind::MusicId;
        case AssetKind::Font:
            return TypeKind::FontId;
        case AssetKind::Material:
            return TypeKind::MaterialId;
    }
    return TypeKind::Unknown;
}

template <typename FieldContainer>
TypeInfo find_field_type_in(const FieldContainer& fields, const std::string& member) {
    for (const auto& field : fields) {
        if (field.name == member) {
            return field.type;
        }
    }
    return make_unknown_type();
}

template <typename FieldContainer>
const ResolvedField* find_field_in(const FieldContainer& fields, const std::string& member) {
    const auto found = std::ranges::find_if(fields, [&member](const auto& field) { return field.name == member; });
    return found == fields.end() ? nullptr : &*found;
}

bool same_type(const TypeInfo& lhs, const TypeInfo& rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }
    if ((lhs.kind == TypeKind::Struct || lhs.kind == TypeKind::Enum) && lhs.symbol_id.has_value() &&
        rhs.symbol_id.has_value()) {
        return *lhs.symbol_id == *rhs.symbol_id;
    }
    if (lhs.kind == TypeKind::List && lhs.element != nullptr && rhs.element != nullptr) {
        return same_type(*lhs.element, *rhs.element);
    }
    return true;
}

struct NumericConstant {
    TypeKind kind = TypeKind::Unknown;
    long double value{};
};

std::optional<NumericConstant> evaluate_numeric_constant(
    const ExprNode& expr,
    const std::unordered_map<std::string, const ExprNode*>& constants,
    std::unordered_set<std::string>& evaluating) {
    if (const auto* literal = std::get_if<LiteralExpr>(&expr.expr)) {
        try {
            if (literal->kind == LiteralExpr::Kind::Int) {
                return NumericConstant{.kind = TypeKind::Int, .value = std::stold(literal->value)};
            }
            if (literal->kind == LiteralExpr::Kind::Float) {
                return NumericConstant{.kind = TypeKind::Float, .value = std::stold(literal->value)};
            }
        } catch (...) {
            return std::nullopt;
        }
        return std::nullopt;
    }
    if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
        const auto found = constants.find(ident->name);
        if (found == constants.end() || !evaluating.insert(ident->name).second) {
            return std::nullopt;
        }
        auto value = evaluate_numeric_constant(*found->second, constants, evaluating);
        evaluating.erase(ident->name);
        return value;
    }
    if (const auto* unary = std::get_if<UnaryExpr>(&expr.expr)) {
        auto operand = evaluate_numeric_constant(*unary->operand, constants, evaluating);
        if (!operand.has_value() || unary->op != "-") {
            return std::nullopt;
        }
        operand->value = -operand->value;
        return operand;
    }
    const auto* binary = std::get_if<BinaryExpr>(&expr.expr);
    if (binary == nullptr) {
        return std::nullopt;
    }
    auto left  = evaluate_numeric_constant(*binary->left, constants, evaluating);
    auto right = evaluate_numeric_constant(*binary->right, constants, evaluating);
    if (!left.has_value() || !right.has_value()) {
        return std::nullopt;
    }
    const auto result_kind =
        left->kind == TypeKind::Float || right->kind == TypeKind::Float ? TypeKind::Float : TypeKind::Int;
    if (binary->op == "+") {
        return NumericConstant{.kind = result_kind, .value = left->value + right->value};
    }
    if (binary->op == "-") {
        return NumericConstant{.kind = result_kind, .value = left->value - right->value};
    }
    if (binary->op == "*") {
        return NumericConstant{.kind = result_kind, .value = left->value * right->value};
    }
    if (binary->op == "/" && right->value != 0.0L) {
        const auto quotient = left->value / right->value;
        return NumericConstant{.kind  = result_kind,
                               .value = result_kind == TypeKind::Int ? std::trunc(quotient) : quotient};
    }
    if (binary->op == "%" && result_kind == TypeKind::Int && right->value != 0.0L) {
        return NumericConstant{.kind = TypeKind::Int, .value = std::fmod(left->value, right->value)};
    }
    return std::nullopt;
}

TypeInfo make_resolved_user_type(TypeKind kind, const SymbolId& symbol, std::string display_name = {}) {
    TypeInfo ti;
    ti.kind      = kind;
    ti.name      = display_name.empty() ? make_canonical_id(symbol) : std::move(display_name);
    ti.symbol_id = symbol;
    return ti;
}

template <typename ResolvedDecl>
SymbolId resolved_decl_symbol(const ResolvedDecl& decl,
                              SymbolKind kind,
                              const std::string& fallback_module,
                              const std::string& fallback_name) {
    if (decl.symbol_id.has_value()) {
        return *decl.symbol_id;
    }
    const std::string module_name = decl.module_name.empty() ? fallback_module : decl.module_name;
    const std::string local_name  = decl.name.empty() ? fallback_name : decl.name;
    return make_symbol_id(kind, module_name, local_name);
}

/// Collect the dotted identifier chain of a member expression, outermost
/// member last (e.g. `inp.Key.A` → ["inp", "Key", "A"]). Returns nullopt when
/// the chain head is not a plain identifier (runtime member access).
std::optional<std::vector<std::string>> member_chain_segments(const MemberExpr& member) {
    std::vector<std::string> segments;
    segments.push_back(member.member);
    const ExprNode* cursor = member.object.get();
    while (cursor != nullptr) {
        if (const auto* inner = std::get_if<MemberExpr>(&cursor->expr)) {
            segments.push_back(inner->member);
            cursor = inner->object.get();
            continue;
        }
        if (const auto* ident = std::get_if<IdentExpr>(&cursor->expr)) {
            segments.push_back(ident->name);
            std::ranges::reverse(segments);
            return segments;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

/// Split a dotted source reference ("phys.Body") into resolver segments.
std::vector<std::string> dotted_segments(const std::string& ref) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    for (auto dot = ref.find('.'); dot != std::string::npos; dot = ref.find('.', start)) {
        segments.push_back(ref.substr(start, dot - start));
        start = dot + 1;
    }
    segments.push_back(ref.substr(start));
    return segments;
}

/// Dotted identifier chain of a callee expression: a bare identifier or a
/// member chain with an identifier head. Nullopt for computed callees.
std::optional<std::vector<std::string>> callee_chain_segments(const ExprNode& callee) {
    if (const auto* ident = std::get_if<IdentExpr>(&callee.expr)) {
        return std::vector<std::string>{ident->name};
    }
    if (const auto* member = std::get_if<MemberExpr>(&callee.expr)) {
        return member_chain_segments(*member);
    }
    return std::nullopt;
}

bool expr_contains_self(const ExprNode& expr);
bool field_assignments_contain_self(const std::vector<FieldAssignment>& assignments);
std::unique_ptr<ExprNode> clone_expr(const ExprNode& expr);
FieldAssignment clone_field_assignment(const FieldAssignment& assignment);
ArchetypeTraitEntry clone_archetype_trait_entry(const ArchetypeTraitEntry& entry);
std::vector<ArchetypeTraitEntry> clone_archetype_trait_entries(const std::vector<ArchetypeTraitEntry>& entries);
ChildOverrideNode clone_child_override_node(const ChildOverrideNode& node);
std::vector<ChildOverrideNode> clone_child_override_nodes(const std::vector<ChildOverrideNode>& nodes);
ChildArchetypeNode clone_child_archetype_node(const ChildArchetypeNode& node);
std::vector<ChildArchetypeNode> clone_child_archetype_nodes(const std::vector<ChildArchetypeNode>& nodes);

template <typename ExprContainer>
bool any_expr_contains_self(const ExprContainer& expressions) {
    return std::ranges::any_of(expressions, [](const auto& expr) { return expr_contains_self(*expr); });
}

bool expr_contains_self(const ExprNode& expr) {
    return std::visit(
        [](const auto& e) -> bool {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, SelfExpr>) {
                return true;
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return expr_contains_self(*e.operand);
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return expr_contains_self(*e.left) || expr_contains_self(*e.right);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                return expr_contains_self(*e.callee) || any_expr_contains_self(e.args);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                return expr_contains_self(*e.object);
            } else if constexpr (std::is_same_v<E, LambdaExpr>) {
                return expr_contains_self(*e.body);
            } else if constexpr (std::is_same_v<E, PipelineExpr>) {
                return expr_contains_self(*e.source) || std::ranges::any_of(e.operations, [](const auto& op) {
                           return any_expr_contains_self(op.args);
                       });
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                return expr_contains_self(*e.subject) || std::ranges::any_of(e.arms, [](const auto& arm) {
                           return expr_contains_self(*arm.pattern) || expr_contains_self(*arm.body);
                       });
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                return expr_contains_self(*e.condition) || expr_contains_self(*e.then_expr) ||
                       expr_contains_self(*e.else_expr);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                return any_expr_contains_self(e.elements);
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                return std::ranges::any_of(
                    e.overrides, [](const auto& trait) { return field_assignments_contain_self(trait.assignments); });
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                return field_assignments_contain_self(e.named_args);
            } else {
                return false;
            }
        },
        expr.expr);
}

bool field_assignments_contain_self(const std::vector<FieldAssignment>& assignments) {
    return std::ranges::any_of(assignments, [](const auto& field) { return expr_contains_self(*field.value); });
}

std::unique_ptr<ExprNode> clone_expr(const ExprNode& expr) {
    return std::visit(
        [&expr](const auto& e) -> std::unique_ptr<ExprNode> {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr> || std::is_same_v<E, IdentExpr> ||
                          std::is_same_v<E, SelfExpr>) {
                return std::make_unique<ExprNode>(ExprNode::Variant{e}, expr.location);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                UnaryExpr copy{.op = e.op, .operand = clone_expr(*e.operand), .location = e.location};
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                BinaryExpr copy{
                    .op = e.op, .left = clone_expr(*e.left), .right = clone_expr(*e.right), .location = e.location};
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                CallExpr copy;
                copy.callee   = clone_expr(*e.callee);
                copy.location = e.location;
                copy.args.reserve(e.args.size());
                for (const auto& arg : e.args) {
                    copy.args.push_back(clone_expr(*arg));
                }
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                MemberExpr copy{.object               = clone_expr(*e.object),
                                .member               = e.member,
                                .resolved_enum_member = e.resolved_enum_member,
                                .location             = e.location};
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, LambdaExpr>) {
                LambdaExpr copy{.params = e.params, .body = clone_expr(*e.body), .location = e.location};
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, PipelineExpr>) {
                PipelineExpr copy;
                copy.source   = clone_expr(*e.source);
                copy.location = e.location;
                copy.operations.reserve(e.operations.size());
                for (const auto& op : e.operations) {
                    PipelineExpr::PipelineOp copied_op;
                    copied_op.method = op.method;
                    copied_op.args.reserve(op.args.size());
                    for (const auto& arg : op.args) {
                        copied_op.args.push_back(clone_expr(*arg));
                    }
                    copy.operations.push_back(std::move(copied_op));
                }
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                MatchExpr copy;
                copy.subject  = clone_expr(*e.subject);
                copy.location = e.location;
                copy.arms.reserve(e.arms.size());
                for (const auto& arm : e.arms) {
                    copy.arms.push_back(MatchArm{
                        .pattern = clone_expr(*arm.pattern), .body = clone_expr(*arm.body), .location = arm.location});
                }
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                IfExpr copy{.condition = clone_expr(*e.condition),
                            .then_expr = clone_expr(*e.then_expr),
                            .else_expr = clone_expr(*e.else_expr),
                            .location  = e.location};
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                ListExpr copy;
                copy.location = e.location;
                copy.elements.reserve(e.elements.size());
                for (const auto& element : e.elements) {
                    copy.elements.push_back(clone_expr(*element));
                }
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                SpawnExpr copy{.template_name   = e.template_name,
                               .overrides       = clone_archetype_trait_entries(e.overrides),
                               .child_overrides = clone_child_override_nodes(e.child_overrides),
                               .location        = e.location};
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                QueryCallExpr copy;
                copy.callee   = clone_expr(*e.callee);
                copy.filters  = e.filters;
                copy.location = e.location;
                copy.named_args.reserve(e.named_args.size());
                for (const auto& arg : e.named_args) {
                    copy.named_args.push_back(clone_field_assignment(arg));
                }
                return std::make_unique<ExprNode>(ExprNode::Variant{std::move(copy)}, expr.location);
            }
        },
        expr.expr);
}

FieldAssignment clone_field_assignment(const FieldAssignment& assignment) {
    return FieldAssignment{
        .name = assignment.name, .value = clone_expr(*assignment.value), .location = assignment.location};
}

ArchetypeTraitEntry clone_archetype_trait_entry(const ArchetypeTraitEntry& entry) {
    ArchetypeTraitEntry copy;
    copy.trait_name        = entry.trait_name;
    copy.resolved_trait_id = entry.resolved_trait_id;
    copy.location          = entry.location;
    copy.assignments.reserve(entry.assignments.size());
    for (const auto& assignment : entry.assignments) {
        copy.assignments.push_back(clone_field_assignment(assignment));
    }
    return copy;
}

std::vector<ArchetypeTraitEntry> clone_archetype_trait_entries(const std::vector<ArchetypeTraitEntry>& entries) {
    std::vector<ArchetypeTraitEntry> copy;
    copy.reserve(entries.size());
    for (const auto& entry : entries) {
        copy.push_back(clone_archetype_trait_entry(entry));
    }
    return copy;
}

ChildOverrideNode clone_child_override_node(const ChildOverrideNode& node) {
    ChildOverrideNode copy;
    copy.role     = node.role;
    copy.location = node.location;
    copy.traits   = clone_archetype_trait_entries(node.traits);
    copy.children = clone_child_override_nodes(node.children);
    return copy;
}

std::vector<ChildOverrideNode> clone_child_override_nodes(const std::vector<ChildOverrideNode>& nodes) {
    std::vector<ChildOverrideNode> copy;
    copy.reserve(nodes.size());
    for (const auto& node : nodes) {
        copy.push_back(clone_child_override_node(node));
    }
    return copy;
}

ChildArchetypeNode clone_child_archetype_node(const ChildArchetypeNode& node) {
    ChildArchetypeNode copy;
    copy.role            = node.role;
    copy.template_ref    = node.template_ref;
    copy.location        = node.location;
    copy.body_entries    = node.body_entries;
    copy.template_uses   = node.template_uses;
    copy.traits          = clone_archetype_trait_entries(node.traits);
    copy.children        = clone_child_archetype_nodes(node.children);
    copy.child_overrides = clone_child_override_nodes(node.child_overrides);
    return copy;
}

std::vector<ChildArchetypeNode> clone_child_archetype_nodes(const std::vector<ChildArchetypeNode>& nodes) {
    std::vector<ChildArchetypeNode> copy;
    copy.reserve(nodes.size());
    for (const auto& node : nodes) {
        copy.push_back(clone_child_archetype_node(node));
    }
    return copy;
}

// ── Hierarchical merge helpers (dsl-hierarchical-entity-templates) ──────────

// Merge one trait entry into a flattened trait list: new traits append, existing
// traits merge field-by-field with later assignments overriding earlier ones.
bool same_trait_entry_identity(const ArchetypeTraitEntry& lhs, const ArchetypeTraitEntry& rhs) {
    if (lhs.resolved_trait_id.has_value() && rhs.resolved_trait_id.has_value()) {
        return *lhs.resolved_trait_id == *rhs.resolved_trait_id;
    }
    return lhs.trait_name == rhs.trait_name;
}

void merge_trait_entry_into(std::vector<ArchetypeTraitEntry>& merged, const ArchetypeTraitEntry& entry) {
    auto existing = std::ranges::find_if(
        merged, [&entry](const auto& candidate) { return same_trait_entry_identity(candidate, entry); });
    if (existing == merged.end()) {
        merged.push_back(clone_archetype_trait_entry(entry));
        return;
    }

    for (const auto& assignment : entry.assignments) {
        auto field = std::ranges::find_if(
            existing->assignments, [&assignment](const auto& candidate) { return candidate.name == assignment.name; });
        if (field == existing->assignments.end()) {
            existing->assignments.push_back(clone_field_assignment(assignment));
        } else {
            *field = clone_field_assignment(assignment);
        }
    }
}

// Merge one flattened child node into a flattened child list, keyed by role.
// Same-role children merge traits field-by-field and children recursively.
void merge_child_archetype_into(std::vector<ChildArchetypeNode>& merged, const ChildArchetypeNode& child) {
    auto existing =
        std::ranges::find_if(merged, [&child](const auto& candidate) { return candidate.role == child.role; });
    if (existing == merged.end()) {
        merged.push_back(clone_child_archetype_node(child));
        return;
    }

    for (const auto& trait : child.traits) {
        merge_trait_entry_into(existing->traits, trait);
    }
    for (const auto& grandchild : child.children) {
        merge_child_archetype_into(existing->children, grandchild);
    }
}

// Apply a child override tree onto flattened children. Unknown roles are
// skipped here; validation reports them separately.
void apply_child_overrides_onto(std::vector<ChildArchetypeNode>& children,
                                const std::vector<ChildOverrideNode>& overrides) {
    for (const auto& override_node : overrides) {
        auto target = std::ranges::find_if(
            children, [&override_node](const auto& candidate) { return candidate.role == override_node.role; });
        if (target == children.end()) {
            continue;
        }
        for (const auto& trait : override_node.traits) {
            merge_trait_entry_into(target->traits, trait);
        }
        apply_child_overrides_onto(target->children, override_node.children);
    }
}

// ── std.text.format helpers ────────────────────────────────────────────────────

struct FormatStringAnalysis {
    int automatic_count  = 0;
    int manual_max_index = -1;
    bool has_automatic   = false;
    bool has_manual      = false;
};

bool analyze_format_string(const std::string& fmt, FormatStringAnalysis& result, std::string& error_msg) {
    size_t i = 0;
    while (i < fmt.size()) {
        const char c = fmt[i];
        if (c == '{') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') {
                i += 2;
                continue;
            }
            size_t j = i + 1;
            while (j < fmt.size() && fmt[j] != '}') {
                j++;
            }
            if (j >= fmt.size()) {
                error_msg = "malformed format string: unclosed '{'";
                return false;
            }
            const std::string content = fmt.substr(i + 1, j - i - 1);
            if (content.empty() || content[0] == ':') {
                result.has_automatic = true;
                result.automatic_count++;
            } else if (std::isdigit(static_cast<unsigned char>(content[0])) != 0) {
                result.has_manual      = true;
                const size_t colon_pos = content.find(':');
                const std::string idx_str =
                    content.substr(0, colon_pos == std::string::npos ? content.size() : colon_pos);
                try {
                    const int idx = std::stoi(idx_str);
                    if (idx < 0) {
                        error_msg = "malformed format string: negative placeholder index";
                        return false;
                    }
                    result.manual_max_index = std::max(result.manual_max_index, idx);
                } catch (...) {
                    error_msg = "malformed format string: invalid placeholder index";
                    return false;
                }
            } else {
                error_msg = "malformed format string: invalid replacement field content";
                return false;
            }
            if (result.has_automatic && result.has_manual) {
                error_msg = "format string mixes automatic and manual positional placeholders";
                return false;
            }
            i = j + 1;
        } else if (c == '}') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '}') {
                i += 2;
                continue;
            }
            error_msg = "malformed format string: unmatched '}'";
            return false;
        } else {
            i++;
        }
    }
    return true;
}

bool is_format_supported_type(TypeKind kind) {
    switch (kind) {
        case TypeKind::Int:
        case TypeKind::Float:
        case TypeKind::Bool:
        case TypeKind::String:
        case TypeKind::EntityId:
        case TypeKind::MeshId:
        case TypeKind::ModelId:
        case TypeKind::TextureId:
        case TypeKind::SoundId:
        case TypeKind::MusicId:
        case TypeKind::FontId:
        case TypeKind::MaterialId:
        case TypeKind::InputButton:
        case TypeKind::InputAxis:
            return true;
        default:
            return false;
    }
}

const std::vector<ChildOverrideNode> kNoChildOverrides;

std::optional<std::unordered_set<std::string>> known_stdlib_effect_summary(const SymbolId& symbol) {
    if (symbol.kind != SymbolKind::Func) {
        return std::nullopt;
    }

    const auto& module = symbol.module.name;
    if (module == "std.math" || module.starts_with("std.math.") || module == "std.random") {
        return std::unordered_set<std::string>{};
    }
    if (module == "std.input") {
        return std::unordered_set<std::string>{"input"};
    }
    if (module.starts_with("std.camera.") || module == "std.render" || module.starts_with("std.render.")) {
        return std::unordered_set<std::string>{"graphics"};
    }
    if (module == "std.editor") {
        return std::unordered_set<std::string>{"editor"};
    }
    if (module == "std.physics" || module.starts_with("std.physics.")) {
        return std::unordered_set<std::string>{"physics"};
    }
    if (module == "std.query" || module == "std.transform" || module.starts_with("std.transform.")) {
        return std::unordered_set<std::string>{"world"};
    }
    return std::nullopt;
}

}  // namespace

// ── ModuleImports::add ──────────────────────────────────────────────────────

void ModuleImports::add(const std::string& qualifier,
                        ImportedSymbols pub_syms,
                        std::unordered_set<std::string> non_pub,
                        std::unordered_set<std::string> non_pub_templates) {
    for (auto& [name, trait] : pub_syms.traits) {
        ensure_symbol_identity(trait, SymbolKind::Trait, pub_syms.module_name, name);
    }
    for (auto& [name, strct] : pub_syms.structs) {
        ensure_symbol_identity(strct, SymbolKind::Struct, pub_syms.module_name, name);
    }
    for (auto& [name, enm] : pub_syms.enums) {
        ensure_symbol_identity(enm, SymbolKind::Enum, pub_syms.module_name, name);
    }
    for (auto& [name, func] : pub_syms.funcs) {
        ensure_symbol_identity(func, SymbolKind::Func, pub_syms.module_name, name);
        if (func.is_extern && !func.effect_summary.has_value()) {
            func.effect_summary = known_stdlib_effect_summary(*func.symbol_id);
        }
    }
    for (auto& [name, tmpl] : pub_syms.templates) {
        const auto symbol = tmpl.symbol_id.value_or(make_symbol_id(SymbolKind::Template, pub_syms.module_name, name));
        tmpl.name         = symbol.local_name;
        tmpl.module_name  = symbol.module.name;
        tmpl.canonical_id = make_canonical_id(symbol);
        tmpl.symbol_id    = symbol;
    }
    for (const auto& name : pub_syms.events) {
        if (!pub_syms.event_symbols.contains(name)) {
            const auto symbol            = make_symbol_id(SymbolKind::Event, pub_syms.module_name, name);
            pub_syms.event_symbols[name] = ImportedEvent{.name         = symbol.local_name,
                                                         .module_name  = symbol.module.name,
                                                         .canonical_id = make_canonical_id(symbol),
                                                         .symbol_id    = symbol};
        }
    }
    for (auto& [name, event] : pub_syms.event_symbols) {
        const auto symbol  = event.symbol_id.value_or(make_symbol_id(SymbolKind::Event, pub_syms.module_name, name));
        event.name         = symbol.local_name;
        event.module_name  = symbol.module.name;
        event.canonical_id = make_canonical_id(symbol);
        event.symbol_id    = symbol;
        pub_syms.events.insert(symbol.local_name);
    }
    for (auto& [name, phase] : pub_syms.phase_symbols) {
        const auto symbol  = phase.symbol_id.value_or(make_symbol_id(SymbolKind::Phase, pub_syms.module_name, name));
        phase.name         = symbol.local_name;
        phase.module_name  = symbol.module.name;
        phase.canonical_id = make_canonical_id(symbol);
        phase.symbol_id    = symbol;
    }
    for (auto& [name, rule] : pub_syms.rules) {
        const auto symbol = rule.symbol_id.value_or(make_symbol_id(SymbolKind::Rule, pub_syms.module_name, name));
        rule.name         = symbol.local_name;
        rule.module_name  = symbol.module.name;
        rule.canonical_id = make_canonical_id(symbol);
        rule.symbol_id    = symbol;
    }

    const auto existing = modules.find(qualifier);
    if (existing != modules.end() && existing->second.module_name == pub_syms.module_name) {
        // Idempotent import of the same module source under the same qualifier.
        // This happens for std.core because lifecycle/core symbols are preloaded
        // while authors may still write `use std.core` explicitly.
        modules[qualifier] = std::move(pub_syms);
        if (!non_pub.empty()) {
            non_pub_trait_names[qualifier] = std::move(non_pub);
        }
        if (!non_pub_templates.empty()) {
            non_pub_template_names[qualifier] = std::move(non_pub_templates);
        }
        return;
    }

    // Build global uniqueness providers index
    for (auto& [name, _] : pub_syms.traits) {
        trait_providers[name].push_back(qualifier);
    }
    for (auto& [name, _] : pub_syms.structs) {
        struct_providers[name].push_back(qualifier);
    }
    for (auto& [name, _] : pub_syms.enums) {
        enum_providers[name].push_back(qualifier);
    }
    // Task 4.8: Index func providers for qualified func resolution
    for (auto& [name, _] : pub_syms.funcs) {
        func_providers[name].push_back(qualifier);
    }
    for (const auto& [name, _] : pub_syms.templates) {
        template_providers[name].push_back(qualifier);
    }
    // Index imported event names
    for (const auto& [ev_name, _] : pub_syms.event_symbols) {
        event_providers[ev_name].push_back(qualifier);
    }
    for (const auto& [phase_name, _] : pub_syms.phase_symbols) {
        phase_providers[phase_name].push_back(qualifier);
    }
    // Index imported extern rule names (for after: resolution)
    for (const auto& [rule_name, _] : pub_syms.rules) {
        rule_providers[rule_name].push_back(qualifier);
    }
    // Store non-pub trait names for error diagnostics
    if (!non_pub.empty()) {
        non_pub_trait_names[qualifier] = std::move(non_pub);
    }
    if (!non_pub_templates.empty()) {
        non_pub_template_names[qualifier] = std::move(non_pub_templates);
    }
    // Store the module
    modules[qualifier] = std::move(pub_syms);
}

// ── SemanticAnalyzer ────────────────────────────────────────────────────────

SemanticAnalyzer::SemanticAnalyzer(ErrorReporter& errors)
    : errors_(errors) {}

DecoratedProgram SemanticAnalyzer::analyze(ProgramNode& program, const ModuleImports& imports) {
    imports_ = imports;

    result_     = DecoratedProgram{};
    result_.ast = &program;

    current_module_is_stdlib_ = false;
    current_module_name_.clear();
    current_module_id_ = ModuleId{};
    struct_names_.clear();
    enum_names_.clear();
    trait_names_.clear();
    event_names_.clear();
    phase_names_.clear();
    func_names_.clear();
    rule_names_.clear();
    const_names_.clear();
    module_scope_symbols_.clear();
    asset_decl_types_.clear();
    input_decl_types_.clear();
    call_graph_.clear();
    template_names_.clear();
    entity_names_.clear();
    use_names_.clear();
    archetype_traits_.clear();
    archetype_children_.clear();
    template_required_fields_.clear();
    event_structs_.clear();

    auto module_name = explicit_module_name(program, errors_);
    if (!module_name.has_value()) {
        return std::move(result_);
    }

    current_module_id_        = ModuleId{.name = *module_name};
    current_module_name_      = current_module_id_.name;
    current_module_is_stdlib_ = module_name_is_stdlib(current_module_id_.name);
    result_.module_name       = current_module_id_.name;
    result_.source_modules.push_back(ResolvedSourceModule{.module_name = current_module_id_.name, .ast = &program});

    // Phase 1: Collect all type declarations
    collect_types(program);

    // dsl-render-passes: recognize render-pass phases by descriptor field
    // type now, right after field types are resolved (collect_types) and
    // before resolve_all_types resolves ordinary handler triggers — a
    // render-pass phase's derived `<phase>.vertex`/`<phase>.fragment`
    // triggers must already be recognizable by the time rule handlers below
    // resolve their own `on ...:` trigger.
    recognize_render_pass_phases(program);

    // dsl-where-clause: lower where: into leading handler-body guards before
    // any pass resolves or inspects handler body shape (see
    // desugar_where_clauses's doc comment).
    desugar_where_clauses(program);

    // Phase 2: Resolve types in fields
    resolve_all_types(program);
    resolve_trait_references(program);

    // Phase 3: Semantic checks
    check_const_strings(program);
    check_func_purity(program);
    check_no_recursion(program);
    check_persist_sync(program);
    validate_rule_filters(program);
    validate_where_clauses(program);
    validate_phase_declarations(program);
    // dsl-render-passes: descriptor-field *value* validation needs
    // resolved_enum_member, populated by resolve_all_types above, so it runs
    // as a separate step after validate_phase_declarations rather than
    // inside validate_phase_field_initializers. Stage-handler cardinality and
    // shape/body validation need every rule's handler trigger already
    // resolved, which is also true by this point.
    validate_render_pass_descriptor_fields(program);
    validate_render_pass_stage_handlers(program);
    validate_external_handler_contracts(program);
    validate_event_usage(program);
    validate_text_format_calls(program);

    // Phase 3: Dynamic ECS checks (dynamic-ecs-language change)
    validate_template_unit_declarations(program);
    validate_template_use_cycles(program);
    flatten_template_compositions(program);
    validate_template_backed_entity_overrides(program);
    validate_hierarchical_entities(program);
    validate_spawn_sites(program);
    validate_stmt_contexts(program);
    validate_trait_default_values(program);
    validate_trait_modifier_rules(program);

    // Phase 4: Build dependency graph
    build_dependency_graph(program);

    // Phase 5: Validate after: clauses and cycle detection
    validate_after_clauses(program);

    return std::move(result_);
}

// ── Phase 1: Collect Types ──────────────────────────────────────────────────

bool SemanticAnalyzer::declare_module_scope_symbol(SymbolKind kind,
                                                   const std::string& name,
                                                   const SourceLocation& loc) {
    const auto symbol         = make_symbol_id(kind, current_module_id_, name);
    auto [existing, inserted] = module_scope_symbols_.emplace(name, symbol);
    if (inserted) {
        return true;
    }

    errors_.error(loc,
                  "duplicate module-scope declaration '" + name + "' in module '" + current_module_name_ +
                      "': " + symbol_kind_name(kind) + " conflicts with existing " +
                      symbol_kind_name(existing->second.kind) + " declaration");
    return false;
}

// Two-pass namespace population: import seeding, then the per-declaration-kind
// symbol-table registration below.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::collect_types(ProgramNode& program) {
    // Seed event_names_ and event_structs_ from all imported modules.
    // std.core prelude events are always accessible unqualified; events from
    // other modules are also allowed unqualified because the `on event:` handler
    // syntax does not yet support qualified module prefixes.
    // event_structs_ stubs are required so handler_event is non-null for
    // marker/cross-module events, enabling world-access calls like exists() inside handlers.
    for (const auto& [qualifier, syms] : imports_.modules) {
        for (const auto& ev_name : syms.events) {
            event_names_.insert(ev_name);
            if (!event_structs_.contains(ev_name)) {
                const auto event_id = syms.event_symbols.contains(ev_name) && syms.event_symbols.at(ev_name).symbol_id
                                          ? *syms.event_symbols.at(ev_name).symbol_id
                                          : make_symbol_id(SymbolKind::Event, syms.module_name, ev_name);
                ResolvedStruct event_stub;
                event_stub.name = event_id.local_name;
                if (const auto imported_event = syms.event_symbols.find(ev_name);
                    imported_event != syms.event_symbols.end()) {
                    event_stub.fields = imported_event->second.fields;
                }
                assign_canonical_identity(event_stub, event_id);
                event_structs_[ev_name] = std::move(event_stub);
            }
        }
        for (const auto& [phase_name, _] : syms.phase_symbols) {
            phase_names_.insert(phase_name);
        }
    }

    // First pass: populate the complete module-scope namespace and all local
    // declaration-name sets before resolving any reference sites. This ensures
    // forward references in event fields and later semantic phases see the same
    // typed local symbol table.
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {  // NOLINT(readability-function-cognitive-complexity) -- exhaustive
                                  // per-declaration-kind symbol-table registration
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, StructNode>) {
                    declare_module_scope_symbol(SymbolKind::Struct, node.name, node.location);
                    struct_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, EnumNode>) {
                    declare_module_scope_symbol(SymbolKind::Enum, node.name, node.location);
                    enum_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, TraitNode>) {
                    node.is_stdlib = current_module_is_stdlib_;
                    declare_module_scope_symbol(SymbolKind::Trait, node.name, node.location);
                    trait_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, EventNode>) {
                    declare_module_scope_symbol(SymbolKind::Event, node.name, node.location);
                    event_names_.insert(node.name);
                    if (node.is_pub) {
                        result_.pub_events.insert(node.name);
                    }
                } else if constexpr (std::is_same_v<T, PhaseNode>) {
                    declare_module_scope_symbol(SymbolKind::Phase, node.name, node.location);
                    phase_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    node.is_stdlib = current_module_is_stdlib_;
                    declare_module_scope_symbol(SymbolKind::Func, node.name, node.location);
                    func_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, RuleNode> || std::is_same_v<T, ExternRuleNode>) {
                    node.is_stdlib = current_module_is_stdlib_;
                    declare_module_scope_symbol(SymbolKind::Rule, node.name, node.location);
                    rule_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, ConstBlockNode>) {
                    for (auto& a : node.assignments) {
                        declare_module_scope_symbol(SymbolKind::Const, a.name, a.location);
                        const_names_.insert(a.name);
                        result_.string_pool.intern(a.name);
                    }
                } else if constexpr (std::is_same_v<T, TemplateNode>) {
                    // Track template names separately from units (5.2)
                    declare_module_scope_symbol(SymbolKind::Template, node.name, node.location);
                    template_names_.insert(node.name);
                    archetype_traits_[node.name] = &node.traits;
                    if (node.is_pub) {
                        result_.pub_templates.insert(node.name);
                    } else {
                        result_.non_pub_templates.insert(node.name);
                    }
                } else if constexpr (std::is_same_v<T, EntityNode>) {
                    // Track entity names to distinguish from templates (5.4)
                    declare_module_scope_symbol(SymbolKind::Entity, node.name, node.location);
                    entity_names_.insert(node.name);
                    archetype_traits_[node.name] = &node.traits;
                } else if constexpr (std::is_same_v<T, UseNode>) {
                    // Track declared module names for `load` reachability (5.6)
                    use_names_.insert(node.module_name);
                    if (node.alias.has_value()) {
                        use_names_.insert(*node.alias);
                    }
                } else if constexpr (std::is_same_v<T, AssetDeclNode>) {
                    // Register asset name → opaque ID TypeKind (task 6.1)
                    declare_module_scope_symbol(SymbolKind::Asset, node.name, node.location);
                    TypeKind tk                  = asset_kind_to_type_kind(node.asset_kind);
                    asset_decl_types_[node.name] = tk;
                } else if constexpr (std::is_same_v<T, InputDeclNode>) {
                    // Register input name → InputButton or InputAxis (task 6.4)
                    declare_module_scope_symbol(SymbolKind::Input, node.name, node.location);
                    TypeKind tk = (node.input_kind == InputKind::Button) ? TypeKind::InputButton : TypeKind::InputAxis;
                    input_decl_types_[node.name] = tk;
                }
            },
            decl);
    }

    // Second pass: build resolved declarations that were historically produced
    // during collection but may contain reference sites (event field types).
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, EnumNode>) {
                    const auto enum_id = make_symbol_id(SymbolKind::Enum, current_module_id_, node.name);
                    ResolvedEnum re;
                    re.name = enum_id.local_name;
                    assign_canonical_identity(re, enum_id);
                    for (auto& v : node.variants) {
                        re.variants.push_back(v.name);
                    }
                    result_.enums[node.name] = std::move(re);
                } else if constexpr (std::is_same_v<T, EventNode>) {
                    const auto event_id    = make_symbol_id(SymbolKind::Event, current_module_id_, node.name);
                    node.resolved_event_id = event_id;
                    ResolvedStruct rs;
                    rs.name = event_id.local_name;
                    assign_canonical_identity(rs, event_id);
                    ResolvedEvent re;
                    re.name        = event_id.local_name;
                    re.is_pub      = node.is_pub;
                    re.is_external = node.is_external;
                    assign_canonical_identity(re, event_id);
                    for (auto& f : node.fields) {
                        ResolvedField rf;
                        rf.name        = f.name;
                        rf.type        = resolve_type_ref(f.type);
                        rf.is_let      = true;
                        rf.is_var      = false;
                        rf.is_persist  = f.modifiers.is_persist;
                        rf.is_sync     = f.modifiers.is_sync;
                        rf.is_pub      = f.modifiers.is_pub;
                        rf.has_default = f.default_value.has_value();
                        rs.fields.push_back(rf);
                        re.fields.push_back(std::move(rf));
                    }
                    event_structs_[node.name] = std::move(rs);
                    result_.events[node.name] = std::move(re);
                } else if constexpr (std::is_same_v<T, PhaseNode>) {
                    const auto phase_id    = make_symbol_id(SymbolKind::Phase, current_module_id_, node.name);
                    node.resolved_phase_id = phase_id;
                    ResolvedPhase rp;
                    rp.name      = phase_id.local_name;
                    rp.is_pub    = node.is_pub;
                    rp.has_every = node.every.has_value();
                    rp.has_max   = node.max.has_value();
                    assign_canonical_identity(rp, phase_id);
                    for (auto& f : node.fields) {
                        ResolvedField rf;
                        rf.name        = f.name;
                        rf.type        = resolve_type_ref(f.type);
                        rf.is_let      = true;
                        rf.has_default = true;
                        rp.fields.push_back(std::move(rf));
                    }
                    result_.phases[node.name] = std::move(rp);
                }
            },
            decl);
    }
}

// ── dsl-where-clause: guard desugaring ──────────────────────────────────────

void SemanticAnalyzer::desugar_where_clauses(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        auto* rule = std::get_if<RuleNode>(&decl);
        if (rule == nullptr || !rule->where_clause.has_value() || rule->where_clause->predicates.empty()) {
            continue;
        }
        const auto& predicates = rule->where_clause->predicates;
        const auto& location   = rule->where_clause->location;

        auto build_guard_condition = [&]() -> std::unique_ptr<ExprNode> {
            auto combined = clone_expr(*predicates.front());
            for (std::size_t i = 1; i < predicates.size(); ++i) {
                BinaryExpr conjunction{
                    .op = "and", .left = std::move(combined), .right = clone_expr(*predicates[i]), .location = location};
                combined = std::make_unique<ExprNode>(ExprNode::Variant{std::move(conjunction)}, location);
            }
            UnaryExpr negated{.op = "not", .operand = std::move(combined), .location = location};
            return std::make_unique<ExprNode>(ExprNode::Variant{std::move(negated)}, location);
        };

        for (auto& handler : rule->handlers) {
            std::vector<std::unique_ptr<StmtNode>> guard_then;
            guard_then.push_back(
                std::make_unique<StmtNode>(StmtNode::Variant{ReturnStmt{.value = std::nullopt, .location = location}},
                                           location));
            IfStmt guard{.condition = build_guard_condition(), .then_body = std::move(guard_then), .location = location};
            handler.body.insert(handler.body.begin(),
                                std::make_unique<StmtNode>(StmtNode::Variant{std::move(guard)}, location));
        }
    }
}

// ── Phase 2: Resolve Types ──────────────────────────────────────────────────

void SemanticAnalyzer::resolve_all_types(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, StructNode>) {
                    const auto struct_id = make_symbol_id(SymbolKind::Struct, current_module_id_, node.name);
                    ResolvedStruct rs;
                    rs.name = struct_id.local_name;
                    assign_canonical_identity(rs, struct_id);
                    for (auto& f : node.fields) {
                        ResolvedField rf;
                        rf.name = f.name;
                        rf.type = resolve_type_ref(f.type);
                        rs.fields.push_back(std::move(rf));
                    }
                    result_.structs[node.name] = std::move(rs);
                } else if constexpr (std::is_same_v<T, TraitNode>) {
                    const auto trait_id = make_symbol_id(SymbolKind::Trait, current_module_id_, node.name);
                    ResolvedTrait rt;
                    rt.name = trait_id.local_name;
                    assign_canonical_identity(rt, trait_id);
                    rt.is_pub    = node.is_pub;
                    rt.is_stdlib = node.is_stdlib;
                    for (auto& f : node.fields) {
                        ResolvedField rf;
                        rf.name        = f.name;
                        rf.type        = resolve_type_ref(f.type);
                        rf.is_let      = f.modifiers.is_let;
                        rf.is_var      = f.modifiers.is_var;
                        rf.is_persist  = f.modifiers.is_persist;
                        rf.is_sync     = f.modifiers.is_sync;
                        rf.is_pub      = f.modifiers.is_pub;
                        rf.has_default = f.default_value.has_value();
                        rt.fields.push_back(std::move(rf));
                    }
                    result_.traits[node.name] = std::move(rt);
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    const auto func_id = make_symbol_id(SymbolKind::Func, current_module_id_, node.name);
                    ResolvedFunc rf;
                    rf.name = func_id.local_name;
                    assign_canonical_identity(rf, func_id);
                    rf.is_pub    = node.is_pub;
                    rf.is_extern = node.is_extern;
                    rf.is_stdlib = node.is_stdlib;
                    if (!rf.is_extern) {
                        rf.effect_summary = std::unordered_set<std::string>{};
                    } else {
                        rf.effect_summary = known_stdlib_effect_summary(func_id);
                    }
                    for (auto& p : node.params) {
                        ResolvedParam rp;
                        rp.name = p.name;
                        rp.type = resolve_type_ref(p.type);
                        rf.params.push_back(std::move(rp));
                    }
                    if (node.return_type.has_value()) {
                        rf.return_type = resolve_type_ref(*node.return_type);
                    }
                    result_.funcs[node.name] = std::move(rf);
                }
            },
            decl);
    }
}

// 282; the single AST-wide reference-resolution walk (trait/template/event/callee
// refs across every expression and statement kind, plus every declaration kind) —
// genuinely a full-tree traversal, not a candidate for further extraction here.
// The previous version of this comment was on the closing-paren line of this
// multi-line signature rather than the function-name line clang-tidy reports the
// diagnostic at, so it was silently not suppressing anything (same systemic bug
// documented in design.md's addendum).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::resolve_trait_references(ProgramNode& program) {
    auto resolve_trait_entry = [this](ArchetypeTraitEntry& entry) {
        entry.resolved_trait_id = try_resolve_trait_ref_to_symbol(entry.trait_name);
    };

    std::function<void(std::vector<ChildOverrideNode>&)> resolve_child_overrides;
    std::function<void(std::vector<ChildArchetypeNode>&)> resolve_child_archetypes;
    std::function<void(ExprNode&)> resolve_expr;
    std::function<void(std::vector<std::unique_ptr<StmtNode>>&)> resolve_stmts;

    resolve_child_overrides = [&](std::vector<ChildOverrideNode>& overrides) {
        for (auto& override_node : overrides) {
            for (auto& trait : override_node.traits) {
                resolve_trait_entry(trait);
            }
            resolve_child_overrides(override_node.children);
        }
    };

    resolve_child_archetypes = [&](std::vector<ChildArchetypeNode>& children) {
        for (auto& child : children) {
            if (child.template_ref.has_value()) {
                child.resolved_template_ref_id = try_resolve_template_ref_to_symbol(*child.template_ref);
            }
            for (auto& use : child.template_uses) {
                use.resolved_template_id = try_resolve_template_ref_to_symbol(use.template_name);
            }
            for (auto& trait : child.traits) {
                resolve_trait_entry(trait);
            }
            resolve_child_archetypes(child.children);
            resolve_child_overrides(child.child_overrides);
        }
    };

    auto resolve_filter_clause = [this](FilterClause& clause) {
        clause.resolved_trait_ids.clear();
        if (!clause.entries.empty()) {
            clause.resolved_trait_ids.reserve(clause.entries.size());
            for (auto& entry : clause.entries) {
                entry.resolved_trait_id = try_resolve_trait_ref_to_symbol(entry.qualified_name);
                if (entry.resolved_trait_id.has_value()) {
                    clause.resolved_trait_ids.push_back(*entry.resolved_trait_id);
                }
            }
            return;
        }

        clause.resolved_trait_ids.reserve(clause.trait_names.size());
        for (const auto& trait_name : clause.trait_names) {
            auto resolved = try_resolve_trait_ref_to_symbol(trait_name);
            if (resolved.has_value()) {
                clause.resolved_trait_ids.push_back(*resolved);
            }
        }
    };

    resolve_expr = [&](ExprNode& expr) {
        std::visit(
            [&](auto& e) {  // NOLINT(readability-function-cognitive-complexity) -- per-ExprNode-kind resolution
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, UnaryExpr>) {
                    resolve_expr(*e.operand);
                } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                    resolve_expr(*e.left);
                    resolve_expr(*e.right);
                } else if constexpr (std::is_same_v<E, CallExpr>) {
                    e.resolved_callee_id = resolve_callee_symbol(*e.callee);
                    resolve_expr(*e.callee);
                    for (auto& arg : e.args) {
                        resolve_expr(*arg);
                    }
                } else if constexpr (std::is_same_v<E, MemberExpr>) {
                    resolve_expr(*e.object);
                    // Enum member chains (`Key.A`, `inp.Key.A`, `std.input.Key.A`)
                    // resolve here; non-enum chains fall through untouched.
                    resolve_enum_member_expr(e, expr.location);
                } else if constexpr (std::is_same_v<E, LambdaExpr>) {
                    resolve_expr(*e.body);
                } else if constexpr (std::is_same_v<E, PipelineExpr>) {
                    resolve_expr(*e.source);
                    for (auto& op : e.operations) {
                        for (auto& arg : op.args) {
                            resolve_expr(*arg);
                        }
                    }
                } else if constexpr (std::is_same_v<E, MatchExpr>) {
                    resolve_expr(*e.subject);
                    for (auto& arm : e.arms) {
                        resolve_expr(*arm.pattern);
                        resolve_expr(*arm.body);
                    }
                } else if constexpr (std::is_same_v<E, IfExpr>) {
                    resolve_expr(*e.condition);
                    resolve_expr(*e.then_expr);
                    resolve_expr(*e.else_expr);
                } else if constexpr (std::is_same_v<E, ListExpr>) {
                    for (auto& element : e.elements) {
                        resolve_expr(*element);
                    }
                } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                    e.resolved_template_id = try_resolve_template_ref_to_symbol(e.template_name);
                    for (auto& override_entry : e.overrides) {
                        resolve_trait_entry(override_entry);
                        for (auto& assignment : override_entry.assignments) {
                            resolve_expr(*assignment.value);
                        }
                    }
                    resolve_child_overrides(e.child_overrides);
                } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                    e.resolved_callee_id = resolve_callee_symbol(*e.callee);
                    resolve_expr(*e.callee);
                    for (auto& pred : e.filters) {
                        pred.resolved_trait_id = try_resolve_trait_ref_to_symbol(pred.trait_name);
                    }
                    for (auto& arg : e.named_args) {
                        resolve_expr(*arg.value);
                    }
                }
            },
            expr.expr);
    };

    resolve_stmts = [&](std::vector<std::unique_ptr<StmtNode>>& stmts) {
        for (auto& stmt : stmts) {
            std::visit(
                [&](auto& s) {  // NOLINT(readability-function-cognitive-complexity) -- per-StmtNode-kind resolution
                    using S = std::decay_t<decltype(s)>;
                    if constexpr (std::is_same_v<S, LetStmt> || std::is_same_v<S, VarAssign>) {
                        resolve_expr(*s.value);
                    } else if constexpr (std::is_same_v<S, EmitStmt>) {
                        s.resolved_event_id = try_resolve_event_ref_to_symbol(s.event_name);
                        if (s.target.has_value()) {
                            resolve_expr(**s.target);
                        }
                        for (auto& field : s.payload) {
                            resolve_expr(*field.value);
                        }
                    } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                        if (s.value.has_value()) {
                            resolve_expr(**s.value);
                        }
                    } else if constexpr (std::is_same_v<S, ExprStmt>) {
                        resolve_expr(*s.expr);
                    } else if constexpr (std::is_same_v<S, IfStmt>) {
                        resolve_expr(*s.condition);
                        resolve_stmts(s.then_body);
                        for (auto& branch : s.else_if_branches) {
                            resolve_expr(*branch.condition);
                            resolve_stmts(branch.body);
                        }
                        resolve_stmts(s.else_body);
                    } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                        resolve_expr(*s.iterable);
                        resolve_stmts(s.body);
                    } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                        resolve_expr(*s.subject);
                        for (auto& arm : s.arms) {
                            arm.resolved_trait_id = try_resolve_trait_ref_to_symbol(arm.trait_name);
                            resolve_stmts(arm.body);
                        }
                        if (s.wildcard.has_value()) {
                            resolve_stmts(s.wildcard->body);
                        }
                    } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                        s.resolved_template_id = try_resolve_template_ref_to_symbol(s.template_name);
                        for (auto& override_entry : s.overrides) {
                            resolve_trait_entry(override_entry);
                            for (auto& assignment : override_entry.assignments) {
                                resolve_expr(*assignment.value);
                            }
                        }
                        resolve_child_overrides(s.child_overrides);
                    } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                        if (s.target_expr.has_value()) {
                            resolve_expr(**s.target_expr);
                        }
                    } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                        s.resolved_trait_id = try_resolve_trait_ref_to_symbol(s.trait_name);
                        for (auto& arg : s.args) {
                            resolve_expr(*arg.value);
                        }
                        if (s.target_expr.has_value()) {
                            resolve_expr(**s.target_expr);
                        }
                    } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                        s.resolved_trait_id = try_resolve_trait_ref_to_symbol(s.trait_name);
                        if (s.target_expr.has_value()) {
                            resolve_expr(**s.target_expr);
                        }
                    } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                        s.resolved_trait_id = try_resolve_trait_ref_to_symbol(s.trait_name);
                        for (auto& arg : s.args) {
                            resolve_expr(*arg.value);
                        }
                        if (s.target_expr.has_value()) {
                            resolve_expr(**s.target_expr);
                        }
                    }
                },
                stmt->stmt);
        }
    };

    for (auto& decl : program.declarations) {
        std::visit(
            [&](auto& node) {  // NOLINT(readability-function-cognitive-complexity) -- per-declaration-kind resolution
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ConstBlockNode>) {
                    for (auto& assignment : node.assignments) {
                        resolve_expr(*assignment.value);
                    }
                } else if constexpr (std::is_same_v<T, TraitNode>) {
                    for (auto& field : node.fields) {
                        if (field.default_value.has_value()) {
                            resolve_expr(**field.default_value);
                        }
                    }
                } else if constexpr (std::is_same_v<T, EntityNode>) {
                    node.resolved_entity_id = make_symbol_id(SymbolKind::Entity, current_module_id_, node.name);
                    if (node.template_ref.has_value()) {
                        node.resolved_template_ref_id = try_resolve_template_ref_to_symbol(*node.template_ref);
                    }
                    for (auto& trait : node.traits) {
                        resolve_trait_entry(trait);
                        for (auto& assignment : trait.assignments) {
                            resolve_expr(*assignment.value);
                        }
                    }
                    for (auto& use : node.template_uses) {
                        use.resolved_template_id = try_resolve_template_ref_to_symbol(use.template_name);
                    }
                    resolve_child_archetypes(node.children);
                    resolve_child_overrides(node.child_overrides);
                } else if constexpr (std::is_same_v<T, TemplateNode>) {
                    node.resolved_template_id = make_symbol_id(SymbolKind::Template, current_module_id_, node.name);
                    for (auto& trait : node.traits) {
                        resolve_trait_entry(trait);
                        for (auto& assignment : trait.assignments) {
                            resolve_expr(*assignment.value);
                        }
                    }
                    for (auto& use : node.template_uses) {
                        use.resolved_template_id = try_resolve_template_ref_to_symbol(use.template_name);
                    }
                    resolve_child_archetypes(node.children);
                } else if constexpr (std::is_same_v<T, PhaseNode>) {
                    node.resolved_phase_id = make_symbol_id(SymbolKind::Phase, current_module_id_, node.name);
                    node.resolved_from.clear();
                    for (const auto& source : node.from_sources) {
                        if (auto symbol = try_resolve_event_ref_to_symbol(source.spelling); symbol.has_value()) {
                            node.resolved_from.push_back(
                                ResolvedHandlerTrigger{.kind = HandlerTriggerKind::Event, .symbol = *symbol});
                        }
                    }
                    node.resolved_after.clear();
                    for (const auto& dependency : node.after_phases) {
                        if (auto symbol = try_resolve_phase_ref_to_symbol(dependency.spelling); symbol.has_value()) {
                            node.resolved_after.push_back(
                                ResolvedHandlerTrigger{.kind = HandlerTriggerKind::Phase, .symbol = *symbol});
                        }
                    }
                    if (node.every.has_value()) {
                        resolve_expr(**node.every);
                    }
                    if (node.max.has_value()) {
                        resolve_expr(**node.max);
                    }
                    for (auto& field : node.fields) {
                        resolve_expr(*field.initializer);
                    }
                    auto phase_it = result_.phases.find(node.name);
                    if (phase_it != result_.phases.end()) {
                        phase_it->second.from_sources = node.resolved_from;
                        phase_it->second.after_phases = node.resolved_after;
                    }
                } else if constexpr (std::is_same_v<T, RuleNode>) {
                    node.resolved_rule_id = make_symbol_id(SymbolKind::Rule, current_module_id_, node.name);
                    resolve_filter_clause(node.filter);
                    resolve_filter_clause(node.exclude);
                    if (node.pairs.has_value()) {
                        for (auto& binding : node.pairs->bindings) {
                            for (auto& entry : binding.traits) {
                                entry.resolved_trait_id = try_resolve_trait_ref_to_symbol(entry.qualified_name);
                            }
                        }
                    }
                    if (node.where_clause.has_value()) {
                        for (auto& predicate : node.where_clause->predicates) {
                            resolve_expr(*predicate);
                        }
                    }
                    for (auto& key : node.order_by) {
                        resolve_expr(*key.expression);
                    }
                    for (auto& handler : node.handlers) {
                        handler.resolved_trigger = try_resolve_handler_trigger(handler.event_name);
                        resolve_stmts(handler.body);
                    }
                } else if constexpr (std::is_same_v<T, ExternRuleNode>) {
                    node.resolved_rule_id = make_symbol_id(SymbolKind::Rule, current_module_id_, node.name);
                    resolve_filter_clause(node.filter);
                    resolve_filter_clause(node.exclude);
                    for (auto& key : node.order_by) {
                        resolve_expr(*key.expression);
                    }
                    for (auto& handler : node.handlers) {
                        handler.resolved_trigger = try_resolve_handler_trigger(handler.trigger_name);
                    }
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    node.resolved_func_id = make_symbol_id(SymbolKind::Func, current_module_id_, node.name);
                    resolve_stmts(node.body);
                } else if constexpr (std::is_same_v<T, AssetDeclNode>) {
                    node.resolved_asset_id = make_symbol_id(SymbolKind::Asset, current_module_id_, node.name);
                } else if constexpr (std::is_same_v<T, InputDeclNode>) {
                    node.resolved_input_id = make_symbol_id(SymbolKind::Input, current_module_id_, node.name);
                    for (auto& prop : node.props) {
                        resolve_expr(*prop.value);
                    }
                    validate_input_decl_props(node);
                }
            },
            decl);
    }
}

TypeInfo SemanticAnalyzer::resolve_type_ref(const TypeRef& ref) {
    // ── Qualified name: "module.Symbol" or "alias.Symbol" ──────────────────
    auto dot = ref.name.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier = ref.name.substr(0, dot);
        auto sym_name  = ref.name.substr(dot + 1);
        return resolve_qualified_type(qualifier, sym_name, ref.location);
    }

    // ── Built-in list type ──────────────────────────────────────────────────
    if (ref.name == "list") {
        if (ref.param) {
            auto elem = resolve_type_ref(**ref.param);
            return make_list_type(std::move(elem));
        }
        errors_.error(ref.location, "list type requires a type parameter, e.g. list[int]");
        return make_unknown_type();
    }

    // ── Built-in primitive ──────────────────────────────────────────────────
    auto kind = type_kind_from_name(ref.name);
    if (kind != TypeKind::Unknown) {
        TypeInfo ti;
        ti.kind = kind;
        ti.name = ref.name;
        return ti;
    }

    // ── Local user-defined types ────────────────────────────────────────────
    if (struct_names_.contains(ref.name)) {
        return make_resolved_user_type(
            TypeKind::Struct, make_symbol_id(SymbolKind::Struct, current_module_id_, ref.name), ref.name);
    }
    if (enum_names_.contains(ref.name)) {
        return make_resolved_user_type(
            TypeKind::Enum, make_symbol_id(SymbolKind::Enum, current_module_id_, ref.name), ref.name);
    }

    // ── Prelude/ordinary import diagnostics ─────────────────────────────────
    if (!imports_.empty()) {
        return resolve_imported_type(ref.name, ref.location);
    }

    errors_.error(ref.location, "unknown type '" + ref.name + "'");
    return make_unknown_type();
}

// ── Task 4.2: Qualified type resolution ────────────────────────────────────

TypeInfo SemanticAnalyzer::resolve_qualified_type(const std::string& qualifier,
                                                  const std::string& sym_name,
                                                  const SourceLocation& loc) {
    // Unified lookup (task 3.2): current-module, alias, and canonical-path
    // qualifiers all resolve through resolve_name; only struct/enum symbols
    // name types.
    auto segments = dotted_segments(qualifier);
    segments.push_back(sym_name);
    if (auto resolved = resolve_name(segments); resolved.has_value() && resolved->member_segments.empty()) {
        if (resolved->symbol.kind == SymbolKind::Struct) {
            return make_resolved_user_type(TypeKind::Struct, resolved->symbol);
        }
        if (resolved->symbol.kind == SymbolKind::Enum) {
            return make_resolved_user_type(TypeKind::Enum, resolved->symbol);
        }
    }

    if (qualifier != current_module_name_ && find_imported_module(qualifier) == nullptr) {
        errors_.error(loc, "unknown module qualifier '" + qualifier + "'");
        return make_unknown_type();
    }

    // ── Task 4.6: Non-pub helpful error ─────────────────────────────────────
    auto np_it = imports_.non_pub_trait_names.find(qualifier);
    if (np_it != imports_.non_pub_trait_names.end() && np_it->second.contains(sym_name)) {
        errors_.error(
            loc,
            "trait '" + sym_name + "' is not public in module '" + qualifier + "'; did you mean to mark it as 'pub'?");
        return make_unknown_type();
    }

    errors_.error(loc, "unknown symbol '" + sym_name + "' in module '" + qualifier + "'");
    return make_unknown_type();
}

// ── Unqualified type lookup: local first, then std.core prelude only. Ordinary
// imports are namespace bindings and must be referenced with module/alias.

TypeInfo SemanticAnalyzer::resolve_imported_type(const std::string& name, const SourceLocation& loc) {
    // Unified lookup (task 3.2): bare names resolve local-first, then the
    // std.core prelude, matching resolve_name's precedence.
    if (auto resolved = resolve_name({name}); resolved.has_value() && resolved->member_segments.empty()) {
        if (resolved->symbol.kind == SymbolKind::Struct) {
            return make_resolved_user_type(TypeKind::Struct, resolved->symbol);
        }
        if (resolved->symbol.kind == SymbolKind::Enum) {
            return make_resolved_user_type(TypeKind::Enum, resolved->symbol);
        }
    }

    const auto providers = type_provider_qualifiers(imports_, name);
    if (providers.empty()) {
        errors_.error(loc, "unknown type '" + name + "'");
        return make_unknown_type();
    }
    errors_.error(loc, imported_reference_diagnostic(imports_, "type", name, providers));
    return make_unknown_type();
}

bool SemanticAnalyzer::is_known_type(const std::string& name) const {
    return (builtin_types().contains(name)) || (struct_names_.contains(name)) || (enum_names_.contains(name));
}

// ── Phase 3a: Const String Check ────────────────────────────────────────────

// Two-tier per-declaration/per-statement string-purity walk.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::check_const_strings(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {  // NOLINT(readability-function-cognitive-complexity) -- const block vs func body
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ConstBlockNode>) {
                    for (auto& a : node.assignments) {
                        check_const_strings_expr(*a.value, true);
                    }
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    for (auto& stmt : node.body) {
                        // Only assign/return/emit arms can hold const-string violations.
                        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
                        std::visit(
                            [this](auto& s) {
                                using S = std::decay_t<decltype(s)>;
                                if constexpr (std::is_same_v<S, VarAssign> || std::is_same_v<S, LetStmt>) {
                                    check_const_strings_expr(*s.value, false);
                                } else if constexpr (std::is_same_v<S, ExprStmt>) {
                                    check_const_strings_expr(*s.expr, false);
                                } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                                    if (s.value) {
                                        check_const_strings_expr(**s.value, false);
                                    }
                                } else if constexpr (std::is_same_v<S, EmitStmt>) {
                                    if (s.target.has_value()) {
                                        check_const_strings_expr(**s.target, false);
                                    }
                                    for (auto& field : s.payload) {
                                        check_const_strings_expr(*field.value, false);
                                    }
                                }
                            },
                            stmt->stmt);
                    }
                }
            },
            decl);
    }
}

void SemanticAnalyzer::check_const_strings_expr(const ExprNode& expr, bool in_const) {
    std::visit(
        [this, in_const](auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::String && !in_const) {
                    errors_.error(e.location, "string literals are only allowed in const blocks");
                }
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                check_const_strings_expr(*e.left, in_const);
                check_const_strings_expr(*e.right, in_const);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                check_const_strings_expr(*e.operand, in_const);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                check_const_strings_expr(*e.callee, in_const);
                if (is_std_text_format_callee(*e.callee)) {
                    for (size_t i = 0; i < e.args.size(); ++i) {
                        check_const_strings_expr(*e.args[i], in_const || (i == 0));
                    }
                } else {
                    for (auto& arg : e.args) {
                        check_const_strings_expr(*arg, in_const);
                    }
                }
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                check_const_strings_expr(*e.object, in_const);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                for (auto& el : e.elements) {
                    check_const_strings_expr(*el, in_const);
                }
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                for (auto& trait : e.overrides) {
                    for (auto& field : trait.assignments) {
                        check_const_strings_expr(*field.value, in_const);
                    }
                }
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                for (auto& arg : e.named_args) {
                    check_const_strings_expr(*arg.value, in_const);
                }
            }
        },
        expr.expr);
}

// ── Phase 3b: Func Purity ───────────────────────────────────────────────────

void SemanticAnalyzer::check_func_purity(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* fn = std::get_if<FuncNode>(&decl)) {
            // Task 4.5: Skip extern funcs — no body, purity is guaranteed
            if (fn->is_extern) {
                continue;
            }
            for (auto& stmt : fn->body) {
                check_func_purity_stmt(*stmt, fn->name);
            }
        }
    }
}

void SemanticAnalyzer::check_func_purity_stmt(const StmtNode& stmt, const std::string& func_name) {
    std::visit(
        [this, &func_name](auto& s) {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, EmitStmt>) {
                errors_.error(s.location, "func '" + func_name + "' cannot use 'emit' (funcs must be pure)");
            } else if constexpr (std::is_same_v<S, LetStmt> || std::is_same_v<S, VarAssign>) {
                check_func_purity_expr(*s.value, func_name);
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                check_func_purity_expr(*s.expr, func_name);
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) {
                    check_func_purity_expr(**s.value, func_name);
                }
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                check_func_purity_expr(*s.condition, func_name);
                for (auto& inner : s.then_body) {
                    check_func_purity_stmt(*inner, func_name);
                }
                for (auto& branch : s.else_if_branches) {
                    check_func_purity_expr(*branch.condition, func_name);
                    for (auto& inner : branch.body) {
                        check_func_purity_stmt(*inner, func_name);
                    }
                }
                for (auto& inner : s.else_body) {
                    check_func_purity_stmt(*inner, func_name);
                }
            }
        },
        stmt.stmt);
}

// The recursive deny-list traversal shared by check_func_purity_expr,
// check_where_purity_expr, and check_order_by_purity_expr (design.md: extract
// rather than add a third divergent copy). The traversal order and shape is
// identical across all three; what differs is which node kinds are impure and
// what diagnostic that produces, so those decisions are the caller-supplied
// hooks (fired after recursing into a CallExpr's children, matching every
// existing call site's order; fired before recursing into a SpawnExpr's or
// QueryCallExpr's children, also matching every existing call site).
void SemanticAnalyzer::check_purity_deny_list(  // NOLINT(readability-function-cognitive-complexity) -- exhaustive
                                                // per-ExprNode-kind traversal, inherited from the
                                                // already-NOLINT'd check_where_purity_expr this consolidates
    const ExprNode& expr,
    const std::function<void(const CallExpr&)>& on_call,
    const std::function<void(const SpawnExpr&)>& on_spawn,
    const std::function<void(const QueryCallExpr&)>& on_query) {
    std::visit(
        [&](auto& e) {  // NOLINT(readability-function-cognitive-complexity) -- see function-level NOLINT above
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, CallExpr>) {
                check_purity_deny_list(*e.callee, on_call, on_spawn, on_query);
                for (auto& arg : e.args) {
                    check_purity_deny_list(*arg, on_call, on_spawn, on_query);
                }
                on_call(e);
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                check_purity_deny_list(*e.left, on_call, on_spawn, on_query);
                check_purity_deny_list(*e.right, on_call, on_spawn, on_query);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                check_purity_deny_list(*e.operand, on_call, on_spawn, on_query);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                check_purity_deny_list(*e.object, on_call, on_spawn, on_query);
            } else if constexpr (std::is_same_v<E, LambdaExpr>) {
                check_purity_deny_list(*e.body, on_call, on_spawn, on_query);
            } else if constexpr (std::is_same_v<E, PipelineExpr>) {
                check_purity_deny_list(*e.source, on_call, on_spawn, on_query);
                for (auto& op : e.operations) {
                    for (auto& arg : op.args) {
                        check_purity_deny_list(*arg, on_call, on_spawn, on_query);
                    }
                }
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                check_purity_deny_list(*e.subject, on_call, on_spawn, on_query);
                for (auto& arm : e.arms) {
                    check_purity_deny_list(*arm.pattern, on_call, on_spawn, on_query);
                    check_purity_deny_list(*arm.body, on_call, on_spawn, on_query);
                }
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                check_purity_deny_list(*e.condition, on_call, on_spawn, on_query);
                check_purity_deny_list(*e.then_expr, on_call, on_spawn, on_query);
                check_purity_deny_list(*e.else_expr, on_call, on_spawn, on_query);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                for (auto& element : e.elements) {
                    check_purity_deny_list(*element, on_call, on_spawn, on_query);
                }
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                on_spawn(e);
                for (auto& trait : e.overrides) {
                    for (auto& field : trait.assignments) {
                        check_purity_deny_list(*field.value, on_call, on_spawn, on_query);
                    }
                }
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                on_query(e);
                for (auto& arg : e.named_args) {
                    check_purity_deny_list(*arg.value, on_call, on_spawn, on_query);
                }
            }
        },
        expr.expr);
}

void SemanticAnalyzer::check_func_purity_expr(const ExprNode& expr, const std::string& func_name) {
    check_purity_deny_list(
        expr,
        /*on_call=*/
        [this, &func_name](const CallExpr& e) {
            if (auto* ident = std::get_if<IdentExpr>(&e.callee->expr); ident != nullptr && ident->name == "exists") {
                errors_.error(e.location, "`exists()` requires world access; only allowed inside rule event handlers");
            }
            if (auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
                // Use canonical IDs so same simple names across modules don't
                // create false recursion edges (task 3.6).
                const auto callee_canonical = func_names_.contains(ident->name)
                                                  ? make_canonical_id(current_module_name_, ident->name)
                                                  : ident->name;
                call_graph_[make_canonical_id(current_module_name_, func_name)].insert(callee_canonical);
                // Ordinary imports are namespace bindings. Unqualified imported
                // function calls are rejected unless they come from std.core.
                if (!func_names_.contains(ident->name) && !imports_.empty()) {
                    auto pit = imports_.func_providers.find(ident->name);
                    if (pit != imports_.func_providers.end() && !pit->second.empty() &&
                        !find_std_core_provider(imports_, pit->second).has_value()) {
                        errors_.error(e.location,
                                      imported_reference_diagnostic(imports_, "function", ident->name, pit->second));
                    }
                }
            }
        },
        /*on_spawn=*/[](const SpawnExpr&) {},
        /*on_query=*/
        [this](const QueryCallExpr& e) {
            errors_.error(e.location, "query expressions require world access; only allowed inside rule event handlers");
        });
}

// ── Phase 3c: No Recursion ──────────────────────────────────────────────────

// Iterative cycle-detection DFS (self-recursion only) plus its own
// diagnostic-location lookup — one cohesive algorithm.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::check_no_recursion(ProgramNode& program) {
    for (auto& [func_canonical, callees] : call_graph_) {
        std::unordered_set<std::string> visited;
        std::vector<std::string> stack = {func_canonical};
        visited.insert(func_canonical);

        while (!stack.empty()) {
            auto current = stack.back();
            stack.pop_back();

            auto it = call_graph_.find(current);
            if (it == call_graph_.end()) {
                continue;
            }

            for (const auto& callee : it->second) {
                if (callee == func_canonical) {
                    // Strip module prefix for readable diagnostic.
                    auto dot        = func_canonical.rfind('.');
                    auto local_name = dot != std::string::npos ? func_canonical.substr(dot + 1) : func_canonical;
                    for (auto& decl : program.declarations) {
                        if (auto* fn = std::get_if<FuncNode>(&decl)) {
                            if (fn->name == local_name) {
                                errors_.error(fn->location,
                                              "func '" + local_name + "' is recursive (recursion is not allowed)");
                                break;
                            }
                        }
                    }
                    break;
                }
                if (!visited.contains(callee)) {
                    visited.insert(callee);
                    stack.push_back(callee);
                }
            }
        }
    }
}

// ── Phase 3d: Persist/Sync Validation ───────────────────────────────────────

void SemanticAnalyzer::check_persist_sync(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* trait = std::get_if<TraitNode>(&decl)) {
            for (auto& field : trait->fields) {
                if (field.modifiers.is_persist && field.modifiers.is_let) {
                    errors_.error(field.location, "persist modifier can only be used on 'var' fields, not 'let'");
                }
                if (field.modifiers.is_sync && field.modifiers.is_let) {
                    errors_.error(field.location, "sync modifier can only be used on 'var' fields, not 'let'");
                }
            }
        } else if (auto* event = std::get_if<EventNode>(&decl)) {
            for (auto& field : event->fields) {
                if (field.modifiers.is_let || field.modifiers.is_var || field.modifiers.is_persist ||
                    field.modifiers.is_sync || field.modifiers.is_pub) {
                    errors_.error(field.location,
                                  "event fields use bare `name: type` syntax; trait field modifiers are not allowed in "
                                  "event declarations");
                }
            }
        }
    }
}

// Validates every trait field's default-value expression: type-compatible
// with the field, plus a recursive check that it's constant-expression-shaped
// (literals, allowed stdlib constructor calls, lists thereof) — two
// independent per-field checks, the second an exhaustive ExprNode dispatch.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::validate_trait_default_values(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* trait = std::get_if<TraitNode>(&decl)) {
            std::unordered_map<std::string, TypeInfo> empty_locals;
            for (auto& field : trait->fields) {
                if (!field.default_value.has_value()) {
                    continue;
                }

                auto expected = resolve_type_ref(field.type);
                auto actual   = infer_expr_type(**field.default_value, {}, empty_locals, nullptr);
                if (actual.kind != TypeKind::Unknown && expected.kind != TypeKind::Unknown &&
                    actual.kind != expected.kind) {
                    errors_.error(
                        field.location,
                        "default value type '" + actual.name + "' does not match field type '" + expected.name + "'");
                }

                bool constant_ok                                 = true;
                std::function<void(const ExprNode&)> check_const = [&](const ExprNode& expr) {
                    std::visit(
                        [&](const auto& e) {
                            using E = std::decay_t<decltype(e)>;
                            if constexpr (std::is_same_v<E, LiteralExpr>) {
                            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                                check_const(*e.operand);
                            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                                check_const(*e.left);
                                check_const(*e.right);
                            } else if constexpr (std::is_same_v<E, CallExpr>) {
                                bool allowed_ctor = false;
                                if (const auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
                                    allowed_ctor =
                                        ident->name == "vec2" || ident->name == "vec3" || ident->name == "quat";
                                }
                                // Allow qualified stdlib constructor calls (e.g. rand.seeded, rand.uniform).
                                if (const auto* member = std::get_if<MemberExpr>(&e.callee->expr)) {
                                    allowed_ctor = member->member == "seeded" || member->member == "uniform" ||
                                                   member->member == "uniform_int" || member->member == "normal" ||
                                                   member->member == "identity";
                                }
                                if (allowed_ctor) {
                                    for (const auto& arg : e.args) {
                                        check_const(*arg);
                                    }
                                } else {
                                    constant_ok = false;
                                }
                            } else if constexpr (std::is_same_v<E, ListExpr>) {
                                for (const auto& el : e.elements) {
                                    check_const(*el);
                                }
                            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                                // A resolved enum-qualified literal (e.g. `GizmoMode.Select`) is
                                // constant; resolve_enum_member_expr already validated it names a
                                // real variant. Any other member access is not.
                                if (!e.resolved_enum_member.has_value()) {
                                    constant_ok = false;
                                }
                            } else {
                                constant_ok = false;
                            }
                        },
                        expr.expr);
                };
                check_const(**field.default_value);
                if (!constant_ok) {
                    errors_.error(field.location, "trait field default value must be a constant expression");
                }
            }
        }
    }
}

// ── Phase 3e: Rule Filter Validation (tasks 4.2, 4.4, 4.5, 4.6) ────────────

bool SemanticAnalyzer::resolve_filter_entry(const FilterEntry& entry, std::string& out_simple_name) {
    const auto& qname = entry.qualified_name;

    // Unified lookup: alias-, canonical-, and current-module-qualified plus
    // bare local/prelude spellings all resolve the same way.
    if (auto resolved = try_resolve_ref_of_kind(qname, {SymbolKind::Trait})) {
        out_simple_name = resolved->local_name;
        return true;
    }

    auto dot = qname.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = qname.substr(0, dot);
        auto trait_name = qname.substr(dot + 1);

        if (qualifier != current_module_name_ && find_imported_module(qualifier) == nullptr) {
            errors_.error(entry.location, "unknown module qualifier '" + qualifier + "' in filter");
            return false;
        }
        // ── Task 4.6: Non-pub helpful error ──────────────────────────────────
        auto np_it = imports_.non_pub_trait_names.find(qualifier);
        if (np_it != imports_.non_pub_trait_names.end() && np_it->second.contains(trait_name)) {
            errors_.error(entry.location,
                          "trait '" + trait_name + "' is not public in module '" + qualifier +
                              "'; did you mean to mark it as 'pub'?");
        } else {
            errors_.error(entry.location,
                          "rule filter references unknown trait '" + trait_name + "' in module '" + qualifier + "'");
        }
        return false;
    }

    // Bare name provided only by ordinary (non-prelude) imports: point at the
    // qualified spelling.
    if (!imports_.empty()) {
        auto it = imports_.trait_providers.find(qname);
        if (it != imports_.trait_providers.end()) {
            errors_.error(entry.location, imported_reference_diagnostic(imports_, "trait", qname, it->second));
            return false;
        }
    }

    errors_.error(entry.location, "unknown trait '" + qname + "' in filter");
    return false;
}

// ── Pair relations (dsl-pair-relations) ─────────────────────────────────────

PairScope SemanticAnalyzer::build_pair_scope(const PairClause& pairs) {
    PairScope scope;
    for (std::size_t index = 0; index < pairs.bindings.size(); ++index) {
        const auto& binding = pairs.bindings[index];
        PairBindingScope binding_scope;
        binding_scope.index = index;
        for (const auto& entry : binding.traits) {
            if (!entry.resolved_trait_id.has_value()) {
                continue;
            }
            binding_scope.trait_by_access_key[entry.qualified_name] = *entry.resolved_trait_id;
            if (entry.alias.has_value()) {
                binding_scope.trait_by_access_key[*entry.alias] = *entry.resolved_trait_id;
            }
        }
        scope[binding.name] = std::move(binding_scope);
    }
    return scope;
}

std::optional<SemanticAnalyzer::PairMemberResolution> SemanticAnalyzer::resolve_pair_member_chain(
    const std::string& binding_name,
    const std::vector<std::string>& segments,
    const PairScope& pair_scope) {
    auto scope_it = pair_scope.find(binding_name);
    if (scope_it == pair_scope.end() || segments.empty()) {
        return std::nullopt;
    }
    const auto& scope  = scope_it->second;
    const auto max_len = std::min<std::size_t>(2, segments.size());
    for (std::size_t len = max_len; len >= 1; --len) {
        std::string key;
        for (std::size_t i = 0; i < len; ++i) {
            if (i != 0) {
                key += '.';
            }
            key += segments[i];
        }
        if (auto trait_it = scope.trait_by_access_key.find(key); trait_it != scope.trait_by_access_key.end()) {
            return PairMemberResolution{
                .binding_index = scope.index, .trait_id = trait_it->second, .consumed_segments = len};
        }
    }
    return std::nullopt;
}

void SemanticAnalyzer::validate_pair_bindings(RuleNode& rule) {
    if (!rule.pairs.has_value()) {
        return;
    }
    auto& pairs = *rule.pairs;

    // order by: is deliberately absent here — a pairs: rule may declare it
    // (dsl-pair-relations); only filter:/exclude: force a unary domain.
    const bool has_unary_clause = !rule.filter.entries.empty() || !rule.filter.trait_names.empty() ||
                                  !rule.exclude.entries.empty() || !rule.exclude.trait_names.empty();
    if (has_unary_clause) {
        errors_.error(pairs.location,
                      "rule '" + rule.name +
                          "' must choose one execution domain: `pairs:` cannot be combined with `filter:` or "
                          "`exclude:`");
    }

    if (pairs.bindings.size() != 2) {
        return;  // cardinality already reported by the parser
    }

    if (pairs.bindings[0].name == pairs.bindings[1].name) {
        errors_.error(pairs.bindings[1].location,
                      "duplicate pair binding name '" + pairs.bindings[1].name + "' in rule '" + rule.name + "'");
    }

    for (auto& binding : pairs.bindings) {
        std::unordered_map<std::string, SourceLocation> seen_keys;
        for (auto& entry : binding.traits) {
            std::string simple_name;
            resolve_filter_entry(entry, simple_name);

            auto register_key = [&](const std::string& key, const SourceLocation& loc) {
                if (seen_keys.contains(key)) {
                    errors_.error(loc, "'" + key + "' is ambiguous on pair binding '" + binding.name + "'");
                } else {
                    seen_keys[key] = loc;
                }
            };
            register_key(entry.qualified_name, entry.location);
            if (entry.alias.has_value()) {
                register_key(*entry.alias, entry.location);
            }
        }
    }
}

std::unordered_map<std::string, const ResolvedTrait*> SemanticAnalyzer::build_filter_bindings(
    const FilterClause& filter) const {
    std::unordered_map<std::string, const ResolvedTrait*> bindings;
    for (const auto& entry : filter.entries) {
        // Use the resolved id + full qualified name for canonical lookup so
        // this returns the correct trait even when multiple modules share a
        // local name (task 3.5).
        const auto* trait = find_resolved_trait(entry.resolved_trait_id, entry.qualified_name);
        auto dot          = entry.qualified_name.rfind('.');
        auto simple       = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
        if (trait != nullptr) {
            bindings[simple] = trait;
            if (entry.alias.has_value()) {
                bindings[*entry.alias] = trait;
            }
        }
    }
    for (const auto& name : filter.trait_names) {
        const auto* trait = find_resolved_trait(name);
        if (trait != nullptr) {
            bindings[name] = trait;
        }
    }
    return bindings;
}

/// The dotted source spelling of an expression that is a plain member chain
/// (`p.pos.y`), for diagnostics that want to name what they're rejecting.
/// Empty for any other expression shape — a computed key has no single
/// spelling to quote, so its diagnostics fall back to generic wording.
std::string member_chain_spelling(const ExprNode& expr) {
    const auto* member = std::get_if<MemberExpr>(&expr.expr);
    if (member == nullptr) {
        return {};
    }
    const auto chain = member_chain_segments(*member);
    if (!chain.has_value()) {
        return {};
    }
    std::string joined;
    for (const auto& segment : *chain) {
        if (!joined.empty()) {
            joined += '.';
        }
        joined += segment;
    }
    return joined;
}

/// The binding a member-chain sort key reads through, or empty when the key
/// isn't rooted at a plain identifier. Takes the key's already-computed
/// member_chain_spelling so a caller that needs both doesn't walk the chain
/// twice.
std::string member_chain_root(const std::string& spelling) {
    const auto dot = spelling.find('.');
    return dot == std::string::npos ? spelling : spelling.substr(0, dot);
}

// Reports the out-of-scope-binding diagnostic for a sort key rooted at a name
// the rule's domain doesn't declare. Returns true when it reported, so the
// caller stops before type inference produces a vaguer second complaint.
bool SemanticAnalyzer::report_unbound_sort_key_root(
    const SortKey& key,
    const std::string& spelling,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const PairScope* pair_scope,
    const std::string& rule_name) {
    const auto root = member_chain_root(spelling);
    if (root.empty()) {
        return false;
    }
    if (pair_scope != nullptr && !pair_scope->contains(root)) {
        errors_.error(key.location, "'" + root + "' is not a declared pair binding in rule '" + rule_name + "'");
        return true;
    }
    if (pair_scope == nullptr && !filter_bindings.contains(root)) {
        errors_.error(key.location, "sort key alias '" + root + "' is not declared in filter:");
        return true;
    }
    return false;
}

void SemanticAnalyzer::validate_order_by_key(
    const SortKey& key,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const PairScope* pair_scope,
    const std::string& rule_name) {
    // Computed once and threaded through below so scope-checking and the
    // eventual diagnostics don't each re-walk the same small chain.
    const auto spelling = member_chain_spelling(*key.expression);

    // Scope first: a key naming an undeclared binding gets that specific
    // dsl-rule-order-by diagnostic rather than a vaguer "not orderable" one
    // once type inference gives up on it.
    if (report_unbound_sort_key_root(key, spelling, filter_bindings, pair_scope, rule_name)) {
        return;
    }

    // Purity before typing: an impure key is invalid whatever its type, and
    // typing it first would surface its own diagnostics (e.g. a query's
    // "requires world access") ahead of the more fundamental rejection.
    const auto error_count_before = errors_.error_count();
    check_order_by_purity_expr(*key.expression);
    if (errors_.error_count() > error_count_before) {
        return;
    }

    const auto type = infer_expr_type(*key.expression, filter_bindings, {}, nullptr, pair_scope);
    if (type.kind == TypeKind::Int || type.kind == TypeKind::Float || type.kind == TypeKind::Bool) {
        return;
    }

    if (type.kind == TypeKind::Unknown) {
        // A genuinely unresolvable reference was already reported by
        // infer_expr_type; a chain that resolves partway then hits an
        // unsupported member (e.g. `.z` on a vec2) reports nothing on its own,
        // so name it here.
        if (errors_.error_count() == error_count_before && !spelling.empty()) {
            errors_.error(key.location,
                          "order by field '" + spelling + "' is not valid for the referenced trait");
        }
        return;
    }

    const std::string subject = spelling.empty() ? "expression" : "'" + spelling + "'";
    std::string message       = "sort key " + subject + " has type '" + type.name + "' which is not orderable";
    if ((type.kind == TypeKind::Vec2 || type.kind == TypeKind::Vec3) && !spelling.empty()) {
        message += "; use a scalar field or member (e.g., '" + spelling + ".y')";
    }
    errors_.error(key.location, message);
}

// Shared by both validateOrderByClause overloads for the unary-filter-domain
// case (a pairs: domain only exists on RuleNode, so that branch stays on the
// RuleNode overload). Assumes rule.order_by is already known non-empty.
void SemanticAnalyzer::validate_unary_order_by(const FilterClause& filter,
                                               const std::vector<SortKey>& order_by,
                                               const SourceLocation& location,
                                               const std::string& rule_name) {
    if (filter.entries.empty() && filter.trait_names.empty()) {
        errors_.error(location, "rule '" + rule_name + "': `order by:` requires a `filter:` or `pairs:` clause");
        return;
    }

    auto filter_bindings = build_filter_bindings(filter);
    for (const auto& key : order_by) {
        validate_order_by_key(key, filter_bindings, nullptr, rule_name);
    }
}

void SemanticAnalyzer::validateOrderByClause(const RuleNode& rule) {
    if (rule.order_by.empty()) {
        return;
    }

    if (rule.pairs.has_value()) {
        auto pair_scope = build_pair_scope(*rule.pairs);
        for (const auto& key : rule.order_by) {
            validate_order_by_key(key, {}, &pair_scope, rule.name);
        }
        return;
    }

    validate_unary_order_by(rule.filter, rule.order_by, rule.location, rule.name);
}

void SemanticAnalyzer::validateOrderByClause(const ExternRuleNode& rule) {
    if (rule.order_by.empty()) {
        return;
    }

    validate_unary_order_by(rule.filter, rule.order_by, rule.location, rule.name);
}

SemanticAnalyzer::PhaseCollection SemanticAnalyzer::collect_phase_declarations(ProgramNode& program) {
    PhaseCollection phases;
    for (auto& declaration : program.declarations) {
        if (auto* phase = std::get_if<PhaseNode>(&declaration)) {
            phases.local_phases[phase->name] = phase;
            phases.phase_order.push_back(phase->name);
        } else if (auto* block = std::get_if<ConstBlockNode>(&declaration)) {
            for (const auto& assignment : block->assignments) {
                phases.constants[assignment.name] = assignment.value.get();
            }
        }
    }
    return phases;
}

// 40 after extraction from validate_phase_declarations (task 6.10); still
// validates 4 independent clauses (from:/after:/every:/max:) per phase.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::validate_phase_from_after_every_max(const PhaseCollection& phases) {
    for (const auto& name : phases.phase_order) {
        auto* phase = phases.local_phases.at(name);
        if (phase->from_sources.empty() && phase->after_phases.empty()) {
            errors_.error(phase->location,
                          "phase '" + phase->name + "' requires a non-empty 'from:' or 'after:' block");
        }
        for (const auto& source : phase->from_sources) {
            const auto resolved = resolve_name(dotted_segments(source.spelling));
            if (!resolved.has_value() || !resolved->member_segments.empty()) {
                (void)resolve_name_required(dotted_segments(source.spelling), source.location);
                continue;
            }
            if (resolved->symbol.kind != SymbolKind::Event) {
                errors_.error(source.location,
                              "phase 'from:' dependency '" + source.spelling +
                                  "' must reference an external event, got " + symbol_kind_name(resolved->symbol.kind));
            } else if (!is_external_event(resolved->symbol)) {
                errors_.error(source.location,
                              "phase 'from:' dependency '" + source.spelling + "' must reference an external event");
            }
        }
        for (const auto& dependency : phase->after_phases) {
            const auto resolved = resolve_name(dotted_segments(dependency.spelling));
            if (!resolved.has_value() || !resolved->member_segments.empty()) {
                (void)resolve_name_required(dotted_segments(dependency.spelling), dependency.location);
                continue;
            }
            if (resolved->symbol.kind != SymbolKind::Phase) {
                errors_.error(dependency.location,
                              "phase 'after:' dependency '" + dependency.spelling + "' must reference a phase, got " +
                                  symbol_kind_name(resolved->symbol.kind));
            }
        }

        auto& resolved_phase = result_.phases.at(name);
        if (phase->max.has_value() && !phase->every.has_value()) {
            errors_.error(phase->location, "phase '" + name + "' may declare 'max:' only together with 'every:'");
        }
        if (phase->every.has_value()) {
            std::unordered_set<std::string> evaluating;
            const auto value = evaluate_numeric_constant(**phase->every, phases.constants, evaluating);
            if (!value.has_value() || value->kind != TypeKind::Float || !std::isfinite(value->value) ||
                value->value <= 0.0L) {
                errors_.error((**phase->every).location,
                              "phase '" + name + "' every value must be a positive compile-time float");
            } else {
                resolved_phase.every_seconds = static_cast<double>(value->value);
            }
        }
        if (phase->max.has_value()) {
            std::unordered_set<std::string> evaluating;
            const auto value = evaluate_numeric_constant(**phase->max, phases.constants, evaluating);
            if (!value.has_value() || value->kind != TypeKind::Int || value->value <= 0.0L ||
                value->value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
                errors_.error((**phase->max).location,
                              "phase '" + name + "' max value must be a positive compile-time integer");
            } else {
                resolved_phase.max_repetitions = static_cast<std::int64_t>(value->value);
            }
        }
    }
}

// 48 after extraction from validate_phase_declarations (task 6.10); the DFS
// (cycle detection, runtime-root union, ambiguity reporting) is one cohesive
// algorithm, not further decomposable without threading more state around.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::resolve_phase_lineage(const PhaseCollection& phases) {
    enum class Visit : std::uint8_t { Visiting, Done };
    std::unordered_map<std::string, Visit> visit;
    std::vector<std::string> stack;
    std::unordered_set<std::string> reported_cycles;
    std::function<void(const std::string&)> resolve_lineage = [&](const std::string& name) {
        if (visit.contains(name)) {
            if (visit.at(name) == Visit::Visiting) {
                const auto cycle_start = std::ranges::find(stack, name);
                std::ostringstream path;
                for (auto it = cycle_start; it != stack.end(); ++it) {
                    if (it != cycle_start) {
                        path << " -> ";
                    }
                    path << make_canonical_id(current_module_name_, *it);
                }
                path << " -> " << make_canonical_id(current_module_name_, name);
                if (reported_cycles.insert(path.str()).second) {
                    errors_.error(phases.local_phases.at(name)->location, "phase cycle: " + path.str());
                }
            }
            return;
        }

        visit[name] = Visit::Visiting;
        stack.push_back(name);
        auto& resolved_phase = result_.phases.at(name);
        std::unordered_set<SymbolId, SymbolIdHash> roots;
        std::unordered_set<SymbolId, SymbolIdHash> upstream;
        for (const auto& source : phases.local_phases.at(name)->resolved_from) {
            if (is_external_event(source.symbol)) {
                roots.insert(source.symbol);
            }
        }
        for (const auto& dependency : phases.local_phases.at(name)->resolved_after) {
            const auto& symbol = dependency.symbol;
            upstream.insert(symbol);
            if (symbol.module == current_module_id_) {
                resolve_lineage(symbol.local_name);
                const auto& local = result_.phases.at(symbol.local_name);
                if (local.runtime_root.has_value()) {
                    roots.insert(*local.runtime_root);
                }
                upstream.insert(local.upstream_phases.begin(), local.upstream_phases.end());
            } else if (const auto* imported = find_imported_phase(symbol); imported != nullptr) {
                if (imported->runtime_root.has_value()) {
                    roots.insert(*imported->runtime_root);
                } else {
                    errors_.error(phases.local_phases.at(name)->location,
                                  "imported phase '" + make_canonical_id(symbol) +
                                      "' has no runtime-root lineage metadata; recompile its module");
                }
                upstream.insert(imported->upstream_phases.begin(), imported->upstream_phases.end());
            }
        }
        resolved_phase.upstream_phases.assign(upstream.begin(), upstream.end());
        std::ranges::sort(resolved_phase.upstream_phases, [](const SymbolId& lhs, const SymbolId& rhs) {
            return make_canonical_id(lhs) < make_canonical_id(rhs);
        });
        if (roots.size() == 1) {
            resolved_phase.runtime_root = *roots.begin();
        } else if (roots.size() > 1) {
            std::vector<std::string> canonical_roots;
            canonical_roots.reserve(roots.size());
            for (const auto& root : roots) {
                canonical_roots.push_back(make_canonical_id(root));
            }
            std::ranges::sort(canonical_roots);
            std::ostringstream message;
            message << "phase '" << make_canonical_id(current_module_name_, name)
                    << "' has ambiguous runtime-root lineage:";
            for (const auto& root : canonical_roots) {
                message << " '" << root << "'";
            }
            errors_.error(phases.local_phases.at(name)->location, message.str());
        }
        stack.pop_back();
        visit[name] = Visit::Done;
    };
    for (const auto& name : phases.phase_order) {
        resolve_lineage(name);
    }
}

// 27 after extraction from validate_phase_declarations (task 6.10); just over
// threshold, from the dt/alpha synthesis plus its runtime-root dt-field check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::synthesize_phase_periodic_fields(const PhaseCollection& phases) {
    // Synthesize all periodic outputs before checking any downstream
    // initializer. This makes field availability follow the phase DAG rather
    // than unordered-map iteration order (for example, render may consume
    // fixed_tick.alpha regardless of declaration/container traversal order).
    for (const auto& name : phases.phase_order) {
        auto* phase          = phases.local_phases.at(name);
        auto& resolved_phase = result_.phases.at(name);
        std::unordered_set<std::string> field_names;
        for (const auto& field : phase->fields) {
            if (!field_names.insert(field.name).second) {
                errors_.error(field.location, "duplicate field '" + field.name + "' in phase '" + name + "'");
            }
        }
        if (resolved_phase.every_seconds.has_value()) {
            for (const auto* synthetic_name : {"dt", "alpha"}) {
                if (field_names.contains(synthetic_name)) {
                    errors_.error(
                        phase->location,
                        "periodic phase '" + name + "' cannot declare synthesized field '" + synthetic_name + "'");
                    continue;
                }
                ResolvedField synthetic;
                synthetic.name               = synthetic_name;
                synthetic.type               = make_float_type();
                synthetic.is_let             = true;
                synthetic.has_default        = true;
                synthetic.is_synthesized     = true;
                synthetic.is_completion_only = std::string_view{synthetic_name} == "alpha";
                resolved_phase.fields.push_back(std::move(synthetic));
            }
            if (resolved_phase.runtime_root.has_value()) {
                const auto* root_fields = find_event_fields(*resolved_phase.runtime_root);
                const auto* root_dt     = root_fields == nullptr ? nullptr : find_field_in(*root_fields, "dt");
                if (root_dt == nullptr || root_dt->type.kind != TypeKind::Float) {
                    errors_.error(phase->location,
                                  "periodic phase '" + name + "' requires runtime root '" +
                                      make_canonical_id(*resolved_phase.runtime_root) +
                                      "' to provide float field 'dt'");
                }
            }
        }
    }
}

// 69 after extraction from validate_phase_declarations (task 6.10); the
// recursive infer_phase_expr closure (literal/unary/binary/member-chain type
// inference) plus the per-field validate+bind loop that reuses it.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::validate_phase_field_initializers(const PhaseCollection& phases) {
    for (const auto& name : phases.phase_order) {
        auto* phase          = phases.local_phases.at(name);
        auto& resolved_phase = result_.phases.at(name);
        std::unordered_set<SymbolId, SymbolIdHash> allowed_phases(resolved_phase.upstream_phases.begin(),
                                                                  resolved_phase.upstream_phases.end());
        std::function<TypeInfo(const ExprNode&)> infer_phase_expr = [&](const ExprNode& expr) -> TypeInfo {
            if (const auto* literal = std::get_if<LiteralExpr>(&expr.expr)) {
                switch (literal->kind) {
                    case LiteralExpr::Kind::Int:
                        return make_int_type();
                    case LiteralExpr::Kind::Float:
                        return make_float_type();
                    case LiteralExpr::Kind::String:
                        return make_string_type();
                    case LiteralExpr::Kind::HexColor:
                        return make_color_type();
                    case LiteralExpr::Kind::Bool:
                        return make_bool_type();
                }
            }
            if (const auto* unary = std::get_if<UnaryExpr>(&expr.expr)) {
                return infer_phase_expr(*unary->operand);
            }
            if (const auto* binary = std::get_if<BinaryExpr>(&expr.expr)) {
                const auto left  = infer_phase_expr(*binary->left);
                const auto right = infer_phase_expr(*binary->right);
                if (left.kind == TypeKind::Float || right.kind == TypeKind::Float) {
                    return make_float_type();
                }
                return left.kind == TypeKind::Int && right.kind == TypeKind::Int ? make_int_type()
                                                                                 : make_unknown_type();
            }
            const auto* member  = std::get_if<MemberExpr>(&expr.expr);
            const auto segments = member == nullptr ? std::nullopt : member_chain_segments(*member);
            if (!segments.has_value()) {
                errors_.error(expr.location, "phase field initializer must read upstream activation data");
                return make_unknown_type();
            }
            const auto resolved = resolve_name(*segments);
            if (!resolved.has_value() || resolved->member_segments.size() != 1) {
                (void)resolve_name_required(*segments, expr.location);
                return make_unknown_type();
            }
            const auto& source_symbol                = resolved->symbol;
            const auto& member_name                  = resolved->member_segments.front();
            const std::vector<ResolvedField>* fields = nullptr;
            if (source_symbol.kind == SymbolKind::Event && resolved_phase.runtime_root == source_symbol) {
                fields = find_event_fields(source_symbol);
            } else if (source_symbol.kind == SymbolKind::Phase && allowed_phases.contains(source_symbol)) {
                fields = find_phase_fields(source_symbol);
            } else {
                errors_.error(expr.location,
                              "phase '" + name + "' initializer cannot read non-upstream value '" +
                                  make_canonical_id(source_symbol) + "'");
                return make_unknown_type();
            }
            const auto* source_field = fields == nullptr ? nullptr : find_field_in(*fields, member_name);
            if (source_field == nullptr) {
                errors_.error(
                    expr.location,
                    "unknown activation field '" + member_name + "' on '" + make_canonical_id(source_symbol) + "'");
                return make_unknown_type();
            }
            return source_field->type;
        };

        for (std::size_t index = 0; index < phase->fields.size(); ++index) {
            // dsl-render-passes: a render-pass phase's Pass/Target descriptor
            // fields are not upstream-activation reads at all — they're
            // compile-time-constant enum literals, validated separately by
            // validate_render_pass_descriptor_fields once resolved_enum_member
            // is available (see analyze()'s ordering comment).
            if (resolved_phase.render_pass.has_value() &&
                (index == resolved_phase.render_pass->pass_field_index ||
                 index == resolved_phase.render_pass->target_field_index)) {
                continue;
            }
            auto& field_node               = phase->fields[index];
            const auto* member_initializer = std::get_if<MemberExpr>(&field_node.initializer->expr);
            if (member_initializer == nullptr) {
                errors_.error(field_node.location,
                              "phase field '" + name + "." + field_node.name +
                                  "' initializer must be a plain member-chain read of upstream activation data");
                continue;
            }

            const auto actual   = infer_phase_expr(*field_node.initializer);
            const auto expected = resolved_phase.fields[index].type;
            if (actual.kind != TypeKind::Unknown && expected.kind != TypeKind::Unknown &&
                !same_type(actual, expected)) {
                errors_.error(field_node.location,
                              "phase field '" + name + "." + field_node.name + "' has type '" + expected.name +
                                  "' but initializer has type '" + actual.name + "'");
            }

            // Re-resolve the member chain (infer_phase_expr already validated it) so the
            // binding can be recorded for codegen without threading an out-parameter
            // through the recursive type-inference closure.
            const auto segments = member_chain_segments(*member_initializer);
            if (!segments.has_value()) {
                continue;
            }
            const auto resolved = resolve_name(*segments);
            if (!resolved.has_value() || resolved->member_segments.size() != 1) {
                continue;
            }
            const auto& source_symbol = resolved->symbol;
            const auto& member_name   = resolved->member_segments.front();
            if (source_symbol.kind == SymbolKind::Event && resolved_phase.runtime_root == source_symbol) {
                resolved_phase.fields[index].source_binding = PhaseFieldSource{
                    .kind = PhaseFieldSource::Kind::RootEvent, .source = source_symbol, .member = member_name};
            } else if (source_symbol.kind == SymbolKind::Phase && allowed_phases.contains(source_symbol)) {
                resolved_phase.fields[index].source_binding = PhaseFieldSource{
                    .kind = PhaseFieldSource::Kind::UpstreamPhase, .source = source_symbol, .member = member_name};
            }
        }
    }
}

void SemanticAnalyzer::validate_phase_declarations(ProgramNode& program) {
    const auto phases = collect_phase_declarations(program);
    validate_phase_from_after_every_max(phases);
    resolve_phase_lineage(phases);
    synthesize_phase_periodic_fields(phases);
    validate_phase_field_initializers(phases);
}

// dsl-render-passes: a phase is recognized as a render-pass phase when one of
// its fields resolves to canonical type std.render.passes.Pass (field name
// insignificant — recognition is by resolved type identity). Runs right after
// collect_types (see analyze()'s ordering comment) so field types are already
// resolved but before resolve_all_types resolves ordinary handler triggers.
void SemanticAnalyzer::recognize_render_pass_phases(ProgramNode& program) {
    static const auto pass_enum_id   = make_symbol_id(SymbolKind::Enum, "std.render.passes", "Pass");
    static const auto target_enum_id = make_symbol_id(SymbolKind::Enum, "std.render.passes", "Target");

    for (auto& decl : program.declarations) {
        auto* phase = std::get_if<PhaseNode>(&decl);
        if (phase == nullptr) {
            continue;
        }
        const auto phase_it = result_.phases.find(phase->name);
        if (phase_it == result_.phases.end()) {
            continue;
        }
        auto& resolved_phase = phase_it->second;

        std::optional<std::size_t> pass_field_index;
        std::optional<std::size_t> target_field_index;
        for (std::size_t index = 0; index < resolved_phase.fields.size(); ++index) {
            const auto& field_type = resolved_phase.fields[index].type;
            if (field_type.kind != TypeKind::Enum || !field_type.symbol_id.has_value()) {
                continue;
            }
            if (*field_type.symbol_id == pass_enum_id) {
                pass_field_index = index;
            } else if (*field_type.symbol_id == target_enum_id) {
                target_field_index = index;
            }
        }
        if (!pass_field_index.has_value()) {
            continue;  // ordinary phase — no render-pass recognition
        }
        if (!target_field_index.has_value()) {
            errors_.error(phase->location,
                          "render-pass phase '" + phase->name +
                              "' declares a std.render.passes.Pass field but no std.render.passes.Target field");
            continue;
        }

        const auto& phase_symbol = *phase->resolved_phase_id;
        RenderPassInfo info;
        info.pass_field_index   = *pass_field_index;
        info.target_field_index = *target_field_index;
        info.pass_enum_id       = pass_enum_id;
        info.target_enum_id     = target_enum_id;
        info.vertex_trigger     = ResolvedHandlerTrigger{
            .kind   = HandlerTriggerKind::RenderStage,
            .symbol = make_symbol_id(SymbolKind::Phase, phase_symbol.module, phase_symbol.local_name + "__vertex")};
        info.fragment_trigger = ResolvedHandlerTrigger{
            .kind   = HandlerTriggerKind::RenderStage,
            .symbol = make_symbol_id(SymbolKind::Phase, phase_symbol.module, phase_symbol.local_name + "__fragment")};
        resolved_phase.render_pass = std::move(info);
    }
}

// dsl-render-passes: validates that a render-pass phase's Pass/Target
// descriptor fields are initialized with a compile-time-constant enum-variant
// literal of the expected type. Runs after resolve_all_types (see analyze()'s
// ordering comment) so MemberExpr::resolved_enum_member is already populated.
void SemanticAnalyzer::validate_render_pass_descriptor_fields(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        auto* phase = std::get_if<PhaseNode>(&decl);
        if (phase == nullptr) {
            continue;
        }
        const auto phase_it = result_.phases.find(phase->name);
        if (phase_it == result_.phases.end() || !phase_it->second.render_pass.has_value()) {
            continue;
        }
        const auto& info = *phase_it->second.render_pass;

        const auto validate_descriptor_field = [&](std::size_t field_index, const SymbolId& expected_enum) {
            auto& field_node    = phase->fields[field_index];
            const auto* member  = std::get_if<MemberExpr>(&field_node.initializer->expr);
            const bool is_valid = member != nullptr && member->resolved_enum_member.has_value() &&
                                  member->resolved_enum_member->enum_id == expected_enum;
            if (!is_valid) {
                errors_.error(field_node.location,
                              "phase field '" + phase->name + "." + field_node.name +
                                  "' must be initialized with a compile-time-constant '" +
                                  make_canonical_id(expected_enum) + "' enum-variant literal");
            }
        };
        validate_descriptor_field(info.pass_field_index, info.pass_enum_id);
        validate_descriptor_field(info.target_field_index, info.target_enum_id);
    }
}

void SemanticAnalyzer::validate_render_pass_stage_handler_shape(const RuleNode& rule,
                                                                 const EventHandlerNode& handler,
                                                                 bool is_vertex) const {
    if (!handler.alias.has_value()) {
        errors_.error(handler.location,
                      "render-pass stage handler must bind an alias (e.g. 'as v') to access its built-in fields");
    }
    if (is_vertex) {
        const bool has_filter = !rule.filter.entries.empty() || !rule.filter.trait_names.empty();
        if (rule.pairs.has_value()) {
            errors_.error(rule.location,
                          "render-pass vertex-stage handler cannot use 'pairs:'; it requires a unary 'filter:'");
        } else if (!has_filter) {
            errors_.error(rule.location, "render-pass vertex-stage handler requires an instance domain ('filter:')");
        }
        return;
    }
    const bool has_filter  = !rule.filter.entries.empty() || !rule.filter.trait_names.empty();
    const bool has_exclude = !rule.exclude.entries.empty() || !rule.exclude.trait_names.empty();
    if (has_filter || has_exclude || rule.pairs.has_value() || rule.where_clause.has_value()) {
        errors_.error(rule.location,
                      "render-pass fragment-stage handler is selectionless: 'filter:'/'exclude:'/'pairs:'/'where:' "
                      "are not allowed");
    }
}

// dsl-render-passes: validates the restricted statement/expression subset a
// stage handler body may use (design.md Decision 3): `let`, assignment to
// invocation-local variables and the handler's own writable built-in output
// fields, `if`/`else`, and calls to `func`/a registered-portable-GLSL `extern
// func`. Everything else — spawn/destroy/add/remove/project/emit, world
// queries, bounded `for`, trait-match, and writes to a filtered trait — is
// rejected with a diagnostic naming the offending statement.
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- single consolidated statement+expression walker
void SemanticAnalyzer::validate_render_pass_stage_handler_body(
    const std::vector<std::unique_ptr<StmtNode>>& body,
    const std::string& alias,
    const std::unordered_set<std::string>& filter_alias_names,
    const std::unordered_set<std::string>& writable_output_fields,
    const char* stage_desc) const {
    std::function<void(const ExprNode&)> visit_expr;
    std::function<void(const std::vector<std::unique_ptr<StmtNode>>&)> visit_stmts;

    visit_expr = [&](const ExprNode& expr) {
        std::visit(
            [&](const auto& node) {
                using E = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<E, QueryCallExpr>) {
                    errors_.error(expr.location,
                                  std::string("a world query is not allowed in a render-pass ") + stage_desc +
                                      "-stage handler body");
                } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                    errors_.error(expr.location,
                                  std::string("'spawn' is not allowed in a render-pass ") + stage_desc +
                                      "-stage handler body");
                } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                    visit_expr(*node.left);
                    visit_expr(*node.right);
                } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                    visit_expr(*node.operand);
                } else if constexpr (std::is_same_v<E, CallExpr>) {
                    const auto* callee_ident = std::get_if<IdentExpr>(&node.callee->expr);
                    const bool is_vector_ctor =
                        callee_ident != nullptr &&
                        (callee_ident->name == "vec2" || callee_ident->name == "vec3" || callee_ident->name == "color");
                    if (!is_vector_ctor) {
                        auto callee_symbol = resolve_callee_symbol(*node.callee);
                        const auto* func    = callee_symbol.has_value() ? find_resolved_func(*callee_symbol) : nullptr;
                        if (func != nullptr && func->is_extern &&
                            !is_render_pass_portable_glsl_intrinsic(*callee_symbol)) {
                            errors_.error(expr.location,
                                          "extern func '" + make_canonical_id(*callee_symbol) +
                                              "' has no registered portable GLSL translation for a render-pass "
                                              "stage handler");
                        }
                    }
                    for (const auto& arg : node.args) {
                        visit_expr(*arg);
                    }
                } else if constexpr (std::is_same_v<E, MemberExpr>) {
                    visit_expr(*node.object);
                } else if constexpr (std::is_same_v<E, LambdaExpr>) {
                    visit_expr(*node.body);
                } else if constexpr (std::is_same_v<E, PipelineExpr>) {
                    visit_expr(*node.source);
                    for (const auto& operation : node.operations) {
                        for (const auto& arg : operation.args) {
                            visit_expr(*arg);
                        }
                    }
                } else if constexpr (std::is_same_v<E, MatchExpr>) {
                    visit_expr(*node.subject);
                    for (const auto& arm : node.arms) {
                        visit_expr(*arm.pattern);
                        visit_expr(*arm.body);
                    }
                } else if constexpr (std::is_same_v<E, IfExpr>) {
                    visit_expr(*node.condition);
                    visit_expr(*node.then_expr);
                    visit_expr(*node.else_expr);
                } else if constexpr (std::is_same_v<E, ListExpr>) {
                    for (const auto& element : node.elements) {
                        visit_expr(*element);
                    }
                }
                // LiteralExpr, IdentExpr, SelfExpr: no sub-expressions to visit.
            },
            expr.expr);
    };

    const auto forbid_statement = [&](const SourceLocation& loc, const char* keyword) {
        errors_.error(loc,
                      std::string("'") + keyword + "' is not allowed in a render-pass " + stage_desc +
                          "-stage handler body");
    };

    visit_stmts = [&](const std::vector<std::unique_ptr<StmtNode>>& stmts) {
        for (const auto& stmt : stmts) {
            std::visit(
                [&](const auto& node) {
                    using S = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<S, LetStmt>) {
                        visit_expr(*node.value);
                    } else if constexpr (std::is_same_v<S, VarAssign>) {
                        visit_expr(*node.value);
                        if (node.name == alias) {
                            if (node.path.size() != 1 || !writable_output_fields.contains(node.path.front())) {
                                errors_.error(node.location,
                                              std::string("render-pass ") + stage_desc +
                                                  "-stage handler may only assign to its own built-in output "
                                                  "field(s)");
                            }
                        } else if (filter_alias_names.contains(node.name)) {
                            errors_.error(node.location,
                                          "only built-in stage-output fields are writable; '" + node.name +
                                              "' is a filtered trait binding");
                        }
                    } else if constexpr (std::is_same_v<S, IfStmt>) {
                        visit_expr(*node.condition);
                        visit_stmts(node.then_body);
                        for (const auto& branch : node.else_if_branches) {
                            visit_expr(*branch.condition);
                            visit_stmts(branch.body);
                        }
                        visit_stmts(node.else_body);
                    } else if constexpr (std::is_same_v<S, ExprStmt>) {
                        visit_expr(*node.expr);
                    } else if constexpr (std::is_same_v<S, EmitStmt>) {
                        forbid_statement(node.location, "emit");
                    } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                        forbid_statement(node.location, "spawn");
                    } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                        forbid_statement(node.location, "destroy");
                    } else if constexpr (std::is_same_v<S, LoadStmt>) {
                        forbid_statement(node.location, "load");
                    } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                        forbid_statement(node.location, "add");
                    } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                        forbid_statement(node.location, "remove");
                    } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                        forbid_statement(node.location, "project");
                    } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                        forbid_statement(node.location, "return");
                    } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                        forbid_statement(node.location, "for");
                    } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                        forbid_statement(node.location, "match");
                    }
                },
                stmt->stmt);
        }
    };

    visit_stmts(body);
}

// dsl-render-passes: finds each render-pass phase's vertex/fragment stage
// handlers, validates cardinality (exactly one of each), and — for a phase
// whose cardinality is valid — validates that handler's domain shape and body.
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- cardinality scan plus per-stage validation dispatch
void SemanticAnalyzer::validate_render_pass_stage_handlers(ProgramNode& program) {
    using StageMatch = std::pair<RuleNode*, EventHandlerNode*>;

    const auto validate_cardinality = [this](const PhaseNode& phase,
                                             const char* stage,
                                             const std::vector<StageMatch>& matches) -> const StageMatch* {
        if (matches.empty()) {
            errors_.error(phase.location,
                          "render-pass phase '" + phase.name + "' has no '" + std::string(stage) + "' stage handler");
            return nullptr;
        }
        if (matches.size() > 1) {
            std::string names;
            for (const auto& [rule, handler] : matches) {
                if (!names.empty()) {
                    names += ", ";
                }
                names += make_canonical_id(*rule->resolved_rule_id) + "/on " + phase.name + "." + stage;
            }
            errors_.error(matches.front().second->location,
                          "render-pass phase '" + phase.name + "' has duplicate '" + std::string(stage) +
                              "' stage handlers: " + names);
            return nullptr;
        }
        return &matches.front();
    };

    for (auto& decl : program.declarations) {
        auto* phase = std::get_if<PhaseNode>(&decl);
        if (phase == nullptr) {
            continue;
        }
        const auto phase_it = result_.phases.find(phase->name);
        if (phase_it == result_.phases.end() || !phase_it->second.render_pass.has_value()) {
            continue;
        }
        const auto& info = *phase_it->second.render_pass;

        std::vector<StageMatch> vertex_matches;
        std::vector<StageMatch> fragment_matches;
        for (auto& other_decl : program.declarations) {
            auto* rule = std::get_if<RuleNode>(&other_decl);
            if (rule == nullptr) {
                continue;
            }
            for (auto& handler : rule->handlers) {
                if (!handler.resolved_trigger.has_value()) {
                    continue;
                }
                if (*handler.resolved_trigger == info.vertex_trigger) {
                    vertex_matches.emplace_back(rule, &handler);
                } else if (*handler.resolved_trigger == info.fragment_trigger) {
                    fragment_matches.emplace_back(rule, &handler);
                }
            }
        }

        static const std::unordered_set<std::string> kVertexOutputs   = {"screen_position", "uv_out", "tint_out"};
        static const std::unordered_set<std::string> kFragmentOutputs = {"frag_color"};

        const auto* vertex_match   = validate_cardinality(*phase, "vertex", vertex_matches);
        const auto* fragment_match = validate_cardinality(*phase, "fragment", fragment_matches);
        if (vertex_match != nullptr) {
            const auto& [rule, handler] = *vertex_match;
            validate_render_pass_stage_handler_shape(*rule, *handler, true);
            if (handler->alias.has_value()) {
                std::unordered_set<std::string> filter_alias_names;
                for (const auto& entry : rule->filter.entries) {
                    filter_alias_names.insert(entry.alias.value_or(
                        entry.resolved_trait_id.has_value() ? entry.resolved_trait_id->local_name
                                                            : entry.qualified_name));
                }
                filter_alias_names.insert(rule->filter.trait_names.begin(), rule->filter.trait_names.end());
                validate_render_pass_stage_handler_body(
                    handler->body, *handler->alias, filter_alias_names, kVertexOutputs, "vertex");
            }
        }
        if (fragment_match != nullptr) {
            const auto& [rule, handler] = *fragment_match;
            validate_render_pass_stage_handler_shape(*rule, *handler, false);
            if (handler->alias.has_value()) {
                validate_render_pass_stage_handler_body(handler->body, *handler->alias, {}, kFragmentOutputs, "fragment");
            }
        }

        // dsl-render-passes, "Render-pass synthetic pass-local edges": record
        // the pass-local vertex/fragment relationship once both stage
        // handlers are unambiguously identified, regardless of any further
        // shape/body diagnostics above (codegen never runs on a program with
        // errors, so a partially-valid entry here is harmless).
        if (vertex_match != nullptr && fragment_match != nullptr) {
            result_.execution_graph.render_passes.push_back(
                RenderPassPlan{.phase           = *phase->resolved_phase_id,
                               .vertex_handler   = HandlerIdentity{.rule    = *vertex_match->first->resolved_rule_id,
                                                                  .trigger = info.vertex_trigger},
                               .fragment_handler = HandlerIdentity{.rule = *fragment_match->first->resolved_rule_id,
                                                                   .trigger = info.fragment_trigger}});
        }
    }
}

void SemanticAnalyzer::validate_filter_clause_traits(FilterClause& filter, const std::string& owner_desc) {
    if (!filter.entries.empty()) {
        // Rich filter entries (multi-module parser path)
        for (auto& entry : filter.entries) {
            std::string simple_name;
            resolve_filter_entry(entry, simple_name);
        }
    } else {
        // Backward-compat: simple trait_names list
        for (auto& trait_name : filter.trait_names) {
            // Local trait — always valid
            if (trait_names_.contains(trait_name)) {
                continue;
            }
            const auto prev_errors = errors_.error_count();
            const auto canonical   = resolve_trait_ref_to_canonical(trait_name, filter.location);
            if (canonical.empty() && errors_.error_count() == prev_errors) {
                errors_.error(filter.location, owner_desc + " filters on unknown trait '" + trait_name + "'");
            }
        }
    }
}

void SemanticAnalyzer::validate_rule_filters(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* rule = std::get_if<RuleNode>(&decl)) {
            validate_filter_clause_traits(rule->filter, "rule '" + rule->name + "'");

            // task 11.12: if rule has no filter traits, handler bodies cannot
            // access trait fields (VarAssign is always a trait-field mutation).
            // Pair rules get their own read-only/no-implicit-entity diagnostics
            // instead of this generic "no filter clause" message.
            bool has_filter = !rule->filter.entries.empty() || !rule->filter.trait_names.empty();
            if (!has_filter && !rule->pairs.has_value()) {
                for (auto& handler : rule->handlers) {
                    // dsl-render-passes: a fragment-stage handler is selectionless
                    // (no filter:) by design, but legitimately accesses its own
                    // built-in fields through its alias — validated separately by
                    // validate_render_pass_stage_handler_body, not this generic
                    // "no entity to access fields on" check.
                    if (handler.resolved_trigger.has_value() &&
                        handler.resolved_trigger->kind == HandlerTriggerKind::RenderStage) {
                        continue;
                    }
                    check_no_field_access(handler.body, rule->name);
                }
            }

            validate_pair_bindings(*rule);
            validateOrderByClause(*rule);
        }
        if (auto* rule = std::get_if<ExternRuleNode>(&decl)) {
            validate_filter_clause_traits(rule->filter, "extern rule '" + rule->name + "'");
            validateOrderByClause(*rule);
        }
    }
}

// ── Phase 3e: Where Clause Validation (dsl-where-clause) ───────────────────

// Structurally identical to check_func_purity_expr's deny-list walk, applied
// to a where: predicate expression instead of a func body statement. Unlike
// func bodies, where: predicates are parsed as bare expressions, so the
// statement-level impure forms (emit, destroy, add, remove, project, trait
// mutation) cannot even appear here — the parser rejects them before this
// runs. Only the two impure *expression* forms need an explicit check:
// QueryCallExpr (world query) and SpawnExpr (structural command); CallExpr
// purity comes from the callee's already-computed ResolvedFunc::effect_summary
// (empty = pure, non-empty or unknown = impure), the same source contract
// inference's add_call_effects already trusts.
void SemanticAnalyzer::check_where_purity_expr(const ExprNode& expr) {
    check_purity_deny_list(
        expr,
        /*on_call=*/
        [this](const CallExpr& e) {
            if (e.resolved_callee_id.has_value()) {
                const auto* function = find_resolved_func(*e.resolved_callee_id);
                if (function != nullptr && (!function->effect_summary.has_value() || !function->effect_summary->empty())) {
                    errors_.error(e.location, "where: predicates must be pure");
                }
            }
        },
        /*on_spawn=*/[this](const SpawnExpr& e) { errors_.error(e.location, "where: predicates must be pure"); },
        /*on_query=*/[this](const QueryCallExpr& e) { errors_.error(e.location, "where: predicates must be pure"); });
}

void SemanticAnalyzer::check_order_by_purity_expr(const ExprNode& expr) {
    check_purity_deny_list(
        expr,
        /*on_call=*/
        [this](const CallExpr& e) {
            if (e.resolved_callee_id.has_value()) {
                const auto* function = find_resolved_func(*e.resolved_callee_id);
                if (function != nullptr && (!function->effect_summary.has_value() || !function->effect_summary->empty())) {
                    errors_.error(e.location, "order by: sort keys must be pure");
                }
            }
        },
        /*on_spawn=*/[this](const SpawnExpr& e) { errors_.error(e.location, "order by: sort keys must be pure"); },
        /*on_query=*/
        [this](const QueryCallExpr& e) { errors_.error(e.location, "order by: sort keys must be pure"); });
}

void SemanticAnalyzer::validate_where_clauses(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        auto* rule = std::get_if<RuleNode>(&decl);
        if (rule == nullptr || !rule->where_clause.has_value()) {
            continue;
        }

        if (rule->filter.entries.empty() && !rule->pairs.has_value()) {
            errors_.error(rule->where_clause->location,
                          "'where:' requires rule '" + rule->name + "' to declare an existing 'filter:' or 'pairs:'"
                                                                    " domain");
            continue;
        }

        auto filter_bindings = build_filter_bindings(rule->filter);
        auto pair_scope       = rule->pairs.has_value() ? build_pair_scope(*rule->pairs) : PairScope{};
        const PairScope* pair_scope_ptr = rule->pairs.has_value() ? &pair_scope : nullptr;

        for (auto& predicate : rule->where_clause->predicates) {
            // Purity first: an impure predicate is invalid regardless of its
            // type, and letting infer_expr_type run first would surface its
            // own diagnostics (e.g. a query's "requires world access") ahead
            // of the more fundamental "must be pure" rejection.
            const auto error_count_before = errors_.error_count();
            check_where_purity_expr(*predicate);
            if (errors_.error_count() > error_count_before) {
                continue;
            }
            auto predicate_type = infer_expr_type(*predicate, filter_bindings, {}, nullptr, pair_scope_ptr);
            if (predicate_type.kind != TypeKind::Bool && predicate_type.kind != TypeKind::Unknown) {
                errors_.error(predicate->location, "where: predicate must be of type 'bool'");
            }
        }
    }
}

void SemanticAnalyzer::validate_external_handler_contracts(ProgramNode& program) {
    const auto valid_effect_domain = [](const std::string& domain) {
        if (domain.empty() || domain.front() == '.' || domain.back() == '.') {
            return false;
        }
        bool at_segment_start = true;
        for (const unsigned char ch : domain) {
            if (ch == '.') {
                if (at_segment_start) {
                    return false;
                }
                at_segment_start = true;
                continue;
            }
            if (at_segment_start ? !std::isalpha(ch) && ch != '_' : !std::isalnum(ch) && ch != '_') {
                return false;
            }
            at_segment_start = false;
        }
        return !at_segment_start;
    };

    for (auto& decl : program.declarations) {
        auto* rule = std::get_if<ExternRuleNode>(&decl);
        if (rule == nullptr) {
            continue;
        }
        if (rule->handlers.empty()) {
            errors_.error(rule->location,
                          "extern rule '" + rule->name + "' requires at least one external handler contract");
            continue;
        }

        std::unordered_map<std::string, SymbolId> aliases;
        const auto add_filter_aliases = [this, &aliases](const FilterClause& clause) {
            for (const auto& entry : clause.entries) {
                auto symbol = entry.resolved_trait_id.has_value()
                                  ? entry.resolved_trait_id
                                  : try_resolve_trait_ref_to_symbol(entry.qualified_name);
                if (!symbol.has_value()) {
                    continue;
                }
                const auto dot = entry.qualified_name.rfind('.');
                aliases[dot == std::string::npos ? entry.qualified_name : entry.qualified_name.substr(dot + 1)] =
                    *symbol;
                if (entry.alias.has_value()) {
                    aliases[*entry.alias] = *symbol;
                }
            }
            for (const auto& name : clause.trait_names) {
                if (auto symbol = try_resolve_trait_ref_to_symbol(name); symbol.has_value()) {
                    aliases[name] = *symbol;
                }
            }
        };
        add_filter_aliases(rule->filter);
        add_filter_aliases(rule->exclude);

        std::unordered_set<SymbolId> selected_traits;
        for (const auto& entry : rule->filter.entries) {
            const auto symbol = entry.resolved_trait_id.has_value()
                                    ? entry.resolved_trait_id
                                    : try_resolve_trait_ref_to_symbol(entry.qualified_name);
            if (symbol.has_value()) {
                selected_traits.insert(*symbol);
            }
        }
        for (const auto& name : rule->filter.trait_names) {
            if (const auto symbol = try_resolve_trait_ref_to_symbol(name); symbol.has_value()) {
                selected_traits.insert(*symbol);
            }
        }

        for (auto& handler : rule->handlers) {
            handler.resolved_reads.clear();
            handler.resolved_writes.clear();
            handler.resolved_projects.clear();
            handler.resolved_emits.clear();
            handler.resolved_effects.clear();

            const auto resolve_traits = [this, &aliases, &rule, &selected_traits](
                                            const std::vector<LocatedName>& entries,
                                            std::vector<SymbolId>& output,
                                            const char* clause,
                                            bool must_be_selected = false) {
                std::unordered_set<SymbolId> seen;
                for (const auto& entry : entries) {
                    std::optional<SymbolId> symbol;
                    if (const auto alias = aliases.find(entry.spelling); alias != aliases.end()) {
                        symbol = alias->second;
                    } else {
                        symbol = try_resolve_trait_ref_to_symbol(entry.spelling);
                    }
                    if (!symbol.has_value()) {
                        errors_.error(entry.location,
                                      "unknown " + std::string(clause) + " contract entry '" + entry.spelling +
                                          "' in extern rule '" + rule->name + "'");
                        continue;
                    }
                    if (!seen.insert(*symbol).second) {
                        errors_.error(entry.location,
                                      "duplicate " + std::string(clause) + " contract entry '" +
                                          make_canonical_id(*symbol) + "'");
                        continue;
                    }
                    if (must_be_selected && !selected_traits.contains(*symbol)) {
                        errors_.error(entry.location,
                                      std::string(clause) + " contract entry '" + make_canonical_id(*symbol) +
                                          "' must be selected by extern rule '" + rule->name + "' filter");
                        continue;
                    }
                    output.push_back(*symbol);
                }
            };
            resolve_traits(handler.reads, handler.resolved_reads, "reads");
            resolve_traits(handler.writes, handler.resolved_writes, "writes");
            resolve_traits(handler.projects, handler.resolved_projects, "projects", true);

            const std::unordered_set<SymbolId> durable_writes(handler.resolved_writes.begin(),
                                                              handler.resolved_writes.end());
            for (const auto& projected : handler.resolved_projects) {
                if (durable_writes.contains(projected)) {
                    errors_.error(handler.location,
                                  "trait '" + make_canonical_id(projected) +
                                      "' cannot be both a writes and projects contract entry in extern rule '" +
                                      rule->name + "'");
                }
            }

            std::unordered_set<SymbolId> emitted;
            for (const auto& entry : handler.emits) {
                auto symbol = try_resolve_event_ref_to_symbol(entry.spelling);
                if (!symbol.has_value()) {
                    errors_.error(entry.location, "unknown emits contract event '" + entry.spelling + "'");
                    continue;
                }
                if (!emitted.insert(*symbol).second) {
                    errors_.error(entry.location,
                                  "duplicate emits contract entry '" + make_canonical_id(*symbol) + "'");
                    continue;
                }
                handler.resolved_emits.push_back(*symbol);
            }

            std::unordered_set<std::string> commands;
            for (auto& command : handler.commands) {
                std::optional<SymbolId> target;
                if (command.kind == HandlerCommandKind::Destroy) {
                    if (command.target.has_value()) {
                        errors_.error(command.location, "destroy command must not name a target");
                    }
                } else if (!command.target.has_value()) {
                    errors_.error(command.location,
                                  std::string(handler_command_kind_name(command.kind)) + " command requires a target");
                } else if (command.kind == HandlerCommandKind::Spawn) {
                    target = try_resolve_ref_of_kind(command.target->spelling, {SymbolKind::Template});
                    if (!target.has_value()) {
                        errors_.error(command.target->location,
                                      "spawn command target '" + command.target->spelling + "' is not a template");
                    }
                } else {
                    target = try_resolve_trait_ref_to_symbol(command.target->spelling);
                    if (!target.has_value()) {
                        errors_.error(command.target->location,
                                      std::string(handler_command_kind_name(command.kind)) + " command target '" +
                                          command.target->spelling + "' is not a trait");
                    }
                }
                command.resolved_target_id = target;
                const auto key             = std::string(handler_command_kind_name(command.kind)) +
                                             (target.has_value() ? ":" + make_canonical_id(*target) : "");
                if (!commands.insert(key).second) {
                    errors_.error(command.location, "duplicate commands contract entry '" + key + "'");
                }
            }

            std::unordered_set<std::string> effects;
            for (const auto& entry : handler.effects) {
                if (!valid_effect_domain(entry.spelling)) {
                    errors_.error(entry.location, "invalid effect domain '" + entry.spelling + "'");
                    continue;
                }
                if (!effects.insert(entry.spelling).second) {
                    errors_.error(entry.location, "duplicate effects contract entry '" + entry.spelling + "'");
                    continue;
                }
                handler.resolved_effects.push_back(entry.spelling);
            }
        }
    }
}

// ── Phase 3f: Event Usage Validation ────────────────────────────────────────

void SemanticAnalyzer::validate_event_usage(  // NOLINT(readability-function-cognitive-complexity)
    ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* rule = std::get_if<ExternRuleNode>(&decl)) {
            for (const auto& handler : rule->handlers) {
                for (const auto& emitted : handler.emits) {
                    const auto symbol = try_resolve_event_ref_to_symbol(emitted.spelling);
                    if (symbol.has_value() && is_external_event(*symbol)) {
                        errors_.error(
                            emitted.location,
                            "external event '" + make_canonical_id(*symbol) + "' can only be emitted by the runtime");
                    }
                }
            }
        }
        if (auto* rule = std::get_if<RuleNode>(&decl)) {
            // Build set of filter-bound names (trait names and their aliases) for alias conflict check
            std::unordered_set<std::string> filter_bound;
            for (const auto& entry : rule->filter.entries) {
                auto dot    = entry.qualified_name.rfind('.');
                auto simple = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
                filter_bound.insert(simple);
                if (entry.alias.has_value()) {
                    filter_bound.insert(*entry.alias);
                }
            }
            for (const auto& t : rule->filter.trait_names) {
                filter_bound.insert(t);
            }

            PairScope pair_scope;
            if (rule->pairs.has_value()) {
                pair_scope = build_pair_scope(*rule->pairs);
                for (const auto& binding : rule->pairs->bindings) {
                    filter_bound.insert(binding.name);
                    for (const auto& entry : binding.traits) {
                        if (entry.alias.has_value()) {
                            filter_bound.insert(*entry.alias);
                        }
                    }
                }
            }
            const PairScope* pair_scope_ptr = rule->pairs.has_value() ? &pair_scope : nullptr;

            for (auto& handler : rule->handlers) {
                auto filter_bindings = build_filter_bindings(rule->filter);

                const bool event_trigger =
                    handler.resolved_trigger.has_value() && handler.resolved_trigger->kind == HandlerTriggerKind::Event;
                const bool phase_trigger =
                    handler.resolved_trigger.has_value() && handler.resolved_trigger->kind == HandlerTriggerKind::Phase;
                const bool render_stage_trigger = handler.resolved_trigger.has_value() &&
                                                  handler.resolved_trigger->kind == HandlerTriggerKind::RenderStage;
                if (!event_trigger && !phase_trigger && !render_stage_trigger) {
                    diagnose_unresolved_handler_trigger("rule '" + rule->name + "'", handler.event_name, handler.location);
                }
                // Task 3.4: Validate handler alias doesn't conflict with filter aliases in scope
                if (handler.alias.has_value() && filter_bound.contains(*handler.alias)) {
                    errors_.error(handler.location,
                                  "handler alias '" + *handler.alias + "' conflicts with filter alias '" +
                                      *handler.alias + "' already in scope");
                }

                std::optional<ResolvedStruct> phase_activation;
                // handler.event_name is the raw source spelling, which is
                // dotted/qualified for a cross-module trigger (e.g.
                // "pointer.Click") and would never match event_structs_'
                // simple-name keys — resolved_trigger's already-resolved
                // symbol carries the correct local_name regardless of how
                // the author wrote the reference (bare, aliased, or fully
                // qualified), the same way the phase_trigger branch below
                // already does for phase symbols.
                const ResolvedStruct* handler_event =
                    event_trigger ? find_resolved_event(handler.resolved_trigger->symbol.local_name) : nullptr;
                if (phase_trigger) {
                    const auto& phase_symbol = handler.resolved_trigger->symbol;
                    if (const auto* fields = find_phase_fields(phase_symbol); fields != nullptr) {
                        phase_activation.emplace();
                        phase_activation->name   = phase_symbol.local_name;
                        phase_activation->fields = *fields;
                        assign_canonical_identity(*phase_activation, phase_symbol);
                        handler_event = &*phase_activation;
                    }
                }
                if (render_stage_trigger) {
                    // dsl-render-passes: exposes the stage's built-in input
                    // (read) and output (write) fields under the handler's
                    // own alias, through the same handler_event/local_bindings
                    // mechanism ordinary phase completion data already uses.
                    phase_activation = build_render_stage_activation_struct(handler.resolved_trigger->symbol);
                    handler_event    = phase_activation.has_value() ? &*phase_activation : nullptr;
                }
                std::unordered_map<std::string, TypeInfo> local_bindings;
                if (handler_event != nullptr) {
                    const auto symbol = resolved_decl_symbol(
                        *handler_event, SymbolKind::Event, current_module_name_, handler_event->name);
                    local_bindings[handler.event_name] =
                        make_resolved_user_type(TypeKind::Struct, symbol, handler_event->name);
                }
                if (handler.alias.has_value() && handler_event != nullptr) {
                    const auto symbol = resolved_decl_symbol(
                        *handler_event, SymbolKind::Event, current_module_name_, handler_event->name);
                    local_bindings[*handler.alias] =
                        make_resolved_user_type(TypeKind::Struct, symbol, handler_event->name);
                }

                validate_event_stmts(
                    handler.body, filter_bindings, local_bindings, handler_event, rule->name, pair_scope_ptr);
            }
        }
    }
}

void SemanticAnalyzer::require_optional_entity_id_target(
    const std::optional<std::unique_ptr<ExprNode>>& target_expr,
    const SourceLocation& location,
    const std::string& wrong_type_message,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& locals,
    const ResolvedStruct* handler_event,
    const PairScope* pair_scope) {
    if (!target_expr.has_value()) {
        return;
    }
    auto t = infer_expr_type(**target_expr, filter_bindings, locals, handler_event, pair_scope);
    if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
        errors_.error(location, wrong_type_message);
    }
}

void SemanticAnalyzer::validate_trait_field_supply(
    const ResolvedTrait& trait,
    const std::vector<FieldAssignment>& args,
    const std::string& context_desc,
    const SourceLocation& required_field_location,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& locals,
    const ResolvedStruct* handler_event,
    const PairScope* pair_scope) {
    std::unordered_map<std::string, const ResolvedField*> fields_by_name;
    for (const auto& field : trait.fields) {
        fields_by_name[field.name] = &field;
    }

    std::unordered_set<std::string> supplied;
    for (const auto& arg : args) {
        supplied.insert(arg.name);
        auto it = fields_by_name.find(arg.name);
        if (it == fields_by_name.end()) {
            errors_.error(arg.location, "unknown field '" + arg.name + "' in " + context_desc);
            continue;
        }

        auto actual          = infer_expr_type(*arg.value, filter_bindings, locals, handler_event, pair_scope);
        const auto& expected = it->second->type;
        if (actual.kind != TypeKind::Unknown && expected.kind != TypeKind::Unknown && actual.kind != expected.kind) {
            errors_.error(arg.location, "type mismatch for field '" + arg.name + "' in " + context_desc);
        }
    }

    for (const auto& field : trait.fields) {
        if (!field.has_default && !supplied.contains(field.name)) {
            errors_.error(required_field_location,
                          "required field '" + field.name + "' must be supplied in " + context_desc);
            break;
        }
    }
}

void SemanticAnalyzer::validate_event_stmts(  // NOLINT(readability-function-cognitive-complexity) -- still 108 after
                                              // require_optional_entity_id_target/validate_trait_field_supply
                                              // extraction; not further in scope here
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event,
    const std::string& rule_name,
    const PairScope* pair_scope) {
    (void)rule_name;
    auto locals = local_bindings;

    auto validate_emit = [this, &filter_bindings, &locals, handler_event, pair_scope](const EmitStmt& emit) {
        const auto event_symbol = emit.resolved_event_id.has_value() ? emit.resolved_event_id
                                                                     : try_resolve_event_ref_to_symbol(emit.event_name);
        if (!event_symbol.has_value()) {
            errors_.error(emit.location, "undeclared event '" + emit.event_name + "'");
            return;
        }
        if (is_external_event(*event_symbol)) {
            errors_.error(
                emit.location,
                "external event '" + make_canonical_id(*event_symbol) + "' can only be emitted by the runtime");
            return;
        }

        const auto* event = find_resolved_event(event_symbol->local_name);
        std::unordered_set<std::string> event_fields;
        if (event != nullptr) {
            for (const auto& field : event->fields) {
                event_fields.insert(field.name);
            }
        }

        for (const auto& field : emit.payload) {
            if (event != nullptr && !event_fields.contains(field.name)) {
                errors_.error(field.location,
                              "unknown event field '" + field.name + "' for event '" + emit.event_name + "'");
            }
        }

        if (!emit.target.has_value()) {
            return;
        }

        auto target_type = infer_expr_type(**emit.target, filter_bindings, locals, handler_event, pair_scope);
        if (target_type.kind != TypeKind::EntityId && target_type.kind != TypeKind::Unknown) {
            errors_.error(emit.location, "emit target must be of type entity_id, got " + target_type.name);
        }
    };

    auto validate_add = [this, &filter_bindings, &locals, handler_event, pair_scope](const AddTraitStmt& add) {
        const auto* trait = find_resolved_trait(add.resolved_trait_id, add.trait_name);
        if (trait == nullptr) {
            const auto prev_errors = errors_.error_count();
            (void)resolve_trait_ref_to_canonical(add.trait_name, add.location);
            if (errors_.error_count() == prev_errors) {
                errors_.error(add.location, "undeclared trait '" + add.trait_name + "'");
            }
            return;
        }

        if (pair_scope != nullptr && !add.target_expr.has_value()) {
            errors_.error(add.location,
                          "pair handlers require an explicit target; use `add " + add.trait_name + " to <binding>`");
        }
        require_optional_entity_id_target(add.target_expr,
                                          add.location,
                                          "`to` target must be of type `entity_id`",
                                          filter_bindings,
                                          locals,
                                          handler_event,
                                          pair_scope);
        validate_trait_field_supply(*trait,
                                    add.args,
                                    "`add " + add.trait_name + "`",
                                    add.location,
                                    filter_bindings,
                                    locals,
                                    handler_event,
                                    pair_scope);
    };

    auto validate_project =
        [this, &filter_bindings, &locals, handler_event, pair_scope](const ProjectTraitStmt& project) {
            const auto* trait = find_resolved_trait(project.resolved_trait_id, project.trait_name);
            if (trait == nullptr) {
                const auto prev_errors = errors_.error_count();
                (void)resolve_trait_ref_to_canonical(project.trait_name, project.location);
                if (errors_.error_count() == prev_errors) {
                    errors_.error(project.location, "undeclared trait '" + project.trait_name + "'");
                }
                return;
            }

            for (const auto& field : trait->fields) {
                if (field.is_persist) {
                    errors_.error(project.location,
                                  "trait '" + project.trait_name + "' has persistent fields and cannot be projected");
                    break;
                }
                if (field.is_sync) {
                    errors_.error(project.location,
                                  "trait '" + project.trait_name + "' has synced fields and cannot be projected");
                    break;
                }
            }

            if (pair_scope != nullptr && !project.target_expr.has_value()) {
                errors_.error(
                    project.location,
                    "pair handlers require an explicit target; use `project " + project.trait_name + " to <binding>`");
            }
            require_optional_entity_id_target(project.target_expr,
                                              project.location,
                                              "`project ... to` target must be of type `entity_id`",
                                              filter_bindings,
                                              locals,
                                              handler_event,
                                              pair_scope);
            validate_trait_field_supply(*trait,
                                        project.args,
                                        "`project " + project.trait_name + "`",
                                        project.location,
                                        filter_bindings,
                                        locals,
                                        handler_event,
                                        pair_scope);
        };

    auto validate_remove = [this, &filter_bindings, &locals, handler_event, pair_scope](const RemoveTraitStmt& remove) {
        if (find_resolved_trait(remove.resolved_trait_id, remove.trait_name) == nullptr) {
            const auto prev_errors = errors_.error_count();
            (void)resolve_trait_ref_to_canonical(remove.trait_name, remove.location);
            if (errors_.error_count() == prev_errors) {
                errors_.error(remove.location, "undeclared trait '" + remove.trait_name + "'");
            }
        }
        if (pair_scope != nullptr && !remove.target_expr.has_value()) {
            errors_.error(
                remove.location,
                "pair handlers require an explicit target; use `remove " + remove.trait_name + " from <binding>`");
        }
        require_optional_entity_id_target(remove.target_expr,
                                          remove.location,
                                          "`from` target must be of type `entity_id`",
                                          filter_bindings,
                                          locals,
                                          handler_event,
                                          pair_scope);
    };

    auto validate_destroy = [this, &filter_bindings, &locals, handler_event, pair_scope](const DestroyStmt& destroy) {
        if (!destroy.target_expr.has_value() && pair_scope != nullptr) {
            errors_.error(destroy.location, "pair handlers require an explicit target; use `destroy <binding>`");
        }
        require_optional_entity_id_target(destroy.target_expr,
                                          destroy.location,
                                          "`destroy` target must be of type `entity_id`",
                                          filter_bindings,
                                          locals,
                                          handler_event,
                                          pair_scope);
    };

    auto in_rule_handler = !rule_name.empty();

    for (const auto& stmt : stmts) {
        if (const auto* let_stmt = std::get_if<LetStmt>(&stmt->stmt)) {
            locals[let_stmt->name] =
                infer_expr_type(*let_stmt->value, filter_bindings, locals, handler_event, pair_scope);
            if (const auto* spawn = std::get_if<SpawnExpr>(&let_stmt->value->expr)) {
                validate_spawn_expr(*spawn, let_stmt->location);
            }
            continue;
        }
        if (const auto* assign_stmt = std::get_if<VarAssign>(&stmt->stmt)) {
            bool target_rejected = false;
            if (pair_scope != nullptr) {
                if (pair_scope->contains(assign_stmt->name)) {
                    errors_.error(assign_stmt->location, "pair-bound durable traits are read-only");
                } else {
                    errors_.error(assign_stmt->location,
                                  "pair handlers have no implicit current entity to assign into; use an explicit "
                                  "binding");
                }
                target_rejected = true;
            } else if (auto local_it = locals.find(assign_stmt->name);
                       local_it != locals.end() && local_it->second.is_let) {
                errors_.error(assign_stmt->location, "foreach loop variable '" + assign_stmt->name + "' is read-only");
                target_rejected = true;
            }

            // Rebuild the assignment target as a member-access chain (e.g. `hp.health`
            // for `hp.health -= 1.0` where `hp` is a filter alias) and resolve it through
            // the shared expression resolver: codegen (system_emitter.cpp's VarAssign
            // lowering) reconstructs this exact chain via rewrite_expr, so validating it
            // here the same way catches an unknown alias/field with a precise DSL-level
            // diagnostic instead of silently accepting it and surfacing a confusing error
            // in generated C++. This also gives compound-assignment operator/type
            // validation below the target's real type for both bare and dotted targets.
            TypeInfo target_type = make_unknown_type();
            if (!target_rejected) {
                ExprNode chain(
                    ExprNode::Variant{IdentExpr{.name = assign_stmt->name, .location = assign_stmt->location}},
                    assign_stmt->location);
                for (const auto& segment : assign_stmt->path) {
                    chain =
                        ExprNode(ExprNode::Variant{MemberExpr{.object = std::make_unique<ExprNode>(std::move(chain)),
                                                              .member = segment,
                                                              .resolved_enum_member = std::nullopt,
                                                              .location             = assign_stmt->location}},
                                 assign_stmt->location);
                }
                target_type = infer_expr_type(chain, filter_bindings, locals, handler_event, pair_scope);
            }

            auto value_type = infer_expr_type(*assign_stmt->value, filter_bindings, locals, handler_event, pair_scope);

            // Compound-assignment operator/type validation for vec2/vec3-typed targets,
            // reusing the same closed matrix BinaryExpr inference consults (dsl-vector-
            // expressions spec: legal exactly when a row exists with the target's type as
            // both left operand and result). Other target types keep their prior
            // (writability-only) behavior — this proposal does not add general "="/int/
            // float compound-assignment type-checking.
            if (!target_rejected && assign_stmt->op != "=" &&
                (target_type.kind == TypeKind::Vec2 || target_type.kind == TypeKind::Vec3) &&
                value_type.kind != TypeKind::Unknown) {
                const auto binary_op   = assign_stmt->op.substr(0, 1);
                auto result_kind       = lookup_vector_binary_op_result(target_type.kind, binary_op, value_type.kind);
                if (!result_kind.has_value() || *result_kind != target_type.kind) {
                    errors_.error(assign_stmt->location, "no compound assignment '" + assign_stmt->op +
                                                              "' for target type '" + target_type.name +
                                                              "' and source type '" + value_type.name + "'");
                }
            }
            continue;
        }
        if (const auto* expr_stmt = std::get_if<ExprStmt>(&stmt->stmt)) {
            if (const auto* spawn = std::get_if<SpawnExpr>(&expr_stmt->expr->expr)) {
                validate_spawn_expr(*spawn, expr_stmt->location);
            }
            (void)infer_expr_type(*expr_stmt->expr, filter_bindings, locals, handler_event, pair_scope);
            continue;
        }
        if (const auto* emit_stmt = std::get_if<EmitStmt>(&stmt->stmt)) {
            validate_emit(*emit_stmt);
            continue;
        }
        if (const auto* add_stmt = std::get_if<AddTraitStmt>(&stmt->stmt)) {
            validate_add(*add_stmt);
            continue;
        }
        if (const auto* project_stmt = std::get_if<ProjectTraitStmt>(&stmt->stmt)) {
            validate_project(*project_stmt);
            continue;
        }
        if (const auto* remove_stmt = std::get_if<RemoveTraitStmt>(&stmt->stmt)) {
            validate_remove(*remove_stmt);
            continue;
        }
        if (const auto* destroy_stmt = std::get_if<DestroyStmt>(&stmt->stmt)) {
            validate_destroy(*destroy_stmt);
            continue;
        }
        if (const auto* trait_match = std::get_if<TraitMatchStmt>(&stmt->stmt)) {
            validate_trait_match_stmt(
                *trait_match, filter_bindings, locals, handler_event, rule_name, in_rule_handler, pair_scope);
            continue;
        }
        if (const auto* if_stmt = std::get_if<IfStmt>(&stmt->stmt)) {
            (void)infer_expr_type(*if_stmt->condition, filter_bindings, locals, handler_event, pair_scope);
            validate_event_stmts(if_stmt->then_body, filter_bindings, locals, handler_event, rule_name, pair_scope);
            for (const auto& branch : if_stmt->else_if_branches) {
                (void)infer_expr_type(*branch.condition, filter_bindings, locals, handler_event, pair_scope);
                validate_event_stmts(branch.body, filter_bindings, locals, handler_event, rule_name, pair_scope);
            }
            validate_event_stmts(if_stmt->else_body, filter_bindings, locals, handler_event, rule_name, pair_scope);
            continue;
        }
        if (const auto* foreach_stmt = std::get_if<ForeachStmt>(&stmt->stmt)) {
            if (validate_range_iterable(*foreach_stmt->iterable, filter_bindings, locals, handler_event, pair_scope)) {
                auto range_locals    = locals;
                TypeInfo element_type = make_int_type();
                element_type.is_let   = true;
                range_locals[foreach_stmt->var_name] = std::move(element_type);
                validate_event_stmts(
                    foreach_stmt->body, filter_bindings, range_locals, handler_event, rule_name, pair_scope);
                continue;
            }

            auto iterable_type =
                infer_expr_type(*foreach_stmt->iterable, filter_bindings, locals, handler_event, pair_scope);
            if (iterable_type.kind != TypeKind::List && iterable_type.kind != TypeKind::Unknown) {
                errors_.error(foreach_stmt->location, "foreach requires a `list[T]` iterable");
                continue;
            }

            auto loop_locals      = locals;
            TypeInfo element_type = iterable_type.element != nullptr ? *iterable_type.element : make_unknown_type();
            element_type.is_let   = true;
            loop_locals[foreach_stmt->var_name] = std::move(element_type);
            validate_event_stmts(
                foreach_stmt->body, filter_bindings, loop_locals, handler_event, rule_name, pair_scope);
        }
    }
}

void SemanticAnalyzer::validate_trait_match_stmt(
    const TraitMatchStmt& stmt,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event,
    const std::string& rule_name,
    bool in_rule_handler,
    const PairScope* pair_scope) {
    if (!in_rule_handler) {
        errors_.error(stmt.location, "statement-level `match entity_id` only allowed inside rule event handlers");
    }

    if (pair_scope != nullptr) {
        if (const auto* ident = std::get_if<IdentExpr>(&stmt.subject->expr);
            ident != nullptr && pair_scope->contains(ident->name)) {
            errors_.error(stmt.location,
                          "pair handlers cannot trait-match directly on binding '" + ident->name +
                              "'; a data-bearing match alias would grant mutable access to a read-only trait");
        }
    }

    auto subject_type = infer_expr_type(*stmt.subject, filter_bindings, local_bindings, handler_event, pair_scope);
    if (subject_type.kind != TypeKind::EntityId && subject_type.kind != TypeKind::Unknown) {
        errors_.error(stmt.location,
                      "statement-level `match` subject must be of type `entity_id`; use expression-level match for "
                      "value dispatch");
    }

    std::unordered_set<std::string> in_scope_names;
    for (const auto& [name, _] : filter_bindings) {
        in_scope_names.insert(name);
    }
    for (const auto& [name, _] : local_bindings) {
        in_scope_names.insert(name);
    }

    for (const auto& arm : stmt.arms) {
        const auto* trait = find_resolved_trait(arm.resolved_trait_id, arm.trait_name);
        if (trait == nullptr) {
            const auto prev_errors = errors_.error_count();
            (void)resolve_trait_ref_to_canonical(arm.trait_name, arm.location);
            if (errors_.error_count() == prev_errors) {
                errors_.error(arm.location, "undeclared trait '" + arm.trait_name + "'");
            }
            continue;
        }

        const bool IS_MARKER = trait->fields.empty();
        if (IS_MARKER && arm.alias.has_value()) {
            errors_.error(
                arm.location,
                "marker trait '" + arm.trait_name + "' has no fields; alias 'as " + *arm.alias + "' is not allowed");
        }

        auto arm_locals = local_bindings;
        if (arm.alias.has_value()) {
            if (in_scope_names.contains(*arm.alias)) {
                errors_.error(arm.location,
                              "match arm alias '" + *arm.alias + "' conflicts with filter alias '" + *arm.alias + "'");
            } else {
                const auto symbol = resolved_decl_symbol(*trait, SymbolKind::Trait, current_module_name_, trait->name);
                arm_locals[*arm.alias] = make_resolved_user_type(TypeKind::Struct, symbol, trait->name);
            }
        }

        validate_event_stmts(arm.body, filter_bindings, arm_locals, handler_event, rule_name, pair_scope);
    }

    if (stmt.wildcard.has_value()) {
        validate_event_stmts(
            stmt.wildcard->body, filter_bindings, local_bindings, handler_event, rule_name, pair_scope);
    }
}

// ── Phase 4: Dependency Graph ───────────────────────────────────────────────

void SemanticAnalyzer::collect_phase_plan(const PhaseNode& phase, std::size_t declaration_index) {
    const auto resolved = result_.phases.find(phase.name);
    if (resolved != result_.phases.end() && resolved->second.symbol_id.has_value()) {
        PhasePlan plan;
        plan.phase               = *resolved->second.symbol_id;
        plan.source_dependencies = resolved->second.from_sources;
        plan.completion_dependencies.reserve(resolved->second.after_phases.size());
        for (const auto& dependency : resolved->second.after_phases) {
            plan.completion_dependencies.push_back(dependency.symbol);
        }
        plan.fields                              = resolved->second.fields;
        plan.runtime_root                        = resolved->second.runtime_root;
        plan.every_seconds                       = resolved->second.every_seconds;
        plan.max_repetitions                     = resolved->second.max_repetitions;
        plan.declaration_order.declaration_index = declaration_index;
        result_.execution_graph.phases.push_back(std::move(plan));
    }
}

void SemanticAnalyzer::collect_rule_dependency(const RuleNode& rule, std::size_t declaration_index) {
    RuleDependency dep;
    dep.rule_name = rule.name;  // simple name (task 5.4 will migrate to canonical)
    dep.rule_id   = rule.resolved_rule_id;

    std::vector<ResolvedHandlerTrigger> declared_triggers;
    const auto pair_scope = rule.pairs.has_value() ? build_pair_scope(*rule.pairs) : PairScope{};
    // Rule-level, not per-handler: every handler on this rule shares the same
    // `pairs:`/`where:` domain, so eligibility is identical for each of them.
    const auto spatial_join =
        rule.pairs.has_value() ? recognize_spatial_join(rule, pair_scope, errors_) : std::nullopt;
    for (std::size_t handler_index = 0; handler_index < rule.handlers.size(); ++handler_index) {
        const auto& handler = rule.handlers[handler_index];
        collect_rule_deps(handler.body, dep);
        if (handler.resolved_trigger.has_value() && rule.resolved_rule_id.has_value()) {
            if (std::ranges::find(declared_triggers, *handler.resolved_trigger) != declared_triggers.end()) {
                errors_.error(handler.trigger_location,
                              "duplicate handler trigger '" + handler.resolved_trigger->debug_string() + "' in rule '" +
                                  make_canonical_id(*rule.resolved_rule_id) + "'");
                continue;
            }
            declared_triggers.push_back(*handler.resolved_trigger);

            auto inferred = rule.pairs.has_value() ? infer_pair_handler_contract(rule, handler, pair_scope)
                                                   : infer_regular_handler_contract(rule, handler);
            inferred.spatial_join = spatial_join;
            result_.handler_contracts.push_back(inferred);

            HandlerNode node;
            node.identity       = HandlerIdentity{.rule = *rule.resolved_rule_id, .trigger = *handler.resolved_trigger};
            node.implementation = HandlerImplementationKind::Cactus;
            node.contract       = static_cast<const HandlerContract&>(inferred);
            node.declaration_order = DeclarationOrder{
                .module_index = 0, .declaration_index = declaration_index, .handler_index = handler_index};
            node.location = handler.location;
            result_.execution_graph.handlers.push_back(std::move(node));
        }
    }

    result_.dependency_graph.push_back(std::move(dep));
}

// 31 after extraction from build_dependency_graph (task 6.11); builds the
// filter-alias map plus a per-handler HandlerContract from resolved
// reads/writes/emits/commands/effects, an inherently multi-field assembly.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::collect_extern_rule_dependency(const ExternRuleNode& rule, std::size_t declaration_index) {
    RuleDependency dep;
    dep.rule_name = rule.name;
    dep.rule_id   = rule.resolved_rule_id;

    std::unordered_map<std::string, SymbolId> filter_aliases;
    for (const auto& entry : rule.filter.entries) {
        if (!entry.resolved_trait_id.has_value()) {
            continue;
        }
        filter_aliases[entry.qualified_name]                = *entry.resolved_trait_id;
        filter_aliases[entry.resolved_trait_id->local_name] = *entry.resolved_trait_id;
        if (entry.alias.has_value()) {
            filter_aliases[*entry.alias] = *entry.resolved_trait_id;
        }
    }
    for (std::size_t index = 0; index < rule.filter.trait_names.size() && index < rule.filter.resolved_trait_ids.size();
         ++index) {
        filter_aliases[rule.filter.trait_names[index]] = rule.filter.resolved_trait_ids[index];
    }

    std::vector<ResolvedHandlerTrigger> declared_triggers;
    for (std::size_t handler_index = 0; handler_index < rule.handlers.size(); ++handler_index) {
        const auto& handler = rule.handlers[handler_index];
        if (!handler.resolved_trigger.has_value() || !rule.resolved_rule_id.has_value()) {
            if (!handler.resolved_trigger.has_value()) {
                diagnose_unresolved_handler_trigger(
                    "extern rule '" + rule.name + "'", handler.trigger_name, handler.trigger_location);
            }
            continue;
        }
        if (handler.resolved_trigger->kind == HandlerTriggerKind::RenderStage) {
            // A render-pass stage handler's body is translated to GLSL
            // (dsl-render-passes); an extern rule has no body to translate.
            errors_.error(handler.trigger_location,
                          "render-pass stage triggers are not valid on 'extern rule' declarations; use an ordinary "
                          "'rule'");
            continue;
        }
        if (std::ranges::find(declared_triggers, *handler.resolved_trigger) != declared_triggers.end()) {
            errors_.error(handler.trigger_location,
                          "duplicate handler trigger '" + handler.resolved_trigger->debug_string() +
                              "' in extern rule '" + make_canonical_id(*rule.resolved_rule_id) + "'");
            continue;
        }
        declared_triggers.push_back(*handler.resolved_trigger);

        HandlerContract contract;
        contract.selection   = rule.filter.resolved_trait_ids;
        contract.exclusion   = rule.exclude.resolved_trait_ids;
        contract.domain_kind = contract.selection.empty() && contract.exclusion.empty()
                                   ? HandlerDomainKind::Selectionless
                                   : HandlerDomainKind::Unary;
        contract.reads.insert(handler.resolved_reads.begin(), handler.resolved_reads.end());
        for (const auto& write : handler.resolved_writes) {
            contract.reads.insert(write);
            contract.writes.insert(write);
        }
        contract.projects.insert(handler.resolved_projects.begin(), handler.resolved_projects.end());
        // An extern rule declares its body reads explicitly, so the only
        // inference left is over its order by: keys — walked with the shared
        // expression walker so a computed key folds every filter-alias read it
        // touches, not just the one its outermost chain happens to be rooted at.
        auto resolve_order_by_read = [&](const ExprNode& expr, const LocalNames&) -> bool {
            const auto* root = std::get_if<IdentExpr>(&expr.expr);
            if (root == nullptr) {
                const auto* member = std::get_if<MemberExpr>(&expr.expr);
                root = member == nullptr ? nullptr : std::get_if<IdentExpr>(&member->object->expr);
            }
            if (root == nullptr) {
                return false;
            }
            const auto found = filter_aliases.find(root->name);
            if (found == filter_aliases.end()) {
                return false;
            }
            contract.reads.insert(found->second);
            return true;
        };
        for (const auto& sort_key : rule.order_by) {
            walk_expression_reads(*sort_key.expression, LocalNames{}, contract, resolve_order_by_read);
        }
        contract.emits.insert(handler.resolved_emits.begin(), handler.resolved_emits.end());
        for (const auto& command : handler.commands) {
            InferredHandlerCommand inferred_command{.kind = command.kind, .target = command.resolved_target_id};
            if (std::ranges::find(contract.commands, inferred_command) == contract.commands.end()) {
                contract.commands.push_back(std::move(inferred_command));
            }
        }
        contract.effects.insert(handler.resolved_effects.begin(), handler.resolved_effects.end());

        HandlerNode node;
        node.identity       = HandlerIdentity{.rule = *rule.resolved_rule_id, .trigger = *handler.resolved_trigger};
        node.implementation = HandlerImplementationKind::External;
        node.contract       = std::move(contract);
        node.declaration_order =
            DeclarationOrder{.module_index = 0, .declaration_index = declaration_index, .handler_index = handler_index};
        node.location = handler.location;
        result_.execution_graph.handlers.push_back(std::move(node));
    }
    result_.dependency_graph.push_back(std::move(dep));
}

void SemanticAnalyzer::build_dependency_graph(ProgramNode& program) {
    for (std::size_t declaration_index = 0; declaration_index < program.declarations.size(); ++declaration_index) {
        auto& decl = program.declarations[declaration_index];
        if (const auto* phase = std::get_if<PhaseNode>(&decl)) {
            collect_phase_plan(*phase, declaration_index);
        }
        if (const auto* rule = std::get_if<RuleNode>(&decl)) {
            collect_rule_dependency(*rule, declaration_index);
        }
        if (const auto* rule = std::get_if<ExternRuleNode>(&decl)) {
            collect_extern_rule_dependency(*rule, declaration_index);
        }
    }
    // Phase-barrier and event-flow edges are computed later, inside
    // compute_handler_schedule (called from validate_after_clauses) — nothing
    // between here and there reads execution_graph.phase_barriers/event_flows.
}

// ── Shared AST walk for handler-contract inference (regular + pair rules) ──

void SemanticAnalyzer::add_contract_command(HandlerContract& contract,
                                            HandlerCommandKind kind,
                                            std::optional<SymbolId> target) {
    InferredHandlerCommand command{.kind = kind, .target = std::move(target)};
    if (std::ranges::find(contract.commands, command) == contract.commands.end()) {
        contract.commands.push_back(std::move(command));
    }
}

void SemanticAnalyzer::add_contract_call_effects(HandlerContract& contract,
                                                 const std::optional<SymbolId>& callee) const {
    if (!callee.has_value()) {
        return;
    }
    const auto* function = find_resolved_func(*callee);
    if (function == nullptr || !function->is_extern) {
        return;
    }
    if (function->effect_summary.has_value()) {
        contract.effects.insert(function->effect_summary->begin(), function->effect_summary->end());
    } else {
        contract.effects.insert("external");
    }
}

void SemanticAnalyzer::walk_expression_reads(  // NOLINT(readability-function-cognitive-complexity) -- exhaustive
                                               // per-ExprNode-kind dispatch, the same shape as the already-NOLINT'd
                                               // resolve_expr
    const ExprNode& expr,
    const LocalNames& locals,
    HandlerContract& contract,
    const std::function<bool(const ExprNode&, const LocalNames&)>& resolve_read) const {
    if (resolve_read(expr, locals)) {
        return;
    }
    auto visit = [&](const ExprNode& child, const LocalNames& child_locals) {
        walk_expression_reads(child, child_locals, contract, resolve_read);
    };
    std::function<void(const std::vector<ChildOverrideNode>&, const LocalNames&)> visit_child_overrides;
    visit_child_overrides = [&](const std::vector<ChildOverrideNode>& overrides, const LocalNames& child_locals) {
        for (const auto& child : overrides) {
            for (const auto& trait : child.traits) {
                for (const auto& field : trait.assignments) {
                    visit(*field.value, child_locals);
                }
            }
            visit_child_overrides(child.children, child_locals);
        }
    };
    std::visit(
        [&](const auto& node) {
            using E = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<E, IdentExpr>) {
                // handled (or intentionally ignored) by resolve_read above
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                visit(*node.left, locals);
                visit(*node.right, locals);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                visit(*node.operand, locals);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                add_contract_call_effects(contract, node.resolved_callee_id);
                for (const auto& arg : node.args) {
                    visit(*arg, locals);
                }
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                // Not resolved directly by resolve_read above; walk the object.
                visit(*node.object, locals);
            } else if constexpr (std::is_same_v<E, LambdaExpr>) {
                auto lambda_locals = locals;
                lambda_locals.insert(node.params.begin(), node.params.end());
                visit(*node.body, lambda_locals);
            } else if constexpr (std::is_same_v<E, PipelineExpr>) {
                visit(*node.source, locals);
                for (const auto& operation : node.operations) {
                    for (const auto& arg : operation.args) {
                        visit(*arg, locals);
                    }
                }
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                visit(*node.subject, locals);
                for (const auto& arm : node.arms) {
                    visit(*arm.pattern, locals);
                    visit(*arm.body, locals);
                }
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                visit(*node.condition, locals);
                visit(*node.then_expr, locals);
                visit(*node.else_expr, locals);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                for (const auto& element : node.elements) {
                    visit(*element, locals);
                }
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                add_contract_command(contract, HandlerCommandKind::Spawn, node.resolved_template_id);
                for (const auto& override_entry : node.overrides) {
                    for (const auto& field : override_entry.assignments) {
                        visit(*field.value, locals);
                    }
                }
                visit_child_overrides(node.child_overrides, locals);
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                add_contract_call_effects(contract, node.resolved_callee_id);
                for (const auto& arg : node.named_args) {
                    visit(*arg.value, locals);
                }
            }
        },
        expr.expr);
}

void SemanticAnalyzer::walk_handler_body(  // NOLINT(readability-function-cognitive-complexity) -- the single
                                           // consolidated home for what used to be two independently-duplicated AST
                                           // walkers; see design.md task 3.1
    const std::vector<std::unique_ptr<StmtNode>>& body,
    LocalNames handler_locals,
    InferredHandlerContract& contract,
    const std::function<bool(const ExprNode&, const LocalNames&)>& resolve_read,
    const std::function<void(const VarAssign&, const LocalNames&)>& handle_var_assign,
    const std::function<void(const SymbolId&)>& on_project_trait) const {
    auto add_command = [&contract](HandlerCommandKind kind, std::optional<SymbolId> target) {
        add_contract_command(contract, kind, std::move(target));
    };

    std::function<void(const std::vector<ChildOverrideNode>&, const LocalNames&)> visit_child_overrides;
    std::function<void(const std::vector<std::unique_ptr<StmtNode>>&, LocalNames)> visit_stmts;
    auto visit_expr = [&](const ExprNode& expr, const LocalNames& locals) {
        walk_expression_reads(expr, locals, contract, resolve_read);
    };
    visit_child_overrides = [&](const std::vector<ChildOverrideNode>& overrides, const LocalNames& locals) {
        for (const auto& child : overrides) {
            for (const auto& trait : child.traits) {
                for (const auto& field : trait.assignments) {
                    visit_expr(*field.value, locals);
                }
            }
            visit_child_overrides(child.children, locals);
        }
    };
    visit_stmts = [&](const std::vector<std::unique_ptr<StmtNode>>& stmts, LocalNames locals) {
        for (const auto& stmt : stmts) {
            std::visit(
                [&](const auto& node) {
                    using S = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<S, LetStmt>) {
                        visit_expr(*node.value, locals);
                        locals.insert(node.name);
                    } else if constexpr (std::is_same_v<S, VarAssign>) {
                        visit_expr(*node.value, locals);
                        handle_var_assign(node, locals);
                    } else if constexpr (std::is_same_v<S, EmitStmt>) {
                        if (node.resolved_event_id.has_value()) {
                            contract.emits.insert(*node.resolved_event_id);
                        }
                        if (node.target.has_value()) {
                            visit_expr(**node.target, locals);
                        }
                        for (const auto& field : node.payload) {
                            visit_expr(*field.value, locals);
                        }
                    } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                        add_command(HandlerCommandKind::Spawn, node.resolved_template_id);
                        for (const auto& override_entry : node.overrides) {
                            for (const auto& field : override_entry.assignments) {
                                visit_expr(*field.value, locals);
                            }
                        }
                        visit_child_overrides(node.child_overrides, locals);
                    } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                        add_command(HandlerCommandKind::Destroy, std::nullopt);
                        if (node.target_expr.has_value()) {
                            visit_expr(**node.target_expr, locals);
                        }
                    } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                        add_command(HandlerCommandKind::Add, node.resolved_trait_id);
                        for (const auto& field : node.args) {
                            visit_expr(*field.value, locals);
                        }
                        if (node.target_expr.has_value()) {
                            visit_expr(**node.target_expr, locals);
                        }
                    } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                        add_command(HandlerCommandKind::Remove, node.resolved_trait_id);
                        if (node.target_expr.has_value()) {
                            visit_expr(**node.target_expr, locals);
                        }
                    } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                        if (node.resolved_trait_id.has_value()) {
                            on_project_trait(*node.resolved_trait_id);
                        }
                        for (const auto& field : node.args) {
                            visit_expr(*field.value, locals);
                        }
                        if (node.target_expr.has_value()) {
                            visit_expr(**node.target_expr, locals);
                        }
                    } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                        if (node.value.has_value()) {
                            visit_expr(**node.value, locals);
                        }
                    } else if constexpr (std::is_same_v<S, ExprStmt>) {
                        visit_expr(*node.expr, locals);
                    } else if constexpr (std::is_same_v<S, IfStmt>) {
                        visit_expr(*node.condition, locals);
                        visit_stmts(node.then_body, locals);
                        for (const auto& branch : node.else_if_branches) {
                            visit_expr(*branch.condition, locals);
                            visit_stmts(branch.body, locals);
                        }
                        visit_stmts(node.else_body, locals);
                    } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                        visit_expr(*node.iterable, locals);
                        auto loop_locals = locals;
                        loop_locals.insert(node.var_name);
                        visit_stmts(node.body, std::move(loop_locals));
                    } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                        visit_expr(*node.subject, locals);
                        for (const auto& arm : node.arms) {
                            if (arm.resolved_trait_id.has_value()) {
                                contract.reads.insert(*arm.resolved_trait_id);
                            }
                            auto arm_locals = locals;
                            if (arm.alias.has_value()) {
                                arm_locals.insert(*arm.alias);
                            }
                            visit_stmts(arm.body, std::move(arm_locals));
                        }
                        if (node.wildcard.has_value()) {
                            visit_stmts(node.wildcard->body, locals);
                        }
                    }
                },
                stmt->stmt);
        }
    };

    visit_stmts(body, std::move(handler_locals));
}

InferredHandlerContract
SemanticAnalyzer::infer_regular_handler_contract(  // NOLINT(readability-function-cognitive-complexity) -- still 57
                                                   // after AST-walker extraction (aliases/trait_for_field
                                                   // read-resolution strategy); not further in scope here
    const RuleNode& rule,
    const EventHandlerNode& handler) const {
    InferredHandlerContract contract;
    contract.rule        = *rule.resolved_rule_id;
    contract.trigger     = *handler.resolved_trigger;
    contract.selection   = rule.filter.resolved_trait_ids;
    contract.exclusion   = rule.exclude.resolved_trait_ids;
    contract.domain_kind = contract.selection.empty() && contract.exclusion.empty() ? HandlerDomainKind::Selectionless
                                                                                    : HandlerDomainKind::Unary;

    std::unordered_map<std::string, SymbolId> aliases;
    auto bind_clause = [this, &aliases](const FilterClause& clause) {
        for (const auto& entry : clause.entries) {
            auto symbol = entry.resolved_trait_id.has_value() ? entry.resolved_trait_id
                                                              : try_resolve_trait_ref_to_symbol(entry.qualified_name);
            if (!symbol.has_value()) {
                continue;
            }
            aliases[entry.qualified_name] = *symbol;
            aliases[symbol->local_name]   = *symbol;
            if (entry.alias.has_value()) {
                aliases[*entry.alias] = *symbol;
            }
        }
        for (const auto& name : clause.trait_names) {
            if (auto symbol = try_resolve_trait_ref_to_symbol(name); symbol.has_value()) {
                aliases[name] = *symbol;
            }
        }
    };
    bind_clause(rule.filter);

    auto trait_for_field = [this, &aliases](const std::string& field) -> std::optional<SymbolId> {
        std::optional<SymbolId> match;
        for (const auto& [_, symbol] : aliases) {
            const auto* trait = find_resolved_trait(make_canonical_id(symbol));
            if (trait == nullptr || !std::ranges::any_of(trait->fields, [&field](const auto& candidate) {
                    return candidate.name == field;
                })) {
                continue;
            }
            if (match.has_value() && *match != symbol) {
                return std::nullopt;
            }
            match = symbol;
        }
        return match;
    };
    auto add_write = [&contract](const SymbolId& symbol) {
        // Contract writes are read/write capabilities, not write-only access.
        contract.reads.insert(symbol);
        contract.writes.insert(symbol);
    };

    auto resolve_read = [&](const ExprNode& expr, const LocalNames& locals) -> bool {
        if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
            if (!locals.contains(ident->name)) {
                if (auto trait = trait_for_field(ident->name); trait.has_value()) {
                    contract.reads.insert(*trait);
                }
            }
            return true;
        }
        if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
            if (const auto* owner = std::get_if<IdentExpr>(&member->object->expr);
                owner != nullptr && aliases.contains(owner->name)) {
                contract.reads.insert(aliases.at(owner->name));
                return true;
            }
        }
        return false;
    };
    auto handle_var_assign = [&](const VarAssign& node, const LocalNames& locals) {
        if (locals.contains(node.name)) {
            return;
        }
        if (auto alias = aliases.find(node.name); alias != aliases.end()) {
            add_write(alias->second);
        } else if (!node.path.empty()) {
            // Dotted assignment whose base identifier isn't itself a bound
            // filter alias (e.g. a binding-relative trait path, per
            // VarAssign::path's contract in ast.hpp). Falling through to
            // trait_for_field(node.name) here would search for a *field*
            // literally named after the binding identifier, which is the
            // wrong lookup for a dotted target and — since no trait plausibly
            // has a field coincidentally matching a binding name — silently
            // drops the write from the contract, which
            // program_linker.cpp:254-283 relies on to detect RAW/WAW
            // scheduling conflicts between rules. Resolve through the path
            // instead: try the leading segment as a trait reference first
            // (the binding-relative-trait-path idiom), then fall back to
            // treating the trailing segment as a field name on one of the
            // rule's aliased traits.
            if (auto trait = try_resolve_trait_ref_to_symbol(node.path.front()); trait.has_value()) {
                add_write(*trait);
            } else if (auto field_trait = trait_for_field(node.path.back()); field_trait.has_value()) {
                add_write(*field_trait);
            }
        } else if (auto trait = trait_for_field(node.name); trait.has_value()) {
            add_write(*trait);
        }
    };

    // An order by: key is an ordinary expression, so its reads resolve through
    // the same walk and the same resolve_read strategy the handler body uses —
    // a computed key reading a trait the body never mentions still reaches the
    // contract (handler-contracts).
    for (const auto& key : rule.order_by) {
        walk_expression_reads(*key.expression, LocalNames{}, contract, resolve_read);
    }
    LocalNames handler_locals;
    handler_locals.insert(handler.event_name);
    if (handler.alias.has_value()) {
        handler_locals.insert(*handler.alias);
    }
    walk_handler_body(handler.body, std::move(handler_locals), contract, resolve_read, handle_var_assign, add_write);
    return contract;
}

InferredHandlerContract SemanticAnalyzer::infer_pair_handler_contract(const RuleNode& rule,
                                                                      const EventHandlerNode& handler,
                                                                      const PairScope& pair_scope) const {
    InferredHandlerContract contract;
    contract.rule        = *rule.resolved_rule_id;
    contract.trigger     = *handler.resolved_trigger;
    contract.domain_kind = HandlerDomainKind::Pair;

    for (const auto& binding : rule.pairs->bindings) {
        RelationBinding relation;
        relation.name = binding.name;
        for (const auto& entry : binding.traits) {
            if (entry.resolved_trait_id.has_value()) {
                relation.required_traits.push_back(*entry.resolved_trait_id);
            }
        }
        contract.pair_bindings.push_back(std::move(relation));
    }

    // Attempts to resolve `expr` as a pair-bound member chain rooted at a
    // binding identifier (e.g. `body.tf.WorldTransform.position`); records a
    // bound read and returns true when the chain is binding-rooted (whether
    // or not it resolved — an invalid access is already diagnosed by
    // validate_event_stmts, so there is nothing further to record here).
    auto try_record_pair_read = [&](const ExprNode& expr) -> bool {
        auto chain = member_chain_segments(std::get<MemberExpr>(expr.expr));
        if (!chain.has_value()) {
            return false;
        }
        const std::string& root_name = chain->front();
        std::vector<std::string> segments(chain->begin() + 1, chain->end());
        if (!pair_scope.contains(root_name)) {
            return false;
        }
        if (auto resolved = resolve_pair_member_chain(root_name, segments, pair_scope); resolved.has_value()) {
            BoundTraitAccess access{.binding_index = resolved->binding_index, .trait = resolved->trait_id};
            if (std::ranges::find(contract.bound_reads, access) == contract.bound_reads.end()) {
                contract.bound_reads.push_back(access);
            }
            contract.reads.insert(resolved->trait_id);
        }
        return true;
    };

    auto resolve_read = [&](const ExprNode& expr, const LocalNames&) -> bool {
        // Bare pair-binding references and locals carry no read on their own.
        return std::holds_alternative<MemberExpr>(expr.expr) && try_record_pair_read(expr);
    };
    // Rejected by validate_event_stmts (pair traits are read-only); the value
    // is already visited for reads by walk_handler_body before this runs.
    auto handle_var_assign = [](const VarAssign&, const LocalNames&) {};
    auto on_project_trait  = [&contract](const SymbolId& trait) { contract.projects.insert(trait); };

    // Same walk and same pair-read primitive as the handler body, so a sort
    // key reading through both bindings records both binding-qualified reads
    // (handler-contracts).
    for (const auto& key : rule.order_by) {
        walk_expression_reads(*key.expression, LocalNames{}, contract, resolve_read);
    }

    LocalNames handler_locals;
    handler_locals.insert(handler.event_name);
    if (handler.alias.has_value()) {
        handler_locals.insert(*handler.alias);
    }
    for (const auto& binding : rule.pairs->bindings) {
        handler_locals.insert(binding.name);
    }
    walk_handler_body(
        handler.body, std::move(handler_locals), contract, resolve_read, handle_var_assign, on_project_trait);
    return contract;
}

std::optional<SemanticAnalyzer::SpatialJoinResolvedArg> SemanticAnalyzer::resolve_spatial_join_arg(
    const ExprNode& arg, const PairScope& pair_scope) {
    const auto* member = std::get_if<MemberExpr>(&arg.expr);
    if (member == nullptr) {
        return std::nullopt;
    }
    auto chain = member_chain_segments(*member);
    if (!chain.has_value() || chain->size() < 2) {
        return std::nullopt;
    }
    const std::string& root_name = chain->front();
    std::vector<std::string> segments(chain->begin() + 1, chain->end());
    auto resolved = resolve_pair_member_chain(root_name, segments, pair_scope);
    if (!resolved.has_value()) {
        return std::nullopt;
    }
    std::vector<std::string> field_path(
        segments.begin() + static_cast<std::ptrdiff_t>(resolved->consumed_segments), segments.end());
    return SpatialJoinResolvedArg{
        .binding_name  = root_name,
        .access        = SpatialJoinAccess{.trait = resolved->trait_id, .field_path = std::move(field_path)},
        .binding_index = resolved->binding_index};
}

std::optional<SemanticAnalyzer::SpatialJoinMatch> SemanticAnalyzer::try_recognize_spatial_predicate(
    const CallExpr& call, const PairScope& pair_scope) {
    if (!call.resolved_callee_id.has_value() || call.args.size() != 4) {
        return std::nullopt;
    }
    const auto canonical = make_canonical_id(*call.resolved_callee_id);
    SpatialJoinDimension dimension{};
    if (canonical == "std.collision.flat.circles_overlap") {
        dimension = SpatialJoinDimension::Flat2D;
    } else if (canonical == "std.collision.volume.spheres_overlap") {
        dimension = SpatialJoinDimension::Volume3D;
    } else {
        return std::nullopt;
    }

    const auto left_position  = resolve_spatial_join_arg(*call.args[0], pair_scope);
    const auto left_radius    = resolve_spatial_join_arg(*call.args[1], pair_scope);
    const auto right_position = resolve_spatial_join_arg(*call.args[2], pair_scope);
    const auto right_radius   = resolve_spatial_join_arg(*call.args[3], pair_scope);
    if (!left_position.has_value() || !left_radius.has_value() || !right_position.has_value() ||
        !right_radius.has_value()) {
        return std::nullopt;
    }
    if (left_position->binding_name != left_radius->binding_name ||
        right_position->binding_name != right_radius->binding_name ||
        left_position->binding_name == right_position->binding_name) {
        return std::nullopt;
    }

    return SpatialJoinMatch{
        .dimension = dimension,
        .left      = SpatialJoinBinding{.binding_index = left_position->binding_index,
                                        .position      = left_position->access,
                                        .radius        = left_radius->access},
        .right     = SpatialJoinBinding{.binding_index = right_position->binding_index,
                                        .position      = right_position->access,
                                        .radius        = right_radius->access},
    };
}

bool SemanticAnalyzer::spatial_join_resolved_args_equal(const SpatialJoinResolvedArg& lhs,
                                                        const SpatialJoinResolvedArg& rhs) {
    return lhs.binding_name == rhs.binding_name && lhs.binding_index == rhs.binding_index && lhs.access == rhs.access;
}

bool SemanticAnalyzer::spatial_join_operand_pairs_equal(const SpatialJoinOperandPair& lhs,
                                                        const SpatialJoinOperandPair& rhs) {
    return spatial_join_resolved_args_equal(lhs.first, rhs.first) && spatial_join_resolved_args_equal(lhs.second, rhs.second);
}

std::optional<SemanticAnalyzer::SpatialJoinOperandPair> SemanticAnalyzer::resolve_spatial_join_operand_pair(
    const ExprNode& expr, const std::string& op, const PairScope& pair_scope) {
    const auto* binary = std::get_if<BinaryExpr>(&expr.expr);
    if (binary == nullptr || binary->op != op) {
        return std::nullopt;
    }
    auto first  = resolve_spatial_join_arg(*binary->left, pair_scope);
    auto second = resolve_spatial_join_arg(*binary->right, pair_scope);
    if (!first.has_value() || !second.has_value()) {
        return std::nullopt;
    }
    return SpatialJoinOperandPair{.first = std::move(*first), .second = std::move(*second)};
}

std::optional<SemanticAnalyzer::SpatialJoinMatch> SemanticAnalyzer::try_recognize_manual_distance_predicate(
    const BinaryExpr& comparison, const PairScope& pair_scope) {
    if (comparison.op != "<" && comparison.op != "<=") {
        return std::nullopt;
    }
    const auto* dot_call = std::get_if<CallExpr>(&comparison.left->expr);
    if (dot_call == nullptr || !dot_call->resolved_callee_id.has_value() || dot_call->args.size() != 2) {
        return std::nullopt;
    }
    SpatialJoinDimension dimension{};
    const auto canonical = make_canonical_id(*dot_call->resolved_callee_id);
    if (canonical == "std.math.vec2.dot") {
        dimension = SpatialJoinDimension::Flat2D;
    } else if (canonical == "std.math.vec3.dot") {
        dimension = SpatialJoinDimension::Volume3D;
    } else {
        return std::nullopt;
    }

    // dot(delta, delta): both call arguments must resolve to the exact same
    // subtraction (same binding roots, same field paths, same operand order).
    const auto delta_a = resolve_spatial_join_operand_pair(*dot_call->args[0], "-", pair_scope);
    const auto delta_b = resolve_spatial_join_operand_pair(*dot_call->args[1], "-", pair_scope);
    if (!delta_a.has_value() || !delta_b.has_value() || !spatial_join_operand_pairs_equal(*delta_a, *delta_b)) {
        return std::nullopt;
    }

    // (radius_sum) * (radius_sum): the multiplication's two operands must be
    // the exact same binding-rooted radius sum.
    const auto* square = std::get_if<BinaryExpr>(&comparison.right->expr);
    if (square == nullptr || square->op != "*") {
        return std::nullopt;
    }
    const auto sum_left  = resolve_spatial_join_operand_pair(*square->left, "+", pair_scope);
    const auto sum_right = resolve_spatial_join_operand_pair(*square->right, "+", pair_scope);
    if (!sum_left.has_value() || !sum_right.has_value() || !spatial_join_operand_pairs_equal(*sum_left, *sum_right)) {
        return std::nullopt;
    }

    const auto& position_first  = delta_a->first;
    const auto& position_second = delta_a->second;
    if (position_first.binding_name == position_second.binding_name) {
        return std::nullopt;
    }

    // Positions and radii are matched by binding name, not by argument
    // order -- the canonical shape's delta ("b - a") and radius sum
    // ("a + b") don't share an operand order.
    const auto& radius_first  = sum_left->first;
    const auto& radius_second = sum_left->second;
    const SpatialJoinResolvedArg* radius_for_first  = nullptr;
    const SpatialJoinResolvedArg* radius_for_second = nullptr;
    if (position_first.binding_name == radius_first.binding_name) {
        radius_for_first = &radius_first;
    } else if (position_first.binding_name == radius_second.binding_name) {
        radius_for_first = &radius_second;
    }
    if (position_second.binding_name == radius_first.binding_name) {
        radius_for_second = &radius_first;
    } else if (position_second.binding_name == radius_second.binding_name) {
        radius_for_second = &radius_second;
    }
    if (radius_for_first == nullptr || radius_for_second == nullptr) {
        return std::nullopt;
    }

    return SpatialJoinMatch{
        .dimension = dimension,
        .left      = SpatialJoinBinding{.binding_index = position_first.binding_index,
                                        .position      = position_first.access,
                                        .radius        = radius_for_first->access},
        .right     = SpatialJoinBinding{.binding_index = position_second.binding_index,
                                        .position      = position_second.access,
                                        .radius        = radius_for_second->access},
    };
}

void SemanticAnalyzer::check_unaccelerated_distance_predicate(const ExprNode& predicate,
                                                               const PairScope& pair_scope,
                                                               ErrorReporter& errors) {
    static const std::unordered_set<std::string> COMPARISON_OPS = {"<", "<=", ">", ">="};
    const auto* comparison = std::get_if<BinaryExpr>(&predicate.expr);
    if (comparison == nullptr || !COMPARISON_OPS.contains(comparison->op)) {
        return;
    }
    const auto* distance_call = std::get_if<CallExpr>(&comparison->left->expr);
    if (distance_call == nullptr || !distance_call->resolved_callee_id.has_value() || distance_call->args.size() != 2) {
        return;
    }
    const char* recognized_alternative = nullptr;
    const auto canonical = make_canonical_id(*distance_call->resolved_callee_id);
    if (canonical == "std.math.vec2.distance") {
        recognized_alternative = "circles_overlap";
    } else if (canonical == "std.math.vec3.distance") {
        recognized_alternative = "spheres_overlap";
    } else {
        return;
    }

    const auto position_a = resolve_spatial_join_arg(*distance_call->args[0], pair_scope);
    const auto position_b = resolve_spatial_join_arg(*distance_call->args[1], pair_scope);
    if (!position_a.has_value() || !position_b.has_value() || position_a->binding_name == position_b->binding_name) {
        return;
    }

    const auto radius_sum = resolve_spatial_join_operand_pair(*comparison->right, "+", pair_scope);
    if (!radius_sum.has_value()) {
        return;
    }
    const bool bindings_match = (radius_sum->first.binding_name == position_a->binding_name &&
                                 radius_sum->second.binding_name == position_b->binding_name) ||
                                (radius_sum->first.binding_name == position_b->binding_name &&
                                 radius_sum->second.binding_name == position_a->binding_name);
    if (!bindings_match) {
        return;
    }

    errors.warning(predicate.location,
                   std::string("where: predicate compares an unaccelerated linear distance (") + canonical +
                       ") against a binding-rooted radius sum; use " + recognized_alternative +
                       "(...) or an equivalent squared-distance expression for broad-phase acceleration");
}

std::optional<SpatialJoinPlan> SemanticAnalyzer::recognize_spatial_join(const RuleNode& rule,
                                                                        const PairScope& pair_scope,
                                                                        ErrorReporter& errors) {
    if (!rule.pairs.has_value() || rule.pairs->bindings.size() != 2 || !rule.where_clause.has_value()) {
        return std::nullopt;
    }

    // Broad-phase eligibility requires matching pair-binding domains
    // (dsl-where-clause), independent of predicate shape, so this never
    // varies per-predicate below -- check it once up front.
    const auto required_traits = [](const PairBindingNode& binding) {
        std::unordered_set<SymbolId, SymbolIdHash> traits;
        for (const auto& entry : binding.traits) {
            if (entry.resolved_trait_id.has_value()) {
                traits.insert(*entry.resolved_trait_id);
            }
        }
        return traits;
    };
    if (required_traits(rule.pairs->bindings[0]) != required_traits(rule.pairs->bindings[1])) {
        return std::nullopt;
    }

    const auto& predicates = rule.where_clause->predicates;
    for (std::size_t predicate_index = 0; predicate_index < predicates.size(); ++predicate_index) {
        const ExprNode& predicate = *predicates[predicate_index];
        std::optional<SpatialJoinMatch> match;
        if (const auto* call = std::get_if<CallExpr>(&predicate.expr); call != nullptr) {
            match = try_recognize_spatial_predicate(*call, pair_scope);
        }
        if (!match.has_value()) {
            if (const auto* comparison = std::get_if<BinaryExpr>(&predicate.expr); comparison != nullptr) {
                match = try_recognize_manual_distance_predicate(*comparison, pair_scope);
            }
        }
        if (!match.has_value()) {
            check_unaccelerated_distance_predicate(predicate, pair_scope, errors);
            continue;
        }
        return SpatialJoinPlan{.dimension               = match->dimension,
                               .left                     = match->left,
                               .right                    = match->right,
                               .matched_predicate_index = predicate_index};
    }
    return std::nullopt;
}

void SemanticAnalyzer::collect_rule_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts, RuleDependency& dep) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &dep](auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, LetStmt> || std::is_same_v<S, VarAssign>) {
                    if constexpr (std::is_same_v<S, VarAssign>) {
                        dep.writes.insert(s.name);
                    }
                } else if constexpr (std::is_same_v<S, EmitStmt>) {
                    dep.emits.insert(s.event_name);
                } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                    dep.writes.insert(s.trait_name);
                } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                    collect_rule_deps(s.body, dep);
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    collect_rule_deps(s.then_body, dep);
                    for (const auto& branch : s.else_if_branches) {
                        collect_rule_deps(branch.body, dep);
                    }
                    collect_rule_deps(s.else_body, dep);
                } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                    for (const auto& arm : s.arms) {
                        dep.reads.insert(arm.trait_name);
                        collect_rule_deps(arm.body, dep);
                    }
                    if (s.wildcard.has_value()) {
                        collect_rule_deps(s.wildcard->body, dep);
                    }
                }
            },
            stmt->stmt);
    }
}

// ── Dynamic ECS: Helpers ────────────────────────────────────────────────────

bool SemanticAnalyzer::is_trait_declared(const std::string& name) const {
    return try_resolve_ref_of_kind(name, {SymbolKind::Trait}).has_value();
}

bool SemanticAnalyzer::imported_symbols_contain_non_template(const ImportedSymbols& symbols, const std::string& name) {
    return symbols.traits.contains(name) || symbols.structs.contains(name) || symbols.enums.contains(name) ||
           symbols.funcs.contains(name) || symbols.events.contains(name);
}

bool SemanticAnalyzer::local_non_template_symbol_exists(const std::string& name) const {
    return trait_names_.contains(name) || entity_names_.contains(name) || struct_names_.contains(name) ||
           enum_names_.contains(name) || event_names_.contains(name) || func_names_.contains(name) ||
           rule_names_.contains(name) || const_names_.contains(name) || asset_decl_types_.contains(name) ||
           input_decl_types_.contains(name) || use_names_.contains(name);
}

bool SemanticAnalyzer::imported_non_template_symbol_exists(const std::string& name) const {
    if (imports_.trait_providers.contains(name) || imports_.struct_providers.contains(name) ||
        imports_.enum_providers.contains(name) || imports_.func_providers.contains(name)) {
        return true;
    }
    return std::ranges::any_of(imports_.modules,
                               [&name](const auto& entry) { return entry.second.events.contains(name); });
}

bool SemanticAnalyzer::resolve_archetype_template_use(const ArchetypeTemplateUseEntry& use,
                                                      const std::string& archetype_kind,
                                                      const std::string& archetype_name) {
    auto report_non_template = [this, &use, &archetype_kind, &archetype_name](const std::string& referenced) {
        errors_.error(use.location,
                      "archetype-body use '" + use.template_name + "' in " + archetype_kind + " '" + archetype_name +
                          "' must reference a template; '" + referenced + "' is not a template");
    };

    const auto dot = use.template_name.rfind('.');
    if (dot != std::string::npos) {
        const auto qualifier     = use.template_name.substr(0, dot);
        const auto template_name = use.template_name.substr(dot + 1);

        auto module_it = imports_.modules.find(qualifier);
        if (module_it == imports_.modules.end()) {
            errors_.error(use.location, "unknown module qualifier '" + qualifier + "' in archetype-body template use");
            return false;
        }

        if (module_it->second.templates.contains(template_name)) {
            return true;
        }

        auto non_pub_it = imports_.non_pub_template_names.find(qualifier);
        if (non_pub_it != imports_.non_pub_template_names.end() && non_pub_it->second.contains(template_name)) {
            errors_.error(use.location,
                          "template '" + template_name + "' is not public in module '" + qualifier +
                              "'; did you mean to mark it as 'pub'?");
            return false;
        }

        if (imported_symbols_contain_non_template(module_it->second, template_name)) {
            report_non_template(template_name);
            return false;
        }

        errors_.error(use.location, "unknown template '" + template_name + "' in module '" + qualifier + "'");
        return false;
    }

    if (template_names_.contains(use.template_name)) {
        return true;
    }

    if (local_non_template_symbol_exists(use.template_name)) {
        report_non_template(use.template_name);
        return false;
    }

    if (!imports_.empty()) {
        auto provider_it = imports_.template_providers.find(use.template_name);
        if (provider_it != imports_.template_providers.end()) {
            if (find_std_core_provider(imports_, provider_it->second).has_value()) {
                return true;
            }
            errors_.error(use.location,
                          imported_reference_diagnostic(imports_, "template", use.template_name, provider_it->second));
            return false;
        }

        for (const auto& [qualifier, names] : imports_.non_pub_template_names) {
            if (names.contains(use.template_name)) {
                errors_.error(use.location,
                              "template '" + use.template_name + "' is not public in module '" + qualifier +
                                  "'; did you mean to mark it as 'pub'?");
                return false;
            }
        }

        if (imported_non_template_symbol_exists(use.template_name)) {
            report_non_template(use.template_name);
            return false;
        }
    }

    errors_.error(use.location, "undefined template '" + use.template_name + "' in archetype-body use");
    return false;
}

const ResolvedTrait* SemanticAnalyzer::find_resolved_trait(const std::string& name) const {
    auto it = result_.traits.find(name);
    if (it != result_.traits.end()) {
        return &it->second;
    }
    // Handle canonical IDs and qualified references (dotted names).
    auto dot = name.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = name.substr(0, dot);
        auto local_name = name.substr(dot + 1);

        // Local canonical ID: strip the current module prefix.
        if (qualifier == current_module_name_) {
            auto lit = result_.traits.find(local_name);
            if (lit != result_.traits.end()) {
                return &lit->second;
            }
        }

        // Qualifier matches an alias/module in imports.
        auto mod_it = imports_.modules.find(qualifier);
        if (mod_it != imports_.modules.end()) {
            auto tit = mod_it->second.traits.find(local_name);
            if (tit != mod_it->second.traits.end()) {
                return &tit->second;
            }
        }

        // Full canonical ID scan: find the trait whose canonical_id matches.
        for (const auto& [q, syms] : imports_.modules) {
            for (const auto& [local, trait] : syms.traits) {
                if (trait.canonical_id == name) {
                    return &trait;
                }
            }
        }
        for (const auto& [local, trait] : result_.traits) {
            if (trait.canonical_id == name) {
                return &trait;
            }
        }
        return nullptr;
    }
    if (!imports_.empty()) {
        auto pit = imports_.trait_providers.find(name);
        if (pit != imports_.trait_providers.end()) {
            if (const auto prelude = find_std_core_provider(imports_, pit->second)) {
                const auto& qualifier = *prelude;
                auto mit              = imports_.modules.find(qualifier);
                if (mit != imports_.modules.end()) {
                    auto tit = mit->second.traits.find(name);
                    if (tit != mit->second.traits.end()) {
                        return &tit->second;
                    }
                }
            }
        }
        return nullptr;
    }

    return nullptr;
}

const ResolvedTrait* SemanticAnalyzer::find_resolved_trait(const SymbolId& symbol) const {
    if (symbol.kind != SymbolKind::Trait) {
        return nullptr;
    }

    if (symbol.module.name == current_module_name_) {
        if (auto it = result_.traits.find(symbol.local_name); it != result_.traits.end()) {
            return &it->second;
        }
    }

    const auto canonical = make_canonical_id(symbol);
    for (const auto& [_, syms] : imports_.modules) {
        if (syms.module_name != symbol.module.name) {
            continue;
        }
        if (auto it = syms.traits.find(symbol.local_name); it != syms.traits.end()) {
            return &it->second;
        }
        for (const auto& [_, trait] : syms.traits) {
            if ((trait.symbol_id.has_value() && *trait.symbol_id == symbol) || trait.canonical_id == canonical) {
                return &trait;
            }
        }
    }

    for (const auto& [_, trait] : result_.traits) {
        if ((trait.symbol_id.has_value() && *trait.symbol_id == symbol) || trait.canonical_id == canonical) {
            return &trait;
        }
    }

    return nullptr;
}

const ResolvedTrait* SemanticAnalyzer::find_resolved_trait(const std::optional<SymbolId>& symbol,
                                                           const std::string& fallback_name) const {
    if (symbol.has_value()) {
        if (const auto* trait = find_resolved_trait(*symbol); trait != nullptr) {
            return trait;
        }
    }
    return find_resolved_trait(fallback_name);
}

const ResolvedFunc* SemanticAnalyzer::find_resolved_func(const SymbolId& symbol) const {
    if (symbol.kind != SymbolKind::Func) {
        return nullptr;
    }
    if (symbol.module == current_module_id_) {
        const auto found = result_.funcs.find(symbol.local_name);
        return found == result_.funcs.end() ? nullptr : &found->second;
    }
    for (const auto& [_, imported] : imports_.modules) {
        if (imported.module_name != symbol.module.name) {
            continue;
        }
        const auto found = imported.funcs.find(symbol.local_name);
        if (found != imported.funcs.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

const ResolvedStruct* SemanticAnalyzer::find_resolved_event(const std::string& name) const {
    auto it = event_structs_.find(name);
    if (it != event_structs_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ── Task 3.1: Canonical trait ID resolution ────────────────────────────────────

std::string SemanticAnalyzer::resolve_trait_ref_to_canonical(const std::string& ref, const SourceLocation& loc) {
    // Unified lookup; the string-splitting below only produces diagnostics.
    if (auto resolved = try_resolve_ref_of_kind(ref, {SymbolKind::Trait})) {
        return make_canonical_id(*resolved);
    }

    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);

        if (qualifier != current_module_name_ && find_imported_module(qualifier) == nullptr) {
            errors_.error(loc, "unknown module qualifier '" + qualifier + "' in trait reference");
            return "";
        }
        auto np_it = imports_.non_pub_trait_names.find(qualifier);
        if (np_it != imports_.non_pub_trait_names.end() && np_it->second.contains(local_name)) {
            errors_.error(loc, "trait '" + local_name + "' is not public in module '" + qualifier + "'");
        } else {
            errors_.error(loc, "unknown trait '" + local_name + "' in module '" + qualifier + "'");
        }
        return "";
    }

    // Bare name provided only by ordinary (non-prelude) imports: point at the
    // qualified spelling.
    if (!imports_.empty()) {
        auto pit = imports_.trait_providers.find(ref);
        if (pit != imports_.trait_providers.end() && !pit->second.empty() &&
            !find_std_core_provider(imports_, pit->second).has_value()) {
            errors_.error(loc, imported_reference_diagnostic(imports_, "trait", ref, pit->second));
            return "";
        }
    }

    return "";  // not found; caller reports error as needed
}

std::optional<SymbolId> SemanticAnalyzer::try_resolve_trait_ref_to_symbol(const std::string& ref) const {
    return try_resolve_ref_of_kind(ref, {SymbolKind::Trait});
}

// ── Task 3.3: Canonical event ID resolution ─────────────────────────────────────

std::optional<SymbolId> SemanticAnalyzer::try_resolve_event_ref_to_symbol(const std::string& ref) const {
    // Unqualified: locally declared event takes priority over prelude; the
    // shared wrapper preserves this because resolve_name consults
    // module_scope_symbols_ (locally declared symbols only) before std.core.
    return try_resolve_ref_of_kind(ref, {SymbolKind::Event});
}

std::optional<SymbolId> SemanticAnalyzer::try_resolve_phase_ref_to_symbol(const std::string& ref) const {
    return try_resolve_ref_of_kind(ref, {SymbolKind::Phase});
}

std::optional<ResolvedHandlerTrigger> SemanticAnalyzer::try_resolve_handler_trigger(const std::string& ref) const {
    auto symbol = try_resolve_ref_of_kind(ref, {SymbolKind::Event, SymbolKind::Phase});
    if (symbol.has_value()) {
        const auto kind = symbol->kind == SymbolKind::Phase ? HandlerTriggerKind::Phase : HandlerTriggerKind::Event;
        return ResolvedHandlerTrigger{.kind = kind, .symbol = *symbol};
    }
    return try_resolve_render_stage_trigger(ref);
}

// dsl-render-passes: resolves `<phase>.vertex`/`<phase>.fragment` — a dotted
// reference whose head names a recognized render-pass phase and whose sole
// remaining segment is a stage name — to that phase's derived trigger.
// Cross-module render-pass phases are not supported yet: only a phase
// declared in the current module can be recognized here.
std::optional<ResolvedHandlerTrigger> SemanticAnalyzer::try_resolve_render_stage_trigger(const std::string& ref) const {
    auto resolved = resolve_name(dotted_segments(ref));
    if (!resolved.has_value() || resolved->symbol.kind != SymbolKind::Phase || resolved->member_segments.size() != 1) {
        return std::nullopt;
    }
    if (resolved->symbol.module != current_module_id_) {
        return std::nullopt;
    }
    const auto phase_it = result_.phases.find(resolved->symbol.local_name);
    if (phase_it == result_.phases.end() || !phase_it->second.render_pass.has_value()) {
        return std::nullopt;
    }
    const auto& stage = resolved->member_segments.front();
    if (stage == "vertex") {
        return phase_it->second.render_pass->vertex_trigger;
    }
    if (stage == "fragment") {
        return phase_it->second.render_pass->fragment_trigger;
    }
    return std::nullopt;
}

void SemanticAnalyzer::diagnose_unresolved_handler_trigger(const std::string& owner_desc,
                                                            const std::string& event_name,
                                                            const SourceLocation& loc) const {
    auto resolved = resolve_name(dotted_segments(event_name));
    if (resolved.has_value() && resolved->symbol.kind == SymbolKind::Phase && resolved->member_segments.size() == 1 &&
        (resolved->member_segments.front() == "vertex" || resolved->member_segments.front() == "fragment")) {
        errors_.error(loc,
                      "phase '" + make_canonical_id(resolved->symbol) + "' exposes no such stage ('" +
                          resolved->member_segments.front() + "'); it is not a render-pass phase");
        return;
    }
    errors_.error(loc, owner_desc + " handles unknown event or phase '" + event_name + "'");
}

// dsl-render-passes: builds the synthetic struct exposing a Quads render-pass
// stage's built-in input (read) and output (write) fields under the stage
// handler's own alias, reusing the same handler_event/local_bindings
// mechanism ordinary phase completion data already uses (validate_rule_filters).
// The stage is identified by the derived trigger symbol's synthesized suffix
// (see recognize_render_pass_phases) rather than by re-deriving it from the
// owning phase, since only one Pass kind (and therefore one field table)
// exists today.
std::optional<ResolvedStruct> SemanticAnalyzer::build_render_stage_activation_struct(const SymbolId& symbol) {
    static const std::string kVertexSuffix   = "__vertex";
    static const std::string kFragmentSuffix = "__fragment";

    ResolvedStruct activation;
    if (symbol.local_name.ends_with(kVertexSuffix)) {
        for (const auto& field : quads_vertex_input_fields()) {
            activation.fields.push_back(ResolvedField{.name = field.name, .type = field.type, .is_let = true});
        }
        for (const auto& field : quads_vertex_output_fields()) {
            activation.fields.push_back(ResolvedField{.name = field.name, .type = field.type, .is_var = true});
        }
    } else if (symbol.local_name.ends_with(kFragmentSuffix)) {
        for (const auto& field : quads_fragment_input_fields()) {
            activation.fields.push_back(ResolvedField{.name = field.name, .type = field.type, .is_let = true});
        }
        for (const auto& field : quads_fragment_output_fields()) {
            activation.fields.push_back(ResolvedField{.name = field.name, .type = field.type, .is_var = true});
        }
    } else {
        return std::nullopt;
    }
    activation.name = symbol.local_name;
    assign_canonical_identity(activation, symbol);
    return activation;
}

bool SemanticAnalyzer::is_external_event(const SymbolId& symbol) const {
    if (symbol.kind != SymbolKind::Event) {
        return false;
    }
    if (symbol.module == current_module_id_) {
        if (const auto it = result_.events.find(symbol.local_name); it != result_.events.end()) {
            return it->second.is_external;
        }
        return false;
    }
    for (const auto& [_, imported] : imports_.modules) {
        if (imported.module_name != symbol.module.name) {
            continue;
        }
        if (const auto it = imported.event_symbols.find(symbol.local_name); it != imported.event_symbols.end()) {
            return it->second.is_external;
        }
    }
    return false;
}

const std::vector<ResolvedField>* SemanticAnalyzer::find_event_fields(const SymbolId& symbol) const {
    if (symbol.kind != SymbolKind::Event) {
        return nullptr;
    }
    if (symbol.module == current_module_id_) {
        if (const auto found = result_.events.find(symbol.local_name); found != result_.events.end()) {
            return &found->second.fields;
        }
        return nullptr;
    }
    for (const auto& [_, imported] : imports_.modules) {
        if (imported.module_name != symbol.module.name) {
            continue;
        }
        if (const auto found = imported.event_symbols.find(symbol.local_name); found != imported.event_symbols.end()) {
            return &found->second.fields;
        }
    }
    return nullptr;
}

const ResolvedPhase* SemanticAnalyzer::find_local_phase(const SymbolId& symbol) const {
    if (symbol.kind != SymbolKind::Phase || symbol.module != current_module_id_) {
        return nullptr;
    }
    const auto found = result_.phases.find(symbol.local_name);
    return found == result_.phases.end() ? nullptr : &found->second;
}

const ImportedPhase* SemanticAnalyzer::find_imported_phase(const SymbolId& symbol) const {
    if (symbol.kind != SymbolKind::Phase || symbol.module == current_module_id_) {
        return nullptr;
    }
    for (const auto& [_, imported] : imports_.modules) {
        if (imported.module_name != symbol.module.name) {
            continue;
        }
        if (const auto found = imported.phase_symbols.find(symbol.local_name); found != imported.phase_symbols.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

const std::vector<ResolvedField>* SemanticAnalyzer::find_phase_fields(const SymbolId& symbol) const {
    if (const auto* local = find_local_phase(symbol); local != nullptr) {
        return &local->fields;
    }
    if (const auto* imported = find_imported_phase(symbol); imported != nullptr) {
        return &imported->fields;
    }
    return nullptr;
}

// ── Task 3.4: Canonical rule ID resolution ──────────────────────────────────────

std::optional<SymbolId> SemanticAnalyzer::try_resolve_rule_ref_to_symbol(const std::string& ref) const {
    return try_resolve_ref_of_kind(ref, {SymbolKind::Rule});
}

std::optional<SymbolId> SemanticAnalyzer::resolve_rule_after_ref_to_symbol(
    const std::string& ref,
    const SourceLocation& /*loc*/,
    const std::unordered_set<std::string>& /*local_rule_names*/) const {
    // All locally declared rules are registered in module_scope_symbols_,
    // which resolve_name consults, so the caller-collected name set is
    // redundant with the unified lookup.
    return try_resolve_ref_of_kind(ref, {SymbolKind::Rule});
}

// ── Task 3.5: Canonical template/entity ID resolution ───────────────────────────

std::optional<SymbolId> SemanticAnalyzer::try_resolve_template_ref_to_symbol(const std::string& ref) const {
    // Spawn/template references accept both template and entity declarations.
    return try_resolve_ref_of_kind(ref, {SymbolKind::Template, SymbolKind::Entity});
}

// ── Task 3.6: Canonical func ID resolution ──────────────────────────────────────

std::optional<SymbolId> SemanticAnalyzer::try_resolve_func_ref_to_symbol(const std::string& ref) const {
    // Missing-SymbolId fallback (synthesizing from the func's declaring module)
    // lives in lookup_imported_symbol via resolved_decl_symbol.
    return try_resolve_ref_of_kind(ref, {SymbolKind::Func});
}

// ── Unified name resolution (unified-name-resolution change, D1/D2/D4) ──────

const ImportedSymbols* SemanticAnalyzer::find_imported_module(const std::string& qualifier_or_canonical) const {
    auto it = imports_.modules.find(qualifier_or_canonical);
    if (it != imports_.modules.end()) {
        return &it->second;
    }
    // Canonical module paths are valid qualifiers even when the module was
    // imported under an alias (spec: alias and canonical are interchangeable).
    // Exact qualifier keys (aliases) win over the canonical scan; aliases are
    // single identifiers, so only dot-free module names can ever collide.
    for (const auto& [_, syms] : imports_.modules) {
        if (syms.module_name == qualifier_or_canonical) {
            return &syms;
        }
    }
    return nullptr;
}

std::optional<SymbolId> SemanticAnalyzer::lookup_imported_symbol(const ImportedSymbols& syms, const std::string& name) {
    // One namespace per module: at most one of these maps can hold the name.
    if (auto trait_it = syms.traits.find(name); trait_it != syms.traits.end()) {
        return resolved_decl_symbol(trait_it->second, SymbolKind::Trait, syms.module_name, name);
    }
    if (auto struct_it = syms.structs.find(name); struct_it != syms.structs.end()) {
        return resolved_decl_symbol(struct_it->second, SymbolKind::Struct, syms.module_name, name);
    }
    if (auto enum_it = syms.enums.find(name); enum_it != syms.enums.end()) {
        return resolved_decl_symbol(enum_it->second, SymbolKind::Enum, syms.module_name, name);
    }
    if (auto func_it = syms.funcs.find(name); func_it != syms.funcs.end()) {
        return resolved_decl_symbol(func_it->second, SymbolKind::Func, syms.module_name, name);
    }
    if (auto tmpl_it = syms.templates.find(name); tmpl_it != syms.templates.end()) {
        return tmpl_it->second.symbol_id.value_or(make_symbol_id(SymbolKind::Template, syms.module_name, name));
    }
    if (auto event_it = syms.event_symbols.find(name); event_it != syms.event_symbols.end()) {
        return event_it->second.symbol_id.value_or(make_symbol_id(SymbolKind::Event, syms.module_name, name));
    }
    if (auto phase_it = syms.phase_symbols.find(name); phase_it != syms.phase_symbols.end()) {
        return phase_it->second.symbol_id.value_or(make_symbol_id(SymbolKind::Phase, syms.module_name, name));
    }
    if (auto rule_it = syms.rules.find(name); rule_it != syms.rules.end()) {
        return rule_it->second.symbol_id.value_or(make_symbol_id(SymbolKind::Rule, syms.module_name, name));
    }
    if (auto fs_it = syms.func_symbols.find(name); fs_it != syms.func_symbols.end()) {
        return fs_it->second.symbol_id.value_or(make_symbol_id(SymbolKind::Func, syms.module_name, name));
    }
    return std::nullopt;
}

std::optional<SymbolId> SemanticAnalyzer::lookup_local_symbol(const std::string& name) const {
    auto it = module_scope_symbols_.find(name);
    if (it == module_scope_symbols_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<ResolvedRef> SemanticAnalyzer::resolve_name(const std::vector<std::string>& segments) const {
    if (segments.empty()) {
        return std::nullopt;
    }

    // Module qualifiers first, longest dotted prefix wins (D2); at least one
    // trailing segment must remain to name the symbol.
    for (std::size_t take = segments.size() - 1; take >= 1; --take) {
        std::string prefix = segments[0];
        for (std::size_t i = 1; i < take; ++i) {
            prefix += "." + segments[i];
        }
        const bool is_current      = prefix == current_module_name_;
        const ImportedSymbols* mod = find_imported_module(prefix);
        if (!is_current && mod == nullptr) {
            continue;
        }

        const std::string& sym_name = segments[take];
        std::vector<std::string> members(segments.begin() + static_cast<std::ptrdiff_t>(take) + 1, segments.end());
        if (is_current) {
            if (auto local = lookup_local_symbol(sym_name)) {
                return ResolvedRef{.symbol = *local, .member_segments = std::move(members)};
            }
        }
        if (mod != nullptr) {
            if (auto imported = lookup_imported_symbol(*mod, sym_name)) {
                return ResolvedRef{.symbol = *imported, .member_segments = std::move(members)};
            }
        }
        // The longest matching module qualifier is authoritative: a matched
        // module that lacks the symbol is a failure, not a cue to reinterpret
        // the prefix as a shorter module plus more segments.
        return std::nullopt;
    }

    // Bare name: local declarations, then the std.core prelude. Ordinary
    // imports stay namespace bindings (Oberon policy) — no unqualified lookup.
    std::vector<std::string> members(segments.begin() + 1, segments.end());
    if (auto local = lookup_local_symbol(segments[0])) {
        return ResolvedRef{.symbol = *local, .member_segments = std::move(members)};
    }
    if (const ImportedSymbols* prelude = find_imported_module("std.core")) {
        if (auto sym = lookup_imported_symbol(*prelude, segments[0])) {
            return ResolvedRef{.symbol = *sym, .member_segments = std::move(members)};
        }
    }
    return std::nullopt;
}

std::optional<ResolvedRef> SemanticAnalyzer::resolve_name_required(const std::vector<std::string>& segments,
                                                                   const SourceLocation& loc) {
    auto resolved = resolve_name(segments);
    if (resolved.has_value()) {
        return resolved;
    }

    std::string spelled = segments.empty() ? "" : segments[0];
    for (std::size_t i = 1; i < segments.size(); ++i) {
        spelled += "." + segments[i];
    }

    // When the head names a symbol some import provides, suggest qualified
    // spellings (alias and canonical) instead of a bare "unknown symbol".
    std::vector<std::string> suggestions;
    if (!segments.empty()) {
        const auto add_suggestions = [&](const std::unordered_map<std::string, std::vector<std::string>>& providers) {
            auto pit = providers.find(segments[0]);
            if (pit == providers.end()) {
                return;
            }
            for (const auto& qualifier : pit->second) {
                suggestions.push_back("'" + qualifier + "." + spelled + "'");
                const auto* mod = find_imported_module(qualifier);
                if (mod != nullptr && mod->module_name != qualifier) {
                    suggestions.push_back("'" + mod->module_name + "." + spelled + "'");
                }
            }
        };
        add_suggestions(imports_.enum_providers);
        add_suggestions(imports_.trait_providers);
        add_suggestions(imports_.struct_providers);
        add_suggestions(imports_.func_providers);
        add_suggestions(imports_.event_providers);
        add_suggestions(imports_.phase_providers);
    }

    std::string message = "unknown symbol '" + spelled + "'";
    if (!suggestions.empty()) {
        message += "; ordinary imports must be qualified: use " + suggestions[0];
        if (suggestions.size() > 1) {
            message += " or " + suggestions[1];
        }
    }
    errors_.error(loc, message);
    return std::nullopt;
}

std::optional<SymbolId> SemanticAnalyzer::try_resolve_ref_of_kind(const std::string& ref,
                                                                  std::initializer_list<SymbolKind> kinds) const {
    auto resolved = resolve_name(dotted_segments(ref));
    if (!resolved.has_value() || !resolved->member_segments.empty()) {
        return std::nullopt;
    }
    if (std::ranges::find(kinds, resolved->symbol.kind) == kinds.end()) {
        return std::nullopt;
    }
    return resolved->symbol;
}

std::optional<SymbolId> SemanticAnalyzer::resolve_callee_symbol(const ExprNode& callee) const {
    auto segments = callee_chain_segments(callee);
    if (!segments.has_value()) {
        return std::nullopt;
    }
    auto resolved = resolve_name(*segments);
    if (!resolved.has_value() || !resolved->member_segments.empty() || resolved->symbol.kind != SymbolKind::Func) {
        return std::nullopt;
    }
    return resolved->symbol;
}

const ResolvedEnum* SemanticAnalyzer::find_resolved_enum(const SymbolId& symbol) const {
    if (symbol.kind != SymbolKind::Enum) {
        return nullptr;
    }
    if (symbol.module.name == current_module_name_) {
        auto it = result_.enums.find(symbol.local_name);
        if (it != result_.enums.end()) {
            return &it->second;
        }
    }
    for (const auto& [_, syms] : imports_.modules) {
        if (syms.module_name != symbol.module.name) {
            continue;
        }
        auto it = syms.enums.find(symbol.local_name);
        if (it != syms.enums.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

void SemanticAnalyzer::resolve_enum_member_expr(MemberExpr& member, const SourceLocation& loc) {
    auto segments = member_chain_segments(member);
    if (!segments.has_value() || segments->size() < 2) {
        return;  // non-identifier head or too short — runtime member access
    }

    auto resolved = resolve_name(*segments);
    if (!resolved.has_value() || resolved->symbol.kind != SymbolKind::Enum) {
        return;  // not an enum reference — leave to type checking / callers
    }
    if (resolved->member_segments.empty()) {
        return;  // the chain names the enum type itself, not a member
    }
    const auto canonical = make_canonical_id(resolved->symbol);
    if (resolved->member_segments.size() > 1) {
        errors_.error(loc, "enum member '" + resolved->member_segments[0] + "' of '" + canonical + "' has no members");
        return;
    }
    const ResolvedEnum* enum_decl = find_resolved_enum(resolved->symbol);
    if (enum_decl == nullptr) {
        return;  // enum record unavailable (should not happen after phase 2)
    }
    const std::string& wanted = resolved->member_segments[0];
    for (std::size_t i = 0; i < enum_decl->variants.size(); ++i) {
        if (enum_decl->variants[i] == wanted) {
            member.resolved_enum_member = ResolvedEnumMember{
                .enum_id = resolved->symbol, .member = wanted, .index = static_cast<std::int32_t>(i)};
            return;
        }
    }
    errors_.error(loc, "'" + wanted + "' is not a member of enum '" + canonical + "'");
}

void SemanticAnalyzer::validate_input_decl_props(const InputDeclNode& node) {
    for (const auto& prop : node.props) {
        const bool is_key_prop   = prop.key == "key" || prop.key == "negative" || prop.key == "positive";
        const bool is_mouse_prop = prop.key == "mouse";
        const bool is_gamepad    = prop.key == "gamepad";

        if (!is_key_prop && !is_mouse_prop && !is_gamepad) {
            if (prop.key == "invert") {
                const auto* lit = std::get_if<LiteralExpr>(&prop.value->expr);
                if (lit == nullptr || lit->kind != LiteralExpr::Kind::Bool) {
                    errors_.error(prop.location, "input property 'invert' requires a bool value");
                }
            }
            continue;
        }

        std::string expected = "Key";
        if (is_mouse_prop) {
            expected = "MouseButton";
        } else if (is_gamepad) {
            expected = node.input_kind == InputKind::Button ? "GamepadButton" : "GamepadAxis";
        }
        const std::string expected_canonical = "std.input." + expected;

        const auto* member = std::get_if<MemberExpr>(&prop.value->expr);
        if (member == nullptr) {
            errors_.error(prop.location,
                          "input property '" + prop.key + "' must reference a " + expected_canonical + " member");
            continue;
        }

        if (member->resolved_enum_member.has_value()) {
            const auto& rem = *member->resolved_enum_member;
            if (rem.enum_id.module.name != "std.input" || rem.enum_id.local_name != expected) {
                errors_.error(prop.location,
                              "input property '" + prop.key + "' requires a " + expected_canonical + " member, got '" +
                                  make_canonical_id(rem.enum_id) + "." + rem.member + "'");
            }
            continue;
        }

        // No resolved member: produce a precise required-mode diagnostic,
        // avoiding a duplicate when resolve_enum_member_expr already reported
        // (known enum, unknown member).
        auto segments = member_chain_segments(*member);
        if (!segments.has_value() || segments->size() < 2) {
            errors_.error(prop.location,
                          "input property '" + prop.key + "' must reference a " + expected_canonical + " member");
            continue;
        }
        auto resolved = resolve_name(*segments);
        if (resolved.has_value()) {
            if (resolved->symbol.kind == SymbolKind::Enum) {
                continue;  // unknown-member error already reported by resolve_enum_member_expr
            }
            errors_.error(prop.location,
                          "input property '" + prop.key + "' requires a " + expected_canonical + " member, got '" +
                              make_canonical_id(resolved->symbol) + "'");
            continue;
        }
        (void)resolve_name_required(*segments, prop.location);
    }
}

// ── Task 3.4: Canonical rule ID resolution for after: clauses ───────────────────

std::string SemanticAnalyzer::resolve_rule_after_ref(const std::string& ref,
                                                     const SourceLocation& loc,
                                                     const std::unordered_set<std::string>& /*local_rule_names*/) {
    // Unified lookup: alias- and canonical-qualified, current-module-qualified,
    // bare local, and std.core prelude spellings all resolve identically.
    if (auto resolved = try_resolve_ref_of_kind(ref, {SymbolKind::Rule})) {
        return make_canonical_id(*resolved);
    }

    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);
        if (qualifier != current_module_name_ && find_imported_module(qualifier) == nullptr) {
            errors_.error(loc, "unknown module qualifier '" + qualifier + "' in 'after:' clause");
            return "";
        }
        errors_.error(loc, "unknown rule '" + local_name + "' in module '" + qualifier + "' in 'after:' clause");
        return "";
    }

    // Bare name provided only by ordinary (non-prelude) imports: point at the
    // qualified spelling instead of a generic unknown-rule error.
    if (!imports_.empty()) {
        auto pit = imports_.rule_providers.find(ref);
        if (pit != imports_.rule_providers.end() && !pit->second.empty() &&
            !find_std_core_provider(imports_, pit->second).has_value()) {
            errors_.error(loc, imported_reference_diagnostic(imports_, "rule", ref, pit->second));
            return "";
        }
    }

    return "";  // not found; caller reports error as needed
}

// ── std.text.format recognition ────────────────────────────────────────────────

static std::optional<std::string> extract_dotted_path(const ExprNode& expr);

bool SemanticAnalyzer::is_std_text_format_callee(const ExprNode& callee) const {
    // Explicit namespace binding: qualifier.format(...), where qualifier is
    // either the full imported module path (`std.text`) or an authored alias.
    if (const auto* member = std::get_if<MemberExpr>(&callee.expr)) {
        if (member->member != "format") {
            return false;
        }
        auto qualifier = extract_dotted_path(*member->object);
        if (!qualifier.has_value()) {
            return false;
        }
        if (*qualifier == "std.text") {
            return true;
        }
        auto import_it = imports_.modules.find(*qualifier);
        if (import_it != imports_.modules.end() && import_it->second.module_name == "std.text") {
            return true;
        }
        // Fall back to scanning UseNode declarations in the AST. This handles
        // unit-test scenarios where the caller omits an explicit imports table
        // but the source still has `use std.text as <qualifier>`.
        if (result_.ast != nullptr) {
            for (const auto& decl : result_.ast->declarations) {
                const auto* use_node = std::get_if<UseNode>(&decl);
                if (use_node == nullptr || use_node->module_name != "std.text") {
                    continue;
                }
                if (use_node->alias.has_value() && *use_node->alias == *qualifier) {
                    return true;
                }
                if (!use_node->alias.has_value() && *qualifier == "std.text") {
                    return true;
                }
            }
        }
    }
    return false;
}

// ── Query expression helpers ──────────────────────────────────────────────────

static std::optional<std::string> extract_dotted_path(const ExprNode& expr) {
    if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
        return ident->name;
    }
    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        auto prefix = extract_dotted_path(*member->object);
        if (prefix) {
            return *prefix + "." + member->member;
        }
    }
    return std::nullopt;
}

static bool is_known_query_module(const std::string& name) {
    return name == "std.query" || name == "std.physics.flat.query" || name == "std.physics.volume.query" ||
           name == "std.ui";
}

// Whether a known query module actually provides the named function; the
// codegen only lowers these pairings (everything else becomes broken C++).
static bool query_module_provides(const std::string& module_name, const std::string& func_name) {
    if (module_name == "std.query") {
        return func_name == "exists" || func_name == "count" || func_name == "first" || func_name == "all" ||
               func_name == "parent" || func_name == "children" || func_name == "hierarchy_preorder" ||
               func_name == "hierarchy_postorder";
    }
    if (module_name == "std.physics.flat.query") {
        return func_name == "nearest" || func_name == "overlap_box" || func_name == "overlap_circle" ||
               func_name == "raycast";
    }
    if (module_name == "std.physics.volume.query") {
        return func_name == "nearest" || func_name == "overlap_box" || func_name == "overlap_sphere" ||
               func_name == "raycast";
    }
    if (module_name == "std.ui") {
        return func_name == "stacking_order";
    }
    return false;
}

std::optional<std::string> SemanticAnalyzer::get_query_func_name(const ExprNode& callee) const {
    if (auto resolved = get_query_module_and_func(callee)) {
        return resolved->second;
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> SemanticAnalyzer::get_query_module_and_func(
    const ExprNode& callee) const {
    if (result_.ast == nullptr) {
        return std::nullopt;
    }
    const auto* member = std::get_if<MemberExpr>(&callee.expr);
    if (member == nullptr) {
        return std::nullopt;
    }
    const auto& func_name = member->member;
    auto qualifier        = extract_dotted_path(*member->object);
    if (!qualifier.has_value()) {
        return std::nullopt;
    }
    if (is_known_query_module(*qualifier)) {
        return std::make_pair(*qualifier, func_name);
    }
    for (const auto& decl : result_.ast->declarations) {
        const auto* use_node = std::get_if<UseNode>(&decl);
        if (use_node == nullptr || !is_known_query_module(use_node->module_name)) {
            continue;
        }
        if (use_node->alias.has_value() && *use_node->alias == *qualifier) {
            return std::make_pair(use_node->module_name, func_name);
        }
    }
    return std::nullopt;
}

void SemanticAnalyzer::validate_query_named_args(
    const QueryCallExpr& qcall,
    const std::string& func_name,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event) const {
    auto has_arg = [&](const std::string& name) {
        return std::ranges::any_of(qcall.named_args, [&](const auto& a) { return a.name == name; });
    };
    if (func_name == "nearest" && !has_arg("from")) {
        errors_.error(qcall.location, "`nearest` requires a `from` named argument");
    }
    if (func_name == "overlap_box" && (!has_arg("center") || !has_arg("size"))) {
        errors_.error(qcall.location, "`overlap_box` requires `center` and `size` named arguments");
    }
    if (func_name == "overlap_circle" && (!has_arg("center") || !has_arg("radius"))) {
        errors_.error(qcall.location, "`overlap_circle` requires `center` and `radius` named arguments");
    }
    if (func_name == "overlap_sphere" && (!has_arg("center") || !has_arg("radius"))) {
        errors_.error(qcall.location, "`overlap_sphere` requires `center` and `radius` named arguments");
    }
    if (func_name == "raycast" && (!has_arg("origin") || !has_arg("dir") || !has_arg("max_dist"))) {
        errors_.error(qcall.location, "`raycast` requires `origin`, `dir`, and `max_dist` named arguments");
    }
    if (func_name == "parent" || func_name == "children" || func_name == "stacking_order") {
        if (!has_arg("of")) {
            errors_.error(qcall.location, "`" + func_name + "` requires an `of` named argument");
        } else {
            auto of_it = std::ranges::find_if(qcall.named_args, [](const auto& a) { return a.name == "of"; });
            if (of_it != qcall.named_args.end()) {
                auto of_type = infer_expr_type(*of_it->value, filter_bindings, local_bindings, handler_event);
                if (of_type.kind != TypeKind::EntityId && of_type.kind != TypeKind::Unknown) {
                    errors_.error(of_it->location, "`" + func_name + "` `of` argument must be of type `entity_id`");
                }
            }
        }
    }
}

// ── std.text.format validation ────────────────────────────────────────────────

void SemanticAnalyzer::validate_one_text_format_call(
    const CallExpr& call,
    const SourceLocation& loc,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event) {
    if (call.args.empty()) {
        errors_.error(loc, "std.text.format requires a format string as the first argument");
        return;
    }

    // First arg must be a string literal
    const auto* fmt_lit = std::get_if<LiteralExpr>(&call.args[0]->expr);
    if (fmt_lit == nullptr || fmt_lit->kind != LiteralExpr::Kind::String) {
        errors_.error(loc, "the first argument to std.text.format must be a string literal");
        return;
    }

    // Parse and validate the format string
    FormatStringAnalysis analysis;
    std::string parse_error;
    if (!analyze_format_string(fmt_lit->value, analysis, parse_error)) {
        errors_.error(loc, parse_error);
        return;
    }

    const auto value_arg_count = static_cast<int>(call.args.size()) - 1;

    if (analysis.has_automatic) {
        if (value_arg_count != analysis.automatic_count) {
            errors_.error(loc,
                          "format string has " + std::to_string(analysis.automatic_count) + " placeholder(s) but " +
                              std::to_string(value_arg_count) + " argument(s) were provided");
            return;
        }
    } else if (analysis.has_manual) {
        if (analysis.manual_max_index >= value_arg_count) {
            errors_.error(loc,
                          "format string placeholder index " + std::to_string(analysis.manual_max_index) +
                              " is out of range; only " + std::to_string(value_arg_count) +
                              " format argument(s) were provided");
            return;
        }
    } else {
        // No placeholders: no value args allowed
        if (value_arg_count > 0) {
            errors_.error(loc,
                          "format string has no placeholders but " + std::to_string(value_arg_count) +
                              " argument(s) were provided");
            return;
        }
    }

    // Validate value arg types
    for (size_t i = 1; i < call.args.size(); ++i) {
        auto arg_type = infer_expr_type(*call.args[i], filter_bindings, local_bindings, handler_event);
        if (arg_type.kind != TypeKind::Unknown && !is_format_supported_type(arg_type.kind)) {
            errors_.error(call.args[i]->location,
                          "type '" + arg_type.name + "' is not supported by std.text.format in v1");
        }
    }
}

void SemanticAnalyzer::validate_text_format_in_expr(
    const ExprNode& expr,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event) {
    if (const auto* call = std::get_if<CallExpr>(&expr.expr)) {
        if (is_std_text_format_callee(*call->callee)) {
            validate_one_text_format_call(*call, expr.location, filter_bindings, local_bindings, handler_event);
        } else {
            validate_text_format_in_expr(*call->callee, filter_bindings, local_bindings, handler_event);
            for (const auto& arg : call->args) {
                validate_text_format_in_expr(*arg, filter_bindings, local_bindings, handler_event);
            }
        }
    } else if (const auto* binary = std::get_if<BinaryExpr>(&expr.expr)) {
        validate_text_format_in_expr(*binary->left, filter_bindings, local_bindings, handler_event);
        validate_text_format_in_expr(*binary->right, filter_bindings, local_bindings, handler_event);
    } else if (const auto* unary = std::get_if<UnaryExpr>(&expr.expr)) {
        validate_text_format_in_expr(*unary->operand, filter_bindings, local_bindings, handler_event);
    } else if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        validate_text_format_in_expr(*member->object, filter_bindings, local_bindings, handler_event);
    } else if (const auto* if_expr = std::get_if<IfExpr>(&expr.expr)) {
        validate_text_format_in_expr(*if_expr->condition, filter_bindings, local_bindings, handler_event);
        validate_text_format_in_expr(*if_expr->then_expr, filter_bindings, local_bindings, handler_event);
        validate_text_format_in_expr(*if_expr->else_expr, filter_bindings, local_bindings, handler_event);
    } else if (const auto* list = std::get_if<ListExpr>(&expr.expr)) {
        for (const auto& elem : list->elements) {
            validate_text_format_in_expr(*elem, filter_bindings, local_bindings, handler_event);
        }
    } else if (const auto* qcall = std::get_if<QueryCallExpr>(&expr.expr)) {
        for (const auto& arg : qcall->named_args) {
            validate_text_format_in_expr(*arg.value, filter_bindings, local_bindings, handler_event);
        }
    }
}

void SemanticAnalyzer::validate_text_format_in_stmts(
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event) {
    auto locals = local_bindings;
    for (const auto& stmt : stmts) {
        std::visit(
            [&](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, LetStmt>) {
                    validate_text_format_in_expr(*s.value, filter_bindings, locals, handler_event);
                    locals[s.name] = infer_expr_type(*s.value, filter_bindings, locals, handler_event);
                } else if constexpr (std::is_same_v<S, VarAssign>) {
                    validate_text_format_in_expr(*s.value, filter_bindings, locals, handler_event);
                } else if constexpr (std::is_same_v<S, ExprStmt>) {
                    validate_text_format_in_expr(*s.expr, filter_bindings, locals, handler_event);
                } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                    if (s.value) {
                        validate_text_format_in_expr(**s.value, filter_bindings, locals, handler_event);
                    }
                } else if constexpr (std::is_same_v<S, EmitStmt>) {
                    if (s.target) {
                        validate_text_format_in_expr(**s.target, filter_bindings, locals, handler_event);
                    }
                    for (const auto& field : s.payload) {
                        validate_text_format_in_expr(*field.value, filter_bindings, locals, handler_event);
                    }
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    validate_text_format_in_expr(*s.condition, filter_bindings, locals, handler_event);
                    validate_text_format_in_stmts(s.then_body, filter_bindings, locals, handler_event);
                    for (const auto& branch : s.else_if_branches) {
                        validate_text_format_in_expr(*branch.condition, filter_bindings, locals, handler_event);
                        validate_text_format_in_stmts(branch.body, filter_bindings, locals, handler_event);
                    }
                    validate_text_format_in_stmts(s.else_body, filter_bindings, locals, handler_event);
                } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                    validate_text_format_in_expr(*s.iterable, filter_bindings, locals, handler_event);
                    validate_text_format_in_stmts(s.body, filter_bindings, locals, handler_event);
                }
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_text_format_calls(  // NOLINT(readability-function-cognitive-complexity) -- 32 after
                                                    // build_filter_bindings extraction; not further in scope here
    ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, FuncNode>) {
                    validate_text_format_in_stmts(node.body, {}, {}, nullptr);
                } else if constexpr (std::is_same_v<T, RuleNode>) {
                    for (auto& handler : node.handlers) {
                        auto filter_bindings = build_filter_bindings(node.filter);

                        std::optional<ResolvedStruct> phase_activation;
                        // See the identical fix/comment where this pattern first
                        // appears (rule handler validation, above): handler.event_name
                        // is the raw, possibly-dotted source spelling and won't match
                        // event_structs_' simple-name keys for a cross-module trigger.
                        const ResolvedStruct* handler_event =
                            (handler.resolved_trigger.has_value() &&
                             handler.resolved_trigger->kind == HandlerTriggerKind::Event)
                                ? find_resolved_event(handler.resolved_trigger->symbol.local_name)
                                : nullptr;
                        if (handler.resolved_trigger.has_value() &&
                            handler.resolved_trigger->kind == HandlerTriggerKind::Phase) {
                            const auto& phase_symbol = handler.resolved_trigger->symbol;
                            if (const auto* fields = find_phase_fields(phase_symbol); fields != nullptr) {
                                phase_activation.emplace();
                                phase_activation->name   = phase_symbol.local_name;
                                phase_activation->fields = *fields;
                                assign_canonical_identity(*phase_activation, phase_symbol);
                                handler_event = &*phase_activation;
                            }
                        }
                        std::unordered_map<std::string, TypeInfo> local_bindings;
                        if (handler_event != nullptr) {
                            const auto symbol = resolved_decl_symbol(
                                *handler_event, SymbolKind::Event, current_module_name_, handler_event->name);
                            local_bindings[handler.event_name] =
                                make_resolved_user_type(TypeKind::Struct, symbol, handler_event->name);
                            if (handler.alias.has_value()) {
                                local_bindings[*handler.alias] =
                                    make_resolved_user_type(TypeKind::Struct, symbol, handler_event->name);
                            }
                        }

                        validate_text_format_in_stmts(handler.body, filter_bindings, local_bindings, handler_event);
                    }
                }
            },
            decl);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TypeInfo SemanticAnalyzer::infer_ident_expr_type(
    const IdentExpr& ident,
    const SourceLocation& location,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const PairScope* pair_scope) const {
    if (auto local_it = local_bindings.find(ident.name); local_it != local_bindings.end()) {
        return local_it->second;
    }
    if (pair_scope != nullptr && pair_scope->contains(ident.name)) {
        return make_entity_id_type();
    }
    const ResolvedField* matching_filter_field = nullptr;
    std::unordered_set<const ResolvedTrait*> visited_traits;
    for (const auto& [_, trait] : filter_bindings) {
        if (trait == nullptr || visited_traits.contains(trait)) {
            continue;
        }
        visited_traits.insert(trait);
        for (const auto& field : trait->fields) {
            if (field.name != ident.name) {
                continue;
            }
            if (matching_filter_field != nullptr) {
                return make_unknown_type();
            }
            matching_filter_field = &field;
        }
    }
    if (matching_filter_field != nullptr) {
        return matching_filter_field->type;
    }
    if (auto asset_it = asset_decl_types_.find(ident.name); asset_it != asset_decl_types_.end()) {
        switch (asset_it->second) {
            case TypeKind::MeshId:
                return make_mesh_id_type();
            case TypeKind::ModelId:
                return make_model_id_type();
            case TypeKind::TextureId:
                return make_texture_id_type();
            case TypeKind::SoundId:
                return make_sound_id_type();
            case TypeKind::MusicId:
                return make_music_id_type();
            case TypeKind::FontId:
                return make_font_id_type();
            case TypeKind::MaterialId:
                return make_material_id_type();
            default:
                break;
        }
    }
    if (auto input_it = input_decl_types_.find(ident.name); input_it != input_decl_types_.end()) {
        return input_it->second == TypeKind::InputAxis ? make_input_axis_type() : make_input_button_type();
    }
    if (entity_names_.contains(ident.name)) {
        errors_.error(location, "entity '" + ident.name + "' is not an entity_id expression");
        return make_unknown_type();
    }
    if (ident.name == "break" || ident.name == "continue") {
        errors_.error(location, "`break`/`continue` are not supported");
    }
    return make_unknown_type();
}

// 137 after extraction from infer_expr_type (task 6.12); resolves a member
// chain across pair-binding, filter-trait, handler-event, and local-struct/
// event/trait/phase owners plus qualified-alias lookup — an inherently large
// exhaustive dispatch, not further decomposable without threading more state.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TypeInfo SemanticAnalyzer::descend_vector_color_members(TypeInfo start,
                                                        const std::vector<std::string>& segments,
                                                        std::size_t from_index) {
    TypeInfo current = std::move(start);
    for (std::size_t i = from_index; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        if (current.kind == TypeKind::Vec2 || current.kind == TypeKind::Vec3) {
            if (seg == "x" || seg == "y" || (current.kind == TypeKind::Vec3 && seg == "z")) {
                current = make_float_type();
                continue;
            }
            return make_unknown_type();
        }
        if (current.kind == TypeKind::Color) {
            if (seg == "r" || seg == "g" || seg == "b" || seg == "a") {
                current = make_float_type();
                continue;
            }
            return make_unknown_type();
        }
        return make_unknown_type();
    }
    return current;
}

TypeInfo SemanticAnalyzer::infer_member_expr_type(
    const MemberExpr& member,
    const SourceLocation& location,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event,
    const PairScope* pair_scope) const {
    // Resolved enum member access (`inp.Key.A`) types as its enum (D3/2.2).
    if (member.resolved_enum_member.has_value()) {
        return make_resolved_user_type(TypeKind::Enum, member.resolved_enum_member->enum_id);
    }
    if (pair_scope != nullptr) {
        // Flatten a (possibly nested) member-access chain rooted at a pair
        // binding into ordered dotted segments, e.g.
        // `body.tf.WorldTransform.position` -> root "body",
        // segments ["tf", "WorldTransform", "position"].
        if (auto chain = member_chain_segments(member); chain.has_value()) {
            const std::string& root_name = chain->front();
            std::vector<std::string> segments(chain->begin() + 1, chain->end());
            if (auto scope_it = pair_scope->find(root_name); scope_it != pair_scope->end()) {
                auto resolved = resolve_pair_member_chain(root_name, segments, *pair_scope);
                if (!resolved.has_value()) {
                    errors_.error(location,
                                  "'" + segments.front() + "' is unavailable on pair binding '" + root_name + "'");
                    return make_unknown_type();
                }
                const auto* trait = find_resolved_trait(make_canonical_id(resolved->trait_id));
                if (resolved->consumed_segments >= segments.size()) {
                    return make_unknown_type();
                }
                const auto first =
                    trait == nullptr ? make_unknown_type()
                                     : find_field_type_in(trait->fields, segments[resolved->consumed_segments]);
                return descend_vector_color_members(first, segments, resolved->consumed_segments + 1);
            }
        }
    }
    // Flatten the chain the same way for a plain filter-alias/local root, so a
    // nested access like `p.pos.y` resolves through vec2/vec3/color component
    // descent identically to the pair-binding case above (dsl-rule-order-by
    // needs this to type-check computed sort keys; where: predicates gain the
    // same precision as an incidental side effect of sharing the walk).
    if (auto chain = member_chain_segments(member); chain.has_value() && chain->size() >= 2) {
        if (auto trait_it = filter_bindings.find(chain->front());
            trait_it != filter_bindings.end() && trait_it->second != nullptr) {
            const auto first = find_field_type_in(trait_it->second->fields, (*chain)[1]);
            return descend_vector_color_members(first, *chain, 2);
        }
    }
    const auto* owner = std::get_if<IdentExpr>(&member.object->expr);
    if (owner == nullptr) {
        return make_unknown_type();
    }
    if (handler_event != nullptr && owner->name == handler_event->name) {
        const auto* field = find_field_in(handler_event->fields, member.member);
        if (field != nullptr && field->is_completion_only && handler_event->symbol_id.has_value() &&
            handler_event->symbol_id->kind == SymbolKind::Phase) {
            errors_.error(location,
                          "phase completion field '" + make_canonical_id(*handler_event->symbol_id) + "." +
                              member.member + "' is available only to downstream phases");
        }
        return field == nullptr ? make_unknown_type() : field->type;
    }
    if (auto local_it = local_bindings.find(owner->name);
        local_it != local_bindings.end() && local_it->second.kind == TypeKind::Struct) {
        const auto& local_type = local_it->second;
        if (local_type.symbol_id.has_value()) {
            const auto& symbol = *local_type.symbol_id;
            if (symbol.kind == SymbolKind::Struct) {
                if (symbol.module.name == current_module_name_) {
                    if (auto struct_it = result_.structs.find(symbol.local_name); struct_it != result_.structs.end()) {
                        return find_field_type_in(struct_it->second.fields, member.member);
                    }
                }
                for (const auto& [_, syms] : imports_.modules) {
                    if (syms.module_name != symbol.module.name) {
                        continue;
                    }
                    if (auto struct_it = syms.structs.find(symbol.local_name); struct_it != syms.structs.end()) {
                        return find_field_type_in(struct_it->second.fields, member.member);
                    }
                }
            } else if (symbol.kind == SymbolKind::Event) {
                if (auto event_it = event_structs_.find(symbol.local_name); event_it != event_structs_.end()) {
                    return find_field_type_in(event_it->second.fields, member.member);
                }
            } else if (symbol.kind == SymbolKind::Trait) {
                if (const auto* trait = find_resolved_trait(make_canonical_id(symbol)); trait != nullptr) {
                    return find_field_type_in(trait->fields, member.member);
                }
            } else if (symbol.kind == SymbolKind::Phase) {
                const auto* fields = find_phase_fields(symbol);
                const auto* field  = fields == nullptr ? nullptr : find_field_in(*fields, member.member);
                if (field != nullptr && field->is_completion_only) {
                    errors_.error(location,
                                  "phase completion field '" + make_canonical_id(symbol) + "." + member.member +
                                      "' is available only to downstream phases");
                }
                if (field != nullptr) {
                    return field->type;
                }
                // A render-pass stage handler's built-in-field alias (e.g.
                // `v` in `on my_pass.vertex as v:`) is keyed under the
                // phase's own derived trigger symbol ("my_pass__vertex"),
                // which find_phase_fields (declared phase descriptor fields
                // only) never sees — its real field table
                // (build_render_stage_activation_struct's quads_*_fields())
                // only exists on the call stack as `handler_event`.
                if (handler_event != nullptr && local_type.name == handler_event->name) {
                    return find_field_type_in(handler_event->fields, member.member);
                }
                return make_unknown_type();
            }
        }

        const auto& type_name = local_type.name;
        const auto dot        = type_name.rfind('.');
        if (dot != std::string::npos) {
            // Qualified alias name ("alias.StructName") or canonical name from resolved TypeInfo.
            const auto qualifier  = type_name.substr(0, dot);
            const auto local_name = type_name.substr(dot + 1);
            if (qualifier == current_module_name_) {
                if (auto struct_it = result_.structs.find(local_name); struct_it != result_.structs.end()) {
                    return find_field_type_in(struct_it->second.fields, member.member);
                }
            }
            if (auto mod_it = imports_.modules.find(qualifier); mod_it != imports_.modules.end()) {
                if (auto s_it = mod_it->second.structs.find(local_name); s_it != mod_it->second.structs.end()) {
                    return find_field_type_in(s_it->second.fields, member.member);
                }
            }
            for (const auto& [_, syms] : imports_.modules) {
                if (syms.module_name != qualifier) {
                    continue;
                }
                if (auto s_it = syms.structs.find(local_name); s_it != syms.structs.end()) {
                    return find_field_type_in(s_it->second.fields, member.member);
                }
            }
        }
        if (auto struct_it = result_.structs.find(type_name); struct_it != result_.structs.end()) {
            return find_field_type_in(struct_it->second.fields, member.member);
        }
        if (auto event_it = event_structs_.find(type_name); event_it != event_structs_.end()) {
            return find_field_type_in(event_it->second.fields, member.member);
        }
    }
    return make_unknown_type();
}

std::optional<TypeInfo> SemanticAnalyzer::infer_vector_constructor_call_type(
    const CallExpr& call,
    const SourceLocation& location,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event,
    const PairScope* pair_scope) const {
    const auto* ident = std::get_if<IdentExpr>(&call.callee->expr);
    if (ident == nullptr || (ident->name != "vec2" && ident->name != "vec3" && ident->name != "color")) {
        return std::nullopt;
    }

    auto check_numeric_args = [&] {
        for (const auto& arg : call.args) {
            auto arg_type = infer_expr_type(*arg, filter_bindings, local_bindings, handler_event, pair_scope);
            if (arg_type.kind != TypeKind::Float && arg_type.kind != TypeKind::Int &&
                arg_type.kind != TypeKind::Unknown) {
                errors_.error(location,
                              "'" + ident->name + "' argument must be of type 'float', got '" + arg_type.name + "'");
            }
        }
    };

    // color(r, g, b, a): exactly 4 channel arguments, no scalar-splat form
    // (dsl-vector-expressions "Color constructor") - unlike vec2/vec3, a
    // single-argument color(s) would be ambiguous between "splat" and "some
    // other 1-arg color constructor", so it's simply not offered.
    if (ident->name == "color") {
        if (call.args.size() != 4) {
            errors_.error(location, "'color' expects 4 arguments, got " + std::to_string(call.args.size()));
            return make_color_type();
        }
        check_numeric_args();
        return make_color_type();
    }

    const bool is_vec2                = ident->name == "vec2";
    const std::size_t component_count = is_vec2 ? 2 : 3;
    TypeInfo result_type              = is_vec2 ? make_vec2_type() : make_vec3_type();

    if (call.args.size() != 1 && call.args.size() != component_count) {
        errors_.error(location, "'" + ident->name + "' expects 1 (splat) or " + std::to_string(component_count) +
                                     " arguments, got " + std::to_string(call.args.size()));
        return result_type;
    }

    check_numeric_args();
    return result_type;
}

bool SemanticAnalyzer::validate_range_iterable(
    const ExprNode& iterable,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event,
    const PairScope* pair_scope) const {
    const auto* call = std::get_if<CallExpr>(&iterable.expr);
    if (call == nullptr) {
        return false;
    }
    const auto* ident = std::get_if<IdentExpr>(&call->callee->expr);
    if (ident == nullptr || ident->name != "range") {
        return false;
    }

    if (call->args.size() != 2 && call->args.size() != 3) {
        errors_.error(iterable.location,
                      "'range' expects 2 or 3 arguments, got " + std::to_string(call->args.size()));
        return true;
    }

    for (const auto& arg : call->args) {
        auto arg_type = infer_expr_type(*arg, filter_bindings, local_bindings, handler_event, pair_scope);
        if (arg_type.kind != TypeKind::Int && arg_type.kind != TypeKind::Unknown) {
            errors_.error(iterable.location,
                          "'range' argument must be of type 'int', got '" + arg_type.name + "'");
        }
    }
    return true;
}

// 84 after extracting infer_ident_expr_type/infer_member_expr_type (task
// 6.12); still the exhaustive ExprNode-variant type-inference dispatch, with
// several arms recursing into itself (Unary/Binary/If/List).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TypeInfo SemanticAnalyzer::infer_expr_type(const ExprNode& expr,
                                           const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                           const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                           const ResolvedStruct* handler_event,
                                           const PairScope* pair_scope) const {
    if (const auto* literal = std::get_if<LiteralExpr>(&expr.expr)) {
        switch (literal->kind) {
            case LiteralExpr::Kind::Int:
                return make_int_type();
            case LiteralExpr::Kind::Float:
                return make_float_type();
            case LiteralExpr::Kind::String:
                return make_string_type();
            case LiteralExpr::Kind::HexColor:
                return make_color_type();
            case LiteralExpr::Kind::Bool:
                return make_bool_type();
        }
    }

    if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
        return infer_ident_expr_type(*ident, expr.location, filter_bindings, local_bindings, pair_scope);
    }

    if (std::holds_alternative<SelfExpr>(expr.expr)) {
        if (handler_event == nullptr && !local_bindings.contains("__self_context")) {
            errors_.error(expr.location, "`self` only allowed inside rule event handlers");
            return make_unknown_type();
        }
        if (pair_scope != nullptr) {
            errors_.error(expr.location,
                          "`self` is unavailable in a pair handler; a pair tuple has no unique current entity, use "
                          "an explicit binding instead");
            return make_unknown_type();
        }
        return make_entity_id_type();
    }

    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        return infer_member_expr_type(
            *member, expr.location, filter_bindings, local_bindings, handler_event, pair_scope);
    }

    if (std::holds_alternative<SpawnExpr>(expr.expr)) {
        return make_entity_id_type();
    }
    if (const auto* qcall = std::get_if<QueryCallExpr>(&expr.expr)) {
        if (handler_event == nullptr) {
            errors_.error(expr.location,
                          "query expressions require world access; only allowed inside rule event handlers");
        }
        for (const auto& pred : qcall->filters) {
            if (!is_trait_declared(pred.trait_name)) {
                errors_.error(pred.location, "undeclared trait '" + pred.trait_name + "' in query filter");
            }
        }
        auto resolved               = get_query_module_and_func(*qcall->callee);
        const std::string func_name = resolved.has_value() ? resolved->second : [&]() -> std::string {
            if (const auto* m = std::get_if<MemberExpr>(&qcall->callee->expr)) {
                return m->member;
            }
            return "";
        }();
        // Only pairings the codegen can lower are valid — e.g. `overlap_sphere`
        // exists in the volume namespace but not the flat one.
        if (resolved.has_value() && !query_module_provides(resolved->first, resolved->second)) {
            errors_.error(qcall->location,
                          "'" + resolved->second + "' is not a query function of module '" + resolved->first + "'");
        }
        validate_query_named_args(*qcall, func_name, filter_bindings, local_bindings, handler_event);
        if (func_name == "exists") {
            return make_bool_type();
        }
        if (func_name == "count" || func_name == "stacking_order") {
            return make_int_type();
        }
        if (func_name == "first" || func_name == "nearest" || func_name == "parent" || func_name == "raycast") {
            return make_entity_id_type();
        }
        if (func_name == "all" || func_name == "children" || func_name == "hierarchy_preorder" ||
            func_name == "hierarchy_postorder" || func_name == "overlap_box" || func_name == "overlap_circle" ||
            func_name == "overlap_sphere") {
            return make_list_type(make_entity_id_type());
        }
        return make_unknown_type();
    }
    if (const auto* call = std::get_if<CallExpr>(&expr.expr)) {
        if (const auto* ident = std::get_if<IdentExpr>(&call->callee->expr); ident != nullptr && ident->name == "range") {
            errors_.error(expr.location, "`range()` is only valid as the iterable of a `for` statement");
            return make_unknown_type();
        }
        if (auto vector_type = infer_vector_constructor_call_type(*call, expr.location, filter_bindings,
                                                                   local_bindings, handler_event, pair_scope)) {
            return *vector_type;
        }
        if (is_std_text_format_callee(*call->callee)) {
            return make_string_type();
        }
        if (auto* ident = std::get_if<IdentExpr>(&call->callee->expr); ident != nullptr && ident->name == "exists") {
            if (handler_event == nullptr) {
                errors_.error(expr.location,
                              "`exists()` requires world access; only allowed inside rule event handlers");
            }
            if (call->args.size() != 1) {
                return make_bool_type();
            }
            auto arg_type =
                infer_expr_type(*call->args.front(), filter_bindings, local_bindings, handler_event, pair_scope);
            if (arg_type.kind != TypeKind::EntityId && arg_type.kind != TypeKind::Unknown) {
                errors_.error(expr.location, "`exists()` argument must be of type `entity_id`");
            }
            return make_bool_type();
        }
        if (auto* ident = std::get_if<IdentExpr>(&call->callee->expr);
            ident != nullptr && !func_names_.contains(ident->name) && !imports_.empty()) {
            auto provider_it = imports_.func_providers.find(ident->name);
            if (provider_it != imports_.func_providers.end() && !provider_it->second.empty() &&
                !find_std_core_provider(imports_, provider_it->second).has_value()) {
                errors_.error(expr.location,
                              imported_reference_diagnostic(imports_, "function", ident->name, provider_it->second));
            }
        }
        if (call->resolved_callee_id.has_value()) {
            if (const auto* function = find_resolved_func(*call->resolved_callee_id);
                function != nullptr && function->return_type.has_value()) {
                return *function->return_type;
            }
        }
        return make_unknown_type();
    }
    if (const auto* unary = std::get_if<UnaryExpr>(&expr.expr)) {
        return infer_expr_type(*unary->operand, filter_bindings, local_bindings, handler_event, pair_scope);
    }
    if (const auto* binary = std::get_if<BinaryExpr>(&expr.expr)) {
        auto left  = infer_expr_type(*binary->left, filter_bindings, local_bindings, handler_event, pair_scope);
        auto right = infer_expr_type(*binary->right, filter_bindings, local_bindings, handler_event, pair_scope);
        if ((binary->op == "==" || binary->op == "!=") &&
            ((left.kind == TypeKind::EntityId && right.kind == TypeKind::Int) ||
             (right.kind == TypeKind::EntityId && left.kind == TypeKind::Int))) {
            errors_.error(expr.location,
                          "entity_id has no null literal; use `exists(id)` to test handle validity or `add`/`remove` "
                          "to model absent relationships via trait presence");
        }
        if (binary->op == "==" || binary->op == "!=" || binary->op == "<" || binary->op == ">" || binary->op == "<=" ||
            binary->op == ">=" || binary->op == "and" || binary->op == "or") {
            return make_bool_type();
        }
        if ((left.kind == TypeKind::Vec2 || left.kind == TypeKind::Vec3 || left.kind == TypeKind::Color ||
             right.kind == TypeKind::Vec2 || right.kind == TypeKind::Vec3 || right.kind == TypeKind::Color) &&
            left.kind != TypeKind::Unknown && right.kind != TypeKind::Unknown) {
            if (auto result_kind = lookup_vector_binary_op_result(left.kind, binary->op, right.kind)) {
                return vector_result_type_info(*result_kind);
            }
            errors_.error(expr.location, "no operator '" + binary->op + "' for operand types '" + left.name +
                                              "' and '" + right.name + "'");
            return make_unknown_type();
        }
        if (left.kind == TypeKind::Float || right.kind == TypeKind::Float) {
            return make_float_type();
        }
        if (left.kind == TypeKind::Int && right.kind == TypeKind::Int) {
            return make_int_type();
        }
        return left;
    }
    if (const auto* if_expr = std::get_if<IfExpr>(&expr.expr)) {
        return infer_expr_type(*if_expr->then_expr, filter_bindings, local_bindings, handler_event, pair_scope);
    }
    if (const auto* list = std::get_if<ListExpr>(&expr.expr)) {
        if (list->elements.empty()) {
            return make_list_type(make_unknown_type());
        }
        return make_list_type(
            infer_expr_type(*list->elements.front(), filter_bindings, local_bindings, handler_event, pair_scope));
    }
    return make_unknown_type();
}

std::unordered_set<std::string> SemanticAnalyzer::get_archetype_fields(
    const std::vector<ArchetypeTraitEntry>& traits) const {
    std::unordered_set<std::string> fields;
    for (const auto& entry : traits) {
        if (const auto* trait = find_resolved_trait(entry.trait_name)) {
            for (const auto& f : trait->fields) {
                fields.insert(f.name);
            }
        }
    }
    return fields;
}

// ── Shared trait-override-assignment validation (6-site family) ────────────

void SemanticAnalyzer::validate_trait_override_assignments(const std::vector<const ArchetypeTraitEntry*>& entries,
                                                           const std::string& context_desc,
                                                           bool check_self,
                                                           bool report_unknown_trait,
                                                           std::unordered_set<std::string>* provided) {
    for (const auto* entry : entries) {
        const auto* trait = find_resolved_trait(entry->trait_name);
        if (trait == nullptr) {
            if (report_unknown_trait) {
                errors_.error(entry->location, "undeclared trait '" + entry->trait_name + "' in " + context_desc);
            }
            continue;
        }

        std::unordered_set<std::string> trait_fields;
        for (const auto& field : trait->fields) {
            trait_fields.insert(field.name);
        }

        for (const auto& assign : entry->assignments) {
            if (provided != nullptr) {
                provided->insert(assign.name);
            }
            if (!trait_fields.contains(assign.name)) {
                errors_.error(
                    assign.location,
                    "unknown field '" + assign.name + "' for trait '" + entry->trait_name + "' in " + context_desc);
            }
            if (check_self && expr_contains_self(*assign.value)) {
                errors_.error(assign.location, "`self` only allowed inside rule event handlers");
            }
        }
    }
}

// ── Task 5.1, 5.2: Validate template and unit declarations ──────────────────

void SemanticAnalyzer::validate_template_unit_declarations(  // NOLINT(readability-function-cognitive-complexity)
    ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {  // NOLINT(readability-function-cognitive-complexity)
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, TemplateNode> || std::is_same_v<T, EntityNode>) {
                    const std::string KIND = std::is_same_v<T, TemplateNode> ? "template" : "entity";

                    // Hierarchical children require the Parent trait for generated
                    // parent relations (dsl-hierarchical-entity-templates D6).
                    if (!node.children.empty() && !is_trait_declared("Parent")) {
                        errors_.error(node.location,
                                      "hierarchical `children:` requires the `Parent` trait to be available; "
                                      "add `use std.core` (or another module providing `Parent`)");
                    }

                    validate_child_archetypes(node.children, KIND, node.name);

                    // For template-backed entities, validate the template reference; body entries are
                    // overrides (not inline traits), so skip the standard trait/field checks below.
                    if constexpr (std::is_same_v<T, EntityNode>) {
                        if (node.template_ref.has_value()) {
                            validate_archetype_template_ref(
                                *node.template_ref, node.location, "entity '" + node.name + "'");
                            return;  // skip inline-entity trait validation for template-backed entities
                        }
                    }

                    // 5.1 / 3.1: Validate all traits in nested trait blocks are declared.
                    // Resolve via canonical helper to catch ambiguous imports and unknown qualifiers.
                    // AST trait_name is preserved for backward-compatible codegen (task 5.x).
                    for (auto& entry : node.traits) {
                        const auto prev_errors = errors_.error_count();
                        auto canonical         = resolve_trait_ref_to_canonical(entry.trait_name, entry.location);
                        if (canonical.empty() && errors_.error_count() == prev_errors) {
                            errors_.error(
                                entry.location,
                                "undeclared trait '" + entry.trait_name + "' in " + KIND + " '" + node.name + "'");
                        }
                    }

                    for (const auto& template_use : node.template_uses) {
                        resolve_archetype_template_use(template_use, KIND, node.name);
                    }

                    // 5.1: Validate field assignments in trait blocks belong to the trait
                    std::vector<const ArchetypeTraitEntry*> trait_entries;
                    trait_entries.reserve(node.traits.size());
                    for (const auto& entry : node.traits) {
                        trait_entries.push_back(&entry);
                    }
                    validate_trait_override_assignments(trait_entries,
                                                        KIND + " '" + node.name + "'",
                                                        /*check_self=*/true,
                                                        /*report_unknown_trait=*/false);

                    // 5.2: Compute required fields for templates
                    // (fields that are `var` with no default and not initialized in trait block)
                    if constexpr (std::is_same_v<T, TemplateNode>) {
                        std::unordered_set<std::string> provided;
                        for (auto& entry : node.traits) {
                            for (auto& assign : entry.assignments) {
                                provided.insert(assign.name);
                            }
                        }

                        std::unordered_set<std::string> required;
                        for (auto& entry : node.traits) {
                            const auto* trait = find_resolved_trait(entry.trait_name);
                            if (!trait) {
                                continue;
                            }

                            for (auto& field : trait->fields) {
                                if (field.is_var && !field.has_default && !provided.contains(field.name)) {
                                    required.insert(field.name);
                                }
                            }
                        }
                        template_required_fields_[node.name] = std::move(required);
                    }
                }
            },
            decl);
    }
}

// ── Hierarchical entity templates (dsl-hierarchical-entity-templates) ────────

// Validate a `from TemplateName` reference using the same local/imported
// resolution rules as top-level template-backed entities.
void SemanticAnalyzer::validate_archetype_template_ref(const std::string& tmpl_ref,
                                                       const SourceLocation& location,
                                                       const std::string& owner_desc) {
    // Unified lookup accepts alias-, canonical-, and current-module-qualified
    // spellings; the string-splitting below only produces diagnostics.
    if (try_resolve_ref_of_kind(tmpl_ref, {SymbolKind::Template}).has_value()) {
        return;
    }

    const auto dot = tmpl_ref.rfind('.');
    if (dot != std::string::npos) {
        const auto qualifier     = tmpl_ref.substr(0, dot);
        const auto template_name = tmpl_ref.substr(dot + 1);
        const auto* module       = find_imported_module(qualifier);
        if (module == nullptr) {
            errors_.error(location, "unknown module qualifier '" + qualifier + "' in 'from' clause of " + owner_desc);
        } else {
            auto non_pub_it = imports_.non_pub_template_names.find(qualifier);
            if (non_pub_it != imports_.non_pub_template_names.end() && non_pub_it->second.contains(template_name)) {
                errors_.error(location, "template '" + template_name + "' is not public in module '" + qualifier + "'");
            } else if (imported_symbols_contain_non_template(*module, template_name)) {
                errors_.error(location,
                              "'" + template_name + "' is not a template; 'from' clause must reference a template");
            } else {
                errors_.error(location, "undefined template '" + template_name + "' in module '" + qualifier + "'");
            }
        }
        return;
    }

    if (template_names_.contains(tmpl_ref)) {
        return;  // valid local template — override validation happens after flattening
    }
    if (local_non_template_symbol_exists(tmpl_ref)) {
        errors_.error(location, "'" + tmpl_ref + "' is not a template; 'from' clause must reference a template");
        return;
    }
    if (!imports_.empty()) {
        if (imports_.template_providers.contains(tmpl_ref)) {
            const auto& providers = imports_.template_providers.at(tmpl_ref);
            if (find_std_core_provider(imports_, providers).has_value()) {
                return;
            }
            errors_.error(location, imported_reference_diagnostic(imports_, "template", tmpl_ref, providers));
            return;
        }
        for (const auto& [qualifier, names] : imports_.non_pub_template_names) {
            if (names.contains(tmpl_ref)) {
                errors_.error(location, "template '" + tmpl_ref + "' is not public in module '" + qualifier + "'");
                return;
            }
        }
    }
    errors_.error(location, "undefined template '" + tmpl_ref + "' in " + owner_desc);
}

// Validate authored child declarations: sibling role uniqueness, no manual
// Parent trait blocks (D8), and root-archetype trait/field rules for inline
// (non-`from`) children. `from`-child bodies are overrides and are validated
// against the flattened template during flattening.
void SemanticAnalyzer::validate_child_archetypes(  // NOLINT(readability-function-cognitive-complexity) -- still 28
                                                   // after trait-override extraction; not further in scope here
    const std::vector<ChildArchetypeNode>& children,
    const std::string& archetype_kind,
    const std::string& archetype_name) {
    std::unordered_set<std::string> sibling_roles;
    for (const auto& child : children) {
        if (!sibling_roles.insert(child.role).second) {
            errors_.error(child.location,
                          "duplicate child role '" + child.role + "' in " + archetype_kind + " '" + archetype_name +
                              "'; sibling child roles must be unique");
        }

        for (const auto& entry : child.traits) {
            if (entry.trait_name == "Parent") {
                errors_.error(entry.location,
                              "child '" + child.role +
                                  "' declares a manual `Parent` trait; the `children:` nesting itself assigns "
                                  "the parent relation");
            }
        }

        if (child.template_ref.has_value()) {
            validate_archetype_template_ref(
                *child.template_ref,
                child.location,
                "child '" + child.role + "' of " + archetype_kind + " '" + archetype_name + "'");
        } else {
            for (const auto& entry : child.traits) {
                if (entry.trait_name == "Parent") {
                    continue;  // already reported above
                }
                if (!is_trait_declared(entry.trait_name)) {
                    errors_.error(entry.location,
                                  "undeclared trait '" + entry.trait_name + "' in child '" + child.role + "' of " +
                                      archetype_kind + " '" + archetype_name + "'");
                }
            }

            for (const auto& template_use : child.template_uses) {
                resolve_archetype_template_use(template_use, "child", child.role);
            }

            std::vector<const ArchetypeTraitEntry*> trait_entries;
            trait_entries.reserve(child.traits.size());
            for (const auto& entry : child.traits) {
                trait_entries.push_back(&entry);
            }
            validate_trait_override_assignments(
                trait_entries, "child '" + child.role + "'", /*check_self=*/true, /*report_unknown_trait=*/false);
        }

        validate_child_archetypes(child.children, archetype_kind, archetype_name);
    }
}

// Validate a nested child override tree against a flattened child list:
// unknown roles, traits not present on the child, unknown fields, and `self`
// usage outside handlers (declaration sites only).
void SemanticAnalyzer::validate_child_override_tree(const std::vector<ChildOverrideNode>& overrides,
                                                    const std::vector<ChildArchetypeNode>& base_children,
                                                    const std::string& site_desc,
                                                    bool allow_self) {
    for (const auto& override_node : overrides) {
        auto target = std::ranges::find_if(
            base_children, [&override_node](const auto& candidate) { return candidate.role == override_node.role; });
        if (target == base_children.end()) {
            std::string known;
            for (const auto& child : base_children) {
                if (!known.empty()) {
                    known += ", ";
                }
                known += "'" + child.role + "'";
            }
            errors_.error(override_node.location,
                          "unknown child role '" + override_node.role + "' in " + site_desc +
                              (known.empty() ? "; it has no child roles" : "; known child roles: " + known));
            continue;
        }

        std::unordered_set<std::string> child_trait_names;
        for (const auto& entry : target->traits) {
            child_trait_names.insert(entry.trait_name);
        }

        std::vector<const ArchetypeTraitEntry*> valid_entries;
        for (const auto& entry : override_node.traits) {
            if (!child_trait_names.contains(entry.trait_name)) {
                errors_.error(entry.location,
                              "trait '" + entry.trait_name + "' is not part of child '" + override_node.role + "' in " +
                                  site_desc + "; cannot override it");
                continue;
            }
            valid_entries.push_back(&entry);
        }
        validate_trait_override_assignments(valid_entries,
                                            "child override '" + override_node.role + "'",
                                            /*check_self=*/!allow_self,
                                            /*report_unknown_trait=*/false);

        validate_child_override_tree(override_node.children, target->children, site_desc, allow_self);
    }
}

// Check that every required field (var, no default) of each descendant is
// satisfied by the flattened child traits or by creation-site overrides.
void SemanticAnalyzer::validate_child_required_fields(const std::vector<ChildArchetypeNode>& children,
                                                      const std::vector<ChildOverrideNode>& overrides,
                                                      const std::string& site_desc,
                                                      const SourceLocation& site_loc) {
    for (const auto& child : children) {
        const ChildOverrideNode* override_node = nullptr;
        for (const auto& candidate : overrides) {
            if (candidate.role == child.role) {
                override_node = &candidate;
                break;
            }
        }

        std::unordered_set<std::string> provided;
        for (const auto& entry : child.traits) {
            for (const auto& assign : entry.assignments) {
                provided.insert(assign.name);
            }
        }
        if (override_node != nullptr) {
            for (const auto& entry : override_node->traits) {
                for (const auto& assign : entry.assignments) {
                    provided.insert(assign.name);
                }
            }
        }

        for (const auto& entry : child.traits) {
            const auto* trait = find_resolved_trait(entry.trait_name);
            if (trait == nullptr) {
                continue;
            }
            for (const auto& field : trait->fields) {
                if (field.is_var && !field.has_default && !provided.contains(field.name)) {
                    errors_.error(
                        site_loc,
                        "required field '" + field.name + "' not set for child '" + child.role + "' in " + site_desc);
                }
            }
        }

        validate_child_required_fields(child.children,
                                       override_node != nullptr ? override_node->children : kNoChildOverrides,
                                       site_desc,
                                       site_loc);
    }
}

// Load-time entities create their descendants at module load, so every
// descendant's required fields must be satisfied by the flattened tree.
void SemanticAnalyzer::validate_hierarchical_entities(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        auto* entity = std::get_if<EntityNode>(&decl);
        if (entity == nullptr || entity->children.empty()) {
            continue;
        }
        validate_child_required_fields(
            entity->children, kNoChildOverrides, "entity '" + entity->name + "'", entity->location);
    }
}

void SemanticAnalyzer::validate_template_use_cycles(ProgramNode& program) {
    struct TemplateUseEdge {
        std::string target;
        SourceLocation location;
    };

    std::unordered_map<std::string, std::vector<TemplateUseEdge>> graph;
    std::vector<std::string> template_order;

    for (auto& decl : program.declarations) {
        auto* tmpl = std::get_if<TemplateNode>(&decl);
        if (tmpl == nullptr) {
            continue;
        }

        template_order.push_back(tmpl->name);
        auto& edges = graph[tmpl->name];

        auto add_edge = [this, &edges](const std::string& target, const SourceLocation& location) {
            if (target.contains('.')) {
                return;
            }
            if (template_names_.contains(target)) {
                edges.push_back({.target = target, .location = location});
            }
        };

        for (const auto& use : tmpl->template_uses) {
            add_edge(use.template_name, use.location);
        }

        // Child `from` references and child body `use` entries are template
        // dependency edges too: creating the template creates its children
        // (dsl-hierarchical-entity-templates D7/D11).
        std::function<void(const ChildArchetypeNode&)> collect_child_edges =
            [&add_edge, &collect_child_edges](const ChildArchetypeNode& child) {
                if (child.template_ref.has_value()) {
                    add_edge(*child.template_ref, child.location);
                }
                for (const auto& use : child.template_uses) {
                    add_edge(use.template_name, use.location);
                }
                for (const auto& grandchild : child.children) {
                    collect_child_edges(grandchild);
                }
            };
        for (const auto& child : tmpl->children) {
            collect_child_edges(child);
        }
    }

    enum class VisitState { Visiting, Done };
    std::unordered_map<std::string, VisitState> state;
    std::vector<std::string> stack;
    std::unordered_set<std::string> reported_cycle_keys;

    auto report_cycle = [this, &stack, &reported_cycle_keys](const std::string& target,
                                                             const SourceLocation& location) {
        auto cycle_start = std::ranges::find(stack, target);
        if (cycle_start == stack.end()) {
            return;
        }

        std::vector<std::string> cycle(cycle_start, stack.end());
        cycle.push_back(target);

        std::string key;
        for (const auto& name : cycle) {
            if (!key.empty()) {
                key += "->";
            }
            key += name;
        }
        if (!reported_cycle_keys.insert(key).second) {
            return;
        }

        std::ostringstream msg;
        msg << "cyclic template-use graph detected: ";
        for (std::size_t i = 0; i < cycle.size(); ++i) {
            if (i > 0) {
                msg << " -> ";
            }
            msg << cycle[i];
        }
        errors_.error(location, msg.str());
    };

    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (auto state_it = state.find(name); state_it != state.end()) {
            return;
        }

        state[name] = VisitState::Visiting;
        stack.push_back(name);

        if (auto graph_it = graph.find(name); graph_it != graph.end()) {
            for (const auto& edge : graph_it->second) {
                auto edge_state = state.find(edge.target);
                if (edge_state == state.end()) {
                    visit(edge.target);
                    continue;
                }
                if (edge_state->second == VisitState::Visiting) {
                    report_cycle(edge.target, edge.location);
                }
            }
        }

        stack.pop_back();
        state[name] = VisitState::Done;
    };

    for (const auto& name : template_order) {
        visit(name);
    }
}

void SemanticAnalyzer::flatten_template_compositions(ProgramNode& program) {
    std::unordered_map<std::string, TemplateNode*> local_templates;
    std::vector<TemplateNode*> template_order;

    for (auto& decl : program.declarations) {
        if (auto* tmpl = std::get_if<TemplateNode>(&decl)) {
            local_templates[tmpl->name] = tmpl;
            template_order.push_back(tmpl);
        }
    }

    // Flattened archetypes carry both merged traits and recursively flattened
    // child trees (dsl-hierarchical-entity-templates D2).
    struct FlattenedArchetype {
        std::vector<ArchetypeTraitEntry> traits;
        std::vector<ChildArchetypeNode> children;
    };

    auto clone_flattened = [](const FlattenedArchetype& flattened) {
        return FlattenedArchetype{.traits   = clone_archetype_trait_entries(flattened.traits),
                                  .children = clone_child_archetype_nodes(flattened.children)};
    };

    std::unordered_map<std::string, FlattenedArchetype> flattened_templates;
    std::unordered_set<std::string> visiting;

    std::function<FlattenedArchetype(TemplateNode&)> flatten_template;
    std::function<FlattenedArchetype(const std::vector<ArchetypeBodyEntry>&,
                                     const std::vector<ArchetypeTemplateUseEntry>&,
                                     const std::vector<ArchetypeTraitEntry>&,
                                     const std::vector<ChildArchetypeNode>&)>
        flatten_body;
    std::function<ChildArchetypeNode(const ChildArchetypeNode&)> flatten_child;

    auto append_template_use = [&](FlattenedArchetype& merged, const ArchetypeTemplateUseEntry& use) {
        if (use.template_name.contains('.')) {
            // Imported templates are resolved during validation. Their concrete
            // archetype bodies are not present in this compilation unit, so they
            // remain represented by the original template-use entry for later
            // multi-module/backend handling.
            return;
        }

        auto template_it = local_templates.find(use.template_name);
        if (template_it == local_templates.end() || template_it->second == nullptr) {
            return;
        }

        auto used = flatten_template(*template_it->second);
        for (const auto& used_trait : used.traits) {
            merge_trait_entry_into(merged.traits, used_trait);
        }
        for (const auto& used_child : used.children) {
            merge_child_archetype_into(merged.children, used_child);
        }
    };

    flatten_body = [&](const std::vector<ArchetypeBodyEntry>& body_entries,
                       const std::vector<ArchetypeTemplateUseEntry>& template_uses,
                       const std::vector<ArchetypeTraitEntry>& traits,
                       const std::vector<ChildArchetypeNode>& children) {
        FlattenedArchetype merged;

        if (body_entries.empty()) {
            // Some tests construct AST nodes directly and predate body_entries.
            // Preserve the parser's effective order as use entries followed by
            // explicit trait blocks for those synthetic nodes.
            for (const auto& template_use : template_uses) {
                append_template_use(merged, template_use);
            }
            for (const auto& trait : traits) {
                merge_trait_entry_into(merged.traits, trait);
            }
        } else {
            for (const auto& entry : body_entries) {
                if (entry.kind == ArchetypeBodyEntry::Kind::TemplateUse) {
                    if (entry.index < template_uses.size()) {
                        append_template_use(merged, template_uses[entry.index]);
                    }
                    continue;
                }
                if (entry.index < traits.size()) {
                    merge_trait_entry_into(merged.traits, traits[entry.index]);
                }
            }
        }

        // Declared children flatten recursively and merge by role with any
        // children contributed by body-level `use` templates.
        for (const auto& child : children) {
            merge_child_archetype_into(merged.children, flatten_child(child));
        }

        return merged;
    };

    flatten_child = [&](const ChildArchetypeNode& node) {
        ChildArchetypeNode flat;
        flat.role     = node.role;
        flat.location = node.location;

        auto own = flatten_body(node.body_entries, node.template_uses, node.traits, node.children);
        if (!node.template_ref.has_value()) {
            flat.traits   = std::move(own.traits);
            flat.children = std::move(own.children);
            return flat;
        }

        // D11: a `from`-template child splices the template's flattened subtree
        // (traits and descendants), then applies the child body and nested
        // overrides on top.
        FlattenedArchetype base;
        bool base_available   = false;
        const auto dot        = node.template_ref->rfind('.');
        const auto lookup_key = dot != std::string::npos ? node.template_ref->substr(dot + 1) : *node.template_ref;
        if (auto template_it = local_templates.find(lookup_key);
            template_it != local_templates.end() && template_it->second != nullptr) {
            base           = flatten_template(*template_it->second);
            base_available = true;
        }

        for (const auto& trait : own.traits) {
            merge_trait_entry_into(base.traits, trait);
        }
        for (const auto& child : own.children) {
            merge_child_archetype_into(base.children, child);
        }
        if (base_available) {
            validate_child_override_tree(node.child_overrides,
                                         base.children,
                                         "child '" + node.role + "' (from template '" + *node.template_ref + "')",
                                         /*allow_self=*/false);
        }
        apply_child_overrides_onto(base.children, node.child_overrides);

        flat.traits   = std::move(base.traits);
        flat.children = std::move(base.children);
        return flat;
    };

    flatten_template = [&](TemplateNode& tmpl) -> FlattenedArchetype {
        if (auto flattened_it = flattened_templates.find(tmpl.name); flattened_it != flattened_templates.end()) {
            return clone_flattened(flattened_it->second);
        }

        if (!visiting.insert(tmpl.name).second) {
            // Cycle diagnostics are emitted by validate_template_use_cycles().
            // Avoid recursing indefinitely if analysis continues after errors.
            return FlattenedArchetype{.traits = clone_archetype_trait_entries(tmpl.traits), .children = {}};
        }

        auto flattened = flatten_body(tmpl.body_entries, tmpl.template_uses, tmpl.traits, tmpl.children);
        visiting.erase(tmpl.name);

        flattened_templates[tmpl.name] = clone_flattened(flattened);
        tmpl.traits                    = clone_archetype_trait_entries(flattened.traits);
        tmpl.children                  = clone_child_archetype_nodes(flattened.children);
        archetype_traits_[tmpl.name]   = &tmpl.traits;
        archetype_children_[tmpl.name] = &tmpl.children;
        return flattened;
    };

    for (auto* tmpl : template_order) {
        if (tmpl != nullptr) {
            (void)flatten_template(*tmpl);
        }
    }

    for (auto& decl : program.declarations) {
        if (auto* entity = std::get_if<EntityNode>(&decl)) {
            if (entity->template_ref.has_value()) {
                // Template-backed entity: start from template's flattened archetype, apply overrides
                const auto& tmpl_ref = *entity->template_ref;
                // archetype_traits_ uses unqualified names; strip module qualifier if present
                const auto dot        = tmpl_ref.rfind('.');
                const auto lookup_key = dot != std::string::npos ? tmpl_ref.substr(dot + 1) : tmpl_ref;
                auto tmpl_it          = archetype_traits_.find(lookup_key);
                if (tmpl_it != archetype_traits_.end() && tmpl_it->second != nullptr) {
                    auto merged = clone_archetype_trait_entries(*tmpl_it->second);
                    for (const auto& override_entry : entity->traits) {
                        merge_trait_entry_into(merged, override_entry);
                    }
                    entity->traits = std::move(merged);
                }
                // Splice the template's flattened child tree and apply the
                // entity's nested child overrides (validated afterwards in
                // validate_template_backed_entity_overrides).
                if (auto children_it = archetype_children_.find(lookup_key);
                    children_it != archetype_children_.end() && children_it->second != nullptr) {
                    auto merged_children = clone_child_archetype_nodes(*children_it->second);
                    apply_child_overrides_onto(merged_children, entity->child_overrides);
                    entity->children = std::move(merged_children);
                }
                // (If template not found, validation already reported the error; leave traits as-is)
            } else {
                auto flattened =
                    flatten_body(entity->body_entries, entity->template_uses, entity->traits, entity->children);
                entity->traits   = clone_archetype_trait_entries(flattened.traits);
                entity->children = clone_child_archetype_nodes(flattened.children);
            }
            archetype_traits_[entity->name]   = &entity->traits;
            archetype_children_[entity->name] = &entity->children;
        }
    }

    template_required_fields_.clear();
    for (auto* tmpl : template_order) {
        if (tmpl == nullptr) {
            continue;
        }

        std::unordered_set<std::string> provided;
        for (const auto& entry : tmpl->traits) {
            for (const auto& assign : entry.assignments) {
                provided.insert(assign.name);
            }
        }

        std::unordered_set<std::string> required;
        for (const auto& entry : tmpl->traits) {
            const auto* trait = find_resolved_trait(entry.trait_name);
            if (trait == nullptr) {
                continue;
            }
            for (const auto& field : trait->fields) {
                if (field.is_var && !field.has_default && !provided.contains(field.name)) {
                    required.insert(field.name);
                }
            }
        }
        template_required_fields_[tmpl->name] = std::move(required);
    }
}

// ── Template-backed entity override validation (runs after flattening) ───────

void SemanticAnalyzer::validate_template_backed_entity_overrides(  // NOLINT(readability-function-cognitive-complexity)
                                                                   // -- still 35 after trait-override extraction; not
                                                                   // further in scope here
    ProgramNode& program) {
    for (auto& decl : program.declarations) {
        auto* entity = std::get_if<EntityNode>(&decl);
        if (entity == nullptr || !entity->template_ref.has_value()) {
            continue;
        }

        const auto& tmpl_ref = *entity->template_ref;

        // Resolve the template's flattened archetype (already done by flatten_template_compositions).
        // archetype_traits_ uses unqualified names; strip module qualifier if present.
        const auto tmpl_dot = tmpl_ref.rfind('.');
        const auto tmpl_key = tmpl_dot != std::string::npos ? tmpl_ref.substr(tmpl_dot + 1) : tmpl_ref;
        auto tmpl_traits_it = archetype_traits_.find(tmpl_key);
        if (tmpl_traits_it == archetype_traits_.end() || tmpl_traits_it->second == nullptr) {
            continue;  // template not found — already reported in validate_template_unit_declarations
        }

        // Build set of trait names on the referenced template
        std::unordered_set<std::string> tmpl_trait_names;
        for (const auto& te : *tmpl_traits_it->second) {
            tmpl_trait_names.insert(te.trait_name);
        }

        // Validate each override entry
        std::vector<const ArchetypeTraitEntry*> valid_entries;
        for (auto& entry : entity->traits) {
            if (!tmpl_trait_names.contains(entry.trait_name)) {
                errors_.error(entry.location,
                              "trait '" + entry.trait_name + "' is not part of template '" + tmpl_ref +
                                  "'; cannot override it in entity '" + entity->name + "'");
                continue;
            }
            valid_entries.push_back(&entry);
        }
        validate_trait_override_assignments(
            valid_entries, "entity '" + entity->name + "'", /*check_self=*/true, /*report_unknown_trait=*/false);

        // Check required fields are satisfied by template defaults or entity overrides.
        // Deliberately scans every override (not just valid_entries above) to match
        // pre-refactor behavior — an override targeting an unrecognized trait still
        // counts its field names toward "provided", however incidentally.
        auto req_it = template_required_fields_.find(tmpl_key);
        if (req_it != template_required_fields_.end()) {
            std::unordered_set<std::string> provided;
            for (const auto& entry : entity->traits) {
                for (const auto& assign : entry.assignments) {
                    provided.insert(assign.name);
                }
            }
            for (const auto& req : req_it->second) {
                if (!provided.contains(req)) {
                    errors_.error(entity->location,
                                  "required field '" + req + "' not set for entity '" + entity->name + "'");
                }
            }
        }

        // Validate nested child overrides against the template's flattened
        // child tree (dsl-hierarchical-entity-templates D5).
        if (auto children_it = archetype_children_.find(tmpl_key);
            children_it != archetype_children_.end() && children_it->second != nullptr) {
            validate_child_override_tree(entity->child_overrides,
                                         *children_it->second,
                                         "entity '" + entity->name + "' (from template '" + tmpl_ref + "')",
                                         /*allow_self=*/false);
        }
    }
}

// ── Task 5.3, 5.4: Validate spawn sites ─────────────────────────────────────

void SemanticAnalyzer::validate_spawn_stmts(  // NOLINT(readability-function-cognitive-complexity) -- still 63 after
                                              // trait-override extraction; not further in scope here
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::string& context_name) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &context_name](auto& s) {  // NOLINT(readability-function-cognitive-complexity) -- still 38 after
                                              // trait-override extraction; not further in scope here
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, SpawnStmt>) {
                    // Handle dotted template references from imports (task 3.2).
                    auto tmpl_dot = s.template_name.rfind('.');
                    if (tmpl_dot != std::string::npos) {
                        validate_archetype_template_ref(s.template_name, s.location, "spawn statement");
                        return;  // field validation deferred — imported template body not available locally
                    }
                    // 5.4: Reject spawn of an entity
                    if (entity_names_.contains(s.template_name)) {
                        errors_.error(s.location,
                                      "'" + s.template_name +
                                          "' is an entity, not a template; use `spawn` only with "
                                          "`template` declarations");
                        return;
                    }
                    // 5.3: Reject spawn of unknown template
                    if (!template_names_.contains(s.template_name)) {
                        errors_.error(s.location, "undefined template '" + s.template_name + "'");
                        return;
                    }
                    // 5.3: Validate override trait blocks
                    auto traits_it = archetype_traits_.find(s.template_name);
                    if (traits_it != archetype_traits_.end() && traits_it->second != nullptr) {
                        // Validate each override trait entry
                        std::vector<const ArchetypeTraitEntry*> override_entries;
                        override_entries.reserve(s.overrides.size());
                        for (const auto& override : s.overrides) {
                            override_entries.push_back(&override);
                        }
                        validate_trait_override_assignments(override_entries,
                                                            "spawn override",
                                                            /*check_self=*/false,
                                                            /*report_unknown_trait=*/true);

                        // 5.3: Check required fields are provided
                        auto req_it = template_required_fields_.find(s.template_name);
                        if (req_it != template_required_fields_.end()) {
                            std::unordered_set<std::string> provided;
                            for (auto& override : s.overrides) {
                                for (auto& assign : override.assignments) {
                                    provided.insert(assign.name);
                                }
                            }
                            for (auto& req : req_it->second) {
                                if (!provided.contains(req)) {
                                    errors_.error(
                                        s.location,
                                        "required field '" + req + "' not set for template '" + s.template_name + "'");
                                }
                            }
                        }
                    }

                    // Hierarchical child overrides at the spawn site (only
                    // meaningful once flattening has registered child trees).
                    if (auto children_it = archetype_children_.find(s.template_name);
                        children_it != archetype_children_.end() && children_it->second != nullptr) {
                        validate_child_override_tree(s.child_overrides,
                                                     *children_it->second,
                                                     "template '" + s.template_name + "'",
                                                     /*allow_self=*/true);
                        validate_child_required_fields(*children_it->second,
                                                       s.child_overrides,
                                                       "spawn of template '" + s.template_name + "'",
                                                       s.location);
                    }
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    validate_spawn_stmts(s.then_body, context_name);
                    for (const auto& branch : s.else_if_branches) {
                        validate_spawn_stmts(branch.body, context_name);
                    }
                    validate_spawn_stmts(s.else_body, context_name);
                }
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_spawn_expr(const SpawnExpr& spawn, const SourceLocation& location) {
    // Handle dotted template references from imports (task 3.2).
    if (spawn.template_name.rfind('.') != std::string::npos) {
        validate_archetype_template_ref(spawn.template_name, location, "spawn expression");
        return;  // field validation deferred — imported template body not available locally
    }
    if (entity_names_.contains(spawn.template_name)) {
        errors_.error(location,
                      "'" + spawn.template_name +
                          "' is an entity, not a template; use `spawn` only with `template` declarations");
        return;
    }
    if (!template_names_.contains(spawn.template_name)) {
        errors_.error(location, "undefined template '" + spawn.template_name + "'");
        return;
    }

    auto traits_it = archetype_traits_.find(spawn.template_name);
    if (traits_it == archetype_traits_.end() || traits_it->second == nullptr) {
        return;
    }

    std::vector<const ArchetypeTraitEntry*> override_entries;
    override_entries.reserve(spawn.overrides.size());
    for (const auto& override_entry : spawn.overrides) {
        override_entries.push_back(&override_entry);
    }
    std::unordered_set<std::string> provided;
    validate_trait_override_assignments(
        override_entries, "spawn override", /*check_self=*/false, /*report_unknown_trait=*/true, &provided);

    if (auto req_it = template_required_fields_.find(spawn.template_name); req_it != template_required_fields_.end()) {
        for (const auto& req : req_it->second) {
            if (!provided.contains(req)) {
                errors_.error(location,
                              "required field '" + req + "' not set for template '" + spawn.template_name + "'");
            }
        }
    }

    // Hierarchical child overrides at the spawn site. archetype_children_ is
    // populated during flattening, so this only runs in the post-flattening
    // validate_spawn_sites pass (validate_event_usage also reaches here
    // earlier, before child trees exist).
    if (auto children_it = archetype_children_.find(spawn.template_name);
        children_it != archetype_children_.end() && children_it->second != nullptr) {
        validate_child_override_tree(spawn.child_overrides,
                                     *children_it->second,
                                     "template '" + spawn.template_name + "'",
                                     /*allow_self=*/true);
        validate_child_required_fields(
            *children_it->second, spawn.child_overrides, "spawn of template '" + spawn.template_name + "'", location);
    }
}

void SemanticAnalyzer::validate_spawn_exprs(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                            const std::string& context_name) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &context_name](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, LetStmt>) {
                    if (const auto* spawn = std::get_if<SpawnExpr>(&s.value->expr)) {
                        validate_spawn_expr(*spawn, s.location);
                    }
                } else if constexpr (std::is_same_v<S, ExprStmt>) {
                    if (const auto* spawn = std::get_if<SpawnExpr>(&s.expr->expr)) {
                        validate_spawn_expr(*spawn, s.location);
                    }
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    validate_spawn_exprs(s.then_body, context_name);
                    for (const auto& branch : s.else_if_branches) {
                        validate_spawn_exprs(branch.body, context_name);
                    }
                    validate_spawn_exprs(s.else_body, context_name);
                }
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_spawn_sites(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* rule = std::get_if<RuleNode>(&decl)) {
            for (auto& handler : rule->handlers) {
                validate_spawn_stmts(handler.body, rule->name);
                validate_spawn_exprs(handler.body, rule->name);
            }
        }
    }
}

// ── Task 5.5, 5.6, 5.7: Statement context validation ────────────────────────

void SemanticAnalyzer::validate_context_stmts(  // NOLINT(readability-function-cognitive-complexity) -- still 139 after
                                                // require_optional_entity_id_target extraction; not further in scope
                                                // here
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::string& context_name,
    bool in_rule_handler) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &context_name, in_rule_handler](auto& s) {  // NOLINT(readability-function-cognitive-complexity)
                using S = std::decay_t<decltype(s)>;
                std::unordered_map<std::string, TypeInfo> self_context_locals;
                if (in_rule_handler) {
                    self_context_locals["__self_context"] = make_entity_id_type();
                }
                auto validate_self_expr = [this, in_rule_handler](const ExprNode& expr,
                                                                  const SourceLocation& location) {
                    if (!in_rule_handler && expr_contains_self(expr)) {
                        errors_.error(location, "`self` only allowed inside rule event handlers");
                    }
                };
                if constexpr (std::is_same_v<S, SpawnStmt> || std::is_same_v<S, DestroyStmt> ||
                              std::is_same_v<S, LoadStmt> || std::is_same_v<S, AddTraitStmt> ||
                              std::is_same_v<S, RemoveTraitStmt> || std::is_same_v<S, ProjectTraitStmt> ||
                              std::is_same_v<S, ForeachStmt> || std::is_same_v<S, TraitMatchStmt>) {
                    if (!in_rule_handler) {
                        // Determine which keyword is used
                        std::string kw;
                        if constexpr (std::is_same_v<S, SpawnStmt>) {
                            kw = "spawn";
                        } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                            kw = "destroy";
                        } else if constexpr (std::is_same_v<S, LoadStmt>) {
                            kw = "load";
                        } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                            kw = "add";
                        } else if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                            kw = "project";
                        } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                            kw = "for";
                        } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                            kw = "match";
                        } else {
                            kw = "remove";
                        }
                        errors_.error(s.location, "`" + kw + "` only allowed inside rule event handlers");
                    }
                    // 5.6: For LoadStmt, validate module name is reachable via `use`
                    if constexpr (std::is_same_v<S, LoadStmt>) {
                        if (in_rule_handler && !use_names_.contains(s.module_name)) {
                            // Check prefix match (e.g. use levels allows load levels.level1)
                            bool found = false;
                            for (const auto& use_name : use_names_) {
                                if (s.module_name == use_name || s.module_name.substr(0, use_name.size()) == use_name) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                errors_.error(s.location,
                                              "unknown module '" + s.module_name + "'; add `use " + s.module_name +
                                                  "` to import it");
                            }
                        }
                    }
                    if constexpr (std::is_same_v<S, AddTraitStmt> || std::is_same_v<S, RemoveTraitStmt>) {
                        const std::string& tname = s.trait_name;
                        if (!is_trait_declared(tname)) {
                            const auto prev_errors = errors_.error_count();
                            (void)resolve_trait_ref_to_canonical(tname, s.location);
                            if (errors_.error_count() == prev_errors) {
                                const std::string KW = std::is_same_v<S, AddTraitStmt> ? "add" : "remove";
                                std::string msg      = "undeclared trait '";
                                msg += tname;
                                msg += "' in `";
                                msg += KW;
                                msg += "` statement";
                                errors_.error(s.location, msg);
                            }
                        }
                        if constexpr (std::is_same_v<S, AddTraitStmt>) {
                            require_optional_entity_id_target(s.target_expr,
                                                              s.location,
                                                              "`to` target must be of type `entity_id`",
                                                              {},
                                                              self_context_locals,
                                                              nullptr);
                        } else {
                            require_optional_entity_id_target(s.target_expr,
                                                              s.location,
                                                              "`from` target must be of type `entity_id`",
                                                              {},
                                                              self_context_locals,
                                                              nullptr);
                        }
                    }
                    if constexpr (std::is_same_v<S, ProjectTraitStmt>) {
                        const std::string& tname = s.trait_name;
                        if (!is_trait_declared(tname)) {
                            const auto prev_errors = errors_.error_count();
                            (void)resolve_trait_ref_to_canonical(tname, s.location);
                            if (errors_.error_count() == prev_errors) {
                                errors_.error(s.location, "undeclared trait '" + tname + "' in `project` statement");
                            }
                        }
                        require_optional_entity_id_target(s.target_expr,
                                                          s.location,
                                                          "`project ... to` target must be of type `entity_id`",
                                                          {},
                                                          self_context_locals,
                                                          nullptr);
                        for (const auto& arg : s.args) {
                            validate_self_expr(*arg.value, arg.location);
                        }
                    }
                    if constexpr (std::is_same_v<S, ForeachStmt>) {
                        validate_self_expr(*s.iterable, s.location);
                        validate_context_stmts(s.body, context_name, in_rule_handler);
                    }
                    if constexpr (std::is_same_v<S, DestroyStmt>) {
                        require_optional_entity_id_target(s.target_expr,
                                                          s.location,
                                                          "`destroy` target must be of type `entity_id`",
                                                          {},
                                                          self_context_locals,
                                                          nullptr);
                    }
                    if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                        for (const auto& arm : s.arms) {
                            validate_context_stmts(arm.body, context_name, in_rule_handler);
                        }
                        if (s.wildcard.has_value()) {
                            validate_context_stmts(s.wildcard->body, context_name, in_rule_handler);
                        }
                    }
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    validate_context_stmts(s.then_body, context_name, in_rule_handler);
                    for (const auto& branch : s.else_if_branches) {
                        validate_context_stmts(branch.body, context_name, in_rule_handler);
                    }
                    validate_context_stmts(s.else_body, context_name, in_rule_handler);
                } else if constexpr (std::is_same_v<S, LetStmt> || std::is_same_v<S, VarAssign>) {
                    validate_self_expr(*s.value, s.location);
                } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                    if (s.value.has_value()) {
                        validate_self_expr(**s.value, s.location);
                    }
                } else if constexpr (std::is_same_v<S, ExprStmt>) {
                    validate_self_expr(*s.expr, s.location);
                }
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_stmt_contexts(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, FuncNode>) {
                    // func bodies must not contain spawn/destroy/load/add/remove
                    validate_context_stmts(node.body, node.name, false);
                } else if constexpr (std::is_same_v<T, RuleNode>) {
                    // Rule handlers: these statements are valid
                    for (auto& handler : node.handlers) {
                        validate_context_stmts(handler.body, node.name, true);
                    }
                }
            },
            decl);
    }
}

// ── Task 5.9: Validate exclude clause trait names ────────────────────────────
// (called as part of validate_rule_filters — integrated inline above)
// Note: exclude clause validation is done here as a separate pass for clarity.

// ── Task 11.12: Check no field access in no-filter rule bodies ───────────────

void SemanticAnalyzer::check_no_field_access(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                             const std::string& rule_name) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &rule_name](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, LetStmt>) {
                    // local binding is allowed without filter access checks
                } else if constexpr (std::is_same_v<S, VarAssign>) {
                    // All VarAssign statements in rule handlers are trait-field accesses
                    errors_.error(s.location,
                                  "trait field '" + s.name + "' is not accessible in rule '" + rule_name +
                                      "': no filter clause declares this trait");
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    check_no_field_access(s.then_body, rule_name);
                    for (const auto& branch : s.else_if_branches) {
                        check_no_field_access(branch.body, rule_name);
                    }
                    check_no_field_access(s.else_body, rule_name);
                }
                // emit, spawn, destroy, load, add, remove, return, expr: all allowed
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_trait_modifier_rules(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, RuleNode> || std::is_same_v<T, ExternRuleNode>) {
                    if (!node.exclude.entries.empty() || !node.exclude.trait_names.empty()) {
                        validate_exclude_clause(node);
                    }
                }
            },
            decl);
    }
}

void SemanticAnalyzer::validate_exclude_clause(const auto& node) {
    if (!node.exclude.entries.empty()) {
        for (auto& entry : node.exclude.entries) {
            if (!is_trait_declared(entry.qualified_name)) {
                const auto prev_errors = errors_.error_count();
                (void)resolve_trait_ref_to_canonical(entry.qualified_name, entry.location);
                if (errors_.error_count() == prev_errors) {
                    errors_.error(entry.location, "undeclared trait '" + entry.qualified_name + "' in exclude clause");
                }
            }
        }
    } else {
        for (auto& trait_name : node.exclude.trait_names) {
            if (!is_trait_declared(trait_name)) {
                const auto prev_errors = errors_.error_count();
                (void)resolve_trait_ref_to_canonical(trait_name, node.exclude.location);
                if (errors_.error_count() == prev_errors) {
                    errors_.error(node.exclude.location, "undeclared trait '" + trait_name + "' in exclude clause");
                }
            }
        }
    }
}

// ── Phase 5a: after: clause validation and cycle detection ─────────────────

void SemanticAnalyzer::validate_after_clauses(ProgramNode& program) {
    std::unordered_set<std::string> local_rule_names;
    std::unordered_set<SymbolId> local_rule_ids;
    for (auto& decl : program.declarations) {
        if (auto* rule = std::get_if<RuleNode>(&decl)) {
            local_rule_names.insert(rule->name);
            if (rule->resolved_rule_id.has_value()) {
                local_rule_ids.insert(*rule->resolved_rule_id);
            }
        }
        if (auto* rule = std::get_if<ExternRuleNode>(&decl)) {
            local_rule_names.insert(rule->name);
            if (rule->resolved_rule_id.has_value()) {
                local_rule_ids.insert(*rule->resolved_rule_id);
            }
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> after_resolved;
    for (auto& decl : program.declarations) {
        auto resolve_after = [&](auto& rule) {
            rule.resolved_after_rule_ids.clear();
            if (!rule.resolved_rule_id.has_value()) {
                return;
            }
            std::unordered_set<SymbolId> seen;
            for (const auto& after_ref : rule.after_rules) {
                auto predecessor = resolve_rule_after_ref_to_symbol(after_ref, rule.location, local_rule_names);
                if (!predecessor.has_value()) {
                    const auto previous_errors = errors_.error_count();
                    (void)resolve_rule_after_ref(after_ref, rule.location, local_rule_names);
                    if (errors_.error_count() == previous_errors) {
                        errors_.error(rule.location, "unknown rule '" + after_ref + "' in after clause");
                    }
                    continue;
                }
                if (*predecessor == *rule.resolved_rule_id) {
                    errors_.error(rule.location, "rule '" + rule.name + "' cannot list itself in after:");
                    continue;
                }
                if (!seen.insert(*predecessor).second) {
                    continue;
                }
                rule.resolved_after_rule_ids.push_back(*predecessor);
                after_resolved[rule.name].push_back(make_canonical_id(*predecessor));
            }
        };
        if (auto* rule = std::get_if<RuleNode>(&decl)) {
            resolve_after(*rule);
        }
        if (auto* rule = std::get_if<ExternRuleNode>(&decl)) {
            resolve_after(*rule);
        }
    }

    // Preserve the compatibility dependency view while the handler graph is
    // the authoritative representation of executable ordering.
    for (auto& dep : result_.dependency_graph) {
        if (const auto found = after_resolved.find(dep.rule_name); found != after_resolved.end()) {
            dep.after_rules = found->second;
        }
    }

    std::unordered_map<HandlerIdentity, HandlerNode*, HandlerIdentityHash> nodes;
    for (auto& node : result_.execution_graph.handlers) {
        nodes.emplace(node.identity, &node);
    }

    auto add_edge = [&](HandlerNode& dependent,
                        const HandlerIdentity& predecessor,
                        ScheduleEdgeKind kind,
                        const SourceLocation& location,
                        bool require_local_node) {
        if (predecessor == dependent.identity) {
            errors_.error(location, "handler '" + dependent.identity.canonical_id() + "' cannot list itself in after:");
            return;
        }
        if (require_local_node && local_rule_ids.contains(predecessor.rule) && !nodes.contains(predecessor)) {
            errors_.error(location, "unknown handler '" + predecessor.canonical_id() + "' in after: clause");
            return;
        }
        if (std::ranges::find(dependent.explicit_after, predecessor) == dependent.explicit_after.end()) {
            dependent.explicit_after.push_back(predecessor);
        }
        const auto duplicate = std::ranges::find_if(result_.execution_graph.schedule_edges, [&](const auto& edge) {
            return edge.before == predecessor && edge.after == dependent.identity && edge.kind == kind;
        });
        if (duplicate == result_.execution_graph.schedule_edges.end()) {
            result_.execution_graph.schedule_edges.push_back(
                ScheduleEdge{.before      = predecessor,
                             .after       = dependent.identity,
                             .kind        = kind,
                             .orientation = ScheduleEdgeOrientation::Explicit});
        }
    };

    for (auto& decl : program.declarations) {
        auto expand_rule = [&](auto& rule) {
            if (!rule.resolved_rule_id.has_value()) {
                return;
            }

            for (auto& dependent : result_.execution_graph.handlers) {
                if (dependent.identity.rule != *rule.resolved_rule_id) {
                    continue;
                }
                for (const auto& predecessor_rule : rule.resolved_after_rule_ids) {
                    const HandlerIdentity predecessor{.rule = predecessor_rule, .trigger = dependent.identity.trigger};
                    // A local predecessor with no matching trigger is not an
                    // edge. Imported candidates are retained for linker
                    // validation once their handler metadata is available.
                    if (local_rule_ids.contains(predecessor_rule) && !nodes.contains(predecessor)) {
                        continue;
                    }
                    add_edge(dependent, predecessor, ScheduleEdgeKind::ExplicitRule, rule.location, false);
                }
            }

            for (std::size_t handler_index = 0; handler_index < rule.handlers.size(); ++handler_index) {
                const auto& handler = rule.handlers[handler_index];
                if (!handler.resolved_trigger.has_value()) {
                    continue;
                }
                const HandlerIdentity dependent_identity{.rule    = *rule.resolved_rule_id,
                                                         .trigger = *handler.resolved_trigger};
                const auto dependent_it = nodes.find(dependent_identity);
                if (dependent_it == nodes.end() ||
                    dependent_it->second->declaration_order.handler_index != handler_index) {
                    continue;
                }
                auto& dependent = *dependent_it->second;
                for (const auto& reference : handler.after_handlers) {
                    auto predecessor_rule = resolve_rule_after_ref_to_symbol(
                        reference.rule.spelling, reference.rule.location, local_rule_names);
                    if (!predecessor_rule.has_value()) {
                        const auto previous_errors = errors_.error_count();
                        (void)resolve_rule_after_ref(
                            reference.rule.spelling, reference.rule.location, local_rule_names);
                        if (errors_.error_count() == previous_errors) {
                            errors_.error(reference.rule.location,
                                          "unknown rule '" + reference.rule.spelling + "' in handler after: clause");
                        }
                        continue;
                    }
                    const auto predecessor_trigger = try_resolve_handler_trigger(reference.trigger.spelling);
                    if (!predecessor_trigger.has_value()) {
                        errors_.error(
                            reference.trigger.location,
                            "unknown handler trigger '" + reference.trigger.spelling + "' in handler after: clause");
                        continue;
                    }
                    if (*predecessor_trigger != dependent.identity.trigger) {
                        errors_.error(reference.location,
                                      "handler dependency '" + reference.spelling() + "' is not eligible for '" +
                                          dependent.identity.canonical_id() + "': resolved triggers differ");
                        continue;
                    }
                    add_edge(dependent,
                             HandlerIdentity{.rule = *predecessor_rule, .trigger = *predecessor_trigger},
                             ScheduleEdgeKind::ExplicitHandler,
                             reference.location,
                             true);
                }
            }
        };
        if (auto* rule = std::get_if<RuleNode>(&decl)) {
            expand_rule(*rule);
        } else if (auto* extern_rule = std::get_if<ExternRuleNode>(&decl)) {
            expand_rule(*extern_rule);
        }
    }

    // Conflict-edge detection, handler-cycle checking, and per-activation
    // topological leveling are computed by the shared scheduling core, which
    // also performs the phase-barrier/event-flow construction that
    // build_dependency_graph used to do inline (see execution_graph_scheduler.hpp).
    (void)compute_handler_schedule(result_.execution_graph, errors_);
}

}  // namespace cactus
