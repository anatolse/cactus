#include "common/ast_expressions.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cactus {

namespace {

constexpr std::size_t kMaxDimensions = 4;

bool is_adjacent_call(const ExprNode& expr) {
    const auto* call  = std::get_if<CallExpr>(&expr.expr);
    const auto* ident = call == nullptr ? nullptr : std::get_if<IdentExpr>(&call->callee->expr);
    return ident != nullptr && ident->name == "adjacent";
}

bool names_ident(const ExprNode& expr, const std::string& name) {
    const auto* ident = std::get_if<IdentExpr>(&expr.expr);
    return ident != nullptr && ident->name == name;
}

bool reads_name(const ExprNode& expr, const std::string& name) {
    bool read = false;
    visit_expression(expr, [&](const ExprNode& node) { read = read || names_ident(node, name); });
    return read;
}

// `binding.member`, when `expr` is exactly that.
const std::string* member_of(const ExprNode& expr, const std::string& binding) {
    const auto* member = std::get_if<MemberExpr>(&expr.expr);
    return member != nullptr && names_ident(*member->object, binding) ? &member->member : nullptr;
}

struct Conjunct {
    const ExprNode* node = nullptr;
    ExprPath path;
};

// The operands of the `op` chain at `at` in `root`.
std::vector<Conjunct> flatten(const ExprNode& root, const ExprPath& at, std::string_view op) {
    std::vector<Conjunct> out;
    for (auto& path : chain_paths(root, at, op)) {
        const auto* node = resolve_expr_path(root, path);
        out.push_back(Conjunct{.node = node, .path = std::move(path)});
    }
    return out;
}

// What the equalities of one `and` chain fix about the second cell of a pair.
struct CellKeys {
    std::vector<std::optional<CellKeyPath>> dimensions;
    bool carrier = false;
    const ExprNode* other_carrier = nullptr;

