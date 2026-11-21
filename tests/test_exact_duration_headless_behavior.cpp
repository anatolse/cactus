// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>

namespace {

namespace game = cactus_gen::exact_duration;

constexpr std::int64_t kStep = 11'760'000;
constexpr std::int64_t kMillisecond = 705'600;

struct World {
    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().exact_duration;
    }

    entt::registry registry;

    World() {
        cactus::runtime::entt_backend::clear_runtime_faults();
        cactus_headless_test::init_and_load(registry);
    }

    void frames(int count, float dt = 1.0F / 60.0F) {
        for (int frame = 0; frame < count; ++frame) {
            cactus_headless_test::drive_frame(registry, dt);
        }
    }

    template <typename Trait>
    [[nodiscard]] const Trait& probe() {
        return registry.get<Trait>(slots().Probe);
    }

    void plant(int x, std::int64_t units) {
        auto& table = registry.get<game::Field>(slots().Probe);
        table.wait.set(table.cactus_shape().at(x), cactus::Duration{units});
    }
    [[nodiscard]] std::int64_t wait(int x) {
        const auto& table = probe<game::Field>();
        return table.wait.get(table.cactus_shape().at(x)).units;
    }
};

}  // namespace

TEST_CASE("a 60 Hz step is exactly 1s / 60", "[runtime][dsl-duration]") {
    World world;
    world.frames(1);
    const auto& steps = world.probe<game::Steps>();
    CHECK(steps.fixed.units == kStep);
    CHECK(std::bit_cast<std::uint32_t>(steps.fixed_dt) == std::bit_cast<std::uint32_t>(static_cast<float>(1.0 / 60.0)));
    CHECK(steps.round_trip.units == kStep + 1);
    CHECK(steps.grace_steps == 6.0F);
}

TEST_CASE("a 100ms countdown ends after exactly 6 steps", "[runtime][dsl-duration]") {
    World world;
    world.frames(5);
    CHECK(world.probe<game::Countdown>().remaining.units == 100 * kMillisecond - (5 * kStep));
    world.frames(1);
    CHECK(world.probe<game::Countdown>().remaining.units == 0);
    CHECK(world.probe<game::Countdown>().steps == 6);
    world.frames(2);
    CHECK(world.probe<game::Countdown>().steps == 6);
}

TEST_CASE("a day of steps does not drift", "[runtime][dsl-duration]") {
    World world;
    world.frames(1);
    CHECK(world.probe<game::Clock>().elapsed.units == 24LL * 3600LL * 705'600'000LL);
}

TEST_CASE("two unordered handlers count down one timer", "[runtime][dsl-duration][handler-contracts]") {
    World world;
    world.frames(1);
    CHECK(world.probe<game::Shared>().left.units == 705'600'000 - (2 * kStep));
}

TEST_CASE("a timer on the variable tick reads the frame's delta", "[runtime][dsl-duration]") {
    World world;
    world.frames(1, 0.25F);
    CHECK(world.probe<game::Steps>().variable.units == 176'400'000);
    CHECK(world.probe<game::FrameTimer>().left.units == 705'600'000 - 176'400'000);
}

TEST_CASE("a std.time expiring span ends on the exact step", "[runtime][dsl-duration][stdlib-time]") {
    World world;
    world.frames(5);
    REQUIRE(world.registry.valid(World::slots().Spark));
    world.frames(1);
    CHECK_FALSE(world.registry.valid(World::slots().Spark));
}

TEST_CASE("a duration cell field counts down to its default and sums", "[runtime][dsl-duration][trait-dimensions]") {
    World world;
    world.plant(1, 100 * kMillisecond);
    world.plant(2, 3 * kStep);
    world.frames(1);
    CHECK(world.wait(1) == 100 * kMillisecond - kStep);
    CHECK(world.wait(2) == 2 * kStep);
    CHECK(world.wait(0) == 0);
    CHECK(world.probe<game::FieldTotal>().total.units == 100 * kMillisecond + kStep);
    CHECK(world.probe<game::FieldTotal>().longest.units == 100 * kMillisecond - kStep);
    world.frames(2);
    CHECK(world.wait(2) == 0);
    world.frames(3);
    CHECK(world.wait(2) == 0);
    CHECK(world.wait(1) == 100 * kMillisecond - (6 * kStep));
}

TEST_CASE("a duration field saves and restores exactly", "[runtime][dsl-duration][persistence]") {
    World world;
    world.frames(2);
    const auto saved    = world.probe<game::Countdown>().remaining;
    const auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);

    World restored;
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(restored.registry, snapshot);
    INFO(outcome.message);
    REQUIRE(outcome.ok);
    const auto view = restored.registry.view<game::Countdown>();
    REQUIRE(view.begin() != view.end());
    CHECK(restored.registry.get<game::Countdown>(*view.begin()).remaining == saved);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
