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

#include <string_view>

namespace {

using cactus::persistence::Value;
using cactus_test::EditableRecord;

namespace game = cactus_gen::color_persistence_runtime;

constexpr std::string_view kTinted = "color_persistence_runtime.Tinted";

auto& lamp() {
    return cactus_gen::detail::generated_named_slots().color_persistence_runtime.Lamp;
}

struct World {
    entt::registry registry;

    World() { cactus_headless_test::init_and_load(registry); }

    CactusColor& tint() { return registry.get<game::Tinted>(lamp()).tint; }
};

}  // namespace

TEST_CASE("color persistence: an out-of-range color round-trips exactly", "[persistence][unbound-color-channels]") {
    World world;
    world.tint()        = color_from_components(1.5F, -0.2F, 0.3F, 1.0F);
    const auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);
    world.tint()        = color_from_bytes(0, 0, 0, 0);

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    REQUIRE(outcome.ok);
    CHECK(world.tint().r == 1.5F);
    CHECK(world.tint().g == -0.2F);
    CHECK(world.tint().b == 0.3F);
    CHECK(world.tint().a == 1.0F);
}

TEST_CASE("color persistence: a document with byte color lanes fails and leaves the world unchanged",
          "[persistence][unbound-color-channels]") {
    World world;
    world.tint()        = color_from_components(0.25F, 0.5F, 0.75F, 1.0F);
    const auto snapshot = cactus_test::edited(
        cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry),
        [](std::vector<EditableRecord>& records) {
            for (auto& record : records) {
                if (auto* trait = record.trait(kTinted); trait != nullptr) {
                    *trait->field("tint") =
                        Value::of_vector(cactus::PersistenceValueKind::Int, 4, {64.0F, 128.0F, 191.0F, 255.0F});
                }
            }
        });
    world.tint() = color_from_components(2.0F, 2.0F, 2.0F, 2.0F);

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(world.tint().r == 2.0F);
    CHECK(world.tint().a == 2.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
