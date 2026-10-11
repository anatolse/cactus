#include "frontend/execution_graph_scheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <sstream>
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

// The event and lifecycle triggers a handler's emits and commands deliver.
std::vector<ResolvedHandlerTrigger> produced_triggers(const HandlerContract& contract) {
    std::vector<ResolvedHandlerTrigger> triggers;
    triggers.reserve(contract.emits.size() + contract.commands.size());
    for (const auto& event : contract.emits) {
        triggers.push_back(ResolvedHandlerTrigger{.kind = HandlerTriggerKind::Event, .symbol = event});
    }
    for (const auto& command : contract.commands) {
        if (!command.target.has_value() ||
            (command.kind != HandlerCommandKind::Add && command.kind != HandlerCommandKind::Remove)) {
            continue;
        }
        const auto kind = command.kind == HandlerCommandKind::Add ? HandlerTriggerKind::TraitAdded
                                                                  : HandlerTriggerKind::TraitRemoved;
        triggers.push_back(ResolvedHandlerTrigger{.kind = kind, .symbol = *command.target});
    }
    return triggers;
}

// Per event or lifecycle trigger, the lowest-id consumer that observes occurrence order.
class OrderObservers {
public:
    explicit OrderObservers(const ExecutionGraph& graph) {
        produced_.reserve(graph.handlers.size());
        for (const auto& handler : graph.handlers) {
            produced_.push_back(produced_triggers(handler.contract));
        }
        const auto observes = observing_handlers(graph);
        for (std::size_t index = 0; index < graph.handlers.size(); ++index) {
            const auto& identity = graph.handlers[index].identity;
            const bool consumer  = identity.trigger.kind != HandlerTriggerKind::Phase &&
                                  identity.trigger.kind != HandlerTriggerKind::RenderStage;
            if (!observes[index] || !consumer) {
                continue;
            }
            auto id         = identity.canonical_id();
            const auto key  = identity.trigger.debug_string();
            const auto seen = observer_.find(key);
            if (seen == observer_.end() || id < seen->second) {
                observer_[key] = std::move(id);
            }
        }
    }

    // The triggers both handlers deliver that some consumer observes the order of.
    [[nodiscard]] std::vector<std::string> conflicts(std::size_t left, std::size_t right) const {
        std::vector<std::string> conflicts;
        for (const auto& trigger : produced_[left]) {
            if (!std::ranges::contains(produced_[right], trigger)) {
                continue;
            }
            const auto key      = trigger.debug_string();
            const auto observer = observer_.find(key);
            if (observer == observer_.end()) {
                continue;
            }
            auto text = key + " observed by " + observer->second;
            if (!std::ranges::contains(conflicts, text)) {
                conflicts.push_back(std::move(text));
            }
        }
        return conflicts;
    }

private:
    // Walks deliveries backwards from non-commuting handlers, so event cycles terminate.
    [[nodiscard]] std::vector<bool> observing_handlers(const ExecutionGraph& graph) const {
        const auto count = graph.handlers.size();
        std::vector<std::vector<std::size_t>> producers(count);
        for (std::size_t consumer = 0; consumer < count; ++consumer) {
            for (std::size_t producer = 0; producer < count; ++producer) {
                if (std::ranges::contains(produced_[producer], graph.handlers[consumer].identity.trigger)) {
                    producers[consumer].push_back(producer);
                }
            }
        }
        std::vector<bool> observes(count, false);
        std::vector<std::size_t> pending;
        for (std::size_t index = 0; index < count; ++index) {
            if (executions_conflict(graph.handlers[index].contract)) {
                observes[index] = true;
                pending.push_back(index);
            }
        }
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            for (const auto producer : producers[current]) {
                if (!observes[producer]) {
                    observes[producer] = true;
                    pending.push_back(producer);
                }
            }
        }
        return observes;
    }

    std::vector<std::vector<ResolvedHandlerTrigger>> produced_;
    std::unordered_map<std::string, std::string> observer_;
};

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

// A conflict no meaningful rule orients; `first` has the smaller canonical id.
struct UnorientableConflict {
    const HandlerNode* first  = nullptr;
    const HandlerNode* second = nullptr;
    std::vector<FieldProvenance> fields;
    std::vector<std::string> effects;
    std::vector<std::string> channels;
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
    std::vector<std::string> reasons;
    for (const auto& provenance : conflict.fields) {
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
    reasons.insert(reasons.end(), conflict.channels.begin(), conflict.channels.end());
    for (const auto& effect : conflict.effects) {
        reasons.push_back("effect " + effect);
    }
    std::string listed;
    for (const auto& reason : reasons) {
        listed += (listed.empty() ? "" : ", ") + reason;
    }
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
bool compute_handler_schedule(ExecutionGraph& graph, ErrorReporter& errors) {
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
    // producer/consumer feedback is legal and bounded by runtime cascade
    // semantics. Sort each producer's canonical event IDs so graph artifacts
    // never inherit unordered_set iteration order.
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
    const OrderObservers order_observers(graph);
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

            std::vector<FieldProvenance> field_provenance;
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
                    const auto existing = std::ranges::find(field_provenance, trait, &FieldProvenance::trait);
                    if (existing == field_provenance.end()) {
                        field_provenance.push_back(FieldProvenance{.trait = trait, .access = *overlap});
                    } else {
                        merge_field_access(existing->access, *overlap);
                    }
                }
                return found_any;
            };
            const bool left_writes_right = add_overlaps(left, right, produced_by_handler[left_index]);
            const bool right_writes_left = add_overlaps(right, left, produced_by_handler[right_index]);
            std::ranges::sort(field_provenance, [](const FieldProvenance& a, const FieldProvenance& b) {
                return make_canonical_id(a.trait) < make_canonical_id(b.trait);
            });
            std::vector<SymbolId> trait_provenance;
            trait_provenance.reserve(field_provenance.size());
            std::ranges::transform(field_provenance, std::back_inserter(trait_provenance), &FieldProvenance::trait);

            std::vector<std::string> effect_provenance;
            for (const auto& effect : left.contract.effects) {
                const bool acts = left.contract.effect_acts.contains(effect) || right.contract.effect_acts.contains(effect);
                if (acts && right.contract.effects.contains(effect)) {
                    effect_provenance.push_back(effect);
                }
            }
            std::ranges::sort(effect_provenance);
            auto channel_provenance = command_conflicts(left.contract, right.contract);
            std::ranges::move(order_observers.conflicts(left_index, right_index), std::back_inserter(channel_provenance));
            std::ranges::sort(channel_provenance);
            if (trait_provenance.empty() && effect_provenance.empty() && channel_provenance.empty()) {
                continue;
            }

            const auto oriented = orient(left_index, right_index, left_writes_right, right_writes_left);
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
