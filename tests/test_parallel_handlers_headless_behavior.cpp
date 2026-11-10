// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

namespace {

namespace backend = cactus::runtime::entt_backend;
namespace game    = cactus_gen::parallel_handlers_runtime;

using cactus_headless_test::WorkerCount;

struct World {
    entt::registry registry;

    World() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        backend::generated_reset_scheduler_state();
        cactus_headless_test::init_and_load(registry);
    }

    // Puts every mover and spinner where both ping rules and both add rules fire on the next tick.
    void arm() {
        for (auto [entity, mover] : registry.view<game::Mover>().each()) {
            mover.x  = 20.5F;
            mover.vx = 0.0F;
        }
        for (auto [entity, spinner] : registry.view<game::Spinner>().each()) {
            spinner.angle = 4.95F;
        }
        backend::advance_binding_index_epoch();
    }

    // Runs the tick phase alone and leaves its outputs queued.
    void tick_phase() {
        auto& activation  = backend::generated_scheduler_state().activation;
        activation.active = true;
        backend::generated_dispatch_phase(registry, std::get<cactus_gen::stdlib::core::TickPhase>(
                                                        backend::generated_scheduler_state().phases));
    }

    std::vector<int> queued_ping_values() {
        std::vector<int> values;
        for (const auto& queued : backend::generated_scheduler_state().activation.event_queue) {
            if (const auto* ping = std::get_if<game::PingEvent>(&queued.occurrence)) {
                values.push_back(ping->k);
            }
        }
        return values;
    }
};

}  // namespace

TEST_CASE("parallel handlers queue their emissions in the sequential order", "[parallel-handlers][headless]") {
    for (const std::size_t workers : {0U, 1U, 4U}) {
        INFO("workers " << workers);
        const WorkerCount count(workers);
        World world;
        world.arm();
        world.tick_phase();
        const auto values = world.queued_ping_values();
        const auto movers = world.registry.view<game::Mover>().size();
        REQUIRE(values.size() == 2 * movers);
        // PingMovers comes before PingSpinners in the stable order.
        CHECK(std::all_of(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(movers),
                          [](int k) { return k == 3; }));
        CHECK(std::all_of(values.begin() + static_cast<std::ptrdiff_t>(movers), values.end(),
                          [](int k) { return k == 5; }));
    }
}

TEST_CASE("parallel handlers queue their commands in the sequential order", "[parallel-handlers][headless]") {
    const WorkerCount count(4);
    World world;
    world.arm();
    world.registry.view<game::Spinner>().each([](auto& spinner) { spinner.angle = 7.95F; });
    world.registry.view<game::Mover>().each([](auto& mover) { mover.x = 30.3F; });
    world.tick_phase();
    const auto& commands = backend::generated_scheduler_state().activation.commands;
    const auto movers    = world.registry.view<game::Mover>().size();
    REQUIRE(commands.size() >= 2 * movers);
    // LightMovers' adds, then TagSpinners' adds: applying the first half lights movers only.
    for (std::size_t index = 0; index < movers; ++index) {
        commands[index].apply(world.registry);
    }
    CHECK(world.registry.view<game::Lit>().size() == movers);
    CHECK(world.registry.view<game::Tagged>().empty());
}

TEST_CASE("a parallel build runs the fixture to the same counts on any worker count", "[parallel-handlers][headless]") {
    std::vector<std::tuple<int, int, int>> results;
    for (const std::size_t workers : {0U, 1U, 4U}) {
        const WorkerCount count(workers);
        World world;
        for (int frame = 0; frame < 120; ++frame) {
            cactus_headless_test::drive_frame(world.registry);
        }
        const auto field = cactus_gen::detail::generated_named_slots().parallel_handlers_runtime.Field;
        int found        = 0;
        for (auto [entity, seeker] : world.registry.view<game::Seeker>().each()) {
            found += seeker.found;
        }
        results.emplace_back(world.registry.get<game::Counter>(field).total,
                             world.registry.get<game::Log>(field).events, found);
    }
    CHECK(std::get<0>(results.front()) == 360);
    CHECK(std::get<1>(results.front()) > 0);
    CHECK(results[1] == results[0]);
    CHECK(results[2] == results[0]);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
