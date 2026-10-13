// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>


using namespace cactus::runtime::entt_backend;

TEST_CASE("reset_editor_camera_rig_state clears the rig entity and saved viewports",
          "[runtime][restore][publication]") {
    set_editor_saved_viewports({entt::entity{1}, entt::entity{2}});
    REQUIRE_FALSE(editor_saved_viewports().empty());

    reset_editor_camera_rig_state();

    CHECK(editor_saved_viewports().empty());
    CHECK(editor_rig_entity() == entt::entity{entt::null});
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
