// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cactus::runtime::entt_backend;

namespace {

// generated_persistence_state() is a function-local static: reset it between
// test cases so one test's queued requests or registered handler cannot leak
// into the next.
struct ResetPersistenceState {
    ResetPersistenceState() { generated_persistence_state() = PersistenceRuntimeState{}; }
    ~ResetPersistenceState() { generated_persistence_state() = PersistenceRuntimeState{}; }
    ResetPersistenceState(const ResetPersistenceState&)            = delete;
    ResetPersistenceState& operator=(const ResetPersistenceState&) = delete;
    ResetPersistenceState(ResetPersistenceState&&)                 = delete;
    ResetPersistenceState& operator=(ResetPersistenceState&&)      = delete;
};

}  // namespace

TEST_CASE("queued requests accumulate in emission order", "[runtime][persistence][scheduling]") {
    ResetPersistenceState reset;

    generated_queue_save_request("slot1", 1);
    generated_queue_save_request("slot2", 2);

    const auto batch = generated_freeze_persistence_request_batch();
    REQUIRE(batch.size() == 2);
    CHECK(batch[0] == PendingPersistenceRequest{
                          .kind = PersistenceRequestKind::Save, .slot = "slot1", .request_id = 1});
    CHECK(batch[1] == PendingPersistenceRequest{
                          .kind = PersistenceRequestKind::Save, .slot = "slot2", .request_id = 2});
}

TEST_CASE("a save and a restore queued together keep their relative emission order",
          "[runtime][persistence][scheduling]") {
    ResetPersistenceState reset;

    generated_queue_save_request("slot1", 1);
    generated_queue_restore_request("slot2", 2);
    generated_queue_save_request("slot3", 3);

    const auto batch = generated_freeze_persistence_request_batch();
    REQUIRE(batch.size() == 3);
    CHECK(batch[0] == PendingPersistenceRequest{
                          .kind = PersistenceRequestKind::Save, .slot = "slot1", .request_id = 1});
    CHECK(batch[1] == PendingPersistenceRequest{
                          .kind = PersistenceRequestKind::Restore, .slot = "slot2", .request_id = 2});
    CHECK(batch[2] == PendingPersistenceRequest{
                          .kind = PersistenceRequestKind::Save, .slot = "slot3", .request_id = 3});
}

TEST_CASE("repeated request IDs remain distinct queued entries", "[runtime][persistence][scheduling]") {
    ResetPersistenceState reset;

    generated_queue_save_request("a", 1);
    generated_queue_save_request("b", 1);

    const auto batch = generated_freeze_persistence_request_batch();
    REQUIRE(batch.size() == 2);
    CHECK(batch[0].slot == "a");
    CHECK(batch[1].slot == "b");
}

TEST_CASE("freezing a batch clears the queue for the next boundary", "[runtime][persistence][scheduling]") {
    ResetPersistenceState reset;

    generated_queue_save_request("slot1", 1);
    const auto first = generated_freeze_persistence_request_batch();
    REQUIRE(first.size() == 1);

    const auto second = generated_freeze_persistence_request_batch();
    CHECK(second.empty());
}

TEST_CASE("executing a request with no registered adapter fails as adapter_unavailable",
          "[runtime][persistence][scheduling]") {
    ResetPersistenceState reset;

    const auto outcome =
        generated_execute_save_request("slot1", 42, cactus::persistence::SchemaDescriptor{}, cactus::persistence::Snapshot{});
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.slot == "slot1");
    CHECK(outcome.request_id == 42);
    CHECK(outcome.code == "adapter_unavailable");
}

TEST_CASE("executing a request with a registered adapter delegates to its write function",
          "[runtime][persistence][scheduling]") {
    ResetPersistenceState reset;

    register_persistence_adapter(PersistenceAdapter{
        .write = [](const std::string&, const cactus::persistence::SchemaDescriptor&,
                   const cactus::persistence::Snapshot&) { return PersistenceWriteResult{.ok = true}; }});

    const auto outcome =
        generated_execute_save_request("slot1", 7, cactus::persistence::SchemaDescriptor{}, cactus::persistence::Snapshot{});
    CHECK(outcome.ok);
    CHECK(outcome.slot == "slot1");
    CHECK(outcome.request_id == 7);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
