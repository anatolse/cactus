#include "frontend/execution_graph_scheduler.hpp"

#include "frontend/delivery_cycles.hpp"
#include "frontend/native_result_ordering.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cactus {

namespace {

bool declaration_precedes(const HandlerNode& left, const HandlerNode& right) {
    const auto& lhs = left.declaration_order;
    const auto& rhs = right.declaration_order;
    if (lhs.module_index != rhs.module_index) {
        return lhs.module_index < rhs.module_index;
    }
    if (lhs.declaration_index != rhs.declaration_index) {
        return lhs.declaration_index < rhs.declaration_index;
    }
    if (lhs.handler_index != rhs.handler_index) {
        return lhs.handler_index < rhs.handler_index;
    }
    return left.identity.canonical_id() < right.identity.canonical_id();
}

// For each module, every module it imports directly or transitively.
std::unordered_map<std::string, std::unordered_set<std::string>> import_closure(const std::vector<ModuleImport>& imports) {
    std::unordered_map<std::string, std::vector<std::string>> direct;
    for (const auto& entry : imports) {
        direct[entry.module].push_back(entry.imported);
    }
    std::unordered_map<std::string, std::unordered_set<std::string>> closure;
    for (const auto& [module, unused] : direct) {
        auto& reached = closure[module];
        std::vector<std::string> pending{module};
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            const auto found = direct.find(current);
            if (found == direct.end()) {
                continue;
            }
            for (const auto& imported : found->second) {
                if (reached.insert(imported).second) {
                    pending.push_back(imported);
                }
            }
        }
    }
    return closure;
}

// The fields of `trait` that `producer` makes and `consumer` observes, if any.
std::optional<FieldAccess> produced_overlap(const HandlerNode& producer,
                                            const HandlerNode& consumer,
                                            const SymbolId& trait) {
    const bool projected = producer.contract.projects.contains(trait);
    if (projected && !consumer.contract.projects.contains(trait) &&
        std::ranges::find(consumer.contract.selection, trait) != consumer.contract.selection.end()) {
        return all_fields();
    }
    if (!consumer.contract.reads.contains(trait)) {
        return std::nullopt;
    }
    const FieldAccess& produced = projected ? all_fields() : field_access(producer.contract.write_fields, trait);
    return overlapping_fields(produced, field_access(consumer.contract.read_fields, trait));
}

// produced_overlap without the fields both handlers only write with commuting kinds.
std::optional<FieldAccess> conflicting_overlap(const HandlerNode& producer,
                                               const HandlerNode& consumer,
                                               const SymbolId& trait) {
    auto overlap = produced_overlap(producer, consumer, trait);
    if (!overlap.has_value() || overlap->all) {
        return overlap;
    }
    std::erase_if(overlap->fields, [&](const std::string& field) {
        return writes_commute(write_kind(producer.contract, trait, field), write_kind(consumer.contract, trait, field));
    });
    return overlap->fields.empty() ? std::nullopt : overlap;
}

bool writes_any(const HandlerNode& producer, const HandlerNode& consumer, const std::unordered_set<SymbolId>& produced) {
    return std::ranges::any_of(
        produced, [&](const SymbolId& trait) { return conflicting_overlap(producer, consumer, trait).has_value(); });
}

// The command table: per trait, marker adds and removes are idempotent,
// destroy and spawn commute with everything, and the rest assign.
bool commands_conflict(const InferredHandlerCommand& left, const InferredHandlerCommand& right) {
    const auto structural = [](HandlerCommandKind kind) {
        return kind == HandlerCommandKind::Spawn || kind == HandlerCommandKind::Destroy;
    };
    if (structural(left.kind) || structural(right.kind) || !left.target.has_value() || left.target != right.target) {
        return false;
    }
    if (left.kind == HandlerCommandKind::Remove || right.kind == HandlerCommandKind::Remove) {
        return left.kind != right.kind;
    }
    if (left.kind != right.kind) {
        return true;
    }
    return overlapping_fields(left.patched.value_or(all_fields()), right.patched.value_or(all_fields())).has_value();
}

std::vector<std::string> command_conflicts(const HandlerContract& left, const HandlerContract& right) {
    std::vector<std::string> conflicts;
    for (const auto& first : left.commands) {
        for (const auto& second : right.commands) {
            if (!commands_conflict(first, second)) {
                continue;
            }
            auto text = "commands " + make_canonical_id(*first.target) + ": " + handler_command_kind_name(first.kind) +
                        " / " + handler_command_kind_name(second.kind);
            if (!std::ranges::contains(conflicts, text)) {
                conflicts.push_back(std::move(text));
            }
        }
    }
    return conflicts;
}

// Whether two executions of one handler can give different results in
// different orders.
bool executions_conflict(const HandlerContract& contract) {
    if (!contract.effect_acts.empty() || !contract.projects.empty()) {
        return true;
    }
    for (const auto& trait : contract.writes) {
        const auto& access = field_access(contract.write_fields, trait);
        const auto ordered = [&](const std::string& field) {
            const auto kind = write_kind(contract, trait, field);
            return !writes_commute(kind, kind);
        };
        if (access.all || std::ranges::any_of(access.fields, ordered)) {
            return true;
        }
    }
    return std::ranges::any_of(contract.commands, [&](const InferredHandlerCommand& command) {
        return std::ranges::any_of(contract.commands, [&](const InferredHandlerCommand& other) {
            return commands_conflict(command, other);
        });
    });
}

