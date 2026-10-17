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

TEST_CASE("emit_event queues for this cascade with its route", "[runtime][activation][scheduler]") {
    ActivationRuntime<TestOccurrence> activation;

    SECTION("an ordinary emit reaches every consumer") {
        emit_event(activation, PingEvent{});

        REQUIRE(activation.event_queue.size() == 1);
        CHECK(activation.event_queue.front().route == DeliveryRoute::All);
        CHECK(activation.event_queue.front().component == kNoComponent);
    }

    SECTION("an immediate part of a split emit keeps its producer cycle") {
        emit_event(activation, PingEvent{}, DeliveryRoute::ImmediateOnly, 3U);

        REQUIRE(activation.event_queue.size() == 1);
        CHECK(activation.event_queue.front().route == DeliveryRoute::ImmediateOnly);
        CHECK(activation.event_queue.front().component == 3U);
    }
}

TEST_CASE("route_admits splits consumers by the producer's cycle", "[runtime][activation][scheduler]") {
    CHECK(route_admits(DeliveryRoute::All, 2U, 2U));
    CHECK(route_admits(DeliveryRoute::All, 2U, kNoComponent));
    CHECK_FALSE(route_admits(DeliveryRoute::ImmediateOnly, 2U, 2U));
    CHECK(route_admits(DeliveryRoute::ImmediateOnly, 2U, 5U));
    CHECK(route_admits(DeliveryRoute::ImmediateOnly, 2U, kNoComponent));
    CHECK(route_admits(DeliveryRoute::DelayedOnly, 2U, 2U));
    CHECK_FALSE(route_admits(DeliveryRoute::DelayedOnly, 2U, kNoComponent));
}

TEST_CASE("a delayed emit with no clock set throws", "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;

    CHECK_THROWS_AS(emit_delayed_event(activation, PingEvent{}, DeliveryRoute::DelayedOnly, 0U), std::runtime_error);
    CHECK_THROWS_AS(
        emit_delayed_targeted_event(activation, PingEvent{}, registry.create(), DeliveryRoute::DelayedOnly, 0U),
        std::runtime_error);
}

TEST_CASE("delayed occurrences keep production order and recipient", "[runtime][activation][scheduler]") {
    entt::registry registry;
    const auto target = registry.create();
    DelayedQueue<TestOccurrence> clock;
    ActivationRuntime<TestOccurrence> activation;
    activation.current_delayed = &clock;

    emit_delayed_targeted_event(activation, PingEvent{}, target, DeliveryRoute::DelayedOnly, 1U);
    emit_delayed_event(activation, PongEvent{}, DeliveryRoute::DelayedOnly, 4U);

    CHECK(activation.event_queue.empty());
    REQUIRE(clock.size() == 2);
    CHECK(std::holds_alternative<PingEvent>(clock[0].occurrence));
    REQUIRE(clock[0].target.has_value());
    CHECK(*clock[0].target == target);
    CHECK(clock[0].component == 1U);
    CHECK(std::holds_alternative<PongEvent>(clock[1].occurrence));
    CHECK_FALSE(clock[1].target.has_value());
    CHECK(clock[1].route == DeliveryRoute::DelayedOnly);
}

TEST_CASE("delayed occurrences enter the cascade ahead of the activation's own emissions",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    DelayedQueue<TestOccurrence> clock;
    ActivationRuntime<TestOccurrence> activation;
    activation.current_delayed = &clock;
    emit_delayed_event(activation, PingEvent{}, DeliveryRoute::DelayedOnly, 0U);
    emit_delayed_event(activation, PongEvent{}, DeliveryRoute::DelayedOnly, 0U);

    // The next activation of the clock: its phase handlers emitted Chain first.
    emit_event(activation, ChainEvent{});
    deliver_delayed_events(activation);
    // Delays produced while delivering wait for the activation after this one.
    std::vector<std::string> order;
    auto dispatch = [&](entt::registry&, const auto& occurrence, std::optional<entt::entity>) {
        if constexpr (is_event_v<PingEvent, decltype(occurrence)>) {
            order.emplace_back("ping");
            emit_delayed_event(activation, PingEvent{}, DeliveryRoute::DelayedOnly, 0U);
        } else if constexpr (is_event_v<PongEvent, decltype(occurrence)>) {
            order.emplace_back("pong");
        } else {
            order.emplace_back("chain");
        }
    };
    drain_event_cascade(activation, registry, dispatch);

    CHECK(order == std::vector<std::string>{"ping", "pong", "chain"});
    REQUIRE(clock.size() == 1);
    CHECK(std::holds_alternative<PingEvent>(clock.front().occurrence));
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
}

