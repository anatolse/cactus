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

using Match = named_entity_access__Match;
using Probe = named_entity_access__Probe;

using cactus_headless_test::drive_frame;
using cactus_headless_test::init_and_load;

template <typename Trait>
entt::entity only_entity(entt::registry& registry) {
    auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return *view.begin();
}

cactus::persistence::Snapshot capture(entt::registry& registry) {
    return cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
}

void restore(entt::registry& registry, const cactus::persistence::Snapshot& snapshot) {
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);
}

}  // namespace

TEST_CASE("restore rebinds a named entity and restores its persist field",
          "[runtime][persistence][restore][named-entity]") {
    entt::registry registry;
    init_and_load(registry);
    registry.get<Match>(only_entity<Match>(registry)).best = 7;
    const auto snapshot = capture(registry);

    entt::registry restored;
    init_and_load(restored);
    restore(restored, snapshot);

    const auto game = only_entity<Match>(restored);
    CHECK(restored.get<Match>(game).best == 7);

    const auto probe_entity = only_entity<Probe>(restored);
    const auto passes       = restored.get<Probe>(probe_entity).match_passes;
    drive_frame(restored);
    CHECK(restored.get<Probe>(probe_entity).match_passes == passes + 1);

    SECTION("a named write lands on the restored instance") {
        restored.get<Probe>(probe_entity).wave_ticks = 0;
        drive_frame(restored);
        CHECK(restored.get<Probe>(probe_entity).wave_ticks == 1);
    }
}

TEST_CASE("restore without the named entity's record leaves the name stale",
          "[runtime][persistence][restore][named-entity]") {
    entt::registry registry;
    init_and_load(registry);
    auto snapshot = capture(registry);
    const auto removed = std::erase_if(snapshot.entities, [](const cactus::persistence::EntityRecord& record) {
        return record.archetype == "named_entity_access.Game";
    });
    REQUIRE(removed == 1);

    restore(registry, snapshot);
    REQUIRE(registry.view<Match>().empty());

    const auto probe_entity = only_entity<Probe>(registry);
    drive_frame(registry);
    drive_frame(registry);
    const auto& probe = registry.get<Probe>(probe_entity);
    CHECK(probe.match_passes == 0);
    CHECK(probe.stats_passes == 0);
    CHECK(probe.wave_ticks == 0);
    CHECK(probe.gated_pings == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
