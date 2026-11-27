// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/fake_raylib.hpp"
#include "perf_benchmark_support.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

struct Simulator {
    entt::registry registry;

    Simulator() {
        cactus_raylib_fake::reset();
        cactus_headless_test::init_and_load(registry);
        cactus_headless_test::drive_frame(registry);
        machine().speed = 8;
    }

    cactus_gen::main::Machine& machine() {
        return registry.get<cactus_gen::main::Machine>(cactus_gen::detail::generated_named_slots().main.Console);
    }

    void frame(float dt = 1.F / 60.F) {
        cactus_headless_test::drive_frame(registry, dt);
        cactus_raylib_fake::clear_call_log();
    }
};

void report(const char* name, const cactus::runtime::entt_backend::PerfCounters& work) {
    WARN(name << ": candidates " << work.candidates << ", commands " << work.commands << ", keep runs "
              << work.keep_runs << ", domain rows " << work.domain_rows);
}

}  // namespace

TEST_CASE("Turing machine playback updates its tape window", "[perf][turing-machine]") {
    Simulator sim;
    BENCHMARK("binary increment with viewport updates") {
        sim.machine().reset_requested = true;
        sim.frame();
        sim.machine().running = true;
        for (int i = 0; i < 3; ++i) {
            sim.frame(0.125F);
        }
        return sim.machine().steps;
    };
    REQUIRE(sim.machine().steps == 3);
}

TEST_CASE("Work of a paused frame", "[perf][turing-machine]") {
    Simulator sim;
    sim.frame();
    const auto work = cactus_perf::work_of([&] { sim.frame(); });
    report("paused", work);
    CHECK(work.keep_runs == 0);
    CHECK(work.candidates <= 18);
    CHECK(work.domain_rows <= 91);
}

TEST_CASE("Work of a running frame", "[perf][turing-machine]") {
    Simulator sim;
    sim.machine().running = true;
    sim.frame();
    const auto work = cactus_perf::work_of([&] { sim.frame(0.125F); });
    report("running", work);
    REQUIRE(sim.machine().steps == 1);
    CHECK(work.keep_runs == 1);
    CHECK(work.candidates <= 23);
    CHECK(work.domain_rows <= 91);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
