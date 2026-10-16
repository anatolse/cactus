#pragma once

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

// Monotonic, non-reused per-entity creation order: pair handlers sort their
// binding snapshots by this ordinal so tuple and emitted-event order is
// deterministic and backend-independent. Assigned once at every entity's
// creation site, never reassigned.
struct CreationOrdinal {
    std::uint64_t value{};
};

[[nodiscard]] std::uint64_t generated_next_creation_ordinal() noexcept;

[[nodiscard]] inline std::uint64_t creation_ordinal(const entt::registry& registry, entt::entity entity) {
    return registry.get<CreationOrdinal>(entity).value;
}

// The entities `range` yields, oldest first, whatever order storage holds them in.
template <typename Range>
[[nodiscard]] std::vector<entt::entity> creation_ordered(const entt::registry& registry, const Range& range) {
    std::vector<std::pair<std::uint64_t, entt::entity>> ordered;
    for (const auto entity : range) {
        ordered.emplace_back(creation_ordinal(registry, entity), entity);
    }
    std::ranges::sort(ordered);
    std::vector<entt::entity> result;
    result.reserve(ordered.size());
    for (const auto& [ordinal, entity] : ordered) {
        result.push_back(entity);
    }
    return result;
}

// The oldest entity `range` yields, or null when it yields none.
template <typename Range>
[[nodiscard]] entt::entity oldest(const entt::registry& registry, const Range& range) {
    entt::entity found = entt::null;
    std::uint64_t found_ordinal{};
    for (const auto entity : range) {
        const auto ordinal = creation_ordinal(registry, entity);
        if (found == entt::null || ordinal < found_ordinal) {
            found         = entity;
            found_ordinal = ordinal;
        }
    }
    return found;
}

}  // namespace cactus::runtime::entt_backend
