#include "backends/cpp-entt/persistence_emitter.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/construction_retention.hpp"
#include "backends/cpp-entt/relation_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include "common/persistence_document.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cactus {

namespace {

constexpr const char* kRuntime = "cactus::runtime::entt_backend::";

const ResolvedTrait* find_resolved_trait(const DecoratedProgram& program,
                                         const std::optional<SymbolId>& resolved,
                                         const std::string& source_name) {
    return EnttCodegenUtils::find_trait(program, resolved.has_value() ? make_canonical_id(*resolved) : source_name);
}

std::vector<SymbolId> symbols_of(const ResolvedTrait& declaration) {
    return declaration.symbol_id.has_value() ? std::vector{*declaration.symbol_id} : std::vector<SymbolId>{};
}

std::string emit_construction_call(const char* function,
                                   const std::string& trait_cpp,
                                   const std::string& entity_expr,
                                   const std::string& indent) {
    std::ostringstream out;
    out << indent << kRuntime << function << "<" << trait_cpp << ">(registry, " << entity_expr << ");\n";
    return out.str();
}

}  // namespace

bool EnttPersistenceEmitter::program_retains_provenance(const DecoratedProgram& program) {
    return std::ranges::any_of(program.persistence.archetypes, [&](const PersistenceArchetypeDescriptor& archetype) {
        return archetype_retains_construction_data(archetype, program.persistence.attaches_persistent_trait);
    });
}

