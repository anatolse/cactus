#pragma once

// Test-only: checks every access a handler makes against its contract.
// Generated code includes it only when compiled with --contract-audit.

#include <entt/entity/fwd.hpp>
#include <entt/entity/mixin.hpp>
#include <entt/entity/storage.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

// One handler's contract. Fields are "module.Trait.field"; "module.Trait.*"
// stands for every field. Write kinds use the compiler's names ("Assign",
// "Accum", "Join max", ..., "General" covers every kind).
struct ContractAuditContract {
    std::set<std::string, std::less<>> reads;
    std::map<std::string, std::string, std::less<>> writes;
    std::set<std::string, std::less<>> traits;  // what the storage hook allows outside a native call
    std::set<std::string, std::less<>> emits;
    std::set<std::string, std::less<>> commands;  // "Add module.Trait", "Spawn module.Template", "Destroy"
    std::set<std::string, std::less<>> effects;   // "observe domain" or "act domain"
};

// The traits a summarized native may touch.
struct ContractAuditNative {
    std::set<std::string, std::less<>> traits;
};

struct ContractAuditState {
    std::mutex mutex;
    std::map<std::string, ContractAuditContract, std::less<>> contracts;
    std::map<std::string, ContractAuditNative, std::less<>> natives;
    std::vector<std::string> failures;
    std::set<std::string, std::less<>> called_natives;
    std::string natives_file;
    // Traits whose fields are all `let`: they change only at commit, so every access may read them.
    std::set<std::string, std::less<>> let_traits;

    ContractAuditState()                                     = default;
    ContractAuditState(const ContractAuditState&)            = delete;
    ContractAuditState& operator=(const ContractAuditState&) = delete;
    ContractAuditState(ContractAuditState&&)                 = delete;
    ContractAuditState& operator=(ContractAuditState&&)      = delete;

    // A run that leaves a failure untaken fails the process, so ctest sees it.
    ~ContractAuditState() {
        try {
            report();
        } catch (...) {
            std::_Exit(1);
        }
    }

private:
    void report() const {
        if (!natives_file.empty()) {
            std::ofstream out(natives_file);
            for (const auto& native : called_natives) {
                out << native << '\n';
            }
        }
        if (failures.empty()) {
            return;
        }
        for (const auto& failure : failures) {
            std::println(stderr, "contract audit: {}", failure);
        }
        std::fflush(stderr);
        std::_Exit(1);
    }
};

inline ContractAuditState& contract_audit_state() {
    static ContractAuditState state;
    return state;
}

struct ContractAuditFrame {
    std::string_view handler;
    const ContractAuditContract* contract = nullptr;
    std::string_view native;
    const ContractAuditNative* native_summary = nullptr;
    std::string_view native_location;
};

inline ContractAuditFrame& contract_audit_frame() {
    thread_local ContractAuditFrame frame;
    return frame;
}

template <typename Value>
Value& contract_audit_entry(std::map<std::string, Value, std::less<>>& entries, std::string_view key) {
    const auto found = entries.find(key);
    return found != entries.end() ? found->second : entries.emplace(std::string{key}, Value{}).first->second;
}

inline ContractAuditContract& contract_audit_contract(std::string_view handler) {
    return contract_audit_entry(contract_audit_state().contracts, handler);
}

inline ContractAuditNative& contract_audit_native(std::string_view native) {
    return contract_audit_entry(contract_audit_state().natives, native);
}

// Where the natives this process calls are written at exit.
inline void set_contract_audit_natives_file(std::string path) {
    contract_audit_state().natives_file = std::move(path);
}

inline std::vector<std::string> take_contract_audit_failures() {
    auto& state = contract_audit_state();
    const std::scoped_lock lock(state.mutex);
    return std::exchange(state.failures, {});
}

// Repeated rows report one failure once.
inline void contract_audit_fail(std::string failure) {
    auto& state = contract_audit_state();
    const std::scoped_lock lock(state.mutex);
    if (std::ranges::find(state.failures, failure) == state.failures.end()) {
        state.failures.push_back(std::move(failure));
    }
}

// "module.Trait" of "module.Trait.field".
inline std::string_view contract_audit_trait_of(std::string_view field) {
    return field.substr(0, field.rfind('.'));
}

// The entry for `field`, else the one for every field of its trait.
template <typename Entries>
auto contract_audit_find(const Entries& entries, std::string_view field) {
    const auto found = entries.find(field);
    return found != entries.end() ? found : entries.find(std::string{contract_audit_trait_of(field)} + ".*");
}

inline bool contract_audit_covers(const std::set<std::string, std::less<>>& fields, std::string_view field) {
    return contract_audit_find(fields, field) != fields.end();
}

// Puts `frame` in place for one scope and restores the one before.
class ContractAuditFrameScope {
public:
    explicit ContractAuditFrameScope(const ContractAuditFrame& frame)
        : saved_(std::exchange(contract_audit_frame(), frame)) {}
    ContractAuditFrameScope(const ContractAuditFrameScope&)            = delete;
    ContractAuditFrameScope& operator=(const ContractAuditFrameScope&) = delete;
    ContractAuditFrameScope(ContractAuditFrameScope&&)                 = delete;
    ContractAuditFrameScope& operator=(ContractAuditFrameScope&&)      = delete;
    ~ContractAuditFrameScope() {
        contract_audit_frame() = saved_;
    }

private:
    ContractAuditFrame saved_;
};

