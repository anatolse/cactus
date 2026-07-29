#include "frontend/program_linker.hpp"

#include "frontend/module_artifact.hpp"
#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <functional>
#include <sstream>

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

ProgramLinker::ProgramLinker(ErrorReporter& errors)
    : errors_(errors) {}

// ── 5.2: Incremental merge ───────────────────────────────────────────────────

bool ProgramLinker::merge_into(DecoratedProgram& target,
                               const DecoratedProgram& src,
                               const std::string& src_module_name) {
    if (merged_modules_.contains(src_module_name)) {
        return true;
    }

    // Canonical key for conflict detection: prefer stored canonical_id; derive from
    // module + simple name when absent (e.g. for programs without explicit module declarations).
    auto canonical_key = [&src_module_name](const auto& decl) -> std::string {
        return decl.canonical_id.empty() ? make_canonical_id(src_module_name, decl.name) : decl.canonical_id;
    };

    // Map insertion key: prefer canonical_id so same-local-named declarations from
    // different modules (e.g. flat.WorldTransform vs volume.WorldTransform) coexist;
    // fall back to the source map key (simple name) when canonical_id is absent.
    auto insert_key = [](const auto& decl, const std::string& src_key) -> std::string {
        return decl.canonical_id.empty() ? src_key : decl.canonical_id;
    };

    bool ok                 = true;
    const auto module_index = static_cast<std::uint64_t>(merged_modules_.size());

    // ── Merge traits (task 4.4: conflict by canonical identity, not simple name) ──
    for (const auto& [name, trait] : src.traits) {
        if (trait.is_pub) {
            const std::string key = canonical_key(trait);
            auto it               = symbol_origins_.find(key);
            if (it != symbol_origins_.end()) {
                std::string msg = "duplicate canonical symbol '";
                msg += key;
                msg += "' defined in module '";
                msg += it->second;
                msg += "' and module '";
                msg += src_module_name;
                msg += "'";
                errors_.error({}, msg);
                ok = false;
                continue;
            }
            symbol_origins_[key] = src_module_name;
        }
        target.traits[insert_key(trait, name)] = trait;
    }

    // ── Merge structs ────────────────────────────────────────────────────────
    for (const auto& [name, strct] : src.structs) {
        const std::string key = canonical_key(strct);
        auto it               = symbol_origins_.find(key);
        if (it != symbol_origins_.end()) {
            std::string msg = "duplicate canonical symbol '";
            msg += key;
            msg += "' defined in module '";
            msg += it->second;
            msg += "' and module '";
            msg += src_module_name;
            msg += "'";
            errors_.error({}, msg);
            ok = false;
            continue;
        }
        symbol_origins_[key]                    = src_module_name;
        target.structs[insert_key(strct, name)] = strct;
    }

    // ── Merge enums ──────────────────────────────────────────────────────────
    for (const auto& [name, enm] : src.enums) {
        const std::string key = canonical_key(enm);
        auto it               = symbol_origins_.find(key);
        if (it != symbol_origins_.end()) {
            std::string msg = "duplicate canonical symbol '";
            msg += key;
            msg += "' defined in module '";
            msg += it->second;
            msg += "' and module '";
            msg += src_module_name;
            msg += "'";
            errors_.error({}, msg);
            ok = false;
            continue;
        }
        symbol_origins_[key]                = src_module_name;
        target.enums[insert_key(enm, name)] = enm;
    }

    for (const auto& [name, func] : src.funcs) {
        target.funcs[insert_key(func, name)] = func;
    }
    for (const auto& [name, event] : src.events) {
        target.events[insert_key(event, name)] = event;
    }
    for (const auto& [name, phase] : src.phases) {
        target.phases[insert_key(phase, name)] = phase;
    }
    target.pub_templates.insert(src.pub_templates.begin(), src.pub_templates.end());
    target.non_pub_templates.insert(src.non_pub_templates.begin(), src.non_pub_templates.end());
    target.pub_events.insert(src.pub_events.begin(), src.pub_events.end());

    // ── 5.4 + 5.2: Merge dependency graph (append) ──────────────────────────
    for (const auto& dep : src.dependency_graph) {
        target.dependency_graph.push_back(dep);
    }
    target.handler_contracts.insert(
        target.handler_contracts.end(), src.handler_contracts.begin(), src.handler_contracts.end());
    for (auto phase : src.execution_graph.phases) {
        phase.declaration_order.module_index = module_index;
        target.execution_graph.phases.push_back(std::move(phase));
    }
    for (auto handler : src.execution_graph.handlers) {
        handler.declaration_order.module_index = module_index;
        target.execution_graph.handlers.push_back(std::move(handler));
    }
    for (const auto& edge : src.execution_graph.schedule_edges) {
        if (edge.kind != ScheduleEdgeKind::ExplicitHandler && edge.kind != ScheduleEdgeKind::ExplicitSystem) {
            continue;
        }
        if (std::ranges::none_of(linked_explicit_edges_, [&](const ScheduleEdge& existing) {
                return existing.before == edge.before && existing.after == edge.after && existing.kind == edge.kind;
            })) {
            linked_explicit_edges_.push_back(edge);
        }
    }

    // ── 5.5: Merge string pool ────────────────────────────────────────────────
    // StringPool has no iteration API, so merging is done at the source level.
    // The linker integrates interned string names from declaration names instead.
    // Intern all known symbol names into the target pool as a practical merge.
    for (const auto& [name, _] : src.traits) {
        target.string_pool.intern(name);
    }
    for (const auto& [name, _] : src.structs) {
        target.string_pool.intern(name);
    }
    for (const auto& [name, _] : src.enums) {
        target.string_pool.intern(name);
    }
    for (const auto& dep : src.dependency_graph) {
        target.string_pool.intern(dep.system_name);
    }

    if (ok) {
        merged_modules_.insert(src_module_name);
        ok = rebuild_execution_graph(target, false);
    }
    return ok;
}

