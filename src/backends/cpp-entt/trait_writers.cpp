#include "backends/cpp-entt/trait_writers.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace cactus {

namespace {

// Extern rules without `each:` whose writes still only reach entities carrying
// known traits.
std::vector<SymbolId> native_write_selection(const SymbolId& rule) {
    if (make_canonical_id(rule) == "std.physics.volume.FollowBodies") {
        const ModuleId transform{.name = "std.transform.volume"};
        return {make_symbol_id(SymbolKind::Trait, transform, "LocalTransform"),
                make_symbol_id(SymbolKind::Trait, transform, "WorldTransform")};
    }
    return {};
}

class WriterCollector {
public:
    explicit WriterCollector(const HandlerNode& handler) : handler_(&handler), contract_(&handler.contract) {}

    void collect(const SymbolId& trait, TraitWriters& out) const {
        if (contract_->effect_acts.contains("editor")) {
            out.target_sets.push_back({trait});
        }
        if (contract_->writes.contains(trait)) {
            collect_write(trait, out);
        }
        for (const auto& command : contract_->commands) {
            if (command.kind == HandlerCommandKind::Set && command.target == trait) {
                collect_set(trait, command, out);
            }
        }
    }

private:
    void collect_write(const SymbolId& trait, TraitWriters& out) const {
        bool explained = false;
        for (const auto& [entity, traits] : contract_->named_writes) {
            if (traits.contains(trait)) {
                out.named_entities.push_back(entity);
                explained = true;
            }
        }
        if (contract_->domain_kind == HandlerDomainKind::Unary && std::ranges::contains(contract_->selection, trait)) {
            out.target_sets.push_back(contract_->selection);
            explained = true;
        }
        for (const auto& binding : contract_->pair_bindings) {
            if (std::ranges::contains(binding.required_traits, trait)) {
                out.target_sets.push_back(binding.required_traits);
                explained = true;
            }
        }
        if (handler_->implementation == HandlerImplementationKind::External &&
            contract_->domain_kind == HandlerDomainKind::Selectionless) {
            auto selection = native_write_selection(handler_->identity.rule);
            out.target_sets.push_back(selection.empty() ? std::vector{trait} : std::move(selection));
            explained = true;
        }
        if (!explained) {
            out.target_sets.push_back({trait});
        }
    }

    void collect_set(const SymbolId& trait, const InferredHandlerCommand& command, TraitWriters& out) const {
        for (const auto& target : command.entities) {
            switch (target.kind) {
                case EntityTargetKind::Self:
                    out.target_sets.push_back(contract_->selection.empty() ? std::vector{trait} : contract_->selection);
                    break;
                case EntityTargetKind::PairBinding:
                    out.target_sets.push_back(target.binding_index < contract_->pair_bindings.size()
                                                  ? contract_->pair_bindings[target.binding_index].required_traits
                                                  : std::vector{trait});
                    break;
                case EntityTargetKind::Named:
                    if (target.symbol.has_value()) {
                        out.named_entities.push_back(*target.symbol);
                    }
                    break;
                case EntityTargetKind::Spawned:
                    // Patches an entity created by the same commit, before anything reads it.
                    break;
                case EntityTargetKind::Unknown:
                    out.target_sets.push_back({trait});
                    break;
            }
        }
        if (command.entities.empty()) {
            out.target_sets.push_back({trait});
        }
    }

    const HandlerNode* handler_;
    const HandlerContract* contract_;
};

void sort_symbols(std::vector<SymbolId>& symbols) {
    std::ranges::sort(symbols, std::less{}, [](const SymbolId& symbol) { return make_canonical_id(symbol); });
    const auto [first, last] = std::ranges::unique(symbols);
    symbols.erase(first, last);
}

}  // namespace

TraitWriters trait_writers(const DecoratedProgram& program, const std::vector<SymbolId>& traits) {
    TraitWriters result;
    for (const auto& handler : program.execution_graph.handlers) {
        const WriterCollector collector(handler);
        for (const auto& trait : traits) {
            collector.collect(trait, result);
        }
    }
    for (auto& set : result.target_sets) {
        sort_symbols(set);
    }
    std::ranges::sort(result.target_sets, std::less{}, [](const std::vector<SymbolId>& set) {
        std::vector<std::string> ids;
        ids.reserve(set.size());
        for (const auto& symbol : set) {
            ids.push_back(make_canonical_id(symbol));
        }
        return ids;
    });
    const auto [first, last] = std::ranges::unique(result.target_sets);
    result.target_sets.erase(first, last);
    sort_symbols(result.named_entities);
    return result;
}

}  // namespace cactus
