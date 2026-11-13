// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace {

namespace game    = cactus_gen::required_link_faults;
namespace backend = cactus::runtime::entt_backend;

std::size_t faults_of(backend::RangeFaultReason reason) {
    return static_cast<std::size_t>(std::ranges::count(backend::runtime_faults(), reason, &backend::RangeFault::reason));
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("a link to a dead target faults, and so does an unresolved loss", "[runtime][required-links]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    backend::clear_runtime_faults();
    const auto& slots    = cactus_gen::detail::generated_named_slots().required_link_faults;
    const auto attacker  = slots.Attacker;

    const auto units = [&] { return registry.view<game::Unit>().size(); };

    {  // tick 1: the add and the spawn name a victim destroyed earlier in the round, so both do nothing
        cactus_headless_test::drive_frame(registry);
        CHECK_FALSE(registry.valid(slots.Victim));
        CHECK(registry.all_of<game::Order::Idle>(attacker));
        CHECK(units() == 4);
        CHECK(faults_of(backend::RangeFaultReason::DeadLink) == 2);
    }

    {  // tick 2: a live target links; the quarry and its stalker die together, so nothing is lost
        cactus_headless_test::drive_frame(registry);
        REQUIRE(registry.all_of<game::Order::Attack>(attacker));
        CHECK(registry.get<game::Order::Attack>(attacker).target == slots.Spare);
        CHECK_FALSE(registry.valid(slots.Quarry));
        CHECK_FALSE(registry.valid(slots.Stalker));
        CHECK(registry.view<game::Order_Attack_target_lost>().empty());
        CHECK(backend::runtime_fault_count() == 2);
    }

    {  // tick 3: the spare dies; on lost retries the dead victim, which fails, so the loss stays unresolved
        cactus_headless_test::drive_frame(registry);
        CHECK(registry.get<game::Script>(attacker).lost == 1);
        CHECK(registry.all_of<game::Order::Attack>(attacker));
        CHECK(registry.all_of<game::Order_Attack_target_lost>(attacker));
        CHECK(faults_of(backend::RangeFaultReason::DeadLink) == 3);
        CHECK(faults_of(backend::RangeFaultReason::UnresolvedLink) == 1);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
