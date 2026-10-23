#pragma once

#include "backends/cpp-entt/trait_writers.hpp"

#include "frontend/semantic_analyzer.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cactus {

// The writes that can change a construction value: of a non-persist field, or a projection.
[[nodiscard]] TraitWriterFilter construction_writer_filter(const DecoratedProgram& program);

// Which archetype nodes keep a separate construction copy of a trait. A node
// keeps one only when it retains provenance and some path can change a
// non-persist field of the trait on its entities after they are made; capture
// reads the live value everywhere else, which still equals the construction value.
class ConstructionRetention {
public:
    ConstructionRetention(const DecoratedProgram& program, const std::vector<SymbolId>& traits);

    // `node` indexes program.persistence.archetypes. A trait with no symbol, or
    // one the table was not built for, is kept wherever provenance is.
    [[nodiscard]] bool retains(std::uint32_t node, const std::optional<SymbolId>& trait) const;
    [[nodiscard]] bool retains_anywhere(const std::optional<SymbolId>& trait) const;
    // Whether every node that retains provenance keeps a copy of `trait`.
    [[nodiscard]] bool retains_everywhere(const std::optional<SymbolId>& trait) const;

private:
    [[nodiscard]] const std::vector<bool>& nodes_of(const std::optional<SymbolId>& trait) const;

    std::vector<bool> provenance_;  // per node
    std::unordered_map<SymbolId, std::vector<bool>> nodes_;
};

}  // namespace cactus