class ContractAuditHandlerScope {
public:
    explicit ContractAuditHandlerScope(std::string_view handler)
        : scope_(ContractAuditFrame{.handler = handler, .contract = &contract_audit_contract(handler)}) {}

private:
    ContractAuditFrameScope scope_;
};

inline void audit_failure(std::string_view what, std::string_view location) {
    const auto& frame = contract_audit_frame();
    contract_audit_fail("handler " + std::string{frame.handler} + " " + std::string{what} + " at " +
                        std::string{location});
}

inline void audit_read(std::string_view field, std::string_view location) {
    const auto& frame = contract_audit_frame();
    if (frame.contract == nullptr || contract_audit_covers(frame.contract->reads, field)) {
        return;
    }
    audit_failure("reads " + std::string{field} + " outside its contract", location);
}

inline void audit_write(std::string_view field, std::string_view kind, std::string_view location) {
    const auto& frame = contract_audit_frame();
    if (frame.contract == nullptr) {
        return;
    }
    const auto& writes = frame.contract->writes;
    const auto found   = contract_audit_find(writes, field);
    if (found == writes.end()) {
        audit_failure("writes " + std::string{field} + " (" + std::string{kind} + ") outside its contract", location);
        return;
    }
    if (found->second != "General" && found->second != kind) {
        audit_failure("writes " + std::string{field} + " as " + std::string{kind} + " but its contract records " +
                          found->second,
                      location);
    }
}

inline void audit_listed(const std::set<std::string, std::less<>> ContractAuditContract::* list,
                         std::string_view what,
                         std::string_view entry,
                         std::string_view location) {
    const auto& frame = contract_audit_frame();
    if (frame.contract == nullptr || (frame.contract->*list).contains(entry)) {
        return;
    }
    audit_failure(std::string{what} + " " + std::string{entry} + " outside its contract", location);
}

inline void audit_emit(std::string_view event, std::string_view location) {
    audit_listed(&ContractAuditContract::emits, "emits", event, location);
}

inline void audit_command(std::string_view command, std::string_view location) {
    audit_listed(&ContractAuditContract::commands, "issues", command, location);
}

inline void audit_effect(std::string_view effect, std::string_view location) {
    audit_listed(&ContractAuditContract::effects, "uses effect", effect, location);
}

// Runs one native call with the storage hook checking its summary.
template <typename Call>
decltype(auto) audit_native_call(std::string_view native, std::string_view location, Call&& call) {
    {
        auto& state = contract_audit_state();
        const std::scoped_lock lock(state.mutex);
        if (!state.called_natives.contains(native)) {
            state.called_natives.emplace(native);
        }
    }
    auto frame            = contract_audit_frame();
    frame.native          = native;
    frame.native_summary  = &contract_audit_native(native);
    frame.native_location = location;
    const ContractAuditFrameScope scope(frame);
    return std::forward<Call>(call)();
}

// A program trait's canonical id; empty for every type the audit ignores.
template <typename Component>
inline constexpr std::string_view contract_audit_trait_name{};

template <typename Component>
inline constexpr bool kContractAudited = !contract_audit_trait_name<Component>.empty();

inline void audit_component_access(std::string_view trait) {
    const auto& frame = contract_audit_frame();
    if (contract_audit_state().let_traits.contains(trait)) {
        return;
    }
    if (frame.native_summary != nullptr && !frame.native_summary->traits.contains(trait)) {
        audit_failure("calls native " + std::string{frame.native} + ", which accesses " + std::string{trait} +
                          " outside its summary",
                      frame.native_location);
        return;
    }
    if (frame.native_summary == nullptr && frame.contract != nullptr && !frame.contract->traits.contains(trait)) {
        audit_failure("accesses " + std::string{trait} + " outside its contract", "storage");
    }
}

// Every value access to an audited component goes through these names.
template <typename Component>
class ContractAuditedStorage : public entt::basic_storage<Component> {
    using base = entt::basic_storage<Component>;

public:
    using base::base;

    [[nodiscard]] const Component& get(const entt::entity entity) const noexcept {
        audit_component_access(contract_audit_trait_name<Component>);
        return base::get(entity);
    }
    [[nodiscard]] Component& get(const entt::entity entity) noexcept {
        audit_component_access(contract_audit_trait_name<Component>);
        return base::get(entity);
    }
    [[nodiscard]] std::tuple<const Component&> get_as_tuple(const entt::entity entity) const noexcept {
        audit_component_access(contract_audit_trait_name<Component>);
        return base::get_as_tuple(entity);
    }
    [[nodiscard]] std::tuple<Component&> get_as_tuple(const entt::entity entity) noexcept {
        audit_component_access(contract_audit_trait_name<Component>);
        return base::get_as_tuple(entity);
    }
    [[nodiscard]] auto each() noexcept {
        audit_component_access(contract_audit_trait_name<Component>);
        return base::each();
    }
    [[nodiscard]] auto each() const noexcept {
        audit_component_access(contract_audit_trait_name<Component>);
        return base::each();
    }
};

}  // namespace cactus::runtime::entt_backend

template <typename Component>
struct entt::storage_type<Component,
                          entt::entity,
                          std::allocator<Component>,
                          std::enable_if_t<cactus::runtime::entt_backend::kContractAudited<Component>>> {
    using type = entt::sigh_mixin<cactus::runtime::entt_backend::ContractAuditedStorage<Component>>;
};
