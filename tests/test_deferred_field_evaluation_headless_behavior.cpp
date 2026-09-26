#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

template <typename Trait>
const Trait& only_instance(entt::registry& registry) {
    auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return registry.get<Trait>(*view.begin());
}

template <typename Trait>
void check_statement_time(entt::registry& registry) {
    const auto& result = only_instance<Trait>(registry);
    CHECK(result.from_field == 1.0F);
    CHECK(result.from_extern == 1.0F);
}

}  // namespace

TEST_CASE("deferred field blocks take their values when the statement runs",
          "[runtime][deferred-field-evaluation]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);

    check_statement_time<deferred_field_evaluation__AddResult>(registry);
    check_statement_time<deferred_field_evaluation__SpawnResult>(registry);
    check_statement_time<deferred_field_evaluation__FamilyResult>(registry);
    check_statement_time<deferred_field_evaluation__ChildResult>(registry);
}
