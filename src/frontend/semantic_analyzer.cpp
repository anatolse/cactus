#include "frontend/semantic_analyzer.hpp"

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

bool program_is_stdlib_module(const ProgramNode& program) {
    for (const auto& decl : program.declarations) {
        if (const auto* module = std::get_if<ModuleNode>(&decl)) {
            return module_name_is_stdlib(module->name);
        }
    }
    return false;
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
                MemberExpr copy{.object = clone_expr(*e.object), .member = e.member, .location = e.location};
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
    copy.trait_name = entry.trait_name;
    copy.location   = entry.location;
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
    copy.role         = node.role;
    copy.template_ref = node.template_ref;
    copy.location     = node.location;
    copy.body_entries = node.body_entries;
    for (const auto& use : node.template_uses) {
        copy.template_uses.push_back(use);
    }
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
void merge_trait_entry_into(std::vector<ArchetypeTraitEntry>& merged, const ArchetypeTraitEntry& entry) {
    auto existing = std::ranges::find_if(
        merged, [&entry](const auto& candidate) { return candidate.trait_name == entry.trait_name; });
    if (existing == merged.end()) {
        merged.push_back(clone_archetype_trait_entry(entry));
        return;
    }

    for (const auto& assignment : entry.assignments) {
        auto field = std::ranges::find_if(existing->assignments, [&assignment](const auto& candidate) {
            return candidate.name == assignment.name;
        });
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

}  // namespace

// ── ModuleImports::add ──────────────────────────────────────────────────────

void ModuleImports::add(const std::string& qualifier,
                        ImportedSymbols pub_syms,
                        std::unordered_set<std::string> non_pub,
                        std::unordered_set<std::string> non_pub_templates) {
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
    for (const auto& name : pub_syms.templates) {
        template_providers[name].push_back(qualifier);
    }
    // Index imported event names (e.g. from std.core)
    for (const auto& ev_name : pub_syms.events) {
        (void)ev_name;  // no uniqueness conflict for events — just populate event_names_ in analyzer
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
    imports_                  = imports;
    current_module_is_stdlib_ = program_is_stdlib_module(program);
    result_.ast               = &program;

    // Phase 1: Collect all type declarations
    collect_types(program);

    // Phase 2: Resolve types in fields
    resolve_all_types(program);

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

void SemanticAnalyzer::collect_types(ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
    // Seed event_names_ from imported modules (e.g. std.core lifecycle events).
    // Also seed event_structs_ with empty stubs so that handler_event is non-null
    // for marker/cross-module events (e.g. `input`), enabling world-access calls
    // like exists() inside on-input handlers.
    for (const auto& [qualifier, syms] : imports_.modules) {
        for (const auto& ev_name : syms.events) {
            event_names_.insert(ev_name);
            if (!event_structs_.contains(ev_name)) {
                event_structs_[ev_name] = ResolvedStruct{.name = ev_name};
            }
        }
    }

    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {  // NOLINT(readability-function-cognitive-complexity)
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, StructNode>) {
                    if (struct_names_.contains(node.name)) {
                        errors_.error(node.location, "duplicate struct '" + node.name + "'");
                    }
                    struct_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, EnumNode>) {
                    if (enum_names_.contains(node.name)) {
                        errors_.error(node.location, "duplicate enum '" + node.name + "'");
                    }
                    enum_names_.insert(node.name);
                    ResolvedEnum re;
                    re.name = node.name;
                    for (auto& v : node.variants) {
                        re.variants.push_back(v.name);
                    }
                    result_.enums[node.name] = std::move(re);
                } else if constexpr (std::is_same_v<T, TraitNode>) {
                    node.is_stdlib = current_module_is_stdlib_;
                    if (trait_names_.contains(node.name)) {
                        errors_.error(node.location, "duplicate trait '" + node.name + "'");
                    }
                    trait_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, EventNode>) {
                    event_names_.insert(node.name);
                    ResolvedStruct rs;
                    rs.name = node.name;
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
                    if (node.is_pub) {
                        result_.pub_events.insert(node.name);
                    }
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    node.is_stdlib = current_module_is_stdlib_;
                    func_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, SystemNode> || std::is_same_v<T, ExternSystemNode>) {
                    node.is_stdlib = current_module_is_stdlib_;
                    if (system_names_.contains(node.name)) {
                        errors_.error(node.location, "duplicate system '" + node.name + "'");
                    }
                    system_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, ConstBlockNode>) {
                    for (auto& a : node.assignments) {
                        result_.string_pool.intern(a.name);
                    }
                } else if constexpr (std::is_same_v<T, TemplateNode>) {
                    // Track template names separately from units (5.2)
                    if (template_names_.contains(node.name)) {
                        errors_.error(node.location, "duplicate template '" + node.name + "'");
                    }
                    template_names_.insert(node.name);
                    archetype_traits_[node.name] = &node.traits;
                    if (node.is_pub) {
                        result_.pub_templates.insert(node.name);
                    } else {
                        result_.non_pub_templates.insert(node.name);
                    }
                } else if constexpr (std::is_same_v<T, EntityNode>) {
                    // Track entity names to distinguish from templates (5.4)
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
                    TypeKind tk                  = asset_kind_to_type_kind(node.asset_kind);
                    asset_decl_types_[node.name] = tk;
                } else if constexpr (std::is_same_v<T, InputDeclNode>) {
                    // Register input name → InputButton or InputAxis (task 6.4)
                    TypeKind tk = (node.input_kind == InputKind::Button) ? TypeKind::InputButton : TypeKind::InputAxis;
                    input_decl_types_[node.name] = tk;
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
                    ResolvedStruct rs;
                    rs.name = node.name;
                    for (auto& f : node.fields) {
                        ResolvedField rf;
                        rf.name = f.name;
                        rf.type = resolve_type_ref(f.type);
                        rs.fields.push_back(std::move(rf));
                    }
                    result_.structs[node.name] = std::move(rs);
                } else if constexpr (std::is_same_v<T, TraitNode>) {
                    ResolvedTrait rt;
                    rt.name      = node.name;
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
                    // Task 4.4: Resolve func parameter types and return type
                    ResolvedFunc rf;
                    rf.name      = node.name;
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

TypeInfo SemanticAnalyzer::resolve_type_ref(const TypeRef& ref) {
    // ── Qualified name: "module.Symbol" or "alias.Symbol" ──────────────────
    auto dot = ref.name.find('.');
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
        TypeInfo ti;
        ti.kind = TypeKind::Struct;
        ti.name = ref.name;
        return ti;
    }
    if (enum_names_.contains(ref.name)) {
        TypeInfo ti;
        ti.kind = TypeKind::Enum;
        ti.name = ref.name;
        return ti;
    }

    // ── Unqualified import lookup (4.3) ─────────────────────────────────────
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
    auto it = imports_.modules.find(qualifier);
    if (it == imports_.modules.end()) {
        errors_.error(loc, "unknown module qualifier '" + qualifier + "'");
        return make_unknown_type();
    }

    const auto& syms = it->second;

    // Check imported structs
    if (syms.structs.contains(sym_name)) {
        TypeInfo ti;
        ti.kind = TypeKind::Struct;
        ti.name = qualifier + "." + sym_name;
        return ti;
    }
    // Check imported enums
    if (syms.enums.contains(sym_name)) {
        TypeInfo ti;
        ti.kind = TypeKind::Enum;
        ti.name = qualifier + "." + sym_name;
        return ti;
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

// ── Task 4.3: Unqualified import lookup ────────────────────────────────────

TypeInfo SemanticAnalyzer::resolve_imported_type(const std::string& name, const SourceLocation& loc) {
    auto struct_it = imports_.struct_providers.find(name);
    auto enum_it   = imports_.enum_providers.find(name);

    size_t struct_count = (struct_it != imports_.struct_providers.end()) ? struct_it->second.size() : 0;
    size_t enum_count   = (enum_it != imports_.enum_providers.end()) ? enum_it->second.size() : 0;
    size_t total        = struct_count + enum_count;

    if (total == 0) {
        errors_.error(loc, "unknown type '" + name + "'");
        return make_unknown_type();
    }

    if (total > 1) {
        // Build a helpful ambiguity message (task 4.3 requirement)
        std::ostringstream msg;
        msg << "ambiguous reference '" << name << "': defined in";
        bool first  = true;
        auto append = [&](const std::vector<std::string>& quals) {
            for (const auto& q : quals) {
                msg << (first ? " module '" : " and module '") << q << "'";
                first = false;
            }
        };
        if (struct_it != imports_.struct_providers.end()) {
            append(struct_it->second);
        }
        if (enum_it != imports_.enum_providers.end()) {
            append(enum_it->second);
        }
        msg << "; use qualified access to disambiguate";
        errors_.error(loc, msg.str());
        return make_unknown_type();
    }

    // Unique — resolve
    if (struct_count == 1) {
        TypeInfo ti;
        ti.kind = TypeKind::Struct;
        ti.name = name;
        return ti;
    }
    TypeInfo ti;
    ti.kind = TypeKind::Enum;
    ti.name = name;
    return ti;
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
                    call_graph_[func_name].insert(ident->name);
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
    for (auto& [func, callees] : call_graph_) {
        std::unordered_set<std::string> visited;
        std::vector<std::string> stack = {func};
        visited.insert(func);

        while (!stack.empty()) {
            auto current = stack.back();
            stack.pop_back();

            auto it = call_graph_.find(current);
            if (it == call_graph_.end()) {
                continue;
            }

            for (const auto& callee : it->second) {
                if (callee == func) {
                    for (auto& decl : program.declarations) {
                        if (auto* fn = std::get_if<FuncNode>(&decl)) {
                            if (fn->name == func) {
                                errors_.error(fn->location,
                                              "func '" + func + "' is recursive (recursion is not allowed)");
                                break;
                            }
                        }
                    }
                    break;
                }
                if (visited.contains(callee)) {
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
                                                   member->member == "uniform_int" ||
                                                   member->member == "normal" || member->member == "identity";
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

        // Allow qualified references to resolve to a local trait when the simple
        // name exists in the current module. This keeps codegen-oriented tests
        // and stdlib-like qualified spellings working even when the backing
        // module is not imported in the snippet under analysis.
        if (trait_names_.contains(simple_trait_name)) {
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
    // Check imports (task 4.3 uniqueness)
    if (!imports_.empty()) {
        auto it = imports_.trait_providers.find(qname);
        if (it != imports_.trait_providers.end()) {
            if (it->second.size() > 1) {
                std::ostringstream msg;
                msg << "ambiguous trait '" << qname << "' in filter: found in module '" << it->second[0]
                    << "' and module '" << it->second[1] << "'";
                if (it->second.size() > 2) {
                    msg << " (and others)";
                }
                msg << "; use qualified access to disambiguate";
                errors_.error(entry.location, msg.str());
                return false;
            }
            out_simple_name = qname;
            return true;
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
        auto dot          = entry.qualified_name.rfind('.');
        auto simple       = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
        const auto* trait = find_resolved_trait(simple);
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
                    // Not local — check imports
                    bool found = false;
                    if (!imports_.empty()) {
                        auto it = imports_.trait_providers.find(trait_name);
                        if (it != imports_.trait_providers.end() && !it->second.empty()) {
                            found = true;
                        }
                    }
                    if (!found) {
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
                    bool found = false;
                    if (!imports_.empty()) {
                        auto it = imports_.trait_providers.find(trait_name);
                        if (it != imports_.trait_providers.end() && !it->second.empty()) {
                            found = true;
                        }
                    }
                    if (!found) {
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
                    auto dot = entry.qualified_name.rfind('.');
                    auto simple =
                        (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
                    const auto* trait = find_resolved_trait(simple);
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
                    local_bindings[handler.event_name] =
                        TypeInfo{.kind = TypeKind::Struct, .name = handler_event->name};
                }
                if (handler.alias.has_value() && handler_event != nullptr) {
                    local_bindings[*handler.alias] = TypeInfo{.kind = TypeKind::Struct, .name = handler_event->name};
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
        const auto* trait = find_resolved_trait(add.trait_name);
        if (trait == nullptr) {
            errors_.error(add.location, "undeclared trait '" + add.trait_name + "'");
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
        const auto* trait = find_resolved_trait(project.trait_name);
        if (trait == nullptr) {
            errors_.error(project.location, "undeclared trait '" + project.trait_name + "'");
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
        if (!is_trait_declared(remove.trait_name)) {
            errors_.error(remove.location, "undeclared trait '" + remove.trait_name + "'");
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
        const auto* trait = find_resolved_trait(arm.trait_name);
        if (trait == nullptr) {
            errors_.error(arm.location, "undeclared trait '" + arm.trait_name + "'");
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
                arm_locals[*arm.alias] = TypeInfo{.kind = TypeKind::Struct, .name = trait->name};
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
    auto add_dep_from_filter = [](const auto& sys, SystemDependency& dep) {
        if (!sys.filter.entries.empty()) {
            for (auto& entry : sys.filter.entries) {
                auto dot    = entry.qualified_name.find('.');
                auto simple = (dot != std::string::npos) ? entry.qualified_name.substr(dot + 1) : entry.qualified_name;
                dep.reads.insert(simple);
            }
        } else {
            for (auto& t : sys.filter.trait_names) {
                dep.reads.insert(t);
            }
        }
    };

    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            SystemDependency dep;
            dep.system_name = sys->name;
            add_dep_from_filter(*sys, dep);

            // Analyze handler bodies
            for (auto& handler : sys->handlers) {
                collect_system_deps(handler.body, dep);
            }

            result_.dependency_graph.push_back(std::move(dep));
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            SystemDependency dep;
            dep.system_name = sys->name;
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
    if (!imports_.empty()) {
        auto it = imports_.trait_providers.find(name);
        if (it != imports_.trait_providers.end() && !it->second.empty()) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::imported_symbols_contain_non_template(const ImportedSymbols& symbols,
                                                             const std::string& name) {
    return symbols.traits.contains(name) || symbols.structs.contains(name) || symbols.enums.contains(name) ||
           symbols.funcs.contains(name) || symbols.events.contains(name);
}

bool SemanticAnalyzer::local_non_template_symbol_exists(const std::string& name) const {
    return trait_names_.contains(name) || entity_names_.contains(name) || struct_names_.contains(name) ||
           enum_names_.contains(name) || event_names_.contains(name) || func_names_.contains(name) ||
           system_names_.contains(name) || asset_decl_types_.contains(name) || input_decl_types_.contains(name) ||
           use_names_.contains(name);
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
            if (provider_it->second.size() > 1) {
                std::ostringstream msg;
                msg << "ambiguous template '" << use.template_name << "' in archetype-body use: found in module '"
                    << provider_it->second[0] << "' and module '" << provider_it->second[1] << "'";
                if (provider_it->second.size() > 2) {
                    msg << " (and others)";
                }
                msg << "; use qualified access to disambiguate";
                errors_.error(use.location, msg.str());
                return false;
            }
            return true;
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
    if (!imports_.empty()) {
        auto pit = imports_.trait_providers.find(name);
        if (pit != imports_.trait_providers.end()) {
            for (const auto& qualifier : pit->second) {
                auto mit = imports_.modules.find(qualifier);
                if (mit != imports_.modules.end()) {
                    auto tit = mit->second.traits.find(name);
                    if (tit != mit->second.traits.end()) {
                        return &tit->second;
                    }
                }
            }
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

// ── std.text.format recognition ────────────────────────────────────────────────

bool SemanticAnalyzer::is_std_text_format_callee(const ExprNode& callee) const {
    if (result_.ast == nullptr) {
        return false;
    }
    // Unaliased: format(...) from use std.text (no alias)
    if (const auto* ident = std::get_if<IdentExpr>(&callee.expr)) {
        if (ident->name != "format") {
            return false;
        }
        for (const auto& decl : result_.ast->declarations) {
            if (const auto* use = std::get_if<UseNode>(&decl)) {
                if (use->module_name == "std.text" && !use->alias.has_value()) {
                    return true;
                }
            }
        }
        return false;
    }
    // Aliased: qualifier.format(...) — qualifier resolves to std.text
    if (const auto* member = std::get_if<MemberExpr>(&callee.expr)) {
        if (member->member != "format") {
            return false;
        }
        if (const auto* obj = std::get_if<IdentExpr>(&member->object->expr)) {
            for (const auto& decl : result_.ast->declarations) {
                if (const auto* use = std::get_if<UseNode>(&decl)) {
                    if (use->module_name != "std.text") {
                        continue;
                    }
                    if (use->alias.has_value() && *use->alias == obj->name) {
                        return true;
                    }
                    if (!use->alias.has_value() && obj->name == "text") {
                        return true;
                    }
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

std::optional<std::string> SemanticAnalyzer::get_query_func_name(const ExprNode& callee) const {
    if (result_.ast == nullptr) {
        return std::nullopt;
    }
    const auto* member = std::get_if<MemberExpr>(&callee.expr);
    if (!member) {
        return std::nullopt;
    }
    const auto& func_name = member->member;
    auto qualifier        = extract_dotted_path(*member->object);
    if (!qualifier) {
        return std::nullopt;
    }
    if (is_known_query_module(*qualifier)) {
        return func_name;
    }
    for (const auto& decl : result_.ast->declarations) {
        const auto* use_node = std::get_if<UseNode>(&decl);
        if (!use_node || !is_known_query_module(use_node->module_name)) {
            continue;
        }
        if (use_node->alias.has_value() && *use_node->alias == *qualifier) {
            return func_name;
        }
        if (!use_node->alias.has_value()) {
            auto last_dot       = use_node->module_name.rfind('.');
            std::string last_comp = (last_dot != std::string::npos) ? use_node->module_name.substr(last_dot + 1)
                                                                     : use_node->module_name;
            if (last_comp == *qualifier) {
                return func_name;
            }
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
                            local_bindings[handler.event_name] =
                                TypeInfo{.kind = TypeKind::Struct, .name = handler_event->name};
                            if (handler.alias.has_value()) {
                                local_bindings[*handler.alias] =
                                    TypeInfo{.kind = TypeKind::Struct, .name = handler_event->name};
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
            if (auto struct_it = result_.structs.find(local_it->second.name); struct_it != result_.structs.end()) {
                return find_field_type_in(struct_it->second.fields, member->member);
            }
            if (auto event_it = event_structs_.find(local_it->second.name); event_it != event_structs_.end()) {
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
        auto func_name_opt = get_query_func_name(*qcall->callee);
        const std::string func_name = func_name_opt.value_or([&]() -> std::string {
            if (const auto* m = std::get_if<MemberExpr>(&qcall->callee->expr)) {
                return m->member;
            }
            return "";
        }());
        validate_query_named_args(*qcall, func_name, filter_bindings, local_bindings, handler_event);
        if (func_name == "exists") {
            return make_bool_type();
        }
        if (func_name == "count") {
            return make_int_type();
        }
        if (func_name == "first" || func_name == "nearest" || func_name == "parent") {
            return make_entity_id_type();
        }
        if (func_name == "all" || func_name == "overlap_box" || func_name == "overlap_circle" ||
            func_name == "overlap_sphere" || func_name == "raycast") {
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

                    // 5.1: Validate all traits in nested trait blocks are declared
                    for (auto& entry : node.traits) {
                        if (!is_trait_declared(entry.trait_name)) {
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
                errors_.error(location,
                              "template '" + template_name + "' is not public in module '" + qualifier + "'");
            } else if (imported_symbols_contain_non_template(module_it->second, template_name)) {
                errors_.error(location,
                              "'" + template_name + "' is not a template; 'from' clause must reference a template");
            } else {
                errors_.error(location,
                              "undefined template '" + template_name + "' in module '" + qualifier + "'");
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
            return;  // valid imported template
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
            validate_archetype_template_ref(*child.template_ref,
                                            child.location,
                                            "child '" + child.role + "' of " + archetype_kind + " '" +
                                                archetype_name + "'");
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
                              "trait '" + entry.trait_name + "' is not part of child '" + override_node.role +
                                  "' in " + site_desc + "; cannot override it");
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
                    errors_.error(site_loc,
                                  "required field '" + field.name + "' not set for child '" + child.role + "' in " +
                                      site_desc);
                }
            }
        }

        static const std::vector<ChildOverrideNode> NO_OVERRIDES;
        validate_child_required_fields(child.children,
                                       override_node != nullptr ? override_node->children : NO_OVERRIDES,
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
        static const std::vector<ChildOverrideNode> NO_OVERRIDES;
        validate_child_required_fields(
            entity->children, NO_OVERRIDES, "entity '" + entity->name + "'", entity->location);
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
        const auto tmpl_dot   = tmpl_ref.rfind('.');
        const auto tmpl_key   = tmpl_dot != std::string::npos ? tmpl_ref.substr(tmpl_dot + 1) : tmpl_ref;
        auto tmpl_traits_it   = archetype_traits_.find(tmpl_key);
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
        validate_child_required_fields(*children_it->second,
                                       spawn.child_overrides,
                                       "spawn of template '" + spawn.template_name + "'",
                                       location);
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
                            const std::string KW = std::is_same_v<S, AddTraitStmt> ? "add" : "remove";
                            std::string msg      = "undeclared trait '";
                            msg += tname;
                            msg += "' in `";
                            msg += KW;
                            msg += "` statement";
                            errors_.error(s.location, msg);
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
                            errors_.error(s.location, "undeclared trait '" + tname + "' in `project` statement");
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
                        errors_.error(
                            node.location,
                            "`extern system` requires a `filter:` clause (no-filter extern systems are not supported)");
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
                errors_.error(entry.location, "undeclared trait '" + entry.qualified_name + "' in exclude clause");
            }
        }
    } else {
        for (auto& trait_name : node.exclude.trait_names) {
            if (!is_trait_declared(trait_name)) {
                errors_.error(node.exclude.location, "undeclared trait '" + trait_name + "' in exclude clause");
            }
        }
    }
}

// ── Phase 5a: after: clause validation and cycle detection ─────────────────

void SemanticAnalyzer::validate_after_clauses(
    ProgramNode& program) {  // NOLINT(readability-function-cognitive-complexity)
    // Collect all system names
    std::unordered_set<std::string> all_system_names;
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            all_system_names.insert(sys->name);
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            all_system_names.insert(sys->name);
        }
    }

    // Validate each system's after_systems list
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            for (auto& after_name : sys->after_systems) {
                if (!all_system_names.contains(after_name)) {
                    errors_.error(sys->location, "unknown system '" + after_name + "' in after clause");
                } else if (after_name == sys->name) {
                    errors_.error(sys->location, "system '" + sys->name + "' cannot list itself in after:");
                }
            }
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            for (auto& after_name : sys->after_systems) {
                if (!all_system_names.contains(after_name)) {
                    errors_.error(sys->location, "unknown system '" + after_name + "' in after clause");
                } else if (after_name == sys->name) {
                    errors_.error(sys->location, "system '" + sys->name + "' cannot list itself in after:");
                }
            }
        }
    }

    // Build adjacency graph for cycle detection: sys → list of systems it must run after
    std::unordered_map<std::string, std::vector<std::string>> after_graph;
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            after_graph[sys->name] = sys->after_systems;
        }
        if (auto* sys = std::get_if<ExternSystemNode>(&decl)) {
            after_graph[sys->name] = sys->after_systems;
        }
    }

    // DFS cycle detection
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
            for (auto& neighbor : it->second) {
                if (!all_system_names.contains(neighbor)) {
                    continue;  // already reported
                }
                if (color[neighbor] == Color::Gray) {
                    // Found a cycle — build path string
                    std::string cycle_path;
                    bool in_cycle = false;
                    for (auto& p : path) {
                        if (p == neighbor) {
                            in_cycle = true;
                        }
                        if (in_cycle) {
                            if (!cycle_path.empty()) {
                                cycle_path += " → ";
                            }
                            cycle_path += p;
                        }
                    }
                    cycle_path += " → " + neighbor;
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

    // Populate after_systems in dependency graph
    for (auto& dep : result_.dependency_graph) {
        auto it = after_graph.find(dep.system_name);
        if (it != after_graph.end()) {
            for (auto& after_name : it->second) {
                if (all_system_names.contains(after_name)) {
                    dep.after_systems.push_back(after_name);
                }
            }
        }
    }
}

}  // namespace cactus
