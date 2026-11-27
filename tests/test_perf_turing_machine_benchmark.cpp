#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Turing machine playback updates its tape window", "[perf][turing-machine]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);
    auto& machine = registry.get<cactus_gen::main::Machine>(cactus_gen::detail::generated_named_slots().main.Console);
    machine.speed = 8;
    BENCHMARK("binary increment with viewport updates") {
        machine.reset_requested = true;
        cactus_headless_test::drive_frame(registry);
        machine.running = true;
        for (int i = 0; i < 3; ++i) {
            cactus_headless_test::drive_frame(registry, 0.125F);
        }
        cactus_raylib_fake::clear_call_log();
        return machine.steps;
    };
    REQUIRE(machine.steps == 3);
}
