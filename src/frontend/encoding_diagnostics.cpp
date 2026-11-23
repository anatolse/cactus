#include "frontend/encoding_diagnostics.hpp"

#include "common/ast_expressions.hpp"
#include "common/error_reporter.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <utility>
#include <vector>

namespace cactus {

namespace {

// A trait's canonical id and one of its field names; an empty field name stands for the whole trait.
using FieldKey = std::pair<std::string, std::string>;
using Handlers = std::vector<const HandlerNode*>;

// `x.h` read as a condition: the flag and the spelling of the value it is read on.
struct Guard {
    FieldKey flag;
    std::string object;

    auto operator<=>(const Guard&) const = default;
};

enum class ReadForm : std::uint8_t { Plain, Filter, Join };

struct Mark {
    ReadForm form = ReadForm::Plain;
    std::vector<SymbolId> join_traits;  // Join: the traits the joined pair binding requires
};

struct ReadSite {
    Handlers handlers;
    std::string object;
    std::set<Guard> guards;
    Mark mark;
};

// A value and the rule it is written in, which gives `self`, pair bindings and `best` reducers their meaning.
struct ValueSite {
    const ExprNode* value = nullptr;
    const RuleNode* rule  = nullptr;
};

struct AssignmentList {
    const std::vector<FieldAssignment>* assignments = nullptr;
    const RuleNode* rule                            = nullptr;
};

using AssignmentLists = std::unordered_map<std::string, std::vector<AssignmentList>>;

std::string object_of(const ExprNode& node) {
    const auto* member = std::get_if<MemberExpr>(&node.expr);
    return member == nullptr ? std::string{} : expression_spelling(*member->object, "?");
}

void collect_conjuncts(const ExprNode& expr, std::vector<const ExprNode*>& out) {
    const auto* binary = std::get_if<BinaryExpr>(&expr.expr);
    if (binary != nullptr && binary->op == "and") {
        collect_conjuncts(*binary->left, out);
        collect_conjuncts(*binary->right, out);
        return;
    }
    out.push_back(&expr);
}

void collect_conjuncts(const std::vector<std::unique_ptr<ExprNode>>& predicates, std::vector<const ExprNode*>& out) {
    for (const auto& predicate : predicates) {
        if (predicate != nullptr) {
            collect_conjuncts(*predicate, out);
        }
    }
}

const ExprNode* negated_operand(const ExprNode& expr) {
    const auto* unary = std::get_if<UnaryExpr>(&expr.expr);
    return unary != nullptr && unary->op == "not" ? unary->operand.get() : nullptr;
}

bool can_return(const StmtNode& stmt) {
    bool found = false;
    for_each_nested_statement(stmt, [&](const StmtNode& nested) {
        found = found || std::holds_alternative<ReturnStmt>(nested.stmt);
    });
    return found;
}

bool is_return_only(const std::vector<std::unique_ptr<StmtNode>>& body) {
    return body.size() == 1 && body.front() != nullptr && std::holds_alternative<ReturnStmt>(body.front()->stmt);
}

bool is_gen_zero(const HandlerNode& handler) {
    return handler.identity.trigger.is_phase_or_render_stage();
}

bool is_discrete(const HandlerNode& handler) {
    const auto kind = handler.identity.trigger.kind;
    return kind == HandlerTriggerKind::Event || handler.identity.trigger.is_lifecycle();
}

std::optional<FieldKey> write_key(const VarAssign& assign) {
    if (!assign.field_write.has_value() || !assign.field_write->field.has_value()) {
        return std::nullopt;
    }
    return FieldKey{make_canonical_id(assign.field_write->trait), *assign.field_write->field};
}

void append_unique(Handlers& handlers, const HandlerNode* handler) {
    if (!std::ranges::contains(handlers, handler)) {
        handlers.push_back(handler);
    }
}

void add_list(AssignmentLists& lists,
              const SymbolId& trait,
              const std::vector<FieldAssignment>& assignments,
              const RuleNode* rule) {
    lists[make_canonical_id(trait)].push_back({.assignments = &assignments, .rule = rule});
}

void add_lists(AssignmentLists& lists, const std::vector<ArchetypeTraitEntry>& entries, const RuleNode* rule) {
    for (const auto& entry : entries) {
        if (entry.resolved_trait_id.has_value()) {
            add_list(lists, *entry.resolved_trait_id, entry.assignments, rule);
        }
    }
}

// Calls `visit` on every expression of a rule's clauses: where:, when:, reduce:, limit:, order by: and keep.
template <typename Visit>
void for_each_clause_expression(const RuleNode& rule, const Visit& visit) {
    const auto each = [&](const std::vector<std::unique_ptr<ExprNode>>& predicates) {
        for (const auto& predicate : predicates) {
            visit(*predicate);
        }
    };
    if (rule.where_clause.has_value()) {
        each(rule.where_clause->predicates);
    }
    if (rule.when_clause.has_value()) {
        each(rule.when_clause->predicates);
    }
    if (rule.reduce.has_value()) {
        for_each_reducer_expr(*rule.reduce, visit);
    }
    if (rule.limit.has_value()) {
        for_each_limit_count(*rule.limit, visit);
    }
    for (const auto& key : rule.order_by) {
        if (key.expression != nullptr) {
            visit(*key.expression);
        }
    }
    if (!rule.keep.has_value()) {
        return;
    }
    if (rule.keep->condition != nullptr) {
        visit(*rule.keep->condition);
    }
    for (const auto& field : rule.keep->fields) {
        if (field.value != nullptr) {
            visit(*field.value);
        }
    }
}

std::string fitting_forms(bool state, bool projected) {
    if (state && projected) {
        return "a `state` variant or a projected trait";
    }
    return state ? "a `state` variant" : "a projected trait";
}

class EncodingCheck {
public:
    explicit EncodingCheck(const DecoratedProgram& program)
        : program_(&program) {
        for (const auto& trait : program.traits | std::views::values) {
            if (trait.symbol_id.has_value()) {
                traits_.emplace(make_canonical_id(*trait.symbol_id), &trait);
            }
        }
        for (const auto& func : program.funcs | std::views::values) {
            if (func.symbol_id.has_value()) {
                funcs_.emplace(make_canonical_id(*func.symbol_id), &func);
            }
        }
        for (const auto& phase : program.phases | std::views::values) {
            if (phase.symbol_id.has_value()) {
                phases_.emplace(make_canonical_id(*phase.symbol_id), &phase);
            }
        }
        for (const auto& handler : program.execution_graph.handlers) {
            handlers_.emplace(handler.identity, &handler);
            for (const auto& [trait, access] : handler.contract.write_fields) {
                writes_[make_canonical_id(trait)].emplace_back(&handler, &access);
            }
        }
        for (const auto& edge : program.execution_graph.schedule_edges) {
            schedule_.emplace(edge.before, edge.after);
        }
    }

