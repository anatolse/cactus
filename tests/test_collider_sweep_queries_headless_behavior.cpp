// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

void run_frame(entt::registry& registry, float dt) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = dt});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

void start(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
}

template <typename Trait>
entt::entity only_entity(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return *view.begin();
}

}  // namespace

TEST_CASE("a later fixed_tick rule sees an earlier rule's WorldTransform write",
          "[runtime][codegen-entt][phase-activation]") {
    entt::registry registry;
    start(registry);
    run_frame(registry, 2.0F / 60.0F);
    const auto probe     = only_entity<collider_sweep_queries__Probe>(registry);
    const auto& position = registry.get<std_transform_volume__WorldTransform>(probe).position;
    const auto& seen     = registry.get<collider_sweep_queries__Probe>(probe);
    REQUIRE(position.x >= 1.0F);
    CHECK(seen.seen_x == position.x);
    CHECK(seen.pair_seen_x == position.x);
}

TEST_CASE("a periodic phase's dt is a constant in reduce: and const", "[runtime][codegen-entt][phase-activation]") {
    entt::registry registry;
    start(registry);
    run_frame(registry, 2.0F / 60.0F);
    const auto probe = only_entity<collider_sweep_queries__Probe>(registry);
    CHECK(registry.get<collider_sweep_queries__Probe>(probe).step_seen == Catch::Approx(1.5F));
}

namespace {

using Shot   = collider_sweep_queries__Shot;
using Struck = collider_sweep_queries__Struck;

constexpr float kStep = 600.0F / 60.0F;

struct ShotWorld {
    entt::registry registry;

    explicit ShotWorld(std::optional<std::size_t> sap_threshold = std::nullopt) {
        cactus::runtime::entt_backend::set_sap_small_domain_threshold_override_for_testing(sap_threshold);
        start(registry);
        run_frame(registry, 1.0F / 60.0F);
        cactus::runtime::entt_backend::set_sap_small_domain_threshold_override_for_testing(std::nullopt);
    }

    const Shot& shot(entt::entity entity) const {
        return registry.get<Shot>(entity);
    }
    const Struck& struck(entt::entity entity) const {
        return registry.get<Struck>(entity);
    }
};

const auto& slots() {
    return generated_named_slots();
}

// A shot of radius 0.1 starting at x = 0 touches a target face at x = 4.5.
constexpr float kContactT = 4.4F / kStep;

}  // namespace

TEST_CASE("a sweep keeps the first hit across box, sphere and capsule targets",
          "[runtime][codegen-entt][stdlib-physics][rule-reduce]") {
    ShotWorld world;
    for (const auto entity : {slots().collider_sweep_queries__ShotBox,
                              slots().collider_sweep_queries__ShotSphere,
                              slots().collider_sweep_queries__ShotCapsule}) {
        const auto& shot = world.shot(entity);
        CHECK(shot.hit);
        CHECK(shot.t == Catch::Approx(kContactT).margin(1e-4));
        CHECK(shot.point.x == Catch::Approx(4.5F).margin(1e-3));
        CHECK(shot.normal.x == Catch::Approx(-1.0F).margin(1e-3));
    }
    // The nearer box wins over the far one; each target got exactly its shot.
    CHECK(world.struck(slots().collider_sweep_queries__BoxTarget).by_shot == 1);
    CHECK(world.struck(slots().collider_sweep_queries__FarBox).by_shot == 0);
    CHECK(world.struck(slots().collider_sweep_queries__SphereTarget).by_shot == 1);
    CHECK(world.struck(slots().collider_sweep_queries__CapsuleTarget).by_shot == 1);
}

TEST_CASE("a shot with nothing in its path gets one activation with the miss value",
          "[runtime][codegen-entt][stdlib-physics][rule-reduce]") {
    ShotWorld world;
    const auto& shot = world.shot(slots().collider_sweep_queries__ShotMiss);
    CHECK_FALSE(shot.hit);
    CHECK(shot.t == 1.0F);
    CHECK(shot.point.x == 0.0F);
    CHECK(shot.normal.x == 0.0F);
}

