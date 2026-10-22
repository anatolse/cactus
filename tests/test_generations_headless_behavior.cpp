// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

namespace {

// 'L' for a line, 'T' for a triangle, in call order.
std::string draw_shapes() {
    std::string shapes;
    for (const auto& entry : cactus_raylib_fake::call_log()) {
        if (std::holds_alternative<cactus_raylib_fake::RecordedDrawLineEx>(entry)) {
            shapes += 'L';
        } else if (std::holds_alternative<cactus_raylib_fake::RecordedDrawTriangle>(entry)) {
            shapes += 'T';
        }
    }
    return shapes;
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("occurrences are delivered in generations", "[runtime][generations]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto recorder = cactus_gen::detail::generated_named_slots().generations_runtime.Recorder;

    cactus_raylib_fake::reset();
    cactus_headless_test::drive_frame(registry, 1.0F / 60.0F);

    // tick emits A then B, and A's handler emits C: A, B, then C.
    CHECK(registry.get<cactus_gen::generations_runtime::Trace>(recorder).digits == 123);
    // Both lifecycle deliveries of the round run before the event the first emits.
    CHECK(registry.get<cactus_gen::generations_runtime::RoundTrace>(recorder).digits == 456);
    // One handler's interleaved draw emits render in emission order.
    CHECK(draw_shapes() == "LTL");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
