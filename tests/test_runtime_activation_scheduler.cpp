// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

using namespace cactus::runtime::entt_backend;

namespace {

struct PingEvent {};
struct PongEvent {};
struct ChainEvent {};

using TestOccurrence = std::variant<PingEvent, PongEvent, ChainEvent>;

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
        emit_event(activation, PingEvent{});

        REQUIRE(activation.event_queue.size() == 1);
        CHECK(activation.event_queue.front().cascade_depth == 1);
        CHECK(activation.deferred_events.empty());
    }

    SECTION("past the cap defers with a reset cascade depth") {
        activation.current_cascade_depth = kMaxEventCascadeDepth;
        emit_event(activation, PingEvent{});

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
        emit_targeted_event(activation, PingEvent{}, target);

        REQUIRE(activation.event_queue.size() == 1);
        REQUIRE(activation.event_queue.front().target.has_value());
        CHECK(*activation.event_queue.front().target == target);
    }

    SECTION("past the cap") {
        activation.current_cascade_depth = kMaxEventCascadeDepth;
        emit_targeted_event(activation, PingEvent{}, target);

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
        QueuedEvent<TestOccurrence>{.occurrence = TestOccurrence{PingEvent{}}, .target = stale_target});
    activation.event_queue.push_back(
        QueuedEvent<TestOccurrence>{.occurrence = TestOccurrence{PongEvent{}}, .target = live_target});

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

TEST_CASE("commit_activation with no lifecycle trackers applies queued commands exactly once",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;
    activation.active = true;

    int applied = 0;
    activation.commands.push_back(
        StructuralCommand{.kind = StructuralCommand::Kind::Spawn, .apply = [&](entt::registry&) { ++applied; }});

    // Without lifecycle trackers commit is a single pass that never drains.
    auto drain_cascade = [](entt::registry&) { FAIL("drain_cascade must not run without lifecycle trackers"); };
    commit_activation(activation, registry, drain_cascade);

    CHECK(applied == 1);
    CHECK(activation.commands.empty());
}

// ── Trait lifecycle triggers ────────────────────────────────────────────────

namespace {

struct Dying {};
struct Burning {
    float intensity = 1.0F;
};
struct Pulse {};
struct Unwatched {};

using LifecycleOccurrence = std::variant<ChainEvent,
                                         TraitAdded<Dying>,
                                         TraitRemoved<Dying>,
                                         TraitAdded<Burning>,
                                         TraitRemoved<Burning>,
                                         TraitAdded<Pulse>,
                                         TraitRemoved<Pulse>>;

struct Delivery {
    std::string kind;
    entt::entity target{entt::null};
    std::size_t depth{};
    float old_intensity{};
};

std::string describe(const ChainEvent&) { return "chain"; }
std::string describe(const TraitAdded<Dying>&) { return "added Dying"; }
std::string describe(const TraitRemoved<Dying>&) { return "removed Dying"; }
std::string describe(const TraitAdded<Burning>&) { return "added Burning"; }
std::string describe(const TraitRemoved<Burning>&) { return "removed Burning"; }
std::string describe(const TraitAdded<Pulse>&) { return "added Pulse"; }
std::string describe(const TraitRemoved<Pulse>&) { return "removed Pulse"; }

struct LifecycleHarness {
    // Declared before the registry so the registry is torn down first.
    LifecycleTrackers<Dying, Burning, Pulse> lifecycle;
    entt::registry registry;
    ActivationRuntime<LifecycleOccurrence> activation;
    std::vector<Delivery> deliveries;

    LifecycleHarness() {
        activation.active = true;
        lifecycle.connect(registry);
    }

    void queue(StructuralCommand::Kind kind, std::function<void(entt::registry&)> apply) {
        queue_structural_command(activation, kind, std::move(apply));
    }

