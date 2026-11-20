#pragma once

// Test-only support for running each dispatch set in another order its
// schedule edges allow, and for comparing the resulting worlds. Generated code
// includes it only under CACTUS_LINEARIZATION_HARNESS.

#include "common/cactus_runtime.hpp"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

// `before` and `after` index one dispatch set's call table.
struct LinearizationEdge {
    std::size_t before;
    std::size_t after;
};

// Two handlers of one dispatch set that its edges leave unordered, by canonical
// identity; `first` comes first in the stable order.
struct LinearizationPair {
    std::string first;
    std::string second;

    friend bool operator==(const LinearizationPair&, const LinearizationPair&) = default;
};

struct LinearizationSettings {
    // Unset runs the stable topological order.
    std::optional<std::uint64_t> seed;
    std::uint64_t state = 0;
    // Runs this pair adjacent, after everything that must precede either.
    std::optional<LinearizationPair> swap;
    bool swapped = false;
    // The unordered pairs of every dispatch set that has run.
    std::vector<LinearizationPair> pairs;
    // Each set lists its pairs once per value, so clearing the list bumps it.
    std::uint64_t listing = 1;
};

inline LinearizationSettings& linearization_settings() {
    static LinearizationSettings settings;
    return settings;
}

inline void set_linearization(std::optional<std::uint64_t> seed) {
    auto& settings = linearization_settings();
    settings.seed  = seed;
    settings.state = seed.value_or(0);
}

inline void set_linearization_swap(std::optional<LinearizationPair> pair, bool swapped = false) {
    auto& settings   = linearization_settings();
    settings.swap    = std::move(pair);
    settings.swapped = swapped;
}

inline const std::vector<LinearizationPair>& linearization_pairs() {
    return linearization_settings().pairs;
}

inline void clear_linearization_pairs() {
    auto& settings = linearization_settings();
    settings.pairs.clear();
    ++settings.listing;
}

// splitmix64, so a seed picks the same orders on every platform.
inline std::uint64_t next_linearization_random() {
    auto& state = linearization_settings().state;
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state;
    mixed               = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed               = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    return mixed ^ (mixed >> 31U);
}

// reaches[a][b]: a must run before b.
template <std::size_t EdgeCount>
std::vector<std::vector<bool>> linearization_reaches(std::size_t count,
                                                    const std::array<LinearizationEdge, EdgeCount>& edges) {
    std::vector<std::vector<bool>> reaches(count, std::vector<bool>(count, false));
    for (const auto& edge : edges) {
        reaches[edge.before][edge.after] = true;
    }
    for (std::size_t via = 0; via < count; ++via) {
        for (std::size_t from = 0; from < count; ++from) {
            if (!reaches[from][via]) {
                continue;
            }
            for (std::size_t to = 0; to < count; ++to) {
                reaches[from][to] = reaches[from][to] || reaches[via][to];
            }
        }
    }
    return reaches;
}

template <std::size_t Count>
void list_linearization_pairs(const std::array<std::string_view, Count>& handlers,
                              const std::vector<std::vector<bool>>& reaches) {
    auto& settings = linearization_settings();
    for (std::size_t first = 0; first < Count; ++first) {
        for (std::size_t second = first + 1; second < Count; ++second) {
            if (!reaches[first][second] && !reaches[second][first]) {
                settings.pairs.push_back(
                    LinearizationPair{.first = std::string(handlers[first]), .second = std::string(handlers[second])});
            }
        }
    }
}

// The stable order with the swap pair adjacent, or nothing when this set does
// not hold both handlers unordered.
template <std::size_t Count>
std::optional<std::vector<std::size_t>> linearization_swap_order(const std::array<std::string_view, Count>& handlers,
                                                                 const std::vector<std::vector<bool>>& reaches) {
    const auto& settings = linearization_settings();
    if (!settings.swap.has_value()) {
        return std::nullopt;
    }
    const auto first  = std::ranges::find(handlers, settings.swap->first);
    const auto second = std::ranges::find(handlers, settings.swap->second);
    if (first == handlers.end() || second == handlers.end() || first == second) {
        return std::nullopt;
    }
    auto low  = static_cast<std::size_t>(std::min(first, second) - handlers.begin());
    auto high = static_cast<std::size_t>(std::max(first, second) - handlers.begin());
    if (reaches[low][high]) {
        return std::nullopt;
    }
    std::vector<std::size_t> order;
    std::vector<std::size_t> rest;
    for (std::size_t index = 0; index < Count; ++index) {
        if (index == low || index == high) {
            continue;
        }
        (reaches[index][low] || reaches[index][high] ? order : rest).push_back(index);
    }
    if (settings.swapped) {
        std::swap(low, high);
    }
    order.push_back(low);
    order.push_back(high);
    order.insert(order.end(), rest.begin(), rest.end());
    return order;
}

