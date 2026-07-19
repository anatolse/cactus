#include "frontend/semantic_analyzer.hpp"

#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <ranges>
#include <sstream>

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
    for (auto& [name, sys] : pub_syms.systems) {
        const auto symbol = sys.symbol_id.value_or(make_symbol_id(SymbolKind::System, pub_syms.module_name, name));
        sys.name          = symbol.local_name;
        sys.module_name   = symbol.module.name;
        sys.canonical_id  = make_canonical_id(symbol);
        sys.symbol_id     = symbol;
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
    // Index imported extern system names (for after: resolution)
    for (const auto& [sys_name, _] : pub_syms.systems) {
        system_providers[sys_name].push_back(qualifier);
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
    func_names_.clear();
    system_names_.clear();
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

    // Phase 2: Resolve types in fields
    resolve_all_types(program);
    resolve_trait_references(program);

    // Phase 3: Semantic checks
    check_const_strings(program);
    check_func_purity(program);
    check_no_recursion(program);
    check_persist_sync(program);
    validate_system_filters(program);
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

void SemanticAnalyzer::collect_types(ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
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
                assign_canonical_identity(event_stub, event_id);
                event_structs_[ev_name] = std::move(event_stub);
            }
        }
    }

    // First pass: populate the complete module-scope namespace and all local
    // declaration-name sets before resolving any reference sites. This ensures
    // forward references in event fields and later semantic phases see the same
    // typed local symbol table.
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {  // NOLINT(readability-function-cognitive-complexity)
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
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    node.is_stdlib = current_module_is_stdlib_;
                    declare_module_scope_symbol(SymbolKind::Func, node.name, node.location);
                    func_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, SystemNode> || std::is_same_v<T, ExternSystemNode>) {
                    node.is_stdlib = current_module_is_stdlib_;
                    declare_module_scope_symbol(SymbolKind::System, node.name, node.location);
                    system_names_.insert(node.name);
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
                    const auto event_id = make_symbol_id(SymbolKind::Event, current_module_id_, node.name);
                    ResolvedStruct rs;
                    rs.name = event_id.local_name;
                    assign_canonical_identity(rs, event_id);
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
                        rs.fields.push_back(std::move(rf));
                    }
                    event_structs_[node.name] = std::move(rs);
                }
            },
            decl);
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