// The trait a transform propagation rule derives, which every other writer of
// that trait in the same trigger must precede.
std::optional<SymbolId> derived_trait(const HandlerNode& handler) {
    const auto& rule       = handler.identity.rule;
    const bool propagation = rule.local_name == "TransformPropagation" &&
                             (rule.module.name == "std.transform.flat" || rule.module.name == "std.transform.volume");
    if (!propagation) {
        return std::nullopt;
    }
    return make_symbol_id(SymbolKind::Trait, rule.module.name, "WorldTransform");
}

bool feeds_derivation(const std::unordered_set<SymbolId>& produced, const HandlerNode& derivation) {
    const auto trait = derived_trait(derivation);
    return trait.has_value() && produced.contains(*trait);
}

// Orients conflicts between a handler and the members of a `pub group` from a
// module it imports. The whole group decides, so every member edge points the
// same way and the group's own chain can't form a cycle with the handler.
class PublicGroupOrder {
public:
    PublicGroupOrder(const ExecutionGraph& graph, const std::vector<std::unordered_set<SymbolId>>& produced)
        : graph_(&graph),
          produced_(&produced),
          imports_(import_closure(graph.module_imports)) {
        group_modules_.reserve(graph.handlers.size());
        for (const auto& handler : graph.handlers) {
            group_modules_.push_back(public_group_module(handler));
        }
    }

    struct Decision {
        bool left_first = false;
        ScheduleEdgeOrientation orientation = ScheduleEdgeOrientation::ImporterBeforePublicGroup;
    };

    [[nodiscard]] std::optional<Decision> decide(std::size_t left, std::size_t right) const {
        if (imports_group_of(left, right)) {
            return decide_for_importer(left, right, true);
        }
        if (imports_group_of(right, left)) {
            return decide_for_importer(right, left, false);
        }
        return std::nullopt;
    }

private:
    [[nodiscard]] const std::string* public_group_module(const HandlerNode& handler) const {
        if (!handler.group.has_value()) {
            return nullptr;
        }
        const auto declared = std::ranges::find(graph_->group_declarations, *handler.group, &GroupDeclaration::group);
        return declared != graph_->group_declarations.end() && declared->is_pub ? &handler.group->module.name : nullptr;
    }

    [[nodiscard]] bool imports_group_of(std::size_t importer, std::size_t member) const {
        const auto* module = group_modules_[member];
        if (module == nullptr || group_modules_[importer] != nullptr) {
            return false;
        }
        const auto found = imports_.find(graph_->handlers[importer].identity.rule.module.name);
        return found != imports_.end() && found->second.contains(*module);
    }

    // A two-way or effect-only conflict with the group runs the importer first;
    // a one-way conflict runs the writer first.
    [[nodiscard]] Decision decide_for_importer(std::size_t importer, std::size_t member, bool importer_is_left) const {
        const auto& handler    = graph_->handlers[importer];
        const auto& group      = graph_->handlers[member].group;
        bool importer_writes   = false;
        bool group_writes      = false;
        for (std::size_t index = 0; index < graph_->handlers.size(); ++index) {
            const auto& other = graph_->handlers[index];
            if (other.group != group || other.identity.trigger != handler.identity.trigger) {
                continue;
            }
            importer_writes = importer_writes || writes_any(handler, other, (*produced_)[importer]);
            group_writes    = group_writes || writes_any(other, handler, (*produced_)[index]);
        }
        if (importer_writes == group_writes) {
            return Decision{.left_first = importer_is_left};
        }
        return Decision{.left_first  = importer_writes == importer_is_left,
                        .orientation = ScheduleEdgeOrientation::WriterBeforeReader};
    }

    const ExecutionGraph* graph_;
    const std::vector<std::unordered_set<SymbolId>>* produced_;
    std::unordered_map<std::string, std::unordered_set<std::string>> imports_;
    std::vector<const std::string*> group_modules_;
};

std::string write_kind_name(const FieldWriteKind& kind) {
    switch (kind.kind) {
        case WriteKind::Assign:
            return "Assign";
        case WriteKind::Accum:
            return "Accum";
        case WriteKind::General:
            return "General";
        case WriteKind::Join:
            break;
    }
    switch (kind.op) {
        case JoinOp::Max:
            return "Join max";
        case JoinOp::Min:
            return "Join min";
        case JoinOp::Or:
            return "Join or";
        case JoinOp::And:
            return "Join and";
    }
    std::unreachable();
}

// How `handler` touches the field, or the whole trait when `field` is unset.
std::string access_text(const HandlerNode& handler, const SymbolId& trait, const std::optional<std::string>& field) {
    const auto& contract = handler.contract;
    if (!field.has_value()) {
        return contract.produced_traits().contains(trait) ? "write" : "read";
    }
    const bool writes = contract.writes.contains(trait) && field_access(contract.write_fields, trait).covers(*field);
    return writes ? write_kind_name(write_kind(contract, trait, *field)) : "read";
}

