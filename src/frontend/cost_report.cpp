#include "frontend/cost_report.hpp"

#include "common/joined.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/delivery_cycles.hpp"
#include "frontend/execution_graph_scheduler.hpp"
#include "frontend/keep_inputs.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cactus {

namespace {

constexpr std::string_view kDot   = "·";
constexpr std::string_view kDelta = "Δ";
constexpr std::string_view kTimes = "×";

constexpr std::string_view kLegend =
    "Cost of each activation as terms in entity populations: a shape, not a bound.\n"
    "  N(T)        entities that carry T\n"
    "  N(*)        all entities, for a rule that requires no trait\n"
    "  Δ(T)        entities of T whose inputs changed since the rule's last run\n"
    "  k           matches per row of a join, not measured\n"
    "  cells(T)    cells of each carrier of T\n"
    "  live(T: f)  cells of T whose fields f are not at their defaults\n"
    "  ×N          the passes of a rule with `repeat: N`\n"
    "A keep rule is skipped when none of its inputs changed.\n";

// A product of factors; a factor of 1 drops out.
std::string product(std::initializer_list<std::string> factors) {
    std::string text;
    for (const auto& factor : factors) {
        if (factor.empty() || factor == "1") {
            continue;
        }
        text += (text.empty() ? "" : std::string(kDot)) + factor;
    }
    return text.empty() ? "1" : text;
}

std::string grouped(const std::string& sum) {
    return sum.contains(" + ") ? "(" + sum + ")" : sum;
}

std::vector<std::string> sorted_unique(std::vector<std::string> texts) {
    std::ranges::sort(texts);
    const auto [first, last] = std::ranges::unique(texts);
    texts.erase(first, last);
    return texts;
}

void sort_by_canonical_id(std::vector<const HandlerNode*>& handlers) {
    std::ranges::sort(handlers, {}, [](const HandlerNode* handler) { return handler->identity.canonical_id(); });
}

std::string_view stage_text(ClauseStage stage) {
    switch (stage) {
        case ClauseStage::Row:
            return "per row";
        case ClauseStage::Activation:
            return "once per activation";
        case ClauseStage::BindingLeft:
            return "once per row of the first binding";
        case ClauseStage::BindingRight:
            return "once per row of the second binding";
    }
    std::unreachable();
}

std::string_view plan_text(PairPlanKind kind) {
    switch (kind) {
        case PairPlanKind::Product:
            return "every tuple";
        case PairPlanKind::BoundedProduct:
            return "every tuple, one side named entities";
        case PairPlanKind::Spatial:
            return "spatial join";
        case PairPlanKind::Equality:
            return "equality join";
        case PairPlanKind::RelationRows:
            return "relation rows";
        case PairPlanKind::Union:
            return "union of joins";
        case PairPlanKind::GridRegion:
            return "grid region";
    }
    std::unreachable();
}

std::string_view orientation_text(ScheduleEdgeOrientation orientation) {
    switch (orientation) {
        case ScheduleEdgeOrientation::Explicit:
            return "ordered by `after:`";
        case ScheduleEdgeOrientation::WriterBeforeReader:
            return "writer before reader";
        case ScheduleEdgeOrientation::ImporterBeforePublicGroup:
            return "importer before the public group";
        case ScheduleEdgeOrientation::DerivationAfterWriters:
            return "derived relation after its writers";
    }
    std::unreachable();
}

struct Root {
    std::string title;
    std::uint32_t depth = 0;
    std::vector<const HandlerNode*> handlers;  // schedule order
    std::vector<std::vector<const HandlerNode*>> cycles;
};

class CostModel {
public:
    explicit CostModel(const DecoratedProgram& program)
        : graph_(&program.execution_graph),
          facts_(&program.reachability),
          rules_(rules_by_id(program.ast)) {
        for (const auto& [_, trait] : program.traits) {
            names_[trait.name].insert(trait.canonical_id);
        }
        for (const auto& [_, relation] : program.relations) {
            names_[relation.name].insert(relation.canonical_id);
            relations_.emplace(relation.canonical_id, &relation);
        }
        for (const auto& handler : graph_->handlers) {
            nodes_.emplace(handler.identity, &handler);
        }
        for (const auto& edge : graph_->event_flows) {
            consumers_[edge.producer].push_back(edge.consumer);
        }
        for (const auto& edge : graph_->lifecycle_flows) {
            consumers_[edge.producer].push_back(edge.consumer);
        }
    }