void SemanticAnalyzer::resolve_trait_references(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
    auto resolve_trait_entry = [this](ArchetypeTraitEntry& entry) {
        entry.resolved_trait_id = try_resolve_trait_ref_to_symbol(entry.trait_name);
    };

    std::function<std::optional<std::string>(const ExprNode&)> module_scope_expr_path =
        [&](const ExprNode& expr) -> std::optional<std::string> {
        if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
            return ident->name;
        }
        if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
            auto prefix = module_scope_expr_path(*member->object);
            if (prefix.has_value()) {
                return *prefix + "." + member->member;
            }
        }
        return std::nullopt;
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
            [&](auto& e) {  // NOLINT(readability-function-cognitive-complexity)
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, UnaryExpr>) {
                    resolve_expr(*e.operand);
                } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                    resolve_expr(*e.left);
                    resolve_expr(*e.right);
                } else if constexpr (std::is_same_v<E, CallExpr>) {
                    if (auto callee_path = module_scope_expr_path(*e.callee)) {
                        e.resolved_callee_id = try_resolve_func_ref_to_symbol(*callee_path);
                    }
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
                    if (auto callee_path = module_scope_expr_path(*e.callee)) {
                        e.resolved_callee_id = try_resolve_func_ref_to_symbol(*callee_path);
                    }
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
                [&](auto& s) {  // NOLINT(readability-function-cognitive-complexity)
                    using S = std::decay_t<decltype(s)>;
                    if constexpr (std::is_same_v<S, LetStmt>) {
                        resolve_expr(*s.value);
                    } else if constexpr (std::is_same_v<S, VarAssign>) {
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
            [&](auto& node) {  // NOLINT(readability-function-cognitive-complexity)
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
                } else if constexpr (std::is_same_v<T, SystemNode>) {
                    node.resolved_system_id = make_symbol_id(SymbolKind::System, current_module_id_, node.name);
                    resolve_filter_clause(node.filter);
                    resolve_filter_clause(node.exclude);
                    for (auto& handler : node.handlers) {
                        handler.resolved_event_id = try_resolve_event_ref_to_symbol(handler.event_name);
                        resolve_stmts(handler.body);
                    }
                } else if constexpr (std::is_same_v<T, ExternSystemNode>) {
                    node.resolved_system_id = make_symbol_id(SymbolKind::System, current_module_id_, node.name);
                    resolve_filter_clause(node.filter);
                    resolve_filter_clause(node.exclude);
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
    // Qualified references may explicitly name the current module.
    if (qualifier == current_module_name_) {
        if (struct_names_.contains(sym_name)) {
            return make_resolved_user_type(TypeKind::Struct,
                                           make_symbol_id(SymbolKind::Struct, current_module_id_, sym_name));
        }
        if (enum_names_.contains(sym_name)) {
            return make_resolved_user_type(TypeKind::Enum,
                                           make_symbol_id(SymbolKind::Enum, current_module_id_, sym_name));
        }
    }

    auto it = imports_.modules.find(qualifier);
    if (it == imports_.modules.end()) {
        errors_.error(loc, "unknown module qualifier '" + qualifier + "'");
        return make_unknown_type();
    }

    const auto& syms = it->second;

    // Check imported structs
    if (auto struct_it = syms.structs.find(sym_name); struct_it != syms.structs.end()) {
        const auto symbol = resolved_decl_symbol(struct_it->second, SymbolKind::Struct, syms.module_name, sym_name);
        return make_resolved_user_type(TypeKind::Struct, symbol);
    }
    // Check imported enums
    if (auto enum_it = syms.enums.find(sym_name); enum_it != syms.enums.end()) {
        const auto symbol = resolved_decl_symbol(enum_it->second, SymbolKind::Enum, syms.module_name, sym_name);
        return make_resolved_user_type(TypeKind::Enum, symbol);
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
    const auto providers = type_provider_qualifiers(imports_, name);
    if (providers.empty()) {
        errors_.error(loc, "unknown type '" + name + "'");
        return make_unknown_type();
    }

    if (auto prelude = find_std_core_provider(imports_, providers)) {
        const auto module_it = imports_.modules.find(*prelude);
        if (module_it != imports_.modules.end()) {
            if (auto struct_it = module_it->second.structs.find(name); struct_it != module_it->second.structs.end()) {
                const auto symbol =
                    resolved_decl_symbol(struct_it->second, SymbolKind::Struct, module_it->second.module_name, name);
                return make_resolved_user_type(TypeKind::Struct, symbol);
            }
            if (auto enum_it = module_it->second.enums.find(name); enum_it != module_it->second.enums.end()) {
                const auto symbol =
                    resolved_decl_symbol(enum_it->second, SymbolKind::Enum, module_it->second.module_name, name);
                return make_resolved_user_type(TypeKind::Enum, symbol);
            }
        }
    }

    errors_.error(loc, imported_reference_diagnostic(imports_, "type", name, providers));
    return make_unknown_type();
}

bool SemanticAnalyzer::is_known_type(const std::string& name) const {
    return (builtin_types().contains(name)) || (struct_names_.contains(name)) || (enum_names_.contains(name));
}

// ── Phase 3a: Const String Check ────────────────────────────────────────────

void SemanticAnalyzer::check_const_strings(ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
    for (auto& decl : program.declarations) {
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        std::visit(
            [this](auto& node) {  // NOLINT(readability-function-cognitive-complexity)
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ConstBlockNode>) {
                    for (auto& a : node.assignments) {
                        check_const_strings_expr(*a.value, true);
                    }
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    for (auto& stmt : node.body) {
                        // NOLINTNEXTLINE(readability-function-cognitive-complexity,bugprone-branch-clone)
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
                for (auto& inner : s.else_body) {
                    check_func_purity_stmt(*inner, func_name);
                }
            }
        },
        stmt.stmt);
}

void SemanticAnalyzer::check_func_purity_expr(const ExprNode& expr, const std::string& func_name) {
    std::visit(
        [this, &func_name](auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, CallExpr>) {
                check_func_purity_expr(*e.callee, func_name);
                for (auto& arg : e.args) {
                    check_func_purity_expr(*arg, func_name);
                }
                if (auto* ident = std::get_if<IdentExpr>(&e.callee->expr);
                    ident != nullptr && ident->name == "exists") {
                    errors_.error(e.location,
                                  "`exists()` requires world access; only allowed inside system event handlers");
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
                            errors_.error(
                                e.location,
                                imported_reference_diagnostic(imports_, "function", ident->name, pit->second));
                        }
                    }
                }
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                check_func_purity_expr(*e.left, func_name);
                check_func_purity_expr(*e.right, func_name);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                check_func_purity_expr(*e.operand, func_name);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                check_func_purity_expr(*e.object, func_name);
            } else if constexpr (std::is_same_v<E, SpawnExpr>) {
                for (auto& trait : e.overrides) {
                    for (auto& field : trait.assignments) {
                        check_func_purity_expr(*field.value, func_name);
                    }
                }
            } else if constexpr (std::is_same_v<E, QueryCallExpr>) {
                errors_.error(e.location,
                              "query expressions require world access; only allowed inside system event handlers");
                for (auto& arg : e.named_args) {
                    check_func_purity_expr(*arg.value, func_name);
                }
            }
        },
        expr.expr);
}

// ── Phase 3c: No Recursion ──────────────────────────────────────────────────

void SemanticAnalyzer::check_no_recursion(ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
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

// ── Phase 3e: System Filter Validation (tasks 4.2, 4.4, 4.5, 4.6) ──────────

bool SemanticAnalyzer::resolve_filter_entry(const FilterEntry& entry, std::string& out_simple_name) {
    const auto& qname = entry.qualified_name;
    auto dot          = qname.find('.');

    if (dot != std::string::npos) {
        // ── Qualified: "module.Trait" or "alias.Trait" ──────────────────────
        auto last_dot          = qname.rfind('.');
        auto qualifier         = qname.substr(0, last_dot);
        auto trait_name        = qname.substr(last_dot + 1);
        auto simple_trait_name = (last_dot != std::string::npos) ? qname.substr(last_dot + 1) : trait_name;

        // Qualified local references must use the current module namespace.
        if (trait_names_.contains(simple_trait_name) && qualifier == current_module_name_) {
            out_simple_name = simple_trait_name;
            return true;
        }

        auto it = imports_.modules.find(qualifier);
        if (it == imports_.modules.end()) {
            errors_.error(entry.location, "unknown module qualifier '" + qualifier + "' in filter");
            return false;
        }
        if (!it->second.traits.contains(trait_name)) {
            // Trait not found in pub traits — check for non-pub helpful error
            // ── Task 4.6: Non-pub helpful error ──────────────────────────────
            auto np_it = imports_.non_pub_trait_names.find(qualifier);
            if (np_it != imports_.non_pub_trait_names.end() && (np_it->second.contains(trait_name))) {
                errors_.error(entry.location,
                              "trait '" + trait_name + "' is not public in module '" + qualifier +
                                  "'; did you mean to mark it as 'pub'?");
            } else {
                errors_.error(
                    entry.location,
                    "system filter references unknown trait '" + trait_name + "' in module '" + qualifier + "'");
            }
            return false;
        }
        out_simple_name = trait_name;
        return true;
    }

    // ── Unqualified ──────────────────────────────────────────────────────────
    // Check local traits first
    if (trait_names_.contains(qname)) {
        out_simple_name = qname;
        return true;
    }
    // Only the std.core prelude is available through unqualified imported lookup.
    // Ordinary imports are namespace bindings and must be qualified.
    if (!imports_.empty()) {
        auto it = imports_.trait_providers.find(qname);
        if (it != imports_.trait_providers.end()) {
            if (find_std_core_provider(imports_, it->second).has_value()) {
                out_simple_name = qname;
                return true;
            }
            errors_.error(entry.location, imported_reference_diagnostic(imports_, "trait", qname, it->second));
            return false;
        }
    }

    errors_.error(entry.location, "unknown trait '" + qname + "' in filter");
    return false;
}

void SemanticAnalyzer::validateOrderByClause(
    const SystemNode& system) {  // NOLINT(readability-function-cognitive-complexity)
    if (system.order_by.empty()) {
        return;
    }

    if (system.filter.entries.empty() && system.filter.trait_names.empty()) {
        errors_.error(system.location,
                      "system '" + system.name + "' cannot use `order by:` without a `filter:` clause");
        return;
    }

    std::unordered_map<std::string, const ResolvedTrait*> filter_bindings;
    for (const auto& entry : system.filter.entries) {
        // Use full qualified name for canonical lookup (task 3.5).
        const auto* trait = find_resolved_trait(entry.resolved_trait_id, entry.qualified_name);
        auto dot          = entry.qualified_name.rfind('.');
        auto simple       = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
        if (trait != nullptr) {
            filter_bindings[simple] = trait;
            if (entry.alias.has_value()) {
                filter_bindings[*entry.alias] = trait;
            }
        }
    }
    for (const auto& name : system.filter.trait_names) {
        const auto* trait = find_resolved_trait(name);
        if (trait != nullptr) {
            filter_bindings[name] = trait;
        }
    }

    for (const auto& key : system.order_by) {
        auto binding_it = filter_bindings.find(key.alias);
        if (binding_it == filter_bindings.end() || binding_it->second == nullptr) {
            errors_.error(key.location, "order by alias '" + key.alias + "' is not declared in system filter");
            continue;
        }

        TypeInfo current;
        bool resolved_any         = false;
        const auto* current_trait = binding_it->second;
        size_t start              = 0;
        while (start < key.field.size()) {
            size_t dot         = key.field.find('.', start);
            std::string member = key.field.substr(start, dot == std::string::npos ? std::string::npos : dot - start);

            if (!resolved_any) {
                current      = find_field_type_in(current_trait->fields, member);
                resolved_any = true;
            } else if (current.kind == TypeKind::Vec2 || current.kind == TypeKind::Vec3) {
                if (member == "x" || member == "y" || (current.kind == TypeKind::Vec3 && member == "z")) {
                    current = make_float_type();
                } else {
                    current = make_unknown_type();
                }
            } else {
                current = make_unknown_type();
            }

            if (current.kind == TypeKind::Unknown) {
                errors_.error(key.location,
                              "order by field '" + key.alias + "." + key.field +
                                  "' is not valid for the referenced filter trait");
                break;
            }

            if (dot == std::string::npos) {
                break;
            }
            start = dot + 1;
        }

        if (current.kind == TypeKind::Unknown) {
            continue;
        }

        if (current.kind != TypeKind::Int && current.kind != TypeKind::Float && current.kind != TypeKind::Bool) {
            errors_.error(key.location,
                          "order by key '" + key.alias + "." + key.field +
                              "' must have scalar-comparable type (int, float, bool)");
        }
    }
}

void SemanticAnalyzer::validateOrderByClause(const ExternSystemNode& system) {
    if (system.order_by.empty()) {
        return;
    }

    SystemNode proxy;
    proxy.name          = system.name;
    proxy.filter        = system.filter;
    proxy.exclude       = system.exclude;
    proxy.order_by      = system.order_by;
    proxy.after_systems = system.after_systems;
    proxy.target        = system.target;
    proxy.location      = system.location;
    validateOrderByClause(proxy);
}

void SemanticAnalyzer::validate_system_filters(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            if (!sys->filter.entries.empty()) {
                // Rich filter entries (multi-module parser path)
                for (auto& entry : sys->filter.entries) {
                    std::string simple_name;
                    resolve_filter_entry(entry, simple_name);
                }
            } else {
                // Backward-compat: simple trait_names list
                for (auto& trait_name : sys->filter.trait_names) {
                    // Local trait — always valid
                    if (trait_names_.contains(trait_name)) {
                        continue;
                    }
                    const auto prev_errors = errors_.error_count();
                    const auto canonical   = resolve_trait_ref_to_canonical(trait_name, sys->filter.location);
                    if (canonical.empty() && errors_.error_count() == prev_errors) {
                        errors_.error(sys->filter.location,
                                      "system '" + sys->name + "' filters on unknown trait '" + trait_name + "'");
                    }
                }
            }

            // task 11.12: if system has no filter traits, handler bodies cannot
            // access trait fields (VarAssign is always a trait-field mutation)
            bool has_filter = !sys->filter.entries.empty() || !sys->filter.trait_names.empty();
            if (!has_filter) {
                for (auto& handler : sys->handlers) {
                    check_no_field_access(handler.body, sys->name);
                }
            }

            validateOrderByClause(*sys);
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            bool has_filter = !sys->filter.entries.empty() || !sys->filter.trait_names.empty();
            if (!has_filter) {
                errors_.error(
                    sys->location,
                    "`extern system` requires a `filter:` clause (no-filter extern systems are not supported)");
            }

            if (!sys->filter.entries.empty()) {
                for (auto& entry : sys->filter.entries) {
                    std::string simple_name;
                    resolve_filter_entry(entry, simple_name);
                }
            } else {
                for (auto& trait_name : sys->filter.trait_names) {
                    if (trait_names_.contains(trait_name)) {
                        continue;
                    }
                    const auto prev_errors = errors_.error_count();
                    const auto canonical   = resolve_trait_ref_to_canonical(trait_name, sys->filter.location);
                    if (canonical.empty() && errors_.error_count() == prev_errors) {
                        errors_.error(
                            sys->filter.location,
                            "extern system '" + sys->name + "' filters on unknown trait '" + trait_name + "'");
                    }
                }
            }

            validateOrderByClause(*sys);
        }
    }
}

// ── Phase 3f: Event Usage Validation ────────────────────────────────────────

void SemanticAnalyzer::validate_event_usage(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            // Build set of filter-bound names (trait names and their aliases) for alias conflict check
            std::unordered_set<std::string> filter_bound;
            for (const auto& entry : sys->filter.entries) {
                auto dot    = entry.qualified_name.rfind('.');
                auto simple = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
                filter_bound.insert(simple);
                if (entry.alias.has_value()) {
                    filter_bound.insert(*entry.alias);
                }
            }
            for (const auto& t : sys->filter.trait_names) {
                filter_bound.insert(t);
            }

            for (auto& handler : sys->handlers) {
                std::unordered_map<std::string, const ResolvedTrait*> filter_bindings;
                for (const auto& entry : sys->filter.entries) {
                    // Use the full qualified name so find_resolved_trait returns the
                    // correct trait even when multiple modules share a local name (task 3.5).
                    const auto* trait = find_resolved_trait(entry.resolved_trait_id, entry.qualified_name);
                    auto dot          = entry.qualified_name.rfind('.');
                    auto simple =
                        (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
                    if (trait != nullptr) {
                        filter_bindings[simple] = trait;
                        if (entry.alias.has_value()) {
                            filter_bindings[*entry.alias] = trait;
                        }
                    }
                }
                for (const auto& name : sys->filter.trait_names) {
                    const auto* trait = find_resolved_trait(name);
                    if (trait != nullptr) {
                        filter_bindings[name] = trait;
                    }
                }

                // Validate event is declared (must be in event_names_ — either local or imported from std.core)
                if (!event_names_.contains(handler.event_name)) {
                    errors_.error(handler.location,
                                  "system '" + sys->name + "' handles unknown event '" + handler.event_name + "'");
                }
                // Task 3.4: Validate handler alias doesn't conflict with filter aliases in scope
                if (handler.alias.has_value() && filter_bound.contains(*handler.alias)) {
                    errors_.error(handler.location,
                                  "handler alias '" + *handler.alias + "' conflicts with filter alias '" +
                                      *handler.alias + "' already in scope");
                }

                const ResolvedStruct* handler_event = find_resolved_event(handler.event_name);
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

                validate_event_stmts(handler.body, filter_bindings, local_bindings, handler_event, sys->name);
            }
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void SemanticAnalyzer::validate_event_stmts(
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event,
    const std::string& system_name) {
    (void)system_name;
    auto locals = local_bindings;

    auto validate_emit = [this, &filter_bindings, &locals, handler_event](const EmitStmt& emit) {
        if (!event_names_.contains(emit.event_name)) {
            errors_.error(emit.location, "undeclared event '" + emit.event_name + "'");
            return;
        }

        const auto* event = find_resolved_event(emit.event_name);
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

        auto target_type = infer_expr_type(**emit.target, filter_bindings, locals, handler_event);
        if (target_type.kind != TypeKind::EntityId && target_type.kind != TypeKind::Unknown) {
            errors_.error(emit.location, "emit target must be of type entity_id, got " + target_type.name);
        }
    };

    auto validate_add = [this, &filter_bindings, &locals, handler_event](const AddTraitStmt& add) {
        const auto* trait = find_resolved_trait(add.resolved_trait_id, add.trait_name);
        if (trait == nullptr) {
            const auto prev_errors = errors_.error_count();
            (void)resolve_trait_ref_to_canonical(add.trait_name, add.location);
            if (errors_.error_count() == prev_errors) {
                errors_.error(add.location, "undeclared trait '" + add.trait_name + "'");
            }
            return;
        }

        if (add.target_expr.has_value()) {
            auto t = infer_expr_type(**add.target_expr, filter_bindings, locals, handler_event);
            if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
                errors_.error(add.location, "`to` target must be of type `entity_id`");
            }
        }

        std::unordered_map<std::string, const ResolvedField*> fields_by_name;
        for (const auto& field : trait->fields) {
            fields_by_name[field.name] = &field;
        }

        std::unordered_set<std::string> supplied;
        for (const auto& arg : add.args) {
            supplied.insert(arg.name);
            auto it = fields_by_name.find(arg.name);
            if (it == fields_by_name.end()) {
                errors_.error(arg.location, "unknown field '" + arg.name + "' in `add " + add.trait_name + "`");
                continue;
            }

            auto actual          = infer_expr_type(*arg.value, filter_bindings, locals, handler_event);
            const auto& expected = it->second->type;
            if (actual.kind != TypeKind::Unknown && expected.kind != TypeKind::Unknown &&
                actual.kind != expected.kind) {
                errors_.error(arg.location,
                              "type mismatch for field '" + arg.name + "' in `add " + add.trait_name + "`");
            }
        }

        for (const auto& field : trait->fields) {
            if (!field.has_default && !supplied.contains(field.name)) {
                errors_.error(add.location,
                              "required field '" + field.name + "' must be supplied in `add " + add.trait_name + "`");
                break;
            }
        }
    };

    auto validate_project = [this, &filter_bindings, &locals, handler_event](const ProjectTraitStmt& project) {
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

        if (project.target_expr.has_value()) {
            auto t = infer_expr_type(**project.target_expr, filter_bindings, locals, handler_event);
            if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
                errors_.error(project.location, "`project ... to` target must be of type `entity_id`");
            }
        }

        std::unordered_map<std::string, const ResolvedField*> fields_by_name;
        for (const auto& field : trait->fields) {
            fields_by_name[field.name] = &field;
        }

        std::unordered_set<std::string> supplied;
        for (const auto& arg : project.args) {
            supplied.insert(arg.name);
            auto it = fields_by_name.find(arg.name);
            if (it == fields_by_name.end()) {
                errors_.error(arg.location, "unknown field '" + arg.name + "' in `project " + project.trait_name + "`");
                continue;
            }

            auto actual          = infer_expr_type(*arg.value, filter_bindings, locals, handler_event);
            const auto& expected = it->second->type;
            if (actual.kind != TypeKind::Unknown && expected.kind != TypeKind::Unknown &&
                actual.kind != expected.kind) {
                errors_.error(arg.location,
                              "type mismatch for field '" + arg.name + "' in `project " + project.trait_name + "`");
            }
        }

        for (const auto& field : trait->fields) {
            if (!field.has_default && !supplied.contains(field.name)) {
                errors_.error(
                    project.location,
                    "required field '" + field.name + "' must be supplied in `project " + project.trait_name + "`");
                break;
            }
        }
    };

    auto validate_remove = [this, &filter_bindings, &locals, handler_event](const RemoveTraitStmt& remove) {
        if (find_resolved_trait(remove.resolved_trait_id, remove.trait_name) == nullptr) {
            const auto prev_errors = errors_.error_count();
            (void)resolve_trait_ref_to_canonical(remove.trait_name, remove.location);
            if (errors_.error_count() == prev_errors) {
                errors_.error(remove.location, "undeclared trait '" + remove.trait_name + "'");
            }
        }
        if (remove.target_expr.has_value()) {
            auto t = infer_expr_type(**remove.target_expr, filter_bindings, locals, handler_event);
            if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
                errors_.error(remove.location, "`from` target must be of type `entity_id`");
            }
        }
    };

    auto validate_destroy = [this, &filter_bindings, &locals, handler_event](const DestroyStmt& destroy) {
        if (!destroy.target_expr.has_value()) {
            return;
        }
        auto t = infer_expr_type(**destroy.target_expr, filter_bindings, locals, handler_event);
        if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
            errors_.error(destroy.location, "`destroy` target must be of type `entity_id`");
        }
    };

    auto in_system_handler = !system_name.empty();

    for (const auto& stmt : stmts) {
        if (const auto* let_stmt = std::get_if<LetStmt>(&stmt->stmt)) {
            locals[let_stmt->name] = infer_expr_type(*let_stmt->value, filter_bindings, locals, handler_event);
            if (const auto* spawn = std::get_if<SpawnExpr>(&let_stmt->value->expr)) {
                validate_spawn_expr(*spawn, let_stmt->location);
            }
            continue;
        }
        if (const auto* assign_stmt = std::get_if<VarAssign>(&stmt->stmt)) {
            if (auto local_it = locals.find(assign_stmt->name); local_it != locals.end() && local_it->second.is_let) {
                errors_.error(assign_stmt->location, "foreach loop variable '" + assign_stmt->name + "' is read-only");
            }
            (void)infer_expr_type(*assign_stmt->value, filter_bindings, locals, handler_event);
            continue;
        }
        if (const auto* expr_stmt = std::get_if<ExprStmt>(&stmt->stmt)) {
            if (const auto* spawn = std::get_if<SpawnExpr>(&expr_stmt->expr->expr)) {
                validate_spawn_expr(*spawn, expr_stmt->location);
            }
            (void)infer_expr_type(*expr_stmt->expr, filter_bindings, locals, handler_event);
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
                *trait_match, filter_bindings, locals, handler_event, system_name, in_system_handler);
            continue;
        }
        if (const auto* if_stmt = std::get_if<IfStmt>(&stmt->stmt)) {
            (void)infer_expr_type(*if_stmt->condition, filter_bindings, locals, handler_event);
            validate_event_stmts(if_stmt->then_body, filter_bindings, locals, handler_event, system_name);
            validate_event_stmts(if_stmt->else_body, filter_bindings, locals, handler_event, system_name);
            continue;
        }
        if (const auto* foreach_stmt = std::get_if<ForeachStmt>(&stmt->stmt)) {
            auto iterable_type = infer_expr_type(*foreach_stmt->iterable, filter_bindings, locals, handler_event);
            if (iterable_type.kind != TypeKind::List && iterable_type.kind != TypeKind::Unknown) {
                errors_.error(foreach_stmt->location, "foreach requires a `list[T]` iterable");
                continue;
            }

            auto loop_locals      = locals;
            TypeInfo element_type = iterable_type.element != nullptr ? *iterable_type.element : make_unknown_type();
            element_type.is_let   = true;
            loop_locals[foreach_stmt->var_name] = std::move(element_type);
            validate_event_stmts(foreach_stmt->body, filter_bindings, loop_locals, handler_event, system_name);
        }
    }
}