    template <typename OnDelivery>
    auto dispatcher(OnDelivery& on_delivery) {
        return [this, &on_delivery](entt::registry&, const auto& occurrence, std::optional<entt::entity> target) {
            Delivery delivery{.kind   = describe(occurrence),
                              .target = target.value_or(entt::entity{entt::null}),
                              .depth  = activation.current_cascade_depth};
            if constexpr (std::is_same_v<std::decay_t<decltype(occurrence)>, TraitRemoved<Burning>>) {
                delivery.old_intensity = occurrence.old.intensity;
            }
            deliveries.push_back(delivery);
            on_delivery(delivery);
        };
    }

    template <typename OnDelivery>
    void commit(OnDelivery on_delivery) {
        auto dispatch = dispatcher(on_delivery);
        commit_activation(
            activation,
            registry,
            [&](entt::registry& reg) { drain_event_cascade(activation, reg, dispatch); },
            lifecycle);
    }

    void commit() {
        commit([](const Delivery&) {});
    }

    [[nodiscard]] std::vector<std::string> kinds() const {
        std::vector<std::string> result;
        for (const auto& delivery : deliveries) {
            result.push_back(delivery.kind);
        }
        return result;
    }
};

}  // namespace

TEST_CASE("Lifecycle: add delivers added to the changed entity only", "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto changed = h.registry.create();
    const auto other   = h.registry.create();
    h.registry.emplace<Burning>(other);

    h.queue(StructuralCommand::Kind::Add, [changed](entt::registry& reg) { reg.emplace<Dying>(changed); });
    h.commit();

    REQUIRE(h.deliveries.size() == 1);
    CHECK(h.deliveries[0].kind == "added Dying");
    CHECK(h.deliveries[0].target == changed);
    CHECK(h.deliveries[0].depth == 1);
}

TEST_CASE("Lifecycle: remove delivers removed with the pre-removal snapshot",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();
    h.registry.emplace<Burning>(entity, Burning{.intensity = 3.0F});

    h.queue(StructuralCommand::Kind::Remove, [entity](entt::registry& reg) { reg.remove<Burning>(entity); });
    h.commit();

    REQUIRE(h.deliveries.size() == 1);
    CHECK(h.deliveries[0].kind == "removed Burning");
    CHECK(h.deliveries[0].target == entity);
    CHECK(h.deliveries[0].old_intensity == 3.0F);
}

TEST_CASE("Lifecycle: only the net change per round fires", "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();

    SECTION("add then remove fires nothing") {
        h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Dying>(entity); });
        h.queue(StructuralCommand::Kind::Remove, [entity](entt::registry& reg) { reg.remove<Dying>(entity); });
    }
    SECTION("remove then add fires nothing") {
        h.registry.emplace<Burning>(entity, Burning{.intensity = 2.0F});
        h.queue(StructuralCommand::Kind::Remove, [entity](entt::registry& reg) { reg.remove<Burning>(entity); });
        h.queue(StructuralCommand::Kind::Add,
                [entity](entt::registry& reg) { reg.emplace<Burning>(entity, Burning{.intensity = 5.0F}); });
    }
    SECTION("replacing an existing trait fires nothing") {
        h.registry.emplace<Burning>(entity);
        h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) {
            reg.emplace_or_replace<Burning>(entity, Burning{.intensity = 9.0F});
        });
    }
    SECTION("set fires nothing") {
        h.registry.emplace<Burning>(entity);
        h.queue(StructuralCommand::Kind::Set,
                [entity](entt::registry& reg) { reg.patch<Burning>(entity, [](Burning& b) { b.intensity = 4.0F; }); });
    }
    SECTION("unwatched traits are not tracked") {
        h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Unwatched>(entity); });
    }

    h.commit();
    CHECK(h.deliveries.empty());
}

TEST_CASE("Lifecycle: destroyed entities fire nothing", "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;

    SECTION("destroy of an entity carrying watched traits") {
        const auto entity = h.registry.create();
        h.registry.emplace<Dying>(entity);
        h.registry.emplace<Burning>(entity);
        h.queue(StructuralCommand::Kind::Destroy, [entity](entt::registry& reg) { reg.destroy(entity); });
    }
    SECTION("spawn and destroy in one round") {
        auto spawned = std::make_shared<entt::entity>(entt::null);
        h.queue(StructuralCommand::Kind::Spawn, [spawned](entt::registry& reg) {
            *spawned = reg.create();
            reg.emplace<Dying>(*spawned);
        });
        h.queue(StructuralCommand::Kind::Destroy, [spawned](entt::registry& reg) { reg.destroy(*spawned); });
    }

    h.commit();
    CHECK(h.deliveries.empty());
}

