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

#include <algorithm>

namespace {

using Cell = cactus_gen::ranged_fields_persistence_runtime::Cell;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }
};

cactus::persistence::Snapshot capture(entt::registry& registry) {
    return cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
}

// Sets `x` in the construction record of the cell whose x is `current`.
cactus::persistence::Snapshot corrupt_cell_x(const cactus::persistence::Snapshot& snapshot, int current,
                                             int replacement) {
    bool found = false;
    auto edited = cactus_test::edited(snapshot, [&](std::vector<cactus_test::EditableRecord>& records) {
        for (auto& record : records) {
            auto* cell = record.trait("ranged_fields_persistence_runtime.Cell");
            auto* x    = cell == nullptr ? nullptr : cell->field("x");
            if (x != nullptr && *x == cactus::persistence::Value::of_int(current)) {
                *x    = cactus::persistence::Value::of_int(replacement);
                found = true;
                return;
            }
        }
    });
    REQUIRE(found);
    return edited;
}

int cell_count(const entt::registry& registry) {
    return static_cast<int>(registry.view<Cell>().size());
}

}  // namespace

TEST_CASE("cells of a destroyed volume save and restore unchanged", "[runtime][persistence][ranged-fields]") {
    World world;
    const auto& slots = cactus_gen::detail::generated_named_slots().ranged_fields_persistence_runtime;
    REQUIRE_FALSE(world.registry.valid(slots.Ruin));
    REQUIRE(world.registry.valid(slots.Orphan));
    const auto snapshot = capture(world.registry);

    World restored;
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(restored.registry, snapshot);
    INFO(outcome.message);
    REQUIRE(outcome.ok);
    CHECK(cell_count(restored.registry) == 2);
    const auto xs = [&] {
        std::vector<int> values;
        for (const auto entity : restored.registry.view<Cell>()) {
            values.push_back(restored.registry.get<Cell>(entity).x);
        }
        std::ranges::sort(values);
        return values;
    }();
    CHECK(xs == std::vector{1, 2});
}

TEST_CASE("a document holding a cell outside its volume fails as invalid data and keeps the world",
          "[runtime][persistence][ranged-fields]") {
    World world;
    const auto snapshot = corrupt_cell_x(capture(world.registry), 2, 9);

    const auto& slots  = cactus_gen::detail::generated_named_slots().ranged_fields_persistence_runtime;
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "invalid_data");
    CHECK(outcome.message.contains("Cell.x = 9 is outside range(0, 4)"));
    REQUIRE(world.registry.valid(slots.Corner));
    CHECK(world.registry.get<Cell>(slots.Corner).x == 2);
    CHECK(cell_count(world.registry) == 2);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