    [[nodiscard]] std::string report() const {
        std::string text{kLegend};
        for (const auto& root : roots()) {
            text += "\n" + root.title + "  depth " + std::to_string(root.depth) + "\n";
            for (const auto* handler : root.handlers) {
                text += "  " + handler->identity.canonical_id() + "  " + cost(*handler) + "\n";
            }
            for (const auto& cycle : root.cycles) {
                std::vector<std::string> ids;
                for (const auto* handler : cycle) {
                    ids.push_back(handler->identity.canonical_id());
                }
                text += "  delayed cycle: " + joined(ids, ", ") + "\n";
            }
        }
        return text;
    }

    [[nodiscard]] std::expected<std::string, std::string> explain(std::string_view rule) const {
        std::vector<const HandlerNode*> handlers;
        for (const auto& handler : graph_->handlers) {
            if (make_canonical_id(handler.identity.rule) == rule) {
                handlers.push_back(&handler);
            }
        }
        if (handlers.empty()) {
            return std::unexpected("unknown rule '" + std::string(rule) + "'");
        }
        sort_by_canonical_id(handlers);
        std::string text;
        for (const auto* handler : handlers) {
            text += (text.empty() ? "" : "\n") + explanation(*handler);
        }
        return text;
    }

private:
    // ── Names and populations ───────────────────────────────────────────────

    // The local name when no other trait or relation shares it.
    [[nodiscard]] std::string name(const SymbolId& symbol) const {
        const auto found = names_.find(symbol.local_name);
        return found != names_.end() && found->second.size() > 1 ? make_canonical_id(symbol) : symbol.local_name;
    }

    [[nodiscard]] static std::string count(bool changed, const std::string& of) {
        return std::string(changed ? kDelta : "N") + "(" + of + ")";
    }

    // Named-only carriers are a constant; otherwise the first required trait drives the pass.
    [[nodiscard]] std::string population(std::span<const SymbolId> required, bool changed) const {
        if (const auto bound = named_bound(*facts_, required)) {
            return std::to_string(*bound);
        }
        if (required.empty()) {
            return count(changed, "*");
        }
        const auto& driver = required.front();
        if (changed || !std::ranges::contains(facts_->never_destroyed_traits, driver)) {
            return count(changed, name(driver));
        }
        return count(false, name(driver) + ", never destroyed");
    }

    [[nodiscard]] std::string population(const RelationBinding& binding, bool changed) const {
        return binding.relation.has_value() ? count(changed, name(*binding.relation))
                                            : population(binding.required_traits, changed);
    }

    // ── Cost terms ──────────────────────────────────────────────────────────

    [[nodiscard]] const RuleNode* rule_of(const SymbolId& rule) const {
        const auto found = rules_.find(rule);
        return found == rules_.end() ? nullptr : found->second;
    }

    [[nodiscard]] std::optional<KeepEvaluation> keep_of(const HandlerNode& handler, const RuleNode* rule) const {
        if (!handler.kept.has_value() || handler.implementation != HandlerImplementationKind::Cactus || rule == nullptr) {
            return std::nullopt;
        }
        return classify_keep(*rule, handler.contract, [this](const SymbolId& relation) -> const ResolvedRelation* {
            const auto found = relations_.find(make_canonical_id(relation));
            return found == relations_.end() ? nullptr : found->second;
        });
    }