TEST_CASE("a delayed occurrence whose recipient is gone is dropped at delivery", "[runtime][activation][scheduler]") {
    entt::registry registry;
    const auto target = registry.create();
    DelayedQueue<TestOccurrence> clock;
    ActivationRuntime<TestOccurrence> activation;
    activation.current_delayed = &clock;
    emit_delayed_targeted_event(activation, PingEvent{}, target, DeliveryRoute::DelayedOnly, 0U);
    registry.destroy(target);

    deliver_delayed_events(activation);
    int dispatched = 0;
    drain_event_cascade(activation, registry, [&](entt::registry&, const auto&, std::optional<entt::entity>) {
        ++dispatched;
    });

    CHECK(dispatched == 0);
    CHECK(clock.empty());
}

TEST_CASE("drain_event_cascade exposes each occurrence's route while it is dispatched",
          "[runtime][activation][scheduler]") {
    entt::registry registry;
    ActivationRuntime<TestOccurrence> activation;
    emit_event(activation, PingEvent{}, DeliveryRoute::ImmediateOnly, 2U);
    emit_event(activation, PongEvent{});

    std::vector<std::pair<DeliveryRoute, std::uint32_t>> seen;
    drain_event_cascade(activation, registry, [&](entt::registry&, const auto&, std::optional<entt::entity>) {
        seen.emplace_back(activation.delivery_route, activation.delivery_component);
    });

    CHECK(seen == std::vector<std::pair<DeliveryRoute, std::uint32_t>>{{DeliveryRoute::ImmediateOnly, 2U},
                                                                      {DeliveryRoute::All, kNoComponent}});
    CHECK(activation.delivery_route == DeliveryRoute::All);
    CHECK(activation.delivery_component == kNoComponent);
}

TEST_CASE("queue_structural_command records the running handler's cycle", "[runtime][activation][scheduler]") {
    ActivationRuntime<TestOccurrence> activation;
    activation.active            = true;
    activation.current_component = 7U;

    queue_structural_command(activation, StructuralCommand::Kind::Add, [](entt::registry&) {});

    REQUIRE(activation.commands.size() == 1);
    CHECK(activation.commands.front().component == 7U);
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
    DeliveryRoute route = DeliveryRoute::All;
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

    DelayedQueue<LifecycleOccurrence> clock;

    LifecycleHarness() {
        activation.active          = true;
        activation.current_delayed = &clock;
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
                              .route  = activation.delivery_route};
            if constexpr (std::is_same_v<std::decay_t<decltype(occurrence)>, TraitRemoved<Burning>>) {
                delivery.old_intensity = occurrence.old.intensity;
            }
            deliveries.push_back(delivery);
            on_delivery(delivery);
        };
    }

    template <typename OnDelivery, typename Routes = ImmediateLifecycle>
    void commit(OnDelivery on_delivery, Routes routes = {}) {
        auto dispatch = dispatcher(on_delivery);
        commit_activation(
            activation,
            registry,
            [&](entt::registry& reg) { drain_event_cascade(activation, reg, dispatch); },
            lifecycle,
            routes);
    }

    // The start of the clock's next activation: delayed deliveries, then their cascade.
    template <typename OnDelivery>
    void next_activation(OnDelivery on_delivery) {
        deliver_delayed_events(activation);
        auto dispatch = dispatcher(on_delivery);
        drain_event_cascade(activation, registry, dispatch);
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
    CHECK(h.deliveries[0].route == DeliveryRoute::All);
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
    CHECK(h.registry.all_of<Burning>(entity));
    CHECK(h.activation.commands.empty());
}

namespace {

// Every watched trait's consumers sit on cycle 0; `outside` adds consumers off it.
struct CycleZeroRoutes {
    bool outside = false;

    [[nodiscard]] LifecycleSplit operator()(std::size_t /*trait_index*/, bool /*added*/, std::uint32_t component) const {
        if (component != 0U) {
            return {};
        }
        return {.immediate = outside, .delayed = true};
    }
};

}  // namespace