    void run(ErrorReporter& errors) {
        if (program_->ast == nullptr) {
            return;
        }
        walk_declarations();
        add_unexplained_reads();
        derive_roots();
        for (const auto& [canonical, trait] : traits_) {
            if (trait->state.has_value() || trait->is_dimensioned() || trait->lost_link.has_value()) {
                continue;
            }
            for (const auto& field : trait->fields) {
                check_field(canonical, *trait, field, errors);
            }
        }
    }

private:
    // ── Declarations ────────────────────────────────────────────────────────

    void walk_declarations() {
        std::string module;
        for (const auto& declaration : program_->ast->declarations) {
            if (const auto* node = std::get_if<ModuleNode>(&declaration)) {
                module = node->name;
            } else if (const auto* rule = std::get_if<RuleNode>(&declaration)) {
                walk_rule(*rule);
            } else if (const auto* trait = std::get_if<TraitNode>(&declaration)) {
                for (const auto& field : trait->fields) {
                    field_locations_[{module + "." + trait->name, field.name}] = field.location;
                }
            } else if (const auto* entity = std::get_if<EntityNode>(&declaration)) {
                if (entity->resolved_entity_id.has_value()) {
                    entities_.emplace(make_canonical_id(*entity->resolved_entity_id), entity);
                }
                add_lists(making_sites_, entity->traits, nullptr);
                add_child_overrides(entity->child_overrides, nullptr);
                for (const auto& child : entity->children) {
                    add_lists(making_sites_, child.traits, nullptr);
                }
            } else if (const auto* node = std::get_if<TemplateNode>(&declaration)) {
                if (node->resolved_template_id.has_value()) {
                    templates_.emplace(make_canonical_id(*node->resolved_template_id), node);
                }
                add_lists(making_sites_, node->traits, nullptr);
                for (const auto& child : node->children) {
                    add_lists(making_sites_, child.traits, nullptr);
                }
            }
        }
    }

    void add_child_overrides(const std::vector<ChildOverrideNode>& children, const RuleNode* rule) {
        for (const auto& child : children) {
            add_lists(overrides_, child.traits, rule);
            add_child_overrides(child.children, rule);
        }
    }

    // ── Reads ───────────────────────────────────────────────────────────────

    [[nodiscard]] const ResolvedField* field_of(const FieldKey& key) const {
        const auto trait = traits_.find(key.first);
        if (trait == traits_.end()) {
            return nullptr;
        }
        const auto found = std::ranges::find(trait->second->fields, key.second, &ResolvedField::name);
        return found == trait->second->fields.end() ? nullptr : &*found;
    }

