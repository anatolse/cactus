// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace audit = cactus::runtime::entt_backend;

bool any_failure_names(const std::vector<std::string>& failures, const std::vector<std::string_view>& parts) {
    return std::ranges::any_of(failures, [&](const std::string& failure) {
        return std::ranges::all_of(parts, [&](std::string_view part) { return failure.contains(part); });
    });
}

template <typename Edit>
std::vector<std::string> failures_with(std::string_view handler, const Edit& edit) {
    auto& contract     = audit::contract_audit_contract(handler);
    const auto correct = contract;
    edit(contract);
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    contract = correct;
    return audit::take_contract_audit_failures();
}

}  // namespace

TEST_CASE("audited cell reads and writes through an alias, self and the short form match their contracts",
          "[contract-audit][trait-dimensions]") {
    entt::registry registry;
    for (int frame = 0; frame < 3; ++frame) {
        cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    }
    CHECK(audit::take_contract_audit_failures().empty());
}

TEST_CASE("a cell write outside the contract fails and names the handler and field", "[contract-audit][trait-dimensions]") {
    constexpr std::string_view kBlow = "trait_dimensions.Blow/on std.core.tick";
    const auto failures              = failures_with(
        kBlow, [](audit::ContractAuditContract& contract) { contract.writes.erase("trait_dimensions.Room.gas"); });
    CHECK(any_failure_names(failures, {kBlow, "trait_dimensions.Room.gas"}));
}

TEST_CASE("a cell read outside the contract fails", "[contract-audit][trait-dimensions]") {
    constexpr std::string_view kPick = "trait_dimensions.Pick/on std.core.tick";
    const auto failures              = failures_with(kPick, [](audit::ContractAuditContract& contract) {
        contract.writes.erase("trait_dimensions.Stock.count");
        contract.reads.erase("trait_dimensions.Stock.count");
    });
    CHECK(any_failure_names(failures, {kPick, "trait_dimensions.Stock.count"}));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
