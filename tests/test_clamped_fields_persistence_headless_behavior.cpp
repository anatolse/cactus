// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_snapshot_edit.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using Health = cactus_gen::clamped_fields_persistence_runtime::Health;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    [[nodiscard]] Health& hero() {
        return registry.get<Health>(cactus_gen::detail::generated_named_slots().clamped_fields_persistence_runtime.Hero);
    }
};

cactus::persistence::Snapshot capture(entt::registry& registry) {
    return cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
}

cactus::persistence::Snapshot with_hp(const cactus::persistence::Snapshot& snapshot, float hp) {
    bool found  = false;
    auto edited = cactus_test::edited(snapshot, [&](std::vector<cactus_test::EditableRecord>& records) {
        for (auto& record : records) {
            auto* health = record.trait("clamped_fields_persistence_runtime.Health");
            auto* value  = health == nullptr ? nullptr : health->field("hp");
            if (value != nullptr) {
                *value = cactus::persistence::Value::of_float(hp);
                found  = true;
            }
        }
    });
    REQUIRE(found);
    return edited;
}

}  // namespace

TEST_CASE("a clamped value saves and restores", "[runtime][persistence][clamped-fields]") {
    World world;
    world.hero().hp = 42.0F;
    const auto snapshot = capture(world.registry);

    World restored;
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(restored.registry, snapshot);
    INFO(outcome.message);
    REQUIRE(outcome.ok);
    CHECK(restored.hero().hp == 42.0F);
}

TEST_CASE("a document holding a clamped value outside its bounds fails as invalid data and keeps the world",
          "[runtime][persistence][clamped-fields]") {
    World world;
    const auto snapshot = with_hp(capture(world.registry), 150.0F);

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "invalid_data");
    CHECK(outcome.message.contains("Health.hp = 150 is outside clamped(min = 0, max = 100)"));
    CHECK(world.hero().hp == 80.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