    [[nodiscard]] std::string cell_rows(const HandlerContract& contract, const CellDomainNode& cells) const {
        const auto carriers = population(contract.selection, false);
        const auto trait    = name(cells.trait);
        const auto visited  = cells.keeps_rest ? "live(" + trait + ": " + joined(cells.rest_fields, ", ") + ")"
                                               : "cells(" + trait + ")";
        return product({carriers, visited, cells.aliases.size() == 2 ? "k" : ""});
    }

    [[nodiscard]] std::string pair_rows(const HandlerContract& contract, bool delta) const {
        const auto& bindings = contract.pair_bindings;
        if (bindings.size() != 2) {
            return "1";
        }
        const auto left  = population(bindings[0], false);
        const auto right = population(bindings[1], false);
        if (delta) {
            std::vector<std::string> changed;
            for (const auto& binding : bindings) {
                if (named_bound(*facts_, binding).has_value()) {
                    continue;
                }
                changed.push_back(population(binding, true));
            }
            if (!changed.empty()) {
                return product({grouped(joined(changed, " + ")), "k"});
            }
        }
        switch (contract.pair_plan.kind) {
            case PairPlanKind::Product:
            case PairPlanKind::BoundedProduct:
                return product({left, right});
            case PairPlanKind::Equality: {
                const bool left_indexed =
                    contract.equality_join.has_value() && contract.equality_join->indexed_binding == 0;
                return product({left_indexed ? right : left, "k"});
            }
            case PairPlanKind::Spatial:
            case PairPlanKind::RelationRows:
            case PairPlanKind::Union:
            case PairPlanKind::GridRegion:
                return product({left, "k"});
        }
        std::unreachable();
    }

    [[nodiscard]] std::string rows(const HandlerNode& handler, const RuleNode* rule, bool delta) const {
        const auto& contract = handler.contract;
        if (handler.identity.trigger.is_lifecycle()) {
            return "1";
        }
        switch (contract.domain_kind) {
            case HandlerDomainKind::Selectionless:
                return "1";
            case HandlerDomainKind::Rows:
                return contract.pair_bindings.empty() ? "1" : population(contract.pair_bindings.front(), delta);
            case HandlerDomainKind::Unary:
                if (rule != nullptr && rule->cells.has_value()) {
                    return cell_rows(contract, *rule->cells);
                }
                return population(contract.selection, delta);
            case HandlerDomainKind::Pair:
                return pair_rows(contract, delta);
        }
        std::unreachable();
    }

    // The world scans one row runs: scans in sequence add, a scan inside a loop over another multiplies.
    [[nodiscard]] std::string scan_work(const HandlerContract& contract,  // NOLINT(misc-no-recursion) -- loop nesting
                                        std::optional<std::size_t> enclosing) const {
        std::vector<std::string> parts;
        for (std::size_t index = 0; index < contract.world_scans.size(); ++index) {
            const auto& scan = contract.world_scans[index];
            if (scan.enclosing != enclosing) {
                continue;
            }
            parts.push_back(product({population(scan.filter, false), grouped(scan_work(contract, index))}));
        }
        return joined(parts, " + ");
    }

    [[nodiscard]] std::string cost(const HandlerNode& handler) const {
        const auto* rule  = rule_of(handler.identity.rule);
        const auto keep   = keep_of(handler, rule);
        const bool delta  = keep.has_value() && keep->level == KeepLevel::Delta;
        const auto& trigger = handler.identity.trigger;
        auto text = product({rows(handler, rule, delta), grouped(scan_work(handler.contract, std::nullopt))});
        if (rule != nullptr && rule->repeat.has_value()) {
            text += " " + std::string(kTimes) + std::to_string(rule->repeat->passes);
        }
        if (trigger.kind == HandlerTriggerKind::Event) {
            text = "per event: " + text;
        } else if (trigger.is_lifecycle()) {
            text = "per delivery: " + text;
        }
        if (keep.has_value()) {
            text += keep->level == KeepLevel::Delta ? "  keep delta" : "  keep full: " + keep->reason;
        }
        return text;
    }

