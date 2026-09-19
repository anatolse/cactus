#pragma once

#include "common/persistence_metadata.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cactus {

// Creation-site provenance for world capture. Archetype creation, spawn
// overrides and runtime trait adds are emitted from two translation units that
// must agree on every answer below, so none of it lives in either one.
class EnttPersistenceEmitter {
public:
    // True when at least one archetype node is ever capturable, i.e. the
    // generated file carries an archetype-node table and creation provenance.
    [[nodiscard]] static bool program_retains_provenance(const DecoratedProgram& program);

    // Index into the generated archetype-node table; nullopt when the node is
    // never capturable and so emits no provenance at its creation site.
    [[nodiscard]] static std::optional<std::uint32_t> archetype_origin_index(
        const DecoratedProgram& program, const SymbolId& archetype, const std::vector<std::string>& role_path = {});

    [[nodiscard]] static bool node_retains_construction(const DecoratedProgram& program,
                                                        const SymbolId& archetype,
                                                        const std::vector<std::string>& role_path = {});

    // A fieldless marker trait is fully recorded by its membership, so it gets
    // no construction copy.
    [[nodiscard]] static bool trait_has_construction_payload(const DecoratedProgram& program,
                                                             const std::optional<SymbolId>& resolved,
                                                             const std::string& source_name);

    // Whether a runtime add/remove of this trait maintains construction data.
    // Add targets are dynamic, so any such site can land on an eligible entity
    // as soon as some archetype retains provenance — and on none when no
    // archetype does, which is what keeps the bound in step with creation.
    [[nodiscard]] static bool structural_site_tracks_construction(const DecoratedProgram& program,
                                                                  const std::optional<SymbolId>& resolved,
                                                                  const std::string& source_name);

    [[nodiscard]] static std::string emit_archetype_origin(std::uint32_t node,
                                                           const std::string& entity_expr,
                                                           const std::string& indent);

    [[nodiscard]] static std::string emit_retain_construction(const std::string& trait_cpp,
                                                              const std::string& entity_expr,
                                                              const std::string& indent);

    [[nodiscard]] static std::string emit_discard_construction(const std::string& trait_cpp,
                                                               const std::string& entity_expr,
                                                               const std::string& indent);

    // The canonical node strings ArchetypeOrigin::node indexes into.
    [[nodiscard]] static std::string emit_archetype_node_table(const DecoratedProgram& program);

    // Finds an entity that carries a durable trait but no provenance, so capture
    // can report a failure instead of writing a record nothing could rebuild.
    [[nodiscard]] static std::string emit_missing_provenance_probe(const DecoratedProgram& program);

    // The adapter-facing schema: what every persistable value in this program
    // means, as static constexpr data, plus a fingerprint that changes whenever
    // a document an older build wrote could no longer be read.
    [[nodiscard]] static std::string emit_schema_descriptor(const DecoratedProgram& program);

    // The canonical rendering the fingerprint is taken over. Ordering is
    // canonicalized here, so declaration order and import aliases cannot reach
    // the hash. Exposed so tests can compare two spellings of one program.
    [[nodiscard]] static std::string canonical_schema_text(const DecoratedProgram& program);

    // The generated capture entry point: eligibility, attached/absent trait
    // recording, hierarchy, and creation-order snapshot assembly. Empty when
    // no archetype ever retains construction data.
    [[nodiscard]] static std::string emit_world_capture(const DecoratedProgram& program);
};

}  // namespace cactus