TEST_CASE("sweeps honor the mask and never hit their own collider", "[runtime][codegen-entt][stdlib-physics]") {
    ShotWorld world;
    CHECK_FALSE(world.shot(slots().collider_sweep_queries__ShotMasked).hit);
    CHECK_FALSE(world.shot(slots().collider_sweep_queries__ShotSelf).hit);
}

TEST_CASE("equal hits resolve by creation order", "[runtime][codegen-entt][stdlib-physics][rule-reduce]") {
    ShotWorld world;
    CHECK(world.shot(slots().collider_sweep_queries__ShotTie).hit);
    CHECK(world.struck(slots().collider_sweep_queries__TieFirst).by_shot == 1);
    CHECK(world.struck(slots().collider_sweep_queries__TieSecond).by_shot == 0);
}

TEST_CASE("a sweep starting inside a box and moving out misses", "[runtime][codegen-entt][stdlib-physics]") {
    ShotWorld world;
    CHECK_FALSE(world.shot(slots().collider_sweep_queries__ShotEscape).hit);
    CHECK(world.struck(slots().collider_sweep_queries__EscapeBox).by_shot == 0);
}

TEST_CASE("touching counts only overlapping colliders the mask selects", "[runtime][codegen-entt][stdlib-physics]") {
    ShotWorld world;
    const auto toucher = only_entity<collider_sweep_queries__Toucher>(world.registry);
    CHECK(world.registry.get<collider_sweep_queries__Toucher>(toucher).touching == 1);
}

TEST_CASE("accelerated and unaccelerated sweeps agree", "[runtime][codegen-entt][stdlib-physics][spatial-join]") {
    for (const auto threshold : {std::optional<std::size_t>{}, std::optional<std::size_t>{0}}) {
        ShotWorld world(threshold);
        for (const auto [entity, shot] : world.registry.view<Shot>().each()) {
            CHECK(shot.hit == shot.plain_hit);
            CHECK(shot.t == shot.plain_t);
        }
        for (const auto [entity, struck] : world.registry.view<Struck>().each()) {
            CHECK(struck.by_shot == struck.by_plain);
        }
    }
}

namespace {

using Pusher = collider_sweep_queries__Pusher;

void check_push(const Vector3& actual, Vector3 expected) {
    CHECK(actual.x == Catch::Approx(expected.x).margin(1e-3));
    CHECK(actual.y == Catch::Approx(expected.y).margin(1e-3));
    CHECK(actual.z == Catch::Approx(expected.z).margin(1e-3));
}

}  // namespace

TEST_CASE("push_out moves a sphere out of a box", "[runtime][codegen-entt][stdlib-physics][rule-reduce]") {
    ShotWorld world;
    check_push(world.registry.get<Pusher>(slots().collider_sweep_queries__PushSphere).push, {-0.2F, 0.0F, 0.0F});
}

TEST_CASE("two character bodies that collide with each other split the push",
          "[runtime][codegen-entt][stdlib-physics][rule-reduce]") {
    ShotWorld world;
    check_push(world.registry.get<Pusher>(slots().collider_sweep_queries__PushLeft).push, {-0.1F, 0.0F, 0.0F});
    check_push(world.registry.get<Pusher>(slots().collider_sweep_queries__PushRight).push, {0.1F, 0.0F, 0.0F});
}

TEST_CASE("a one-sided character body takes the whole push", "[runtime][codegen-entt][stdlib-physics][rule-reduce]") {
    ShotWorld world;
    check_push(world.registry.get<Pusher>(slots().collider_sweep_queries__PushSeer).push, {-0.2F, 0.0F, 0.0F});
    check_push(world.registry.get<Pusher>(slots().collider_sweep_queries__PushBlind).push, {0.0F, 0.0F, 0.0F});
}

TEST_CASE("accelerated and unaccelerated push-outs agree", "[runtime][codegen-entt][stdlib-physics][spatial-join]") {
    for (const auto threshold : {std::optional<std::size_t>{}, std::optional<std::size_t>{0}}) {
        ShotWorld world(threshold);
        for (const auto [entity, pusher] : world.registry.view<Pusher>().each()) {
            CHECK(pusher.push.x == pusher.plain_push.x);
            CHECK(pusher.push.y == pusher.plain_push.y);
            CHECK(pusher.push.z == pusher.plain_push.z);
        }
    }
}
