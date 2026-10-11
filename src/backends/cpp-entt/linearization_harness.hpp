#pragma once

// Test-only support for running each dispatch set in another order its
// schedule edges allow, and for comparing the resulting worlds. Generated code
// includes it only under CACTUS_LINEARIZATION_HARNESS.

#include <raylib.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace cactus::runtime::entt_backend {

// `before` and `after` index one dispatch set's call table.
struct LinearizationEdge {
    std::size_t before;
    std::size_t after;
};

struct LinearizationSettings {
    // Unset runs the stable topological order.
    std::optional<std::uint64_t> seed;
    std::uint64_t state = 0;
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

// splitmix64, so a seed picks the same orders on every platform.
inline std::uint64_t next_linearization_random() {
    auto& state = linearization_settings().state;
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state;
    mixed               = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed               = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    return mixed ^ (mixed >> 31U);
}

// Kahn's algorithm choosing uniformly among ready calls, so every linear
// extension of the set's edges can come up.
template <std::size_t EdgeCount, typename Call>
void dispatch_linear_extension(std::size_t count, const std::array<LinearizationEdge, EdgeCount>& edges, const Call& call) {
    const auto& settings = linearization_settings();
    if (!settings.seed.has_value()) {
        for (std::size_t index = 0; index < count; ++index) {
            call(index);
        }
        return;
    }
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

}  // namespace cactus::runtime::entt_backend
