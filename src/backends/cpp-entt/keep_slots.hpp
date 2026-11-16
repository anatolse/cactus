#pragma once

#include "frontend/keep_inputs.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cactus {

// The versions a program keeps so that a keep rule whose inputs did not
// change since its last run is skipped: one slot per trait a keep reads,
// changed by every membership change, and one per `var` field a keep reads,
// changed at every write site.
class KeepSlotPlan {
public:
    struct Keep {
        std::uint32_t index = 0;
        std::vector<std::uint32_t> slots;  // sorted
        std::vector<SymbolId> relations;
        // What it keeps: a change it did not make itself, by host code, makes it run again.
        std::optional<std::uint32_t> output_slot;
        std::optional<SymbolId> output_relation;
        KeepEvaluation evaluation;
    };

    explicit KeepSlotPlan(const DecoratedProgram& program);

    [[nodiscard]] bool empty() const {
        return keeps_.empty();
    }
    [[nodiscard]] const Keep* keep(const HandlerIdentity& handler) const;
    [[nodiscard]] std::optional<std::uint32_t> trait_slot(const SymbolId& trait) const;
    // The slots a write of `field` (every field when unset) of `trait` changes.
    [[nodiscard]] std::vector<std::uint32_t> write_slots(const SymbolId& trait,
                                                         const std::optional<std::string>& field) const;
    // The slots of every tracked field `contract` writes.
    [[nodiscard]] std::vector<std::uint32_t> contract_write_slots(const HandlerContract& contract) const;

    // The program's version storage and the storage signals that change trait slots.
    [[nodiscard]] std::string emit_setup(const DecoratedProgram& program) const;

    // `generated_keep_versions(registry).touch(...)` for each slot, one statement per line.
    // With `entity`, a delta keep that reads a slot re-evaluates only that entity's rows.
    [[nodiscard]] static std::string touches(const std::vector<std::uint32_t>& slots,
                                             const std::string& indent,
                                             const std::string& entity = {});

private:
    void add_slots(const KeepInputs& read, const SymbolId& kept);
    [[nodiscard]] Keep make_keep(KeepInputs& read, const SymbolId& kept) const;

    std::unordered_map<HandlerIdentity, Keep, HandlerIdentityHash> keeps_;
    std::map<std::string, std::pair<SymbolId, std::uint32_t>> traits_;  // by canonical id
    std::map<std::pair<std::string, std::string>, std::uint32_t> fields_;
    std::size_t slot_count_ = 0;
};

// Scoped, not a parameter: statement rewriting reaches write sites with partial context.
class KeepSlotScope {
public:
    explicit KeepSlotScope(const KeepSlotPlan& plan);
    KeepSlotScope(const KeepSlotScope&)            = delete;
    KeepSlotScope& operator=(const KeepSlotScope&) = delete;
    KeepSlotScope(KeepSlotScope&&)                 = delete;
    KeepSlotScope& operator=(KeepSlotScope&&)      = delete;
    ~KeepSlotScope();

    [[nodiscard]] static const KeepSlotPlan* active();

private:
    const KeepSlotPlan* previous_;
};

}  // namespace cactus