// The data, command and effect reasons two handlers' runs do not commute.
struct PairConflict {
    std::vector<FieldProvenance> fields;  // sorted by trait canonical id
    bool left_writes_right = false;
    bool right_writes_left = false;
    std::vector<std::string> effects;   // sorted
    std::vector<std::string> commands;  // sorted
};

PairConflict pair_conflict(const HandlerNode& left,
                           const HandlerNode& right,
                           const std::unordered_set<SymbolId>& left_produced,
                           const std::unordered_set<SymbolId>& right_produced) {
    PairConflict conflict;
    const auto add_overlaps = [&](const HandlerNode& producer,
                                  const HandlerNode& consumer,
                                  const std::unordered_set<SymbolId>& produced) {
        bool found_any = false;
        for (const auto& trait : produced) {
            const auto overlap = conflicting_overlap(producer, consumer, trait);
            if (!overlap.has_value()) {
                continue;
            }
            found_any           = true;
            const auto existing = std::ranges::find(conflict.fields, trait, &FieldProvenance::trait);
            if (existing == conflict.fields.end()) {
                conflict.fields.push_back(FieldProvenance{.trait = trait, .access = *overlap});
            } else {
                merge_field_access(existing->access, *overlap);
            }
        }
        return found_any;
    };
    conflict.left_writes_right = add_overlaps(left, right, left_produced);
    conflict.right_writes_left = add_overlaps(right, left, right_produced);
    std::ranges::sort(conflict.fields, {}, [](const FieldProvenance& entry) { return make_canonical_id(entry.trait); });
    for (const auto& effect : left.contract.effects) {
        const bool acts = left.contract.effect_acts.contains(effect) || right.contract.effect_acts.contains(effect);
        if (acts && right.contract.effects.contains(effect)) {
            conflict.effects.push_back(effect);
        }
    }
    std::ranges::sort(conflict.effects);
    conflict.commands = command_conflicts(left.contract, right.contract);
    std::ranges::sort(conflict.commands);
    return conflict;
}

std::vector<std::string> conflict_reasons(const HandlerNode& first,
                                          const HandlerNode& second,
                                          const std::vector<FieldProvenance>& fields,
                                          const std::vector<std::string>& channels,
                                          const std::vector<std::string>& effects) {
    std::vector<std::string> reasons;
    for (const auto& provenance : fields) {
        const auto side = [&](const std::optional<std::string>& field) {
            std::ostringstream text;
            text << " (" << first.identity.rule.local_name << ": " << access_text(first, provenance.trait, field) << ", "
                 << second.identity.rule.local_name << ": " << access_text(second, provenance.trait, field) << ")";
            return std::move(text).str();
        };
        const auto trait = make_canonical_id(provenance.trait);
        if (provenance.access.all) {
            reasons.push_back(std::format("{} all fields{}", trait, side(std::nullopt)));
            continue;
        }
        for (const auto& field : provenance.access.fields) {
            reasons.push_back(std::format("{}.{}{}", trait, field, side(field)));
        }
    }
    reasons.insert(reasons.end(), channels.begin(), channels.end());
    for (const auto& effect : effects) {
        reasons.push_back("effect " + effect);
    }
    return reasons;
}

std::string joined(const std::vector<std::string>& parts, std::string_view separator) {
    std::string text;
    for (const auto& part : parts) {
        text += (text.empty() ? "" : std::string(separator)) + part;
    }
    return text;
}

// The program's handlers, indexed as in the graph, followed by the runtime's
// own event consumers, which act on their effect in arrival order.
class ConflictNodes {
public:
    ConflictNodes(const ExecutionGraph& graph, const std::vector<std::unordered_set<SymbolId>>& produced)
        : produced_(&produced) {
        for (const auto& handler : graph.handlers) {
            nodes_.push_back(&handler);
        }
        for (const auto& consumer : native_consumers()) {
            add_native_consumer(graph, consumer);
        }
    }

    [[nodiscard]] std::size_t size() const {
        return nodes_.size();
    }
    [[nodiscard]] const HandlerNode& node(std::size_t index) const {
        return *nodes_[index];
    }
    // Native consumers produce no traits.
    [[nodiscard]] const std::unordered_set<SymbolId>& produced(std::size_t index) const {
        static const std::unordered_set<SymbolId> none;
        return index < produced_->size() ? (*produced_)[index] : none;
    }
    // (producer, native consumer) for every emit of a runtime-consumed event.
    [[nodiscard]] const std::vector<std::pair<std::size_t, std::size_t>>& native_flows() const {
        return native_flows_;
    }

private:
    void add_native_consumer(const ExecutionGraph& graph, const NativeConsumer& consumer) {
        std::optional<std::size_t> index;
        for (std::size_t producer = 0; producer < graph.handlers.size(); ++producer) {
            const auto& emits = graph.handlers[producer].contract.emits;
            const auto event  = std::ranges::find_if(
                emits, [&](const SymbolId& emitted) { return make_canonical_id(emitted) == consumer.event; });
            if (event == emits.end()) {
                continue;
            }
            if (!index.has_value()) {
                index = nodes_.size();
                nodes_.push_back(&native_.emplace_back(native_node(*event, consumer)));
            }
            native_flows_.emplace_back(producer, *index);
        }
    }

