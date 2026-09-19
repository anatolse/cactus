// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

namespace {

using cactus_headless_test::drive_frame;
using cactus_headless_test::init_and_load;

constexpr float kStep = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        cactus::runtime::entt_backend::generated_scheduler_state().std_core__fixed_tick = {};
        init_and_load(registry);
    }
};

template <typename Trait>
entt::entity entity_with(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    REQUIRE(view.begin() != view.end());
    return *view.begin();
}

void inject_and_drain(entt::registry& registry, auto occurrence) {
    cactus::runtime::entt_backend::generated_inject_external_event(occurrence);
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

}  // namespace

TEST_CASE("an ordinary SaveRequested is emitted from a rule handler",
          "[runtime][persistence][request][headless]") {
    World world;
    auto& registry = world.registry;

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    status.resubmitted      = true;  // isolate this test from the resubmit behavior
    status.pending_requests = 1;

    drive_frame(registry, kStep);

    CHECK(status.requests_sent == 1);
    CHECK(status.pending_requests == 0);
}

TEST_CASE("an injected SaveCompleted reaches its rule handler with the correlated fields",
          "[runtime][persistence][outcome][headless]") {
    World world;
    auto& registry = world.registry;

    inject_and_drain(registry,
                     std_persistence__SaveCompletedEvent{.slot = "slot1", .request_id = 42});

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    CHECK(status.completed_count == 1);
    CHECK(status.last_completed_slot == "slot1");
    CHECK(status.last_completed_id == 42);
}

TEST_CASE("an injected SaveFailed reaches its rule handler with slot, id, code, and message",
          "[runtime][persistence][outcome][headless]") {
    World world;
    auto& registry = world.registry;

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    status.resubmitted = true;  // isolate this test from the resubmit behavior

    inject_and_drain(registry,
                     std_persistence__SaveFailedEvent{
                         .slot = "slot1", .request_id = 7, .code = "io_error", .message = "disk full"});

    CHECK(status.failed_count == 1);
    CHECK(status.last_failed_slot == "slot1");
    CHECK(status.last_failed_id == 7);
    CHECK(status.last_code == "io_error");
    CHECK(status.last_message == "disk full");
}

TEST_CASE("repeated request IDs each produce their own correlated outcome",
          "[runtime][persistence][outcome][headless]") {
    World world;
    auto& registry = world.registry;

    inject_and_drain(registry,
                     std_persistence__SaveCompletedEvent{.slot = "a", .request_id = 1});
    inject_and_drain(registry,
                     std_persistence__SaveCompletedEvent{.slot = "b", .request_id = 1});

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    CHECK(status.completed_count == 2);
    CHECK(status.last_completed_slot == "b");
}

TEST_CASE("a SaveRequested reaches SaveFailed with adapter_unavailable when no adapter is registered",
          "[runtime][persistence][scheduling][headless]") {
    World world;
    auto& registry = world.registry;

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    status.resubmitted     = true;  // isolate this test from the resubmit behavior
    status.pending_requests = 1;

    drive_frame(registry, kStep);

    CHECK(status.completed_count == 0);
    CHECK(status.failed_count == 1);
    CHECK(status.last_failed_slot == "slot1");
    CHECK(status.last_failed_id == 1);
    CHECK(status.last_code == "adapter_unavailable");
}

TEST_CASE("both requests emitted within one activation are processed as a single frozen batch",
          "[runtime][persistence][scheduling][headless]") {
    World world;
    auto& registry = world.registry;

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    status.resubmitted   = true;  // isolate this test from the resubmit behavior
    status.pending_burst = 1;

    drive_frame(registry, kStep);

    // Both requests from the burst were emitted inside the same `on tick:`
    // activation, so both are visible in the queue by the time the boundary
    // opens and both get an outcome from this one frame.
    CHECK(status.failed_count == 2);
}

TEST_CASE("both requests in a batch are written before either outcome is delivered",
          "[runtime][persistence][scheduling][headless]") {
    World world;
    auto& registry = world.registry;

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    status.resubmitted   = true;  // isolate this test from the resubmit behavior
    status.pending_burst = 1;

    // A spy adapter: records what ObserveSaveCompleted has counted so far at
    // the moment each request is written. If a request's outcome were
    // delivered (and its handler run) before the OTHER request's write, that
    // write would observe a nonzero count.
    std::vector<int> completed_count_at_write;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(cactus::runtime::entt_backend::PersistenceAdapter{
        .write = [&](const std::string&,
                    const cactus::persistence::SchemaDescriptor&,
                    const cactus::persistence::Snapshot&) {
            completed_count_at_write.push_back(status.completed_count);
            return cactus::runtime::entt_backend::PersistenceWriteResult{.ok = true};
        }});

    drive_frame(registry, kStep);

    REQUIRE(completed_count_at_write.size() == 2);
    CHECK(completed_count_at_write[0] == 0);
    CHECK(completed_count_at_write[1] == 0);
    CHECK(status.completed_count == 2);
}

TEST_CASE("a write that throws produces an io_failure outcome instead of wedging the boundary",
          "[runtime][persistence][scheduling][headless]") {
    World world;
    auto& registry = world.registry;

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    status.resubmitted = true;  // isolate this test from the resubmit behavior

    // A non-compliant adapter: the ABI is a host-supplied std::function, so
    // nothing stops one from throwing even though the shipped example
    // adapter never does.
    cactus_test::ScopedPersistenceAdapter scoped_adapter(cactus::runtime::entt_backend::PersistenceAdapter{
        .write = [](const std::string&,
                    const cactus::persistence::SchemaDescriptor&,
                    const cactus::persistence::Snapshot&) -> cactus::runtime::entt_backend::PersistenceWriteResult {
            throw std::runtime_error("adapter blew up");
        }});

    status.pending_requests = 1;
    drive_frame(registry, kStep);

    CHECK(status.failed_count == 1);
    CHECK(status.last_code == "io_failure");
    CHECK_FALSE(cactus::runtime::entt_backend::generated_persistence_state().processing_batch);

    // The reentrancy guard must not be stuck: a later, independent request
    // through the same (still-throwing) adapter is still processed rather
    // than silently absorbed by a wedged `processing_batch`.
    status.pending_requests = 1;
    drive_frame(registry, kStep);
    CHECK(status.failed_count == 2);
}

TEST_CASE("a request emitted from an outcome handler is deferred to a later boundary",
          "[runtime][persistence][scheduling][headless]") {
    World world;
    auto& registry = world.registry;

    auto& status = registry.get<persistence_save_events__SaveStatus>(
        entity_with<persistence_save_events__SaveStatus>(registry));
    status.pending_requests = 1;

    drive_frame(registry, kStep);
    // ObserveSaveFailed's own resubmitted SaveRequested must NOT be processed
    // within this same frame: only the original request's outcome has
    // arrived so far.
    CHECK(status.failed_count == 1);

    drive_frame(registry, kStep);
    // The resubmitted request's outcome arrives at the next boundary.
    CHECK(status.failed_count == 2);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
