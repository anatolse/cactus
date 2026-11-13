#include "frontend/parallel_batches.hpp"

#include "common/ast_expressions.hpp"
#include "common/host_facades.hpp"

#include <algorithm>
#include <ranges>

namespace cactus {

namespace {

[[nodiscard]] bool overlaps(const FieldAccess& left, const FieldAccess& right) {
    return left.all || right.all ||
           std::ranges::any_of(left.fields, [&](const std::string& field) { return right.fields.contains(field); });
}

// Whether `writer` writes a field that `other` reads or writes.
[[nodiscard]] bool writes_what_other_touches(const HandlerContract& writer, const HandlerContract& other) {
    return std::ranges::any_of(writer.writes, [&](const SymbolId& trait) {
        const auto& written = field_access(writer.write_fields, trait);
        return (other.reads.contains(trait) && overlaps(written, field_access(other.read_fields, trait))) ||
               (other.writes.contains(trait) && overlaps(written, field_access(other.write_fields, trait)));
    });
}

[[nodiscard]] bool conflicts(const HandlerNode& left, const HandlerNode& right) {
    return writes_what_other_touches(left.contract, right.contract) ||
           writes_what_other_touches(right.contract, left.contract);
}

// Native systems may change registry structure directly, and a projecting
// handler moves component storage during its pass: neither shares a batch.
[[nodiscard]] bool runs_alone(const HandlerNode& handler) {
    return handler.implementation != HandlerImplementationKind::Cactus || !handler.contract.projects.empty();
}

[[nodiscard]] bool looks_up_keyed_relation(const HandlerContract& contract, const DecoratedProgram& program) {
    return std::ranges::any_of(program.relations | std::views::values, [&](const ResolvedRelation& relation) {
        return relation.derived_by_key() && relation.symbol_id.has_value() && contract.reads.contains(*relation.symbol_id);
    });
}

[[nodiscard]] const EventHandlerNode* find_handler_body(const HandlerIdentity& identity, const DecoratedProgram& program) {
    if (program.ast == nullptr) {
        return nullptr;
    }
    for (const auto& declaration : program.ast->declarations) {
        const auto* rule = std::get_if<RuleNode>(&declaration);
        if (rule == nullptr || rule->resolved_rule_id != identity.rule) {
            continue;
        }
        const auto handler = std::ranges::find(rule->handlers, std::optional{identity.trigger},
                                               &EventHandlerNode::resolved_trigger);
        return handler == rule->handlers.end() ? nullptr : &*handler;
    }
    return nullptr;
}

// Whether `body`, or a function it calls, calls a host facade such as `input.pressed(Jump)`.
[[nodiscard]] bool calls_host_facade(const std::vector<std::unique_ptr<StmtNode>>& body,
                                     const ProgramNode& ast,
                                     std::vector<const FuncNode*>& visited) {
    bool found = false;
    std::vector<const FuncNode*> callees;
    for_each_statement_expression(body, [&](const ExprNode& root) {
        visit_expression(root, [&](const ExprNode& node) {
            const auto* call = std::get_if<CallExpr>(&node.expr);
            if (call == nullptr) {
                return;
            }
            const auto* member = std::get_if<MemberExpr>(&call->callee->expr);
            const auto* object = member == nullptr ? nullptr : std::get_if<IdentExpr>(&member->object->expr);
            found |= object != nullptr && is_host_facade(object->name);
            const auto* func = call->resolved_callee_id.has_value() ? find_func_decl(ast, *call->resolved_callee_id) : nullptr;
            if (func != nullptr && !std::ranges::contains(visited, func)) {
                visited.push_back(func);
                callees.push_back(func);
            }
        });
    });
    return found || std::ranges::any_of(callees, [&](const FuncNode* func) {
               return calls_host_facade(func->body, ast, visited);
           });
}

// The contract does not record host state a facade call reads.
[[nodiscard]] bool calls_host_facade(const HandlerNode& handler, const DecoratedProgram& program) {
    const auto* body = find_handler_body(handler.identity, program);
    if (body == nullptr) {
        return true;
    }
    std::vector<const FuncNode*> visited;
    return calls_host_facade(body->body, *program.ast, visited);
}

[[nodiscard]] const HandlerNode* find_handler(const ExecutionGraph& graph, const HandlerIdentity& identity) {
    const auto found = std::ranges::find(graph.handlers, identity, &HandlerNode::identity);
    return found == graph.handlers.end() ? nullptr : &*found;
}

struct OpenBatch {
    std::vector<const HandlerNode*> members;
    bool closed = false;
};

// Greedy in stable order: each handler joins the first batch it shares no written field with.
[[nodiscard]] std::vector<ParallelBatch> split_level(const DependencyLevel& level, const ExecutionGraph& graph) {
    std::vector<OpenBatch> open;
    for (const auto& identity : level.handlers) {
        const auto* handler = find_handler(graph, identity);
        if (handler == nullptr) {
            continue;
        }
        if (runs_alone(*handler)) {
            open.push_back(OpenBatch{.members = {handler}, .closed = true});
            continue;
        }
        const auto fits = [&](const OpenBatch& batch) {
            return !batch.closed && std::ranges::none_of(batch.members, [&](const HandlerNode* member) {
                       return conflicts(*member, *handler);
                   });
        };
        if (const auto batch = std::ranges::find_if(open, fits); batch != open.end()) {
            batch->members.push_back(handler);
        } else {
            open.push_back(OpenBatch{.members = {handler}, .closed = false});
        }
    }
    std::vector<ParallelBatch> batches;
    batches.reserve(open.size());
    for (const auto& batch : open) {
        auto& added = batches.emplace_back();
        for (const auto* member : batch.members) {
            added.handlers.push_back(member->identity);
        }
    }
    return batches;
}

}  // namespace

const ParallelLevel* ParallelPlan::level_of(const HandlerIdentity& handler) const {
    const auto holds = [&](const ParallelLevel& level) {
        return std::ranges::any_of(level.batches, [&](const ParallelBatch& batch) {
            return std::ranges::contains(batch.handlers, handler);
        });
    };
    const auto found = std::ranges::find_if(levels, holds);
    return found == levels.end() ? nullptr : &*found;
}

// A write of a link field moves its holder in the link's shared index.
bool writes_link(const HandlerContract& contract, const DecoratedProgram& program) {
    return std::ranges::any_of(program.traits | std::views::values, [&](const ResolvedTrait& trait) {
        const auto written = trait.symbol_id.has_value() ? contract.write_fields.find(*trait.symbol_id)
                                                         : contract.write_fields.end();
        return written != contract.write_fields.end() &&
               std::ranges::any_of(trait.fields, [&](const ResolvedField& field) {
                   return is_link_type(field.type) && written->second.covers(field.name);
               });
    });
}

bool parallel_eligible(const HandlerNode& handler, const DecoratedProgram& program) {
    const auto& contract = handler.contract;
    const bool spawns    = !contract.native_spawns.empty() ||
                        std::ranges::contains(contract.commands, HandlerCommandKind::Spawn, &InferredHandlerCommand::kind);
    return !runs_alone(handler) && handler.identity.trigger.kind == HandlerTriggerKind::Phase &&
           contract.effects.empty() && !spawns && !looks_up_keyed_relation(contract, program) &&
           !calls_host_facade(handler, program) && !writes_link(contract, program);
}

ParallelPlan plan_parallel_handlers(const DecoratedProgram& program) {
    const auto& graph = program.execution_graph;
    ParallelPlan plan;
    for (const auto& handler : graph.handlers) {
        if (parallel_eligible(handler, program)) {
            plan.eligible.insert(handler.identity);
        }
    }
    for (const auto& level : graph.dependency_levels) {
        if (level.activation.kind != HandlerTriggerKind::Phase) {
            continue;
        }
        auto batches          = split_level(level, graph);
        const auto concurrent = [&](const ParallelBatch& batch) {
            return batch.handlers.size() > 1 && std::ranges::any_of(batch.handlers, [&](const HandlerIdentity& member) {
                       return plan.eligible.contains(member);
                   });
        };
        if (std::ranges::any_of(batches, concurrent)) {
            plan.levels.push_back(
                ParallelLevel{.index = level.index, .batches = std::move(batches)});
        }
    }
    return plan;
}

}  // namespace cactus