    [[nodiscard]] static std::optional<FieldKey> key_of(const ExprNode& node) {
        if (!node.field_read.has_value() || !node.field_read->field.has_value()) {
            return std::nullopt;
        }
        return FieldKey{make_canonical_id(node.field_read->trait), *node.field_read->field};
    }

    // `x.h` for a `bool` field `h`.
    [[nodiscard]] std::optional<Guard> flag_read(const ExprNode& node) const {
        const auto key = key_of(node);
        if (!key.has_value() || !std::holds_alternative<MemberExpr>(node.expr)) {
            return std::nullopt;
        }
        const auto* field = field_of(*key);
        if (field == nullptr || field->type.kind != TypeKind::Bool) {
            return std::nullopt;
        }
        return Guard{.flag = *key, .object = object_of(node)};
    }

    [[nodiscard]] std::set<Guard> with_flag_guards(const ExprNode& condition, std::set<Guard> guards) const {
        std::vector<const ExprNode*> parts;
        collect_conjuncts(condition, parts);
        for (const auto* part : parts) {
            if (const auto guard = flag_read(*part)) {
                guards.insert(*guard);
            }
        }
        return guards;
    }

    void record(const ExprNode& expr, const std::set<Guard>& guards, const Handlers& handlers) {
        visit_expression(expr, [&](const ExprNode& node) {
            if (const auto key = key_of(node)) {
                const auto mark = marks_.find(&node);
                reads_[*key].push_back(ReadSite{.handlers = handlers,
                                                .object   = object_of(node),
                                                .guards   = guards,
                                                .mark     = mark == marks_.end() ? Mark{} : mark->second});
                note_seen(handlers, *key);
            }
            if (const auto* call = std::get_if<CallExpr>(&node.expr)) {
                record_parameter_reads(*call, guards, handlers);
            }
        });
    }

    void note_seen(const Handlers& handlers, const FieldKey& key) {
        for (const auto* handler : handlers) {
            seen_[handler].insert(key);
        }
    }

    // A trait argument reads, at the call, the fields its parameter reads in the callee.
    void record_parameter_reads(const CallExpr& call, const std::set<Guard>& guards, const Handlers& handlers) {
        if (!call.resolved_callee_id.has_value()) {
            return;
        }
        const auto func = funcs_.find(make_canonical_id(*call.resolved_callee_id));
        if (func == funcs_.end()) {
            return;
        }
        const auto& params = func->second->params;
        for (std::size_t index = 0; index < call.args.size(); ++index) {
            const bool named = index < call.arg_names.size() && !call.arg_names[index].empty();
            const auto param = named ? std::ranges::find(params, call.arg_names[index], &ResolvedParam::name)
                                     : params.begin() + static_cast<std::ptrdiff_t>(std::min(index, params.size()));
            if (param == params.end() || param->type.kind != TypeKind::TraitRef || !param->type.symbol_id.has_value()) {
                continue;
            }
            const auto trait = make_canonical_id(*param->type.symbol_id);
            const auto owner = traits_.find(trait);
            if (owner == traits_.end()) {
                continue;
            }
            const auto object = expression_spelling(*call.args[index], "?");
            for (const auto& field : owner->second->fields) {
                if (!param->reads.covers(field.name)) {
                    continue;
                }
                const FieldKey key{trait, field.name};
                reads_[key].push_back(ReadSite{.handlers = handlers, .object = object, .guards = guards});
                note_seen(handlers, key);
            }
        }
    }

    // The linked handlers of a rule, with their bodies.
    std::vector<std::pair<const EventHandlerNode*, const HandlerNode*>> linked_handlers(const RuleNode& rule) {
        std::vector<std::pair<const EventHandlerNode*, const HandlerNode*>> bodies;
        for (const auto& handler : rule.handlers) {
            if (!handler.resolved_trigger.has_value()) {
                continue;
            }
            const auto found =
                handlers_.find(HandlerIdentity{.rule = *rule.resolved_rule_id, .trigger = *handler.resolved_trigger});
            if (found != handlers_.end()) {
                bodies.emplace_back(&handler, found->second);
                handler_bodies_.emplace(found->second, &handler);
            }
        }
        return bodies;
    }

    void walk_rule(const RuleNode& rule) {
        if (!rule.resolved_rule_id.has_value()) {
            return;
        }
        const auto bodies = linked_handlers(rule);
        if (bodies.empty()) {
            return;
        }
        Handlers handlers;
        std::ranges::transform(bodies, std::back_inserter(handlers), [](const auto& entry) { return entry.second; });
        std::set<Guard> guards;
        mark_clause(rule, guards);
        for_each_clause_expression(rule, [&](const ExprNode& expr) { record(expr, guards, handlers); });
        for (const auto& [body, handler] : bodies) {
            walk_body(body->body, guards, Handlers{handler}, &rule);
        }
    }

