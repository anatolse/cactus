// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

using namespace cactus::runtime::entt_backend;

namespace {

struct SpawnNotify {};
struct DestroyNotify {};
struct ChainEvent {};

using TestOccurrence = std::variant<SpawnNotify, DestroyNotify, ChainEvent>;

// Stops a regressed, unbounded commit loop from hanging the test run.
constexpr int kRunawayGuard = 10'000;

template <typename Event, typename Occurrence>
constexpr bool is_event_v = std::is_same_v<std::decay_t<Occurrence>, Event>;

}  // namespace

TEST_CASE("generated_next_creation_ordinal produces a monotonic, non-reused sequence", "[runtime][activation]") {
    const auto first  = generated_next_creation_ordinal();
    const auto second = generated_next_creation_ordinal();
    const auto third  = generated_next_creation_ordinal();

    CHECK(second == first + 1);
    CHECK(third == second + 1);

    std::unordered_set<std::uint64_t> seen{first, second, third};
    CHECK(seen.size() == 3);
}

TEST_CASE("StructuralCommand carries a Kind and an apply callback", "[runtime][activation]") {
    bool applied     = false;
    entt::registry registry;
    StructuralCommand command{.kind = StructuralCommand::Kind::Spawn,
                              .apply = [&](entt::registry&) { applied = true; }};

    CHECK(command.kind == StructuralCommand::Kind::Spawn);
    command.apply(registry);
    CHECK(applied);
}

TEST_CASE("StructuralCommand::Kind covers spawn, destroy, add, and remove", "[runtime][activation]") {
    CHECK(StructuralCommand::Kind::Spawn != StructuralCommand::Kind::Destroy);
    CHECK(StructuralCommand::Kind::Add != StructuralCommand::Kind::Remove);
}

TEST_CASE("reserve_entity throws once the deferred entity identifier space is exhausted",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;
    activation.next_reserved_entity = 0U;

    CHECK_THROWS_AS(reserve_entity(registry, activation), std::runtime_error);
}

TEST_CASE("reserve_entity returns a not-yet-valid entity and counts down", "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;

    const auto first  = reserve_entity(registry, activation);
    const auto second = reserve_entity(registry, activation);

    CHECK_FALSE(registry.valid(first));
    CHECK_FALSE(registry.valid(second));
    CHECK(first != second);
}

TEST_CASE("queue_structural_command throws when no activation is active", "[runtime][activation][scheduler]") {
    ActivationRuntime<TestOccurrence> activation;
    activation.active = false;

    CHECK_THROWS_AS(queue_structural_command(activation, StructuralCommand::Kind::Spawn, [](entt::registry&) {}),
                    std::runtime_error);
}

TEST_CASE("queue_structural_command records a command while an activation is active",
          "[runtime][activation][scheduler]") {
    ActivationRuntime<TestOccurrence> activation;
    activation.active = true;

    queue_structural_command(activation, StructuralCommand::Kind::Spawn, [](entt::registry&) {});

    REQUIRE(activation.commands.size() == 1);
    CHECK(activation.commands.front().kind == StructuralCommand::Kind::Spawn);
}

TEST_CASE("emit_event enqueues below the cascade-depth cap and defers past it",
          "[runtime][activation][scheduler]") {
    ActivationRuntime<TestOccurrence> activation;

    SECTION("below the cap") {
        activation.current_cascade_depth = 0;
        emit_event(activation, SpawnNotify{});

        REQUIRE(activation.event_queue.size() == 1);
        CHECK(activation.event_queue.front().cascade_depth == 1);
        CHECK(activation.deferred_events.empty());
    }

    SECTION("past the cap defers with a reset cascade depth") {
        activation.current_cascade_depth = kMaxEventCascadeDepth;
        emit_event(activation, SpawnNotify{});

        CHECK(activation.event_queue.empty());
        REQUIRE(activation.deferred_events.size() == 1);
        CHECK(activation.deferred_events.front().cascade_depth == 0);
    }
}

TEST_CASE("emit_targeted_event carries its target through both enqueue and deferral",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    const auto target = registry.create();
    ActivationRuntime<TestOccurrence> activation;

    SECTION("below the cap") {
        activation.current_cascade_depth = 0;
        emit_targeted_event(activation, SpawnNotify{}, target);

        REQUIRE(activation.event_queue.size() == 1);
        REQUIRE(activation.event_queue.front().target.has_value());
        CHECK(*activation.event_queue.front().target == target);
    }

    SECTION("past the cap") {
        activation.current_cascade_depth = kMaxEventCascadeDepth;
        emit_targeted_event(activation, SpawnNotify{}, target);

        REQUIRE(activation.deferred_events.size() == 1);
        REQUIRE(activation.deferred_events.front().target.has_value());
        CHECK(*activation.deferred_events.front().target == target);
    }
}

