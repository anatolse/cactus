#include "frontend/single_traits.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace cactus {

namespace {

SourceLocation declaration_location(const DecoratedProgram& program, const SymbolId& archetype) {
    if (program.ast == nullptr) {
        return {};
    }
    for (const auto& decl : program.ast->declarations) {
        if (const auto* entity = std::get_if<EntityNode>(&decl); entity != nullptr && entity->resolved_entity_id == archetype) {
            return entity->location;
        }
        if (const auto* tmpl = std::get_if<TemplateNode>(&decl); tmpl != nullptr && tmpl->resolved_template_id == archetype) {
            return tmpl->location;
        }
    }
    return {};
}

bool carries(const PersistenceArchetypeDescriptor& node, const SymbolId& trait) {
    return std::ranges::contains(node.baseline_traits, trait, &PersistenceBaselineTrait::trait);
}

struct Carrier {
    SymbolId archetype;
    std::size_t nodes = 0;
};

void validate_single_trait(const DecoratedProgram& program, const SymbolId& trait, ErrorReporter& errors) {
    const auto name = make_canonical_id(trait);
    std::map<std::string, Carrier> carriers;  // by canonical archetype id
    for (const auto& node : program.persistence.archetypes) {
        if (carries(node, trait)) {
            ++carriers.try_emplace(make_canonical_id(node.node.archetype), Carrier{.archetype = node.node.archetype})
                  .first->second.nodes;
        }
    }
    std::vector<std::pair<std::string, SymbolId>> named;
    for (const auto& [canonical, carrier] : carriers) {
        const auto& [archetype, nodes] = carrier;
        if (nodes > 1) {
            errors.error(declaration_location(program, archetype),
                         std::format("'{}' carries single trait '{}' on more than one node", canonical, name));
        }
        if (archetype.kind == SymbolKind::Entity) {
            named.emplace_back(canonical, archetype);
        }
    }
    for (std::size_t index = 1; index < named.size(); ++index) {
        errors.error(declaration_location(program, named[index].second),
                     std::format("single trait '{}' is carried by two named entities, '{}' and '{}'",
                                 name,
                                 named[0].first,
                                 named[index].first));
    }
}

}  // namespace

std::vector<SymbolId> single_trait_ids(const DecoratedProgram& program) {
    std::vector<SymbolId> singles;
    for (const auto& [_, trait] : program.traits) {
        if (trait.is_single && trait.symbol_id.has_value()) {
            singles.push_back(*trait.symbol_id);
        }
    }
    std::ranges::sort(singles, {}, [](const SymbolId& trait) { return make_canonical_id(trait); });
    return singles;
}

SingleTraitFacts single_trait_facts(const DecoratedProgram& program) {
    SingleTraitFacts facts{.traits = single_trait_ids(program)};
    for (const auto& trait : facts.traits) {
        const auto carried = std::ranges::any_of(program.persistence.archetypes, [&](const PersistenceArchetypeDescriptor& node) {
            const auto& archetype = node.node.archetype;
            return archetype.kind == SymbolKind::Template && program.pub_templates.contains(archetype.local_name) &&
                   carries(node, trait);
        });
        if (carried) {
            facts.pub_template_traits.push_back(trait);
        }
    }
    return facts;
}

void validate_single_traits(const DecoratedProgram& program, ErrorReporter& errors) {
    for (const auto& trait : single_trait_ids(program)) {
        validate_single_trait(program, trait, errors);
    }
}

}  // namespace cactus