TEST_CASE("Lifecycle: deliveries follow first-touch order, and a spawn's traits follow tracker order",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto first  = h.registry.create();
    const auto second = h.registry.create();

    h.queue(StructuralCommand::Kind::Add, [second](entt::registry& reg) { reg.emplace<Burning>(second); });
    h.queue(StructuralCommand::Kind::Add, [first](entt::registry& reg) { reg.emplace<Dying>(first); });
    // Emplaced against tracker order; the harness tracks Dying before Burning.
    auto spawned = std::make_shared<std::vector<entt::entity>>();
    h.queue(StructuralCommand::Kind::Spawn, [spawned](entt::registry& reg) {
        for (int index = 0; index < 2; ++index) {
            const auto entity = reg.create();
            spawned->push_back(entity);
            reg.emplace<Burning>(entity);
            reg.emplace<Dying>(entity);
        }
    });
    h.commit();

    CHECK(h.kinds() == std::vector<std::string>{"added Burning",
                                                "added Dying",
                                                "added Dying",
                                                "added Burning",
                                                "added Dying",
                                                "added Burning"});
    REQUIRE(h.deliveries.size() == 6);
    REQUIRE(spawned->size() == 2);
    CHECK(h.deliveries[0].target == second);
    CHECK(h.deliveries[1].target == first);
    CHECK(h.deliveries[2].target == spawned->at(0));
    CHECK(h.deliveries[3].target == spawned->at(0));
    CHECK(h.deliveries[4].target == spawned->at(1));
    CHECK(h.deliveries[5].target == spawned->at(1));
}

TEST_CASE("Lifecycle: a delivery observes every structural change of its round",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();
    auto bullet       = std::make_shared<entt::entity>(entt::null);
    bool saw_bullet   = false;

    h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Dying>(entity); });
    h.queue(StructuralCommand::Kind::Spawn, [bullet](entt::registry& reg) { *bullet = reg.create(); });
    h.commit([&](const Delivery&) { saw_bullet = h.registry.valid(*bullet); });

    REQUIRE(h.deliveries.size() == 1);
    CHECK(saw_bullet);
}

TEST_CASE("Lifecycle: changes outside the commit-apply window are not recorded",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();
    h.registry.emplace<Dying>(entity);
    h.registry.emplace<Burning>(entity);
    h.registry.remove<Burning>(entity);

    h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Unwatched>(entity); });
    h.commit();
    h.registry.remove<Dying>(entity);

    CHECK(h.deliveries.empty());
}

TEST_CASE("Lifecycle: commands from a delivery commit in the next round", "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();

    h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Dying>(entity); });
    h.commit([&](const Delivery& delivery) {
        if (delivery.kind == "added Dying") {
            h.queue(StructuralCommand::Kind::Add,
                    [target = delivery.target](entt::registry& reg) { reg.emplace<Burning>(target); });
        }
    });

    CHECK(h.kinds() == std::vector<std::string>{"added Dying", "added Burning"});
    REQUIRE(h.deliveries.size() == 2);
    CHECK(h.deliveries[0].depth == 1);
    CHECK(h.deliveries[1].depth == 2);
    CHECK(h.registry.all_of<Burning>(entity));
    CHECK(h.activation.commands.empty());
}