// Kahn's algorithm choosing uniformly among ready calls, so every linear
// extension of the set's edges can come up.
template <std::size_t EdgeCount, typename Call>
void dispatch_seeded_extension(std::size_t count,
                               const std::array<LinearizationEdge, EdgeCount>& edges,
                               const Call& call) {
    std::vector<std::size_t> pending(count, 0);
    for (const auto& edge : edges) {
        ++pending[edge.after];
    }
    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < count; ++index) {
        if (pending[index] == 0) {
            ready.push_back(index);
        }
    }
    while (!ready.empty()) {
        const auto pick  = static_cast<std::ptrdiff_t>(next_linearization_random() % ready.size());
        const auto index = ready[static_cast<std::size_t>(pick)];
        ready.erase(ready.begin() + pick);
        call(index);
        for (const auto& edge : edges) {
            if (edge.before == index && --pending[edge.after] == 0) {
                ready.push_back(edge.after);
            }
        }
    }
}

// `handlers` holds the set's canonical handler identities in stable order.
template <std::size_t Count, std::size_t EdgeCount, typename Call>
void dispatch_linear_extension(const std::array<std::string_view, Count>& handlers,
                               const std::array<LinearizationEdge, EdgeCount>& edges,
                               const Call& call) {
    // Each call site has its own `Call` type, so these statics belong to one set.
    static const auto reaches = linearization_reaches(Count, edges);
    static std::uint64_t listed = 0;
    if (listed != linearization_settings().listing) {
        listed = linearization_settings().listing;
        list_linearization_pairs(handlers, reaches);
    }
    if (const auto order = linearization_swap_order(handlers, reaches)) {
        for (const auto index : *order) {
            call(index);
        }
        return;
    }
    if (linearization_settings().seed.has_value()) {
        dispatch_seeded_extension(Count, edges, call);
        return;
    }
    for (std::size_t index = 0; index < Count; ++index) {
        call(index);
    }
}

// FNV-1a over field values; floats hash by bit pattern.
class WorldDigest {
public:
    void bytes(const void* data, std::size_t size) {
        const auto* first = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash_ = (hash_ ^ first[i]) * 1099511628211ULL;
        }
    }

    template <typename T>
    void add(const T& value) {
        digest_value(*this, value);
    }

    [[nodiscard]] std::uint64_t value() const {
        return hash_;
    }

private:
    std::uint64_t hash_ = 14695981039346656037ULL;
};

template <typename T>
    requires(std::is_integral_v<T> || std::is_enum_v<T>)
void digest_value(WorldDigest& digest, T value) {
    const auto widened = static_cast<std::uint64_t>(value);
    digest.bytes(&widened, sizeof widened);
}

inline void digest_value(WorldDigest& digest, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    digest.bytes(&bits, sizeof bits);
}

inline void digest_value(WorldDigest& digest, Duration value) {
    digest.add(value.units);
}

inline void digest_value(WorldDigest& digest, const std::string& value) {
    digest.add(value.size());
    digest.bytes(value.data(), value.size());
}

inline void digest_value(WorldDigest& digest, const Vector2& value) {
    digest.add(value.x);
    digest.add(value.y);
}

inline void digest_value(WorldDigest& digest, const Vector3& value) {
    digest.add(value.x);
    digest.add(value.y);
    digest.add(value.z);
}

inline void digest_value(WorldDigest& digest, const Vector4& value) {
    digest.add(value.x);
    digest.add(value.y);
    digest.add(value.z);
    digest.add(value.w);
}

inline void digest_value(WorldDigest& digest, const Color& value) {
    digest.add(value.r);
    digest.add(value.g);
    digest.add(value.b);
    digest.add(value.a);
}

template <typename T>
void digest_value(WorldDigest& digest, const std::vector<T>& values) {
    digest.add(values.size());
    for (const auto& value : values) {
        digest.add(value);
    }
}

// A column digests its stored cells in cell order, so equal tables digest equally.
template <typename T, T Default, std::size_t DenseSize>
void digest_value(WorldDigest& digest, const CellColumn<T, Default, DenseSize>& column) {
    std::vector<std::uint64_t> cells;
    column.append_stored(cells);
    digest.add(cells.size());
    for (const auto cell : cells) {
        digest.add(cell);
        digest.add(column.get(cell));
    }
}

}  // namespace cactus::runtime::entt_backend
