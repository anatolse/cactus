#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Concise and legacy rules agree on movement, empty groups and kept lifecycle", "[runtime][concise]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots  = generated_named_slots();
    const auto compact = slots.concise_rule_notation__CompactActor;
    const auto legacy  = slots.concise_rule_notation__LegacyActor;
    CHECK_FALSE(registry.all_of<concise_rule_notation__CompactKept>(compact));
    for (int frame = 0; frame < 3; ++frame) {
        cactus_headless_test::drive_frame(registry, 1.0F / 60.0F);
        const auto& a = registry.get<concise_rule_notation__CompactBody>(compact);
        const auto& b = registry.get<concise_rule_notation__LegacyBody>(legacy);
        CHECK(a.x == b.x);
        CHECK(a.total == b.total);
        CHECK(a.enters == b.enters);
        CHECK(a.exits == b.exits);
        CHECK(a.visible == b.visible);
        CHECK(a.visible == (frame == 0 ? -1 : 2));
        CHECK(a.rank == b.rank);
        if (frame == 0) {
            CHECK(a.rank == 2);
            CHECK(registry.get<concise_rule_notation__CompactKept>(compact).n == 2);
            CHECK(registry.get<concise_rule_notation__LegacyKept>(legacy).n == 2);
        }
        const auto& compact_control =
            registry.get<concise_rule_notation__CompactControl>(slots.concise_rule_notation__CompactSettings);
        const auto& legacy_control =
            registry.get<concise_rule_notation__LegacyControl>(slots.concise_rule_notation__LegacySettings);
        CHECK(compact_control.trace == 2010);
        CHECK(compact_control.trace == legacy_control.trace);
        CHECK(compact_control.empty == 0);
        CHECK(legacy_control.empty == 0);
        CHECK(a.total == (frame == 0 ? 2 : 0));
        CHECK(a.enters == 1);
        CHECK(a.exits == (frame == 0 ? 0 : 1));
        CHECK(registry.all_of<concise_rule_notation__CompactKept>(compact) == (frame == 0));
        CHECK(registry.all_of<concise_rule_notation__LegacyKept>(legacy) == (frame == 0));
        CHECK(registry.get<concise_rule_notation__CompactBody>(slots.concise_rule_notation__CompactEmpty).total == 0);
        CHECK(registry.get<concise_rule_notation__LegacyBody>(slots.concise_rule_notation__LegacyEmpty).total == 0);
    }
    registry.get<concise_rule_notation__CompactControl>(slots.concise_rule_notation__CompactSettings).enabled = false;
    registry.get<concise_rule_notation__LegacyControl>(slots.concise_rule_notation__LegacySettings).enabled   = false;
    cactus_headless_test::drive_frame(registry, 1.0F / 60.0F);
    CHECK(registry.get<concise_rule_notation__CompactBody>(compact).x == 3);
    CHECK(registry.get<concise_rule_notation__LegacyBody>(legacy).x == 3);
}
