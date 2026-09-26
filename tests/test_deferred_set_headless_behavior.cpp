// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using Director = deferred_set__Director;
using Health   = deferred_set__Health;
using Animator = deferred_set__Animator;

template <typename Trait>
entt::entity only_entity(entt::registry& registry) {
    auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return *view.begin();
}

const Director& director(entt::registry& registry) {
    return registry.get<Director>(only_entity<Director>(registry));
}

}  // namespace

TEST_CASE("set patches another entity's trait at the activation commit", "[runtime][deferred-set]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);

    SECTION("values are taken when the statement runs") {
        const auto& snapshot = registry.get<deferred_set__Snapshot>(only_entity<deferred_set__Snapshot>(registry));
        CHECK(snapshot.from_field == 1.0F);
        CHECK(snapshot.from_extern == 1.0F);
    }

    SECTION("the patch is not visible within the activation, and visible in the next") {
        const auto watched = director(registry).watched;
        CHECK(registry.get<deferred_set__Watch>(watched).first == 10);
        CHECK(registry.get<Health>(watched).current == 1);
        cactus_headless_test::drive_frame(registry);
        CHECK(registry.get<deferred_set__Watch>(watched).second == 1);
    }

    SECTION("the last writer wins") {
        CHECK(registry.get<Health>(director(registry).contested).current == 7);
    }

    SECTION("patches to different fields merge and unnamed fields keep their values") {
        const auto& animator = registry.get<Animator>(director(registry).animated);
        CHECK(animator.clip == 3);
        CHECK(animator.time == 0.5F);
        CHECK(animator.speed == 2.0F);
    }

    SECTION("a missing trait is not attached") {
        CHECK_FALSE(registry.all_of<Health>(director(registry).bare));
    }

    SECTION("a stale target is a no-op") {
        CHECK_FALSE(registry.valid(director(registry).doomed));
    }

    SECTION("add then set patches the added trait") {
        const auto& health = registry.get<Health>(director(registry).fresh);
        CHECK(health.current == 8);
        CHECK(health.max == 10);
    }

    SECTION("remove then set is a no-op") {
        CHECK_FALSE(registry.all_of<Health>(director(registry).stripped));
    }

    SECTION("a patch under a projection lands on the durable value and survives cleanup") {
        const auto tinted = director(registry).tinted;
        CHECK(registry.get<deferred_set__TintSeen>(tinted).late == 2);
        CHECK(registry.get<deferred_set__Tint>(tinted).level == 3);
    }

    SECTION("a projection-only trait is not patched") {
        CHECK_FALSE(registry.all_of<deferred_set__Highlight>(director(registry).glowing));
    }

    SECTION("a pair handler patch reads the pre-pass values in every tuple") {
        auto view = registry.view<deferred_set__Push>();
        REQUIRE(view.size() == 3);
        for (const auto entity : view) {
            CHECK(registry.get<deferred_set__Push>(entity).amount == 1.0F);
        }
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
