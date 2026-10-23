// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

using namespace cactus::runtime::entt_backend;

namespace {

RangeFault out_of_range(int value) {
    return RangeFault{
        .trait = "Cell", .field = "x", .value = value, .lo = 0, .hi = 16, .reason = RangeFaultReason::OutOfRange};
}

}  // namespace

TEST_CASE("Range faults: a reported fault keeps its trait, field, value and range", "[runtime][ranged-fields]") {
    clear_runtime_faults();
    report_range_fault(out_of_range(16));
    report_range_fault(RangeFault{
        .trait = "Cell", .field = "x", .value = 1, .lo = 0, .hi = 0, .reason = RangeFaultReason::StaleContainer});
    const auto& faults = runtime_faults();
    REQUIRE(faults.size() == 2);
    CHECK(faults[0].trait == "Cell");
    CHECK(faults[0].field == "x");
    CHECK(faults[0].value == 16);
    CHECK(faults[0].lo == 0);
    CHECK(faults[0].hi == 16);
    CHECK(faults[0].reason == RangeFaultReason::OutOfRange);
    CHECK(faults[1].reason == RangeFaultReason::StaleContainer);
    CHECK(runtime_fault_count() == 2);
}

TEST_CASE("Range faults: the log keeps the latest faults up to its capacity", "[runtime][ranged-fields]") {
    clear_runtime_faults();
    const auto extra = std::size_t{5};
    for (std::size_t index = 0; index < RUNTIME_FAULT_LOG_CAPACITY + extra; ++index) {
        report_range_fault(out_of_range(static_cast<int>(index)));
    }
    const auto& faults = runtime_faults();
    REQUIRE(faults.size() == RUNTIME_FAULT_LOG_CAPACITY);
    CHECK(faults.front().value == static_cast<int>(extra));
    CHECK(faults.back().value == static_cast<int>(RUNTIME_FAULT_LOG_CAPACITY + extra - 1));
    CHECK(runtime_fault_count() == RUNTIME_FAULT_LOG_CAPACITY + extra);
}

TEST_CASE("Range faults: a passed check reports nothing", "[runtime][ranged-fields]") {
    clear_runtime_faults();
    CHECK(passes_range_check(std::nullopt));
    CHECK(runtime_faults().empty());
    CHECK_FALSE(passes_range_check(out_of_range(16)));
    CHECK(runtime_faults().size() == 1);
}

TEST_CASE("Range faults: clearing empties the log and resets the count", "[runtime][ranged-fields]") {
    report_range_fault(out_of_range(16));
    clear_runtime_faults();
    CHECK(runtime_faults().empty());
    CHECK(runtime_fault_count() == 0);
}

TEST_CASE("Range faults: a fault describes itself in one line", "[runtime][ranged-fields]") {
    CHECK(describe_range_fault(out_of_range(16)) == "Cell.x = 16 is outside range(0, 16)");
    CHECK(
        describe_range_fault(RangeFault{
            .trait = "Cell", .field = "x", .value = 1, .lo = 0, .hi = 0, .reason = RangeFaultReason::StaleContainer}) ==
        "Cell.x = 1 reads a container that no longer exists");
    CHECK(describe_range_fault(RangeFault{.trait  = "Cell",
                                          .field  = "x",
                                          .value  = 1,
                                          .lo     = 0,
                                          .hi     = 0,
                                          .reason = RangeFaultReason::MissingContainerTrait}) ==
          "Cell.x = 1 reads a container entity that lacks its container trait");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
