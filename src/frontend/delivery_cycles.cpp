#include "frontend/delivery_cycles.hpp"

#include "frontend/archetype_reachability.hpp"
#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>

namespace cactus {

namespace {

using NodeIndex = std::uint32_t;

constexpr NodeIndex kUnvisited = std::numeric_limits<NodeIndex>::max();

// Whether `slot` is the slot trait of `state`; works for the state symbol and a variant's state.
bool is_slot_of(const SymbolId& slot, const SymbolId& state) {
    return slot.module == state.module && slot.local_name == state.local_name;
}

bool same_state(const SymbolId& left, const SymbolId& right) {
    const auto left_state  = state_of_variant_trait(left);
    const auto right_state = state_of_variant_trait(right);
    return left_state.has_value() && right_state.has_value() && *left_state == *right_state;
}

class LifecycleProducers {
public:
    explicit LifecycleProducers(const TemplateTraitLookup& templates) : templates_(&templates) {}

    [[nodiscard]] bool adds(const HandlerContract& contract, const SymbolId& trait) const {
        const auto state = state_of_variant_trait(trait);
        const bool by_command = std::ranges::any_of(contract.commands, [&](const InferredHandlerCommand& command) {
            switch (command.kind) {
                case HandlerCommandKind::Add:
                    return command.target.has_value() && (*command.target == trait || slot_entered(*command.target, trait));
                case HandlerCommandKind::Spawn:
                    return spawn_adds(command.target, trait, state);
                case HandlerCommandKind::Destroy:
                case HandlerCommandKind::Remove:
                case HandlerCommandKind::Set:
                    return false;
            }
            std::unreachable();
        });
        return by_command || std::ranges::any_of(contract.native_spawns, [&](const SymbolId& function) {
                   return native_adds(function, trait);
               });
    }

    [[nodiscard]] static bool removes(const HandlerContract& contract, const SymbolId& trait) {
        const auto state = state_of_variant_trait(trait);
        return std::ranges::any_of(contract.commands, [&](const InferredHandlerCommand& command) {
            if (!command.target.has_value()) {
                return false;
            }
            const auto& target = *command.target;
            if (command.kind == HandlerCommandKind::Remove) {
                return target == trait || (state.has_value() && is_slot_of(target, *state));
            }
            // Entering one variant leaves every other variant of its state.
            return command.kind == HandlerCommandKind::Add && target != trait && same_state(target, trait);
        });
    }

private:
    // Adding variant S.V also adds the slot trait S.
    static bool slot_entered(const SymbolId& added, const SymbolId& trait) {
        const auto state = state_of_variant_trait(added);
        return state.has_value() && !state_of_variant_trait(trait).has_value() && is_slot_of(trait, *state);
    }

    [[nodiscard]] bool spawn_adds(const std::optional<SymbolId>& archetype,
                                  const SymbolId& trait,
                                  const std::optional<SymbolId>& state) const {
        const auto found = archetype.has_value() ? templates_->find(*archetype) : templates_->end();
        if (found == templates_->end()) {
            return true;
        }
        // A spawn may place its entity in any variant of a state the template carries.
        return std::ranges::any_of(found->second, [&](const SymbolId& carried) {
            return carried == trait || slot_entered(carried, trait) || (state.has_value() && same_state(carried, trait));
        });
    }

    static bool native_adds(const SymbolId& function, const SymbolId& trait) {
        const auto* creation = native_creation(make_canonical_id(function));
        if (creation == nullptr || creation->pub_templates) {
            return true;
        }
        const auto canonical = make_canonical_id(trait);
        return std::ranges::any_of(creation->trait_sets,
                                   [&](const std::vector<std::string>& set) { return std::ranges::contains(set, canonical); });
    }

    const TemplateTraitLookup* templates_;
};

void build_lifecycle_flows(ExecutionGraph& graph, const TemplateTraitLookup& templates) {
    const LifecycleProducers producers(templates);
    graph.lifecycle_flows.clear();
    for (const auto& producer : graph.handlers) {
        for (const auto& consumer : graph.handlers) {
            const auto& trigger = consumer.identity.trigger;
            if (!trigger.is_lifecycle()) {
                continue;
            }
            const bool added  = trigger.kind == HandlerTriggerKind::TraitAdded;
            const bool reaches = added ? producers.adds(producer.contract, trigger.symbol)
                                       : LifecycleProducers::removes(producer.contract, trigger.symbol);
            if (reaches) {
                graph.lifecycle_flows.push_back(LifecycleFlowEdge{
                    .producer = producer.identity, .trait = trigger.symbol, .added = added, .consumer = consumer.identity});
            }
        }
    }
}

// Handlers indexed in canonical-id order, so every derived numbering is
// independent of declaration order.
class DeliveryGraph {
public:
    explicit DeliveryGraph(const ExecutionGraph& graph) {
        for (const auto& handler : graph.handlers) {
            nodes_.push_back(handler.identity);
        }
        std::ranges::sort(nodes_, {}, &HandlerIdentity::canonical_id);
        for (NodeIndex index = 0; index < nodes_.size(); ++index) {
            index_.emplace(nodes_[index], index);
        }
        all_.resize(nodes_.size());
        immediate_.resize(nodes_.size());
        delayed_.resize(nodes_.size());
        for (const auto& edge : graph.event_flows) {
            add(edge.producer, edge.consumer, edge.delayed);
        }
        for (const auto& edge : graph.lifecycle_flows) {
            add(edge.producer, edge.consumer, edge.delayed);
        }
        for (auto* adjacency : {&all_, &immediate_, &delayed_}) {
            for (auto& targets : *adjacency) {
                std::ranges::sort(targets);
                const auto [first, last] = std::ranges::unique(targets);
                targets.erase(first, last);
            }
        }
    }