    [[nodiscard]] bool complete() const {
        return carrier && std::ranges::all_of(dimensions, [](const auto& key) { return key.has_value(); });
    }
};

void apply_equality(const Conjunct& conjunct,
                    std::size_t predicate,
                    const ResolvedTrait& trait,
                    const std::string& first,
                    const std::string& second,
                    CellKeys& keys) {
    const auto* binary = std::get_if<BinaryExpr>(&conjunct.node->expr);
    if (binary == nullptr || binary->op != "==") {
        return;
    }
    for (std::uint8_t side = 0; side < 2; ++side) {
        const auto& fixed = side == 0 ? *binary->left : *binary->right;
        const auto& value = side == 0 ? *binary->right : *binary->left;
        const auto* member = member_of(fixed, second);
        if (member == nullptr || reads_name(value, second)) {
            continue;
        }
        if (*member == "carrier") {
            const auto* other = member_of(value, first);
            if (other != nullptr && *other == "carrier") {
                keys.carrier = true;
            } else {
                keys.other_carrier = conjunct.node;
            }
            return;
        }
        const auto found = std::ranges::find(trait.dimensions, *member, &ResolvedDimension::name);
        if (found == trait.dimensions.end()) {
            continue;
        }
        auto path = conjunct.path;
        path.push_back(static_cast<std::uint8_t>(1 - side));
        keys.dimensions[static_cast<std::size_t>(found - trait.dimensions.begin())] =
            CellKeyPath{.predicate = predicate, .path = std::move(path)};
        return;
    }
}

struct TopLevelBound {
    CellKeys keys;
    std::vector<std::pair<std::size_t, Conjunct>> disjunctions;
    std::optional<std::size_t> adjacent;
};

// The `adjacent` conjunct, the equalities of the top-level `and` chains, and the `or`s left to try.
TopLevelBound top_level_bound(const WhereClause& where, const ResolvedTrait& trait, const CellDomainNode& cells) {
    const auto& first  = cells.aliases.front();
    const auto& second = cells.aliases.back();
    TopLevelBound bound{.keys = CellKeys{.dimensions = std::vector<std::optional<CellKeyPath>>(trait.dimensions.size())},
                        .disjunctions = {},
                        .adjacent     = std::nullopt};
    for (std::size_t index = 0; index < where.predicates.size(); ++index) {
        for (const auto& conjunct : flatten(*where.predicates[index], {}, "and")) {
            const auto* call   = std::get_if<CallExpr>(&conjunct.node->expr);
            const auto* binary = std::get_if<BinaryExpr>(&conjunct.node->expr);
            if (call != nullptr && is_adjacent_call(*conjunct.node) && conjunct.path.empty() && call->args.size() == 2 &&
                names_ident(*call->args[0], first) && names_ident(*call->args[1], second)) {
                bound.adjacent = index;
            } else if (binary != nullptr && binary->op == "or") {
                bound.disjunctions.emplace_back(index, conjunct);
            } else {
                apply_equality(conjunct, index, trait, first, second, bound.keys);
            }
        }
    }
    return bound;
}

CellKeys merge(const CellKeys& base, const CellKeys& branch) {
    auto merged    = base;
    merged.carrier = base.carrier || branch.carrier;
    for (std::size_t index = 0; index < merged.dimensions.size(); ++index) {
        if (!merged.dimensions[index].has_value()) {
            merged.dimensions[index] = branch.dimensions[index];
        }
    }
    return merged;
}

std::vector<CellKeyPath> key_paths(const CellKeys& keys) {
    std::vector<CellKeyPath> paths;
    paths.reserve(keys.dimensions.size());
    for (const auto& key : keys.dimensions) {
        paths.push_back(*key);
    }
    return paths;
}

// `a.b.C` as an identifier and member chain.
std::unique_ptr<ExprNode> dotted_expression(const std::string& dotted, const SourceLocation& location) {
    std::unique_ptr<ExprNode> expr;
    std::size_t begin = 0;
    while (begin <= dotted.size()) {
        const auto end = std::min(dotted.find('.', begin), dotted.size());
        auto segment   = dotted.substr(begin, end - begin);
        if (expr == nullptr) {
            expr = std::make_unique<ExprNode>(
                ExprNode::Variant{IdentExpr{.name = std::move(segment), .location = location}}, location);
        } else {
            expr = std::make_unique<ExprNode>(
                ExprNode::Variant{MemberExpr{.object = std::move(expr), .member = std::move(segment), .location = location}},
                location);
        }
        begin = end + 1;
    }
    return expr;
}

std::string dimension_list(const ResolvedTrait& trait) {
    std::string names;
    for (const auto& dimension : trait.dimensions) {
        names += (names.empty() ? "" : ", ") + dimension.name;
    }
    return names;
}

// The expressions a statement uses as entities: event and command targets and relation ends.
std::vector<const ExprNode*> entity_targets(const StmtNode& stmt) {
    std::vector<const ExprNode*> targets;
    const auto add = [&](const ExprNode* target) {
        if (target != nullptr) {
            targets.push_back(target);
        }
    };
    std::visit(
        [&](const auto& node) {
            using S = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<S, EmitStmt>) {
                add(node.target.has_value() ? node.target->get() : nullptr);
            } else if constexpr (requires { node.target_expr.has_value(); }) {
                add(node.target_expr.has_value() ? node.target_expr->get() : nullptr);
            } else if constexpr (std::is_same_v<S, SetTraitStmt>) {
                add(node.target_expr.get());
            } else if constexpr (std::is_same_v<S, AddRelationStmt> || std::is_same_v<S, RemoveRelationStmt>) {
                for_each_relation_end(node, [&](const ExprNode& end) { add(&end); });
            }
        },
        stmt.stmt);
    return targets;
}

bool is_cell_value_type(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Bool:
        case TypeKind::Int:
        case TypeKind::Float:
        case TypeKind::Enum:
        case TypeKind::Vec2:
        case TypeKind::Vec3:
            return true;
        default:
            return false;
    }
}

}  // namespace

// ── Rules over cells ────────────────────────────────────────────────────────

void SemanticAnalyzer::extract_cell_domains(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* rule = std::get_if<RuleNode>(&decl)) {
            if (rule->pairs.has_value()) {
                extract_cell_pairs(*rule);
            } else {
                extract_cell_rows(*rule);
            }
        }
    }
}

const ResolvedTrait* SemanticAnalyzer::dimensioned_trait(const std::optional<SymbolId>& trait) const {
    const auto* declaration = trait.has_value() ? find_resolved_trait(*trait) : nullptr;
    return declaration != nullptr && declaration->is_dimensioned() ? declaration : nullptr;
}

