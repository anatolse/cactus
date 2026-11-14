// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {

namespace audit = cactus::runtime::entt_backend;

constexpr std::string_view kHeal = "contract_audit.Heal/on std.core.tick";

bool any_failure_names(const std::vector<std::string>& failures, const std::vector<std::string_view>& parts) {
    return std::ranges::any_of(failures, [&](const std::string& failure) {
        return std::ranges::all_of(parts, [&](std::string_view part) { return failure.contains(part); });
    });
}

// Runs one frame against a contract that `edit` changed, then restores it.
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

TEST_CASE("an audited frame of a correct program reports nothing", "[contract-audit]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    CHECK(audit::take_contract_audit_failures().empty());
}

TEST_CASE("a read missing from the contract fails with the handler, field and location", "[contract-audit]") {
    const auto failures =
        failures_with(kHeal, [](audit::ContractAuditContract& contract) { contract.reads.erase("contract_audit.Health.regen"); });
    CHECK(any_failure_names(failures, {kHeal, "reads contract_audit.Health.regen", "contract_audit.cactus:61"}));
}

TEST_CASE("a write of another kind fails and names both kinds", "[contract-audit]") {
    const auto failures = failures_with(
        kHeal, [](audit::ContractAuditContract& contract) { contract.writes["contract_audit.Health.hp"] = "Accum"; });
    CHECK(any_failure_names(failures, {kHeal, "contract_audit.Health.hp", "as Assign", "records Accum"}));
}

TEST_CASE("an emit missing from the contract fails", "[contract-audit]") {
    const auto failures =
        failures_with(kHeal, [](audit::ContractAuditContract& contract) { contract.emits.erase("contract_audit.Healed"); });
    CHECK(any_failure_names(failures, {kHeal, "emits contract_audit.Healed", "contract_audit.cactus:63"}));
}

TEST_CASE("a trait access missing from the contract fails at the storage", "[contract-audit]") {
    const auto failures =
        failures_with(kHeal, [](audit::ContractAuditContract& contract) { contract.traits.erase("contract_audit.Tally"); });
    CHECK(any_failure_names(failures, {kHeal, "accesses contract_audit.Tally"}));
}

TEST_CASE("selection is not a read", "[contract-audit]") {
    const auto& stamp = audit::contract_audit_contract("contract_audit.Stamp/on std.core.tick");
    CHECK_FALSE(audit::contract_audit_covers(stamp.reads, "contract_audit.Health.hp"));
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    CHECK_FALSE(any_failure_names(audit::take_contract_audit_failures(), {"contract_audit.Health"}));
}

TEST_CASE("a native that accesses a trait outside its summary fails", "[contract-audit]") {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<cactus_gen::contract_audit::Health>(entity);
    audit::contract_audit_native("stub.native").traits.clear();
    {
        const audit::ContractAuditHandlerScope handler(kHeal);
        CHECK(audit::audit_native_call("stub.native", "stub.cactus:1:1", [&] {
                  return registry.get<cactus_gen::contract_audit::Health>(entity).hp;
              }) == 10);
    }
    const auto failures = audit::take_contract_audit_failures();
    REQUIRE(failures.size() == 1);
    CHECK(any_failure_names(failures, {kHeal, "native stub.native", "contract_audit.Health", "stub.cactus:1:1"}));
}

TEST_CASE("a native that stays inside its summary passes", "[contract-audit]") {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<cactus_gen::contract_audit::Health>(entity);
    audit::contract_audit_native("stub.listed").traits = {"contract_audit.Health"};
    {
        const audit::ContractAuditHandlerScope handler(kHeal);
        CHECK(audit::audit_native_call("stub.listed", "stub.cactus:2:1", [&] {
                  return registry.get<cactus_gen::contract_audit::Health>(entity).hp;
              }) == 10);
    }
    CHECK(audit::take_contract_audit_failures().empty());
}

TEST_CASE("volume transform propagation runs after a writer of its local transform", "[contract-audit]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    auto view = registry.view<cactus_gen::stdlib::transform::volume::LocalTransform,
                              cactus_gen::stdlib::transform::volume::WorldTransform>();
    REQUIRE(view.size_hint() == 1);
    for (const auto entity : view) {
        CHECK(registry.get<cactus_gen::stdlib::transform::volume::WorldTransform>(entity).position.x == 1.0F);
    }
    CHECK(audit::take_contract_audit_failures().empty());
}

TEST_CASE("an index may read the let bounds of a ranged field the handler reads", "[contract-audit]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    auto view = registry.view<cactus_gen::contract_audit::Cursor>();
    REQUIRE(view.size() == 1);
    CHECK(registry.get<cactus_gen::contract_audit::Cursor>(*view.begin()).hits == 1);
    CHECK_FALSE(any_failure_names(audit::take_contract_audit_failures(), {"contract_audit.Grid"}));
}

// Hidden: ctest runs it alone and expects the process to fail at exit.
TEST_CASE("an untaken audit failure fails the process", "[.contract-audit-untaken]") {
    audit::set_contract_audit_natives_file({});
    const audit::ContractAuditHandlerScope handler(kHeal);
    audit::audit_emit("contract_audit.Unlisted", "untaken.cactus:1:1");
    CHECK(true);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