bool ProgramLinker::rebuild_execution_graph(
    DecoratedProgram& program,
    bool validate_references) {  // NOLINT(readability-function-cognitive-complexity)
    auto& graph = program.execution_graph;
    graph.schedule_edges.clear();
    graph.phase_barriers.clear();
    graph.event_flows.clear();
    graph.stable_topological_order.clear();
    graph.dependency_levels.clear();

    std::ranges::sort(graph.phases, [](const PhasePlan& left, const PhasePlan& right) {
        if (left.declaration_order.module_index != right.declaration_order.module_index) {
            return left.declaration_order.module_index < right.declaration_order.module_index;
        }
        return left.declaration_order.declaration_index < right.declaration_order.declaration_index;
    });
    std::ranges::sort(graph.handlers, declaration_precedes);

    std::unordered_map<HandlerIdentity, const HandlerNode*, HandlerIdentityHash> nodes;
    for (const auto& handler : graph.handlers) {
        if (!nodes.emplace(handler.identity, &handler).second) {
            errors_.error(handler.location, "duplicate linked handler '" + handler.identity.canonical_id() + "'");
        }
    }

    const auto edge_exists = [&](const ScheduleEdge& candidate) {
        return std::ranges::any_of(graph.schedule_edges, [&](const ScheduleEdge& edge) {
            return edge.before == candidate.before && edge.after == candidate.after && edge.kind == candidate.kind;
        });
    };
    for (const auto& edge : linked_explicit_edges_) {
        if (!nodes.contains(edge.after)) {
            continue;
        }
        if (!nodes.contains(edge.before)) {
            if (validate_references && edge.kind == ScheduleEdgeKind::ExplicitHandler) {
                const auto& dependent = *nodes.at(edge.after);
                errors_.error(dependent.location,
                              "unknown linked handler '" + edge.before.canonical_id() + "' required by '" +
                                  edge.after.canonical_id() + "'");
            }
            continue;
        }
        if (!edge_exists(edge)) {
            graph.schedule_edges.push_back(edge);
        }
    }

    std::unordered_map<HandlerIdentity, std::vector<HandlerIdentity>, HandlerIdentityHash> explicit_adjacency;
    for (const auto& edge : graph.schedule_edges) {
        explicit_adjacency[edge.before].push_back(edge.after);
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
            for (const auto& trait : left.contract.writes) {
                if (right.contract.reads.contains(trait)) {
                    left_writes_right = true;
                    add_trait(trait);
                }
            }
            for (const auto& trait : right.contract.writes) {
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
    for (const auto& producer : graph.handlers) {
        std::vector<SymbolId> emitted(producer.contract.emits.begin(), producer.contract.emits.end());
        std::ranges::sort(
            emitted, [](const SymbolId& a, const SymbolId& b) { return make_canonical_id(a) < make_canonical_id(b); });
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

    enum class Color : std::uint8_t { White, Gray, Black };
    std::unordered_map<HandlerIdentity, Color, HandlerIdentityHash> color;
    std::unordered_map<HandlerIdentity, std::vector<HandlerIdentity>, HandlerIdentityHash> adjacency;
    for (const auto& [identity, _] : nodes) {
        color[identity] = Color::White;
    }
    for (const auto& edge : graph.schedule_edges) {
        if (nodes.contains(edge.before) && nodes.contains(edge.after)) {
            adjacency[edge.before].push_back(edge.after);
        }
    }
    std::vector<HandlerIdentity> path;
    std::function<void(const HandlerIdentity&)> dfs = [&](const HandlerIdentity& node) {
        color[node] = Color::Gray;
        path.push_back(node);
        for (const auto& neighbor : adjacency[node]) {
            if (color[neighbor] == Color::Gray) {
                const auto start = std::ranges::find(path, neighbor);
                std::ostringstream cycle;
                for (auto it = start; it != path.end(); ++it) {
                    if (it != start) {
                        cycle << " -> ";
                    }
                    cycle << it->canonical_id();
                }
                cycle << " -> " << neighbor.canonical_id();
                errors_.error({}, "handler cycle: " + cycle.str());
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

    std::unordered_map<SymbolId, std::vector<SymbolId>> phase_adjacency;
    for (const auto& phase : graph.phases) {
        for (const auto& upstream : phase.completion_dependencies) {
            phase_adjacency[upstream].push_back(phase.phase);
        }
        for (const auto& source : phase.source_dependencies) {
            if (source.kind == HandlerTriggerKind::Phase) {
                phase_adjacency[source.symbol].push_back(phase.phase);
            }
        }
    }
    std::unordered_map<SymbolId, Color> phase_color;
    for (const auto& phase : graph.phases) {
        phase_color[phase.phase] = Color::White;
    }
    std::function<void(const SymbolId&)> phase_dfs = [&](const SymbolId& phase) {
        phase_color[phase] = Color::Gray;
        for (const auto& downstream : phase_adjacency[phase]) {
            if (!phase_color.contains(downstream)) {
                continue;
            }
            if (phase_color[downstream] == Color::Gray) {
                errors_.error({}, "phase cycle: " + make_canonical_id(phase) + " -> " + make_canonical_id(downstream));
            } else if (phase_color[downstream] == Color::White) {
                phase_dfs(downstream);
            }
        }
        phase_color[phase] = Color::Black;
    };
    for (const auto& phase : graph.phases) {
        if (phase_color[phase.phase] == Color::White) {
            phase_dfs(phase.phase);
        }
    }

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
                break;
            }
            graph.dependency_levels.push_back(
                DependencyLevel{.activation = activation, .index = level_index++, .handlers = ready});
            graph.stable_topological_order.insert(graph.stable_topological_order.end(), ready.begin(), ready.end());
            for (const auto& identity : ready) {
                emitted.insert(identity);
                for (const auto& dependent : local_adjacency[identity]) {
                    --indegree[dependent];
                }
            }
        }
    }
    return !errors_.has_errors();
}

// ── 5.1: Link from artifact files ───────────────────────────────────────────

std::optional<DecoratedProgram> ProgramLinker::link(const std::vector<std::filesystem::path>& artifact_paths) {
    DecoratedProgram merged;
    merged.ast = nullptr;  // AST is not preserved in artifacts

    for (const auto& path : artifact_paths) {
        ErrorReporter artifact_errors;
        ModuleArtifact artifact(artifact_errors);

        std::string module_name;
        auto prog = artifact.load(path, module_name);

        if (!prog) {
            // Forward artifact load errors
            for (const auto& d : artifact_errors.diagnostics()) {
                errors_.error(d.location, d.message);
            }
            return std::nullopt;
        }

        if (!merge_into(merged, *prog, module_name)) {
            // merge_into already reported the duplicate error
            return std::nullopt;
        }
    }

    if (!rebuild_execution_graph(merged, true)) {
        return std::nullopt;
    }

    if (errors_.has_errors()) {
        return std::nullopt;
    }
    return merged;
}

}  // namespace cactus
