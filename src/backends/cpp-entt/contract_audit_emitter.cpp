#include "backends/cpp-entt/contract_audit_emitter.hpp"

#include "backends/cpp-entt/type_utils.hpp"
#include "frontend/native_field_reads.hpp"
#include "frontend/symbol_identity.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

namespace cactus::contract_audit {

namespace {

constexpr std::string_view kAudit = "cactus::runtime::entt_backend::";

std::string literal(std::string_view text) {
    return EnttCodegenUtils::string_literal_cpp(text);
}

std::string location_literal(const SourceLocation& location) {
    return literal(std::filesystem::path(location.filename).filename().string() + ":" + std::to_string(location.line) +
                   ":" + std::to_string(location.column));
}

std::string field_key(const SymbolId& trait, std::string_view field) {
    return make_canonical_id(trait) + "." + std::string{field};
}

std::string all_fields_key(const SymbolId& trait) {
    return make_canonical_id(trait) + ".*";
}

std::string command_entry(HandlerCommandKind kind, const std::optional<SymbolId>& target) {
    std::string entry = handler_command_kind_name(kind);
    return target.has_value() ? entry + " " + make_canonical_id(*target) : entry;
}

std::string set_literal(const std::set<std::string>& entries) {
    std::string out = "{";
    for (const auto& entry : entries) {
        out += (out.size() == 1 ? "" : ", ") + literal(entry);
    }
    return out + "}";
}

std::string map_literal(const std::map<std::string, std::string>& entries) {
    std::string out = "{";
    for (const auto& [key, value] : entries) {
        out += (out.size() == 1 ? "" : ", ") + std::string{"{"} + literal(key) + ", " + literal(value) + "}";
    }
    return out + "}";
}

std::set<std::string> read_keys(const HandlerContract& contract) {
    std::set<std::string> keys;
    for (const auto& trait : contract.reads) {
        const auto access = contract.read_fields.find(trait);
        if (access == contract.read_fields.end() || access->second.all) {
            keys.insert(all_fields_key(trait));
            continue;
        }
        for (const auto& field : access->second.fields) {
            keys.insert(field_key(trait, field));
        }
    }
    return keys;
}

std::map<std::string, std::string> write_keys(const HandlerContract& contract) {
    std::map<std::string, std::string> keys;
    for (const auto& trait : contract.writes) {
        const auto access = contract.write_fields.find(trait);
        if (access == contract.write_fields.end() || access->second.all) {
            keys.emplace(all_fields_key(trait), "General");
            continue;
        }
        for (const auto& field : access->second.fields) {
            keys.emplace(field_key(trait, field), write_kind_name(write_kind(contract, trait, field)));
        }
    }
    return keys;
}

// The containers whose `let` bounds a read ranged field's index takes.
void add_range_containers(const HandlerContract& contract, const DecoratedProgram& program, std::set<std::string>& keys) {
    for (const auto& [trait, access] : contract.read_fields) {
        const auto* declaration = EnttCodegenUtils::find_trait(program, make_canonical_id(trait));
        if (declaration == nullptr) {
            continue;
        }
        for (const auto& field : declaration->fields) {
            if (!field.range.has_value() || !access.covers(field.name)) {
                continue;
            }
            for (const auto& container : field.range->containers) {
                keys.insert(make_canonical_id(container));
            }
        }
    }
}

// Outside a native call a handler may fetch what it selects, reads, writes,
// projects or commands; the storage hook checks only these traits.
std::set<std::string> trait_keys(const HandlerContract& contract, const DecoratedProgram& program) {
    std::set<std::string> keys;
    const auto add = [&](const SymbolId& trait) { keys.insert(make_canonical_id(trait)); };
    std::ranges::for_each(contract.selection, add);
    for (const auto& binding : contract.pair_bindings) {
        std::ranges::for_each(binding.required_traits, add);
    }
    std::ranges::for_each(contract.reads, add);
    std::ranges::for_each(contract.writes, add);
    std::ranges::for_each(contract.projects, add);
    for (const auto& command : contract.commands) {
        if (command.target.has_value()) {
            add(*command.target);
        }
    }
    add_range_containers(contract, program, keys);
    return keys;
}

std::set<std::string> command_keys(const HandlerContract& contract) {
    std::set<std::string> keys;
    for (const auto& command : contract.commands) {
        keys.insert(command_entry(command.kind, command.target));
    }
    return keys;
}

std::set<std::string> effect_keys(const std::unordered_set<std::string>& observes,
                                  const std::unordered_set<std::string>& acts) {
    std::set<std::string> keys;
    for (const auto& domain : observes) {
        keys.insert("observe " + domain);
    }
    for (const auto& domain : acts) {
        keys.insert("act " + domain);
    }
    return keys;
}

std::set<std::string> symbol_keys(const std::unordered_set<SymbolId>& symbols) {
    std::set<std::string> keys;
    for (const auto& symbol : symbols) {
        keys.insert(make_canonical_id(symbol));
    }
    return keys;
}

// A trait whose every field is `let` changes only at commit, so any handler or native may read it.
std::set<std::string> let_trait_keys(const DecoratedProgram& program) {
    std::set<std::string> keys;
    for (const auto& [_, trait] : program.traits) {
        if (!trait.fields.empty() && std::ranges::all_of(trait.fields, &ResolvedField::is_let)) {
            keys.insert(trait.canonical_id);
        }
    }
    return keys;
}

// The stdlib traits generated code aliases to runtime types; the runtime library uses them too.
bool runtime_owned(const ResolvedTrait& trait) {
    return trait.module_name == "std.random";
}

std::optional<SymbolId> callee_of(const ExprNode& expr) {
    if (const auto* call = std::get_if<CallExpr>(&expr.expr)) {
        return call->resolved_callee_id;
    }
    if (const auto* query = std::get_if<QueryCallExpr>(&expr.expr)) {
        return query->resolved_callee_id;
    }
    return std::nullopt;
}

std::set<std::string> call_effects(const SymbolId& callee, const DecoratedProgram& program) {
    const auto* function = EnttCodegenUtils::find_func(program, callee);
    if (function == nullptr || !function->is_extern) {
        return {};
    }
    const auto effects = extern_call_effects(callee, *function);
    return effect_keys(effects.observes, effects.acts);
}

std::string check_call(std::string_view function, std::string_view entry, const SourceLocation& location) {
    return std::string{kAudit} + std::string{function} + "(" + literal(entry) + ", " + location_literal(location) + ")";
}

std::string command_check(HandlerCommandKind kind, const std::optional<SymbolId>& target, const SourceLocation& location) {
    return check_call("audit_command", command_entry(kind, target), location);
}

std::string write_check(const SymbolId& trait, std::string_view field, std::string_view kind, const SourceLocation& location) {
    return std::string{kAudit} + "audit_write(" + literal(field_key(trait, field)) + ", " + literal(kind) + ", " +
           location_literal(location) + ")";
}

std::optional<std::string> statement_check(const StmtNode& stmt) {
    return std::visit(
        [](const auto& node) -> std::optional<std::string> {
            using S = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<S, VarAssign>) {
                if (node.accumulation.has_value()) {
                    return write_check(node.accumulation->trait, node.accumulation->field, "Accum", node.location);
                }
                if (!node.field_write.has_value() || !node.field_write->field.has_value()) {
                    return std::nullopt;
                }
                return write_check(node.field_write->trait,
                                   *node.field_write->field,
                                   write_kind_name(assignment_write_kind(node)),
                                   node.location);
            } else if constexpr (std::is_same_v<S, EmitStmt>) {
                if (!node.resolved_event_id.has_value()) {
                    return std::nullopt;
                }
                return check_call("audit_emit", make_canonical_id(*node.resolved_event_id), node.location);
            } else if constexpr (std::is_same_v<S, SpawnStmt>) {
                return command_check(HandlerCommandKind::Spawn, node.resolved_template_id, node.location);
            } else if constexpr (std::is_same_v<S, DestroyStmt>) {
                return command_check(HandlerCommandKind::Destroy, std::nullopt, node.location);
            } else if constexpr (std::is_same_v<S, AddTraitStmt>) {
                return command_check(HandlerCommandKind::Add, node.resolved_trait_id, node.location);
            } else if constexpr (std::is_same_v<S, RemoveTraitStmt>) {
                return command_check(HandlerCommandKind::Remove, node.resolved_trait_id, node.location);
            } else if constexpr (std::is_same_v<S, SetTraitStmt>) {
                return command_check(HandlerCommandKind::Set, node.resolved_trait_id, node.location);
            } else if constexpr (std::is_same_v<S, AddRelationStmt>) {
                return command_check(HandlerCommandKind::Add, node.resolved_relation_id, node.location);
            } else if constexpr (std::is_same_v<S, RemoveRelationStmt>) {
                return command_check(HandlerCommandKind::Remove, node.resolved_relation_id, node.location);
            } else {
                return std::nullopt;
            }
        },
        stmt.stmt);
}

}  // namespace

