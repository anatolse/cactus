#include "backends/cpp-entt/persistence_emitter.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include "common/persistence_document.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace cactus {

namespace {

constexpr const char* kRuntime = "cactus::runtime::entt_backend::";

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

bool EnttPersistenceEmitter::node_retains_construction(const DecoratedProgram& program,
                                                       const SymbolId& archetype,
                                                       const std::vector<std::string>& role_path) {
    return archetype_origin_index(program, archetype, role_path).has_value();
}

bool EnttPersistenceEmitter::trait_has_construction_payload(const DecoratedProgram& program,
                                                            const std::optional<SymbolId>& resolved,
                                                            const std::string& source_name) {
    const auto* declaration = EnttCodegenUtils::find_trait(
        program, resolved.has_value() ? make_canonical_id(*resolved) : source_name);
    return declaration != nullptr && !declaration->fields.empty();
}

bool EnttPersistenceEmitter::structural_site_tracks_construction(const DecoratedProgram& program,
                                                                 const std::optional<SymbolId>& resolved,
                                                                 const std::string& source_name) {
    return program_retains_provenance(program) && trait_has_construction_payload(program, resolved, source_name);
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

struct SchemaModel {
    std::vector<FlatValueType> value_types;
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

    for (const auto* archetype : model.archetypes) {
        const auto node = canonical_node_string(archetype->node);
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
            << "_baseline, .parameters = " << array_name_for(node) << "_parameters},\n";
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

std::string enum_variant_function(const std::string& enum_cpp) {
    return "generated_variant_" + enum_cpp;
}

std::string struct_capture_function(const std::string& struct_cpp) {
    return "generated_capture_" + struct_cpp;
}

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

// C++ text producing a persistence::Value for one field access. Empty when the
// schema cannot yet represent the type, so the caller omits the field rather
// than recording something wrong for it.
std::string capture_expression(const DecoratedProgram& program, const TypeInfo& type, const std::string& access) {
    switch (describe_persistence_value(type).kind) {
        case PersistenceValueKind::Bool:
        case PersistenceValueKind::Int:
        case PersistenceValueKind::Float:
        case PersistenceValueKind::String:
        case PersistenceValueKind::Vector:
            return "cactus::runtime::entt_backend::to_persistence_value(" + access + ")";
        case PersistenceValueKind::EntityRef:
            return "cactus::persistence::Value::of_entity(ids.reference(" + access + "))";
        case PersistenceValueKind::Enum: {
            const auto* declaration = EnttCodegenUtils::find_enum(program, type.name);
            if (declaration == nullptr || EnttComponentEmitter::emit_enum(*declaration).empty()) {
                return {};  // deferred to a raylib enum: no generated variant names
            }
            return "cactus::persistence::Value::of_enum(\"" + declaration_identity(*declaration) + "\", std::string(" +
                   enum_variant_function(canonical_to_cpp_name(declaration->module_name, declaration->name)) + "(" +
                   access + ")))";
        }
        case PersistenceValueKind::Struct: {
            const auto* declaration = EnttCodegenUtils::find_struct(program, type.name);
            if (declaration == nullptr) {
                return {};
            }
            return struct_capture_function(canonical_to_cpp_name(declaration->module_name, declaration->name)) + "(" +
                   access + ", ids)";
        }
        case PersistenceValueKind::List: {
            if (type.element == nullptr) {
                return {};
            }
            const auto element = capture_expression(program, *type.element, "item");
            if (element.empty()) {
                return {};
            }
            return "[&]() { cactus::persistence::ListValue items; items.items.reserve(std::min<std::size_t>(" +
                   access + ".size(), cactus::persistence::kMaxPersistenceCollectionSize)); for (const auto& item : " +
                   access +
                   ") { if (items.items.size() >= cactus::persistence::kMaxPersistenceCollectionSize) { "
                   "ids.mark_limit_exceeded(); break; } items.items.push_back(" + element +
                   "); } return cactus::persistence::Value::of_list(std::move(items)); }()";
        }
        case PersistenceValueKind::AssetRef:
            return "cactus::persistence::Value::of_asset(std::string(generated_asset_name(" + access + ")))";
        case PersistenceValueKind::InputRef:
            if (type.kind == TypeKind::InputAxis) {
                return "cactus::persistence::Value::of_input(std::string(generated_input_axis_name(" + access +
                       ")))";
            }
            return "cactus::persistence::Value::of_input(std::string(generated_input_button_name(" + access + ")))";
        case PersistenceValueKind::Unsupported:
            return {};
    }
    return {};
}

void emit_field_capture(std::ostringstream& out,
                        const DecoratedProgram& program,
                        const ResolvedField& field,
                        const std::string& access,
                        const std::string& target,
                        const std::string& indent) {
    const auto expression = capture_expression(program, field.type, access + field.name);
    if (expression.empty()) {
        return;
    }
    out << indent << target << ".push_back(cactus::persistence::FieldValue{.name = \"" << field.name
        << "\", .value = " << expression << "});\n";
}

void emit_enum_variant_functions(std::ostringstream& out, const DecoratedProgram& program) {
    std::unordered_set<std::string> seen;
    for (const auto& [key, declaration] : program.enums) {
        if (!seen.insert(declaration_identity(declaration)).second ||
            EnttComponentEmitter::emit_enum(declaration).empty()) {
            continue;
        }
        const auto enum_cpp = canonical_to_cpp_name(declaration.module_name, declaration.name);
        out << "[[nodiscard]] inline std::string_view " << enum_variant_function(enum_cpp) << "(" << enum_cpp
            << " value) {\n";
        out << "    switch (value) {\n";
        for (const auto& variant : declaration.variants) {
            out << "        case " << enum_cpp << "::" << variant << ": return \"" << variant << "\";\n";
        }
        out << "    }\n    return \"\";\n}\n\n";
    }
}

void emit_struct_capture_functions(std::ostringstream& out, const DecoratedProgram& program) {
    std::unordered_set<std::string> seen;
    for (const auto& [key, declaration] : program.structs) {
        if (!seen.insert(declaration_identity(declaration)).second) {
            continue;
        }
        const auto struct_cpp = canonical_to_cpp_name(declaration.module_name, declaration.name);
        out << "[[nodiscard]] inline cactus::persistence::Value " << struct_capture_function(struct_cpp) << "(const "
            << struct_cpp << "& value, DocumentIdMap& ids) {\n";
        out << "    if (!ids.enter_nesting()) { return cactus::persistence::Value{}; }\n";
        out << "    cactus::persistence::StructValue fields;\n";
        const auto field_count =
            std::ranges::count_if(declaration.fields, [&program](const auto& field) {
                return !capture_expression(program, field.type, "x").empty();
            });
        if (field_count > 0) {
            out << "    fields.fields.reserve(" << field_count << ");\n";
        }
        for (const auto& field : declaration.fields) {
            emit_field_capture(out, program, field, "value.", "fields.fields", "    ");
        }
        out << "    ids.exit_nesting();\n";
        out << "    return cactus::persistence::Value::of_struct(std::move(fields));\n}\n\n";
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

struct SupportedFieldCounts {
    std::size_t construction = 0;
    std::size_t persisted    = 0;
};

// How many of a trait's fields will actually reach `emit_field_capture`, split
// by construction vs. persisted — known at codegen time, so the generated
// vectors can be sized once instead of growing by repeated reallocation.
SupportedFieldCounts count_supported_fields(const DecoratedProgram& program, const ResolvedTrait& trait) {
    SupportedFieldCounts counts;
    for (const auto& field : trait.fields) {
        if (capture_expression(program, field.type, "x").empty()) {
            continue;
        }
        (field.is_persist ? counts.persisted : counts.construction)++;
    }
    return counts;
}

void emit_attached_traits(std::ostringstream& out,
                          const DecoratedProgram& program,
                          const std::vector<PersistableTrait>& traits) {
    out << "inline void generated_capture_traits(const entt::registry& registry,\n";
    out << "                                     entt::entity entity,\n";
    out << "                                     DocumentIdMap& ids,\n";
    out << "                                     cactus::persistence::EntityRecord& record) {\n";
    out << "    (void)ids;\n";
    for (const auto& trait : traits) {
        out << "    if (!::projected_" << trait.cpp_name << ".is_projected(entity)) {\n";
        if (trait.declaration->fields.empty()) {
            out << "        if (registry.all_of<" << trait.cpp_name << ">(entity)) {\n";
            out << "            record.traits.push_back(cactus::persistence::TraitRecord{.trait = \""
                << trait.canonical << "\"});\n";
            out << "        }\n";
        } else {
            const auto counts = count_supported_fields(program, *trait.declaration);
            out << "        if (const auto* live = registry.try_get<" << trait.cpp_name << ">(entity)) {\n";
            out << "            (void)live;\n";
            out << "            cactus::persistence::TraitRecord attached{.trait = \"" << trait.canonical << "\"};\n";
            if (counts.construction > 0) {
                out << "            attached.construction.reserve(" << counts.construction << ");\n";
            }
            if (counts.persisted > 0) {
                out << "            attached.persisted.reserve(" << counts.persisted << ");\n";
            }
            out << "            if (const auto* built = registry.try_get<Construction<" << trait.cpp_name
                << ">>(entity)) {\n";
            out << "                (void)built;\n";
            for (const auto& field : trait.declaration->fields) {
                if (!field.is_persist) {
                    emit_field_capture(out, program, field, "built->value.", "attached.construction",
                                       "                ");
                }
            }
            out << "            }\n";
            for (const auto& field : trait.declaration->fields) {
                if (field.is_persist) {
                    emit_field_capture(out, program, field, "live->", "attached.persisted", "            ");
                }
            }
            out << "            record.traits.push_back(std::move(attached));\n";
            out << "        }\n";
        }
        out << "    }\n";
    }
    out << "}\n\n";
}

void emit_absent_traits(std::ostringstream& out, const DecoratedProgram& program) {
    out << "// A baseline trait gameplay removed is recorded as absent: membership\n";
    out << "// information, no payloads.\n";
    out << "inline void generated_capture_absent_traits(const entt::registry& registry,\n";
    out << "                                           entt::entity entity,\n";
    out << "                                           std::uint32_t node,\n";
    out << "                                           cactus::persistence::EntityRecord& record) {\n";
    out << "    switch (node) {\n";
    std::uint32_t node_index = 0;
    for (const auto& archetype : program.persistence.archetypes) {
        if (archetype_retains_construction_data(archetype, program.persistence.attaches_persistent_trait)) {
            out << "        case " << node_index << ":\n";
            for (const auto& baseline : archetype.baseline_traits) {
                out << "            if (!registry.all_of<" << EnttCodegenUtils::trait_cpp_name(baseline.trait)
                    << ">(entity)) {\n";
                out << "                record.traits.push_back(cactus::persistence::TraitRecord{.trait = \""
                    << make_canonical_id(baseline.trait) << "\", .present = false});\n";
                out << "            }\n";
            }
            out << "            break;\n";
        }
        ++node_index;
    }
    out << "        default: break;\n";
    out << "    }\n}\n\n";
}

}  // namespace

std::string EnttPersistenceEmitter::emit_world_capture(const DecoratedProgram& program) {
    if (!program_retains_provenance(program)) {
        return {};
    }
    const auto traits        = without_parent_trait(persistable_traits(program), program);
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
    emit_enum_variant_functions(out, program);
    emit_struct_capture_functions(out, program);
    emit_eligibility(out, traits);
    emit_attached_traits(out, program, traits);
    emit_absent_traits(out, program);

    out << "// Read-only: capture never mutates the world, creates entities, or runs\n";
    out << "// gameplay handlers.\n";
    out << "[[nodiscard]] inline cactus::persistence::Snapshot generated_capture_world_snapshot(\n";
    out << "    const entt::registry& registry) {\n";
    out << "    cactus::persistence::Snapshot snapshot;\n";
    out << "    snapshot.schema_revision    = generated_schema.revision;\n";
    out << "    snapshot.schema_fingerprint = generated_schema.fingerprint;\n";
    out << "    snapshot.module             = \"" << program.module_name << "\";\n\n";
    out << "    std::vector<std::pair<std::uint64_t, entt::entity>> ordered;\n";
    out << "    if (const auto* origins = registry.storage<ArchetypeOrigin>()) { ordered.reserve(origins->size()); "
           "}\n";
    out << "    for (const auto entity : registry.view<ArchetypeOrigin, CreationOrdinal>()) {\n";
    out << "        if (generated_entity_eligible(registry, entity)) {\n";
    out << "            ordered.emplace_back(registry.get<CreationOrdinal>(entity).value, entity);\n";
    out << "        }\n";
    out << "    }\n";
    out << "    std::ranges::sort(ordered);\n\n";
    out << "    DocumentIdMap ids;\n";
    out << "    ids.reserve(ordered.size());\n";
    out << "    snapshot.entities.reserve(ordered.size());\n";
    out << "    std::vector<cactus::persistence::DocumentId> ordered_ids;\n";
    out << "    ordered_ids.reserve(ordered.size());\n";
    out << "    for (const auto& [ordinal, entity] : ordered) { (void)ordinal; "
           "ordered_ids.push_back(ids.include(entity)); }\n\n";
    out << "    for (std::size_t index = 0; index < ordered.size(); ++index) {\n";
    out << "        const auto entity = ordered[index].second;\n";
    out << "        const auto node = registry.get<ArchetypeOrigin>(entity).node;\n";
    out << "        cactus::persistence::EntityRecord record;\n";
    out << "        record.id        = ordered_ids[index];\n";
    out << "        record.archetype = std::string(generated_archetype_nodes[node]);\n";
    if (parent_trait != nullptr) {
        out << "        if (const auto* parent = registry.try_get<"
            << EnttCodegenUtils::trait_cpp_name(parent_trait->symbol_id, parent_trait->name, program)
            << ">(entity)) {\n";
        out << "            record.parent = ids.reference(parent->parent);\n";
        out << "        }\n";
    }
    out << "        generated_capture_traits(registry, entity, ids, record);\n";
    out << "        generated_capture_absent_traits(registry, entity, node, record);\n";
    out << "        snapshot.entities.push_back(std::move(record));\n";
    out << "    }\n";
    out << "    snapshot.truncated = ids.limit_exceeded();\n";
    out << "    return snapshot;\n";
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