    // Top-level `where:`/`when:` conjuncts: guards, filters and joins.
    void mark_clause(const RuleNode& rule, std::set<Guard>& guards) {
        std::vector<const ExprNode*> parts;
        if (rule.where_clause.has_value()) {
            collect_conjuncts(rule.where_clause->predicates, parts);
        }
        if (rule.when_clause.has_value()) {
            collect_conjuncts(rule.when_clause->predicates, parts);
        }
        for (const auto* part : parts) {
            if (const auto guard = flag_read(*part)) {
                guards.insert(*guard);
                marks_[part] = Mark{.form = ReadForm::Filter};
                continue;
            }
            if (const auto* operand = negated_operand(*part); operand != nullptr && flag_read(*operand).has_value()) {
                marks_[operand] = Mark{.form = ReadForm::Filter};
                continue;
            }
            mark_join(rule, *part);
        }
    }

    void mark_join(const RuleNode& rule, const ExprNode& part) {
        const auto* equality = std::get_if<BinaryExpr>(&part.expr);
        if (equality == nullptr || equality->op != "==" || !rule.pairs.has_value()) {
            return;
        }
        for (const auto& [field_side, binding_side] :
             {std::pair{equality->left.get(), equality->right.get()}, {equality->right.get(), equality->left.get()}}) {
            const auto key     = key_of(*field_side);
            const auto* field  = key.has_value() ? field_of(*key) : nullptr;
            const auto* ident  = std::get_if<IdentExpr>(&binding_side->expr);
            const auto* traits = ident == nullptr ? nullptr : binding_traits(rule, ident->name);
            if (field == nullptr || field->type.kind != TypeKind::EntityId || traits == nullptr) {
                continue;
            }
            Mark mark{.form = ReadForm::Join};
            for (const auto& entry : *traits) {
                if (entry.resolved_trait_id.has_value()) {
                    mark.join_traits.push_back(*entry.resolved_trait_id);
                }
            }
            marks_[field_side] = std::move(mark);
        }
    }

    // The trait entries of the entity pair binding `name`; null for anything else.
    [[nodiscard]] static const std::vector<FilterEntry>* binding_traits(const RuleNode& rule, const std::string& name) {
        if (!rule.pairs.has_value()) {
            return nullptr;
        }
        const auto& bindings = rule.pairs->bindings;
        const auto found     = std::ranges::find(bindings, name, &PairBindingNode::name);
        return found == bindings.end() || found->is_row() ? nullptr : &found->traits;
    }

    void walk_body(const std::vector<std::unique_ptr<StmtNode>>& body,
                   std::set<Guard> guards,
                   const Handlers& handlers,
                   const RuleNode* rule) {
        for (const auto& stmt : body) {
            if (stmt == nullptr) {
                continue;
            }
            if (const auto* branch = std::get_if<IfStmt>(&stmt->stmt)) {
                walk_if(*branch, guards, handlers, rule);
                continue;
            }
            std::visit([&](const auto& node) { walk_statement(node, guards, handlers, rule); }, stmt->stmt);
        }
    }

    template <typename Node>
    void walk_statement(const Node& node, const std::set<Guard>& guards, const Handlers& handlers, const RuleNode* rule) {
        const auto visit = [&](const ExprNode& expr) { record(expr, guards, handlers); };
        if constexpr (std::is_same_v<Node, VarAssign>) {
            if (node.value != nullptr) {
                visit(*node.value);
            }
            note_assignment(node, handlers, rule);
        } else {
            statement_children::visit_statement_expressions(node, visit);
        }
        note_trait_values(node, handlers, rule);
        if constexpr (requires { node.iterable; }) {
            walk_body(node.body, guards, handlers, rule);
        }
        if constexpr (requires { node.arms; }) {
            for (const auto& arm : node.arms) {
                walk_body(arm.body, guards, handlers, rule);
            }
        }
    }

    // The field values a statement gives a trait: spawn overrides, `add`/`project` blocks and `set` patches.
    template <typename Node>
    void note_trait_values(const Node& node, const Handlers& handlers, const RuleNode* rule) {
        if constexpr (requires { node.overrides; }) {
            add_lists(overrides_, node.overrides, rule);
            add_child_overrides(node.child_overrides, rule);
        }
        if constexpr (std::is_same_v<Node, AddTraitStmt> || std::is_same_v<Node, ProjectTraitStmt>) {
            if (node.resolved_trait_id.has_value()) {
                add_list(making_sites_, *node.resolved_trait_id, node.args, rule);
            }
        }
        if constexpr (std::is_same_v<Node, SetTraitStmt>) {
            if (!node.resolved_trait_id.has_value()) {
                return;
            }
            add_list(overrides_, *node.resolved_trait_id, node.args, rule);
            const FieldKey whole{make_canonical_id(*node.resolved_trait_id), ""};
            for (const auto* handler : handlers) {
                explained_writes_[handler].insert(whole);
            }
        }
    }

