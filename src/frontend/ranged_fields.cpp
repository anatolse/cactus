#include "frontend/ranged_fields.hpp"

#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cactus {

namespace {

// Each container trait and the first ranged field, by name, that reads it.
std::unordered_map<SymbolId, std::string, SymbolIdHash> container_readers(const DecoratedProgram& program) {
    std::unordered_map<SymbolId, std::string, SymbolIdHash> readers;
    for (const auto& trait : program.traits | std::views::values) {
        for (const auto& field : trait.fields) {
            if (!field.range.has_value()) {
                continue;
            }
            const auto reader = trait.name + "." + field.name;
            for (const auto& container : field.range->containers) {
                auto [existing, inserted] = readers.emplace(container, reader);
                if (!inserted && reader < existing->second) {
                    existing->second = reader;
                }
            }
        }
    }
    return readers;
}

}  // namespace

void validate_container_traits(const DecoratedProgram& program, ErrorReporter& errors) {
    const auto readers = container_readers(program);
    if (readers.empty()) {
        return;
    }
    for (const auto& handler : program.execution_graph.handlers) {
        std::unordered_set<std::string> reported;
        const auto check = [&](const SymbolId& trait, const char* verb) {
            const auto reader = readers.find(trait);
            if (reader == readers.end() || !reported.insert(std::string{verb} + make_canonical_id(trait)).second) {
                return;
            }
            errors.error(handler.location,
                         "rule '" + handler.identity.rule.local_name + "' can't " + verb + " container trait '" +
                             trait.local_name + "': ranged field '" + reader->second + "' reads it; give '" +
                             trait.local_name + "' its value when its entity is made");
        };
        for (const auto& command : handler.contract.commands) {
            if (command.kind == HandlerCommandKind::Add && command.target.has_value()) {
                check(*command.target, "add");
            }
        }
        for (const auto& projection : handler.contract.projections) {
            check(projection.trait, "project");
        }
    }
}

}  // namespace cactus
