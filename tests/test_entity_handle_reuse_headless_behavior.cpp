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

using Memory = cactus_gen::entity_handle_reuse::Memory;
using Marked = cactus_gen::entity_handle_reuse::Marked;
using Shot   = cactus_gen::entity_handle_reuse::Shot;

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("a stale handle never names a later entity, however often its slot is reused", "[runtime][entity-handles]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto gun = cactus_gen::detail::generated_named_slots().entity_handle_reuse.Gun;

    // Two slots alternate, so 9000 ticks take each past its last version.
    for (int frame = 0; frame < 9000; ++frame) {
        cactus_headless_test::drive_frame(registry);
    }

    const auto& memory = registry.get<Memory>(gun);
    CHECK(memory.kept);
    CHECK_FALSE(registry.valid(memory.first));
    CHECK(registry.valid(memory.current));
    CHECK(memory.current != memory.first);
    CHECK(registry.view<Shot>().size() == 1);
    // Only the tick right after the first shot spawned saw it alive.
    CHECK(memory.alive_seen == 1);
    CHECK(memory.matched == 1);
    CHECK(registry.view<Marked>().empty());
    CHECK(memory.heard == 1);
    for (const auto shot : registry.view<Shot>()) {
        CHECK(registry.get<Shot>(shot).heard == 0);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
