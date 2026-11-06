#include "frontend/pair_plans.hpp"

#include "frontend/archetype_reachability.hpp"

#include <algorithm>
#include <optional>
#include <set>

namespace cactus {

namespace {

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