    // ── Roots ───────────────────────────────────────────────────────────────

    [[nodiscard]] bool reachable(const HandlerNode& handler) const {
        return !std::ranges::contains(facts_->unreachable_handlers, handler.identity);
    }

    [[nodiscard]] std::vector<const HandlerNode*> starting(const ActivationDepth& depth) const {
        std::vector<const HandlerNode*> found;
        for (const auto& handler : graph_->handlers) {
            const auto& trigger = handler.identity.trigger;
            const bool starts =
                depth.kind == ActivationRootKind::StartupArrival
                    ? trigger.kind == HandlerTriggerKind::TraitAdded
                    : trigger.symbol == depth.symbol &&
                          (depth.kind == ActivationRootKind::Event
                               ? trigger.kind == HandlerTriggerKind::Event
                               : trigger.is_phase_or_render_stage());
            if (starts) {
                found.push_back(&handler);
            }
        }
        return found;
    }

    // Every handler a run of `start` can deliver to, over immediate and delayed edges.
    [[nodiscard]] std::vector<const HandlerNode*> reached(std::vector<const HandlerNode*> pending) const {
        std::unordered_set<HandlerIdentity, HandlerIdentityHash> seen;
        std::vector<const HandlerNode*> found;
        for (const auto* handler : pending) {
            seen.insert(handler->identity);
        }
        while (!pending.empty()) {
            const auto* handler = pending.back();
            pending.pop_back();
            if (reachable(*handler)) {
                found.push_back(handler);
            }
            const auto consumers = consumers_.find(handler->identity);
            if (consumers == consumers_.end()) {
                continue;
            }
            for (const auto& consumer : consumers->second) {
                if (seen.insert(consumer).second) {
                    pending.push_back(nodes_.at(consumer));
                }
            }
        }
        return in_schedule_order(found);
    }

    // Schedule edges and immediate deliveries order the handlers; canonical ids break ties,
    // so declaration order never shows. A handler left on a cycle follows in canonical order.
    [[nodiscard]] std::vector<const HandlerNode*> in_schedule_order(std::vector<const HandlerNode*> handlers) const {
        sort_by_canonical_id(handlers);
        std::unordered_map<HandlerIdentity, std::size_t, HandlerIdentityHash> index;
        for (std::size_t position = 0; position < handlers.size(); ++position) {
            index.emplace(handlers[position]->identity, position);
        }
        std::vector<std::vector<std::size_t>> later(handlers.size());
        std::vector<std::size_t> waiting(handlers.size(), 0);
        const auto order = [&](const HandlerIdentity& before, const HandlerIdentity& after) {
            const auto from = index.find(before);
            const auto to   = index.find(after);
            if (from != index.end() && to != index.end() && from->second != to->second) {
                later[from->second].push_back(to->second);
                ++waiting[to->second];
            }
        };
        for (const auto& edge : graph_->schedule_edges) {
            order(edge.before, edge.after);
        }
        for (const auto& edge : graph_->event_flows) {
            if (!edge.delayed) {
                order(edge.producer, edge.consumer);
            }
        }
        for (const auto& edge : graph_->lifecycle_flows) {
            if (!edge.delayed) {
                order(edge.producer, edge.consumer);
            }
        }
        std::set<std::size_t> ready;
        for (std::size_t position = 0; position < handlers.size(); ++position) {
            if (waiting[position] == 0) {
                ready.insert(position);
            }
        }
        std::vector<const HandlerNode*> ordered;
        std::vector<bool> placed(handlers.size(), false);
        while (ordered.size() < handlers.size()) {
            if (ready.empty()) {
                ready.insert(static_cast<std::size_t>(std::ranges::find(placed, false) - placed.begin()));
            }
            const auto next = *ready.begin();
            ready.erase(ready.begin());
            if (placed[next]) {
                continue;
            }
            placed[next] = true;
            ordered.push_back(handlers[next]);
            for (const auto successor : later[next]) {
                if (--waiting[successor] == 0 && !placed[successor]) {
                    ready.insert(successor);
                }
            }
        }
        return ordered;
    }