void SemanticAnalyzer::validate_trait_match_stmt(
    const TraitMatchStmt& stmt,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event,
    const std::string& system_name,
    bool in_system_handler) {
    if (!in_system_handler) {
        errors_.error(stmt.location, "statement-level `match entity_id` only allowed inside system event handlers");
    }

    auto subject_type = infer_expr_type(*stmt.subject, filter_bindings, local_bindings, handler_event);
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

        validate_event_stmts(arm.body, filter_bindings, arm_locals, handler_event, system_name);
    }

    if (stmt.wildcard.has_value()) {
        validate_event_stmts(stmt.wildcard->body, filter_bindings, local_bindings, handler_event, system_name);
    }
}

// ── Phase 4: Dependency Graph ───────────────────────────────────────────────

void SemanticAnalyzer::build_dependency_graph(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
    // Resolve a filter entry to its canonical trait ID for the dependency graph (task 3.7).
    auto filter_entry_canonical = [this](const FilterEntry& entry) -> std::string {
        auto canonical = resolve_trait_ref_to_canonical(entry.qualified_name, entry.location);
        if (!canonical.empty()) {
            return canonical;
        }
        // Fallback: use simple name (resolution errors already reported elsewhere).
        auto dot = entry.qualified_name.rfind('.');
        return (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
    };

    auto add_dep_from_filter = [&](const auto& sys, SystemDependency& dep) {
        if (!sys.filter.entries.empty()) {
            for (const auto& entry : sys.filter.entries) {
                dep.reads.insert(filter_entry_canonical(entry));
            }
        } else {
            for (const auto& t : sys.filter.trait_names) {
                dep.reads.insert(make_canonical_id(current_module_name_, t));
            }
        }
    };

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            SystemDependency dep;
            dep.system_name = sys->name;  // simple name (task 5.4 will migrate to canonical)
            dep.system_id   = sys->resolved_system_id;
            add_dep_from_filter(*sys, dep);

            for (auto& handler : sys->handlers) {
                collect_system_deps(handler.body, dep);
            }

            result_.dependency_graph.push_back(std::move(dep));
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            SystemDependency dep;
            dep.system_name = sys->name;
            dep.system_id   = sys->resolved_system_id;
            add_dep_from_filter(*sys, dep);
            result_.dependency_graph.push_back(std::move(dep));
        }
    }
}

