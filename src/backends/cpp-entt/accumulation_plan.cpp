#include "backends/cpp-entt/accumulation_plan.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <map>
#include <ranges>

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

}  // namespace

AccumulationPlan::AccumulationPlan(const DecoratedProgram& program) {
    if (program.ast == nullptr) {
        return;
    }
    FieldWriterTable fields;
    for (const auto& declaration : program.ast->declarations) {
        const auto* rule = std::get_if<RuleNode>(&declaration);
        if (rule == nullptr || !rule->resolved_rule_id.has_value()) {
            continue;
        }
        for (const auto& handler : rule->handlers) {
            if (handler.resolved_trigger.has_value()) {
                const HandlerIdentity identity{.rule = *rule->resolved_rule_id, .trigger = *handler.resolved_trigger};
                collect_sites(handler.body, identity, false, fields);
            }
        }
    }
    for (auto& [key, writers] : fields) {
        const bool buffered = holds_floats(writers.target, program) && !deltas_never_meet(writers);
        for (const auto* statement : writers.statements) {
            if (statement->accumulation.has_value()) {
                statement->accumulation->buffered = buffered;
            }
        }
        if (!buffered) {
            continue;
        }
        const auto table = table_name(writers.target, program);
        tables_.push_back(table);
        for (const auto& handler : program.execution_graph.handlers) {
            if (touches_directly(handler.contract, writers.target) || may_move_storage(handler.contract)) {
                entry_flushes_[handler.identity].push_back(table);
            }
        }
    }
    std::ranges::sort(tables_);
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

std::string AccumulationPlan::table_name(const AccumulationTarget& target, const DecoratedProgram& program) {
    return "generated_accumulation__" +
           EnttCodegenUtils::trait_cpp_name(target.trait, target.trait.local_name, program) + "__" + target.field;
}

}  // namespace cactus