// `each: T as c` with a dimensioned T: one row per carrier and cell.
void SemanticAnalyzer::extract_cell_rows(RuleNode& rule) {
    const FilterEntry* first = nullptr;
    for (const auto& entry : rule.filter.entries) {
        if (!entry.alias.has_value() || dimensioned_trait(entry.resolved_trait_id) == nullptr) {
            continue;
        }
        if (first != nullptr) {
            errors_.error(entry.location,
                          "`each:` can hold one aliased dimensioned trait; '" + first->qualified_name + " as " +
                              *first->alias + "' already gives the rows. Read '" + entry.qualified_name +
                              "' with a cell access 'self[" + entry.qualified_name +
                              "][...]', or pair cells in `pairs:`");
            continue;
        }
        first = &entry;
    }
    if (first == nullptr) {
        return;
    }
    rule.cells = CellDomainNode{.trait      = *first->resolved_trait_id,
                                .trait_name = first->qualified_name,
                                .aliases    = {*first->alias},
                                .location   = first->location};
}

// A `pairs:` rule whose bindings each hold only a dimensioned trait pairs cells of one carrier. It runs
// once per carrier, so it becomes a rule over the carriers with the two bindings as its cell aliases.
void SemanticAnalyzer::extract_cell_pairs(RuleNode& rule) {
    auto& bindings     = rule.pairs->bindings;
    const auto is_cell = [this](const PairBindingNode& binding) {
        return !binding.is_row() && binding.traits.size() == 1 && !binding.traits.front().alias.has_value() &&
               dimensioned_trait(binding.traits.front().resolved_trait_id) != nullptr;
    };
    const auto cell_count = std::ranges::count_if(bindings, is_cell);
    if (cell_count == 0 || bindings.size() != 2) {
        return;
    }
    if (cell_count == 1) {
        const auto& cell = is_cell(bindings[0]) ? bindings[0] : bindings[1];
        errors_.error(cell.location,
                      "cell binding '" + cell.name +
                          "' can't be paired with an entity or row binding; read cells with 'e[T][...]'");
        return;
    }
    const auto& left  = bindings[0];
    const auto& right = bindings[1];
    if (left.traits.front().resolved_trait_id != right.traits.front().resolved_trait_id) {
        errors_.error(right.location,
                      "both cell bindings of a pair must name one trait; '" + left.name + "' names '" +
                          left.traits.front().qualified_name + "' and '" + right.name + "' names '" +
                          right.traits.front().qualified_name + "'");
        return;
    }
    if (!right.exclude.entries.empty()) {
        errors_.error(right.exclude.location,
                      "`without:` belongs on the first cell binding '" + left.name +
                          "': both cells of a pair have one carrier");
    }
    const auto& entry = left.traits.front();
    const auto trait  = *entry.resolved_trait_id;
    const auto simple = entry.qualified_name.substr(entry.qualified_name.rfind('.') + 1);
    rule.cells        = CellDomainNode{.trait      = trait,
                                       .trait_name = entry.qualified_name,
                                       .aliases    = {left.name, right.name},
                                       .location   = rule.pairs->location};
    FilterClause filter;
    filter.location = rule.pairs->location;
    for (const auto& binding : bindings) {
        filter.entries.push_back(FilterEntry{.qualified_name    = entry.qualified_name,
                                             .resolved_trait_id = trait,
                                             .alias             = binding.name,
                                             .location          = binding.location});
    }
    filter.trait_names        = {simple};
    filter.resolved_trait_ids = {trait};
    rule.filter               = std::move(filter);
    rule.exclude              = std::move(bindings[0].exclude);
    rule.pairs.reset();
}

void SemanticAnalyzer::validate_trait_dimensions(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        auto* trait = std::get_if<TraitNode>(&decl);
        if (trait == nullptr || trait->dimensions.empty()) {
            continue;
        }
        auto resolved = result_.traits.find(trait->name);
        if (resolved == result_.traits.end()) {
            continue;
        }
        validate_trait_dimension_shape(*trait, resolved->second);
        validate_cell_fields(*trait, resolved->second);
    }
}