    static HandlerNode native_node(const SymbolId& event, const NativeConsumer& consumer) {
        HandlerNode node{
            .identity = HandlerIdentity{.rule    = make_symbol_id(SymbolKind::Rule, event.module.name, "Runtime"),
                                        .trigger = ResolvedHandlerTrigger{.kind = HandlerTriggerKind::Event, .symbol = event}},
            .implementation = HandlerImplementationKind::External};
        node.contract.effects.emplace(consumer.effect);
        node.contract.effect_acts.emplace(consumer.effect);
        return node;
    }

    std::vector<const HandlerNode*> nodes_;
    const std::vector<std::unordered_set<SymbolId>>* produced_;
    std::deque<HandlerNode> native_;
    std::vector<std::pair<std::size_t, std::size_t>> native_flows_;
};

// Why two runs do not commute, filled lazily per handler pair; empty when they commute.
class RunConflicts {
public:
    explicit RunConflicts(const ConflictNodes& nodes) : nodes_(&nodes) {}

    const std::string& reason(std::size_t left, std::size_t right) {
        const auto key   = std::pair(std::min(left, right), std::max(left, right));
        const auto found = cache_.find(key);
        if (found != cache_.end()) {
            return found->second;
        }
        return cache_.emplace(key, compute(key.first, key.second)).first->second;
    }

private:
    [[nodiscard]] std::string compute(std::size_t left, std::size_t right) const {
        const auto& first  = nodes_->node(left);
        const auto& second = nodes_->node(right);
        if (left == right && !executions_conflict(first.contract)) {
            return {};
        }
        const auto conflict = pair_conflict(first, second, nodes_->produced(left), nodes_->produced(right));
        auto reasons        = conflict_reasons(first, second, conflict.fields, conflict.commands, conflict.effects);
        if (reasons.empty() && left == right) {
            reasons.emplace_back("its runs do not commute");
        }
        return joined(reasons, ", ");
    }

    const ConflictNodes* nodes_;
    std::map<std::pair<std::size_t, std::size_t>, std::string> cache_;
};

// A run's place relative to the producer that reaches it: commit rounds after
// the producer's, then the cascade generation within that round.
struct Position {
    std::uint32_t round = 0;
    std::uint32_t depth = 0;

    friend auto operator<=>(const Position&, const Position&) = default;
};

// A handler run reached from a producer, with the shortest delivery path to it.
// A delayed run has no position.
struct ReachedRun {
    std::size_t handler = 0;
    Position position;
    bool delayed = false;
    std::vector<std::string> path;
};

// Which runs each handler's occurrences reach, and the occurrence races
// between two co-eligible handlers.
class OccurrenceReach {
public:
    OccurrenceReach(const ExecutionGraph& graph, const ConflictNodes& nodes, RunConflicts& conflicts)
        : nodes_(&nodes),
          conflicts_(&conflicts) {
        std::unordered_map<HandlerIdentity, std::size_t, HandlerIdentityHash> index;
        for (std::size_t handler = 0; handler < graph.handlers.size(); ++handler) {
            index.emplace(graph.handlers[handler].identity, handler);
        }
        edges_.resize(nodes.size());
        const auto add = [&](std::size_t from, std::size_t to, bool lifecycle, bool delayed) {
            auto label    = nodes.node(to).identity.trigger.debug_string();
            auto& targets = edges_[from];
            const Edge edge{.to = to, .lifecycle = lifecycle, .delayed = delayed, .label = std::move(label)};
            if (!std::ranges::contains(targets, edge)) {
                targets.push_back(edge);
            }
        };
        for (const auto& flow : graph.event_flows) {
            add(index.at(flow.producer), index.at(flow.consumer), false, flow.delayed);
        }
        for (const auto& flow : graph.lifecycle_flows) {
            add(index.at(flow.producer), index.at(flow.consumer), true, flow.delayed);
        }
        for (const auto& [producer, consumer] : nodes.native_flows()) {
            add(producer, consumer, false, false);
        }
        for (auto& targets : edges_) {
            std::ranges::sort(targets, {}, [&](const Edge& edge) {
                return std::pair(edge.label, nodes.node(edge.to).identity.canonical_id());
            });
        }
        reach_.resize(nodes.size());
    }

    // The shortest race between runs reached from `left` and from `right`, if any.
    [[nodiscard]] std::optional<std::string> race(std::size_t left, std::size_t right) {
        if (nodes_->node(right).identity.canonical_id() < nodes_->node(left).identity.canonical_id()) {
            std::swap(left, right);
        }
        const auto& first  = reach(left);
        const auto& second = reach(right);
        std::optional<std::pair<std::size_t, std::string>> best;
        for (const auto& one : first) {
            for (const auto& other : second) {
                const bool together = one.delayed == other.delayed && (one.delayed || one.position == other.position);
                if (!together) {
                    continue;
                }
                const auto& reason = conflicts_->reason(one.handler, other.handler);
                if (reason.empty()) {
                    continue;
                }
                auto candidate = std::pair(one.path.size() + other.path.size(),
                                           describe(left, one) + "; " + describe(right, other) + ": " + reason);
                if (!best.has_value() || candidate < *best) {
                    best = std::move(candidate);
                }
            }
        }
        if (!best.has_value()) {
            return std::nullopt;
        }
        return std::move(best->second);
    }

private:
    struct Edge {
        std::size_t to = 0;
        bool lifecycle = false;
        bool delayed   = false;
        std::string label;