    [[nodiscard]] static std::string title(const ActivationDepth& depth) {
        switch (depth.kind) {
            case ActivationRootKind::Phase:
                return "phase " + make_canonical_id(*depth.symbol);
            case ActivationRootKind::Event:
                return "event " + make_canonical_id(*depth.symbol);
            case ActivationRootKind::StartupArrival:
                return "startup arrival";
        }
        std::unreachable();
    }

    // An event a handler emits starts no activation of its own.
    [[nodiscard]] bool emitted(const ActivationDepth& depth) const {
        return depth.kind == ActivationRootKind::Event &&
               std::ranges::any_of(graph_->event_flows,
                                   [&](const EventFlowEdge& edge) { return edge.event == depth.symbol; });
    }

    [[nodiscard]] std::vector<Root> roots() const {
        const auto components = delivery_components(*graph_);
        std::vector<Root> found;
        for (const auto& depth : graph_->activation_depths) {
            if (emitted(depth)) {
                continue;
            }
            Root root{.title = title(depth), .depth = depth.depth, .handlers = reached(starting(depth))};
            if (root.handlers.empty()) {
                continue;
            }
            std::map<std::uint32_t, std::vector<const HandlerNode*>> cycles;
            for (const auto* handler : root.handlers) {
                if (const auto component = components.find(handler->identity); component != components.end()) {
                    cycles[component->second].push_back(handler);
                }
            }
            for (auto& [_, cycle] : cycles) {
                sort_by_canonical_id(cycle);
                root.cycles.push_back(std::move(cycle));
            }
            found.push_back(std::move(root));
        }
        return found;
    }

    // ── Explanations ────────────────────────────────────────────────────────

    [[nodiscard]] std::string domain(const HandlerNode& handler, const RuleNode* rule) const {
        const auto& contract = handler.contract;
        const auto names     = [this](const std::vector<SymbolId>& traits) {
            std::vector<std::string> spelled;
            spelled.reserve(traits.size());
            for (const auto& trait : traits) {
                spelled.push_back(name(trait));
            }
            return joined(spelled, ", ");
        };
        switch (contract.domain_kind) {
            case HandlerDomainKind::Selectionless:
                return "none";
            case HandlerDomainKind::Rows:
                return contract.pair_bindings.empty() || !contract.pair_bindings.front().relation.has_value()
                           ? "rows"
                           : "rows of " + name(*contract.pair_bindings.front().relation);
            case HandlerDomainKind::Unary: {
                auto text = rule != nullptr && rule->cells.has_value() ? "cells of " + name(rule->cells->trait) + ", "
                                                                       : std::string{};
                text += "each " + names(contract.selection);
                return contract.exclusion.empty() ? text : text + " without " + names(contract.exclusion);
            }
            case HandlerDomainKind::Pair: {
                std::vector<std::string> bindings;
                for (const auto& binding : contract.pair_bindings) {
                    const auto members = binding.relation.has_value() ? "rows of " + name(*binding.relation)
                                                                      : names(binding.required_traits);
                    bindings.push_back(binding.name + ": " + members +
                                       (binding.excluded_traits.empty() ? "" : " without " + names(binding.excluded_traits)));
                }
                return "pairs " + joined(bindings, "; ") + " (" + std::string(plan_text(contract.pair_plan.kind)) + ")";
            }
        }
        std::unreachable();
    }