TEST_CASE("drain_event_cascade dispatches valid events and drops events targeting a no-longer-valid entity",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    const auto live_target  = registry.create();
    const auto stale_target = registry.create();
    registry.destroy(stale_target);

    ActivationRuntime<TestOccurrence> activation;
    activation.event_queue.push_back(
        QueuedEvent<TestOccurrence>{.occurrence = TestOccurrence{SpawnNotify{}}, .target = stale_target});
    activation.event_queue.push_back(
        QueuedEvent<TestOccurrence>{.occurrence = TestOccurrence{DestroyNotify{}}, .target = live_target});

    std::vector<std::optional<entt::entity>> dispatched_targets;
    auto dispatch = [&](entt::registry&, const auto&, std::optional<entt::entity> target) {
        dispatched_targets.push_back(target);
    };

    drain_event_cascade(activation, registry, dispatch);

    REQUIRE(dispatched_targets.size() == 1);
    REQUIRE(dispatched_targets.front().has_value());
    CHECK(*dispatched_targets.front() == live_target);
    CHECK(activation.current_cascade_depth == 0);
}

TEST_CASE("commit_activation with no notification hooks applies queued commands exactly once",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;
    activation.active = true;

    int applied = 0;
    activation.commands.push_back(
        StructuralCommand{.kind = StructuralCommand::Kind::Spawn, .apply = [&](entt::registry&) { ++applied; }});

    // A handler-less commit takes the single-pass branch, which never calls
    // drain_cascade at all — failing loudly here catches a regression of
    // that guarantee.
    auto drain_cascade = [](entt::registry&) { FAIL("drain_cascade must not run without notification hooks"); };
    commit_activation(activation, registry, drain_cascade);

    CHECK(applied == 1);
    CHECK(activation.commands.empty());
}

TEST_CASE("commit_activation loops while an OnSpawn hook keeps producing new commands",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;
    activation.active = true;

    int applied     = 0;
    int spawn_count = 0;

    // Simulates one `on spawn` handler re-spawning a single further entity:
    // the dispatch handler (invoked by drain_cascade, which mirrors codegen's
    // generated_drain_event_cascade wrapping runtime drain_event_cascade)
    // queues exactly one more Spawn command the first time it observes a
    // SpawnNotify, so the loop runs exactly twice.
    auto dispatch = [&](entt::registry&, const auto& occurrence, std::optional<entt::entity>) {
        if constexpr (std::is_same_v<std::decay_t<decltype(occurrence)>, SpawnNotify>) {
            if (spawn_count == 0) {
                ++spawn_count;
                queue_structural_command(
                    activation, StructuralCommand::Kind::Spawn, [&](entt::registry&) { ++applied; });
            }
        }
    };
    auto drain_cascade = [&](entt::registry& reg) { drain_event_cascade(activation, reg, dispatch); };
    auto on_spawn      = [](ActivationRuntime<TestOccurrence>& act) { emit_event(act, SpawnNotify{}); };

    activation.commands.push_back(
        StructuralCommand{.kind = StructuralCommand::Kind::Spawn, .apply = [&](entt::registry&) { ++applied; }});

    commit_activation(activation, registry, drain_cascade, on_spawn);

    CHECK(applied == 2);
    CHECK(spawn_count == 1);
    CHECK(activation.commands.empty());
}

TEST_CASE("commit_activation terminates when every spawn notification queues another spawn",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;
    activation.active = true;

    int applied   = 0;
    int delivered = 0;
    std::vector<std::size_t> delivery_depths;
    auto dispatch = [&](entt::registry&, const auto& occurrence, std::optional<entt::entity>) {
        if constexpr (is_event_v<SpawnNotify, decltype(occurrence)>) {
            ++delivered;
            delivery_depths.push_back(activation.current_cascade_depth);
            if (delivered < kRunawayGuard) {
                queue_structural_command(
                    activation, StructuralCommand::Kind::Spawn, [&](entt::registry&) { ++applied; });
            }
        }
    };
    auto drain_cascade = [&](entt::registry& reg) { drain_event_cascade(activation, reg, dispatch); };
    auto on_spawn      = [](ActivationRuntime<TestOccurrence>& act) { emit_event(act, SpawnNotify{}); };

    activation.commands.push_back(
        StructuralCommand{.kind = StructuralCommand::Kind::Spawn, .apply = [&](entt::registry&) { ++applied; }});

    commit_activation(activation, registry, drain_cascade, on_spawn);

    CHECK(delivered == static_cast<int>(kMaxEventCascadeDepth));
    CHECK(applied == static_cast<int>(kMaxEventCascadeDepth) + 1);
    CHECK(activation.commands.empty());
    REQUIRE(activation.deferred_events.size() == 1);
    CHECK(std::holds_alternative<SpawnNotify>(activation.deferred_events.front().occurrence));
    CHECK(activation.current_cascade_depth == 0);

    SECTION("round r's notification is dispatched at cascade depth r") {
        REQUIRE(delivery_depths.size() == kMaxEventCascadeDepth);
        for (std::size_t round = 1; round <= delivery_depths.size(); ++round) {
            CHECK(delivery_depths[round - 1] == round);
        }
    }
}