void SemanticAnalyzer::collect_system_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts, SystemDependency& dep) {
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
                    collect_system_deps(s.body, dep);
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    collect_system_deps(s.then_body, dep);
                    collect_system_deps(s.else_body, dep);
                } else if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                    for (const auto& arm : s.arms) {
                        dep.reads.insert(arm.trait_name);
                        collect_system_deps(arm.body, dep);
                    }
                    if (s.wildcard.has_value()) {
                        collect_system_deps(s.wildcard->body, dep);
                    }
                }
            },
            stmt->stmt);
    }
}

// ── Dynamic ECS: Helpers ────────────────────────────────────────────────────

bool SemanticAnalyzer::is_trait_declared(const std::string& name) const {
    if (trait_names_.contains(name)) {
        return true;
    }
    // Handle qualified names: "qualifier.TraitName"
    auto dot = name.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = name.substr(0, dot);
        auto local_name = name.substr(dot + 1);
        if (trait_names_.contains(local_name) && qualifier == current_module_name_) {
            return true;  // qualifies a local trait
        }
        auto it = imports_.modules.find(qualifier);
        return it != imports_.modules.end() && it->second.traits.contains(local_name);
    }
    if (!imports_.empty()) {
        auto it = imports_.trait_providers.find(name);
        if (it != imports_.trait_providers.end() && find_std_core_provider(imports_, it->second).has_value()) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::imported_symbols_contain_non_template(const ImportedSymbols& symbols, const std::string& name) {
    return symbols.traits.contains(name) || symbols.structs.contains(name) || symbols.enums.contains(name) ||
           symbols.funcs.contains(name) || symbols.events.contains(name);
}

bool SemanticAnalyzer::local_non_template_symbol_exists(const std::string& name) const {
    return trait_names_.contains(name) || entity_names_.contains(name) || struct_names_.contains(name) ||
           enum_names_.contains(name) || event_names_.contains(name) || func_names_.contains(name) ||
           system_names_.contains(name) || const_names_.contains(name) || asset_decl_types_.contains(name) ||
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
        for (const auto& [__, trait] : syms.traits) {
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

const ResolvedStruct* SemanticAnalyzer::find_resolved_event(const std::string& name) const {
    auto it = event_structs_.find(name);
    if (it != event_structs_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ── Task 3.1: Canonical trait ID resolution ────────────────────────────────────

std::string SemanticAnalyzer::resolve_trait_ref_to_canonical(const std::string& ref, const SourceLocation& loc) {
    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);

        // Qualified local reference (e.g., current_module.TraitName).
        if (trait_names_.contains(local_name) && qualifier == current_module_name_) {
            return make_canonical_id(current_module_name_, local_name);
        }

        auto mod_it = imports_.modules.find(qualifier);
        if (mod_it == imports_.modules.end()) {
            errors_.error(loc, "unknown module qualifier '" + qualifier + "' in trait reference");
            return "";
        }
        if (!mod_it->second.traits.contains(local_name)) {
            auto np_it = imports_.non_pub_trait_names.find(qualifier);
            if (np_it != imports_.non_pub_trait_names.end() && np_it->second.contains(local_name)) {
                errors_.error(loc, "trait '" + local_name + "' is not public in module '" + qualifier + "'");
            } else {
                errors_.error(loc, "unknown trait '" + local_name + "' in module '" + qualifier + "'");
            }
            return "";
        }
        return mod_it->second.traits.at(local_name).canonical_id;
    }

    // Unqualified: local trait first.
    if (trait_names_.contains(ref)) {
        return make_canonical_id(current_module_name_, ref);
    }

    // Unqualified: check the std.core prelude only. Ordinary imports must be
    // referenced through their module path or alias.
    if (!imports_.empty()) {
        auto pit = imports_.trait_providers.find(ref);
        if (pit != imports_.trait_providers.end() && !pit->second.empty()) {
            const auto prelude = find_std_core_provider(imports_, pit->second);
            if (!prelude.has_value()) {
                errors_.error(loc, imported_reference_diagnostic(imports_, "trait", ref, pit->second));
                return "";
            }
            const auto& qualifier = *prelude;
            auto mit              = imports_.modules.find(qualifier);
            if (mit != imports_.modules.end()) {
                auto tit = mit->second.traits.find(ref);
                if (tit != mit->second.traits.end()) {
                    return tit->second.canonical_id;
                }
            }
        }
    }

    return "";  // not found; caller reports error as needed
}

std::optional<SymbolId> SemanticAnalyzer::try_resolve_trait_ref_to_symbol(const std::string& ref) const {
    const auto* trait = find_resolved_trait(ref);
    if (trait == nullptr) {
        return std::nullopt;
    }
    return resolved_decl_symbol(*trait, SymbolKind::Trait, current_module_name_, trait->name);
}

// ── Task 3.3: Canonical event ID resolution ─────────────────────────────────────

std::optional<SymbolId> SemanticAnalyzer::try_resolve_event_ref_to_symbol(const std::string& ref) const {
    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);

        // Qualified local reference — only matches a locally declared event.
        if (qualifier == current_module_name_) {
            auto local_it = module_scope_symbols_.find(local_name);
            if (local_it != module_scope_symbols_.end() && local_it->second.kind == SymbolKind::Event) {
                return local_it->second;
            }
            return std::nullopt;
        }

        // Qualified import reference.
        auto mod_it = imports_.modules.find(qualifier);
        if (mod_it != imports_.modules.end()) {
            if (auto ev_it = mod_it->second.event_symbols.find(local_name);
                ev_it != mod_it->second.event_symbols.end() && ev_it->second.symbol_id.has_value()) {
                return ev_it->second.symbol_id;
            }
        }
        return std::nullopt;
    }

    // Unqualified: locally declared event takes priority over prelude.
    // event_names_ includes std.core prelude events, so consult module_scope_symbols_
    // (which only holds locally declared symbols) to correctly distinguish the two.
    {
        auto local_it = module_scope_symbols_.find(ref);
        if (local_it != module_scope_symbols_.end() && local_it->second.kind == SymbolKind::Event) {
            return local_it->second;
        }
    }

    // Unqualified: check std.core prelude only.
    if (!imports_.empty()) {
        auto pit = imports_.event_providers.find(ref);
        if (pit != imports_.event_providers.end() && !pit->second.empty()) {
            if (const auto prelude = find_std_core_provider(imports_, pit->second)) {
                const auto& qualifier = *prelude;
                auto mit              = imports_.modules.find(qualifier);
                if (mit != imports_.modules.end()) {
                    auto eit = mit->second.event_symbols.find(ref);
                    if (eit != mit->second.event_symbols.end() && eit->second.symbol_id.has_value()) {
                        return eit->second.symbol_id;
                    }
                }
            }
        }
    }

    return std::nullopt;
}

// ── Task 3.4: Canonical system ID resolution ────────────────────────────────────

std::optional<SymbolId> SemanticAnalyzer::try_resolve_system_ref_to_symbol(const std::string& ref) const {
    return try_resolve_system_ref_impl(ref, true);
}

std::optional<SymbolId> SemanticAnalyzer::try_resolve_system_ref_impl(const std::string& ref, bool allow_local) const {
    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);

        if (allow_local && system_names_.contains(local_name) && qualifier == current_module_name_) {
            return make_symbol_id(SymbolKind::System, current_module_id_, local_name);
        }

        auto mod_it = imports_.modules.find(qualifier);
        if (mod_it != imports_.modules.end()) {
            if (auto sys_it = mod_it->second.systems.find(local_name);
                sys_it != mod_it->second.systems.end() && sys_it->second.symbol_id.has_value()) {
                return sys_it->second.symbol_id;
            }
        }
        return std::nullopt;
    }

    if (allow_local && system_names_.contains(ref)) {
        return make_symbol_id(SymbolKind::System, current_module_id_, ref);
    }

    if (!imports_.empty()) {
        auto pit = imports_.system_providers.find(ref);
        if (pit != imports_.system_providers.end() && !pit->second.empty()) {
            if (const auto prelude = find_std_core_provider(imports_, pit->second)) {
                const auto& qualifier = *prelude;
                auto mit              = imports_.modules.find(qualifier);
                if (mit != imports_.modules.end()) {
                    auto sit = mit->second.systems.find(ref);
                    if (sit != mit->second.systems.end() && sit->second.symbol_id.has_value()) {
                        return sit->second.symbol_id;
                    }
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<SymbolId> SemanticAnalyzer::resolve_system_after_ref_to_symbol(
    const std::string& ref,
    const SourceLocation& /*loc*/,
    const std::unordered_set<std::string>& local_system_names) const {
    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);

        if (local_system_names.contains(local_name) && qualifier == current_module_name_) {
            return make_symbol_id(SymbolKind::System, current_module_id_, local_name);
        }

        auto mod_it = imports_.modules.find(qualifier);
        if (mod_it != imports_.modules.end()) {
            if (auto sys_it = mod_it->second.systems.find(local_name);
                sys_it != mod_it->second.systems.end() && sys_it->second.symbol_id.has_value()) {
                return sys_it->second.symbol_id;
            }
        }
        return std::nullopt;
    }

    if (local_system_names.contains(ref)) {
        return make_symbol_id(SymbolKind::System, current_module_id_, ref);
    }

    if (!imports_.empty()) {
        auto pit = imports_.system_providers.find(ref);
        if (pit != imports_.system_providers.end() && !pit->second.empty()) {
            if (const auto prelude = find_std_core_provider(imports_, pit->second)) {
                const auto& qualifier = *prelude;
                auto mit              = imports_.modules.find(qualifier);
                if (mit != imports_.modules.end()) {
                    auto sit = mit->second.systems.find(ref);
                    if (sit != mit->second.systems.end() && sit->second.symbol_id.has_value()) {
                        return sit->second.symbol_id;
                    }
                }
            }
        }
    }

    return std::nullopt;
}

// ── Task 3.5: Canonical template/entity ID resolution ───────────────────────────

std::optional<SymbolId> SemanticAnalyzer::try_resolve_template_ref_to_symbol(const std::string& ref) const {
    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);

        if (template_names_.contains(local_name) && qualifier == current_module_name_) {
            return make_symbol_id(SymbolKind::Template, current_module_id_, local_name);
        }
        if (entity_names_.contains(local_name) && qualifier == current_module_name_) {
            return make_symbol_id(SymbolKind::Entity, current_module_id_, local_name);
        }

        auto mod_it = imports_.modules.find(qualifier);
        if (mod_it != imports_.modules.end()) {
            auto tmpl_it = mod_it->second.templates.find(local_name);
            if (tmpl_it != mod_it->second.templates.end() && tmpl_it->second.symbol_id.has_value()) {
                return tmpl_it->second.symbol_id;
            }
        }
        return std::nullopt;
    }

    if (template_names_.contains(ref)) {
        return make_symbol_id(SymbolKind::Template, current_module_id_, ref);
    }
    if (entity_names_.contains(ref)) {
        return make_symbol_id(SymbolKind::Entity, current_module_id_, ref);
    }

    if (!imports_.empty()) {
        auto pit = imports_.template_providers.find(ref);
        if (pit != imports_.template_providers.end() && !pit->second.empty()) {
            if (const auto prelude = find_std_core_provider(imports_, pit->second)) {
                const auto& qualifier = *prelude;
                auto mit              = imports_.modules.find(qualifier);
                if (mit != imports_.modules.end()) {
                    auto tit = mit->second.templates.find(ref);
                    if (tit != mit->second.templates.end() && tit->second.symbol_id.has_value()) {
                        return tit->second.symbol_id;
                    }
                }
            }
        }
    }

    return std::nullopt;
}