        friend bool operator==(const Edge&, const Edge&) = default;
    };

    [[nodiscard]] std::string describe(std::size_t producer, const ReachedRun& run) const {
        return nodes_->node(producer).identity.canonical_id() + " via " + joined(run.path, " -> ") + " reaches " +
               nodes_->node(run.handler).identity.canonical_id();
    }

    // Breadth-first, so each run keeps its shortest path. Immediate edges form
    // a DAG and delayed runs carry no position, so the search ends.
    const std::vector<ReachedRun>& reach(std::size_t producer) {
        auto& cached = reach_[producer];
        if (cached.has_value()) {
            return *cached;
        }
        std::vector<ReachedRun> runs;
        std::set<std::tuple<std::size_t, bool, Position>> seen;
        std::deque<ReachedRun> pending{ReachedRun{.handler = producer}};
        while (!pending.empty()) {
            const auto current = std::move(pending.front());
            pending.pop_front();
            for (const auto& edge : edges_[current.handler]) {
                ReachedRun next{.handler = edge.to, .delayed = current.delayed || edge.delayed, .path = current.path};
                if (!next.delayed) {
                    next.position = edge.lifecycle ? Position{.round = current.position.round + 1, .depth = 0}
                                                   : Position{.round = current.position.round,
                                                              .depth = current.position.depth + 1};
                }
                next.path.push_back(edge.delayed ? edge.label + " (delayed)" : edge.label);
                if (!seen.emplace(next.handler, next.delayed, next.position).second) {
                    continue;
                }
                runs.push_back(next);
                pending.push_back(std::move(next));
            }
        }
        cached = std::move(runs);
        return *cached;
    }

    const ConflictNodes* nodes_;
    RunConflicts* conflicts_;
    std::vector<std::vector<Edge>> edges_;
    std::vector<std::optional<std::vector<ReachedRun>>> reach_;
};

// A conflict no meaningful rule orients; `first` has the smaller canonical id.
struct UnorientableConflict {
    const HandlerNode* first  = nullptr;
    const HandlerNode* second = nullptr;
    std::vector<FieldProvenance> fields;
    std::vector<std::string> effects;
    std::vector<std::string> channels;
};

// How `from`'s module names `handler` in an `after:` entry.
std::string handler_reference(const HandlerNode& handler, const HandlerNode& from) {
    const auto& rule    = handler.identity.rule;
    const auto& trigger = handler.identity.trigger;
    const auto& module  = from.identity.rule.module;
    const auto name     = rule.module == module ? rule.local_name : make_canonical_id(rule);
    // std.core is in scope everywhere; any other module's symbol is spelled in full.
    const bool local    = trigger.symbol.module == module || trigger.symbol.module.name == "std.core";
    const auto symbol   = local ? trigger.symbol.local_name : make_canonical_id(trigger.symbol);
    const auto event    = trigger.is_lifecycle() ? std::string(handler_trigger_kind_name(trigger.kind)) + " " + symbol
                                                 : symbol;
    return name + "/on " + event;
}

std::string unorientable_message(const UnorientableConflict& conflict) {
    const auto& first  = *conflict.first;
    const auto& second = *conflict.second;
    const auto listed  = joined(conflict_reasons(first, second, conflict.fields, conflict.channels, conflict.effects), ", ");
    return "unorientable conflict between '" + first.identity.canonical_id() + "' and '" +
           second.identity.canonical_id() + "' on " + listed + "; nothing orders them: add 'after: " +
           handler_reference(second, first) + "' to the " + handler_reference(first, second) +
           " handler, or 'after: " + handler_reference(first, second) + "' to the " +
           handler_reference(second, first) + " handler";
}

}  // namespace

