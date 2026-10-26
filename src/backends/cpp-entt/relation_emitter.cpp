#include "backends/cpp-entt/relation_emitter.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <sstream>

namespace cactus {

std::vector<const ResolvedRelation*> sorted_relations(const DecoratedProgram& program) {
    std::vector<const ResolvedRelation*> relations;
    relations.reserve(program.relations.size());
    for (const auto& [_, relation] : program.relations) {
        relations.push_back(&relation);
    }
    std::ranges::sort(relations, {}, &ResolvedRelation::canonical_id);
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
        row << EnttComponentEmitter::emit_field_lines(relation->module_name, relation->name, relation->fields, program);
        row << "};\n";
        out << namespace_block(cpp_namespace_path(relation->module_name), row.str()) << "\n";
    }
    return out.str();
}

std::string emit_relation_setup(const DecoratedProgram& program) {
    const auto relations = sorted_relations(program);
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
