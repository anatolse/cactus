#include "frontend/state_transitions.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cactus {

namespace {

struct VariantRef {
    const ResolvedState* state = nullptr;
    const ResolvedStateVariant* variant = nullptr;
};

// The program's states, sorted by canonical id so diagnostics come out in a stable order.
std::vector<const ResolvedState*> sorted_states(const DecoratedProgram& program) {
    std::vector<const ResolvedState*> states;
    for (const auto& state : program.states | std::views::values) {
        if (state.symbol_id.has_value()) {
            states.push_back(&state);
        }
    }
    std::ranges::sort(states, {}, [](const ResolvedState* state) { return make_canonical_id(*state->symbol_id); });
    return states;
}

std::unordered_map<SymbolId, VariantRef> index_variants(const std::vector<const ResolvedState*>& states) {
    std::unordered_map<SymbolId, VariantRef> variants;
    for (const auto* state : states) {
        for (const auto& variant : state->variants) {
            variants.emplace(state_variant_trait(*state->symbol_id, variant.name),
                             VariantRef{.state = state, .variant = &variant});
        }
    }
    return variants;
}

const StateExit* find_arrow(const ResolvedState& state, const std::string& from, const std::string& to) {
    const auto variant = state.variant_index(from);
    if (!variant.has_value()) {
        return nullptr;
    }
    const auto& exits = state.variants[*variant].exits;
    const auto exit   = std::ranges::find(exits, to, &StateExit::variant);
    return exit == exits.end() ? nullptr : &*exit;
}

// One error per `add` site, naming every source there that has no arrow to the target.
void report_missing_arrows(const VariantRef& target, const InferredHandlerCommand& command, ErrorReporter& errors) {
    std::vector<std::pair<SourceLocation, std::vector<std::string>>> sites;
    for (const auto& source : command.sources) {
        if (find_arrow(*target.state, source.variant, target.variant->name) != nullptr) {
            continue;
        }
        auto site = std::ranges::find(sites, source.location, &std::pair<SourceLocation, std::vector<std::string>>::first);
        if (site == sites.end()) {
            site = sites.insert(sites.end(), {source.location, {}});
        }
        site->second.push_back(source.variant);
    }
    const auto spelling = target.state->name + "." + target.variant->name;
    for (const auto& [location, missing] : sites) {
        const bool one = missing.size() == 1;
        errors.error(location,
                     "`add " + spelling + "` can move an entity out of " +
                         spelled_list(missing, [](const std::string& name) { return "'" + name + "'"; }) +
                         (one ? ", which has" : ", which have") + " no arrow to '" + target.variant->name +
                         "'; narrow the source with `each:` or a state `match` arm, or declare the arrow" +
                         (one ? "" : "s"));
    }
}

void report_unused_arrows(const std::vector<const ResolvedState*>& user_states,
                          const std::unordered_set<const StateExit*>& taken,
                          ErrorReporter& errors) {
    for (const auto* state : user_states) {
        for (const auto& variant : state->variants) {
            for (const auto& exit : variant.exits) {
                if (taken.contains(&exit)) {
                    continue;
                }
                errors.error(exit.location,
                             "arrow '" + variant.name + " -> " + exit.variant + "' of state '" + state->name +
                                 "' is never taken: no reachable `add " + state->name + "." + exit.variant +
                                 "` can leave '" + variant.name + "'");
            }
        }
    }
}

void report_unentered_variants(const DecoratedProgram& program,
                               const std::vector<const ResolvedState*>& user_states,
                               ErrorReporter& errors) {
    const auto& facts = program.reachability;
    if (!facts.derived) {
        return;
    }
    std::unordered_set<SymbolId> entered(facts.free_traits.begin(), facts.free_traits.end());
    for (const auto& trait_set : facts.trait_sets) {
        entered.insert(trait_set.begin(), trait_set.end());
    }
    for (const auto* state : user_states) {
        for (const auto& variant : state->variants) {
            if (entered.contains(state_variant_trait(*state->symbol_id, variant.name))) {
                continue;
            }
            errors.error(variant.location,
                         "variant '" + variant.name + "' of state '" + state->name +
                             "' is never entered: no entity starts in it and no reachable `add` enters it");
        }
    }
}

}  // namespace

void validate_state_transitions(const DecoratedProgram& program, ErrorReporter& errors) {
    const auto states = sorted_states(program);
    if (states.empty()) {
        return;
    }
    const auto variants     = index_variants(states);
    const auto& unreachable = program.reachability.unreachable_handlers;
    std::unordered_set<const StateExit*> taken;
    for (const auto& handler : program.execution_graph.handlers) {
        if (std::ranges::contains(unreachable, handler.identity)) {
            continue;
        }
        for (const auto& command : handler.contract.commands) {
            const auto target = command.kind == HandlerCommandKind::Add && command.target.has_value()
                                    ? variants.find(*command.target)
                                    : variants.end();
            if (target == variants.end()) {
                continue;
            }
            for (const auto& source : command.sources) {
                taken.insert(find_arrow(*target->second.state, source.variant, target->second.variant->name));
            }
            if (target->second.state->has_arrows()) {
                report_missing_arrows(target->second, command, errors);
            }
        }
    }
    std::vector<const ResolvedState*> user_states;
    std::ranges::copy_if(states, std::back_inserter(user_states), [](const ResolvedState* state) {
        return state->symbol_id.has_value() && !module_name_is_stdlib(state->symbol_id->module.name);
    });
    report_unused_arrows(user_states, taken, errors);
    report_unentered_variants(program, user_states, errors);
}

}  // namespace cactus