// The single consolidated home for what used to be two independently-duplicated
// ~250-line scheduling algorithms; see design.md D1-D5.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool compute_handler_schedule(ExecutionGraph& graph, const TemplateTraitLookup& templates, ErrorReporter& errors) {
    std::unordered_map<HandlerIdentity, const HandlerNode*, HandlerIdentityHash> nodes;
    for (const auto& handler : graph.handlers) {
        nodes.emplace(handler.identity, &handler);
    }

    // ── Phase-barrier and event-flow construction ───────────────────────────
    // Phase completion is a barrier relation, not an ordinary handler-order
    // edge. A downstream phase activation can begin only after each direct
    // upstream phase has completed its entire activation batch (including all
    // periodic repetitions), so attach every direct phase dependency to every
    // handler selected by the downstream phase.
    for (const auto& phase : graph.phases) {
        for (const auto& upstream : phase.completion_dependencies) {
            for (const auto& handler : graph.handlers) {
                if (handler.identity.trigger.kind == HandlerTriggerKind::Phase &&
                    handler.identity.trigger.symbol == phase.phase) {
                    graph.phase_barriers.push_back(
                        PhaseBarrierEdge{.upstream_phase = upstream, .downstream_handler = handler.identity});
                }
            }
        }
    }
    // Event delivery is deliberately separate from schedule dependencies:
    // producer/consumer feedback is legal because every cycle is delayed
    // (classify_delivery_cycles). Sort each producer's canonical event IDs so
    // graph artifacts never inherit unordered_set iteration order.
    for (const auto& producer : graph.handlers) {
        std::vector<SymbolId> emitted(producer.contract.emits.begin(), producer.contract.emits.end());
        std::ranges::sort(emitted, [](const SymbolId& left, const SymbolId& right) {
            return make_canonical_id(left) < make_canonical_id(right);
        });
        for (const auto& event : emitted) {
            for (const auto& consumer : graph.handlers) {
                if (consumer.identity.trigger.kind == HandlerTriggerKind::Event &&
                    consumer.identity.trigger.symbol == event) {
                    graph.event_flows.push_back(
                        EventFlowEdge{.producer = producer.identity, .event = event, .consumer = consumer.identity});
                }
            }
        }
    }

    classify_delivery_cycles(graph, templates);

    // ── Conflict-edge detection ──────────────────────────────────────────────
    // Contracts create serialization requirements only between handlers that
    // are co-eligible for the same canonical trigger. Detect the conflict
    // independently from its direction so provenance survives whichever
    // ordering rule wins. explicit_adjacency/explicitly_precedes reflect the
    // caller's already-inserted explicit edges (from AST after: clauses or
    // cross-module linking) and decide orientation below.
    std::unordered_map<HandlerIdentity, std::vector<HandlerIdentity>, HandlerIdentityHash> explicit_adjacency;
    for (const auto& edge : graph.schedule_edges) {
        const bool is_explicit = edge.kind == ScheduleEdgeKind::ExplicitHandler ||
                                 edge.kind == ScheduleEdgeKind::ExplicitRule ||
                                 edge.kind == ScheduleEdgeKind::ExplicitGroup;
        if (is_explicit && nodes.contains(edge.before) && nodes.contains(edge.after)) {
            explicit_adjacency[edge.before].push_back(edge.after);
        }
    }
    const auto explicitly_precedes = [&](const HandlerIdentity& before, const HandlerIdentity& after) {
        std::vector<HandlerIdentity> pending{before};
        std::unordered_set<HandlerIdentity, HandlerIdentityHash> visited;
        while (!pending.empty()) {
            auto current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            if (current == after) {
                return true;
            }
            if (const auto found = explicit_adjacency.find(current); found != explicit_adjacency.end()) {
                pending.insert(pending.end(), found->second.begin(), found->second.end());
            }
        }
        return false;
    };

    const auto produced_by_handler = precompute_produced_by_handler(graph.handlers);

    const PublicGroupOrder public_group_order(graph, produced_by_handler);
    const ConflictNodes conflict_nodes(graph, produced_by_handler);
    RunConflicts run_conflicts(conflict_nodes);
    OccurrenceReach occurrence_reach(graph, conflict_nodes, run_conflicts);
    std::vector<UnorientableConflict> unorientable;

    struct Orientation {
        bool left_first = false;
        ScheduleEdgeOrientation reason;
    };
    const auto orient = [&](std::size_t left_index,
                            std::size_t right_index,
                            bool left_writes_right,
                            bool right_writes_left) -> std::optional<Orientation> {
        const auto& left  = graph.handlers[left_index];
        const auto& right = graph.handlers[right_index];
        if (feeds_derivation(produced_by_handler[left_index], right)) {
            return Orientation{.left_first = true, .reason = ScheduleEdgeOrientation::DerivationAfterWriters};
        }
        if (feeds_derivation(produced_by_handler[right_index], left)) {
            return Orientation{.left_first = false, .reason = ScheduleEdgeOrientation::DerivationAfterWriters};
        }
        if (explicitly_precedes(left.identity, right.identity)) {
            return Orientation{.left_first = true, .reason = ScheduleEdgeOrientation::Explicit};
        }
        if (explicitly_precedes(right.identity, left.identity)) {
            return Orientation{.left_first = false, .reason = ScheduleEdgeOrientation::Explicit};
        }
        if (const auto decision = public_group_order.decide(left_index, right_index)) {
            return Orientation{.left_first = decision->left_first, .reason = decision->orientation};
        }
        if (left_writes_right != right_writes_left) {
            return Orientation{.left_first = left_writes_right, .reason = ScheduleEdgeOrientation::WriterBeforeReader};
        }
        return std::nullopt;
    };

    for (std::size_t left_index = 0; left_index < graph.handlers.size(); ++left_index) {
        const auto& left = graph.handlers[left_index];
        for (std::size_t right_index = left_index + 1; right_index < graph.handlers.size(); ++right_index) {
            const auto& right = graph.handlers[right_index];
            if (left.identity.trigger != right.identity.trigger) {
                continue;
            }

            auto conflict =
                pair_conflict(left, right, produced_by_handler[left_index], produced_by_handler[right_index]);
            auto field_provenance = std::move(conflict.fields);
            std::vector<SymbolId> trait_provenance;
            trait_provenance.reserve(field_provenance.size());
            std::ranges::transform(field_provenance, std::back_inserter(trait_provenance), &FieldProvenance::trait);
            auto effect_provenance  = std::move(conflict.effects);
            auto channel_provenance = std::move(conflict.commands);
            if (auto race = occurrence_reach.race(left_index, right_index)) {
                channel_provenance.push_back(std::move(*race));
            }
            std::ranges::sort(channel_provenance);
            if (trait_provenance.empty() && effect_provenance.empty() && channel_provenance.empty()) {
                continue;
            }

            const auto oriented =
                orient(left_index, right_index, conflict.left_writes_right, conflict.right_writes_left);
            if (!oriented.has_value()) {
                const bool left_smaller = left.identity.canonical_id() < right.identity.canonical_id();
                unorientable.push_back(UnorientableConflict{.first    = left_smaller ? &left : &right,
                                                            .second   = left_smaller ? &right : &left,
                                                            .fields   = std::move(field_provenance),
                                                            .effects  = std::move(effect_provenance),
                                                            .channels = std::move(channel_provenance)});
                continue;
            }
            const auto* before     = oriented->left_first ? &left : &right;
            const auto* after      = oriented->left_first ? &right : &left;
            const auto orientation = oriented->reason;
            if (!trait_provenance.empty()) {
                graph.schedule_edges.push_back(ScheduleEdge{.before           = before->identity,
                                                            .after            = after->identity,
                                                            .kind             = ScheduleEdgeKind::DataConflict,
                                                            .orientation      = orientation,
                                                            .trait_provenance = std::move(trait_provenance),
                                                            .field_provenance = std::move(field_provenance)});
            }
            if (!effect_provenance.empty()) {
                graph.schedule_edges.push_back(ScheduleEdge{.before            = before->identity,
                                                            .after             = after->identity,
                                                            .kind              = ScheduleEdgeKind::EffectConflict,
                                                            .orientation       = orientation,
                                                            .effect_provenance = std::move(effect_provenance)});
            }
            if (!channel_provenance.empty()) {
                graph.schedule_edges.push_back(ScheduleEdge{.before             = before->identity,
                                                            .after              = after->identity,
                                                            .kind               = ScheduleEdgeKind::ChannelConflict,
                                                            .orientation        = orientation,
                                                            .channel_provenance = std::move(channel_provenance)});
            }
        }
    }

    std::ranges::sort(unorientable, [](const UnorientableConflict& a, const UnorientableConflict& b) {
        return std::pair(a.first->identity.canonical_id(), a.second->identity.canonical_id()) <
               std::pair(b.first->identity.canonical_id(), b.second->identity.canonical_id());
    });
    for (const auto& conflict : unorientable) {
        errors.error(conflict.second->location, unorientable_message(conflict));
    }

    // ── Handler-cycle DFS ─────────────────────────────────────────────────────
    // Each distinct cycle (by its canonical path string) is reported only
    // once, regardless of how many graph traversals reach it.
    enum class Color : std::uint8_t { White, Gray, Black };
    std::unordered_map<HandlerIdentity, Color, HandlerIdentityHash> color;
    std::unordered_map<HandlerIdentity, std::vector<HandlerIdentity>, HandlerIdentityHash> adjacency;
    for (const auto& [identity, unused] : nodes) {
        color[identity] = Color::White;
    }
    for (const auto& edge : graph.schedule_edges) {
        if (nodes.contains(edge.before) && nodes.contains(edge.after)) {
            adjacency[edge.before].push_back(edge.after);
        }
    }
    std::vector<HandlerIdentity> path;
    std::unordered_set<std::string> reported_cycles;
    std::function<void(const HandlerIdentity&)> dfs = [&](const HandlerIdentity& node) {
        color[node] = Color::Gray;
        path.push_back(node);
        for (const auto& neighbor : adjacency[node]) {
            if (color[neighbor] == Color::Gray) {
                const auto cycle_start = std::ranges::find(path, neighbor);
                std::ostringstream cycle;
                for (auto it = cycle_start; it != path.end(); ++it) {
                    if (it != cycle_start) {
                        cycle << " -> ";
                    }
                    cycle << it->canonical_id();
                }
                cycle << " -> " << neighbor.canonical_id();
                if (reported_cycles.insert(cycle.str()).second) {
                    errors.error({}, "handler cycle: " + cycle.str());
                }
            } else if (color[neighbor] == Color::White) {
                dfs(neighbor);
            }
        }
        path.pop_back();
        color[node] = Color::Black;
    };
    for (const auto& handler : graph.handlers) {
        if (color[handler.identity] == Color::White) {
            dfs(handler.identity);
        }
    }

    // ── Per-activation topological leveling ──────────────────────────────────
    // Finalize each activation-local scheduling DAG independently. A wave of
    // currently-ready handlers is one parallelizable dependency level; stable
    // declaration order within the wave is the sequential backend's tie-break.
    // Multiple provenance edges between the same pair represent one scheduling
    // dependency and therefore contribute only one indegree.
    std::vector<ResolvedHandlerTrigger> activations;
    for (const auto& handler : graph.handlers) {
        if (std::ranges::find(activations, handler.identity.trigger) == activations.end()) {
            activations.push_back(handler.identity.trigger);
        }
    }
    std::ranges::sort(activations, [&](const auto& left, const auto& right) {
        const auto lhs =
            std::ranges::find_if(graph.handlers, [&](const auto& node) { return node.identity.trigger == left; });
        const auto rhs =
            std::ranges::find_if(graph.handlers, [&](const auto& node) { return node.identity.trigger == right; });
        return lhs != graph.handlers.end() && rhs != graph.handlers.end()
                   ? declaration_precedes(*lhs, *rhs)
                   : make_canonical_id(left.symbol) < make_canonical_id(right.symbol);
    });
    for (const auto& activation : activations) {
        std::vector<const HandlerNode*> activation_nodes;
        for (const auto& handler : graph.handlers) {
            if (handler.identity.trigger == activation) {
                activation_nodes.push_back(&handler);
            }
        }
        std::ranges::sort(activation_nodes,
                          [](const auto* left, const auto* right) { return declaration_precedes(*left, *right); });
        std::unordered_map<HandlerIdentity, std::uint64_t, HandlerIdentityHash> indegree;
        std::unordered_map<HandlerIdentity, std::vector<HandlerIdentity>, HandlerIdentityHash> local_adjacency;
        for (const auto* node : activation_nodes) {
            indegree[node->identity] = 0;
        }
        std::unordered_set<std::string> dependency_pairs;
        for (const auto& edge : graph.schedule_edges) {
            if (!indegree.contains(edge.before) || !indegree.contains(edge.after)) {
                continue;
            }
            const auto pair = edge.before.canonical_id() + "\n" + edge.after.canonical_id();
            if (dependency_pairs.insert(pair).second) {
                local_adjacency[edge.before].push_back(edge.after);
                ++indegree[edge.after];
            }
        }
        std::unordered_set<HandlerIdentity, HandlerIdentityHash> emitted;
        std::uint64_t level_index = 0;
        while (emitted.size() < activation_nodes.size()) {
            std::vector<HandlerIdentity> ready;
            for (const auto* node : activation_nodes) {
                if (!emitted.contains(node->identity) && indegree[node->identity] == 0) {
                    ready.push_back(node->identity);
                }
            }
            if (ready.empty()) {
                // The DFS above already emitted canonical cycle diagnostics.
                break;
            }
            graph.dependency_levels.push_back(
                DependencyLevel{.activation = activation, .index = level_index++, .handlers = ready});
            graph.stable_topological_order.insert(graph.stable_topological_order.end(), ready.begin(), ready.end());
            for (const auto& identity : ready) {
                emitted.insert(identity);
                for (const auto& dependent : local_adjacency[identity]) {
                    if (indegree[dependent] > 0) {
                        --indegree[dependent];
                    }
                }
            }
        }
    }

    return !errors.has_errors();
}

