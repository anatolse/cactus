#include "frontend/execution_graph_scheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
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

}  // namespace

bool compute_handler_schedule(ExecutionGraph& graph, ErrorReporter& errors) {  // NOLINT(readability-function-cognitive-complexity) -- the single consolidated home for what used to be two independently-duplicated ~250-line scheduling algorithms; see design.md D1-D5
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
        if ((edge.kind == ScheduleEdgeKind::ExplicitHandler || edge.kind == ScheduleEdgeKind::ExplicitRule) &&
            nodes.contains(edge.before) && nodes.contains(edge.after)) {
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

    for (std::size_t left_index = 0; left_index < graph.handlers.size(); ++left_index) {
        const auto& left = graph.handlers[left_index];
        for (std::size_t right_index = left_index + 1; right_index < graph.handlers.size(); ++right_index) {
            const auto& right = graph.handlers[right_index];
            if (left.identity.trigger != right.identity.trigger) {
                continue;
            }

            std::vector<SymbolId> trait_provenance;
            bool left_writes_right = false;
            bool right_writes_left = false;
            const auto add_trait   = [&](const SymbolId& trait) {
                if (std::ranges::find(trait_provenance, trait) == trait_provenance.end()) {
                    trait_provenance.push_back(trait);
                }
            };
            for (const auto& trait : produced_by_handler[left_index]) {
                if (right.contract.reads.contains(trait)) {
                    left_writes_right = true;
                    add_trait(trait);
                }
            }
            for (const auto& trait : produced_by_handler[right_index]) {
                if (left.contract.reads.contains(trait)) {
                    right_writes_left = true;
                    add_trait(trait);
                }
            }
            std::ranges::sort(trait_provenance, [](const SymbolId& a, const SymbolId& b) {
                return make_canonical_id(a) < make_canonical_id(b);
            });

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
                                                             .trait_provenance = std::move(trait_provenance)});
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

}  // namespace cactus