TEST_CASE("Lifecycle: a self-feeding add/remove commits one round per activation and queues the trigger",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    h.activation.current_component = 0U;
    const auto entity              = h.registry.create();
    auto toggle                    = [&](const Delivery& delivery) {
        if (delivery.kind == "added Pulse") {
            h.queue(StructuralCommand::Kind::Remove,
                    [target = delivery.target](entt::registry& reg) { reg.remove<Pulse>(target); });
        } else if (delivery.kind == "removed Pulse") {
            h.queue(StructuralCommand::Kind::Add,
                    [target = delivery.target](entt::registry& reg) { reg.emplace<Pulse>(target); });
        }
    };

    h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Pulse>(entity); });
    h.commit(toggle, CycleZeroRoutes{});

    CHECK(h.deliveries.empty());
    CHECK(h.activation.commands.empty());
    CHECK(h.registry.all_of<Pulse>(entity));
    REQUIRE(h.clock.size() == 1);
    CHECK(std::holds_alternative<TraitAdded<Pulse>>(h.clock.front().occurrence));
    CHECK(*h.clock.front().target == entity);

    for (int activation = 0; activation < 3; ++activation) {
        const bool had_pulse = h.registry.all_of<Pulse>(entity);
        h.next_activation(toggle);
        REQUIRE(h.activation.commands.size() == 1);
        h.commit(toggle, CycleZeroRoutes{});
        CHECK(h.registry.all_of<Pulse>(entity) != had_pulse);
        CHECK(h.deliveries.size() == static_cast<std::size_t>(activation) + 1);
        CHECK(h.clock.size() == 1);
    }
    CHECK(h.kinds() == std::vector<std::string>{"added Pulse", "removed Pulse", "added Pulse"});
}

TEST_CASE("Lifecycle: a delivery splits into immediate and delayed consumers by producer component",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();

    SECTION("a producer on the consumers' cycle splits the delivery") {
        h.activation.current_component = 0U;
        h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Dying>(entity); });
        h.commit([](const Delivery&) {}, CycleZeroRoutes{.outside = true});

        REQUIRE(h.deliveries.size() == 1);
        CHECK(h.deliveries[0].route == DeliveryRoute::ImmediateOnly);
        REQUIRE(h.clock.size() == 1);
        CHECK(h.clock.front().route == DeliveryRoute::DelayedOnly);
        CHECK(h.clock.front().component == 0U);
        CHECK(*h.clock.front().target == entity);
    }
    SECTION("a producer off every cycle delivers immediately to all") {
        h.activation.current_component = 4U;
        h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace<Dying>(entity); });
        h.commit([](const Delivery&) {}, CycleZeroRoutes{.outside = true});

        REQUIRE(h.deliveries.size() == 1);
        CHECK(h.deliveries[0].route == DeliveryRoute::All);
        CHECK(h.clock.empty());
    }
}

namespace {

void queue_add_dying(LifecycleHarness& h, entt::entity entity, std::uint32_t component) {
    h.activation.current_component = component;
    h.queue(StructuralCommand::Kind::Add, [entity](entt::registry& reg) { reg.emplace_or_replace<Dying>(entity); });
}

void queue_remove_dying(LifecycleHarness& h, entt::entity entity, std::uint32_t component) {
    h.activation.current_component = component;
    h.queue(StructuralCommand::Kind::Remove, [entity](entt::registry& reg) { reg.remove<Dying>(entity); });
}

}  // namespace

TEST_CASE("Lifecycle: a change with several causes is immediate when any cause is off the consumers' cycle",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();

    SECTION("the cycle's command first") {
        queue_add_dying(h, entity, 0U);
        queue_add_dying(h, entity, 4U);
    }
    SECTION("the other command first") {
        queue_add_dying(h, entity, 4U);
        queue_add_dying(h, entity, 0U);
    }
    SECTION("a cause on no cycle") {
        queue_add_dying(h, entity, 0U);
        queue_add_dying(h, entity, kNoComponent);
    }
    h.commit([](const Delivery&) {}, CycleZeroRoutes{});

    REQUIRE(h.deliveries.size() == 1);
    CHECK(h.deliveries[0].kind == "added Dying");
    CHECK(h.deliveries[0].route == DeliveryRoute::All);
    CHECK(h.clock.empty());
}

