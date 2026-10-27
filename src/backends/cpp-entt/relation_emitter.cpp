#include "backends/cpp-entt/relation_emitter.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <utility>

namespace cactus {

namespace {

// How many derivations separate a relation from its stored source.
std::size_t derivation_depth(const DecoratedProgram& program, const ResolvedRelation& relation) {
    std::size_t depth  = 0;
    const auto* source = &relation;
    while (source != nullptr && source->derivation.has_value()) {
        ++depth;
        source = EnttCodegenUtils::find_relation(program, source->derivation->source);
    }
    return depth;
}

// A defaulted `==` lets a store skip a keep run that rewrites a row unchanged.
bool comparable_fields(const ResolvedRelation& relation) {
    return std::ranges::all_of(relation.fields, [](const ResolvedField& field) {
        switch (field.type.kind) {
            case TypeKind::Int:
            case TypeKind::Float:
            case TypeKind::Bool:
            case TypeKind::String:
            case TypeKind::EntityId:
            case TypeKind::Enum:
                return true;
            default:
                return false;
        }
    });
}

const char* derivation_cpp(DerivationKind kind) {
    switch (kind) {
        case DerivationKind::Closure:
            return "Closure";
        case DerivationKind::Shortest:
            return "Shortest";
        case DerivationKind::Components:
            return "Components";
    }
    std::unreachable();
}

std::string derivation_lines(const ResolvedRelation& relation) {
    const auto& derivation = *relation.derivation;
    std::string lines      = "    static constexpr cactus::runtime::entt_backend::Derivation kDerivation =\n"
                             "        cactus::runtime::entt_backend::Derivation::" +
                        std::string(derivation_cpp(derivation.kind)) + ";\n";
    lines += "    using Source = " + EnttCodegenUtils::relation_cpp_name(derivation.source) + ";\n";
    if (derivation.cost_field.has_value()) {
        lines += "    static std::int64_t source_cost(const Source& row) noexcept { return row." +
                 cpp_escaped(*derivation.cost_field) + "; }\n";
    }
    return lines;
}

}  // namespace

// Stored relations first, then each derived relation after its source.
std::vector<const ResolvedRelation*> sorted_relations(const DecoratedProgram& program) {
    std::vector<std::pair<std::size_t, const ResolvedRelation*>> ranked;
    ranked.reserve(program.relations.size());
    for (const auto& [_, relation] : program.relations) {
        ranked.emplace_back(derivation_depth(program, relation), &relation);
    }
    std::ranges::sort(ranked, {}, [](const auto& entry) { return std::pair{entry.first, entry.second->canonical_id}; });
    std::vector<const ResolvedRelation*> relations;
    relations.reserve(ranked.size());
    for (const auto& [_, relation] : ranked) {
        relations.push_back(relation);
    }
    return relations;
}

std::vector<const ResolvedRelation*> owned_relations(const DecoratedProgram& program) {
    auto relations = sorted_relations(program);
    std::erase_if(relations, [](const ResolvedRelation* relation) { return relation->derivation.has_value(); });
    return relations;
}

std::string emit_relation_declarations(const DecoratedProgram& program) {
    return program.relations.empty() ? std::string{} : emit_relation_rows(program) + emit_relation_setup(program);
}

std::string emit_relation_rows(const DecoratedProgram& program) {
    std::ostringstream out;
    for (const auto* relation : sorted_relations(program)) {
        std::ostringstream row;
        row << "struct " << cpp_decl_name(SymbolKind::Relation, relation->name) << " {\n";
        row << "    static constexpr bool kFromOne = " << (relation->from.one ? "true" : "false") << ";\n";
        row << "    static constexpr bool kToOne   = " << (relation->to.one ? "true" : "false") << ";\n";
        row << "    using FromTrait = "
            << EnttCodegenUtils::trait_cpp_name(relation->from.trait, relation->from.trait.local_name, program) << ";\n";
        row << "    using ToTrait   = "
            << EnttCodegenUtils::trait_cpp_name(relation->to.trait, relation->to.trait.local_name, program) << ";\n";
        if (relation->derivation.has_value()) {
            row << derivation_lines(*relation);
        }
        row << EnttComponentEmitter::emit_field_lines(relation->module_name, relation->name, relation->fields, program);
        if (!relation->derivation.has_value() && comparable_fields(*relation)) {
            const auto name = cpp_decl_name(SymbolKind::Relation, relation->name);
            row << "    friend bool operator==(const " << name << "&, const " << name << "&) = default;\n";
        }
        row << "};\n";
        out << namespace_block(cpp_namespace_path(relation->module_name), row.str()) << "\n";
    }
    return out.str();
}

std::string emit_relation_setup(const DecoratedProgram& program) {
    const auto relations = owned_relations(program);
    std::ostringstream out;
    out << "inline void generated_connect_relations([[maybe_unused]] entt::registry& registry) {\n";
    for (const auto* relation : relations) {
        out << "    cactus::runtime::entt_backend::connect_relation_ends<"
            << EnttCodegenUtils::relation_cpp_name(*relation->symbol_id) << ">(registry);\n";
    }
    out << "}\n\n";
    out << "inline void generated_compact_relations([[maybe_unused]] entt::registry& registry) {\n";
    for (const auto* relation : relations) {
        out << "    " << EnttCodegenUtils::relation_store_cpp(*relation->symbol_id) << ".compact();\n";
    }
    out << "}\n";
    return namespace_block("cactus::runtime::entt_backend", out.str()) + "\n";
}

}  // namespace cactus