void SemanticAnalyzer::validate_trait_dimension_shape(TraitNode& trait, ResolvedTrait& resolved) {
    std::unordered_set<std::string> names;
    for (std::size_t index = 0; index < trait.dimensions.size(); ++index) {
        auto& dimension     = trait.dimensions[index];
        auto& resolved_dim  = resolved.dimensions[index];
        const auto qualified = trait.name + "." + dimension.name;
        if (index == kMaxDimensions) {
            errors_.error(dimension.location, "trait '" + trait.name + "' declares more than four dimensions; at most four dimensions are allowed");
        }
        if (!names.insert(dimension.name).second) {
            errors_.error(dimension.location, "trait '" + trait.name + "' declares '" + dimension.name + "' twice");
        }
        const bool is_enum = resolved_dim.type.kind == TypeKind::Enum && !dimension.range.has_value();
        const bool is_range = resolved_dim.type.kind == TypeKind::Int && dimension.range.has_value();
        if (!is_enum && !is_range) {
            errors_.error(dimension.location, "dimension '" + qualified + "' needs an enum or 'int in range(a, b)'");
            continue;
        }
        if (!is_range) {
            continue;
        }
        std::vector<SymbolId> containers;
        const bool lo_valid = validate_range_bound(*dimension.range->lo, trait, qualified, containers);
        const bool hi_valid = validate_range_bound(*dimension.range->hi, trait, qualified, containers);
        if (!containers.empty()) {
            errors_.error(dimension.range->location,
                          "range bound of '" + qualified + "' can read only literals, constants and 'let' fields of '" +
                              trait.name + "'");
            continue;
        }
        if (lo_valid && hi_valid) {
            resolved_dim.range =
                ResolvedFieldRange{.lo = clone_expression(*dimension.range->lo), .hi = clone_expression(*dimension.range->hi)};
        }
    }
    for (const auto& field : trait.fields) {
        if (names.contains(field.name)) {
            errors_.error(field.location, "trait '" + trait.name + "' declares '" + field.name + "' twice");
        }
    }
}

void SemanticAnalyzer::validate_cell_fields(const TraitNode& trait, const ResolvedTrait& resolved) {
    bool has_cell_field = false;
    for (const auto& field : trait.fields) {
        if (!field.modifiers.is_var) {
            continue;
        }
        has_cell_field       = true;
        const auto qualified = "cell field '" + trait.name + "." + field.name + "'";
        const auto found     = std::ranges::find(resolved.fields, field.name, &ResolvedField::name);
        const auto* type     = found == resolved.fields.end() ? nullptr : &*found;
        if (field.modifiers.is_persist) {
            errors_.error(field.location, qualified + " can't be `persist`; cells are not saved");
        }
        if (type != nullptr && !is_cell_value_type(type->type)) {
            errors_.error(field.location, qualified + " must be bool, int, float, an enum or a vector");
        } else if (!field.default_value.has_value()) {
            errors_.error(field.location, qualified + " needs a constant default");
        }
    }
    if (!has_cell_field) {
        errors_.error(trait.location,
                      "dimensioned trait '" + trait.name + "' needs a `var` field: each `var` field holds one value per cell");
    }
}

void SemanticAnalyzer::reject_cell_field_values(const ResolvedTrait& trait,
                                                const std::vector<FieldAssignment>& values) const {
    for (const auto& value : values) {
        reject_cell_field_value(trait, value.name, value.location);
    }
}

void SemanticAnalyzer::reject_cell_field_value(const ResolvedTrait& trait,
                                               const std::string& field,
                                               const SourceLocation& location) const {
    if (trait.find_cell_field(field) != nullptr) {
        errors_.error(location,
                      "cell field '" + trait.name + "." + field +
                          "' can't be given a value here; cells are written by handlers");
    }
}

// The dimensioned traits a unary rule's `each:` requires: those `self[T]` may name.
std::vector<SymbolId> SemanticAnalyzer::self_cell_traits(const RuleNode& rule) const {
    std::vector<SymbolId> traits;
    if (rule.pairs.has_value() || rule.rows.has_value()) {
        return traits;
    }
    for (const auto& entry : rule.filter.entries) {
        if (dimensioned_trait(entry.resolved_trait_id) != nullptr) {
            traits.push_back(*entry.resolved_trait_id);
        }
    }
    return traits;
}