    [[nodiscard]] std::string source_line(const SourceLocation& location) const {
        auto& lines = sources_[location.filename];
        if (lines.empty()) {
            std::ifstream stream(location.filename);
            for (std::string line; std::getline(stream, line);) {
                lines.push_back(line);
            }
        }
        if (location.line < 1 || static_cast<std::size_t>(location.line) > lines.size()) {
            return location.filename + ":" + std::to_string(location.line);
        }
        auto line = lines[static_cast<std::size_t>(location.line) - 1];
        const auto first = line.find_first_not_of(" \t");
        const auto last  = line.find_last_not_of(" \t\r");
        return first == std::string::npos ? line : line.substr(first, last - first + 1);
    }

    [[nodiscard]] std::string conditions(const HandlerNode& handler, const RuleNode* rule) const {
        if (rule == nullptr) {
            return {};
        }
        std::string text;
        if (rule->where_clause.has_value()) {
            const auto& predicates = rule->where_clause->predicates;
            for (std::size_t index = 0; index < predicates.size(); ++index) {
                const auto stage = stage_at(handler.contract.staging.where, index);
                text += "  where: " + source_line(predicates[index]->location) + "  (run-time, " +
                        std::string(stage_text(stage)) + ")\n";
            }
        }
        if (rule->when_clause.has_value()) {
            for (const auto& predicate : rule->when_clause->predicates) {
                text += "  when: " + source_line(predicate->location) + "  (run-time, " +
                        std::string(stage_text(ClauseStage::Activation)) + ")\n";
            }
        }
        return text;
    }

    [[nodiscard]] static std::vector<std::string> accesses(const std::unordered_set<SymbolId>& traits,
                                                           const std::unordered_map<SymbolId, FieldAccess>& fields,
                                                           const auto& label) {
        std::vector<std::string> listed;
        for (const auto& trait : traits) {
            const auto canonical = make_canonical_id(trait);
            const auto& access   = field_access(fields, trait);
            if (trait.kind == SymbolKind::Relation || access.all) {
                listed.push_back(canonical + label(trait, std::nullopt));
                continue;
            }
            for (const auto& field : access.fields) {
                auto entry = canonical;
                entry += ".";
                entry += field;
                entry += label(trait, field);
                listed.push_back(std::move(entry));
            }
        }
        std::ranges::sort(listed);
        return listed;
    }

    [[nodiscard]] static std::vector<std::string> sorted_texts(const auto& items, const auto& text) {
        std::vector<std::string> texts;
        texts.reserve(items.size());
        for (const auto& item : items) {
            texts.push_back(text(item));
        }
        return sorted_unique(std::move(texts));
    }

    [[nodiscard]] static std::string contract_lines(const HandlerContract& contract) {
        const auto plain   = [](const SymbolId&, const std::optional<std::string>&) { return std::string{}; };
        const auto written = [&](const SymbolId& trait, const std::optional<std::string>& field) {
            return field.has_value() ? " (" + write_kind_name(write_kind(contract, trait, *field)) + ")"
                                     : std::string(" (all fields)");
        };
        std::string text;
        const auto line = [&](std::string_view label, const std::vector<std::string>& items) {
            if (!items.empty()) {
                text += "  " + std::string(label) + ": " + joined(items, ", ") + "\n";
            }
        };
        line("reads", accesses(contract.reads, contract.read_fields, plain));
        line("writes", accesses(contract.writes, contract.write_fields, written));
        const auto command_text = [](const InferredHandlerCommand& command) {
            return std::string(handler_command_kind_name(command.kind)) +
                   (command.target.has_value() ? " " + make_canonical_id(*command.target) : "");
        };
        const auto symbol_text = [](const SymbolId& symbol) { return make_canonical_id(symbol); };
        const auto effect_text = [](const std::string& effect) { return effect; };
        line("projects", sorted_texts(contract.projects, symbol_text));
        line("emits", sorted_texts(contract.emits, symbol_text));
        line("commands", sorted_texts(contract.commands, command_text));
        line("effects", sorted_texts(contract.effects, effect_text));
        return text;
    }

