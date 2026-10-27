#include "frontend/semantic_analyzer.hpp"

#include "common/ast_expressions.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <unordered_set>

namespace cactus {

namespace {

const char* end_label(bool from) {
    return from ? "from" : "to";
}

std::string end_phrase(const std::string& relation_name, bool from) {
    return std::string("the `") + end_label(from) + ":` end of relation '" + relation_name + "'";
}

}  // namespace

ResolvedRelation SemanticAnalyzer::declared_relation(const RelationNode& node) const {
    const auto relation_id = make_symbol_id(SymbolKind::Relation, current_module_id_, node.name);
    ResolvedRelation relation;
    relation.name         = node.name;
    relation.is_pub       = node.is_pub;
    relation.module_name  = relation_id.module.name;
    relation.canonical_id = make_canonical_id(relation_id);
    relation.symbol_id    = relation_id;
    return relation;
}

// Ends and fields of every relation declared in this module.
void SemanticAnalyzer::resolve_relation_declarations(ProgramNode& program) {
    std::vector<RelationNode*> derived;
    for (auto& decl : program.declarations) {
        auto* node = std::get_if<RelationNode>(&decl);
        if (node == nullptr) {
            continue;
        }
        if (node->derivation.has_value()) {
            derived.push_back(node);
            continue;
        }
        auto relation = declared_relation(*node);
        for (const bool from : {true, false}) {
            auto& end = from ? node->from : node->to;
            if (end.trait_name.empty()) {
                continue;  // the parser reported the missing end
            }
            end.resolved_trait_id = resolve_relation_end(*node, end, from);
            auto& resolved        = from ? relation.from : relation.to;
            resolved.one          = end.one;
            if (end.resolved_trait_id.has_value()) {
                resolved.trait = *end.resolved_trait_id;
            }
        }
        for (auto& field : node->fields) {
            relation.fields.push_back(resolve_relation_field(field));
        }
        result_.relations[node->name] = std::move(relation);
    }
    resolve_derived_relations(derived);
}

namespace {

ResolvedField derived_field(const std::string& name, TypeInfo type) {
    return ResolvedField{.name = name, .type = std::move(type), .is_let = true};
}

std::string derived_read_only_message(const ResolvedRelation& relation) {
    return "'" + relation.name + "' is derived from '" + relation.derivation->source.local_name + "' and is read-only";
}

}  // namespace

// Derived relations in dependency order: a source derived in this module resolves first.
void SemanticAnalyzer::resolve_derived_relations(const std::vector<RelationNode*>& nodes) {
    std::unordered_map<std::string, RelationNode*> pending;
    for (auto* node : nodes) {
        pending.emplace(node->name, node);
    }
    std::unordered_set<std::string> active;
    const auto resolve = [&](this const auto& self, RelationNode& node) -> void {
        if (result_.relations.contains(node.name)) {
            return;
        }
        if (!active.insert(node.name).second) {
            errors_.error(node.location, "relation '" + node.name + "' is derived from itself");
            return;
        }
        auto& derivation  = *node.derivation;
        const auto source = try_resolve_ref_of_kind(derivation.source_name, {SymbolKind::Relation});
        const auto local  = source.has_value() && source->module == current_module_id_
                                ? pending.find(source->local_name)
                                : pending.end();
        if (local != pending.end()) {
            self(*local->second);
        }
        auto relation = declared_relation(node);
        if (const auto* resolved = source.has_value() ? find_resolved_relation(*source) : nullptr) {
            derivation.resolved_source = source;
            derive_relation_shape(node, *resolved, relation);
        } else if (!source.has_value() || !active.contains(source->local_name)) {
            errors_.error(derivation.location,
                          "unknown relation '" + derivation.source_name + "' in `" + std::string(derivation_form(derivation.kind)) +
                              "(" + derivation.source_name + ")`");
        }
        result_.relations[node.name] = std::move(relation);
        active.erase(node.name);
    };
    for (auto* node : nodes) {
        resolve(*node);
    }
}

// Both ends name the source's end trait; `shortest` rows carry `next` and `distance`.
void SemanticAnalyzer::derive_relation_shape(const RelationNode& node,
                                             const ResolvedRelation& source,
                                             ResolvedRelation& relation) const {
    const auto& derivation = *node.derivation;
    if (source.from.trait != source.to.trait) {
        errors_.error(derivation.location,
                      "`" + std::string(derivation_form(derivation.kind)) + "` needs one trait on both ends; '" +
                          derivation.source_name + "' has `from: " + source.from.trait.local_name + "` and `to: " +
                          source.to.trait.local_name + "`");
    }
    relation.from.trait = source.from.trait;
    relation.to.trait   = source.from.trait;
    relation.from.one   = derivation.kind == DerivationKind::Components;
    relation.derivation = ResolvedDerivation{
        .kind = derivation.kind, .source = *derivation.resolved_source, .cost_field = derivation.cost_field};
    if (derivation.kind != DerivationKind::Shortest || !derivation.cost_field.has_value()) {
        return;
    }
    relation.fields   = {derived_field("next", make_entity_id_type()), derived_field("distance", make_int_type())};
    const auto& cost  = *derivation.cost_field;
    const auto* field = source.find_field(cost);
    if (field == nullptr) {
        errors_.error(derivation.location,
                      "relation '" + derivation.source_name + "' has no field '" + cost + "' for `by`");
    } else if (field->type.kind != TypeKind::Int) {
        errors_.error(derivation.location,
                      "`by` needs an `int` field; '" + derivation.source_name + "." + cost + "' is '" +
                          field->type.name + "'; scale costs to integers");
    }
}

// A `closure` or `shortest` row binding must fix an end, so a pass never lists all pairs.
void SemanticAnalyzer::require_fixed_derived_end(const RuleNode& rule) {
    const auto* name = row_binding_name(rule);
    if (name == nullptr || find_row_end_join(rule).has_value()) {
        return;
    }
    std::optional<SymbolId> relation;
    SourceLocation location;
    if (rule.rows.has_value()) {
        relation = rule.rows->relation;
        location = rule.rows->location;
    } else {
        const auto binding = std::ranges::find(rule.pairs->bindings, *name, &PairBindingNode::name);
        relation           = binding->resolved_relation_id;
        location           = binding->location;
    }
    const auto* declaration = relation.has_value() ? find_resolved_relation(*relation) : nullptr;
    if (declaration == nullptr || !declaration->derived_by_key()) {
        return;
    }
    errors_.error(location,
                  "row binding '" + *name + "' of derived relation '" + declaration->name +
                      "' lists every pair; fix one end with `" + *name + ".from == ...` or `" + *name +
                      ".to == ...` in `where:`");
}

// A relation field is a `let` field whose default is a const expression of its type.
ResolvedField SemanticAnalyzer::resolve_relation_field(FieldNode& field) {
    ResolvedField resolved;
    resolved.name        = field.name;
    resolved.type        = resolve_type_ref(field.type);
    resolved.is_let      = true;
    resolved.has_default = field.default_value.has_value();
    if (!field.default_value.has_value()) {
        return resolved;
    }
    auto& value = **field.default_value;
    resolve_const_refs(value);
    const auto actual = infer_expr_type(value, {}, {}, nullptr);
    if (actual.kind != TypeKind::Unknown && resolved.type.kind != TypeKind::Unknown &&
        actual.kind != resolved.type.kind) {
        errors_.error(field.location,
                      "default value type '" + actual.name + "' does not match field type '" + resolved.type.name +
                          "'");
    }
    validate_const_expression(value, "relation field default value must be a const expression", true);
    return resolved;
}

std::optional<SymbolId> SemanticAnalyzer::resolve_relation_end(const RelationNode& relation,
                                                               const RelationEndNode& end,
                                                               bool from) {
    const auto prefix = end_phrase(relation.name, from);
    auto trait        = try_resolve_ref_of_kind(end.trait_name, {SymbolKind::Trait});
    if (!trait.has_value() || find_resolved_trait(*trait) == nullptr) {
        errors_.error(end.location, "unknown trait '" + end.trait_name + "' at " + prefix);
        return std::nullopt;
    }
    if (state_of_variant_trait(*trait).has_value()) {
        errors_.error(end.location, prefix + " can't name state variant '" + end.trait_name + "'");
        return std::nullopt;
    }
    return trait;
}

const ResolvedRelation* SemanticAnalyzer::find_resolved_relation(const SymbolId& relation) const {
    if (relation.kind != SymbolKind::Relation) {
        return nullptr;
    }
    if (relation.module == current_module_id_) {
        const auto found = result_.relations.find(relation.local_name);
        return found == result_.relations.end() ? nullptr : &found->second;
    }
    for (const auto& [_, imported] : imports_.modules) {
        if (imported.module_name != relation.module.name) {
            continue;
        }
        if (const auto found = imported.relations.find(relation.local_name); found != imported.relations.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

// A row needs durable ends: a projected trait comes and goes within a frame.
void SemanticAnalyzer::validate_relation_ends(const ProgramNode& program) {
    std::unordered_set<SymbolId> projected;
    for (const auto& node : result_.execution_graph.handlers) {
        projected.insert(node.contract.projects.begin(), node.contract.projects.end());
    }
    for (const auto& decl : program.declarations) {
        const auto* node = std::get_if<RelationNode>(&decl);
        if (node == nullptr) {
            continue;
        }
        for (const bool from : {true, false}) {
            const auto& end = from ? node->from : node->to;
            if (end.resolved_trait_id.has_value() && projected.contains(*end.resolved_trait_id)) {
                errors_.error(end.location,
                              end_phrase(node->name, from) + " can't name projected trait '" + end.trait_name + "'");
            }
        }
    }
}

// ── Row bindings ────────────────────────────────────────────────────────────

// A `pairs:` entry naming a relation binds rows, not entities: it moves out of
// the binding's trait entries so every trait pass skips it.
void SemanticAnalyzer::extract_row_bindings(RuleNode& rule) {
    if (!rule.pairs.has_value()) {
        return;
    }
    std::size_t rows = 0;
    for (auto& binding : rule.pairs->bindings) {
        std::optional<SymbolId> relation;
        const auto entry = std::ranges::find_if(binding.traits, [&](const FilterEntry& candidate) {
            relation = try_resolve_ref_of_kind(candidate.qualified_name, {SymbolKind::Relation});
            return relation.has_value();
        });
        if (entry == binding.traits.end()) {
            continue;
        }
        if (binding.traits.size() != 1) {
            errors_.error(entry->location,
                          "a row binding has exactly one relation entry; '" + binding.name + "' also lists traits");
            binding.traits.erase(entry);
            continue;
        }
        if (entry->alias.has_value()) {
            errors_.error(entry->location,
                          "a row binding takes no alias; read the row through `" + binding.name + ".<field>`");
        }
        binding.relation_name        = entry->qualified_name;
        binding.resolved_relation_id = relation;
        binding.traits.clear();
        if (++rows == 2) {
            errors_.error(binding.location, "a rule may bind at most one relation; '" + rule.name + "' binds two");
        }
    }
}

// `each: R as r` runs over the rows of `R`; the entry moves out of the filter.
void SemanticAnalyzer::extract_row_domain(RuleNode& rule) {
    auto& entries = rule.filter.entries;
    std::optional<SymbolId> relation;
    const auto entry = std::ranges::find_if(entries, [&](const FilterEntry& candidate) {
        relation = try_resolve_ref_of_kind(candidate.qualified_name, {SymbolKind::Relation});
        return relation.has_value();
    });
    if (entry == entries.end()) {
        return;
    }
    const auto location = entry->location;
    if (entries.size() != 1) {
        errors_.error(location, "a relation row domain has exactly one relation entry; '" + rule.name +
                                    "' also lists traits");
        entries.erase(entry);
        return;
    }
    if (!entry->alias.has_value()) {
        errors_.error(location, "`each: " + entry->qualified_name + "` binds relation rows; name them with `as`");
    }
    if (!rule.exclude.entries.empty() || !rule.exclude.trait_names.empty()) {
        errors_.error(rule.exclude.location, "a relation row domain takes no `without:`");
    }
    if (rule.pairs.has_value()) {
        errors_.error(rule.pairs->location,
                      "rule '" + rule.name +
                          "' must choose one execution domain: `pairs:` cannot be combined with `each:` or "
                          "`without:`");
    }
    rule.rows = RowDomainNode{
        .relation_name = entry->qualified_name,
        .alias         = entry->alias.value_or(""),
        .relation      = *relation,
        .location      = location,
    };
    entries.clear();
    rule.filter.trait_names.clear();
}

std::optional<PairScope> SemanticAnalyzer::rule_scope(const RuleNode& rule) {
    if (rule.pairs.has_value()) {
        return build_pair_scope(rule);
    }
    if (rule.rows.has_value()) {
        return PairScope{{rule.rows->alias, PairBindingScope{.relation = rule.rows->relation}}};
    }
    return std::nullopt;
}

bool SemanticAnalyzer::reject_row_domain_clauses(const RuleNode& rule) {
    const bool row_binding = rule.pairs.has_value() && std::ranges::any_of(rule.pairs->bindings, &PairBindingNode::is_row);
    if (!row_binding && !rule.rows.has_value()) {
        return false;
    }
    require_fixed_derived_end(rule);
    if (rule.reduce.has_value()) {
        errors_.error(rule.reduce->location,
                      "rule '" + rule.name + "': `reduce:` can't run over relation rows; reduce over entity bindings");
    }
    if (rule.keep.has_value()) {
        errors_.error(rule.keep->location,
                      "rule '" + rule.name + "': `keep` can't run over relation rows; keep from entity bindings");
    }
    return true;
}

void SemanticAnalyzer::report_row_binding_value(const std::string& name, const SourceLocation& location) const {
    errors_.error(location,
                  "'" + name + "' is a relation row, not an entity; use `" + name + ".from` or `" + name + ".to`");
}

// `r.from` and `r.to` are the row's ends; any other member is one of its fields.
TypeInfo SemanticAnalyzer::infer_row_member_type(const std::vector<std::string>& chain,
                                                 const SourceLocation& location,
                                                 const SymbolId& relation) const {
    if (chain[1] == "from" || chain[1] == "to") {
        return chain.size() == 2 ? make_entity_id_type() : make_unknown_type();
    }
    const auto* declaration = find_resolved_relation(relation);
    if (declaration == nullptr) {
        return make_unknown_type();
    }
    const auto* field = declaration->find_field(chain[1]);
    if (field == nullptr) {
        errors_.error(location, "relation '" + relation.local_name + "' has no field '" + chain[1] + "'");
        return make_unknown_type();
    }
    return descend_vector_color_members(field->type, chain, 2);
}

// ── Relation tests ──────────────────────────────────────────────────────────

void SemanticAnalyzer::lower_relation_test(ExprNode& expr) const {
    auto* call = std::get_if<CallExpr>(&expr.expr);
    if (call == nullptr) {
        return;
    }
    const auto name = dotted_name_of(*call->callee);
    if (name.empty()) {
        return;
    }
    const auto relation = try_resolve_ref_of_kind(name, {SymbolKind::Relation});
    if (!relation.has_value()) {
        return;
    }
    RelationTestExpr test{.relation_name = name, .relation = *relation, .location = call->location};
    if (call->args.size() != 2) {
        errors_.error(call->location, "a relation test takes two entities: `" + name + "(from, to)`");
    } else {
        const auto take = [](std::unique_ptr<ExprNode>& arg) {
            return is_wildcard_pattern(*arg) ? nullptr : std::move(arg);
        };
        test.from = take(call->args[0]);
        test.to   = take(call->args[1]);
        if (test.from == nullptr && test.to == nullptr) {
            errors_.error(call->location, "`" + name + "(_, _)` tests nothing; name at least one end");
        }
    }
    expr.expr = std::move(test);
}

TypeInfo SemanticAnalyzer::infer_relation_test_type(
    const RelationTestExpr& test,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& local_bindings,
    const ResolvedStruct* handler_event,
    const PairScope* pair_scope) const {
    for_each_relation_end(test, [&](const ExprNode& end) {
        const auto type = infer_expr_type(end, filter_bindings, local_bindings, handler_event, pair_scope);
        if (type.kind != TypeKind::EntityId && type.kind != TypeKind::Unknown) {
            errors_.error(end.location, "relation test arguments must be of type 'entity_id', got '" + type.name + "'");
        }
    });
    return make_bool_type();
}

namespace {

template <typename Visit>
void visit_child(const std::unique_ptr<ExprNode>& child, const Visit& visit) {
    if (child != nullptr) {
        visit(*child);
    }
}

template <typename Visit>
void visit_child(const std::optional<std::unique_ptr<ExprNode>>& child, const Visit& visit) {
    if (child.has_value()) {
        visit_child(*child, visit);
    }
}

template <typename Visit>
void visit_children(const std::vector<FieldAssignment>& assignments, const Visit& visit) {
    for (const auto& assignment : assignments) {
        visit_child(assignment.value, visit);
    }
}

// The expressions a statement holds itself, not those of its nested blocks.
template <typename Node, typename Visit>
void visit_statement_expressions(const Node& node, const Visit& visit) {
    if constexpr (requires { node.value; }) {
        visit_child(node.value, visit);
    }
    if constexpr (requires { node.target_expr; }) {
        visit_child(node.target_expr, visit);
    }
    if constexpr (requires { node.target; }) {
        visit_child(node.target, visit);
    }
    if constexpr (requires { node.payload; }) {
        visit_children(node.payload, visit);
    }
    if constexpr (requires { node.args; }) {
        visit_children(node.args, visit);
    }
    if constexpr (requires { node.from; node.to; }) {
        visit_child(node.from, visit);
        visit_child(node.to, visit);
    }
    if constexpr (requires { node.expr; }) {
        visit_child(node.expr, visit);
    }
    if constexpr (requires { node.iterable; }) {
        visit_child(node.iterable, visit);
    }
    if constexpr (requires { node.condition; }) {
        visit_child(node.condition, visit);
    }
    if constexpr (requires { node.subject; }) {
        visit_child(node.subject, visit);
    }
}

// Calls `visit` on every expression a statement list holds, statements nested included.
template <typename Visit>
void for_each_statement_expression(const std::vector<std::unique_ptr<StmtNode>>& stmts, const Visit& visit) {
    for (const auto& stmt : stmts) {
        std::visit(
            [&](const auto& node) {
                visit_statement_expressions(node, visit);
                if constexpr (requires { node.iterable; }) {
                    for_each_statement_expression(node.body, visit);
                }
                if constexpr (requires { node.then_body; }) {
                    for_each_statement_expression(node.then_body, visit);
                    for (const auto& branch : node.else_if_branches) {
                        visit_child(branch.condition, visit);
                        for_each_statement_expression(branch.body, visit);
                    }
                    for_each_statement_expression(node.else_body, visit);
                }
                if constexpr (requires { node.arms; }) {
                    for (const auto& arm : node.arms) {
                        for_each_statement_expression(arm.body, visit);
                    }
                }
            },
            stmt->stmt);
    }
}

}  // namespace

// A test reads the pass's snapshot, which only `where:` and `when:` evaluate against.
void SemanticAnalyzer::validate_relation_test_contexts(const ProgramNode& program) {
    const auto check = [this](const ExprNode& root) {
        visit_expression(root, [this](const ExprNode& node) {
            if (const auto* test = std::get_if<RelationTestExpr>(&node.expr)) {
                errors_.error(test->location,
                              "a relation test is allowed only in `where:` and `when:`; move `" + test->relation_name +
                                  "(...)` into `where:`");
            }
        });
    };
    for (const auto& decl : program.declarations) {
        if (const auto* func = std::get_if<FuncNode>(&decl)) {
            for_each_statement_expression(func->body, check);
            continue;
        }
        const auto* rule = std::get_if<RuleNode>(&decl);
        if (rule == nullptr) {
            continue;
        }
        for (const auto& handler : rule->handlers) {
            for_each_statement_expression(handler.body, check);
        }
        for (const auto& key : rule->order_by) {
            check(*key.expression);
        }
        if (rule->limit.has_value()) {
            for_each_limit_count(*rule->limit, check);
        }
        if (rule->reduce.has_value()) {
            for_each_reducer_expr(*rule->reduce, check);
        }
        if (const auto& keep = rule->keep; keep.has_value()) {
            visit_children(keep->fields, check);
            visit_child(keep->condition, check);
        }
    }
}

// ── Kept relations ──────────────────────────────────────────────────────────

FieldAccess declared_fields(const ResolvedRelation* relation) {
    if (relation == nullptr) {
        return all_fields();
    }
    FieldAccess access;
    for (const auto& field : relation->fields) {
        access.fields.insert(field.name);
    }
    return access;
}

// `keep R(a, b)`: `R` holds one row per tuple of an unreduced pair rule.
void SemanticAnalyzer::validate_relation_keep(const RuleNode& rule) {
    const auto& keep = *rule.keep;
    reject_keep_rule_clauses(rule);
    const auto prefix = "rule '" + rule.name + "': ";
    const auto& target   = keep.resolved_target;
    const auto* relation = target.has_value() ? find_resolved_relation(*target) : nullptr;
    if (relation != nullptr && relation->derivation.has_value()) {
        errors_.error(keep.location, derived_read_only_message(*relation));
        return;
    }
    if (!rule.pairs.has_value() || rule.reduce.has_value()) {
        errors_.error(keep.location, prefix + "a relation keep needs an unreduced `pairs:` rule");
        return;
    }
    if (keep.condition != nullptr) {
        errors_.error(keep.condition->location,
                      prefix + "`if` works only on a `reduce per` keep; use `where:` to choose which pairs '" +
                          keep.target_name + "' holds");
    }
    if (keep.binding.has_value()) {
        errors_.error(keep.binding_location,
                      prefix + "`keep " + keep.target_name + "(...)` names its bindings; remove `on " + *keep.binding +
                          "`");
    }
    if (relation == nullptr) {
        errors_.error(keep.location,
                      "'" + keep.target_name + "' is not a relation; `keep " + keep.target_name + "(...)` needs one");
        return;
    }
    if (target->module != current_module_id_) {
        errors_.error(keep.location,
                      prefix + "can't keep '" + keep.target_name +
                          "': a kept relation must be declared in the rule's own module");
    }
    if (!validate_relation_keep_arguments(keep, *rule.pairs, *relation)) {
        return;
    }
    for (const auto& field : keep.fields) {
        check_clause_purity_expr(*field.value, "keep: field values must be pure");
    }
    const auto scope = build_pair_scope(rule);
    const auto desc  = "`keep " + keep.target_name + "(" + keep.arguments[0].spelling + ", " +
                      keep.arguments[1].spelling + ")`";
    validate_trait_field_supply(relation->fields, keep.fields, desc, keep.location, {}, {}, nullptr, &scope);
}

// The arguments are the rule's two bindings, each selecting the trait of its end.
bool SemanticAnalyzer::validate_relation_keep_arguments(const KeepClause& keep,
                                                        const PairClause& pairs,
                                                        const ResolvedRelation& relation) {
    const auto& bindings = pairs.bindings;
    if (bindings.size() != 2 || keep.arguments.size() != 2) {
        return false;  // the parser reported the shape
    }
    const auto& from = keep.arguments[0].spelling;
    const auto& to   = keep.arguments[1].spelling;
    const auto names = std::pair{bindings[0].name, bindings[1].name};
    if (std::pair{from, to} != names && std::pair{to, from} != names) {
        errors_.error(keep.location,
                      "the arguments of `keep " + keep.target_name + "(...)` must be the rule's two bindings '" +
                          names.first + "' and '" + names.second + "'");
        return false;
    }
    bool valid = true;
    for (const bool from_end : {true, false}) {
        const auto& argument = keep.arguments[from_end ? 0 : 1];
        const auto& end      = from_end ? relation.from : relation.to;
        const auto binding   = std::ranges::find(bindings, argument.spelling, &PairBindingNode::name);
        if (std::ranges::contains(binding->traits, std::optional(end.trait), &FilterEntry::resolved_trait_id)) {
            continue;
        }
        errors_.error(argument.location,
                      "'" + argument.spelling + "' must select `" + end.trait.local_name + "` to be the `" +
                          end_label(from_end) + "` end of `" + keep.target_name + "`");
        valid = false;
    }
    return valid;
}

// A keep run makes one row per tuple, and nothing bounds the tuples per entity.
void SemanticAnalyzer::reject_one_on_kept_relations(const ProgramNode& program, const KeepClauses& clauses) {
    for (const auto& decl : program.declarations) {
        const auto* node = std::get_if<RelationNode>(&decl);
        if (node == nullptr || !clauses.keepers.contains(make_symbol_id(SymbolKind::Relation, current_module_id_, node->name))) {
            continue;
        }
        for (const auto* end : {&node->from, &node->to}) {
            if (!end->one) {
                continue;
            }
            errors_.error(end->location,
                          "kept relation '" + node->name +
                              "' can't mark an end `one`; store the relation and add rows from a rule that proves "
                              "the limit, or keep a trait with `reduce per` and `best(...)`");
        }
    }
}

// ── Contracts ───────────────────────────────────────────────────────────────

void record_relation_read(HandlerContract& contract, const SymbolId& relation, const std::optional<std::string>& field) {
    contract.reads.insert(relation);
    auto& access = contract.read_fields[relation];
    if (field.has_value()) {
        access.fields.insert(*field);
    }
}

// A derived relation reads each relation it derives from, and `shortest` its cost field.
void SemanticAnalyzer::expand_derived_reads(HandlerContract& contract) const {
    std::vector<SymbolId> pending;
    std::ranges::copy_if(contract.reads, std::back_inserter(pending), [](const SymbolId& read) {
        return read.kind == SymbolKind::Relation;
    });
    while (!pending.empty()) {
        const auto* relation = find_resolved_relation(pending.back());
        pending.pop_back();
        if (relation == nullptr || !relation->derivation.has_value()) {
            continue;
        }
        const auto& derivation = *relation->derivation;
        if (!contract.reads.contains(derivation.source)) {
            pending.push_back(derivation.source);
        }
        record_relation_read(contract, derivation.source, derivation.cost_field);
    }
}

bool SemanticAnalyzer::record_row_field_read(const ExprNode& expr,
                                             const PairScope& pair_scope,
                                             HandlerContract& contract) {
    if (!std::holds_alternative<MemberExpr>(expr.expr)) {
        return false;
    }
    const auto dotted = dotted_name_of(expr);
    const auto first  = dotted.find('.');
    if (first == std::string::npos) {
        return false;
    }
    const auto binding = pair_scope.find(dotted.substr(0, first));
    if (binding == pair_scope.end() || !binding->second.relation.has_value()) {
        return false;
    }
    const auto member_name = dotted.substr(first + 1, dotted.find('.', first + 1) - first - 1);
    const bool end         = member_name == "from" || member_name == "to";
    record_relation_read(contract, *binding->second.relation, end ? std::nullopt : std::optional(member_name));
    return true;
}

// ── Stored relation commands ────────────────────────────────────────────────

void SemanticAnalyzer::validate_relation_command(
    const RelationCommand& command,
    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
    const std::unordered_map<std::string, TypeInfo>& locals,
    const ResolvedStruct* handler_event,
    const PairScope* pair_scope) {
    const bool adds      = command.fields != nullptr;
    const auto* relation = command.relation.has_value() ? find_resolved_relation(*command.relation) : nullptr;
    if (relation == nullptr) {
        errors_.error(command.location, "unknown relation '" + command.relation_name + "'");
        return;
    }
    if (relation->derivation.has_value()) {
        errors_.error(command.location, derived_read_only_message(*relation));
        return;
    }
    for (const auto* end : {command.from, command.to}) {
        if (end == nullptr) {
            continue;  // the parser reported the row shape
        }
        if (is_wildcard_pattern(*end)) {
            if (adds) {
                errors_.error(end->location, "`_` is allowed only in `remove` and in relation tests");
            }
            continue;
        }
        const auto type = infer_expr_type(*end, filter_bindings, locals, handler_event, pair_scope);
        if (type.kind != TypeKind::EntityId && type.kind != TypeKind::Unknown) {
            errors_.error(end->location, "relation row arguments must be of type 'entity_id', got '" + type.name + "'");
        }
    }
    if (adds) {
        validate_trait_field_supply(relation->fields,
                                    *command.fields,
                                    "`add " + command.relation_name + "(...)`",
                                    command.location,
                                    filter_bindings,
                                    locals,
                                    handler_event,
                                    pair_scope);
    }
}

// ── The `one` proof ─────────────────────────────────────────────────────────

namespace {

// Both name the same rule entity: `self`, or one binding.
bool same_entity(const ExprNode* left, const ExprNode* right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    if (std::holds_alternative<SelfExpr>(left->expr) && std::holds_alternative<SelfExpr>(right->expr)) {
        return true;
    }
    const auto* a = std::get_if<IdentExpr>(&left->expr);
    const auto* b = std::get_if<IdentExpr>(&right->expr);
    return a != nullptr && b != nullptr && a->name == b->name;
}

// Whether `predicate`'s top-level `and` chain holds `not R(e, _)` (from) or `not R(_, e)` (to).
bool and_chain_has_free_test(const ExprNode& predicate, const SymbolId& relation, const ExprNode& end, bool from) {
    if (const auto* binary = std::get_if<BinaryExpr>(&predicate.expr); binary != nullptr && binary->op == "and") {
        return and_chain_has_free_test(*binary->left, relation, end, from) ||
               and_chain_has_free_test(*binary->right, relation, end, from);
    }
    const auto* negation = std::get_if<UnaryExpr>(&predicate.expr);
    const auto* test     = negation != nullptr && negation->op == "not"
                               ? std::get_if<RelationTestExpr>(&negation->operand->expr)
                               : nullptr;
    if (test == nullptr || test->relation != relation) {
        return false;
    }
    return from ? test->to == nullptr && same_entity(test->from.get(), &end)
                : test->from == nullptr && same_entity(test->to.get(), &end);
}

std::string spelling(const ExprNode& expr) {
    if (std::holds_alternative<SelfExpr>(expr.expr)) {
        return "self";
    }
    const auto name = dotted_name_of(expr);
    return name.empty() ? "the argument" : name;
}

std::string free_test(const std::string& relation_name, const std::string& end, bool from) {
    return from ? "not " + relation_name + "(" + end + ", _)" : "not " + relation_name + "(_, " + end + ")";
}

}  // namespace

bool SemanticAnalyzer::is_fixed_row_argument(const RuleNode& rule, const ExprNode& argument) {
    if (std::holds_alternative<SelfExpr>(argument.expr) || std::holds_alternative<LiteralExpr>(argument.expr)) {
        return true;
    }
    if (const auto* member = std::get_if<MemberExpr>(&argument.expr)) {
        return member->resolved_entity_id.has_value() || member->resolved_const_id.has_value();
    }
    const auto* ident = std::get_if<IdentExpr>(&argument.expr);
    if (ident == nullptr) {
        return false;
    }
    const bool binding = rule.pairs.has_value() &&
                         std::ranges::contains(rule.pairs->bindings, ident->name, &PairBindingNode::name);
    return binding || ident->resolved_entity_id.has_value() || ident->resolved_const_id.has_value();
}

// The first condition of the proof `add` fails for the end it names, or nullopt when the end is proven.
std::optional<std::string> SemanticAnalyzer::unproven_one_end(const RuleNode& rule,
                                                              const EventHandlerNode& handler,
                                                              const AddRelationStmt& add,
                                                              bool from) {
    const auto& end   = from ? *add.from : *add.to;
    const auto& other = from ? *add.to : *add.from;
    const auto prefix = std::string("the `") + end_label(from) + "` end of `" + add.relation_name + "` is `one`";
    const auto* ident = std::get_if<IdentExpr>(&end.expr);
    const bool unary  = !rule.pairs.has_value() && !rule.rows.has_value() && !rule.filter.entries.empty();
    const bool pair_binding =
        ident != nullptr && rule.pairs.has_value() &&
        std::ranges::contains(rule.pairs->bindings, ident->name, &PairBindingNode::name);
    if (pair_binding) {
        const auto* part = rule.limit.has_value() ? rule.limit->part_for(ident->name) : nullptr;
        if (part == nullptr || !part->provably_one) {
            return prefix + " and `" + ident->name + "` is not limited to one";
        }
    } else if (!unary || !std::holds_alternative<SelfExpr>(end.expr)) {
        return prefix + " and `" + spelling(end) + "` is not `self` or a pair binding limited to one";
    }
    if (!is_fixed_row_argument(rule, other)) {
        return "the other argument of `add " + add.relation_name +
               "(...)` must be a pair binding, `self`, a named entity or a constant";
    }
    if (!handler.resolved_trigger.has_value() || handler.resolved_trigger->kind != HandlerTriggerKind::Phase) {
        return prefix + " and its handler runs on '" + handler.event_name +
               "', which can run more than once per activation; record the order as a trait and claim in a phase rule";
    }
    const auto predicates = rule.where_clause.has_value() ? std::span(rule.where_clause->predicates)
                                                           : std::span<const std::unique_ptr<ExprNode>>{};
    const bool tested = std::ranges::any_of(predicates, [&](const std::unique_ptr<ExprNode>& predicate) {
        return and_chain_has_free_test(*predicate, *add.resolved_relation_id, end, from);
    });
    if (!tested) {
        return prefix + " and `where:` lacks `" + free_test(add.relation_name, spelling(end), from) + "`";
    }
    return std::nullopt;
}

// The rule shape that proves every `one` end of `relation` for this add.
std::string SemanticAnalyzer::proven_one_form(const RuleNode& rule,
                                              const AddRelationStmt& add,
                                              const ResolvedRelation& relation) {
    std::vector<std::string> parts;
    std::vector<std::string> tests;
    for (const bool from : {true, false}) {
        if (!(from ? relation.from.one : relation.to.one)) {
            continue;
        }
        const auto end = spelling(from ? *add.from : *add.to);
        if (rule.pairs.has_value()) {
            parts.push_back("1 per " + end);
        }
        tests.push_back(free_test(add.relation_name, end, from));
    }
    std::string form = "a proven add of `" + add.relation_name + "` has ";
    if (!parts.empty()) {
        form += "`limit: " + parts.front() + (parts.size() > 1 ? ", " + parts.back() : "") + "`, ";
    }
    form += "`where: " + tests.front() + "`";
    if (tests.size() > 1) {
        form += " and `" + tests.back() + "`";
    }
    return form + ", and adds in a phase handler";
}

// The relation `add` targets when it is stored and has a `one` end, else null.
const ResolvedRelation* SemanticAnalyzer::one_end_relation(const AddRelationStmt& add) const {
    if (!add.resolved_relation_id.has_value() || add.from == nullptr || add.to == nullptr) {
        return nullptr;
    }
    const auto* relation = find_resolved_relation(*add.resolved_relation_id);
    if (relation == nullptr || (!relation->from.one && !relation->to.one) || relation->kept_by.has_value()) {
        return nullptr;
    }
    return relation;
}

void SemanticAnalyzer::check_one_add(const RuleNode& rule,
                                     const EventHandlerNode& handler,
                                     const AddRelationStmt& add,
                                     const ResolvedRelation& relation) {
    for (const bool from : {true, false}) {
        if (!(from ? relation.from.one : relation.to.one)) {
            continue;
        }
        if (const auto failure = unproven_one_end(rule, handler, add, from)) {
            errors_.error(add.location, *failure + "; " + proven_one_form(rule, add, relation));
            return;
        }
    }
}

// An `add R` of a relation with a `one` end must provably keep at most one row per entity on that end.
void SemanticAnalyzer::validate_one_ends(const ProgramNode& program) {
    OneAdders adders;
    for (const auto& decl : program.declarations) {
        const auto* rule = std::get_if<RuleNode>(&decl);
        if (rule == nullptr) {
            continue;
        }
        for (const auto& handler : rule->handlers) {
            const auto& trigger = handler.resolved_trigger;
            const auto phase    = trigger.has_value() && trigger->kind == HandlerTriggerKind::Phase
                                      ? std::optional(trigger->symbol.local_name)
                                      : std::nullopt;
            std::vector<const AddRelationStmt*> adds;
            collect_statements(handler.body, adds);
            for (const auto* add : adds) {
                const auto* relation = one_end_relation(*add);
                if (relation == nullptr) {
                    continue;
                }
                check_one_add(*rule, handler, *add, *relation);
                if (phase.has_value()) {
                    adders[{relation->canonical_id, *phase}].emplace_back(rule->name, add->location);
                }
            }
        }
    }
    report_two_one_adders(adders);
}

void SemanticAnalyzer::report_two_one_adders(const OneAdders& adders) {
    for (const auto& [key, sites] : adders) {
        const auto& first = sites.front().first;
        const auto other  = std::ranges::find_if(sites, [&](const auto& site) { return site.first != first; });
        if (other == sites.end()) {
            continue;
        }
        const auto relation = key.first.substr(key.first.rfind('.') + 1);
        errors_.error(other->second,
                      std::format("rules '{}' and '{}' both add `{}` in phase '{}'; two adders can't prove a `one` "
                                  "end, so add it from one rule",
                                  first,
                                  other->first,
                                  relation,
                                  key.second));
    }
}

bool SemanticAnalyzer::reject_row_limit_parts(const RuleNode& rule, const LimitClause& limit) {
    const auto& parts = limit.parts;
    const auto row    = std::ranges::find_if(parts, [&](const LimitPart& part) {
        const auto binding = std::ranges::find(rule.pairs->bindings, part.binding, &PairBindingNode::name);
        return binding != rule.pairs->bindings.end() && binding->is_row();
    });
    if (row == parts.end()) {
        return false;
    }
    if (parts.size() == 2) {
        errors_.error(limit.location,
                      "rule '" + rule.name + "': a two-sided limit needs two entity bindings; '" + row->binding +
                          "' binds relation rows");
    } else {
        errors_.error(row->location, "`per " + row->binding + "` names a relation row; a `per` part needs an entity binding");
    }
    return true;
}

}  // namespace cactus
