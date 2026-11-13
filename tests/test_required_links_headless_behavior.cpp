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

namespace game = cactus_gen::required_links;
namespace backend = cactus::runtime::entt_backend;

using Unit   = game::Unit;
using Log    = game::Log;
using Patrol = game::Order::Patrol;
using Attack = game::Order::Attack;
using Lost   = game::Order_Attack_target_lost;  // marks a holder whose target link is lost

// Every holder a pass can see names a live unit.
void require_live_targets(const entt::registry& registry) {
    for (const auto [holder, order] : registry.view<const Attack>().each()) {
        if (!registry.all_of<Lost>(holder)) {
            REQUIRE(registry.valid(order.target));
            REQUIRE(registry.all_of<Unit>(order.target));
        }
    }
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("a required link names a live target until on lost resolves it", "[runtime][required-links]") {
    entt::registry registry;
    backend::clear_runtime_faults();
    cactus_headless_test::init_and_load(registry);
    const auto& slots  = cactus_gen::detail::generated_named_slots();
    const auto goblin  = slots.required_links.Goblin;
    const auto hero    = slots.required_links.Hero;
    const auto knight  = slots.required_links.Knight;
    const auto bowman  = slots.required_links.Bowman;

    REQUIRE(registry.get<Attack>(knight).target == goblin);
    REQUIRE(registry.get<Attack>(bowman).target == goblin);

    {  // attackers read the target's position through the link
        cactus_headless_test::drive_frame(registry);
        require_live_targets(registry);
        CHECK(registry.get<Unit>(knight).x == 4.0F);
        CHECK(registry.get<Log>(knight).hits == 1);
        CHECK(registry.get<Log>(bowman).hits == 1);
    }

    int frames = 0;
    while (registry.valid(goblin) && frames < 10) {
        cactus_headless_test::drive_frame(registry);
        require_live_targets(registry);
        ++frames;
    }
    REQUIRE_FALSE(registry.valid(goblin));

    {  // the melee fighter retreats; the archer picks the hero
        cactus_headless_test::drive_frame(registry);
        require_live_targets(registry);
        CHECK(registry.all_of<Patrol>(knight));
        CHECK_FALSE(registry.all_of<Attack>(knight));
        CHECK(registry.get<Log>(knight).lost == 1);
        REQUIRE(registry.all_of<Attack>(bowman));
        CHECK(registry.get<Attack>(bowman).target == hero);
        CHECK(registry.get<Log>(bowman).lost == 1);
        CHECK_FALSE(registry.all_of<Lost>(bowman));
    }

    {  // the archer keeps hitting its new target; nothing faulted
        const auto hits = registry.get<Log>(bowman).hits;
        cactus_headless_test::drive_frame(registry);
        require_live_targets(registry);
        CHECK(registry.get<Log>(bowman).hits == hits + 1);
        CHECK(registry.get<Log>(knight).lost == 1);
        CHECK(backend::runtime_fault_count() == 0);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