    void walk_if(const IfStmt& branch, std::set<Guard>& guards, const Handlers& handlers, const RuleNode* rule) {
        if (branch.condition == nullptr) {
            return;
        }
        record(*branch.condition, guards, handlers);
        const auto* negated = negated_operand(*branch.condition);
        const auto gate     = negated == nullptr ? std::nullopt : flag_read(*negated);
        if (gate.has_value() && is_return_only(branch.then_body) && branch.else_if_branches.empty() &&
            branch.else_body.empty()) {
            guards.insert(*gate);
            return;
        }
        walk_body(branch.then_body, with_flag_guards(*branch.condition, guards), handlers, rule);
        for (const auto& else_if : branch.else_if_branches) {
            if (else_if.condition == nullptr) {
                continue;
            }
            record(*else_if.condition, guards, handlers);
            walk_body(else_if.body, with_flag_guards(*else_if.condition, guards), handlers, rule);
        }
        walk_body(branch.else_body, guards, handlers, rule);
    }

    void note_assignment(const VarAssign& assign, const Handlers& handlers, const RuleNode* rule) {
        const auto key = write_key(assign);
        if (!key.has_value() || assign.value == nullptr) {
            return;
        }
        assignments_[*key].push_back({.value = assign.value.get(), .rule = rule});
        for (const auto* handler : handlers) {
            explained_writes_[handler].insert(*key);
        }
    }

    // A read the handler's contract records but no walked site explains, such as an extern rule's.
    // Contract reads include a write's own read of its target, so a field the handler writes is skipped.
    void add_unexplained_reads() {
        static const FieldAccess kAll{.all = true};
        for (const auto& handler : program_->execution_graph.handlers) {
            const auto& contract = handler.contract;
            const auto& seen     = seen_[&handler];
            for (const auto& trait : contract.reads) {
                const auto owner = traits_.find(make_canonical_id(trait));
                if (owner == traits_.end()) {
                    continue;
                }
                const auto read    = contract.read_fields.find(trait);
                const auto written = contract.write_fields.find(trait);
                const auto& access = read == contract.read_fields.end() ? kAll : read->second;
                for (const auto& field : owner->second->fields) {
                    const FieldKey key{owner->first, field.name};
                    const bool writes = written != contract.write_fields.end() && written->second.covers(field.name);
                    if (access.covers(field.name) && !writes && !seen.contains(key)) {
                        reads_[key].push_back(ReadSite{.handlers = {&handler}});
                    }
                }
            }
        }
    }

    // ── Timing ──────────────────────────────────────────────────────────────

    // The roots of the activations a handler can run in: phases, external events, startup.
    void derive_roots() {
        std::unordered_map<HandlerIdentity, Handlers, HandlerIdentityHash> producers;
        const auto add_producers = [&](const auto& flows) {
            for (const auto& flow : flows) {
                if (const auto found = handlers_.find(flow.producer); found != handlers_.end()) {
                    producers[flow.consumer].push_back(found->second);
                }
            }
        };
        add_producers(program_->execution_graph.event_flows);
        add_producers(program_->execution_graph.lifecycle_flows);
        for (const auto& handler : program_->execution_graph.handlers) {
            auto& roots = roots_[&handler];
            if (is_gen_zero(handler) || producers[handler.identity].empty()) {
                roots.insert(handler.identity.trigger.source_spelling());
            }
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& handler : program_->execution_graph.handlers) {
                if (is_gen_zero(handler)) {
                    continue;
                }
                auto& roots = roots_[&handler];
                for (const auto* producer : producers[handler.identity]) {
                    for (const auto& root : roots_[producer]) {
                        changed = roots.insert(root).second || changed;
                    }
                }
            }
        }
    }

    [[nodiscard]] bool share_activation(const HandlerNode* left, const HandlerNode* right) const {
        const auto& a = roots_.at(left);
        const auto& b = roots_.at(right);
        return std::ranges::any_of(a, [&](const std::string& root) { return b.contains(root); });
    }

    [[nodiscard]] bool scheduled_after(const HandlerNode* writer, const HandlerNode* reader) const {
        std::unordered_set<HandlerIdentity, HandlerIdentityHash> visited;
        std::deque<HandlerIdentity> queue{writer->identity};
        while (!queue.empty()) {
            const auto current = queue.front();
            queue.pop_front();
            const auto [first, last] = schedule_.equal_range(current);
            for (auto edge = first; edge != last; ++edge) {
                if (edge->second == reader->identity) {
                    return true;
                }
                if (visited.insert(edge->second).second) {
                    queue.push_back(edge->second);
                }
            }
        }
        return false;
    }