    [[nodiscard]] std::size_t size() const {
        return nodes_.size();
    }
    [[nodiscard]] NodeIndex index_of(const HandlerIdentity& identity) const {
        return index_.at(identity);
    }
    [[nodiscard]] const std::vector<std::vector<NodeIndex>>& all() const {
        return all_;
    }
    [[nodiscard]] const std::vector<std::vector<NodeIndex>>& immediate() const {
        return immediate_;
    }
    [[nodiscard]] const std::vector<std::vector<NodeIndex>>& delayed() const {
        return delayed_;
    }
    [[nodiscard]] const HandlerIdentity& node(NodeIndex index) const {
        return nodes_[index];
    }

private:
    void add(const HandlerIdentity& producer, const HandlerIdentity& consumer, bool delayed) {
        const auto from = index_.at(producer);
        const auto to   = index_.at(consumer);
        all_[from].push_back(to);
        (delayed ? delayed_ : immediate_)[from].push_back(to);
    }

    std::vector<HandlerIdentity> nodes_;
    std::unordered_map<HandlerIdentity, NodeIndex, HandlerIdentityHash> index_;
    std::vector<std::vector<NodeIndex>> all_;
    std::vector<std::vector<NodeIndex>> immediate_;
    std::vector<std::vector<NodeIndex>> delayed_;
};

// Tarjan's algorithm; returns each node's strongly connected component.
class StrongComponents {
public:
    explicit StrongComponents(const std::vector<std::vector<NodeIndex>>& edges)
        : edges_(&edges), order_(edges.size(), kUnvisited), low_(edges.size()), component_(edges.size(), kUnvisited),
          on_stack_(edges.size(), false) {
        for (NodeIndex node = 0; node < edges.size(); ++node) {
            if (order_[node] == kUnvisited) {
                visit(node);
            }
        }
    }

    [[nodiscard]] const std::vector<NodeIndex>& components() const {
        return component_;
    }

private:
    void visit(NodeIndex node) {  // NOLINT(misc-no-recursion) -- depth is bounded by the handler count
        order_[node] = low_[node] = next_order_++;
        stack_.push_back(node);
        on_stack_[node] = true;
        for (const auto next : (*edges_)[node]) {
            if (order_[next] == kUnvisited) {
                visit(next);
                low_[node] = std::min(low_[node], low_[next]);
            } else if (on_stack_[next]) {
                low_[node] = std::min(low_[node], order_[next]);
            }
        }
        if (low_[node] != order_[node]) {
            return;
        }
        NodeIndex member = kUnvisited;
        while (member != node) {
            member = stack_.back();
            stack_.pop_back();
            on_stack_[member]  = false;
            component_[member] = next_component_;
        }
        ++next_component_;
    }

    const std::vector<std::vector<NodeIndex>>* edges_;
    std::vector<NodeIndex> order_;
    std::vector<NodeIndex> low_;
    std::vector<NodeIndex> component_;
    std::vector<bool> on_stack_;
    std::vector<NodeIndex> stack_;
    NodeIndex next_order_     = 0;
    NodeIndex next_component_ = 0;
};

void mark_delayed_edges(ExecutionGraph& graph) {
    const DeliveryGraph delivery(graph);
    const StrongComponents strong(delivery.all());
    const auto& component = strong.components();
    // Two handlers in one component are on a cycle together; a self edge puts one handler on a cycle.
    const auto delayed = [&](const HandlerIdentity& producer, const HandlerIdentity& consumer) {
        return component[delivery.index_of(producer)] == component[delivery.index_of(consumer)];
    };
    for (auto& edge : graph.event_flows) {
        edge.delayed = delayed(edge.producer, edge.consumer);
    }
    for (auto& edge : graph.lifecycle_flows) {
        edge.delayed = delayed(edge.producer, edge.consumer);
    }
}

class ActivationDepths {
public:
    explicit ActivationDepths(const ExecutionGraph& graph) : graph_(&graph), delivery_(graph) {
        height_.assign(delivery_.size(), 0);
    }

