// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {

using Mode      = exclusive_states__EnemyMode;
using Idle      = exclusive_states__EnemyMode__Idle;
using Chasing   = exclusive_states__EnemyMode__Chasing;
using Attacking = exclusive_states__EnemyMode__Attacking;
using Dying     = exclusive_states__EnemyMode__Dying;
using Orders    = exclusive_states__Orders;
using Order     = exclusive_states__Order;
using Log       = exclusive_states__Log;
using Watcher   = exclusive_states__Watcher;

// A carrier holds exactly the variant its slot names; a non-carrier holds none.
void require_one_variant(const entt::registry& registry, entt::entity entity) {
    const int count =
        static_cast<int>(registry.all_of<Idle>(entity)) + static_cast<int>(registry.all_of<Chasing>(entity)) +
        static_cast<int>(registry.all_of<Attacking>(entity)) + static_cast<int>(registry.all_of<Dying>(entity));
    const auto* slot = registry.try_get<Mode>(entity);
    if (slot == nullptr) {
        REQUIRE(count == 0);
        return;
    }
    REQUIRE(count == 1);
    switch (slot->index) {
        case 0:
            REQUIRE(registry.all_of<Idle>(entity));
            break;
        case 1:
            REQUIRE(registry.all_of<Chasing>(entity));
            break;
        case 2:
            REQUIRE(registry.all_of<Attacking>(entity));
            break;
        case 3:
            REQUIRE(registry.all_of<Dying>(entity));
            break;
        default:
            FAIL("slot index out of range");
    }
}

struct World {
    entt::registry registry;
    std::vector<entt::entity> watched;

    World() {
        cactus_headless_test::init_and_load(registry);
        const auto& slots = generated_named_slots();
        watched = {slots.exclusive_states__Grunt, slots.exclusive_states__Boss, slots.exclusive_states__Drifter};
        check_invariant();
    }

    void check_invariant() {
        for (const auto entity : watched) {
            if (registry.valid(entity)) {
                require_one_variant(registry, entity);
            }
        }
    }

    void order(entt::entity entity, Order first, Order second = Order::Stay) {
        auto& orders  = registry.get<Orders>(entity);
        orders.first  = first;
        orders.second = second;
    }

    void frame() {
        cactus_headless_test::drive_frame(registry);
        check_invariant();
    }

