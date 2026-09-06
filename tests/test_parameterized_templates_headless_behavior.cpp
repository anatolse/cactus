#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("parameterized templates reuse runtime values across descendants", "[runtime][template-parameters]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus_headless_test::dispatch_load_event(registry);
    int roots = 0;
    int children = 0;
    for (const auto entity : registry.view<parameterized_templates__Data>()) {
        const auto& data = registry.get<parameterized_templates__Data>(entity);
        if (registry.all_of<std_core__Parent>(entity)) {
            ++children;
            const auto parent = registry.get<std_core__Parent>(entity).parent;
            REQUIRE(registry.valid(parent));
            CHECK(data.value == registry.get<parameterized_templates__Data>(parent).value);
            CHECK(data.derived == (data.value == 11 ? 88 : data.value * 2));
        } else {
            ++roots;
            CHECK((data.value == 3 || data.value == 7 || data.value == 11));
            CHECK(data.derived == (data.value == 3 ? 99 : (data.value == 7 ? 14 : 77)));
        }
    }
    CHECK(roots == 3);
    CHECK(children == 3);
}