    [[nodiscard]] ActivationDepth measure(ActivationRootKind kind,
                                          std::optional<SymbolId> symbol,
                                          const std::vector<NodeIndex>& starting) {
        std::vector<bool> reached(delivery_.size(), false);
        std::vector<NodeIndex> pending = starting;
        for (const auto node : starting) {
            reached[node] = true;
        }
        ActivationDepth result{.kind = kind, .symbol = std::move(symbol)};
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            result.depth = std::max(result.depth, height(node));
            result.produces_delays |= !delivery_.delayed()[node].empty();
            for (const auto next : delivery_.all()[node]) {
                if (!reached[next]) {
                    reached[next] = true;
                    pending.push_back(next);
                }
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<NodeIndex> handlers_where(const auto& matches) const {
        std::vector<NodeIndex> found;
        for (const auto& handler : graph_->handlers) {
            if (matches(handler.identity.trigger)) {
                found.push_back(delivery_.index_of(handler.identity));
            }
        }
        return found;
    }

    [[nodiscard]] std::vector<NodeIndex> handlers_of(const ResolvedHandlerTrigger& trigger) const {
        return handlers_where([&](const ResolvedHandlerTrigger& candidate) { return candidate == trigger; });
    }

private:
    // Immediate edges form a DAG, so the memoized walk ends.
    std::uint32_t height(NodeIndex node) {  // NOLINT(misc-no-recursion) -- bounded by the handler count
        if (height_[node] != 0) {
            return height_[node];
        }
        std::uint32_t below = 0;
        for (const auto next : delivery_.immediate()[node]) {
            below = std::max(below, height(next));
        }
        height_[node] = below + 1;
        return height_[node];
    }

    const ExecutionGraph* graph_;
    DeliveryGraph delivery_;
    std::vector<std::uint32_t> height_;
};

void record_activation_depths(ExecutionGraph& graph) {
    ActivationDepths depths(graph);
    std::vector<ActivationDepth> recorded;
    for (const auto& phase : graph.phases) {
        const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Phase, .symbol = phase.phase};
        recorded.push_back(depths.measure(ActivationRootKind::Phase, phase.phase, depths.handlers_of(trigger)));
    }
    std::vector<SymbolId> events;
    for (const auto& handler : graph.handlers) {
        const auto& trigger = handler.identity.trigger;
        if (trigger.kind != HandlerTriggerKind::Event || std::ranges::contains(events, trigger.symbol)) {
            continue;
        }
        const bool claimed = std::ranges::any_of(graph.phases, [&](const PhasePlan& phase) {
            return phase.runtime_root == trigger.symbol;
        });
        if (!claimed) {
            events.push_back(trigger.symbol);
        }
    }
    std::ranges::sort(events, {}, [](const SymbolId& event) { return make_canonical_id(event); });
    for (const auto& event : events) {
        const ResolvedHandlerTrigger trigger{.kind = HandlerTriggerKind::Event, .symbol = event};
        recorded.push_back(depths.measure(ActivationRootKind::Event, event, depths.handlers_of(trigger)));
    }
    const auto arrivals = depths.handlers_where(
        [](const ResolvedHandlerTrigger& trigger) { return trigger.kind == HandlerTriggerKind::TraitAdded; });
    if (!arrivals.empty()) {
        recorded.push_back(depths.measure(ActivationRootKind::StartupArrival, std::nullopt, arrivals));
    }
    graph.activation_depths = std::move(recorded);
}

}  // namespace

TemplateTraitLookup template_trait_lookup(const ModulePersistenceMetadata& persistence) {
    const auto parent = make_symbol_id(SymbolKind::Trait, "std.core", "Parent");
    TemplateTraitLookup lookup;
    for (const auto& archetype : persistence.archetypes) {
        auto& traits = lookup[archetype.node.archetype];
        for (const auto& baseline : archetype.baseline_traits) {
            if (!std::ranges::contains(traits, baseline.trait)) {
                traits.push_back(baseline.trait);
            }
        }
        if (!archetype.node.role_path.empty() && !std::ranges::contains(traits, parent)) {
            traits.push_back(parent);
        }
    }
    return lookup;
}

void classify_delivery_cycles(ExecutionGraph& graph, const TemplateTraitLookup& templates) {
    build_lifecycle_flows(graph, templates);
    mark_delayed_edges(graph);
    record_activation_depths(graph);
}

std::unordered_map<HandlerIdentity, std::uint32_t, HandlerIdentityHash> delivery_components(
    const ExecutionGraph& graph) {
    const DeliveryGraph delivery(graph);
    // Every edge of a cycle is delayed and no delayed edge leaves one, so the
    // cycles are the connected groups of delayed edges.
    std::vector<std::vector<NodeIndex>> linked(delivery.size());
    for (NodeIndex node = 0; node < delivery.size(); ++node) {
        for (const auto next : delivery.delayed()[node]) {
            linked[node].push_back(next);
            linked[next].push_back(node);
        }
    }
    std::unordered_map<HandlerIdentity, std::uint32_t, HandlerIdentityHash> components;
    std::uint32_t next_component = 0;
    for (NodeIndex start = 0; start < delivery.size(); ++start) {
        if (linked[start].empty() || components.contains(delivery.node(start))) {
            continue;
        }
        std::vector<NodeIndex> pending{start};
        components.emplace(delivery.node(start), next_component);
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            for (const auto next : linked[node]) {
                if (components.emplace(delivery.node(next), next_component).second) {
                    pending.push_back(next);
                }
            }
        }
        ++next_component;
    }
    return components;
}

}  // namespace cactus
