#pragma once

#include "common/error_reporter.hpp"
#include "common/persistence_schema.hpp"
#include "common/types.hpp"
#include "frontend/symbol_identity.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Per-module facts world capture needs that no existing decorated declaration
// already carries. Anything derivable from a ResolvedTrait/ResolvedStruct
// (field modifiers, types, defaults) is read back through those, not copied.

namespace cactus {

struct ResolvedTrait;
struct DecoratedProgram;

// A template, an authored entity, or a nested child role addressed by its
// sibling-scoped role path. Never a generated index or a source line.
struct ArchetypeNodeId {
    SymbolId archetype;                  // SymbolKind::Template or SymbolKind::Entity
    std::vector<std::string> role_path;  // empty for the root node

    friend bool operator==(const ArchetypeNodeId&, const ArchetypeNodeId&) = default;
};

// "game.Boss" for a root node, "game.Tank/turret/barrel" for a nested role.
[[nodiscard]] std::string canonical_node_string(const ArchetypeNodeId& node);

// A declaration's resolved symbol, else its module-local identity. Metadata
// construction and codegen must agree on it, so both resolve it here.
[[nodiscard]] SymbolId resolved_or_local_symbol(SymbolKind kind,
                                                const std::optional<SymbolId>& resolved,
                                                const std::string& module_name,
                                                const std::string& local_name);

// Exact backend representation of one value, resolved from a declared TypeInfo.
// `element` holds 0 or 1 entries (a list's element type) so the whole record
// stays comparable by value.
struct PersistenceValueType {
    PersistenceValueKind kind      = PersistenceValueKind::Unsupported;
    std::uint8_t bit_width         = 0;  // scalar width, or a vector lane's width
    std::uint8_t lanes             = 1;  // vector lane count (vec2 = 2, quat/color = 4)
    PersistenceValueKind lane_kind = PersistenceValueKind::Float;
    std::optional<SymbolId> declared_type;  // Enum/Struct identity
    std::vector<PersistenceValueType> element;

    friend bool operator==(const PersistenceValueType&, const PersistenceValueType&) = default;
};

[[nodiscard]] PersistenceValueType describe_persistence_value(const TypeInfo& type);

// Values live in generated construction code; the descriptor only records which
// fields the declaration pins.
struct PersistenceBaselineTrait {
    SymbolId trait;
    std::vector<std::string> assigned_fields;  // sorted

    friend bool operator==(const PersistenceBaselineTrait&, const PersistenceBaselineTrait&) = default;
};

struct PersistenceArchetypeDescriptor {
    ArchetypeNodeId node;
    std::vector<PersistenceBaselineTrait> baseline_traits;  // sorted by canonical trait id
    std::vector<std::string> parameters;                    // template parameters, declaration order
    std::vector<std::string> child_roles;                   // direct children, declaration order
    // Eligibility through this node's own declared trait set; survives runtime
    // removal of the trait that granted it.
    bool declares_persistent_trait = false;

    friend bool operator==(const PersistenceArchetypeDescriptor&, const PersistenceArchetypeDescriptor&) = default;
};

struct ModulePersistenceMetadata {
    std::vector<PersistenceArchetypeDescriptor> archetypes;  // sorted by canonical node string
    // This module holds an authored `add` or a native add capability for a trait
    // that declares a persist field. OR-ed across modules by the linker.
    bool attaches_persistent_trait = false;

    friend bool operator==(const ModulePersistenceMetadata&, const ModulePersistenceMetadata&) = default;
};

// Restores the canonical ordering `ArchetypeOrigin::node` indexes into, so the
// analyzer and the linker cannot drift apart on it.
void sort_archetype_descriptors(std::vector<PersistenceArchetypeDescriptor>& archetypes);

// One persist field the schema cannot represent, reported by field path rather
// than silently dropped from the document.
struct PersistenceUnsupportedField {
    std::string field_path;     // "game.Health.history[]" — trait, field, then nesting
    std::string type_spelling;  // the declared type as written

    friend bool operator==(const PersistenceUnsupportedField&, const PersistenceUnsupportedField&) = default;
};

[[nodiscard]] std::vector<PersistenceUnsupportedField> collect_unsupported_persistence_fields(
    const DecoratedProgram& program);

// Turns collect_unsupported_persistence_fields's findings into compile
// errors: an unsupported persist field type is reported by field path rather
// than silently dropped from the schema.
void report_unsupported_persistence_fields(const DecoratedProgram& program, ErrorReporter& errors);

[[nodiscard]] bool trait_declares_persist_field(const ResolvedTrait& trait);

// The key a declaration is deduplicated under while walking DecoratedProgram's
// maps, which hold the same declaration under both its canonical and its simple
// name.
template <typename Declaration>
[[nodiscard]] const std::string& declaration_identity(const Declaration& declaration) {
    return declaration.canonical_id.empty() ? declaration.name : declaration.canonical_id;
}

// An archetype pays for construction metadata only when the program can ever
// capture one of its instances.
[[nodiscard]] bool archetype_retains_construction_data(const PersistenceArchetypeDescriptor& archetype,
                                                       bool program_attaches_persistent_trait);

[[nodiscard]] const PersistenceArchetypeDescriptor* find_archetype_descriptor(const ModulePersistenceMetadata& metadata,
                                                                              const ArchetypeNodeId& node);

}  // namespace cactus
