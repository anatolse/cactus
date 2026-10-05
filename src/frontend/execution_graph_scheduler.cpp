#include "frontend/execution_graph_scheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
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

const FieldAccess ALL_FIELDS{.all = true, .fields = {}};

const FieldAccess& field_access(const std::unordered_map<SymbolId, FieldAccess>& table, const SymbolId& trait) {
    const auto found = table.find(trait);
    return found == table.end() ? ALL_FIELDS : found->second;
}

std::optional<FieldAccess> overlapping_fields(const FieldAccess& left, const FieldAccess& right) {
    if (left.all) {
        return right.all || !right.fields.empty() ? std::optional(right) : std::nullopt;
    }
    if (right.all) {
        return left.fields.empty() ? std::nullopt : std::optional(left);
    }
    FieldAccess shared;
    std::ranges::set_intersection(left.fields, right.fields, std::inserter(shared.fields, shared.fields.end()));
    return shared.fields.empty() ? std::nullopt : std::optional(shared);
}

// The fields of `trait` that `producer` makes and `consumer` observes, if any.
std::optional<FieldAccess> produced_overlap(const HandlerNode& producer,
                                            const HandlerNode& consumer,
                                            const SymbolId& trait) {
    const bool projected = producer.contract.projects.contains(trait);
    if (projected && !consumer.contract.projects.contains(trait) &&
        std::ranges::find(consumer.contract.selection, trait) != consumer.contract.selection.end()) {
        return ALL_FIELDS;
    }
    if (!consumer.contract.reads.contains(trait)) {
        return std::nullopt;
    }
    const FieldAccess& produced = projected ? ALL_FIELDS : field_access(producer.contract.write_fields, trait);
    return overlapping_fields(produced, field_access(consumer.contract.read_fields, trait));
}

void merge_field_access(FieldAccess& into, const FieldAccess& from) {
    if (into.all || from.all) {
        into = ALL_FIELDS;
        return;
    }
    into.fields.insert(from.fields.begin(), from.fields.end());
}

bool writes_any(const HandlerNode& producer, const HandlerNode& consumer, const std::unordered_set<SymbolId>& produced) {
    return std::ranges::any_of(
        produced, [&](const SymbolId& trait) { return produced_overlap(producer, consumer, trait).has_value(); });
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
                    const auto overlap = produced_overlap(producer, consumer, trait);
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
                if (right.contract.effects.contains(effect)) {
                    effect_provenance.push_back(effect);
                }
            }
            std::ranges::sort(effect_provenance);
            if (trait_provenance.empty() && effect_provenance.empty()) {
                continue;
            }

            const HandlerNode* before           = nullptr;
            const HandlerNode* after            = nullptr;
            ScheduleEdgeOrientation orientation = ScheduleEdgeOrientation::DeclarationOrder;
            if (explicitly_precedes(left.identity, right.identity)) {
                before      = &left;
                after       = &right;
                orientation = ScheduleEdgeOrientation::Explicit;
            } else if (explicitly_precedes(right.identity, left.identity)) {
                before      = &right;
                after       = &left;
                orientation = ScheduleEdgeOrientation::Explicit;
            } else if (const auto decision = public_group_order.decide(left_index, right_index)) {
                before      = decision->left_first ? &left : &right;
                after       = decision->left_first ? &right : &left;
                orientation = decision->orientation;
            } else if (left_writes_right != right_writes_left) {
                before      = left_writes_right ? &left : &right;
                after       = left_writes_right ? &right : &left;
                orientation = ScheduleEdgeOrientation::WriterBeforeReader;
            } else if (declaration_precedes(left, right)) {
                before = &left;
                after  = &right;
            } else {
                before = &right;
                after  = &left;
            }
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
        }
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

void expand_group_orderings(ExecutionGraph& graph) {
    std::vector<ScheduleEdge> expanded;
    for (const auto& ordering : graph.group_orderings) {
        for (const auto& member : graph.handlers) {
            if (member.group != ordering.group || member.identity == ordering.handler) {
                continue;
            }
            const bool before = ordering.direction == GroupOrderingDirection::Before;
            ScheduleEdge edge{.before      = before ? ordering.handler : member.identity,
                              .after       = before ? member.identity : ordering.handler,
                              .kind        = ScheduleEdgeKind::ExplicitGroup,
                              .orientation = ScheduleEdgeOrientation::Explicit};
            const auto duplicate = std::ranges::any_of(expanded, [&](const ScheduleEdge& existing) {
                return existing.before == edge.before && existing.after == edge.after;
            });
            if (!duplicate) {
                expanded.push_back(std::move(edge));
            }
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
