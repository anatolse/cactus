#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Inline and block rules agree on movement, empty groups and kept lifecycle", "[runtime][concise]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots  = cactus_gen::detail::generated_named_slots();
    const auto compact = slots.concise_rule_notation.CompactActor;
    const auto block   = slots.concise_rule_notation.BlockActor;
    CHECK_FALSE(registry.all_of<cactus_gen::concise_rule_notation::CompactKept>(compact));
    for (int frame = 0; frame < 3; ++frame) {
        cactus_headless_test::drive_frame(registry, 1.0F / 60.0F);
        const auto& a = registry.get<cactus_gen::concise_rule_notation::CompactBody>(compact);
        const auto& b = registry.get<cactus_gen::concise_rule_notation::BlockBody>(block);
        CHECK(a.x == b.x);
        CHECK(a.total == b.total);
        CHECK(a.enters == b.enters);
        CHECK(a.exits == b.exits);
        CHECK(a.visible == b.visible);
        CHECK(a.visible == (frame == 0 ? -1 : 2));
        CHECK(a.rank == b.rank);
        if (frame == 0) {
            CHECK(a.rank == 2);
            CHECK(registry.get<cactus_gen::concise_rule_notation::CompactKept>(compact).n == 2);
            CHECK(registry.get<cactus_gen::concise_rule_notation::BlockKept>(block).n == 2);
        }
        const auto& compact_control = registry.get<cactus_gen::concise_rule_notation::CompactControl>(
            slots.concise_rule_notation.CompactSettings);
        const auto& block_control =
            registry.get<cactus_gen::concise_rule_notation::BlockControl>(slots.concise_rule_notation.BlockSettings);
        CHECK(compact_control.trace == 2010);
        CHECK(compact_control.trace == block_control.trace);
        CHECK(compact_control.empty == 0);
        CHECK(block_control.empty == 0);
        CHECK(a.total == (frame == 0 ? 2 : 0));
        CHECK(a.enters == 1);
        CHECK(a.exits == (frame == 0 ? 0 : 1));
        CHECK(registry.all_of<cactus_gen::concise_rule_notation::CompactKept>(compact) == (frame == 0));
        CHECK(registry.all_of<cactus_gen::concise_rule_notation::BlockKept>(block) == (frame == 0));
        CHECK(registry.get<cactus_gen::concise_rule_notation::CompactBody>(slots.concise_rule_notation.CompactEmpty)
                  .total == 0);
        CHECK(
            registry.get<cactus_gen::concise_rule_notation::BlockBody>(slots.concise_rule_notation.BlockEmpty).total ==
            0);
    }
    registry.get<cactus_gen::concise_rule_notation::CompactControl>(slots.concise_rule_notation.CompactSettings)
        .enabled = false;
    registry.get<cactus_gen::concise_rule_notation::BlockControl>(slots.concise_rule_notation.BlockSettings).enabled =
        false;
    cactus_headless_test::drive_frame(registry, 1.0F / 60.0F);
    CHECK(registry.get<cactus_gen::concise_rule_notation::CompactBody>(compact).x == 3);
    CHECK(registry.get<cactus_gen::concise_rule_notation::BlockBody>(block).x == 3);
}
