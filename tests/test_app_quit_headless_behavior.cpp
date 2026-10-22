// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

namespace {

namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
    }

    void frame() {
        backend::generated_inject_external_event(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        backend::generated_drain_external_events(registry);
    }

    const cactus_gen::app_quit::ExitButton& button() {
        return registry.get<cactus_gen::app_quit::ExitButton>(
            cactus_gen::detail::generated_named_slots().app_quit.Exit);
    }
};

}  // namespace

TEST_CASE("a handler that requests quit finishes, and the host observes the request",
          "[runtime][codegen-entt][stdlib-application-lifecycle]") {
    World world;
    CHECK_FALSE(backend::quit_requested());
    world.frame();
    CHECK_FALSE(backend::quit_requested());
    world.frame();
    CHECK(world.button().after_request == 1);
    CHECK(backend::quit_requested());
}

TEST_CASE("repeated quit requests coalesce and the headless host stays alive",
          "[runtime][codegen-entt][stdlib-application-lifecycle]") {
    World world;
    world.frame();
    world.frame();
    world.frame();
    CHECK(world.button().after_request == 2);
    CHECK(backend::quit_requested());
    backend::reset_quit_request();
    CHECK_FALSE(backend::quit_requested());
}

TEST_CASE("initializing a project clears an earlier quit request",
          "[runtime][codegen-entt][stdlib-application-lifecycle]") {
    {
        World first;
        first.frame();
        first.frame();
        REQUIRE(backend::quit_requested());
    }
    World second;
    CHECK_FALSE(backend::quit_requested());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
