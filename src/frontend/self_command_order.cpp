#include "frontend/self_command_order.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace cactus {

namespace {

// A state variant change is seen through its whole state.
std::string change_key(const SymbolId& symbol) {
    return make_canonical_id(state_of_variant_trait(symbol).value_or(symbol));
}

// Lifecycle deliveries follow command order, a second `single` carrier faults in
// command order, and Parent keeps children in the order they were attached.
class CommandOrderObservers {
public:
    explicit CommandOrderObservers(const DecoratedProgram& program) : visible_{make_canonical_id("std.core", "Parent")} {
        for (const auto& handler : program.execution_graph.handlers) {
            if (handler.identity.trigger.is_lifecycle()) {
                watched_.insert(change_key(handler.identity.trigger.symbol));
            }
        }
        for (const auto& [name, trait] : program.traits) {
            if (trait.is_single && trait.symbol_id.has_value()) {
                visible_.insert(make_canonical_id(*trait.symbol_id));
            }
        }
    }

    [[nodiscard]] bool sees_order_of(const InferredHandlerCommand& command) const {
        const auto& target = *command.target;
        if (state_of_variant_trait(target).has_value()) {
            return watched_.contains(change_key(target));
        }
        const auto canonical = make_canonical_id(target);
        return visible_.contains(canonical) || watched_.contains(canonical);
    }

private:
    std::unordered_set<std::string> watched_;
    std::unordered_set<std::string> visible_;
};

}  // namespace

void resolve_self_command_order(DecoratedProgram& program) {
    const CommandOrderObservers observers(program);
    for (auto& handler : program.execution_graph.handlers) {
        auto& order = handler.contract.iteration_order;
        if (order != IterationOrder::SelfCommands) {
            continue;
        }
        const bool seen = std::ranges::any_of(handler.contract.commands, [&](const InferredHandlerCommand& command) {
            return observers.sees_order_of(command);
        });
        order = seen ? IterationOrder::Creation : IterationOrder::Free;
    }
}

}  // namespace cactus