TEST_CASE("Lifecycle: self-feeding add/remove handlers terminate and defer past the bound",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();
    int delivered     = 0;
    auto toggle       = [&](const Delivery& delivery) {
        ++delivered;
        if (delivered >= kRunawayGuard) {
            return;
        }
        if (delivery.kind == "added Pulse") {
            h.queue(StructuralCommand::Kind::Remove,
                    [target = delivery.target](entt::registry& reg) { reg.remove<Pulse>(target); });
        } else if (delivery.kind == "removed Pulse") {
            h.queue(StructuralCommand::Kind::Add,
                    [target = delivery.target](entt::registry& reg) { reg.emplace<Pulse>(target); });
        }
    };

    h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Pulse>(entity); });
    h.commit(toggle);

    CHECK(delivered == static_cast<int>(kMaxEventCascadeDepth));
    CHECK(h.activation.commands.empty());
    CHECK(h.activation.current_cascade_depth == 0);
    REQUIRE(h.activation.deferred_events.size() == 1);
    const auto& deferred = h.activation.deferred_events.front();
    REQUIRE(deferred.target.has_value());
    CHECK(*deferred.target == entity);

    for (std::size_t round = 1; round <= h.deliveries.size(); ++round) {
        CHECK(h.deliveries[round - 1].depth == round);
    }

    SECTION("the deferred delivery and its commands belong to a later activation") {
        const bool pulse_before = h.registry.all_of<Pulse>(entity);
        const auto count_before = h.deliveries.size();
        // Replays the deferred occurrence as its own activation, as generated code does.
        h.activation.event_queue.push_back(std::move(h.activation.deferred_events.front()));
        h.activation.deferred_events.pop_front();
        auto later_toggle = [&](const Delivery& delivery) {
            if (h.deliveries.size() == count_before + 1) {
                toggle(delivery);
            }
        };
        auto dispatch = h.dispatcher(later_toggle);
        drain_event_cascade(h.activation, h.registry, dispatch);
        REQUIRE(h.activation.commands.size() == 1);
        h.commit(later_toggle);

        CHECK(h.registry.all_of<Pulse>(entity) != pulse_before);
        CHECK(h.deliveries.size() == count_before + 2);
    }
}

TEST_CASE("Lifecycle: arrivals follow creation order, then trait order", "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto later   = h.registry.create();
    const auto earlier = h.registry.create();
    h.registry.emplace<CreationOrdinal>(later, CreationOrdinal{.value = 5});
    h.registry.emplace<Burning>(later);
    h.registry.emplace<CreationOrdinal>(earlier, CreationOrdinal{.value = 2});
    h.registry.emplace<Burning>(earlier);
    h.registry.emplace<Dying>(earlier);

    h.lifecycle.deliver_arrivals(h.activation, h.registry);
    auto no_op    = [](const Delivery&) {};
    auto dispatch = h.dispatcher(no_op);
    drain_event_cascade(h.activation, h.registry, dispatch);

    CHECK(h.kinds() == std::vector<std::string>{"added Dying", "added Burning", "added Burning"});
    REQUIRE(h.deliveries.size() == 3);
    CHECK(h.deliveries[0].target == earlier);
    CHECK(h.deliveries[1].target == earlier);
    CHECK(h.deliveries[2].target == later);
}

TEST_CASE("Lifecycle: commit rounds share the cascade-depth budget with event chains",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();
    int chain_delivered = 0;

    // Round 1's delivery queues one more add; round 2's delivery (depth 2)
    // starts an endless chain, which must defer past the bound.
    h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Dying>(entity); });
    h.commit([&](const Delivery& delivery) {
        if (delivery.kind == "added Dying") {
            h.queue(StructuralCommand::Kind::Add,
                    [target = delivery.target](entt::registry& reg) { reg.emplace<Burning>(target); });
        } else if (delivery.kind == "added Burning" || delivery.kind == "chain") {
            if (delivery.kind == "chain") {
                ++chain_delivered;
            }
            if (chain_delivered < kRunawayGuard) {
                emit_event(h.activation, ChainEvent{});
            }
        }
    });

    CHECK(chain_delivered == static_cast<int>(kMaxEventCascadeDepth) - 2);
    REQUIRE(h.activation.deferred_events.size() == 1);
    CHECK(std::holds_alternative<ChainEvent>(h.activation.deferred_events.front().occurrence));
    CHECK(h.activation.current_cascade_depth == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