std::string emit_trait_names(const DecoratedProgram& program) {
    if (!program.contract_audit) {
        return {};
    }
    std::map<std::string, std::string> names;
    for (const auto& [_, trait] : program.traits) {
        if (trait.fields.empty() || runtime_owned(trait)) {
            continue;
        }
        names.emplace(trait.canonical_id, EnttCodegenUtils::trait_cpp_name(trait.canonical_id, program));
    }
    std::ostringstream out;
    for (const auto& [canonical, cpp] : names) {
        out << "template <>\ninline constexpr std::string_view " << kAudit << "contract_audit_trait_name<" << cpp
            << "> = " << literal(canonical) << ";\n";
    }
    return out.str() + "\n";
}

std::string emit_tables(const DecoratedProgram& program) {
    if (!program.contract_audit) {
        return {};
    }
    std::ostringstream out;
    out << "namespace cactus_gen::detail {\n";
    out << "[[maybe_unused]] const bool contract_audit_tables = [] {\n";
    out << "#ifdef CACTUS_CONTRACT_AUDIT_NATIVES_FILE\n";
    out << "    " << kAudit << "set_contract_audit_natives_file(CACTUS_CONTRACT_AUDIT_NATIVES_FILE);\n";
    out << "#endif\n";
    out << "    " << kAudit << "contract_audit_state().let_traits = " << set_literal(let_trait_keys(program)) << ";\n";
    for (const auto& handler : program.execution_graph.handlers) {
        const auto& contract = handler.contract;
        out << "    {\n";
        out << "        auto& contract = " << kAudit << "contract_audit_contract("
            << literal(handler.identity.canonical_id()) << ");\n";
        out << "        contract.reads = " << set_literal(read_keys(contract)) << ";\n";
        out << "        contract.writes = " << map_literal(write_keys(contract)) << ";\n";
        out << "        contract.traits = " << set_literal(trait_keys(contract, program)) << ";\n";
        out << "        contract.emits = " << set_literal(symbol_keys(contract.emits)) << ";\n";
        out << "        contract.commands = " << set_literal(command_keys(contract)) << ";\n";
        out << "        contract.effects = " << set_literal(effect_keys(contract.effects, contract.effect_acts))
            << ";\n";
        out << "    }\n";
    }
    for (const auto native : summarized_natives()) {
        std::set<std::string> traits;
        for (const auto& read : *native_field_reads(native)) {
            traits.insert(std::string{read.module} + "." + std::string{read.trait});
        }
        out << "    " << kAudit << "contract_audit_native(" << literal(native) << ").traits = " << set_literal(traits)
            << ";\n";
    }
    out << "    return true;\n";
    out << "}();\n";
    out << "}  // namespace cactus_gen::detail\n\n";
    return out.str();
}

