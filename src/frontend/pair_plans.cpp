#include "frontend/pair_plans.hpp"

#include <algorithm>
#include <optional>
#include <set>

namespace cactus {

namespace {

// How many entities can fill `binding` when only named entities can.
std::optional<std::size_t> named_bound(const ReachabilityFacts& facts, const RelationBinding& binding) {
    if (!facts.derived || binding.relation.has_value()) {
        return std::nullopt;
    }
    std::vector<SymbolId> required;
    std::ranges::copy_if(binding.required_traits, std::back_inserter(required), [&](const SymbolId& trait) {
        return !std::ranges::contains(facts.free_traits, trait);
    });
    std::size_t bound = 0;
    for (const auto& origin : facts.origins) {
        const bool holds = std::ranges::any_of(origin.trait_sets, [&](const ReachableTraitSet& traits) {
            return std::ranges::all_of(required, [&](const SymbolId& trait) { return std::ranges::contains(traits, trait); });
        });
        if (!holds) {
            continue;
        }
        if (origin.node.archetype.kind != SymbolKind::Entity) {
            return std::nullopt;
        }
        ++bound;
    }
    return bound;
}

void bound_product(const ReachabilityFacts& facts, HandlerContract& contract) {
    auto& plan = contract.pair_plan;
    if (contract.pair_bindings.size() != 2 || plan.kind != PairPlanKind::Product) {
        return;
    }
    for (std::size_t binding = 0; binding < contract.pair_bindings.size(); ++binding) {
        if (const auto bound = named_bound(facts, contract.pair_bindings[binding])) {
            plan.kind    = PairPlanKind::BoundedProduct;
            plan.bounded = BoundedProductPlan{.binding = binding, .bound = *bound};
            return;
        }
    }
}

}  // namespace

void finalize_pair_plans(DecoratedProgram& program, ErrorReporter& errors) {
    std::set<std::string> warned;
    for (auto& contract : program.handler_contracts) {
        bound_product(program.reachability, contract);
        const auto& plan = contract.pair_plan;
        if (contract.pair_bindings.size() != 2 || plan.kind != PairPlanKind::Product || plan.warned ||
            !warned.insert(make_canonical_id(contract.rule)).second) {
            continue;
        }
        const auto& bindings = contract.pair_bindings;
        errors.warning(plan.location,
                       "pair rule '" + contract.rule.local_name +
                           "' is not accelerated: no recognized overlap predicate in where:, so every (" +
                           bindings[0].name + ", " + bindings[1].name + ") tuple is checked");
    }
    for (auto& handler : program.execution_graph.handlers) {
        bound_product(program.reachability, handler.contract);
    }
}

}  // namespace cactus
