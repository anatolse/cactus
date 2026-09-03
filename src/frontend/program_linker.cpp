#include "frontend/program_linker.hpp"

#include "frontend/execution_graph_scheduler.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <functional>

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

// Phase-lineage cycle checking operates on string-keyed PhaseNode names with
// 2-state coloring, a different shape from the HandlerIdentity-keyed
// handler-cycle DFS inside compute_handler_schedule, so it stays a separate
// helper rather than folding into the shared core (see design.md's Non-Goals).
void check_phase_lineage_cycles(const std::vector<PhasePlan>& phases, ErrorReporter& errors) {
    std::unordered_map<SymbolId, std::vector<SymbolId>> phase_adjacency;
    for (const auto& phase : phases) {
        for (const auto& upstream : phase.completion_dependencies) {
            phase_adjacency[upstream].push_back(phase.phase);
        }
        for (const auto& source : phase.source_dependencies) {
            if (source.kind == HandlerTriggerKind::Phase) {
                phase_adjacency[source.symbol].push_back(phase.phase);
            }
        }
    }
    enum class Color : std::uint8_t { White, Gray, Black };
    std::unordered_map<SymbolId, Color> phase_color;
    for (const auto& phase : phases) {
        phase_color[phase.phase] = Color::White;
    }
    std::function<void(const SymbolId&)> phase_dfs = [&](const SymbolId& phase) {
        phase_color[phase] = Color::Gray;
        for (const auto& downstream : phase_adjacency[phase]) {
            if (!phase_color.contains(downstream)) {
                continue;
            }
            if (phase_color[downstream] == Color::Gray) {
                errors.error({}, "phase cycle: " + make_canonical_id(phase) + " -> " + make_canonical_id(downstream));
            } else if (phase_color[downstream] == Color::White) {
                phase_dfs(downstream);
            }
        }
        phase_color[phase] = Color::Black;
    };
    for (const auto& phase : phases) {
        if (phase_color[phase.phase] == Color::White) {
            phase_dfs(phase.phase);
        }
    }
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
    // dsl-render-passes: RenderPassPlan entries reference already
    // module-qualified SymbolId/HandlerIdentity values directly (no
    // declaration_order to patch), so a straight append is sufficient.
    target.execution_graph.render_passes.insert(target.execution_graph.render_passes.end(),
                                                src.execution_graph.render_passes.begin(),
                                                src.execution_graph.render_passes.end());
    for (const auto& edge : src.execution_graph.schedule_edges) {
        if (edge.kind != ScheduleEdgeKind::ExplicitHandler && edge.kind != ScheduleEdgeKind::ExplicitRule) {
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
        target.string_pool.intern(dep.rule_name);
    }

    if (ok) {
        merged_modules_.insert(src_module_name);
        target.linked_modules.push_back(src_module_name);
        ok = rebuild_execution_graph(target, false);
    }
    return ok;
}

bool ProgramLinker::rebuild_execution_graph(DecoratedProgram& program, bool validate_references) {
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

    const bool schedule_ok = compute_handler_schedule(graph, collect_external_events(program.events), errors_);
    check_phase_lineage_cycles(graph.phases, errors_);

    return schedule_ok && !errors_.has_errors();
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
