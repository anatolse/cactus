#include "frontend/query_costs.hpp"

#include "frontend/archetype_reachability.hpp"

#include <algorithm>
#include <string>

namespace cactus {

namespace {

// An event's rows depend on its delivery, which a handler can't see; a phase runs them all.
bool runs_every_row(const ResolvedHandlerTrigger& trigger) {
    return trigger.is_phase_or_render_stage();
}

bool domain_bounded(const ReachabilityFacts& facts, const HandlerContract& contract) {
    if (contract.domain_kind == HandlerDomainKind::Unary) {
        return named_bound(facts, contract.selection).has_value();
    }
    return std::ranges::all_of(contract.pair_bindings, [&](const RelationBinding& binding) {
        return named_bound(facts, binding).has_value();
    });
}

bool scan_bounded(const ReachabilityFacts& facts, const WorldScan& scan) {
    return named_bound(facts, scan.filter).has_value();
}

bool in_unbounded_loop(const ReachabilityFacts& facts, const HandlerContract& contract, const WorldScan& scan) {
    for (auto loop = scan.enclosing; loop.has_value(); loop = contract.world_scans[*loop].enclosing) {
        if (!scan_bounded(facts, contract.world_scans[*loop])) {
            return true;
        }
    }
    return false;
}

bool unreachable(const ReachabilityFacts& facts, const InferredHandlerContract& contract) {
    return std::ranges::contains(facts.unreachable_handlers,
                                 HandlerIdentity{.rule = contract.rule, .trigger = contract.trigger});
}

}  // namespace

void check_query_costs(const DecoratedProgram& program, ErrorReporter& errors) {
    const auto& facts = program.reachability;
    for (const auto& contract : program.handler_contracts) {
        if ((contract.world_scans.empty() && contract.key_lookups.empty()) || unreachable(facts, contract)) {
            continue;
        }
        const bool bounded = domain_bounded(facts, contract);
        const bool per_row = runs_every_row(contract.trigger) && !bounded;
        const auto& rule   = contract.rule.local_name;
        for (const auto& scan : contract.world_scans) {
            const bool nested = in_unbounded_loop(facts, contract, scan);
            if (scan_bounded(facts, scan) || (!per_row && !nested)) {
                continue;
            }
            errors.warning(scan.location,
                           "rule '" + rule + "' is superlinear: " + make_canonical_id(scan.callee) + " scans every match " +
                               (nested ? "inside a loop over another scan" : "once per row of the pass"));
        }
        if (bounded) {
            continue;
        }
        for (const auto& lookup : contract.key_lookups) {
            errors.warning(lookup.location,
                           "rule '" + rule + "' is not accelerated: where: scans every row to find one value of '" +
                               lookup.key + "'");
        }
    }
}

}  // namespace cactus
