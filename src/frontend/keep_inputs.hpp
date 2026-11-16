#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cactus {

// What a keep rule's rows depend on. A run whose inputs did not change since
// the rule's last run gives the same rows, so the backend may skip it.
struct KeepInputs {
    // Traits whose membership or fields it reads; a change of membership changes every field.
    std::vector<SymbolId> traits;
    // `var` fields it reads; `let` fields change only with membership.
    std::vector<TraitFieldRef> fields;
    // Stored relations whose rows or values it reads, derived relations replaced by their sources.
    std::vector<SymbolId> relations;

    friend bool operator==(const KeepInputs&, const KeepInputs&) = default;
};

using TraitLookup    = std::function<const ResolvedTrait*(const SymbolId&)>;
using RelationLookup = std::function<const ResolvedRelation*(const SymbolId&)>;

// Every list is sorted by canonical id and holds no duplicates.
[[nodiscard]] KeepInputs keep_inputs(const HandlerContract& contract,
                                     const TraitLookup& find_trait,
                                     const RelationLookup& find_relation);

// Orders and identifies a field input: canonical trait id, field name.
[[nodiscard]] std::pair<std::string, std::string> keep_field_key(const TraitFieldRef& ref);

// How a backend may evaluate a keep rule; either level skips a run whose inputs did not change.
// Delta: rows read only their own bindings, and the pair plan can look up from both ends,
// so a run evaluates only the rows of changed entities. Full: any input change evaluates every row.
enum class KeepLevel : std::uint8_t { Delta, Full };

struct KeepEvaluation {
    KeepLevel level = KeepLevel::Delta;
    std::string reason;  // full only: the first construct that forces it
    SourceLocation location;
};

[[nodiscard]] KeepEvaluation classify_keep(const RuleNode& rule,
                                           const HandlerContract& contract,
                                           const RelationLookup& find_relation);

}  // namespace cactus