// ── Task 3.6: Canonical func ID resolution ──────────────────────────────────────

std::optional<SymbolId> SemanticAnalyzer::try_resolve_func_ref_to_symbol(const std::string& ref) const {
    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);

        if (func_names_.contains(local_name) && qualifier == current_module_name_) {
            return make_symbol_id(SymbolKind::Func, current_module_id_, local_name);
        }

        auto mod_it = imports_.modules.find(qualifier);
        if (mod_it != imports_.modules.end()) {
            auto func_it = mod_it->second.funcs.find(local_name);
            if (func_it != mod_it->second.funcs.end()) {
                if (func_it->second.symbol_id.has_value()) {
                    return func_it->second.symbol_id;
                }
                if (!func_it->second.module_name.empty()) {
                    return make_symbol_id(SymbolKind::Func,
                                         ModuleId{.name = func_it->second.module_name},
                                         local_name);
                }
            }
        }
        return std::nullopt;
    }

    if (func_names_.contains(ref)) {
        return make_symbol_id(SymbolKind::Func, current_module_id_, ref);
    }

    if (!imports_.empty()) {
        auto pit = imports_.func_providers.find(ref);
        if (pit != imports_.func_providers.end() && !pit->second.empty()) {
            if (const auto prelude = find_std_core_provider(imports_, pit->second)) {
                const auto& qualifier = *prelude;
                auto mit              = imports_.modules.find(qualifier);
                if (mit != imports_.modules.end()) {
                    auto fit = mit->second.funcs.find(ref);
                    if (fit != mit->second.funcs.end() && fit->second.symbol_id.has_value()) {
                        return fit->second.symbol_id;
                    }
                }
            }
        }
    }

    return std::nullopt;
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

std::optional<SymbolId> SemanticAnalyzer::lookup_imported_symbol(const ImportedSymbols& syms,
                                                                 const std::string& name) {
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
    if (auto sys_it = syms.systems.find(name); sys_it != syms.systems.end()) {
        return sys_it->second.symbol_id.value_or(make_symbol_id(SymbolKind::System, syms.module_name, name));
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
        errors_.error(loc,
                      "enum member '" + resolved->member_segments[0] + "' of '" + canonical + "' has no members");
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
                              "input property '" + prop.key + "' requires a " + expected_canonical +
                                  " member, got '" + make_canonical_id(rem.enum_id) + "." + rem.member + "'");
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
                          "input property '" + prop.key + "' requires a " + expected_canonical +
                              " member, got '" + make_canonical_id(resolved->symbol) + "'");
            continue;
        }
        (void)resolve_name_required(*segments, prop.location);
    }
}

// ── Task 3.4: Canonical system ID resolution for after: clauses ─────────────────