    Log& log(entt::entity entity) {
        return registry.get<Log>(entity);
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("exclusive states keep one variant per carrier through every transition", "[runtime][exclusive-states]") {
    World world;
    auto& registry     = world.registry;
    const auto& slots  = generated_named_slots();
    const auto grunt   = slots.exclusive_states__Grunt;
    const auto boss    = slots.exclusive_states__Boss;
    const auto drifter = slots.exclusive_states__Drifter;
    const auto player  = slots.exclusive_states__Player;
    const auto lookout = slots.exclusive_states__Lookout;

    {  // a bare state starts in the first variant
        REQUIRE(registry.all_of<Idle>(grunt));
        CHECK(registry.get<Mode>(grunt).index == 0);
        CHECK(world.log(grunt).idle_added == 1);
    }

    {  // an entity variant replaces its template's
        REQUIRE(registry.all_of<Chasing>(boss));
        CHECK_FALSE(registry.all_of<Idle>(boss));
        CHECK(registry.get<Chasing>(boss).target == player);
        CHECK(world.log(boss).hunted_hero);
        CHECK_FALSE(registry.all_of<Mode>(drifter));
    }

    {  // rules select by variant, by slot, and by excluding either
        world.frame();
        CHECK(world.log(boss).chase_ticks == 1);
        CHECK(world.log(grunt).chase_ticks == 0);
        CHECK(world.log(grunt).awake_ticks == 1);
        CHECK(world.log(drifter).stateless_ticks == 1);
        CHECK(world.log(grunt).stateless_ticks == 0);
        CHECK(world.log(grunt).code == 10);
        CHECK(world.log(boss).code == 20);
        CHECK(world.log(grunt).arm == 1);
        CHECK(world.log(boss).arm == 2);
        CHECK(world.log(drifter).code == 0);
    }

    {  // add swaps the variant and fires removed before added
        world.order(grunt, Order::Chase);
        world.frame();
        REQUIRE(registry.all_of<Chasing>(grunt));
        CHECK(registry.get<Chasing>(grunt).target == player);
        CHECK(world.log(grunt).idle_left == 1);
        CHECK(world.log(grunt).chasing_added == 1);
        CHECK(world.log(grunt).idle_left_at < world.log(grunt).chasing_added_at);
    }

    {  // two transitions in one round net out
        world.order(grunt, Order::Idle);
        world.frame();
        REQUIRE(registry.all_of<Idle>(grunt));
        CHECK(world.log(grunt).chasing_left == 1);
        world.order(grunt, Order::Chase, Order::Attack);
        world.frame();
        REQUIRE(registry.all_of<Attacking>(grunt));
        CHECK(world.log(grunt).idle_left == 2);
        CHECK(world.log(grunt).attacking_added == 1);
        CHECK(world.log(grunt).chasing_added == 1);
        CHECK(world.log(grunt).chasing_left == 1);
    }

    {  // a state match runs the current variant's arm
        CHECK(registry.get<Attacking>(grunt).windup == 0.3F);
        world.frame();
        CHECK(world.log(grunt).arm == 3);
        CHECK(world.log(grunt).code == 30);
        CHECK(std::abs(registry.get<Attacking>(grunt).windup - 0.2F) < 1e-6F);
    }

    {  // a round trip in one round fires nothing
        world.order(grunt, Order::Idle);
        world.frame();
        REQUIRE(registry.all_of<Idle>(grunt));
        const auto before = world.log(grunt);
        world.order(grunt, Order::Chase, Order::Idle);
        world.frame();
        REQUIRE(registry.all_of<Idle>(grunt));
        CHECK(world.log(grunt).idle_left == before.idle_left);
        CHECK(world.log(grunt).idle_added == before.idle_added);
        CHECK(world.log(grunt).chasing_added == before.chasing_added);
        CHECK(world.log(grunt).chasing_left == before.chasing_left);
    }

    {  // conflicting transitions resolve by command order
        world.order(boss, Order::Attack, Order::Idle);
        world.frame();
        CHECK(registry.all_of<Idle>(boss));
    }

    {  // add to an entity without the state gives it the state
        world.order(drifter, Order::Idle);
        world.frame();
        REQUIRE(registry.all_of<Idle>(drifter));
        CHECK(registry.get<Mode>(drifter).index == 0);
        CHECK(world.log(drifter).idle_added == 1);
        const auto stateless = world.log(drifter).stateless_ticks;
        world.frame();
        CHECK(world.log(drifter).stateless_ticks == stateless);
    }

    {  // remove drops the whole slot and fires removed for the variant
        world.order(drifter, Order::Chase);
        world.frame();
        REQUIRE(registry.all_of<Chasing>(drifter));
        world.order(drifter, Order::Leave);
        world.frame();
        CHECK_FALSE(registry.all_of<Mode>(drifter));
        CHECK(world.log(drifter).chasing_left == 1);
    }

    {  // a final variant wins over a later transition in the same round
        CHECK_FALSE(registry.get<Watcher>(lookout).saw_dying);
        world.order(grunt, Order::Die, Order::Attack);
        world.frame();
        REQUIRE(registry.all_of<Dying>(grunt));
        CHECK(world.log(grunt).dying_added == 1);
        const auto awake = world.log(grunt).awake_ticks;
        world.frame();
        CHECK(world.log(grunt).awake_ticks == awake);
        CHECK(registry.get<Watcher>(lookout).saw_dying);
    }

    {  // a final variant is never left or reset
        registry.get<Dying>(grunt).elapsed = 0.4F;
        world.order(grunt, Order::Die);
        world.frame();
        CHECK(registry.get<Dying>(grunt).elapsed == 0.4F);
        CHECK(world.log(grunt).dying_added == 1);
        world.order(grunt, Order::Leave, Order::Idle);
        world.frame();
        REQUIRE(registry.all_of<Dying>(grunt));
        CHECK(world.log(grunt).code == 40);
    }

    {  // destroy still applies to an entity in a final variant
        world.order(grunt, Order::Vanish);
        world.frame();
        CHECK_FALSE(registry.valid(grunt));
    }
}
TEST_CASE("exclusive states above index 63 preserve final variants", "[runtime][exclusive-states]") {
    World world;
    auto& registry    = world.registry;
    const auto entity = generated_named_slots().exclusive_states__WideCarrier;
    REQUIRE(registry.get<exclusive_states__WideMode>(entity).index == 63);
    CHECK_FALSE(cactus::runtime::entt_backend::state_is_final(exclusive_states__WideMode{.index = 0}));
    CHECK_FALSE(cactus::runtime::entt_backend::state_is_final(registry.get<exclusive_states__WideMode>(entity)));
    world.frame();
    REQUIRE(registry.all_of<exclusive_states__WideMode__Done>(entity));
    CHECK_FALSE(registry.all_of<exclusive_states__WideMode__V63>(entity));
    CHECK_FALSE(registry.all_of<exclusive_states__WideMode__V0>(entity));
    CHECK(registry.get<exclusive_states__WideMode>(entity).index == 64);
    world.frame();
    REQUIRE(registry.all_of<exclusive_states__WideMode__Done>(entity));
    CHECK(registry.get<exclusive_states__WideMode__Done>(entity).elapsed == 0.4F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