TEST_CASE("Lifecycle: a change whose causes all share the consumers' cycle is delayed",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();
    queue_add_dying(h, entity, 0U);
    queue_add_dying(h, entity, 0U);
    h.commit([](const Delivery&) {}, CycleZeroRoutes{});

    CHECK(h.deliveries.empty());
    REQUIRE(h.clock.size() == 1);
    CHECK(h.clock.front().route == DeliveryRoute::DelayedOnly);
    CHECK(h.clock.front().component == 0U);
}

TEST_CASE("Lifecycle: only commands in the net change's direction decide its route",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    const auto entity = h.registry.create();

    SECTION("add, remove, add: the remove's cycle is ignored") {
        queue_add_dying(h, entity, 0U);
        queue_remove_dying(h, entity, 4U);
        queue_add_dying(h, entity, 0U);
        h.commit([](const Delivery&) {}, CycleZeroRoutes{});

        CHECK(h.deliveries.empty());
        REQUIRE(h.clock.size() == 1);
        CHECK(std::holds_alternative<TraitAdded<Dying>>(h.clock.front().occurrence));
    }
    SECTION("remove, add, remove: the add's cycle is ignored") {
        h.registry.emplace<Dying>(entity);
        queue_remove_dying(h, entity, 0U);
        queue_add_dying(h, entity, 4U);
        queue_remove_dying(h, entity, 0U);
        h.commit([](const Delivery&) {}, CycleZeroRoutes{});

        CHECK(h.deliveries.empty());
        REQUIRE(h.clock.size() == 1);
        CHECK(std::holds_alternative<TraitRemoved<Dying>>(h.clock.front().occurrence));
    }
    SECTION("a set after the add is not a cause") {
        queue_add_dying(h, entity, 0U);
        h.activation.current_component = 4U;
        h.queue(StructuralCommand::Kind::Set, [entity](entt::registry& reg) { reg.patch<Dying>(entity); });
        h.commit([](const Delivery&) {}, CycleZeroRoutes{});

        CHECK(h.deliveries.empty());
        CHECK(h.clock.size() == 1);
    }
}

TEST_CASE("Lifecycle: a delayed added delivery is skipped when the trait is gone",
          "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    h.activation.current_component = 0U;
    const auto kept                = h.registry.create();
    const auto lost                = h.registry.create();
    h.queue(StructuralCommand::Kind::Add, [kept](entt::registry& reg) { reg.emplace<Dying>(kept); });
    h.queue(StructuralCommand::Kind::Add, [lost](entt::registry& reg) { reg.emplace<Dying>(lost); });
    h.commit([](const Delivery&) {}, CycleZeroRoutes{});
    REQUIRE(h.clock.size() == 2);

    h.registry.remove<Dying>(lost);
    h.next_activation([](const Delivery&) {});

    REQUIRE(h.deliveries.size() == 1);
    CHECK(h.deliveries[0].target == kept);
    CHECK(h.deliveries[0].route == DeliveryRoute::DelayedOnly);
}

TEST_CASE("Lifecycle: a delayed removed delivery keeps its snapshot", "[runtime][activation][trait-lifecycle]") {
    LifecycleHarness h;
    h.activation.current_component = 0U;
    const auto entity              = h.registry.create();
    h.registry.emplace<Burning>(entity, Burning{.intensity = 3.0F});
    h.queue(StructuralCommand::Kind::Remove, [entity](entt::registry& reg) { reg.remove<Burning>(entity); });
    h.commit([](const Delivery&) {}, CycleZeroRoutes{});
    REQUIRE(h.clock.size() == 1);

    SECTION("delivered with the value from before removal") {
        h.next_activation([](const Delivery&) {});
        REQUIRE(h.deliveries.size() == 1);
        CHECK(h.deliveries[0].kind == "removed Burning");
        CHECK(h.deliveries[0].old_intensity == 3.0F);
    }
    SECTION("skipped when the trait came back") {
        h.registry.emplace<Burning>(entity, Burning{.intensity = 8.0F});
        h.next_activation([](const Delivery&) {});
        CHECK(h.deliveries.empty());
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

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
