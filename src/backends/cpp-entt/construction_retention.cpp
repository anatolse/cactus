#include "backends/cpp-entt/construction_retention.hpp"

#include "backends/cpp-entt/index_cell_size.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include "common/persistence_metadata.hpp"

#include <algorithm>
#include <cstddef>

namespace cactus {

namespace {

const ResolvedTrait* declaration_of(const DecoratedProgram& program, const SymbolId& trait) {
    return EnttCodegenUtils::find_trait(program, make_canonical_id(trait));
}

bool touches_construction_field(const DecoratedProgram& program, const SymbolId& trait, const FieldAccess& access) {
    const auto* declaration = declaration_of(program, trait);
    if (declaration == nullptr) {
        return true;
    }
    return std::ranges::any_of(declaration->fields, [&](const ResolvedField& field) {
        return !field.is_persist && access.covers(field.name);
    });
}

// Indexes into program.reachability.origins per archetype node; size() when the node has none.
std::vector<std::size_t> origin_of_nodes(const DecoratedProgram& program) {
    const auto& origins = program.reachability.origins;
    std::vector<std::size_t> result;
    result.reserve(program.persistence.archetypes.size());
    for (const auto& archetype : program.persistence.archetypes) {
        const auto found = std::ranges::find(origins, archetype.node, &OriginReachability::node);
        result.push_back(static_cast<std::size_t>(found - origins.begin()));
    }
    return result;
}

}  // namespace

TraitWriterFilter construction_writer_filter(const DecoratedProgram& program) {
    return TraitWriterFilter{
        .fields      = [&program](const SymbolId& trait,
                             const FieldAccess& access) { return touches_construction_field(program, trait, access); },
        .projections = true,
    };
}

ConstructionRetention::ConstructionRetention(const DecoratedProgram& program, const std::vector<SymbolId>& traits) {
    const auto& archetypes = program.persistence.archetypes;
    provenance_.reserve(archetypes.size());
    for (const auto& archetype : archetypes) {
        provenance_.push_back(
            archetype_retains_construction_data(archetype, program.persistence.attaches_persistent_trait));
    }
    // Without per-trait answers every trait is kept wherever provenance is.
    if (program.retain_all_construction || !program.reachability.derived ||
        !std::ranges::contains(provenance_, true)) {
        return;
    }
    const auto origins = origin_of_nodes(program);
    const auto filter  = construction_writer_filter(program);
    for (const auto& trait : traits) {
        const auto* declaration = declaration_of(program, trait);
        auto& nodes             = nodes_[trait];
        nodes.assign(archetypes.size(), false);
        if (declaration == nullptr || declaration->fields.empty()) {
            continue;
        }
        std::vector<bool> written(program.reachability.origins.size(), false);
        const auto writers = trait_writers(program, {trait}, filter);
        for (auto required : writers.target_sets) {
            required.push_back(trait);
            for (const auto origin : member_origins(program, required)) {
                written[origin] = true;
            }
        }
        for (std::size_t node = 0; node < archetypes.size(); ++node) {
            const bool unknown = origins[node] == written.size();
            const bool named   = std::ranges::contains(writers.named_entities, archetypes[node].node.archetype);
            nodes[node] = provenance_[node] && (unknown || named || written[origins[node]]);
        }
    }
}

const std::vector<bool>& ConstructionRetention::nodes_of(const std::optional<SymbolId>& trait) const {
    const auto found = trait.has_value() ? nodes_.find(*trait) : nodes_.end();
    return found == nodes_.end() ? provenance_ : found->second;
}

bool ConstructionRetention::retains(std::uint32_t node, const std::optional<SymbolId>& trait) const {
    const auto& nodes = nodes_of(trait);
    return node < nodes.size() && nodes[node];
}

bool ConstructionRetention::retains_anywhere(const std::optional<SymbolId>& trait) const {
    return std::ranges::contains(nodes_of(trait), true);
}

bool ConstructionRetention::retains_everywhere(const std::optional<SymbolId>& trait) const {
    const auto& nodes = nodes_of(trait);
    for (std::size_t node = 0; node < provenance_.size(); ++node) {
        if (provenance_[node] && !nodes[node]) {
            return false;
        }
    }
    return true;
}

}  // namespace cactus