TEST_CASE("commit_activation terminates when every destroy notification queues another destroy",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;
    activation.active = true;

    int applied   = 0;
    int delivered = 0;
    auto dispatch = [&](entt::registry&, const auto& occurrence, std::optional<entt::entity>) {
        if constexpr (is_event_v<DestroyNotify, decltype(occurrence)>) {
            ++delivered;
            if (delivered < kRunawayGuard) {
                queue_structural_command(
                    activation, StructuralCommand::Kind::Destroy, [&](entt::registry&) { ++applied; });
            }
        }
    };
    auto drain_cascade = [&](entt::registry& reg) { drain_event_cascade(activation, reg, dispatch); };
    auto on_destroy    = [](ActivationRuntime<TestOccurrence>& act) { emit_event(act, DestroyNotify{}); };

    activation.commands.push_back(
        StructuralCommand{.kind = StructuralCommand::Kind::Destroy, .apply = [&](entt::registry&) { ++applied; }});

    commit_activation(activation, registry, drain_cascade, NoNotify{}, on_destroy);

    CHECK(delivered == static_cast<int>(kMaxEventCascadeDepth));
    CHECK(applied == static_cast<int>(kMaxEventCascadeDepth) + 1);
    REQUIRE(activation.deferred_events.size() == 1);
    CHECK(std::holds_alternative<DestroyNotify>(activation.deferred_events.front().occurrence));
    CHECK(activation.current_cascade_depth == 0);
}

TEST_CASE("commit_activation shares the cascade-depth budget between rounds and their event chains",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;
    activation.active = true;

    // Round 1's notification queues one more spawn; round 2's notification
    // (depth 2) starts an endless ChainEvent chain, which must defer past the
    // bound instead of getting a fresh budget.
    int spawn_notifications = 0;
    int chain_delivered     = 0;
    auto dispatch = [&](entt::registry&, const auto& occurrence, std::optional<entt::entity>) {
        if constexpr (is_event_v<SpawnNotify, decltype(occurrence)>) {
            ++spawn_notifications;
            if (spawn_notifications == 1) {
                queue_structural_command(activation, StructuralCommand::Kind::Spawn, [](entt::registry&) {});
            } else {
                emit_event(activation, ChainEvent{});
            }
        } else if constexpr (is_event_v<ChainEvent, decltype(occurrence)>) {
            ++chain_delivered;
            if (chain_delivered < kRunawayGuard) {
                emit_event(activation, ChainEvent{});
            }
        }
    };
    auto drain_cascade = [&](entt::registry& reg) { drain_event_cascade(activation, reg, dispatch); };
    auto on_spawn      = [](ActivationRuntime<TestOccurrence>& act) { emit_event(act, SpawnNotify{}); };

    activation.commands.push_back(
        StructuralCommand{.kind = StructuralCommand::Kind::Spawn, .apply = [](entt::registry&) {}});

    commit_activation(activation, registry, drain_cascade, on_spawn);

    CHECK(spawn_notifications == 2);
    CHECK(chain_delivered == static_cast<int>(kMaxEventCascadeDepth) - 2);
    REQUIRE(activation.deferred_events.size() == 1);
    CHECK(std::holds_alternative<ChainEvent>(activation.deferred_events.front().occurrence));
    CHECK(activation.current_cascade_depth == 0);
}

TEST_CASE("notify_structural_command fires no lifecycle notification for a Set command",
          "[runtime][activation][deferred-set]") {
    ActivationRuntime<TestOccurrence> activation;
    int spawn_notified   = 0;
    int destroy_notified = 0;
    auto on_spawn        = [&](ActivationRuntime<TestOccurrence>&) { ++spawn_notified; };
    auto on_destroy      = [&](ActivationRuntime<TestOccurrence>&) { ++destroy_notified; };

    const StructuralCommand command{.kind = StructuralCommand::Kind::Set, .apply = [](entt::registry&) {}};
    notify_structural_command(activation, command, on_spawn, on_destroy);

    CHECK(spawn_notified == 0);
    CHECK(destroy_notified == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
