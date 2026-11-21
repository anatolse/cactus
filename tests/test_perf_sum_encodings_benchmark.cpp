// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "heap_allocation_counter.hpp"
#include "perf_benchmark_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

namespace game    = cactus_gen::perf_sum_encodings;
namespace backend = cactus::runtime::entt_backend;

enum class Form : std::uint8_t { Flag = 0, State = 1, Projected = 2, Presence = 3 };

constexpr std::array kForms{Form::Flag, Form::State, Form::Projected, Form::Presence};

std::string_view form_name(Form form) {
    switch (form) {
        case Form::Flag:
            return "flag";
        case Form::State:
            return "state";
        case Form::Projected:
            return "projected";
        case Form::Presence:
            return "presence";
    }
    return "?";
}

// A unit's fact holds when (id + frame * step) % period < share.
struct Case {
    std::string_view name;
    int period = 1;
    int share  = 1;
    int step   = 0;
};

constexpr std::array kCases{
    Case{.name = "1% selected, no swaps", .period = 100, .share = 1, .step = 0},
    Case{.name = "50% selected, no swaps", .period = 2, .share = 1, .step = 0},
    Case{.name = "all selected, no swaps", .period = 1, .share = 1, .step = 0},
    Case{.name = "50% selected, 2% swap per frame", .period = 100, .share = 50, .step = 1},
    Case{.name = "50% selected, all swap every frame", .period = 2, .share = 1, .step = 1},
};

constexpr int kUnits        = 1000;
constexpr int kWarmupFrames = 3;
constexpr int kFrames       = 4;

struct Work {
    backend::PerfCounters counters;
    std::size_t allocations = 0;
};

struct World {
    entt::registry registry;

    World(Form form, const Case& shape, int units = kUnits) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            auto& config  = reg.get<game::Config>(cactus_gen::detail::generated_named_slots().perf_sum_encodings.Field);
            config.form   = static_cast<int>(form);
            config.units  = units;
            config.period = shape.period;
            config.share  = shape.share;
            config.step   = shape.step;
        });
        for (int frame = 0; frame < kWarmupFrames; ++frame) {
            tick();
        }
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    // Work of kFrames frames, divided per frame.
    Work per_frame() {
        Work work;
        {
            const cactus_test::AllocationCount allocations;
            work.counters = cactus_perf::work_of([&] {
                for (int frame = 0; frame < kFrames; ++frame) {
                    tick();
                }
            });
            work.allocations = cactus_test::AllocationCount::count();
        }
        work.counters.commands /= kFrames;
        work.counters.component_moves /= kFrames;
        work.counters.domain_rows /= kFrames;
        work.allocations /= kFrames;
        return work;
    }
};

}  // namespace

TEST_CASE("perf: domain rows count each row a unary pass visits", "[perf][sum-encodings]") {
    World world(Form::Flag, kCases[2]);
    const auto work = world.per_frame();
    // Advance visits the ticker; DecideFlag and UseFlag visit every unit.
    CHECK(work.counters.domain_rows == 1 + (2 * kUnits));
}

TEST_CASE("perf: component moves count each construct and destroy", "[perf][sum-encodings]") {
    World flag(Form::Flag, kCases[4]);
    CHECK(flag.per_frame().counters.component_moves == 0);
    // Half the units gain Active and half lose it each frame.
    World presence(Form::Presence, kCases[4]);
    CHECK(presence.per_frame().counters.component_moves == kUnits);
}

// A command applies at commit, so the state and presence forms see the fact one
// frame later than the flag and projected forms; each pair must agree per unit.
TEST_CASE("perf: encodings with the same timing give each unit the same result", "[perf][sum-encodings]") {
    const auto used_by_id = [](Form form, const Case& shape) {
        World world(form, shape);
        world.per_frame();
        std::vector<float> used(kUnits, 0.0F);
        for (const auto [entity, unit] : world.registry.view<game::Unit>().each()) {
            used.at(static_cast<std::size_t>(unit.id)) = unit.used;
        }
        return used;
    };
    for (const auto& shape : {kCases[3], kCases[4]}) {
        INFO(shape.name);
        CHECK(used_by_id(Form::Projected, shape) == used_by_id(Form::Flag, shape));
        CHECK(used_by_id(Form::State, shape) == used_by_id(Form::Presence, shape));
    }
}

TEST_CASE("perf: sum encodings measured", "[.measure][sum-encodings]") {
    std::cout << "| case | form | commands | component moves | domain rows | allocations |\n";
    std::cout << "|---|---|---|---|---|---|\n";
    for (const auto& shape : kCases) {
        for (const auto form : kForms) {
            World world(form, shape);
            const auto work = world.per_frame();
            std::cout << "| " << shape.name << " | " << form_name(form) << " | " << work.counters.commands << " | "
                      << work.counters.component_moves << " | " << work.counters.domain_rows << " | "
                      << work.allocations << " |\n";
        }
    }
    if (cactus_perf::timing_enabled()) {
        std::cout << "\n| case | flag ms | state ms | projected ms | presence ms |\n|---|---|---|---|---|\n";
        for (const auto& shape : kCases) {
            std::cout << "| " << shape.name;
            for (const auto form : kForms) {
                World world(form, shape, 20 * kUnits);
                auto step = [&] { world.tick(); };
                std::cout << " | " << cactus_perf::median_ms(step);
            }
            std::cout << " |\n";
        }
    }
    SUCCEED();
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
