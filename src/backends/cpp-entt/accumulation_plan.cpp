#include "backends/cpp-entt/accumulation_plan.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <utility>

namespace cactus {

namespace {

struct HandlerSites {
    int count    = 0;
    bool in_loop = false;
    bool own_row = true;
};

struct FieldWriters {
    AccumulationTarget target;
    std::vector<const VarAssign*> statements;
    std::unordered_map<HandlerIdentity, HandlerSites, HandlerIdentityHash> handlers;
};

using FieldWriterTable = std::map<std::string, FieldWriters>;

void collect_sites(const std::vector<std::unique_ptr<StmtNode>>& body,
                   const HandlerIdentity& handler,
                   bool in_loop,
                   FieldWriterTable& fields);

void collect_statement(const StmtNode& statement, const HandlerIdentity& handler, bool in_loop, FieldWriterTable& fields) {
    if (const auto* assign = std::get_if<VarAssign>(&statement.stmt); assign != nullptr && assign->accumulation) {
        const auto& target = *assign->accumulation;
        auto& writers      = fields[make_canonical_id(target.trait) + "." + target.field];
        writers.target     = target;
        writers.statements.push_back(assign);
        auto& sites   = writers.handlers[handler];
        sites.count  += 1;
        sites.in_loop = sites.in_loop || in_loop;
        sites.own_row = sites.own_row && target.own_row;
    } else if (const auto* branch = std::get_if<IfStmt>(&statement.stmt)) {
        collect_sites(branch->then_body, handler, in_loop, fields);
        for (const auto& other : branch->else_if_branches) {
            collect_sites(other.body, handler, in_loop, fields);
        }
        collect_sites(branch->else_body, handler, in_loop, fields);
    } else if (const auto* match = std::get_if<MatchStmt>(&statement.stmt)) {
        for (const auto& arm : match->arms) {
            collect_sites(arm.body, handler, in_loop, fields);
        }
    } else if (const auto* loop = std::get_if<ForeachStmt>(&statement.stmt)) {
        collect_sites(loop->body, handler, true, fields);
    }
}

void collect_sites(const std::vector<std::unique_ptr<StmtNode>>& body,
                   const HandlerIdentity& handler,
                   bool in_loop,
                   FieldWriterTable& fields) {
    for (const auto& statement : body) {
        collect_statement(*statement, handler, in_loop, fields);
    }
}

bool holds_floats(const AccumulationTarget& target, const DecoratedProgram& program) {
    const auto* trait = EnttCodegenUtils::find_trait(program, make_canonical_id(target.trait));
    if (trait == nullptr) {
        trait = EnttCodegenUtils::find_trait(program, target.trait.local_name);
    }
    if (trait == nullptr) {
        return false;
    }
    const auto field = std::ranges::find(trait->fields, target.field, &ResolvedField::name);
    return field != trait->fields.end() &&
           (field->type.kind == TypeKind::Float || field->type.kind == TypeKind::Vec2 ||
            field->type.kind == TypeKind::Vec3);
}

// One writer that adds once per row of a phase pass, to the row itself.
bool deltas_never_meet(const FieldWriters& writers) {
    if (writers.handlers.size() != 1) {
        return false;
    }
    const auto& [handler, sites] = *writers.handlers.begin();
    return sites.count == 1 && !sites.in_loop && sites.own_row && handler.trigger.kind == HandlerTriggerKind::Phase;
}

// Whether `contract` reads or writes the field other than by accumulating into it.
bool touches_directly(const HandlerContract& contract, const AccumulationTarget& target) {
    if (write_kind(contract, target.trait, target.field).kind == WriteKind::Accum) {
        return false;
    }
    const bool reads =
        contract.reads.contains(target.trait) && field_access(contract.read_fields, target.trait).covers(target.field);
    const bool writes =
        contract.writes.contains(target.trait) && field_access(contract.write_fields, target.trait).covers(target.field);
    return reads || writes || contract.projects.contains(target.trait);
}

// Editor and unknown extern actions may create or destroy entities at once, moving component storage.
bool may_move_storage(const HandlerContract& contract) {
    return contract.effect_acts.contains("editor") || contract.effect_acts.contains("external");
}

// Whether some access of the field in `handler` has no settle call at its site.
bool needs_entry_flush(const HandlerNode& handler,
                       const AccumulationTarget& target,
                       const std::vector<TraitFieldRef>* untracked) {
    const auto& contract = handler.contract;
    if (handler.implementation == HandlerImplementationKind::External || untracked == nullptr ||
        may_move_storage(contract) || contract.projects.contains(target.trait) || contract.spatial_join.has_value() ||
        contract.reduction.has_value()) {
        return true;
    }
    return std::ranges::any_of(*untracked, [&](const TraitFieldRef& access) {
        return access.trait == target.trait && (!access.field.has_value() || *access.field == target.field);
    });
}

using HandlerNodes = std::unordered_map<HandlerIdentity, const EventHandlerNode*, HandlerIdentityHash>;

// Every Cactus handler by identity, with its accumulating statements collected into `fields`.
HandlerNodes collect_handlers(const ProgramNode& ast, FieldWriterTable& fields) {
    HandlerNodes handlers;
    for (const auto& declaration : ast.declarations) {
        const auto* rule = std::get_if<RuleNode>(&declaration);
        if (rule == nullptr || !rule->resolved_rule_id.has_value()) {
            continue;
        }
        for (const auto& handler : rule->handlers) {
            if (handler.resolved_trigger.has_value()) {
                const HandlerIdentity identity{.rule = *rule->resolved_rule_id, .trigger = *handler.resolved_trigger};
                collect_sites(handler.body, identity, false, fields);
                handlers.emplace(identity, &handler);
            }
        }
    }
    return handlers;
}

// Null for a handler with no Cactus body.
const std::vector<TraitFieldRef>* untracked_accesses(const HandlerNodes& handlers, const HandlerIdentity& identity) {
    const auto found = handlers.find(identity);
    return found == handlers.end() ? nullptr : &found->second->untracked_accesses;
}

const AccumulationPlan*& active_plan() {
    thread_local const AccumulationPlan* active = nullptr;
    return active;
}

}  // namespace

AccumulationPlan::AccumulationPlan(const DecoratedProgram& program) {
    if (program.ast == nullptr) {
        return;
    }
    FieldWriterTable fields;
    const auto handler_nodes = collect_handlers(*program.ast, fields);
    for (auto& writers : fields | std::views::values) {
        const bool buffered = holds_floats(writers.target, program) && !deltas_never_meet(writers);
        for (const auto* statement : writers.statements) {
            if (statement->accumulation.has_value()) {
                statement->accumulation->buffered = buffered;
            }
        }
        if (!buffered) {
            continue;
        }
        const auto table = table_name(writers.target.trait, writers.target.field, program);
        tables_.push_back(table);
        fields_.push_back(TraitFieldRef{.trait = writers.target.trait, .field = writers.target.field});
        for (const auto& handler : program.execution_graph.handlers) {
            if (touches_directly(handler.contract, writers.target) &&
                needs_entry_flush(handler, writers.target, untracked_accesses(handler_nodes, handler.identity))) {
                entry_flushes_[handler.identity].push_back(table);
            }
        }
    }
    if (tables_.empty()) {
        return;
    }
    for (const auto& handler : program.execution_graph.handlers) {
        if (may_move_storage(handler.contract)) {
            rebases_.insert(handler.identity);
        }
    }
}

bool AccumulationPlan::buffered(const TraitFieldRef& access) const {
    return access.field.has_value() && std::ranges::find(fields_, access) != fields_.end();
}

std::string AccumulationPlan::entry_flushes(const HandlerIdentity& handler) const {
    const auto found = entry_flushes_.find(handler);
    if (found == entry_flushes_.end()) {
        return {};
    }
    std::string text;
    for (const auto& table : found->second) {
        text += "    ::" + table + "().flush();\n";
    }
    return text;
}

std::string AccumulationPlan::exit_rebase(const HandlerIdentity& handler) const {
    return rebases_.contains(handler) ? "    ::generated_rebase_accumulations(registry);\n" : std::string{};
}

std::string AccumulationPlan::table_name(const SymbolId& trait,
                                         const std::string& field,
                                         const DecoratedProgram& program) {
    return "generated_accumulation__" + EnttCodegenUtils::trait_cpp_name(trait, trait.local_name, program) + "__" +
           field;
}

AccumulationScope::AccumulationScope(const AccumulationPlan& plan)
    : previous_(std::exchange(active_plan(), &plan)) {}

AccumulationScope::~AccumulationScope() {
    active_plan() = previous_;
}

const AccumulationPlan* AccumulationScope::active() {
    return active_plan();
}

}  // namespace cactus
