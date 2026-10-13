#include "frontend/hierarchy_forest.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cactus {

bool is_hierarchy_trait(const SymbolId& trait) {
    return trait.kind == SymbolKind::Trait && trait.local_name == "Parent";
}

std::optional<SymbolId> find_hierarchy_trait(const DecoratedProgram& program) {
    std::optional<SymbolId> local;
    for (const auto& [_, trait] : program.traits) {
        if (!trait.symbol_id.has_value() || !is_hierarchy_trait(*trait.symbol_id)) {
            continue;
        }
        if (trait.symbol_id->module.name == "std.core") {
            return trait.symbol_id;
        }
        local = trait.symbol_id;
    }
    return local;
}

std::string declared_entity_identity(const EntityNode& entity) {
    return entity.resolved_entity_id.has_value() ? make_canonical_id(*entity.resolved_entity_id) : entity.name;
}

namespace {

bool is_hierarchy_trait_spelling(std::string_view spelling) {
    return spelling == "Parent" || spelling.ends_with(".Parent");
}

// The identity of the entity a declared entity's Parent entry names, if it names one.
std::optional<std::string> named_parent(const EntityNode& entity) {
    for (const auto& entry : entity.traits) {
        const bool hierarchy = entry.resolved_trait_id.has_value() ? is_hierarchy_trait(*entry.resolved_trait_id)
                                                                    : is_hierarchy_trait_spelling(entry.trait_name);
        if (!hierarchy) {
            continue;
        }
        for (const auto& assignment : entry.assignments) {
            const auto* ident = assignment.value != nullptr ? std::get_if<IdentExpr>(&assignment.value->expr) : nullptr;
            if (assignment.name != "parent" || ident == nullptr) {
                continue;
            }
            return ident->resolved_entity_id.has_value() ? make_canonical_id(*ident->resolved_entity_id)
                                                         : ident->name;
        }
    }
    return std::nullopt;
}

}  // namespace

DeclaredEntityOrder parent_first_order(const std::vector<const EntityNode*>& entities) {
    std::unordered_map<std::string, std::size_t> index_of;
    for (std::size_t index = 0; index < entities.size(); ++index) {
        index_of.emplace(declared_entity_identity(*entities[index]), index);
    }
    std::vector<std::optional<std::size_t>> parent_of(entities.size());
    std::vector<std::vector<std::size_t>> children_of(entities.size());
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const auto parent = named_parent(*entities[index]);
        const auto found  = parent.has_value() ? index_of.find(*parent) : index_of.end();
        if (found != index_of.end()) {
            parent_of[index] = found->second;
            children_of[found->second].push_back(index);
        }
    }

    DeclaredEntityOrder result;
    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (!parent_of[index].has_value()) {
            ready.push(index);
        }
    }
    std::vector<bool> placed(entities.size(), false);
    while (!ready.empty()) {
        const auto index = ready.top();
        ready.pop();
        placed[index] = true;
        result.order.push_back(entities[index]);
        for (const auto child : children_of[index]) {
            ready.push(child);
        }
    }

    // Every unplaced entity sits on a cycle or below one; a cycle member reaches itself.
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (placed[index]) {
            continue;
        }
        auto walk = parent_of[index];
        for (std::size_t steps = 0; walk.has_value() && *walk != index && steps < entities.size(); ++steps) {
            walk = parent_of[*walk];
        }
        if (walk == index) {
            result.cyclic.push_back(entities[index]);
        }
    }
    std::ranges::sort(result.cyclic, {}, [](const EntityNode* entity) { return declared_entity_identity(*entity); });
    return result;
}

namespace {

void add_archetype_sources(const DecoratedProgram& program,
                           const SymbolId& parent,
                           HierarchyFacts& facts,
                           std::uint32_t& depth) {
    for (const auto& archetype : program.persistence.archetypes) {
        const auto site = canonical_node_string(archetype.node);
        if (!archetype.node.role_path.empty()) {
            facts.sources.push_back({.kind = ParentSourceKind::Structural, .site = site});
            depth = std::max(depth, static_cast<std::uint32_t>(archetype.node.role_path.size()));
        }
        if (std::ranges::any_of(archetype.baseline_traits,
                                [&](const PersistenceBaselineTrait& baseline) { return baseline.trait == parent; })) {
            facts.sources.push_back({.kind = ParentSourceKind::Attached, .site = site});
        }
    }
}

void add_handler_sources(const DecoratedProgram& program, const SymbolId& parent, HierarchyFacts& facts) {
    const auto& unreachable = program.reachability.unreachable_handlers;
    for (const auto& handler : program.execution_graph.handlers) {
        if (std::ranges::contains(unreachable, handler.identity)) {
            continue;
        }
        for (const auto& command : handler.contract.commands) {
            if (command.target != parent) {
                continue;
            }
            if (command.kind == HandlerCommandKind::Add) {
                facts.sources.push_back({.kind = ParentSourceKind::Reparent, .site = handler.identity.canonical_id()});
            } else if (command.kind == HandlerCommandKind::Remove) {
                facts.sources.push_back({.kind = ParentSourceKind::Detach, .site = handler.identity.canonical_id()});
            }
        }
    }
}

}  // namespace

void derive_hierarchy_facts(DecoratedProgram& program) {
    HierarchyFacts facts{.derived = true};
    std::uint32_t depth = 0;
    if (const auto parent = find_hierarchy_trait(program); parent.has_value()) {
        add_archetype_sources(program, *parent, facts, depth);
        add_handler_sources(program, *parent, facts);
    }
    std::ranges::sort(facts.sources);
    const auto [first, last] = std::ranges::unique(facts.sources);
    facts.sources.erase(first, last);
    facts.has_reparent = std::ranges::any_of(
        facts.sources, [](const ParentSource& source) { return source.kind == ParentSourceKind::Reparent; });
    const bool bounded = std::ranges::all_of(facts.sources, [](const ParentSource& source) {
        return source.kind == ParentSourceKind::Structural || source.kind == ParentSourceKind::Detach;
    });
    if (bounded) {
        facts.depth_bound = depth;
    }
    program.hierarchy = std::move(facts);
}

}  // namespace cactus