namespace {

using HandlerOrder = std::pair<HandlerIdentity, HandlerIdentity>;  // (before, after)

// A rule-level group reference orders one handler against every member.
std::vector<HandlerOrder> group_reference_orders(const ExecutionGraph& graph) {
    std::vector<HandlerOrder> orders;
    for (const auto& ordering : graph.group_orderings) {
        const bool before = ordering.direction == GroupOrderingDirection::Before;
        for (const auto& member : graph.handlers) {
            if (member.group != ordering.group || member.identity == ordering.handler) {
                continue;
            }
            orders.emplace_back(before ? ordering.handler : member.identity,
                                before ? member.identity : ordering.handler);
        }
    }
    return orders;
}

// A group's after: orders every member after every member of each listed group.
std::vector<HandlerOrder> group_after_orders(const ExecutionGraph& graph) {
    const auto members_of = [&](const SymbolId& group) {
        return graph.handlers | std::views::filter([&group](const HandlerNode& node) { return node.group == group; });
    };
    std::vector<HandlerOrder> orders;
    for (const auto& declaration : graph.group_declarations) {
        for (const auto& earlier : declaration.after_groups) {
            for (const auto& later : members_of(declaration.group)) {
                for (const auto& first : members_of(earlier)) {
                    orders.emplace_back(first.identity, later.identity);
                }
            }
        }
    }
    return orders;
}

}  // namespace