    // Who declared an explicit edge: the later rule's `after:` or the earlier rule's `before:`.
    [[nodiscard]] std::string declared(const ScheduleEdge& edge) const {
        if (edge.kind == ScheduleEdgeKind::ExplicitGroup) {
            return "declared by group order";
        }
        const auto* later = rule_of(edge.after.rule);
        if (later != nullptr && std::ranges::contains(later->resolved_after_rule_ids, edge.before.rule)) {
            return "declared by `after:`";
        }
        const auto* earlier = rule_of(edge.before.rule);
        if (earlier != nullptr && std::ranges::contains(earlier->resolved_ordering.before_rules, edge.after.rule)) {
            return "declared by `before:`";
        }
        return "declared by `after:` or `before:`";
    }

    [[nodiscard]] std::string reason(const ScheduleEdge& edge) const {
        switch (edge.kind) {
            case ScheduleEdgeKind::ExplicitHandler:
            case ScheduleEdgeKind::ExplicitRule:
            case ScheduleEdgeKind::ExplicitGroup:
                return declared(edge);
            case ScheduleEdgeKind::DataConflict:
            case ScheduleEdgeKind::EffectConflict:
            case ScheduleEdgeKind::ChannelConflict:
                break;
        }
        const auto reasons = conflict_reasons(*nodes_.at(edge.before),
                                              *nodes_.at(edge.after),
                                              edge.field_provenance,
                                              edge.channel_provenance,
                                              edge.effect_provenance);
        return joined(reasons, ", ") + "; " + std::string(orientation_text(edge.orientation));
    }

    [[nodiscard]] std::string edges(const HandlerNode& handler) const {
        std::vector<std::string> lines;
        for (const auto& edge : graph_->schedule_edges) {
            const bool is_after = edge.after == handler.identity;
            if ((!is_after && edge.before != handler.identity) || !nodes_.contains(edge.before) ||
                !nodes_.contains(edge.after)) {
                continue;
            }
            const auto& other = is_after ? edge.before : edge.after;
            lines.push_back("  " + std::string(is_after ? "after " : "before ") + other.canonical_id() + ": " +
                            reason(edge) + "\n");
        }
        return joined(sorted_unique(std::move(lines)), "");
    }

    [[nodiscard]] std::string explanation(const HandlerNode& handler) const {
        const auto* rule      = rule_of(handler.identity.rule);
        const auto& location  = handler.location;
        std::string text      = handler.identity.canonical_id() + "\n";
        text += "  at " + location.filename + ":" + std::to_string(location.line) + ":" +
                std::to_string(location.column) + "\n";
        text += "  domain: " + domain(handler, rule) + "\n";
        text += "  activation: " + handler.identity.trigger.debug_string() + "\n";
        if (!reachable(handler)) {
            text += "  never runs: no reachable entity or producer starts it\n";
        }
        text += conditions(handler, rule);
        text += contract_lines(handler.contract);
        text += edges(handler);
        text += "  cost: " + cost(handler) + "\n";
        return text;
    }

    const ExecutionGraph* graph_;
    const ReachabilityFacts* facts_;
    std::unordered_map<SymbolId, const RuleNode*> rules_;
    std::unordered_map<std::string, const ResolvedRelation*> relations_;
    std::unordered_map<std::string, std::set<std::string>> names_;
    std::unordered_map<HandlerIdentity, const HandlerNode*, HandlerIdentityHash> nodes_;
    std::unordered_map<HandlerIdentity, std::vector<HandlerIdentity>, HandlerIdentityHash> consumers_;
    mutable std::unordered_map<std::string, std::vector<std::string>> sources_;
};

}  // namespace

std::string cost_report(const DecoratedProgram& program) {
    return CostModel(program).report();
}

std::expected<std::string, std::string> explain_rule(const DecoratedProgram& program, std::string_view rule) {
    return CostModel(program).explain(rule);
}

}  // namespace cactus