std::optional<std::uint32_t> EnttPersistenceEmitter::archetype_origin_index(const DecoratedProgram& program,
                                                                           const SymbolId& archetype,
                                                                           const std::vector<std::string>& role_path) {
    const auto& archetypes = program.persistence.archetypes;
    const auto* found      = find_archetype_descriptor(
        program.persistence, ArchetypeNodeId{.archetype = archetype, .role_path = role_path});
    if (found == nullptr ||
        !archetype_retains_construction_data(*found, program.persistence.attaches_persistent_trait)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(found - archetypes.data());
}

bool EnttPersistenceEmitter::node_keeps_construction_copy(const DecoratedProgram& program,
                                                          std::optional<std::uint32_t> node,
                                                          const std::optional<SymbolId>& resolved,
                                                          const std::string& source_name) {
    const auto* declaration = find_resolved_trait(program, resolved, source_name);
    return node.has_value() && declaration != nullptr && !declaration->fields.empty() &&
           ConstructionRetention(program, symbols_of(*declaration)).retains(*node, declaration->symbol_id);
}

bool EnttPersistenceEmitter::structural_site_tracks_construction(const DecoratedProgram& program,
                                                                 const std::optional<SymbolId>& resolved,
                                                                 const std::string& source_name) {
    const auto* declaration = find_resolved_trait(program, resolved, source_name);
    return declaration != nullptr && !declaration->fields.empty() &&
           ConstructionRetention(program, symbols_of(*declaration)).retains_anywhere(declaration->symbol_id);
}

std::string EnttPersistenceEmitter::emit_archetype_origin(std::uint32_t node,
                                                          const std::string& entity_expr,
                                                          const std::string& indent) {
    std::ostringstream out;
    out << indent << "registry.emplace_or_replace<" << kRuntime << "ArchetypeOrigin>(" << entity_expr << ", "
        << kRuntime << "ArchetypeOrigin{.node = " << node << "});\n";
    return out.str();
}

std::string EnttPersistenceEmitter::emit_retain_construction(const std::string& trait_cpp,
                                                             const std::string& entity_expr,
                                                             const std::string& indent) {
    return emit_construction_call("retain_construction", trait_cpp, entity_expr, indent);
}

std::string EnttPersistenceEmitter::emit_discard_construction(const std::string& trait_cpp,
                                                              const std::string& entity_expr,
                                                              const std::string& indent) {
    return emit_construction_call("discard_construction", trait_cpp, entity_expr, indent);
}

std::string EnttPersistenceEmitter::emit_archetype_node_table(const DecoratedProgram& program) {
    if (!program_retains_provenance(program)) {
        return {};
    }
    std::ostringstream out;
    out << "// ── World Persistence Archetype Nodes ───────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";
    out << "// Canonical declaration identity per ArchetypeOrigin::node, never a\n";
    out << "// generated index or source line.\n";
    out << "inline constexpr std::array<std::string_view, " << program.persistence.archetypes.size()
        << "> generated_archetype_nodes = {\n";
    for (const auto& archetype : program.persistence.archetypes) {
        // Excluded nodes keep an empty slot so an index always equals the
        // node's position in the program's canonical archetype metadata.
        const bool retained =
            archetype_retains_construction_data(archetype, program.persistence.attaches_persistent_trait);
        out << "    \"" << (retained ? canonical_node_string(archetype.node) : std::string{}) << "\",\n";
    }
    out << "};\n\n";
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

std::string EnttPersistenceEmitter::emit_missing_provenance_probe(const DecoratedProgram& program) {
    if (!program_retains_provenance(program)) {
        return {};
    }

    std::vector<std::string> durable_types;
    std::unordered_set<std::string> seen;
    for (const auto& [key, trait] : program.traits) {
        if (!trait_declares_persist_field(trait) || !seen.insert(declaration_identity(trait)).second) {
            continue;
        }
        durable_types.push_back(EnttCodegenUtils::trait_cpp_name(trait.symbol_id, trait.name, program));
    }
    std::ranges::sort(durable_types);

    std::ostringstream out;
    out << "// ── World Persistence Provenance Check ──────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";
    out << "// An entity carrying a durable trait that no provenance-retaining\n";
    out << "// creation path produced — a host emplacing one directly, say. Capture\n";
    out << "// reports it rather than writing a record with no construction data.\n";
    out << "[[nodiscard]] inline std::optional<entt::entity> generated_entity_missing_provenance(\n";
    out << "    const entt::registry& registry) {\n";
    for (const auto& type : durable_types) {
        out << "    for (const auto entity : registry.view<const " << type << ">()) {\n";
        out << "        if (!has_capture_provenance(registry, entity)) { return entity; }\n";
        out << "    }\n";
    }
    out << "    return std::nullopt;\n";
    out << "}\n\n";
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

// ── Schema descriptor ───────────────────────────────────────────────────────

namespace {

// One entry of the generated value-type table. Unlike PersistenceValueType it
// names a list's element by index, so nested lists stay describable in flat
// constexpr data.
struct FlatValueType {
    PersistenceValueKind kind      = PersistenceValueKind::Unsupported;
    std::uint8_t bit_width         = 0;
    std::uint8_t lanes             = 1;
    PersistenceValueKind lane_kind = PersistenceValueKind::Float;
    std::string declared_type;
    std::uint32_t element = persistence::kNoValueType;

    friend bool operator==(const FlatValueType&, const FlatValueType&) = default;
};

std::uint32_t intern_value_type(std::vector<FlatValueType>& table, const PersistenceValueType& type) {
    FlatValueType flat{.kind      = type.kind,
                       .bit_width = type.bit_width,
                       .lanes     = type.lanes,
                       .lane_kind = type.lane_kind};
    if (type.declared_type.has_value()) {
        flat.declared_type = make_canonical_id(*type.declared_type);
    }
    if (!type.element.empty()) {
        flat.element = intern_value_type(table, type.element.front());
    }
    const auto found = std::ranges::find(table, flat);
    if (found != table.end()) {
        return static_cast<std::uint32_t>(std::distance(table.begin(), found));
    }
    table.push_back(std::move(flat));
    return static_cast<std::uint32_t>(table.size() - 1);
}

struct FieldModel {
    std::string name;
    std::uint32_t type = persistence::kNoValueType;
    bool persist       = false;
    std::string default_text;  // the declared default as written, empty when none
};

struct DeclarationModel {
    std::string canonical;
    std::vector<FieldModel> fields;     // sorted by name
    std::vector<std::string> variants;  // enums only; declaration order is the values
};

struct RelationModel {
    DeclarationModel declaration;
    const ResolvedRelation* relation = nullptr;
};

struct SchemaModel {
    std::vector<FlatValueType> value_types;
    std::vector<RelationModel> relations;
    std::vector<DeclarationModel> traits;
    std::vector<DeclarationModel> structs;
    std::vector<DeclarationModel> enums;
    std::vector<const PersistenceArchetypeDescriptor*> archetypes;
};

// Sorted by name: reordering a declaration's fields renames nothing, so it must
// not invalidate documents.
std::vector<FieldModel> field_models(const std::vector<ResolvedField>& fields,
                                     std::vector<FlatValueType>& value_types) {
    std::vector<FieldModel> models;
    models.reserve(fields.size());
    for (const auto& field : fields) {
        models.push_back(FieldModel{.name    = field.name,
                                    .type    = intern_value_type(value_types, describe_persistence_value(field.type)),
                                    .persist = field.is_persist});
    }
    std::ranges::sort(models, {}, &FieldModel::name);
    return models;
}

template <typename Map, typename Build>
std::vector<DeclarationModel> declaration_models(const Map& declarations, Build build) {
    std::vector<DeclarationModel> models;
    std::unordered_set<std::string> seen;
    for (const auto& [key, declaration] : declarations) {
        if (seen.insert(declaration_identity(declaration)).second) {
            models.push_back(build(declaration));
        }
    }
    std::ranges::sort(models, {}, &DeclarationModel::canonical);
    return models;
}

SchemaModel build_schema_model(const DecoratedProgram& program) {
    SchemaModel model;
    model.traits = declaration_models(program.traits, [&](const ResolvedTrait& trait) {
        DeclarationModel entry{.canonical = declaration_identity(trait),
                               .fields    = field_models(trait.fields, model.value_types)};
        for (auto& field : entry.fields) {
            const auto* declared =
                EnttCodegenUtils::find_trait_field_default(program, trait.module_name, trait.name, field.name);
            if (declared != nullptr) {
                field.default_text = EnttCodegenUtils::emit_expr(*declared, program);
            }
        }
        return entry;
    });
    model.structs = declaration_models(program.structs, [&](const ResolvedStruct& declaration) {
        return DeclarationModel{.canonical = declaration_identity(declaration),
                                .fields    = field_models(declaration.fields, model.value_types)};
    });
    model.enums = declaration_models(program.enums, [](const ResolvedEnum& declaration) {
        return DeclarationModel{.canonical = declaration_identity(declaration), .variants = declaration.variants};
    });
    for (const auto* relation : stored_relations(program)) {
        model.relations.push_back(RelationModel{
            .declaration = DeclarationModel{.canonical = relation->canonical_id,
                                            .fields    = field_models(relation->fields, model.value_types)},
            .relation    = relation});
    }
    for (const auto& archetype : program.persistence.archetypes) {
        if (archetype_retains_construction_data(archetype, program.persistence.attaches_persistent_trait)) {
            model.archetypes.push_back(&archetype);
        }
    }
    return model;
}

void render_value_type(std::ostringstream& out, const std::vector<FlatValueType>& table, std::uint32_t index) {
    if (index == persistence::kNoValueType) {
        out << "-";
        return;
    }
    const auto& type = table[index];
    out << persistence_value_kind_name(type.kind) << ":" << static_cast<int>(type.bit_width) << ":"
        << static_cast<int>(type.lanes) << ":" << persistence_value_kind_name(type.lane_kind) << ":"
        << type.declared_type << ":";
    render_value_type(out, table, type.element);
}

void render_fields(std::ostringstream& out, const SchemaModel& model, const DeclarationModel& declaration) {
    for (const auto& field : declaration.fields) {
        out << "  field " << field.name << " ";
        render_value_type(out, model.value_types, field.type);
        out << " persist=" << (field.persist ? "1" : "0") << " default=" << field.default_text << "\n";
    }
}

std::string array_name_for(const std::string& canonical) {
    std::string name = "generated_schema_";
    for (const char c : canonical) {
        name += (c == '.' || c == '/') ? '_' : c;
    }
    return name;
}

void emit_field_array(std::ostringstream& out, const DeclarationModel& declaration) {
    out << "inline constexpr std::array<cactus::persistence::FieldDescriptor, " << declaration.fields.size() << "> "
        << array_name_for(declaration.canonical) << " = {{\n";
    for (const auto& field : declaration.fields) {
        out << "    {.name = \"" << field.name << "\", .type = " << field.type
            << ", .persist = " << (field.persist ? "true" : "false") << "},\n";
    }
    out << "}};\n\n";
}

void emit_string_array(std::ostringstream& out, const std::string& name, const std::vector<std::string>& values) {
    out << "inline constexpr std::array<std::string_view, " << values.size() << "> " << name << " = {{\n";
    for (const auto& value : values) {
        out << "    \"" << value << "\",\n";
    }
    out << "}};\n";
}

// Traits restore does not compare: those any entity may carry or lack, states
// (a save does not record them) and Parent (restored from the hierarchy).
std::vector<std::string> unchecked_traits(const DecoratedProgram& program) {
    std::set<std::string> unchecked;
    for (const auto& trait : program.reachability.free_traits) {
        unchecked.insert(make_canonical_id(trait));
    }
    for (const auto& [_, trait] : program.traits) {
        if (trait.state.has_value() || trait.name == "Parent") {
            unchecked.insert(declaration_identity(trait));
        }
    }
    return {unchecked.begin(), unchecked.end()};
}

std::vector<std::vector<std::string>> reachable_sets(const DecoratedProgram& program,
                                                     const ArchetypeNodeId& node,
                                                     const std::vector<std::string>& unchecked) {
    std::set<std::vector<std::string>> sets;
    for (const auto& origin : program.reachability.origins) {
        if (origin.node != node) {
            continue;
        }
        for (const auto& traits : origin.trait_sets) {
            std::vector<std::string> names;
            for (const auto& trait : traits) {
                auto name = make_canonical_id(trait);
                if (!std::ranges::binary_search(unchecked, name)) {
                    names.push_back(std::move(name));
                }
            }
            std::ranges::sort(names);
            sets.insert(std::move(names));
        }
    }
    return {sets.begin(), sets.end()};
}

void emit_reachable_sets(std::ostringstream& out,
                         const std::string& name,
                         const std::vector<std::vector<std::string>>& sets) {
    for (std::size_t index = 0; index < sets.size(); ++index) {
        emit_string_array(out, name + "_" + std::to_string(index), sets[index]);
    }
    out << "inline constexpr std::array<std::span<const std::string_view>, " << sets.size() << "> " << name
        << " = {{\n";
    for (std::size_t index = 0; index < sets.size(); ++index) {
        out << "    " << name << "_" << index << ",\n";
    }
    out << "}};\n";
}

}  // namespace

std::string EnttPersistenceEmitter::canonical_schema_text(const DecoratedProgram& program) {
    const auto model = build_schema_model(program);
    std::ostringstream out;
    out << "revision " << kPersistenceSchemaRevision << "\n";
    for (const auto& declaration : model.enums) {
        out << "enum " << declaration.canonical << "\n";
        for (const auto& variant : declaration.variants) {
            out << "  variant " << variant << "\n";
        }
    }
    for (const auto& declaration : model.structs) {
        out << "struct " << declaration.canonical << "\n";
        render_fields(out, model, declaration);
    }
    for (const auto& declaration : model.traits) {
        out << "trait " << declaration.canonical << "\n";
        render_fields(out, model, declaration);
    }
    for (const auto& [declaration, relation] : model.relations) {
        out << "relation " << declaration.canonical << " from " << make_canonical_id(relation->from.trait)
            << (relation->from.one ? " one" : "") << " to " << make_canonical_id(relation->to.trait)
            << (relation->to.one ? " one" : "") << "\n";
        render_fields(out, model, declaration);
    }
    for (const auto* archetype : model.archetypes) {
        out << "archetype " << canonical_node_string(archetype->node) << "\n";
        for (const auto& baseline : archetype->baseline_traits) {
            out << "  trait " << make_canonical_id(baseline.trait) << "\n";
            for (const auto& assigned : baseline.assigned_fields) {
                out << "    pins " << assigned << "\n";
            }
        }
        for (const auto& parameter : archetype->parameters) {
            out << "  param " << parameter << "\n";
        }
    }
    return out.str();
}

std::string EnttPersistenceEmitter::emit_schema_descriptor(const DecoratedProgram& program) {
    if (!program_retains_provenance(program)) {
        return {};
    }
    const auto model       = build_schema_model(program);
    const auto fingerprint = persistence_fingerprint(canonical_schema_text(program));

    std::ostringstream out;
    out << "// ── World Persistence Schema ────────────────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";

    out << "inline constexpr std::array<cactus::persistence::ValueTypeDescriptor, " << model.value_types.size()
        << "> generated_value_types = {{\n";
    for (const auto& type : model.value_types) {
        out << "    {.kind = cactus::PersistenceValueKind::" << persistence_value_kind_enumerator(type.kind)
            << ", .bit_width = " << static_cast<int>(type.bit_width) << ", .lanes = " << static_cast<int>(type.lanes)
            << ", .lane_kind = cactus::PersistenceValueKind::" << persistence_value_kind_enumerator(type.lane_kind)
            << ", .declared_type = \"" << type.declared_type << "\", .element = ";
        if (type.element == persistence::kNoValueType) {
            out << "cactus::persistence::kNoValueType";
        } else {
            out << type.element;
        }
        out << "},\n";
    }
    out << "}};\n\n";

    for (const auto& declaration : model.enums) {
        emit_string_array(out, array_name_for(declaration.canonical), declaration.variants);
        out << "\n";
    }
    out << "inline constexpr std::array<cactus::persistence::EnumDescriptor, " << model.enums.size()
        << "> generated_enums = {{\n";
    for (const auto& declaration : model.enums) {
        out << "    {.name = \"" << declaration.canonical
            << "\", .variants = " << array_name_for(declaration.canonical) << "},\n";
    }
    out << "}};\n\n";

    for (const auto& declaration : model.structs) {
        emit_field_array(out, declaration);
    }
    out << "inline constexpr std::array<cactus::persistence::StructDescriptor, " << model.structs.size()
        << "> generated_structs = {{\n";
    for (const auto& declaration : model.structs) {
        out << "    {.name = \"" << declaration.canonical
            << "\", .fields = " << array_name_for(declaration.canonical) << "},\n";
    }
    out << "}};\n\n";

    for (const auto& declaration : model.traits) {
        emit_field_array(out, declaration);
    }
    out << "inline constexpr std::array<cactus::persistence::TraitDescriptor, " << model.traits.size()
        << "> generated_traits = {{\n";
    for (const auto& declaration : model.traits) {
        out << "    {.trait = \"" << declaration.canonical
            << "\", .fields = " << array_name_for(declaration.canonical) << "},\n";
    }
    out << "}};\n\n";

    for (const auto& [declaration, relation] : model.relations) {
        emit_field_array(out, declaration);
    }
    out << "inline constexpr std::array<cactus::persistence::RelationDescriptor, " << model.relations.size()
        << "> generated_relations = {{\n";
    for (const auto& [declaration, relation] : model.relations) {
        out << "    {.relation = \"" << declaration.canonical << "\", .from_trait = \""
            << make_canonical_id(relation->from.trait) << "\", .to_trait = \"" << make_canonical_id(relation->to.trait)
            << "\", .from_one = " << (relation->from.one ? "true" : "false")
            << ", .to_one = " << (relation->to.one ? "true" : "false")
            << ", .fields = " << array_name_for(declaration.canonical) << "},\n";
    }
    out << "}};\n\n";

    const auto unchecked = unchecked_traits(program);
    emit_string_array(out, "generated_unchecked_traits", unchecked);
    for (const auto* archetype : model.archetypes) {
        const auto node = canonical_node_string(archetype->node);
        emit_reachable_sets(out, array_name_for(node) + "_reachable", reachable_sets(program, archetype->node, unchecked));
        std::vector<std::string> baseline;
        baseline.reserve(archetype->baseline_traits.size());
        for (const auto& trait : archetype->baseline_traits) {
            baseline.push_back(make_canonical_id(trait.trait));
        }
        emit_string_array(out, array_name_for(node) + "_baseline", baseline);
        emit_string_array(out, array_name_for(node) + "_parameters", archetype->parameters);
        out << "\n";
    }
    out << "inline constexpr std::array<cactus::persistence::ArchetypeDescriptor, " << model.archetypes.size()
        << "> generated_archetypes = {{\n";
    for (const auto* archetype : model.archetypes) {
        const auto node = canonical_node_string(archetype->node);
        out << "    {.node = \"" << node << "\", .baseline_traits = " << array_name_for(node)
            << "_baseline, .parameters = " << array_name_for(node) << "_parameters, .reachable_trait_sets = "
            << array_name_for(node) << "_reachable},\n";
    }
    out << "}};\n\n";

    out << "inline constexpr cactus::persistence::SchemaDescriptor generated_schema = {\n";
    out << "    .revision = cactus::kPersistenceSchemaRevision,\n";
    out << "    .fingerprint = " << fingerprint << "ULL,\n";
    out << "    .module = \"" << program.module_name << "\",\n";
    out << "    .value_types = generated_value_types,\n";
    out << "    .traits = generated_traits,\n";
    out << "    .structs = generated_structs,\n";
    out << "    .enums = generated_enums,\n";
    out << "    .archetypes = generated_archetypes,\n";
    out << "    .relations = generated_relations,\n";
    out << "    .checks_trait_sets = " << (program.reachability.derived ? "true" : "false") << ",\n";
    out << "    .unchecked_traits = generated_unchecked_traits,\n";
    out << "    .parents_first = " << (EnttCodegenUtils::checks_reparent(program) ? "false" : "true") << ",\n";
    out << "};\n\n";
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

// ── World capture ───────────────────────────────────────────────────────────

std::vector<PersistableTrait> persistable_traits(const DecoratedProgram& program) {
    std::vector<PersistableTrait> traits;
    std::unordered_set<std::string> seen;
    for (const auto& [key, trait] : program.traits) {
        if (!seen.insert(declaration_identity(trait)).second) {
            continue;
        }
        traits.push_back(
            PersistableTrait{.canonical   = declaration_identity(trait),
                             .cpp_name    = EnttCodegenUtils::trait_cpp_name(trait.symbol_id, trait.name, program),
                             .declaration = &trait});
    }
    std::ranges::sort(traits, {}, &PersistableTrait::canonical);
    return traits;
}

std::vector<const ResolvedRelation*> stored_relations(const DecoratedProgram& program) {
    auto relations = sorted_relations(program);
    std::erase_if(relations, [](const ResolvedRelation* relation) { return relation->kept_by.has_value(); });
    return relations;
}

std::vector<SymbolId> persistable_symbols(const std::vector<PersistableTrait>& traits) {
    std::vector<SymbolId> symbols;
    symbols.reserve(traits.size());
    for (const auto& trait : traits) {
        if (trait.declaration->symbol_id.has_value()) {
            symbols.push_back(*trait.declaration->symbol_id);
        }
    }
    return symbols;
}

std::vector<PersistableTrait> without_parent_trait(std::vector<PersistableTrait> traits,
                                                    const DecoratedProgram& program) {
    const auto* parent_trait = EnttCodegenUtils::find_trait(program, "Parent");
    if (parent_trait == nullptr) {
        return traits;
    }
    const auto& canonical = declaration_identity(*parent_trait);
    std::erase_if(traits, [&](const PersistableTrait& trait) { return trait.canonical == canonical; });
    return traits;
}

namespace {

// Assets and input actions are referenced by canonical declaration (dsl-
// persistence-schema), never by the numeric handle a runtime happens to
// assign them: handles are allocation-order artifacts, not stable identity.
struct ReferenceNameTable {
    std::vector<std::string> assets;         // AssetHandle order (1-based)
    std::vector<std::string> input_buttons;  // InputButton order (0-based)
    std::vector<std::string> input_axes;     // InputAxis order (0-based)
};

std::string canonical_declaration_name(const DecoratedProgram& program,
                                       const std::optional<SymbolId>& resolved,
                                       const std::string& local_name) {
    return resolved.has_value() ? make_canonical_id(*resolved) : program.module_name + "." + local_name;
}

ReferenceNameTable build_reference_name_table(const DecoratedProgram& program) {
    ReferenceNameTable table;
    if (program.ast == nullptr) {
        return table;
    }
    for (const auto& decl : program.ast->declarations) {
        if (const auto* asset = std::get_if<AssetDeclNode>(&decl)) {
            table.assets.push_back(canonical_declaration_name(program, asset->resolved_asset_id, asset->name));
        } else if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
            auto& target = input->input_kind == InputKind::Button ? table.input_buttons : table.input_axes;
            target.push_back(canonical_declaration_name(program, input->resolved_input_id, input->name));
        }
    }
    return table;
}

void emit_reference_name_lookup(std::ostringstream& out,
                                const std::string& array_name,
                                const std::string& function_name,
                                const std::string& handle_type,
                                const std::vector<std::string>& names,
                                int base) {
    out << "inline constexpr std::array<std::string_view, " << names.size() << "> " << array_name << " = {{\n";
    for (const auto& name : names) {
        out << "    \"" << name << "\",\n";
    }
    out << "}};\n\n";
    out << "[[nodiscard]] inline std::string_view " << function_name << "(" << handle_type << " handle) {\n";
    out << "    const auto index = static_cast<std::size_t>(handle) - " << base << ";\n";
    out << "    return index < " << array_name << ".size() ? " << array_name << "[index] : std::string_view{};\n";
    out << "}\n\n";
}

// Canonical-identity lookups for the numeric handles AssetRef/InputRef fields
// store, built once so struct/trait field capture can call a plain function.
void emit_reference_name_tables(std::ostringstream& out, const DecoratedProgram& program) {
    const auto table = build_reference_name_table(program);
    emit_reference_name_lookup(out, "generated_asset_names", "generated_asset_name", "std::uint32_t", table.assets, 1);
    emit_reference_name_lookup(
        out, "generated_input_button_names", "generated_input_button_name", "std::uint8_t", table.input_buttons, 0);
    emit_reference_name_lookup(
        out, "generated_input_axis_names", "generated_input_axis_name", "std::uint8_t", table.input_axes, 0);
}

// The inverse of emit_reference_name_lookup: a declared name back to its
// numeric handle, nullopt when this build declares no such name. Reuses the
// array emit_reference_name_lookup already emitted rather than rebuilding it.
// Called once per restored AssetRef/InputRef/archetype field, so the lookup
// itself is a function-local static hash map built once on first use (O(1)
// thereafter) rather than a linear scan repeated on every call.
void emit_reference_name_reverse_lookup(std::ostringstream& out,
                                        const std::string& array_name,
                                        const std::string& function_name,
                                        const std::string& handle_type,
                                        int base) {
    out << "[[nodiscard]] inline std::optional<" << handle_type << "> " << function_name
        << "(std::string_view name) {\n";
    out << "    static const std::unordered_map<std::string_view, " << handle_type << "> lookup = [] {\n";
    out << "        std::unordered_map<std::string_view, " << handle_type << "> map;\n";
    out << "        map.reserve(" << array_name << ".size());\n";
    out << "        for (std::size_t index = 0; index < " << array_name << ".size(); ++index) {\n";
    out << "            map.emplace(" << array_name << "[index], static_cast<" << handle_type << ">(index + " << base
        << "));\n";
    out << "        }\n";
    out << "        return map;\n";
    out << "    }();\n";
    out << "    const auto found = lookup.find(name);\n";
    out << "    return found == lookup.end() ? std::nullopt : std::optional<" << handle_type << ">(found->second);\n";
    out << "}\n\n";
}

std::string kind_enumerator(PersistenceValueKind kind) {
    return std::string("cactus::PersistenceValueKind::") + persistence_value_kind_enumerator(kind);
}

// Structs are registered before any type that names them, so a struct can
// reach itself through a list.
using StructSlots = std::unordered_map<std::string, std::string>;

std::string struct_slot_name(std::size_t index) {
    return "document_struct_" + std::to_string(index);
}

// C++ text registering `type` in the document being built (`out`) and yielding
// its document type index.
std::string type_registration(const DecoratedProgram& program, const TypeInfo& type, const StructSlots& structs) {
    const auto described = describe_persistence_value(type);
    const auto kind      = kind_enumerator(described.kind);
    switch (described.kind) {
        case PersistenceValueKind::Vector:
            return "out.type({.kind = " + kind + ", .lanes = " + std::to_string(described.lanes) +
                   ", .lane_kind = " + kind_enumerator(described.lane_kind) + "})";
        case PersistenceValueKind::Enum: {
            const auto* declaration = EnttCodegenUtils::find_enum(program, type.name);
            std::string variants;
            for (const auto& variant : declaration->variants) {
                variants += (variants.empty() ? "\"" : ", \"") + variant + "\"";
            }
            return "out.type({.kind = " + kind + ", .ref = out.enumeration(\"" + declaration_identity(*declaration) +
                   "\", std::array<std::string_view, " + std::to_string(declaration->variants.size()) + ">{" +
                   variants + "})})";
        }
        case PersistenceValueKind::Struct:
            return "out.type({.kind = " + kind + ", .ref = " +
                   structs.at(declaration_identity(*EnttCodegenUtils::find_struct(program, type.name))) + "})";
        case PersistenceValueKind::List:
            return "out.type({.kind = " + kind + ", .ref = " + type_registration(program, *type.element, structs) +
                   "})";
        default:
            return "out.type({.kind = " + kind + "})";
    }
}

std::string field_list_registration(const DecoratedProgram& program,
                                    const std::vector<const ResolvedField*>& fields,
                                    const StructSlots& structs) {
    std::string text = "{";
    for (const auto* field : fields) {
        text += (text.size() == 1 ? "" : ", ");
        text += "{.name = out.name(\"" + field->name + "\"), .type = " +
                type_registration(program, field->type, structs) + "}";
    }
    return text + "}";
}


void emit_document_tables(std::ostringstream& out,
                          const DecoratedProgram& program,
                          const std::vector<PersistableTrait>& traits) {
    const auto table     = build_reference_name_table(program);
    const auto relations = stored_relations(program);
    out << "// Where this program's names and layouts sit in one document's tables.\n";
    out << "struct GeneratedDocumentTables {\n";
    out << "    std::uint32_t unset_name = 0;\n";
    out << "    std::array<std::uint32_t, " << table.assets.size() << "> assets{};\n";
    out << "    std::array<std::uint32_t, " << table.input_buttons.size() << "> buttons{};\n";
    out << "    std::array<std::uint32_t, " << table.input_axes.size() << "> axes{};\n";
    out << "    std::array<std::uint32_t, " << program.persistence.archetypes.size() << "> nodes{};\n";
    out << "    std::array<std::uint32_t, " << traits.size() << "> traits{};\n";
    out << "    std::array<std::uint32_t, " << relations.size() << "> relations{};\n";
    out << "};\n\n";

    out << "[[nodiscard]] inline GeneratedDocumentTables generated_register_document_tables(\n";
    out << "    cactus::persistence::SnapshotBuilder& out) {\n";
    out << "    GeneratedDocumentTables tables;\n";
    out << "    tables.unset_name = out.name(\"\");\n";
    const std::array<std::pair<const char*, const char*>, 4> name_tables = {{
        {"assets", "generated_asset_names"},
        {"buttons", "generated_input_button_names"},
        {"axes", "generated_input_axis_names"},
        {"nodes", "generated_archetype_nodes"},
    }};
    for (const auto& [member, names] : name_tables) {
        out << "    for (std::size_t index = 0; index < " << names << ".size(); ++index) {\n";
        out << "        tables." << member << "[index] = out.name(" << names << "[index]);\n";
        out << "    }\n";
    }
    StructSlots slots;
    const auto structs = unique_structs(program);
    for (std::size_t index = 0; index < structs.size(); ++index) {
        slots.emplace(declaration_identity(*structs[index]), struct_slot_name(index));
        out << "    const auto " << struct_slot_name(index) << " = out.declare_struct();\n";
    }
    for (std::size_t index = 0; index < structs.size(); ++index) {
        out << "    out.define_struct(" << struct_slot_name(index) << ", "
            << field_list_registration(program, persisting_fields(program, structs[index]->fields), slots) << ");\n";
    }
    for (std::size_t index = 0; index < traits.size(); ++index) {
        const auto& fields = traits[index].declaration->fields;
        out << "    tables.traits[" << index << "] = out.trait_shape(\"" << traits[index].canonical << "\", "
            << field_list_registration(program, persisting_fields(program, fields, false), slots) << ", "
            << field_list_registration(program, persisting_fields(program, fields, true), slots) << ");\n";
    }
    for (std::size_t index = 0; index < relations.size(); ++index) {
        out << "    tables.relations[" << index << "] = out.trait_shape(\"" << relations[index]->canonical_id << "\", "
            << field_list_registration(program, persisting_fields(program, relations[index]->fields), slots)
            << ", {});\n";
    }
    out << "    return tables;\n";
    out << "}\n\n";
}

// C++ statement writing one value at `access` in the field-list order the
// document tables record.
std::string put_statement(const DecoratedProgram& program, const TypeInfo& type, const std::string& access, int depth) {
    switch (describe_persistence_value(type).kind) {
        case PersistenceValueKind::Bool:
            return "out.put_bool(" + access + ");";
        case PersistenceValueKind::Int:
            return "out.put_int(" + access + ");";
        case PersistenceValueKind::Float:
            return "out.put_float(" + access + ");";
        case PersistenceValueKind::String:
            return "out.put_name(" + access + ");";
        case PersistenceValueKind::Vector:
            return std::string(kRuntime) + "put_persistence_lanes(out, " + access + ");";
        case PersistenceValueKind::EntityRef:
            return "out.put_entity(ids.reference(" + access + "));";
        case PersistenceValueKind::Enum:
            return "out.put_index(static_cast<std::uint32_t>(" + access + "));";
        case PersistenceValueKind::Struct:
            return "generated_put_struct(out, " + access + ", ids, tables);";
        case PersistenceValueKind::List: {
            const auto item = "item_" + std::to_string(depth);
            return std::string(kRuntime) + "put_persistence_list(out, " + access + ", ids, [&](const auto& " + item +
                   ") { " + put_statement(program, *type.element, item, depth + 1) + " });";
        }
        case PersistenceValueKind::AssetRef:
            return "out.put_index(" + std::string(kRuntime) + "document_name_at(tables.assets, static_cast<std::size_t>(" +
                   access + ") - 1U, tables.unset_name));";
        case PersistenceValueKind::InputRef:
            return "out.put_index(" + std::string(kRuntime) + "document_name_at(tables." +
                   (type.kind == TypeKind::InputAxis ? "axes" : "buttons") + ", static_cast<std::size_t>(" + access +
                   "), tables.unset_name));";
        case PersistenceValueKind::Unsupported:
            return {};
    }
    return {};
}

void emit_struct_encoders(std::ostringstream& out, const DecoratedProgram& program) {
    const auto structs = unique_structs(program);
    const auto signature = [](const ResolvedStruct& declaration) {
        return "inline void generated_put_struct(cactus::persistence::SnapshotBuilder& out, const " +
               cpp_type_ref(SymbolKind::Struct, declaration.module_name, declaration.name) +
               "& value, DocumentIdMap& ids, const GeneratedDocumentTables& tables)";
    };
    for (const auto* declaration : structs) {
        out << signature(*declaration) << ";\n";
    }
    out << "\n";
    for (const auto* declaration : structs) {
        out << signature(*declaration) << " {\n";
        out << "    (void)tables;\n";
        out << "    ids.enter_nesting();\n";
        for (const auto* field : persisting_fields(program, declaration->fields)) {
            out << "    " << put_statement(program, field->type, "value." + cpp_escaped(field->name), 0) << "\n";
        }
        out << "    ids.exit_nesting();\n";
        out << "}\n\n";
    }
}

// Rows of each stored relation whose two ends the save records, in creation order.
void emit_relation_capture(std::ostringstream& out, const DecoratedProgram& program) {
    const auto relations = stored_relations(program);
    for (std::size_t index = 0; index < relations.size(); ++index) {
        const auto& relation = *relations[index];
        out << "    if (const auto* store = registry.ctx().find<RelationStore<"
            << EnttCodegenUtils::relation_cpp_name(*relation.symbol_id) << ">>()) {\n";
        out << "        store->for_each_live([&](const auto& entry) {\n";
        out << "            const auto from = ids.reference(entry.from);\n";
        out << "            const auto to   = ids.reference(entry.to);\n";
        out << "            if (!from.present || !to.present) { return; }\n";
        out << "            out.begin_relation_row(tables.relations[" << index << "], from.id, to.id);\n";
        for (const auto* field : persisting_fields(program, relation.fields)) {
            out << "            " << put_statement(program, field->type, "entry.row." + cpp_escaped(field->name), 0)
                << "\n";
        }
        out << "            out.end_relation_row();\n";
        out << "        });\n";
        out << "    }\n";
    }
}

void emit_eligibility(std::ostringstream& out, const std::vector<PersistableTrait>& traits) {
    out << "// Eligible through the originating archetype's declared trait set, or\n";
    out << "// through a durable trait attached right now.\n";
    out << "[[nodiscard]] inline bool generated_entity_eligible(const entt::registry& registry, entt::entity "
           "entity) {\n";
    out << "    const auto* origin = registry.try_get<ArchetypeOrigin>(entity);\n";
    out << "    if (origin == nullptr) { return false; }\n";
    out << "    if (generated_archetype_eligible[origin->node]) { return true; }\n";
    std::vector<std::string> durable;
    for (const auto& trait : traits) {
        if (trait_declares_persist_field(*trait.declaration)) {
            durable.push_back(trait.cpp_name);
        }
    }
    if (durable.empty()) {
        out << "    return false;\n";
    } else {
        out << "    return registry.any_of<";
        for (std::size_t index = 0; index < durable.size(); ++index) {
            out << (index == 0 ? "" : ", ") << durable[index];
        }
        out << ">(entity);\n";
    }
    out << "}\n\n";
}

constexpr const char* kCaptureParameters =
    "(const entt::registry& registry,\n"
    "                                     entt::entity entity,\n"
    "                                     DocumentIdMap& ids,\n"
    "                                     cactus::persistence::SnapshotBuilder& out,\n"
    "                                     const GeneratedDocumentTables& tables)";

// One encoder per trait with fields keeps each generated function small
// however many traits a program persists.
void emit_trait_capture(std::ostringstream& out,
                        const DecoratedProgram& program,
                        const PersistableTrait& trait,
                        std::size_t layout,
                        const ConstructionRetention& retention) {
    const auto construction = persisting_fields(program, trait.declaration->fields, false);
    const auto persisted    = persisting_fields(program, trait.declaration->fields, true);
    out << "template <>\n";
    out << "inline void generated_capture_trait<" << trait.cpp_name << ">" << kCaptureParameters << " {\n";
    out << "    (void)ids;\n";
    out << "    const auto* live = registry.try_get<" << trait.cpp_name << ">(entity);\n";
    out << "    if (live == nullptr) {\n";
    out << "        return;\n";
    out << "    }\n";
    // Without a copy nothing can have changed the construction fields, so the live value holds them.
    std::string constructed = "live->";
    if (!construction.empty() && retention.retains_anywhere(trait.declaration->symbol_id)) {
        out << "    const auto* built = registry.try_get<Construction<" << trait.cpp_name << ">>(entity);\n";
        out << "    const auto& constructed = built != nullptr ? built->value : *live;\n";
        constructed = "constructed.";
    }
    out << "    out.begin_trait(tables.traits[" << layout << "]);\n";
    for (const auto* field : construction) {
        out << "    " << put_statement(program, field->type, constructed + cpp_escaped(field->name), 0) << "\n";
    }
    out << "    out.begin_persisted();\n";
    for (const auto* field : persisted) {
        out << "    " << put_statement(program, field->type, "live->" + cpp_escaped(field->name), 0) << "\n";
    }
    out << "    out.end_trait();\n";
    out << "}\n\n";
}

void emit_attached_traits(std::ostringstream& out,
                          const DecoratedProgram& program,
                          const std::vector<PersistableTrait>& layouts,
                          const std::vector<PersistableTrait>& traits) {
    out << "template <class T>\n";
    out << "void generated_capture_trait" << kCaptureParameters << ";\n\n";
    const ConstructionRetention retention(program, persistable_symbols(traits));
    for (const auto& trait : traits) {
        if (!trait.declaration->fields.empty()) {
            emit_trait_capture(out, program, trait, layout_index(layouts, trait.canonical), retention);
        }
    }
    out << "inline void generated_capture_traits" << kCaptureParameters << " {\n";
    out << "    (void)ids;\n";
    out << "    (void)out;\n";
    out << "    (void)tables;\n";
    for (const auto& trait : traits) {
        out << "    if (!::cactus_gen::detail::projected<" << trait.cpp_name << ">.is_projected(entity)) {\n";
        if (trait.declaration->fields.empty()) {
            out << "        if (registry.all_of<" << trait.cpp_name << ">(entity)) {\n";
            out << "            out.begin_trait(tables.traits[" << layout_index(layouts, trait.canonical) << "]);\n";
            out << "            out.end_trait();\n";
            out << "        }\n";
        } else {
            out << "        generated_capture_trait<" << trait.cpp_name << ">(registry, entity, ids, out, tables);\n";
        }
        out << "    }\n";
    }
    out << "}\n\n";
}

void emit_absent_traits(std::ostringstream& out,
                        const DecoratedProgram& program,
                        const std::vector<PersistableTrait>& layouts) {
    out << "// A baseline trait gameplay removed is recorded as absent: membership\n";
    out << "// information, no payloads.\n";
    out << "inline void generated_capture_absent_traits(const entt::registry& registry,\n";
    out << "                                           entt::entity entity,\n";
    out << "                                           std::uint32_t node,\n";
    out << "                                           cactus::persistence::SnapshotBuilder& out,\n";
    out << "                                           const GeneratedDocumentTables& tables) {\n";
    out << "    (void)tables;\n";
    out << "    switch (node) {\n";
    // Nodes with the same baseline share one case body.
    std::vector<std::pair<std::string, std::vector<std::uint32_t>>> cases;
    std::uint32_t node_index = 0;
    for (const auto& archetype : program.persistence.archetypes) {
        if (archetype_retains_construction_data(archetype, program.persistence.attaches_persistent_trait)) {
            std::ostringstream body;
            for (const auto& baseline : archetype.baseline_traits) {
                const auto layout = layout_index(layouts, make_canonical_id(baseline.trait));
                if (layout == layouts.size()) {
                    continue;
                }
                body << "            if (!registry.all_of<" << EnttCodegenUtils::trait_cpp_name(baseline.trait)
                     << ">(entity)) {\n";
                body << "                out.begin_trait(tables.traits[" << layout << "], false);\n";
                body << "                out.end_trait();\n";
                body << "            }\n";
            }
            auto text  = std::move(body).str();
            auto found = std::ranges::find(cases, text, &std::pair<std::string, std::vector<std::uint32_t>>::first);
            if (found == cases.end()) {
                cases.emplace_back(std::move(text), std::vector<std::uint32_t>{node_index});
            } else {
                found->second.push_back(node_index);
            }
        }
        ++node_index;
    }
    for (const auto& [body, nodes] : cases) {
        for (const auto node : nodes) {
            out << "        case " << node << ":\n";
        }
        out << body << "            break;\n";
    }
    out << "        default: break;\n";
    out << "    }\n}\n\n";
}

}  // namespace

bool field_type_persists(const DecoratedProgram& program, const TypeInfo& type) {
    switch (describe_persistence_value(type).kind) {
        case PersistenceValueKind::Enum: {
            const auto* declaration = EnttCodegenUtils::find_enum(program, type.name);
            // An enum deferred to raylib has no generated variant table.
            return declaration != nullptr && !EnttComponentEmitter::emit_enum(*declaration).empty();
        }
        case PersistenceValueKind::Struct:
            return EnttCodegenUtils::find_struct(program, type.name) != nullptr;
        case PersistenceValueKind::List:
            return type.element != nullptr && field_type_persists(program, *type.element);
        case PersistenceValueKind::Unsupported:
            return false;
        default:
            return true;
    }
}

std::vector<const ResolvedField*> persisting_fields(const DecoratedProgram& program,
                                                    const std::vector<ResolvedField>& fields,
                                                    std::optional<bool> persist) {
    std::vector<const ResolvedField*> kept;
    for (const auto& field : fields) {
        if (field_type_persists(program, field.type) && (!persist.has_value() || field.is_persist == *persist)) {
            kept.push_back(&field);
        }
    }
    return kept;
}

std::vector<const ResolvedStruct*> unique_structs(const DecoratedProgram& program) {
    std::vector<const ResolvedStruct*> structs;
    std::unordered_set<std::string> seen;
    for (const auto& [key, declaration] : program.structs) {
        if (seen.insert(declaration_identity(declaration)).second) {
            structs.push_back(&declaration);
        }
    }
    std::ranges::sort(structs, {}, [](const ResolvedStruct* declaration) { return declaration_identity(*declaration); });
    return structs;
}

std::size_t layout_index(const std::vector<PersistableTrait>& layouts, const std::string& canonical) {
    const auto found = std::ranges::find(layouts, canonical, &PersistableTrait::canonical);
    return static_cast<std::size_t>(found - layouts.begin());
}

std::string EnttPersistenceEmitter::emit_world_capture(const DecoratedProgram& program) {
    if (!program_retains_provenance(program)) {
        return {};
    }
    const auto layouts       = persistable_traits(program);
    const auto traits        = without_parent_trait(layouts, program);
    const auto* parent_trait = EnttCodegenUtils::find_trait(program, "Parent");

    std::ostringstream out;
    out << "// ── World Persistence Capture ───────────────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";
    out << "inline constexpr std::array<bool, " << program.persistence.archetypes.size()
        << "> generated_archetype_eligible = {{\n";
    for (const auto& archetype : program.persistence.archetypes) {
        out << "    " << (archetype.declares_persistent_trait ? "true" : "false") << ",\n";
    }
    out << "}};\n\n";

    emit_reference_name_tables(out, program);
    emit_document_tables(out, program, layouts);
    emit_struct_encoders(out, program);
    emit_eligibility(out, traits);
    emit_attached_traits(out, program, layouts, traits);
    emit_absent_traits(out, program, layouts);

    out << "// Read-only: capture never mutates the world, creates entities, or runs\n";
    out << "// gameplay handlers.\n";
    out << "[[nodiscard]] inline cactus::persistence::Snapshot generated_capture_world_snapshot(\n";
    out << "    const entt::registry& registry) {\n";
    out << "    std::vector<std::pair<std::uint64_t, entt::entity>> ordered;\n";
    out << "    if (const auto* origins = registry.storage<ArchetypeOrigin>()) { ordered.reserve(origins->size()); "
           "}\n";
    out << "    for (const auto entity : registry.view<ArchetypeOrigin, CreationOrdinal>()) {\n";
    out << "        if (generated_entity_eligible(registry, entity)) {\n";
    out << "            ordered.emplace_back(registry.get<CreationOrdinal>(entity).value, entity);\n";
    out << "        }\n";
    out << "    }\n";
    out << "    std::ranges::sort(ordered);\n\n";
    out << "    cactus::persistence::SnapshotBuilder out;\n";
    out << "    out.set_schema(generated_schema.revision, generated_schema.fingerprint);\n";
    out << "    out.set_module(\"" << program.module_name << "\");\n";
    out << "    const auto tables = generated_register_document_tables(out);\n";
    out << "    DocumentIdMap ids;\n";
    out << "    ids.reserve(ordered.size());\n";
    out << "    std::vector<cactus::persistence::DocumentId> ordered_ids;\n";
    out << "    ordered_ids.reserve(ordered.size());\n";
    out << "    for (const auto& [ordinal, entity] : ordered) { (void)ordinal; "
           "ordered_ids.push_back(ids.include(entity)); }\n\n";
    out << "    out.expect_entities(ordered.size());\n";
    out << "    for (std::size_t index = 0; index < ordered.size(); ++index) {\n";
    out << "        const auto entity = ordered[index].second;\n";
    out << "        const auto node = registry.get<ArchetypeOrigin>(entity).node;\n";
    out << "        cactus::persistence::EntityRef parent;\n";
    if (parent_trait != nullptr) {
        out << "        if (const auto* link = registry.try_get<"
            << EnttCodegenUtils::trait_cpp_name(parent_trait->symbol_id, parent_trait->name, program)
            << ">(entity)) {\n";
        out << "            parent = ids.reference(link->parent);\n";
        out << "        }\n";
    }
    out << "        out.begin_entity(ordered_ids[index], tables.nodes[node], parent);\n";
    out << "        generated_capture_traits(registry, entity, ids, out, tables);\n";
    out << "        generated_capture_absent_traits(registry, entity, node, out, tables);\n";
    out << "    }\n";
    emit_relation_capture(out, program);
    out << "    if (ids.limit_exceeded()) { out.mark_truncated(); }\n";
    out << "    return std::move(out).finish();\n";
    out << "}\n\n";
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

std::string EnttPersistenceEmitter::emit_reference_name_reverse_lookups(const DecoratedProgram& program) {
    if (!program_retains_provenance(program)) {
        return {};
    }
    std::ostringstream out;
    out << "namespace cactus::runtime::entt_backend {\n\n";
    emit_reference_name_reverse_lookup(
        out, "generated_asset_names", "generated_asset_handle_for_name", "std::uint32_t", 1);
    emit_reference_name_reverse_lookup(
        out, "generated_input_button_names", "generated_input_button_handle_for_name", "std::uint8_t", 0);
    emit_reference_name_reverse_lookup(
        out, "generated_input_axis_names", "generated_input_axis_handle_for_name", "std::uint8_t", 0);
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

std::string EnttPersistenceEmitter::emit_archetype_node_reverse_lookup() {
    std::ostringstream out;
    out << "namespace cactus::runtime::entt_backend {\n\n";
    emit_reference_name_reverse_lookup(
        out, "generated_archetype_nodes", "generated_archetype_node_index", "std::uint32_t", 0);
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

}  // namespace cactus