void expand_group_orderings(ExecutionGraph& graph) {
    auto orders = group_reference_orders(graph);
    std::ranges::move(group_after_orders(graph), std::back_inserter(orders));
    std::vector<ScheduleEdge> expanded;
    for (const auto& [before, after] : orders) {
        const auto duplicate = std::ranges::any_of(expanded, [&](const ScheduleEdge& existing) {
            return existing.before == before && existing.after == after;
        });
        if (!duplicate) {
            expanded.push_back(ScheduleEdge{.before      = before,
                                            .after       = after,
                                            .kind        = ScheduleEdgeKind::ExplicitGroup,
                                            .orientation = ScheduleEdgeOrientation::Explicit});
        }
    }
    graph.schedule_edges.insert(graph.schedule_edges.end(), expanded.begin(), expanded.end());
}

void validate_lifecycle_trigger_traits(const ExecutionGraph& graph, ErrorReporter& errors) {
    std::unordered_set<SymbolId> projected;
    for (const auto& handler : graph.handlers) {
        projected.insert(handler.contract.projects.begin(), handler.contract.projects.end());
    }
    for (const auto& handler : graph.handlers) {
        const auto& trigger = handler.identity.trigger;
        if (trigger.is_lifecycle() && projected.contains(trigger.symbol)) {
            errors.error(handler.location,
                         "lifecycle triggers require a durable trait; '" + make_canonical_id(trigger.symbol) +
                             "' is projected");
        }
    }
}

}  // namespace cactus
