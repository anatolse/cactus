// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>

using namespace cactus::runtime::entt_backend;

namespace {

using Traits = entt::entt_traits<entt::entity>;

[[nodiscard]] std::uint32_t raw(entt::entity entity) {
    return static_cast<std::uint32_t>(entity);
}

}  // namespace

TEST_CASE("A slot is retired when its version would wrap", "[runtime][entity-allocator]") {
    entt::registry registry;
    const auto first = create_entity(registry);
    auto entity      = first;
    while (Traits::to_version(entity) != Traits::version_mask - 1U) {
        release_entity(registry, entity);
        entity = create_entity(registry);
        REQUIRE(Traits::to_entity(entity) == Traits::to_entity(first));
    }
    release_entity(registry, entity);

    const auto next = create_entity(registry);
    CHECK(Traits::to_entity(next) != Traits::to_entity(first));
    for (std::uint32_t version = 0; version < Traits::version_mask; ++version) {
        CHECK_FALSE(registry.valid(Traits::construct(Traits::to_entity(first), version)));
    }
}

TEST_CASE("Repeated create and destroy never returns an earlier handle", "[runtime][entity-allocator]") {
    entt::registry registry;
    std::unordered_set<std::uint32_t> seen;
    for (int cycle = 0; cycle < 5000; ++cycle) {
        const auto entity = create_entity(registry);
        REQUIRE(seen.insert(raw(entity)).second);
        release_entity(registry, entity);
    }
}

TEST_CASE("A reserved handle is new, not live, and costs one slot", "[runtime][entity-allocator]") {
    entt::registry registry;
    const auto destroyed = create_entity(registry);
    release_entity(registry, destroyed);
    const auto slots_before = registry.storage<entt::entity>().size();

    const auto reserved = reserve_entity(registry);

    CHECK(reserved != destroyed);
    CHECK_FALSE(registry.valid(reserved));
    CHECK(registry.storage<entt::entity>().size() <= slots_before + 1U);
    CHECK(create_reserved(registry, reserved) == reserved);
    CHECK(registry.valid(reserved));
}

TEST_CASE("Pending reservations are distinct and live only once committed", "[runtime][entity-allocator]") {
    entt::registry registry;
    const auto first  = reserve_entity(registry);
    const auto second = reserve_entity(registry);

    CHECK_FALSE(registry.valid(first));
    CHECK_FALSE(registry.valid(second));
    CHECK(first != second);

    CHECK(adopt_or_create(registry, second) == second);
    CHECK(registry.valid(second));
    CHECK_FALSE(registry.valid(first));
}

TEST_CASE("Reserving a fresh handle grows storage by at most one slot", "[runtime][entity-allocator]") {
    entt::registry registry;
    const auto reserved = create_reserved(registry, reserve_entity(registry));
    CHECK(registry.valid(reserved));
    CHECK(registry.storage<entt::entity>().size() <= 1U);
}

TEST_CASE("Releasing a stale handle changes nothing", "[runtime][entity-allocator]") {
    entt::registry registry;
    const auto entity = create_entity(registry);
    release_entity(registry, entity);
    const auto reused = create_entity(registry);
    release_entity(registry, entity);

    CHECK(registry.valid(reused));
    CHECK(create_entity(registry) != reused);
}

TEST_CASE("A slot created directly through the registry is skipped", "[runtime][entity-allocator]") {
    entt::registry registry;
    const auto foreign = registry.create();

    const auto reserved = reserve_entity(registry);

    CHECK(Traits::to_entity(reserved) != Traits::to_entity(foreign));
    CHECK_FALSE(registry.valid(reserved));
    CHECK(create_reserved(registry, reserved) == reserved);
}

TEST_CASE("Each registry has its own allocator", "[runtime][entity-allocator]") {
    entt::registry first;
    entt::registry second;
    (void)create_entity(first);
    (void)create_entity(first);

    CHECK(Traits::to_entity(create_entity(second)) == 0U);
}

TEST_CASE("Running out of handles is an error", "[runtime][entity-allocator]") {
    entt::registry registry;
    for (std::uint32_t index = 0; index < Traits::entity_mask; ++index) {
        (void)create_entity(registry);
    }

    try {
        (void)create_entity(registry);
        FAIL("expected exhaustion");
    } catch (const std::runtime_error& error) {
        CHECK(std::string{error.what()} == "cactus entity handle space exhausted");
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