bool SemanticAnalyzer::resolve_cell_short_form(TraitAccessExpr& access,
                                               const SourceLocation& location,
                                               const std::unordered_set<std::string>& shadowed,
                                               NamedAccessContext context) {
    const auto spelling = dotted_name_of(*access.object);
    if (spelling.empty()) {
        return false;
    }
    const auto root  = spelling.substr(0, spelling.find('.'));
    // A rule's domain puts its trait names among the shadowed names, so only a pair binding hides a trait here.
    const bool bound = access_pair_scope_ != nullptr && access_pair_scope_->contains(root);
    const auto trait = bound ? std::nullopt : try_resolve_trait_ref_to_symbol(spelling);
    const auto* declaration = trait.has_value() ? find_resolved_trait(*trait) : nullptr;
    if (declaration == nullptr) {
        if (access.cell && access.trait_name.empty()) {
            errors_.error(location, "a trait access takes exactly one trait name");
            return true;
        }
        return false;
    }
    if (context != NamedAccessContext::Rule) {
        errors_.error(location, "cells are read and written only in rules");
        return true;
    }
    if (!declaration->is_dimensioned()) {
        errors_.error(location,
                      "trait '" + spelling + "' has no dimensions; read a carrier's fields with 'e[" + spelling +
                          "].field'");
        return true;
    }
    if (!declaration->is_single) {
        errors_.error(location,
                      "the short form '" + spelling + "[...]' needs a single trait; name the carrier: 'e[" + spelling +
                          "][...]'");
        return true;
    }
    if (!access.trait_name.empty() && access.cell) {
        errors_.error(location, "the short form '" + spelling + "[...]' takes one bracket with every dimension");
        return true;
    }
    if (!access.trait_name.empty()) {
        access.cell_keys.push_back(dotted_expression(access.trait_name, location));
        resolve_named_access_expr(*access.cell_keys.back(), shadowed, context);
        access.cell = true;
    }
    access.trait_name        = spelling;
    access.resolved_trait_id = trait;
    access.short_form        = true;
    return true;
}

bool SemanticAnalyzer::resolve_self_cell_access(TraitAccessExpr& access) {
    if (!std::holds_alternative<SelfExpr>(access.object->expr)) {
        return false;
    }
    const auto trait = try_resolve_trait_ref_to_symbol(access.trait_name);
    if (!trait.has_value() || !std::ranges::contains(access_self_traits_, *trait)) {
        return false;
    }
    access.resolved_trait_id = trait;
    access.carrier_self      = true;
    return true;
}

void SemanticAnalyzer::check_cell_access(const TraitAccessExpr& access,
                                         const std::vector<std::string>& fields,
                                         const SourceLocation& location,
                                         const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                         const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                         const ResolvedStruct* handler_event,
                                         const PairScope* pair_scope) const {
    const auto* trait = access.resolved_trait_id.has_value() ? find_resolved_trait(*access.resolved_trait_id) : nullptr;
    if (trait == nullptr) {
        return;
    }
    if (!trait->is_dimensioned()) {
        if (access.cell) {
            errors_.error(location, "trait '" + trait->name + "' has no dimensions");
        }
        return;
    }
    const auto found     = fields.empty() ? trait->fields.end()
                                          : std::ranges::find(trait->fields, fields.front(), &ResolvedField::name);
    const auto* field    = found == trait->fields.end() ? nullptr : &*found;
    const auto qualified = "'" + trait->name + "." + (fields.empty() ? std::string{} : fields.front()) + "'";
    if (field != nullptr && trait->is_cell_field(*field) && !access.cell) {
        errors_.error(location,
                      qualified + " holds one value per cell; name the cell: '" + dotted_name_of(*access.object) + "[" +
                          trait->name + "][...]." + field->name + "'");
        return;
    }
    if (field != nullptr && !field->is_var && access.cell) {
        errors_.error(location, qualified + " belongs to the carrier; read it without a cell");
        return;
    }
    if (!access.cell) {
        return;
    }
    if (fields.empty()) {
        errors_.error(location, "a cell access needs a field after the brackets");
        return;
    }
    if (access.cell_keys.size() != trait->dimensions.size()) {
        errors_.error(location,
                      "'" + trait->name + "' has " + std::to_string(trait->dimensions.size()) + " dimensions (" +
                          dimension_list(*trait) + "); give one value for each");
        return;
    }
    for (std::size_t index = 0; index < access.cell_keys.size(); ++index) {
        const auto& dimension = trait->dimensions[index];
        const auto key =
            infer_expr_type(*access.cell_keys[index], filter_bindings, local_bindings, handler_event, pair_scope);
        const bool matches = key.kind == TypeKind::Unknown ||
                             (key.kind == dimension.type.kind &&
                              (key.kind != TypeKind::Enum || key.symbol_id == dimension.type.symbol_id));
        if (matches) {
            continue;
        }
        const auto expected = dimension.type.kind == TypeKind::Enum && dimension.type.symbol_id.has_value()
                                  ? dimension.type.symbol_id->local_name
                                  : dimension.type.name;
        errors_.error(access.cell_keys[index]->location,
                      "dimension '" + trait->name + "." + dimension.name + "' takes '" + expected + "'");
    }
}