    // Whether phase `later` runs after phase `earlier` in the same frame.
    [[nodiscard]] bool phase_after(const std::string& earlier, const std::string& later) const {
        const auto phase = phases_.find(later);
        return phase != phases_.end() && std::ranges::any_of(phase->second->upstream_phases, [&](const SymbolId& id) {
                   return make_canonical_id(id) == earlier;
               });
    }

    [[nodiscard]] Handlers writers_of(const FieldKey& key) const {
        Handlers writers;
        const auto found = writes_.find(key.first);
        if (found == writes_.end()) {
            return writers;
        }
        for (const auto& [handler, access] : found->second) {
            if (access->covers(key.second)) {
                writers.push_back(handler);
            }
        }
        return writers;
    }

    [[nodiscard]] Handlers writers_of(const std::vector<FieldKey>& keys) const {
        Handlers writers;
        for (const auto& key : keys) {
            for (const auto* writer : writers_of(key)) {
                append_unique(writers, writer);
            }
        }
        return writers;
    }

    [[nodiscard]] Handlers readers_of(const std::vector<FieldKey>& keys) const {
        Handlers readers;
        for (const auto& key : keys) {
            for (const auto& site : sites_of(key)) {
                for (const auto* handler : site.handlers) {
                    append_unique(readers, handler);
                }
            }
        }
        return readers;
    }

    // Every writer of the flag runs on an event or a lifecycle trigger, and no reader can follow one in its activation.
    [[nodiscard]] bool state_fits(const Handlers& flag_writers, const Handlers& readers) const {
        if (!std::ranges::all_of(flag_writers, [](const HandlerNode* writer) { return is_discrete(*writer); })) {
            return false;
        }
        return std::ranges::all_of(readers, [&](const HandlerNode* reader) {
            return is_gen_zero(*reader) || std::ranges::none_of(flag_writers, [&](const HandlerNode* writer) {
                       return share_activation(reader, writer);
                   });
        });
    }

    // Every write is in handlers of one phase, one sets the flag on every path, and readers follow them.
    [[nodiscard]] bool projected_fits(const FieldKey& flag,
                                      const Handlers& flag_writers,
                                      const Handlers& all_writers,
                                      const Handlers& readers) const {
        if (flag_writers.empty() || all_writers.empty()) {
            return false;
        }
        const auto& phase = all_writers.front()->identity.trigger;
        const auto in_phase = [&](const HandlerNode* handler) {
            return handler->identity.trigger.kind == HandlerTriggerKind::Phase && handler->identity.trigger == phase;
        };
        if (!std::ranges::all_of(all_writers, in_phase) ||
            !std::ranges::any_of(flag_writers, [&](const HandlerNode* writer) { return sets_on_every_path(writer, flag); })) {
            return false;
        }
        const auto phase_name = make_canonical_id(phase.symbol);
        // A projection lasts the frame, so a later repetition of a periodic phase would still see it.
        if (const auto found = phases_.find(phase_name); found == phases_.end() || found->second->has_every) {
            return false;
        }
        return std::ranges::all_of(readers, [&](const HandlerNode* reader) {
            if (in_phase(reader)) {
                return std::ranges::all_of(flag_writers, [&](const HandlerNode* writer) {
                    return writer != reader && scheduled_after(writer, reader);
                });
            }
            return reader->identity.trigger.kind == HandlerTriggerKind::Phase &&
                   phase_after(phase_name, make_canonical_id(reader->identity.trigger.symbol));
        });
    }

    // The handler's body assigns the flag at its top level before any statement that can return.
    [[nodiscard]] bool sets_on_every_path(const HandlerNode* handler, const FieldKey& flag) const {
        const auto body = handler_bodies_.find(handler);
        if (body == handler_bodies_.end()) {
            return false;
        }
        for (const auto& stmt : body->second->body) {
            if (stmt == nullptr) {
                continue;
            }
            if (const auto* assign = std::get_if<VarAssign>(&stmt->stmt); assign != nullptr && write_key(*assign) == flag) {
                return true;
            }
            if (can_return(*stmt)) {
                return false;
            }
        }
        return false;
    }

    // ── Warnings ────────────────────────────────────────────────────────────

    [[nodiscard]] SourceLocation location_of(const FieldKey& key) const {
        const auto found = field_locations_.find(key);
        return found == field_locations_.end() ? SourceLocation{} : found->second;
    }

    void check_field(const std::string& canonical,
                     const ResolvedTrait& trait,
                     const ResolvedField& field,
                     ErrorReporter& errors) {
        const FieldKey key{canonical, field.name};
        if (field.type.kind == TypeKind::Bool) {
            if (field.is_let && check_filter_only(key, trait, errors)) {
                return;
            }
            check_flag(key, trait, field, errors);
            return;
        }
        if (field.type.kind == TypeKind::EntityId && !is_link_type(field.type)) {
            check_join(key, trait, field, errors);
        }
    }

