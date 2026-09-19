// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using cactus_headless_test::init_and_load;

template <typename Trait>
entt::entity entity_with(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    REQUIRE(view.begin() != view.end());
    return *view.begin();
}

}  // namespace

TEST_CASE("a SaveRequested emitted from on-load is processed at that same load boundary, not deferred to a frame",
          "[runtime][persistence][scheduling][headless]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_scheduler_state().std_core__fixed_tick = {};

    // init_and_load runs module init, load-time setup, and the `on load:`
    // activation — nothing here drives a frame.
    init_and_load(registry);

    auto& status = registry.get<persistence_save_on_load__SaveStatus>(
        entity_with<persistence_save_on_load__SaveStatus>(registry));
    CHECK(status.requests_sent == 1);
    // Scene transitions obey the same boundary ordering as any other
    // activation: the outcome must already have arrived, without a frame
    // ever running.
    CHECK(status.failed_count == 1);
    CHECK(status.last_code == "adapter_unavailable");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