std::string SemanticAnalyzer::resolve_system_after_ref(const std::string& ref,
                                                       const SourceLocation& loc,
                                                       const std::unordered_set<std::string>& local_system_names) {
    auto dot = ref.rfind('.');
    if (dot != std::string::npos) {
        auto qualifier  = ref.substr(0, dot);
        auto local_name = ref.substr(dot + 1);

        // Qualified local system (e.g., current_module.SystemName).
        if (local_system_names.contains(local_name) && qualifier == current_module_name_) {
            return make_canonical_id(current_module_name_, local_name);
        }

        auto mod_it = imports_.modules.find(qualifier);
        if (mod_it == imports_.modules.end()) {
            errors_.error(loc, "unknown module qualifier '" + qualifier + "' in 'after:' clause");
            return "";
        }
        if (!mod_it->second.systems.contains(local_name)) {
            errors_.error(loc, "unknown system '" + local_name + "' in module '" + qualifier + "' in 'after:' clause");
            return "";
        }
        return make_canonical_id(mod_it->second.module_name, local_name);
    }

    // Unqualified: local system first.
    if (local_system_names.contains(ref)) {
        return make_canonical_id(current_module_name_, ref);
    }

    // Unqualified: check the std.core prelude only. Ordinary imports must be
    // referenced through their module path or alias.
    if (!imports_.empty()) {
        auto pit = imports_.system_providers.find(ref);
        if (pit != imports_.system_providers.end() && !pit->second.empty()) {
            const auto prelude = find_std_core_provider(imports_, pit->second);
            if (!prelude.has_value()) {
                errors_.error(loc, imported_reference_diagnostic(imports_, "system", ref, pit->second));
                return "";
            }
            const auto& qualifier = *prelude;
            auto mit              = imports_.modules.find(qualifier);
            if (mit != imports_.modules.end() && mit->second.systems.contains(ref)) {
                return make_canonical_id(mit->second.module_name, ref);
            }
        }
    }

    return "";  // not found
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
    return name == "std.query" || name == "std.physics.flat.query" || name == "std.physics.volume.query";
}