// ── Checks on rules over cells ──────────────────────────────────────────────

std::optional<TypeInfo> SemanticAnalyzer::cell_alias_member_type(const ResolvedTrait& trait,
                                                                 const std::string& member) {
    if (!trait.is_dimensioned()) {
        return std::nullopt;
    }
    if (const auto* dimension = trait.find_dimension(member)) {
        return dimension->type;
    }
    if (member == "carrier" && !std::ranges::contains(trait.fields, member, &ResolvedField::name)) {
        return make_entity_id_type();
    }
    return std::nullopt;
}

bool SemanticAnalyzer::reads_cell_coordinate(const SymbolId& trait, const std::string& member) const {
    const auto* declaration = find_resolved_trait(trait);
    return declaration != nullptr && cell_alias_member_type(*declaration, member).has_value();
}

std::unordered_map<std::string, const ResolvedTrait*> SemanticAnalyzer::retained_cell_bindings(
    const RuleNode& rule) const {
    std::unordered_map<std::string, const ResolvedTrait*> bindings;
    if (!rule.cells.has_value() || !rule.reduce.has_value() || !rule.reduce->per_binding.has_value() ||
        !rule.cells->names(*rule.reduce->per_binding)) {
        return bindings;
    }
    bindings.emplace(*rule.reduce->per_binding, find_resolved_trait(rule.cells->trait));
    return bindings;
}

bool SemanticAnalyzer::valid_cell_group(const RuleNode& rule) const {
    const auto& cells  = *rule.cells;
    const auto& reduce = *rule.reduce;
    const auto& per    = *reduce.per_binding;
    if (!cells.pairs() && per != "self") {
        errors_.error(reduce.per_location,
                      "rule '" + rule.name + "' runs over cells; group them per carrier with `reduce per self:`");
        return false;
    }
    if (cells.pairs() && per != cells.aliases.front()) {
        errors_.error(reduce.per_location,
                      "a pair of cells groups by its first binding: write `reduce per " + cells.aliases.front() + ":`");
        return false;
    }
    return true;
}

void SemanticAnalyzer::validate_cell_rules(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        auto* rule = std::get_if<RuleNode>(&decl);
        if (rule == nullptr) {
            continue;
        }
        if (rule->cells.has_value()) {
            validate_cell_rule(*rule);
        }
        // `adjacent` is a bound, not a value: only the conjunct a cell pair rule recognized may hold it.
        const auto* consumed = rule->cells.has_value() && rule->cells->adjacent.has_value() &&
                                       rule->where_clause.has_value()
                                   ? rule->where_clause->predicates[*rule->cells->adjacent].get()
                                   : nullptr;
        for_each_rule_body_expression(*rule, [&](const ExprNode& expr) {
            visit_expression(expr, [&](const ExprNode& node) {
                if (is_adjacent_call(node) && &node != consumed) {
                    errors_.error(node.location,
                                  "`adjacent` is a `where:` condition of a pair of cells; write it as one line of "
                                  "`where:` in a `pairs:` rule whose bindings are cell bindings");
                }
            });
        });
    }
}

void SemanticAnalyzer::validate_cell_rule(RuleNode& rule) {
    const auto& cells = *rule.cells;
    if (!rule.order_by.empty()) {
        errors_.error(rule.order_by.front().location,
                      "a rule over cells can't declare `order by:`; cells run in dimension order");
    }
    if (rule.limit.has_value()) {
        errors_.error(rule.limit->location, "a rule over cells can't declare `limit:`; narrow its rows with `where:`");
    }
    if (rule.keep.has_value()) {
        errors_.error(rule.keep->location,
                      "a rule over cells can't keep: a cell is not an entity; keep from a rule over carriers that "
                      "reads 'self[" + cells.trait_name + "][...]'");
    }
    for (const auto& handler : rule.handlers) {
        if (handler.has_lifecycle_trigger()) {
            errors_.error(handler.trigger_location,
                          "a rule over cells can't run on a lifecycle trigger; a cell is never added or removed");
        }
        if (handler.resolved_trigger.has_value() && handler.resolved_trigger->kind == HandlerTriggerKind::RenderStage) {
            errors_.error(handler.trigger_location,
                          "a render pass draws entities; a vertex handler can't run over the cells of '" +
                              cells.trait_name + "'");
        }
    }
    if (cells.pairs()) {
        validate_cell_pair_bound(rule);
        reject_pair_self(rule);
    }
    reject_cell_values(rule);
    prove_rest_law(rule);
}