    [[nodiscard]] const std::vector<ReadSite>& sites_of(const FieldKey& key) const {
        static const std::vector<ReadSite> kNone;
        const auto found = reads_.find(key);
        return found == reads_.end() ? kNone : found->second;
    }

    bool check_filter_only(const FieldKey& key, const ResolvedTrait& trait, ErrorReporter& errors) const {
        const auto& sites = sites_of(key);
        if (sites.empty() || !std::ranges::all_of(sites, [](const ReadSite& site) {
                return site.mark.form == ReadForm::Filter;
            })) {
            return false;
        }
        if (!writers_of(key).empty()) {
            return false;
        }
        errors.warning(location_of(key),
                       "'" + trait.name + "." + key.second +
                           "' only filters rules in where: or when:; make it a marker trait that rules select with "
                           "each: or without:");
        return true;
    }

    void check_flag(const FieldKey& flag,
                    const ResolvedTrait& trait,
                    const ResolvedField& field,
                    ErrorReporter& errors) const {
        std::vector<FieldKey> keys{flag};
        for (const auto& other : trait.fields) {
            const FieldKey key{flag.first, other.name};
            const auto& sites = sites_of(key);
            const bool all_guarded =
                !sites.empty() && key != flag && std::ranges::all_of(sites, [&](const ReadSite& site) {
                    return site.guards.contains(Guard{.flag = flag, .object = site.object});
                });
            if (all_guarded) {
                keys.push_back(key);
            }
        }
        const auto guarded = keys | std::views::drop(1);
        if (guarded.empty()) {
            return;
        }
        const auto flag_writers = writers_of(flag);
        // A `var` flag no handler writes is set outside this program, such as a game setting a library field.
        if (flag_writers.empty() && !field.is_let) {
            return;
        }
        const auto readers   = readers_of(keys);
        const bool state     = state_fits(flag_writers, readers);
        const bool projected = projected_fits(flag, flag_writers, writers_of(keys), readers);
        if (!state && !projected) {
            return;
        }
        const bool one   = guarded.size() == 1;
        const auto names = spelled_list(guarded, [](const FieldKey& key) { return "'" + key.second + "'"; });
        errors.warning(location_of(flag),
                       (one ? "field " : "fields ") + names + " of '" + trait.name + "' " + (one ? "is" : "are") +
                           " read only where '" + flag.second + "' is true; move " + (one ? "it" : "them") +
                           " into a trait whose presence means '" + flag.second +
                           "': " + fitting_forms(state, projected));
    }

    // ── Joined handles ──────────────────────────────────────────────────────

    void check_join(const FieldKey& key,
                    const ResolvedTrait& trait,
                    const ResolvedField& field,
                    ErrorReporter& errors) const {
        const auto& sites = sites_of(key);
        if (sites.empty() || !std::ranges::all_of(sites, [](const ReadSite& site) {
                return site.mark.form == ReadForm::Join;
            })) {
            return;
        }
        const auto& candidates = sites.front().mark.join_traits;
        const auto target      = std::ranges::find_if(candidates, [&](const SymbolId& candidate) {
            return std::ranges::all_of(
                sites, [&](const ReadSite& site) { return std::ranges::contains(site.mark.join_traits, candidate); });
        });
        if (target == candidates.end() || !assigned_only_links(key, *target)) {
            return;
        }
        errors.warning(location_of(key),
                       "'" + trait.name + "." + key.second + "' is only joined with '==' against a binding that "
                           "requires '" + target->local_name + "'; declare it as a link: '" +
                           (field.is_let ? "let " : "var ") + key.second + ": " + target->local_name +
                           "', and say what happens when the target is gone with `on lost " + trait.name + "." +
                           key.second + "`");
    }

    [[nodiscard]] bool assigned_only_links(const FieldKey& key, const SymbolId& target) const {
        const FieldKey whole{key.first, ""};
        for (const auto* writer : writers_of(key)) {
            const auto found = explained_writes_.find(writer);
            if (found == explained_writes_.end() || (!found->second.contains(key) && !found->second.contains(whole))) {
                return false;
            }
        }
        if (const auto found = assignments_.find(key); found != assignments_.end()) {
            for (const auto& site : found->second) {
                if (!carries(*site.value, target, site.rule)) {
                    return false;
                }
            }
        }
        const auto lists_assign = [&](const std::vector<AssignmentList>& lists, bool required) {
            return std::ranges::all_of(lists, [&](const AssignmentList& list) {
                const auto entry = std::ranges::find(*list.assignments, key.second, &FieldAssignment::name);
                if (entry == list.assignments->end()) {
                    return !required;
                }
                return entry->value != nullptr && carries(*entry->value, target, list.rule);
            });
        };
        const auto making = making_sites_.find(key.first);
        if (making != making_sites_.end() && !lists_assign(making->second, true)) {
            return false;
        }
        const auto patched = overrides_.find(key.first);
        return patched == overrides_.end() || lists_assign(patched->second, false);
    }

