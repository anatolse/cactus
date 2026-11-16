#include "frontend/keep_inputs.hpp"

#include "common/ast_expressions.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <unordered_set>

namespace cactus {

namespace {

struct InputSets {
    std::unordered_set<SymbolId> traits;
    std::unordered_set<SymbolId> relations;
    std::map<std::pair<std::string, std::string>, TraitFieldRef> fields;
};

bool is_derived(const SymbolId& relation, const RelationLookup& find_relation) {
    const auto* declaration = find_relation(relation);
    return declaration != nullptr && declaration->derivation.has_value();
}

void add_relation(InputSets& sets, const SymbolId& relation, const RelationLookup& find_relation) {
    const auto* declaration = find_relation(relation);
    if (declaration == nullptr || !declaration->derivation.has_value()) {
        sets.relations.insert(relation);
        return;
    }
    // Derived rows change with their source's rows and, for closure and components, with the end trait's members.
    sets.traits.insert(declaration->from.trait);
    add_relation(sets, declaration->derivation->source, find_relation);
}

void add_fields(InputSets& sets, const SymbolId& trait, const FieldAccess& access, const TraitLookup& find_trait) {
    const auto* declaration = find_trait(trait);
    if (declaration == nullptr) {
        return;
    }
    for (const auto& field : declaration->fields) {
        if (field.is_let || !access.covers(field.name)) {
            continue;
        }
        TraitFieldRef ref{.trait = trait, .field = field.name};
        sets.fields.try_emplace(keep_field_key(ref), std::move(ref));
    }
}

template <typename Symbols>
std::vector<SymbolId> sorted(const Symbols& symbols) {
    std::vector<SymbolId> result(symbols.begin(), symbols.end());
    std::ranges::sort(result, {}, [](const SymbolId& symbol) { return make_canonical_id(symbol); });
    return result;
}

}  // namespace

KeepInputs keep_inputs(const HandlerContract& contract,
                       const TraitLookup& find_trait,
                       const RelationLookup& find_relation) {
    InputSets sets;
    for (const auto& table : tables_read_by(contract)) {
        if (table.kind == SymbolKind::Relation) {
            add_relation(sets, table, find_relation);
        } else {
            sets.traits.insert(table);
        }
    }
    sets.traits.insert(contract.observed_membership.begin(), contract.observed_membership.end());
    for (const auto& read : contract.reads) {
        if (read.kind != SymbolKind::Relation) {
            add_fields(sets, read, field_access(contract.read_fields, read), find_trait);
        }
    }
    KeepInputs inputs{.traits = sorted(sets.traits), .relations = sorted(sets.relations)};
    for (auto& [key, ref] : sets.fields) {
        inputs.fields.push_back(std::move(ref));
    }
    return inputs;
}

std::pair<std::string, std::string> keep_field_key(const TraitFieldRef& ref) {
    return {make_canonical_id(ref.trait), ref.field.value_or("")};
}

namespace {

// The first construct in `expression` that reads beyond the row's own bindings.
std::optional<KeepEvaluation> foreign_read(const ExprNode& expression, const RelationLookup& find_relation) {
    std::optional<KeepEvaluation> found;
    visit_expression(expression, [&](const ExprNode& node) {
        if (found.has_value()) {
            return;
        }
        if (const auto* access = std::get_if<TraitAccessExpr>(&node.expr)) {
            if (access->resolved_entity_id.has_value()) {
                found = KeepEvaluation{.level    = KeepLevel::Full,
                                       .reason   = "the named entity read `" + access->resolved_entity_id->local_name +
                                                 "[" + access->trait_name + "]`",
                                       .location = access->location};
            } else if (access->link_target.has_value()) {
                found = KeepEvaluation{.level    = KeepLevel::Full,
                                       .reason   = "the read through link `" + access->link_field + "`",
                                       .location = access->location};
            }
            return;
        }
        if (const auto* test = std::get_if<RelationTestExpr>(&node.expr)) {
            const bool derived = is_derived(test->relation, find_relation);
            found = KeepEvaluation{.level    = KeepLevel::Full,
                                   .reason   = (derived ? "the derived relation `" : "the relation test `") +
                                             test->relation_name + "`",
                                   .location = test->location};
        }
    });
    return found;
}

std::vector<const ExprNode*> keep_clause_expressions(const RuleNode& rule) {
    std::vector<const ExprNode*> expressions;
    if (rule.where_clause.has_value()) {
        for (const auto& predicate : rule.where_clause->predicates) {
            expressions.push_back(predicate.get());
        }
    }
    if (rule.reduce.has_value()) {
        for_each_reducer_expr(*rule.reduce, [&](const ExprNode& expression) { expressions.push_back(&expression); });
    }
    if (rule.keep.has_value()) {
        if (rule.keep->condition != nullptr) {
            expressions.push_back(rule.keep->condition.get());
        }
        for (const auto& field : rule.keep->fields) {
            expressions.push_back(field.value.get());
        }
    }
    return expressions;
}

// Only an equality join can find the partners of a changed entity on either side.
std::optional<KeepEvaluation> pair_plan_limit(const RuleNode& rule, const HandlerContract& contract) {
    if (!rule.pairs.has_value()) {
        return std::nullopt;
    }
    for (const auto& binding : contract.pair_bindings) {
        if (binding.relation.has_value()) {
            return KeepEvaluation{.level    = KeepLevel::Full,
                                  .reason   = "the row binding `" + binding.name + "`",
                                  .location = rule.pairs->location};
        }
    }
    if (contract.equality_join.has_value() && contract.equality_join->branches.empty()) {
        return std::nullopt;
    }
    const auto* plan = contract.spatial_join.has_value() ? "the spatial join" : "the pair plan";
    return KeepEvaluation{.level    = KeepLevel::Full,
                          .reason   = std::string(plan) + ", which can't look up rows from both bindings",
                          .location = rule.pairs->location};
}

}  // namespace

KeepEvaluation classify_keep(const RuleNode& rule,
                             const HandlerContract& contract,
                             const RelationLookup& find_relation) {
    if (rule.rows.has_value() && is_derived(rule.rows->relation, find_relation)) {
        return {.level    = KeepLevel::Full,
                .reason   = "the row domain over the derived relation `" + rule.rows->relation_name + "`",
                .location = rule.rows->location};
    }
    for (const auto* expression : keep_clause_expressions(rule)) {
        if (auto found = foreign_read(*expression, find_relation)) {
            return *found;
        }
    }
    return pair_plan_limit(rule, contract).value_or(KeepEvaluation{});
}

}  // namespace cactus