void SemanticAnalyzer::reject_pair_self(const RuleNode& rule) const {
    for_each_rule_body_expression(rule, [&](const ExprNode& root) {
        visit_expression(root, [&](const ExprNode& node) {
            if (std::holds_alternative<SelfExpr>(node.expr)) {
                errors_.error(node.location,
                              "a pair of cells has no `self`; use '" + rule.cells->aliases.front() + ".carrier'");
            }
        });
    });
}

void SemanticAnalyzer::validate_cell_pair_bound(RuleNode& rule) {
    auto& cells             = *rule.cells;
    const auto& first       = cells.aliases.front();
    const auto& second      = cells.aliases.back();
    const auto* trait       = find_resolved_trait(cells.trait);
    const auto unbounded    = [&] {
        errors_.error(cells.location,
                      "a pair of cells must be bounded: add `where: adjacent(" + first + ", " + second +
                          ")`, or equalities that fix '" + second + ".carrier' to '" + first +
                          ".carrier' and every dimension of '" + second + "'");
    };
    if (trait == nullptr) {
        return;
    }
    if (!rule.where_clause.has_value()) {
        unbounded();
        return;
    }
    auto [base, disjunctions, adjacent] = top_level_bound(*rule.where_clause, *trait, cells);
    cells.adjacent                      = adjacent;
    if (base.other_carrier != nullptr) {
        errors_.error(base.other_carrier->location,
                      "both cells of a pair must have one carrier: write '" + second + ".carrier == " + first +
                          ".carrier'");
        return;
    }
    if (cells.adjacent.has_value()) {
        return;
    }
    if (base.complete()) {
        cells.key_branches = {key_paths(base)};
        return;
    }
    for (const auto& [index, disjunction] : disjunctions) {
        const auto& root = *rule.where_clause->predicates[index];
        std::vector<std::vector<CellKeyPath>> found;
        for (const auto& branch : flatten(root, disjunction.path, "or")) {
            CellKeys keys{.dimensions = std::vector<std::optional<CellKeyPath>>(trait->dimensions.size())};
            for (const auto& conjunct : flatten(root, branch.path, "and")) {
                apply_equality(conjunct, index, *trait, first, second, keys);
            }
            const auto merged = merge(base, keys);
            if (!merged.complete() || keys.other_carrier != nullptr) {
                found.clear();
                break;
            }
            found.push_back(key_paths(merged));
        }
        if (!found.empty()) {
            cells.key_branches = std::move(found);
            return;
        }
    }
    unbounded();
}

// A cell alias or binding is not an entity, and only the `reduce per` binding of a pair of cells is written.
void SemanticAnalyzer::reject_cell_values(const RuleNode& rule) {
    const auto& cells   = *rule.cells;
    const auto retained = rule.reduce.has_value() ? rule.reduce->per_binding : std::nullopt;
    for (const auto& handler : rule.handlers) {
        for_each_nested_statement(handler.body, [&](const StmtNode& stmt) {
            for (const auto* target : entity_targets(stmt)) {
                const auto* ident = std::get_if<IdentExpr>(&target->expr);
                if (ident != nullptr && cells.names(ident->name)) {
                    errors_.error(target->location,
                                  "a cell is not an entity; use " + std::string(cells.pairs() ? "" : "'self' or ") + "'" +
                                      ident->name + ".carrier'");
                }
            }
            const auto* assign = std::get_if<VarAssign>(&stmt.stmt);
            const bool written = assign != nullptr && cells.pairs() && assign->target == nullptr &&
                                 cells.names(assign->name) && assign->name != retained.value_or("");
            if (written) {
                errors_.error(assign->location,
                              "cell binding '" + assign->name +
                                  "' is read-only; a pair of cells writes only its `reduce per` binding");
            }
        });
    }
}

}  // namespace cactus