    // Whether `value` names an entity that carries `target` wherever it is evaluated.
    [[nodiscard]] bool carries(const ExprNode& value, const SymbolId& target, const RuleNode* rule) const {
        if (value.field_read.has_value() && value.field_read->field.has_value()) {
            const auto* field = field_of({make_canonical_id(value.field_read->trait), *value.field_read->field});
            return field != nullptr && is_link_type(field->type) && *field->type.symbol_id == target;
        }
        if (std::holds_alternative<SelfExpr>(value.expr)) {
            return rule != nullptr && std::ranges::any_of(rule->filter.entries, [&](const FilterEntry& entry) {
                       return entry.resolved_trait_id == target;
                   });
        }
        if (const auto* member = std::get_if<MemberExpr>(&value.expr)) {
            return member->resolved_entity_id.has_value() && entity_carries(*member->resolved_entity_id, target);
        }
        const auto* ident = std::get_if<IdentExpr>(&value.expr);
        if (ident == nullptr) {
            return false;
        }
        if (ident->resolved_entity_id.has_value()) {
            return entity_carries(*ident->resolved_entity_id, target);
        }
        if (rule == nullptr) {
            return false;
        }
        const auto requires_target = [&](const std::string& name) {
            const auto* entries = binding_traits(*rule, name);
            return entries != nullptr && std::ranges::any_of(*entries, [&](const FilterEntry& entry) {
                       return entry.resolved_trait_id == target;
                   });
        };
        if (requires_target(ident->name)) {
            return true;
        }
        return rule->reduce.has_value() && std::ranges::any_of(rule->reduce->reducers, [&](const ReducerDecl& reducer) {
                   const auto* input = reducer.input == nullptr ? nullptr : std::get_if<IdentExpr>(&reducer.input->expr);
                   return reducer.name == ident->name && reducer.kind == ReducerKind::Best && input != nullptr &&
                          requires_target(input->name);
               });
    }

    [[nodiscard]] bool entity_carries(const SymbolId& entity, const SymbolId& target) const {
        const auto found = entities_.find(make_canonical_id(entity));
        if (found == entities_.end()) {
            return false;
        }
        const auto& node      = *found->second;
        const auto in_entries = [&](const std::vector<ArchetypeTraitEntry>& entries) {
            return std::ranges::any_of(
                entries, [&](const ArchetypeTraitEntry& entry) { return entry.resolved_trait_id == target; });
        };
        const auto in_template = [&](const std::optional<SymbolId>& id) {
            const auto blueprint = id.has_value() ? templates_.find(make_canonical_id(*id)) : templates_.end();
            return blueprint != templates_.end() && in_entries(blueprint->second->traits);
        };
        return in_entries(node.traits) || in_template(node.resolved_template_ref_id) ||
               std::ranges::any_of(node.template_uses, [&](const ArchetypeTemplateUseEntry& use) {
                   return in_template(use.resolved_template_id);
               });
    }

    const DecoratedProgram* program_;
    std::map<std::string, const ResolvedTrait*> traits_;
    std::unordered_map<std::string, const ResolvedFunc*> funcs_;
    std::unordered_map<std::string, const ResolvedPhase*> phases_;
    std::unordered_map<std::string, const EntityNode*> entities_;
    std::unordered_map<std::string, const TemplateNode*> templates_;
    std::unordered_map<HandlerIdentity, const HandlerNode*, HandlerIdentityHash> handlers_;
    std::unordered_map<std::string, std::vector<std::pair<const HandlerNode*, const FieldAccess*>>> writes_;
    std::unordered_multimap<HandlerIdentity, HandlerIdentity, HandlerIdentityHash> schedule_;
    std::unordered_map<const HandlerNode*, const EventHandlerNode*> handler_bodies_;
    std::map<FieldKey, SourceLocation> field_locations_;
    std::unordered_map<const ExprNode*, Mark> marks_;
    std::map<FieldKey, std::vector<ReadSite>> reads_;
    std::unordered_map<const HandlerNode*, std::set<FieldKey>> seen_;
    std::unordered_map<const HandlerNode*, std::set<std::string>> roots_;
    std::map<FieldKey, std::vector<ValueSite>> assignments_;
    std::unordered_map<const HandlerNode*, std::set<FieldKey>> explained_writes_;
    AssignmentLists making_sites_;
    AssignmentLists overrides_;
};

}  // namespace

void check_encodings(const DecoratedProgram& program, ErrorReporter& errors) {
    EncodingCheck(program).run(errors);
}

}  // namespace cactus