std::string emit_handler_scope(const HandlerIdentity& identity) {
    return "    const " + std::string{kAudit} + "ContractAuditHandlerScope cactus_audit_scope(" +
           literal(identity.canonical_id()) + ");\n";
}

std::string statement_checks(const StmtNode& stmt, const std::string& indent, const DecoratedProgram& program) {
    if (!program.contract_audit) {
        return {};
    }
    const auto check = statement_check(stmt);
    return check.has_value() ? indent + *check + ";\n" : std::string{};
}

std::string audited_expr(const ExprNode& expr, std::string lowered, const DecoratedProgram& program) {
    if (!program.contract_audit) {
        return lowered;
    }
    std::vector<std::string> checks;
    if (const auto callee = callee_of(expr); callee.has_value()) {
        const auto canonical = make_canonical_id(*callee);
        if (native_field_reads(canonical) != nullptr) {
            lowered = std::string{kAudit} + "audit_native_call(" + literal(canonical) + ", " +
                      location_literal(expr.location) + ", [&] { return " + lowered + "; })";
        }
        for (const auto& effect : call_effects(*callee, program)) {
            checks.push_back(check_call("audit_effect", effect, expr.location));
        }
    }
    if (const auto* spawn = std::get_if<SpawnExpr>(&expr.expr)) {
        checks.push_back(command_check(HandlerCommandKind::Spawn, spawn->resolved_template_id, expr.location));
    }
    if (expr.field_read.has_value() && expr.field_read->field.has_value()) {
        checks.push_back(check_call("audit_read", field_key(expr.field_read->trait, *expr.field_read->field), expr.location));
    }
    if (checks.empty()) {
        return lowered;
    }
    std::string out = "(";
    for (const auto& check : checks) {
        out += check + ", ";
    }
    return out + lowered + ")";
}

}  // namespace cactus::contract_audit