// Whether a known query module actually provides the named function; the
// codegen only lowers these pairings (everything else becomes broken C++).
static bool query_module_provides(const std::string& module_name, const std::string& func_name) {
    if (module_name == "std.query") {
        return func_name == "exists" || func_name == "count" || func_name == "first" || func_name == "all" ||
               func_name == "parent";
    }
    if (module_name == "std.physics.flat.query") {
        return func_name == "nearest" || func_name == "overlap_box" || func_name == "overlap_circle" ||
               func_name == "raycast";
    }
    if (module_name == "std.physics.volume.query") {
        return func_name == "nearest" || func_name == "overlap_box" || func_name == "overlap_sphere" ||
               func_name == "raycast";
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
    if (func_name == "parent") {
        if (!has_arg("of")) {
            errors_.error(qcall.location, "`parent` requires an `of` named argument");
        } else {
            auto of_it = std::ranges::find_if(qcall.named_args, [](const auto& a) { return a.name == "of"; });
            if (of_it != qcall.named_args.end()) {
                auto of_type = infer_expr_type(*of_it->value, filter_bindings, local_bindings, handler_event);
                if (of_type.kind != TypeKind::EntityId && of_type.kind != TypeKind::Unknown) {
                    errors_.error(of_it->location, "`parent` `of` argument must be of type `entity_id`");
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
                    validate_text_format_in_stmts(s.else_body, filter_bindings, locals, handler_event);
                } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                    validate_text_format_in_expr(*s.iterable, filter_bindings, locals, handler_event);
                    validate_text_format_in_stmts(s.body, filter_bindings, locals, handler_event);
                }
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_text_format_calls(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, FuncNode>) {
                    validate_text_format_in_stmts(node.body, {}, {}, nullptr);
                } else if constexpr (std::is_same_v<T, SystemNode>) {
                    for (auto& handler : node.handlers) {
                        std::unordered_map<std::string, const ResolvedTrait*> filter_bindings;
                        for (const auto& entry : node.filter.entries) {
                            auto dot       = entry.qualified_name.rfind('.');
                            auto simple    = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1)
                                                                        : entry.qualified_name;
                            const auto* tr = find_resolved_trait(simple);
                            if (tr != nullptr) {
                                filter_bindings[simple] = tr;
                                if (entry.alias.has_value()) {
                                    filter_bindings[*entry.alias] = tr;
                                }
                            }
                        }
                        for (const auto& trait_name : node.filter.trait_names) {
                            const auto* tr = find_resolved_trait(trait_name);
                            if (tr != nullptr) {
                                filter_bindings[trait_name] = tr;
                            }
                        }

                        const ResolvedStruct* handler_event = find_resolved_event(handler.event_name);
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
TypeInfo SemanticAnalyzer::infer_expr_type(const ExprNode& expr,
                                           const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                           const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                           const ResolvedStruct* handler_event) const {
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
        if (auto local_it = local_bindings.find(ident->name); local_it != local_bindings.end()) {
            return local_it->second;
        }
        const ResolvedField* matching_filter_field = nullptr;
        std::unordered_set<const ResolvedTrait*> visited_traits;
        for (const auto& [_, trait] : filter_bindings) {
            if (trait == nullptr || visited_traits.contains(trait)) {
                continue;
            }
            visited_traits.insert(trait);
            for (const auto& field : trait->fields) {
                if (field.name != ident->name) {
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
        if (auto asset_it = asset_decl_types_.find(ident->name); asset_it != asset_decl_types_.end()) {
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
        if (auto input_it = input_decl_types_.find(ident->name); input_it != input_decl_types_.end()) {
            return input_it->second == TypeKind::InputAxis ? make_input_axis_type() : make_input_button_type();
        }
        if (entity_names_.contains(ident->name)) {
            errors_.error(expr.location, "entity '" + ident->name + "' is not an entity_id expression");
        }
        return make_unknown_type();
    }

    if (std::holds_alternative<SelfExpr>(expr.expr)) {
        if (handler_event == nullptr && !local_bindings.contains("__self_context")) {
            errors_.error(expr.location, "`self` only allowed inside system event handlers");
            return make_unknown_type();
        }
        return make_entity_id_type();
    }

    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        const auto* owner = std::get_if<IdentExpr>(&member->object->expr);
        if (owner == nullptr) {
            return make_unknown_type();
        }
        if (auto trait_it = filter_bindings.find(owner->name);
            trait_it != filter_bindings.end() && trait_it->second != nullptr) {
            return find_field_type_in(trait_it->second->fields, member->member);
        }
        if (handler_event != nullptr && owner->name == handler_event->name) {
            return find_field_type_in(handler_event->fields, member->member);
        }
        if (auto local_it = local_bindings.find(owner->name);
            local_it != local_bindings.end() && local_it->second.kind == TypeKind::Struct) {
            const auto& local_type = local_it->second;
            if (local_type.symbol_id.has_value()) {
                const auto& symbol = *local_type.symbol_id;
                if (symbol.kind == SymbolKind::Struct) {
                    if (symbol.module.name == current_module_name_) {
                        if (auto struct_it = result_.structs.find(symbol.local_name);
                            struct_it != result_.structs.end()) {
                            return find_field_type_in(struct_it->second.fields, member->member);
                        }
                    }
                    for (const auto& [_, syms] : imports_.modules) {
                        if (syms.module_name != symbol.module.name) {
                            continue;
                        }
                        if (auto struct_it = syms.structs.find(symbol.local_name); struct_it != syms.structs.end()) {
                            return find_field_type_in(struct_it->second.fields, member->member);
                        }
                    }
                } else if (symbol.kind == SymbolKind::Event) {
                    if (auto event_it = event_structs_.find(symbol.local_name); event_it != event_structs_.end()) {
                        return find_field_type_in(event_it->second.fields, member->member);
                    }
                } else if (symbol.kind == SymbolKind::Trait) {
                    if (const auto* trait = find_resolved_trait(make_canonical_id(symbol)); trait != nullptr) {
                        return find_field_type_in(trait->fields, member->member);
                    }
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
                        return find_field_type_in(struct_it->second.fields, member->member);
                    }
                }
                if (auto mod_it = imports_.modules.find(qualifier); mod_it != imports_.modules.end()) {
                    if (auto s_it = mod_it->second.structs.find(local_name); s_it != mod_it->second.structs.end()) {
                        return find_field_type_in(s_it->second.fields, member->member);
                    }
                }
                for (const auto& [_, syms] : imports_.modules) {
                    if (syms.module_name != qualifier) {
                        continue;
                    }
                    if (auto s_it = syms.structs.find(local_name); s_it != syms.structs.end()) {
                        return find_field_type_in(s_it->second.fields, member->member);
                    }
                }
            }
            if (auto struct_it = result_.structs.find(type_name); struct_it != result_.structs.end()) {
                return find_field_type_in(struct_it->second.fields, member->member);
            }
            if (auto event_it = event_structs_.find(type_name); event_it != event_structs_.end()) {
                return find_field_type_in(event_it->second.fields, member->member);
            }
        }
        return make_unknown_type();
    }

    if (std::holds_alternative<SpawnExpr>(expr.expr)) {
        return make_entity_id_type();
    }
    if (const auto* qcall = std::get_if<QueryCallExpr>(&expr.expr)) {
        if (handler_event == nullptr) {
            errors_.error(expr.location,
                          "query expressions require world access; only allowed inside system event handlers");
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
        if (func_name == "count") {
            return make_int_type();
        }
        if (func_name == "first" || func_name == "nearest" || func_name == "parent" || func_name == "raycast") {
            return make_entity_id_type();
        }
        if (func_name == "all" || func_name == "overlap_box" || func_name == "overlap_circle" ||
            func_name == "overlap_sphere") {
            return make_list_type(make_entity_id_type());
        }
        return make_unknown_type();
    }
    if (const auto* call = std::get_if<CallExpr>(&expr.expr)) {
        if (is_std_text_format_callee(*call->callee)) {
            return make_string_type();
        }
        if (auto* ident = std::get_if<IdentExpr>(&call->callee->expr); ident != nullptr && ident->name == "exists") {
            if (handler_event == nullptr) {
                errors_.error(expr.location,
                              "`exists()` requires world access; only allowed inside system event handlers");
            }
            if (call->args.size() != 1) {
                return make_bool_type();
            }
            auto arg_type = infer_expr_type(*call->args.front(), filter_bindings, local_bindings, handler_event);
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
        return make_unknown_type();
    }
    if (const auto* unary = std::get_if<UnaryExpr>(&expr.expr)) {
        return infer_expr_type(*unary->operand, filter_bindings, local_bindings, handler_event);
    }
    if (const auto* binary = std::get_if<BinaryExpr>(&expr.expr)) {
        auto left  = infer_expr_type(*binary->left, filter_bindings, local_bindings, handler_event);
        auto right = infer_expr_type(*binary->right, filter_bindings, local_bindings, handler_event);
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
        if (left.kind == TypeKind::Float || right.kind == TypeKind::Float) {
            return make_float_type();
        }
        if (left.kind == TypeKind::Int && right.kind == TypeKind::Int) {
            return make_int_type();
        }
        return left;
    }
    if (const auto* if_expr = std::get_if<IfExpr>(&expr.expr)) {
        return infer_expr_type(*if_expr->then_expr, filter_bindings, local_bindings, handler_event);
    }
    if (const auto* list = std::get_if<ListExpr>(&expr.expr)) {
        if (list->elements.empty()) {
            return make_list_type(make_unknown_type());
        }
        return make_list_type(infer_expr_type(*list->elements.front(), filter_bindings, local_bindings, handler_event));
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

// ── Task 5.1, 5.2: Validate template and unit declarations ──────────────────

void SemanticAnalyzer::validate_template_unit_declarations(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
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
                    for (auto& entry : node.traits) {
                        const auto* trait = find_resolved_trait(entry.trait_name);
                        if (trait == nullptr) {
                            continue;
                        }

                        std::unordered_set<std::string> trait_fields;
                        for (const auto& f : trait->fields) {
                            trait_fields.insert(f.name);
                        }

                        for (auto& assign : entry.assignments) {
                            if (!trait_fields.contains(assign.name)) {
                                errors_.error(assign.location,
                                              "unknown field '" + assign.name + "' for trait '" + entry.trait_name +
                                                  "' in " + KIND + " '" + node.name + "'");
                            }
                            if (expr_contains_self(*assign.value)) {
                                errors_.error(assign.location, "`self` only allowed inside system event handlers");
                            }
                        }
                    }

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
    const auto dot = tmpl_ref.rfind('.');
    if (dot != std::string::npos) {
        const auto qualifier     = tmpl_ref.substr(0, dot);
        const auto template_name = tmpl_ref.substr(dot + 1);
        auto module_it           = imports_.modules.find(qualifier);
        if (module_it == imports_.modules.end()) {
            errors_.error(location, "unknown module qualifier '" + qualifier + "' in 'from' clause of " + owner_desc);
        } else if (!module_it->second.templates.contains(template_name)) {
            auto non_pub_it = imports_.non_pub_template_names.find(qualifier);
            if (non_pub_it != imports_.non_pub_template_names.end() && non_pub_it->second.contains(template_name)) {
                errors_.error(location, "template '" + template_name + "' is not public in module '" + qualifier + "'");
            } else if (imported_symbols_contain_non_template(module_it->second, template_name)) {
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
void SemanticAnalyzer::validate_child_archetypes(  // NOLINT(readability-function-cognitive-complexity)
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

            for (const auto& entry : child.traits) {
                const auto* trait = find_resolved_trait(entry.trait_name);
                if (trait == nullptr) {
                    continue;
                }
                std::unordered_set<std::string> trait_fields;
                for (const auto& field : trait->fields) {
                    trait_fields.insert(field.name);
                }
                for (const auto& assign : entry.assignments) {
                    if (!trait_fields.contains(assign.name)) {
                        errors_.error(assign.location,
                                      "unknown field '" + assign.name + "' for trait '" + entry.trait_name +
                                          "' in child '" + child.role + "'");
                    }
                    if (expr_contains_self(*assign.value)) {
                        errors_.error(assign.location, "`self` only allowed inside system event handlers");
                    }
                }
            }
        }

        validate_child_archetypes(child.children, archetype_kind, archetype_name);
    }
}

// Validate a nested child override tree against a flattened child list:
// unknown roles, traits not present on the child, unknown fields, and `self`
// usage outside handlers (declaration sites only).
void SemanticAnalyzer::validate_child_override_tree(  // NOLINT(readability-function-cognitive-complexity)
    const std::vector<ChildOverrideNode>& overrides,
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

        for (const auto& entry : override_node.traits) {
            if (!child_trait_names.contains(entry.trait_name)) {
                errors_.error(entry.location,
                              "trait '" + entry.trait_name + "' is not part of child '" + override_node.role + "' in " +
                                  site_desc + "; cannot override it");
                continue;
            }
            const auto* trait = find_resolved_trait(entry.trait_name);
            if (trait == nullptr) {
                continue;
            }
            std::unordered_set<std::string> trait_fields;
            for (const auto& field : trait->fields) {
                trait_fields.insert(field.name);
            }
            for (const auto& assign : entry.assignments) {
                if (!trait_fields.contains(assign.name)) {
                    errors_.error(assign.location,
                                  "unknown field '" + assign.name + "' for trait '" + entry.trait_name +
                                      "' in child override '" + override_node.role + "'");
                }
                if (!allow_self && expr_contains_self(*assign.value)) {
                    errors_.error(assign.location, "`self` only allowed inside system event handlers");
                }
            }
        }

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
            if (target.find('.') != std::string::npos) {
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
        if (use.template_name.find('.') != std::string::npos) {
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

void SemanticAnalyzer::validate_template_backed_entity_overrides(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
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
        for (auto& entry : entity->traits) {
            if (!tmpl_trait_names.contains(entry.trait_name)) {
                errors_.error(entry.location,
                              "trait '" + entry.trait_name + "' is not part of template '" + tmpl_ref +
                                  "'; cannot override it in entity '" + entity->name + "'");
                continue;
            }

            const auto* trait = find_resolved_trait(entry.trait_name);
            if (trait == nullptr) {
                continue;
            }

            std::unordered_set<std::string> trait_fields;
            for (const auto& f : trait->fields) {
                trait_fields.insert(f.name);
            }

            for (auto& assign : entry.assignments) {
                if (!trait_fields.contains(assign.name)) {
                    errors_.error(assign.location,
                                  "unknown field '" + assign.name + "' for trait '" + entry.trait_name +
                                      "' in entity '" + entity->name + "'");
                }
                if (expr_contains_self(*assign.value)) {
                    errors_.error(assign.location, "`self` only allowed inside system event handlers");
                }
            }
        }

        // Check required fields are satisfied by template defaults or entity overrides
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

void SemanticAnalyzer::validate_spawn_stmts(  // NOLINT(readability-function-cognitive-complexity)
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::string& context_name) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &context_name](auto& s) {  // NOLINT(readability-function-cognitive-complexity)
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
                        for (auto& override : s.overrides) {
                            const auto* trait = find_resolved_trait(override.trait_name);
                            if (!trait) {
                                errors_.error(override.location,
                                              "undeclared trait '" + override.trait_name + "' in spawn override");
                                continue;
                            }

                            // Validate field assignments belong to the trait
                            std::unordered_set<std::string> trait_fields;
                            for (const auto& f : trait->fields) {
                                trait_fields.insert(f.name);
                            }

                            for (auto& assign : override.assignments) {
                                if (!trait_fields.contains(assign.name)) {
                                    errors_.error(assign.location,
                                                  "unknown field '" + assign.name + "' for trait '" +
                                                      override.trait_name + "' in spawn override");
                                }
                            }
                        }

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

    std::unordered_set<std::string> provided;
    for (const auto& override_entry : spawn.overrides) {
        const auto* trait = find_resolved_trait(override_entry.trait_name);
        if (trait == nullptr) {
            errors_.error(override_entry.location,
                          "undeclared trait '" + override_entry.trait_name + "' in spawn override");
            continue;
        }

        std::unordered_set<std::string> trait_fields;
        for (const auto& field : trait->fields) {
            trait_fields.insert(field.name);
        }

        for (const auto& assign : override_entry.assignments) {
            provided.insert(assign.name);
            if (!trait_fields.contains(assign.name)) {
                errors_.error(assign.location,
                              "unknown field '" + assign.name + "' for trait '" + override_entry.trait_name +
                                  "' in spawn override");
            }
        }
    }

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
                    validate_spawn_exprs(s.else_body, context_name);
                }
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_spawn_sites(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            for (auto& handler : sys->handlers) {
                validate_spawn_stmts(handler.body, sys->name);
                validate_spawn_exprs(handler.body, sys->name);
            }
        }
    }
}

// ── Task 5.5, 5.6, 5.7: Statement context validation ────────────────────────

void SemanticAnalyzer::validate_context_stmts(  // NOLINT(readability-function-cognitive-complexity)
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::string& context_name,
    bool in_system_handler) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &context_name, in_system_handler](auto& s) {  // NOLINT(readability-function-cognitive-complexity)
                using S = std::decay_t<decltype(s)>;
                std::unordered_map<std::string, TypeInfo> self_context_locals;
                if (in_system_handler) {
                    self_context_locals["__self_context"] = make_entity_id_type();
                }
                auto validate_self_expr = [this, in_system_handler](const ExprNode& expr,
                                                                    const SourceLocation& location) {
                    if (!in_system_handler && expr_contains_self(expr)) {
                        errors_.error(location, "`self` only allowed inside system event handlers");
                    }
                };
                if constexpr (std::is_same_v<S, SpawnStmt> || std::is_same_v<S, DestroyStmt> ||
                              std::is_same_v<S, LoadStmt> || std::is_same_v<S, AddTraitStmt> ||
                              std::is_same_v<S, RemoveTraitStmt> || std::is_same_v<S, ProjectTraitStmt> ||
                              std::is_same_v<S, ForeachStmt> || std::is_same_v<S, TraitMatchStmt>) {
                    if (!in_system_handler) {
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
                        errors_.error(s.location, "`" + kw + "` only allowed inside system event handlers");
                    }
                    // 5.6: For LoadStmt, validate module name is reachable via `use`
                    if constexpr (std::is_same_v<S, LoadStmt>) {
                        if (in_system_handler && !use_names_.contains(s.module_name)) {
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
                            if (s.target_expr.has_value()) {
                                auto t = infer_expr_type(**s.target_expr, {}, self_context_locals, nullptr);
                                if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
                                    errors_.error(s.location, "`to` target must be of type `entity_id`");
                                }
                            }
                        } else {
                            if (s.target_expr.has_value()) {
                                auto t = infer_expr_type(**s.target_expr, {}, self_context_locals, nullptr);
                                if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
                                    errors_.error(s.location, "`from` target must be of type `entity_id`");
                                }
                            }
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
                        if (s.target_expr.has_value()) {
                            auto t = infer_expr_type(**s.target_expr, {}, self_context_locals, nullptr);
                            if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
                                errors_.error(s.location, "`project ... to` target must be of type `entity_id`");
                            }
                        }
                        for (const auto& arg : s.args) {
                            validate_self_expr(*arg.value, arg.location);
                        }
                    }
                    if constexpr (std::is_same_v<S, ForeachStmt>) {
                        validate_self_expr(*s.iterable, s.location);
                        validate_context_stmts(s.body, context_name, in_system_handler);
                    }
                    if constexpr (std::is_same_v<S, DestroyStmt>) {
                        if (s.target_expr.has_value()) {
                            auto t = infer_expr_type(**s.target_expr, {}, self_context_locals, nullptr);
                            if (t.kind != TypeKind::EntityId && t.kind != TypeKind::Unknown) {
                                errors_.error(s.location, "`destroy` target must be of type `entity_id`");
                            }
                        }
                    }
                    if constexpr (std::is_same_v<S, TraitMatchStmt>) {
                        for (const auto& arm : s.arms) {
                            validate_context_stmts(arm.body, context_name, in_system_handler);
                        }
                        if (s.wildcard.has_value()) {
                            validate_context_stmts(s.wildcard->body, context_name, in_system_handler);
                        }
                    }
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    validate_context_stmts(s.then_body, context_name, in_system_handler);
                    validate_context_stmts(s.else_body, context_name, in_system_handler);
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
                } else if constexpr (std::is_same_v<T, SystemNode>) {
                    // System handlers: these statements are valid
                    for (auto& handler : node.handlers) {
                        validate_context_stmts(handler.body, node.name, true);
                    }
                } else if constexpr (std::is_same_v<T, ExternSystemNode>) {
                    if (node.filter.entries.empty() && node.filter.trait_names.empty()) {
                        errors_.error(node.location,
                                      "`extern system` requires a `filter:` clause (no-filter extern systems are "
                                      "not supported)");
                    }
                }
            },
            decl);
    }
}

// ── Task 5.9: Validate exclude clause trait names ────────────────────────────
// (called as part of validate_system_filters — integrated inline above)
// Note: exclude clause validation is done here as a separate pass for clarity.

// ── Task 11.12: Check no field access in no-filter system bodies ─────────────

void SemanticAnalyzer::check_no_field_access(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                             const std::string& sys_name) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &sys_name](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, LetStmt>) {
                    // local binding is allowed without filter access checks
                } else if constexpr (std::is_same_v<S, VarAssign>) {
                    // All VarAssign statements in system handlers are trait-field accesses
                    errors_.error(s.location,
                                  "trait field '" + s.name + "' is not accessible in system '" + sys_name +
                                      "': no filter clause declares this trait");
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    check_no_field_access(s.then_body, sys_name);
                    check_no_field_access(s.else_body, sys_name);
                }
                // emit, spawn, destroy, load, add, remove, return, expr: all allowed
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_trait_modifier_rules(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)

    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, SystemNode> || std::is_same_v<T, ExternSystemNode>) {
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

void SemanticAnalyzer::validate_after_clauses(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
    // Collect local system names and build canonical ID map (task 3.4 / 3.7).
    std::unordered_set<std::string> local_system_names;
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            local_system_names.insert(sys->name);
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            local_system_names.insert(sys->name);
        }
    }

    // Resolve each system's after_systems to canonical IDs and validate.
    // after_resolved: local_simple_name → list of canonical system IDs it runs after.
    std::unordered_map<std::string, std::vector<std::string>> after_resolved;
    for (auto& decl : program.declarations) {
        auto resolve_after = [&](const auto& sys) {
            const auto self_canonical = make_canonical_id(current_module_name_, sys.name);
            for (const auto& after_ref : sys.after_systems) {
                auto canonical = resolve_system_after_ref(after_ref, sys.location, local_system_names);
                if (canonical.empty()) {
                    errors_.error(sys.location, "unknown system '" + after_ref + "' in after clause");
                } else if (canonical == self_canonical) {
                    errors_.error(sys.location, "system '" + sys.name + "' cannot list itself in after:");
                } else {
                    after_resolved[sys.name].push_back(canonical);
                }
            }
        };
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            resolve_after(*sys);
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            resolve_after(*sys);
        }
    }

    // Build adjacency graph for cycle detection using canonical IDs.
    std::unordered_map<std::string, std::vector<std::string>> after_graph;
    for (auto& decl : program.declarations) {
        auto add_to_graph = [&](const auto& sys) {
            const auto canonical = make_canonical_id(current_module_name_, sys.name);
            after_graph[canonical] =
                after_resolved.count(sys.name) ? after_resolved.at(sys.name) : std::vector<std::string>{};
        };
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            add_to_graph(*sys);
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            add_to_graph(*sys);
        }
    }

    // Build a set of valid canonical IDs for cycle-skip guard.
    std::unordered_set<std::string> all_canonical;
    for (const auto& [canonical, _] : after_graph) {
        all_canonical.insert(canonical);
    }

    // DFS cycle detection on canonical IDs.
    enum class Color : uint8_t { White, Gray, Black };
    std::unordered_map<std::string, Color> color;
    for (auto& [name, _] : after_graph) {
        color[name] = Color::White;
    }

    std::function<bool(const std::string&, std::vector<std::string>&)> dfs =
        [&](const std::string& node,
            std::vector<std::string>& path) -> bool {  // NOLINT(readability-function-cognitive-complexity)
        color[node] = Color::Gray;
        path.push_back(node);
        auto it = after_graph.find(node);
        if (it != after_graph.end()) {
            for (const auto& neighbor : it->second) {
                if (!all_canonical.contains(neighbor)) {
                    continue;  // imported system — no local node, skip cycle check
                }
                if (color.count(neighbor) == 0) {
                    color[neighbor] = Color::White;
                }
                if (color[neighbor] == Color::Gray) {
                    std::string cycle_path;
                    bool in_cycle = false;
                    for (const auto& p : path) {
                        if (p == neighbor) {
                            in_cycle = true;
                        }
                        if (in_cycle) {
                            if (!cycle_path.empty()) {
                                cycle_path += " -> ";
                            }
                            cycle_path += p;
                        }
                    }
                    cycle_path += " -> " + neighbor;
                    errors_.error({}, "cycle in system ordering: " + cycle_path);
                    color[node] = Color::Black;
                    path.pop_back();
                    return true;
                }
                if (color[neighbor] == Color::White) {
                    if (dfs(neighbor, path)) {
                        color[node] = Color::Black;
                        path.pop_back();
                        return true;
                    }
                }
            }
        }
        color[node] = Color::Black;
        path.pop_back();
        return false;
    };

    for (auto& [name, _] : after_graph) {
        if (color[name] == Color::White) {
            std::vector<std::string> path;
            dfs(name, path);
        }
    }

    // Populate after_systems in dependency graph with canonical IDs (task 3.7).
    for (auto& dep : result_.dependency_graph) {
        // dep.system_name is the simple name; find canonical after entries via after_resolved.
        auto it = after_resolved.find(dep.system_name);
        if (it != after_resolved.end()) {
            dep.after_systems = it->second;
        }
    }
}

}  // namespace cactus
