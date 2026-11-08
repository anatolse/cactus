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

using GuardMode = cactus_gen::state_transitions::GuardMode;
using Idle      = cactus_gen::state_transitions::GuardMode::Idle;
using Alert     = cactus_gen::state_transitions::GuardMode::Alert;
using Chasing   = cactus_gen::state_transitions::GuardMode::Chasing;
using Down      = cactus_gen::state_transitions::GuardMode::Down;
using Senses    = cactus_gen::state_transitions::Senses;
using Trap      = cactus_gen::state_transitions::Trap;

struct World {
    entt::registry registry;
    entt::entity sentry;
    entt::entity snare;

    World() {
        cactus_headless_test::init_and_load(registry);
        const auto& slots = cactus_gen::detail::generated_named_slots();
        sentry            = slots.state_transitions.Sentry;
        snare             = slots.state_transitions.Snare;
    }

    void sense(bool noise, bool sees) {
        auto& senses = registry.get<Senses>(sentry);
        senses.noise = noise;
        senses.sees  = sees;
    }

    void frame() { cactus_headless_test::drive_frame(registry); }

    [[nodiscard]] int mode() const { return static_cast<int>(registry.get<GuardMode>(sentry).index); }
};

}  // namespace

TEST_CASE("state transitions follow declared arrows through filters, match arms and add to another entity",
          "[runtime][state-transitions]") {
    World world;
    auto& registry = world.registry;
    REQUIRE(registry.all_of<Idle>(world.sentry));

    world.frame();
    CHECK(registry.all_of<Idle>(world.sentry));

    world.sense(true, false);
    world.frame();
    CHECK(registry.all_of<Alert>(world.sentry));
    CHECK(world.mode() == 1);

    world.sense(true, true);
    world.frame();
    CHECK(registry.all_of<Chasing>(world.sentry));

    world.sense(false, false);
    world.frame();
    CHECK(registry.all_of<Idle>(world.sentry));

    world.sense(true, false);
    world.frame();
    REQUIRE(registry.all_of<Alert>(world.sentry));
    world.sense(false, false);
    world.frame();
    CHECK(registry.all_of<Idle>(world.sentry));

    auto& trap  = registry.get<Trap>(world.snare);
    trap.victim = world.sentry;
    trap.sprung = true;
    world.frame();
    CHECK(registry.all_of<Down>(world.sentry));
    CHECK_FALSE(registry.get<Trap>(world.snare).sprung);

    world.sense(true, true);
    world.frame();
    CHECK(registry.all_of<Down>(world.sentry));
    CHECK_FALSE(registry.all_of<Idle, Alert, Chasing>(world.sentry));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
