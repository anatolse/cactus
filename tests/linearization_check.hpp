#pragma once

// Include after CACTUS_HEADLESS_GENERATED_CPP in a target built with
// LINEARIZATION_HARNESS. Runs one fixture in its stable order and under a
// fixed list of seeds, digesting the world after every frame.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cactus::test {

inline constexpr std::array<std::uint64_t, 24> kLinearizationSeeds{
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};

// `setup(registry)` runs after load, before the first frame.
template <typename Setup>
std::vector<std::uint64_t> linearization_digests(std::optional<std::uint64_t> seed, int frames, const Setup& setup) {
    namespace backend = cactus::runtime::entt_backend;
    backend::generated_reset_scheduler_state();
    backend::set_linearization(seed);
    std::vector<std::uint64_t> digests;
    {
        entt::registry registry;
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
        setup(registry);
        for (int frame = 0; frame < frames; ++frame) {
            backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
            backend::generated_drain_external_events(registry);
            digests.push_back(backend::generated_world_digest(registry));
        }
    }
    backend::set_linearization(std::nullopt);
    backend::generated_reset_scheduler_state();
    return digests;
}

inline auto no_setup() {
    return [](entt::registry&) {};
}

// Every seed must give the stable order's digest after every frame.
template <typename Setup>
void require_linearizations_match(int frames, const Setup& setup) {
    const auto baseline = linearization_digests(std::nullopt, frames, setup);
    for (const auto seed : kLinearizationSeeds) {
        INFO("seed " << seed);
        CHECK(linearization_digests(seed, frames, setup) == baseline);
    }
}

}  // namespace cactus::test
