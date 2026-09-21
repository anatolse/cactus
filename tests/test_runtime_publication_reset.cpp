// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace cactus::runtime::entt_backend;

TEST_CASE("reset_editor_camera_rig_state clears the rig entity and saved viewports",
          "[runtime][restore][publication]") {
    set_editor_saved_viewports({entt::entity{1}, entt::entity{2}});
    REQUIRE_FALSE(editor_saved_viewports().empty());

    reset_editor_camera_rig_state();

    CHECK(editor_saved_viewports().empty());
    CHECK(editor_rig_entity() == entt::entity{entt::null});
}

TEST_CASE("reset_pending_destruction_state clears an entity stuck by an interrupted cascade",
          "[runtime][restore][publication]") {
    entt::registry registry;
    const auto entity = registry.create();

    // An exception from visit_children unwinds out of destroy_entity_recursive
    // before its own destroying_entities.erase(entity) runs, leaving the guard
    // stuck on this entity: a normal replay never reaches the underlying
    // registry.destroy() because the re-entry check returns early.
    try {
        destroy_entity_recursive(
            registry, entity, [&](entt::entity, const auto&) { throw std::runtime_error("interrupted"); });
        FAIL("expected the visit_children exception to propagate");
    } catch (const std::runtime_error&) {
        SUCCEED("interrupted cascade left the entity stuck, as expected");
    }
    REQUIRE(registry.valid(entity));

    bool visited_while_stuck = false;
    destroy_entity_recursive(registry, entity, [&](entt::entity, const auto&) { visited_while_stuck = true; });
    CHECK_FALSE(visited_while_stuck);
    CHECK(registry.valid(entity));  // still stuck: the guard silently no-op'd the replay

    reset_pending_destruction_state();

    bool visited_after_reset = false;
    destroy_entity_recursive(registry, entity, [&](entt::entity, const auto&) { visited_after_reset = true; });
    CHECK(visited_after_reset);
    CHECK_FALSE(registry.valid(entity));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
