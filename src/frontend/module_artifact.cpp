#include "frontend/module_artifact.hpp"
#include "common/ast_expressions.hpp"
#include "common/binary_io.hpp"
#include "common/template_metadata.hpp"

#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <vector>

namespace cactus {

namespace fs = std::filesystem;

ModuleArtifact::ModuleArtifact(ErrorReporter& errors)
    : errors_(errors) {}

// ── Artifact filename ───────────────────────────────────────────────────────

fs::path ModuleArtifact::artifact_filename(const std::string& module_name) {
    return {module_name + ".cmod"};
}

// ── Write helpers ───────────────────────────────────────────────────────────

void ModuleArtifact::write_u8(std::ostream& out, uint8_t v) {
    binary_io::write_uint(out, v);
}

void ModuleArtifact::write_u32(std::ostream& out, uint32_t v) {
    binary_io::write_uint(out, v);
}

void ModuleArtifact::write_u64(std::ostream& out, uint64_t v) {
    binary_io::write_uint(out, v);
}

void ModuleArtifact::write_i64(std::ostream& out, int64_t v) {
    write_u64(out, static_cast<uint64_t>(v));
}

void ModuleArtifact::write_double(std::ostream& out, double v) {
    static_assert(sizeof(double) == sizeof(uint64_t));
    write_u64(out, std::bit_cast<uint64_t>(v));
}

void ModuleArtifact::write_bool(std::ostream& out, bool v) {
    write_u8(out, v ? 1 : 0);
}

void ModuleArtifact::write_str(std::ostream& out, const std::string& s) {
    binary_io::write_string(out, s);
}

void ModuleArtifact::write_symbol_id(std::ostream& out, const SymbolId& symbol) {
    write_u8(out, static_cast<uint8_t>(symbol.kind));
    write_str(out, symbol.module.name);
    write_str(out, symbol.local_name);
}

void ModuleArtifact::write_optional_symbol_id(std::ostream& out, const std::optional<SymbolId>& symbol) {
    write_bool(out, symbol.has_value());
    if (symbol.has_value()) {
        write_symbol_id(out, *symbol);
    }
}

void ModuleArtifact::write_trigger(std::ostream& out, const ResolvedHandlerTrigger& trigger) {
    write_u8(out, static_cast<uint8_t>(trigger.kind));
    write_symbol_id(out, trigger.symbol);
}

void ModuleArtifact::write_handler_identity(std::ostream& out, const HandlerIdentity& identity) {
    write_symbol_id(out, identity.rule);
    write_trigger(out, identity.trigger);
}

void ModuleArtifact::write_declaration_order(std::ostream& out, const DeclarationOrder& order) {
    write_u64(out, order.module_index);
    write_u64(out, order.declaration_index);
    write_u64(out, order.handler_index);
}

void ModuleArtifact::write_location(std::ostream& out, const SourceLocation& location) {
    write_str(out, location.filename);
    write_i64(out, location.line);
    write_i64(out, location.column);
}

void ModuleArtifact::write_type_info(std::ostream& out, const TypeInfo& t) {
    write_u8(out, static_cast<uint8_t>(t.kind));
    write_str(out, t.name);
    write_bool(out, t.is_let);
    write_bool(out, t.is_persist);
    write_bool(out, t.is_pub);

    // Optional canonical symbol identity (for Enum/Struct field types)
    write_bool(out, t.symbol_id.has_value());
    if (t.symbol_id.has_value()) {
        write_u8(out, static_cast<uint8_t>(t.symbol_id->kind));
        write_str(out, t.symbol_id->module.name);
        write_str(out, t.symbol_id->local_name);
    }

    // For List: write element type
    bool has_element = (t.kind == TypeKind::List && t.element);
    write_bool(out, has_element);
    if (has_element) {
        write_type_info(out, *t.element);
    }

    // For Func: write params + return type
    write_u32(out, static_cast<uint32_t>(t.params.size()));
    for (const auto& p : t.params) {
        write_type_info(out, p);
    }
    bool has_ret = (t.kind == TypeKind::Func && t.ret);
    write_bool(out, has_ret);
    if (has_ret) {
        write_type_info(out, *t.ret);
    }
}

void ModuleArtifact::write_field(std::ostream& out, const ResolvedField& field) {
    write_str(out, field.name);
    write_bool(out, field.is_let);
    write_bool(out, field.is_var);
    write_bool(out, field.is_persist);
    write_bool(out, field.is_pub);
    write_bool(out, field.has_default);
    write_bool(out, field.is_synthesized);
    write_bool(out, field.is_completion_only);
    write_type_info(out, field.type);
    write_bool(out, field.source_binding.has_value());
    if (field.source_binding.has_value()) {
        write_bool(out, field.source_binding->kind == PhaseFieldSource::Kind::UpstreamPhase);
        write_symbol_id(out, field.source_binding->source);
        write_str(out, field.source_binding->member);
    }
}

void ModuleArtifact::write_traits(std::ostream& out,
                                  const std::unordered_map<std::string, ResolvedTrait>& traits,
                                  const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(traits.size()));
    for (const auto& [name, trait] : traits) {
        write_str(out, trait.name);
        // Write canonical identity as stored; the linker derives it from src_module_name
        // when canonical_id is absent, so we must not silently derive here or the loader
        // would see a non-empty canonical_id and treat it as an explicit map key.
        const std::string& mod = trait.module_name.empty() ? module_name : trait.module_name;
        write_str(out, mod);
        write_str(out, trait.canonical_id);
        write_optional_symbol_id(out, trait.symbol_id);
        write_bool(out, trait.is_pub);
        write_bool(out, trait.is_stdlib);
        write_u32(out, static_cast<uint32_t>(trait.fields.size()));
        for (const auto& field : trait.fields) {
            write_field(out, field);
        }
    }
}

void ModuleArtifact::write_structs(std::ostream& out,
                                   const std::unordered_map<std::string, ResolvedStruct>& structs,
                                   const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(structs.size()));
    for (const auto& [name, strct] : structs) {
        write_str(out, strct.name);
        const std::string& mod = strct.module_name.empty() ? module_name : strct.module_name;
        write_str(out, mod);
        write_str(out, strct.canonical_id);
        write_optional_symbol_id(out, strct.symbol_id);
        write_u32(out, static_cast<uint32_t>(strct.fields.size()));
        for (const auto& field : strct.fields) {
            write_field(out, field);
        }
    }
}

void ModuleArtifact::write_enums(std::ostream& out,
                                 const std::unordered_map<std::string, ResolvedEnum>& enums,
                                 const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(enums.size()));
    for (const auto& [name, enm] : enums) {
        write_str(out, enm.name);
        const std::string& mod = enm.module_name.empty() ? module_name : enm.module_name;
        write_str(out, mod);
        write_str(out, enm.canonical_id);
        write_optional_symbol_id(out, enm.symbol_id);
        write_u32(out, static_cast<uint32_t>(enm.variants.size()));
        for (const auto& v : enm.variants) {
            write_str(out, v);
        }
    }
}

void ModuleArtifact::write_funcs(std::ostream& out,
                                 const std::unordered_map<std::string, ResolvedFunc>& funcs,
                                 const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(funcs.size()));
    for (const auto& [name, func] : funcs) {
        write_str(out, func.name);
        const std::string& mod = func.module_name.empty() ? module_name : func.module_name;
        write_str(out, mod);
        write_str(out, func.canonical_id);
        write_optional_symbol_id(out, func.symbol_id);
        write_bool(out, func.is_pub);
        write_bool(out, func.is_extern);
        write_bool(out, func.is_stdlib);
        write_bool(out, func.effect_summary.has_value());
        if (func.effect_summary.has_value()) {
            write_string_set(out, *func.effect_summary);
        }
        write_u32(out, static_cast<uint32_t>(func.params.size()));
        for (const auto& p : func.params) {
            write_str(out, p.name);
            write_type_info(out, p.type);
        }
        bool has_ret = func.return_type.has_value();
        write_bool(out, has_ret);
        if (has_ret) {
            write_type_info(out, *func.return_type);
        }
    }
}

void ModuleArtifact::write_events(std::ostream& out,
                                  const std::unordered_map<std::string, ResolvedEvent>& events,
                                  const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(events.size()));
    for (const auto& [name, event] : events) {
        write_str(out, event.name);
        write_str(out, event.module_name.empty() ? module_name : event.module_name);
        write_str(out, event.canonical_id);
        write_optional_symbol_id(out, event.symbol_id);
        write_bool(out, event.is_pub);
        write_bool(out, event.is_external);
        write_u32(out, static_cast<uint32_t>(event.fields.size()));
        for (const auto& field : event.fields) {
            write_field(out, field);
        }
    }
}

void ModuleArtifact::write_phases(std::ostream& out,
                                  const std::unordered_map<std::string, ResolvedPhase>& phases,
                                  const std::string& module_name) {
    write_u32(out, static_cast<uint32_t>(phases.size()));
    for (const auto& [name, phase] : phases) {
        write_str(out, phase.name);
        write_str(out, phase.module_name.empty() ? module_name : phase.module_name);
        write_str(out, phase.canonical_id);
        write_optional_symbol_id(out, phase.symbol_id);
        write_bool(out, phase.is_pub);
        write_bool(out, phase.has_every);
        write_bool(out, phase.has_max);
        write_u32(out, static_cast<uint32_t>(phase.fields.size()));
        for (const auto& field : phase.fields) {
            write_field(out, field);
        }
        write_u32(out, static_cast<uint32_t>(phase.from_sources.size()));
        for (const auto& trigger : phase.from_sources) {
            write_trigger(out, trigger);
        }
        write_u32(out, static_cast<uint32_t>(phase.after_phases.size()));
        for (const auto& trigger : phase.after_phases) {
            write_trigger(out, trigger);
        }
        write_symbol_vector(out, phase.upstream_phases);
        write_optional_symbol_id(out, phase.runtime_root);
        write_bool(out, phase.every_seconds.has_value());
        if (phase.every_seconds.has_value()) {
            write_double(out, *phase.every_seconds);
        }
        write_bool(out, phase.max_repetitions.has_value());
        if (phase.max_repetitions.has_value()) {
            write_i64(out, *phase.max_repetitions);
        }
        write_bool(out, phase.render_pass.has_value());
        if (phase.render_pass.has_value()) {
            const auto& info = *phase.render_pass;
            write_u32(out, static_cast<uint32_t>(info.pass_field_index));
            write_u32(out, static_cast<uint32_t>(info.target_field_index));
            write_symbol_id(out, info.pass_enum_id);
            write_symbol_id(out, info.target_enum_id);
            write_trigger(out, info.vertex_trigger);
            write_trigger(out, info.fragment_trigger);
        }
    }
}

void ModuleArtifact::write_string_set(std::ostream& out, const std::unordered_set<std::string>& values) {
    std::vector<std::string> ordered(values.begin(), values.end());
    std::ranges::sort(ordered);
    ModuleArtifact::write_u32(out, static_cast<uint32_t>(ordered.size()));
    for (const auto& value : ordered) {
        ModuleArtifact::write_str(out, value);
    }
}

void ModuleArtifact::write_symbol_set(std::ostream& out, const std::unordered_set<SymbolId>& values) {
    std::vector<SymbolId> ordered(values.begin(), values.end());
    std::ranges::sort(ordered, [](const SymbolId& left, const SymbolId& right) {
        return make_canonical_id(left) < make_canonical_id(right);
    });
    write_u32(out, static_cast<uint32_t>(ordered.size()));
    for (const auto& value : ordered) {
        write_symbol_id(out, value);
    }
}

void ModuleArtifact::write_symbol_vector(std::ostream& out, const std::vector<SymbolId>& values) {
    write_u32(out, static_cast<uint32_t>(values.size()));
    for (const auto& value : values) {
        write_symbol_id(out, value);
    }
}

void ModuleArtifact::write_string_vector(std::ostream& out, const std::vector<std::string>& values) {
    write_u32(out, static_cast<uint32_t>(values.size()));
    for (const auto& value : values) {
        write_str(out, value);
    }
}

void ModuleArtifact::write_dep_graph(std::ostream& out, const std::vector<RuleDependency>& graph) {
    write_u32(out, static_cast<uint32_t>(graph.size()));
    for (const auto& dep : graph) {
        write_str(out, dep.rule_name);
        write_optional_symbol_id(out, dep.rule_id);
        // reads
        write_u32(out, static_cast<uint32_t>(dep.reads.size()));
        for (const auto& r : dep.reads) {
            write_str(out, r);
        }
        // writes
        write_u32(out, static_cast<uint32_t>(dep.writes.size()));
        for (const auto& w : dep.writes) {
            write_str(out, w);
        }
        // emits
        write_u32(out, static_cast<uint32_t>(dep.emits.size()));
        for (const auto& e : dep.emits) {
            write_str(out, e);
        }
        // after_rules (canonical rule IDs, task 4.3)
        write_u32(out, static_cast<uint32_t>(dep.after_rules.size()));
        for (const auto& a : dep.after_rules) {
            write_str(out, a);
        }
        write_symbol_vector(out, dep.resolved_after_rule_ids);
    }
}

void ModuleArtifact::write_contract(std::ostream& out, const HandlerContract& contract) {
    write_u8(out, static_cast<uint8_t>(contract.domain_kind));
    write_symbol_vector(out, contract.selection);
    write_symbol_vector(out, contract.exclusion);
    write_u32(out, static_cast<uint32_t>(contract.pair_bindings.size()));
    for (const auto& binding : contract.pair_bindings) {
        write_str(out, binding.name);
        write_symbol_vector(out, binding.required_traits);
    }
    write_symbol_set(out, contract.reads);
    write_u32(out, static_cast<uint32_t>(contract.bound_reads.size()));
    for (const auto& access : contract.bound_reads) {
        write_u64(out, static_cast<uint64_t>(access.binding_index));
        write_symbol_id(out, access.trait);
    }
    write_symbol_set(out, contract.writes);
    write_symbol_set(out, contract.projects);
    write_symbol_set(out, contract.emits);
    write_u32(out, static_cast<uint32_t>(contract.commands.size()));
    for (const auto& command : contract.commands) {
        write_u8(out, static_cast<uint8_t>(command.kind));
        write_optional_symbol_id(out, command.target);
    }
    write_string_set(out, contract.effects);

    write_bool(out, contract.spatial_join.has_value());
    if (contract.spatial_join.has_value()) {
        const auto& plan = *contract.spatial_join;
        const auto write_access = [&out](const SpatialJoinAccess& access) {
            write_symbol_id(out, access.trait);
            write_u32(out, static_cast<uint32_t>(access.field_path.size()));
            for (const auto& segment : access.field_path) {
                write_str(out, segment);
            }
        };
        const auto write_binding = [&out, &write_access](const SpatialJoinBinding& binding) {
            write_u64(out, static_cast<uint64_t>(binding.binding_index));
            write_access(binding.position);
            write_access(binding.radius);
        };
        write_u8(out, static_cast<uint8_t>(plan.dimension));
        write_binding(plan.left);
        write_binding(plan.right);
        write_u64(out, static_cast<uint64_t>(plan.matched_predicate_index));
    }
}

void ModuleArtifact::write_handler_contracts(std::ostream& out, const std::vector<InferredHandlerContract>& contracts) {
    write_u32(out, static_cast<uint32_t>(contracts.size()));
    for (const auto& contract : contracts) {
        write_symbol_id(out, contract.rule);
        write_trigger(out, contract.trigger);
        write_contract(out, contract);
    }
}

void ModuleArtifact::write_execution_graph(std::ostream& out, const ExecutionGraph& graph) {
    write_u32(out, static_cast<uint32_t>(graph.phases.size()));
    for (const auto& phase : graph.phases) {
        write_symbol_id(out, phase.phase);
        write_u32(out, static_cast<uint32_t>(phase.source_dependencies.size()));
        for (const auto& trigger : phase.source_dependencies) {
            write_trigger(out, trigger);
        }
        write_symbol_vector(out, phase.completion_dependencies);
        write_u32(out, static_cast<uint32_t>(phase.fields.size()));
        for (const auto& field : phase.fields) {
            write_field(out, field);
        }
        write_optional_symbol_id(out, phase.runtime_root);
        write_bool(out, phase.every_seconds.has_value());
        if (phase.every_seconds.has_value()) {
            write_double(out, *phase.every_seconds);
        }
        write_bool(out, phase.max_repetitions.has_value());
        if (phase.max_repetitions.has_value()) {
            write_i64(out, *phase.max_repetitions);
        }
        write_declaration_order(out, phase.declaration_order);
    }

    write_u32(out, static_cast<uint32_t>(graph.handlers.size()));
    for (const auto& handler : graph.handlers) {
        write_handler_identity(out, handler.identity);
        write_u8(out, static_cast<uint8_t>(handler.implementation));
        write_contract(out, handler.contract);
        write_u32(out, static_cast<uint32_t>(handler.explicit_after.size()));
        for (const auto& identity : handler.explicit_after) {
            write_handler_identity(out, identity);
        }
        write_declaration_order(out, handler.declaration_order);
        write_location(out, handler.location);
    }

    write_u32(out, static_cast<uint32_t>(graph.render_passes.size()));
    for (const auto& plan : graph.render_passes) {
        write_symbol_id(out, plan.phase);
        write_handler_identity(out, plan.vertex_handler);
        write_handler_identity(out, plan.fragment_handler);
    }

    write_u32(out, static_cast<uint32_t>(graph.schedule_edges.size()));
    for (const auto& edge : graph.schedule_edges) {
        write_handler_identity(out, edge.before);
        write_handler_identity(out, edge.after);
        write_u8(out, static_cast<uint8_t>(edge.kind));
        write_u8(out, static_cast<uint8_t>(edge.orientation));
        write_symbol_vector(out, edge.trait_provenance);
        write_u32(out, static_cast<uint32_t>(edge.effect_provenance.size()));
        for (const auto& effect : edge.effect_provenance) {
            write_str(out, effect);
        }
    }

    write_u32(out, static_cast<uint32_t>(graph.phase_barriers.size()));
    for (const auto& edge : graph.phase_barriers) {
        write_symbol_id(out, edge.upstream_phase);
        write_handler_identity(out, edge.downstream_handler);
    }

    write_u32(out, static_cast<uint32_t>(graph.event_flows.size()));
    for (const auto& edge : graph.event_flows) {
        write_handler_identity(out, edge.producer);
        write_symbol_id(out, edge.event);
        write_handler_identity(out, edge.consumer);
    }

    write_u32(out, static_cast<uint32_t>(graph.stable_topological_order.size()));
    for (const auto& identity : graph.stable_topological_order) {
        write_handler_identity(out, identity);
    }

    write_u32(out, static_cast<uint32_t>(graph.dependency_levels.size()));
    for (const auto& level : graph.dependency_levels) {
        write_trigger(out, level.activation);
        write_u64(out, level.index);
        write_u32(out, static_cast<uint32_t>(level.handlers.size()));
        for (const auto& identity : level.handlers) {
            write_handler_identity(out, identity);
        }
    }
}

void ModuleArtifact::write_string_pool(std::ostream& out, const StringPool& pool) {
    // We can't directly iterate over private maps, so we use size=0 as a sentinel.
    // In practice the string pool is used internally; for artifact purposes we
    // serialize a count of 0 and rebuild on load.
    write_u32(out, 0);
    // Future: serialize pool entries if direct access is added.
    (void)pool;
}

// ── Read helpers ─────────────────────────────────────────────────────────────

uint8_t ModuleArtifact::read_u8(std::istream& in) {
    return binary_io::read_uint<uint8_t>(in);
}

uint32_t ModuleArtifact::read_u32(std::istream& in) {
    return binary_io::read_uint<uint32_t>(in);
}

uint64_t ModuleArtifact::read_u64(std::istream& in) {
    return binary_io::read_uint<uint64_t>(in);
}

int64_t ModuleArtifact::read_i64(std::istream& in) {
    return static_cast<int64_t>(read_u64(in));
}

double ModuleArtifact::read_double(std::istream& in) {
    return std::bit_cast<double>(read_u64(in));
}

bool ModuleArtifact::read_bool(std::istream& in) {
    return read_u8(in) != 0;
}

std::string ModuleArtifact::read_str(std::istream& in) {
    return binary_io::read_string(in);
}

SymbolId ModuleArtifact::read_symbol_id(std::istream& in) {
    return SymbolId{.kind       = static_cast<SymbolKind>(read_u8(in)),
                    .module     = ModuleId{.name = read_str(in)},
                    .local_name = read_str(in)};
}

std::optional<SymbolId> ModuleArtifact::read_optional_symbol_id(std::istream& in) {
    if (!read_bool(in)) {
        return std::nullopt;
    }
    return read_symbol_id(in);
}

ResolvedHandlerTrigger ModuleArtifact::read_trigger(std::istream& in) {
    return ResolvedHandlerTrigger{.kind = static_cast<HandlerTriggerKind>(read_u8(in)), .symbol = read_symbol_id(in)};
}

HandlerIdentity ModuleArtifact::read_handler_identity(std::istream& in) {
    return HandlerIdentity{.rule = read_symbol_id(in), .trigger = read_trigger(in)};
}

DeclarationOrder ModuleArtifact::read_declaration_order(std::istream& in) {
    return DeclarationOrder{
        .module_index = read_u64(in), .declaration_index = read_u64(in), .handler_index = read_u64(in)};
}

SourceLocation ModuleArtifact::read_location(std::istream& in) {
    return SourceLocation{read_str(in), static_cast<int>(read_i64(in)), static_cast<int>(read_i64(in))};
}

TypeInfo ModuleArtifact::read_type_info(std::istream& in) {
    TypeInfo t;
    t.kind       = static_cast<TypeKind>(read_u8(in));
    t.name       = read_str(in);
    t.is_let     = read_bool(in);
    t.is_persist = read_bool(in);
    t.is_pub     = read_bool(in);

    bool has_symbol_id = read_bool(in);
    if (has_symbol_id) {
        SymbolId sym;
        sym.kind        = static_cast<SymbolKind>(read_u8(in));
        sym.module.name = read_str(in);
        sym.local_name  = read_str(in);
        t.symbol_id     = sym;
    }

    bool has_element = read_bool(in);
    if (has_element) {
        t.element = std::make_shared<TypeInfo>(read_type_info(in));
    }

    uint32_t param_count = read_u32(in);
    t.params.reserve(param_count);
    for (uint32_t i = 0; i < param_count; ++i) {
        t.params.push_back(read_type_info(in));
    }

    bool has_ret = read_bool(in);
    if (has_ret) {
        t.ret = std::make_shared<TypeInfo>(read_type_info(in));
    }

    return t;
}

ResolvedField ModuleArtifact::read_field(std::istream& in) {
    ResolvedField field;
    field.name               = read_str(in);
    field.is_let             = read_bool(in);
    field.is_var             = read_bool(in);
    field.is_persist         = read_bool(in);
    field.is_pub             = read_bool(in);
    field.has_default        = read_bool(in);
    field.is_synthesized     = read_bool(in);
    field.is_completion_only = read_bool(in);
    field.type               = read_type_info(in);
    if (read_bool(in)) {
        PhaseFieldSource binding;
        binding.kind   = read_bool(in) ? PhaseFieldSource::Kind::UpstreamPhase : PhaseFieldSource::Kind::RootEvent;
        binding.source = read_symbol_id(in);
        binding.member = read_str(in);
        field.source_binding = binding;
    }
    return field;
}

std::unordered_map<std::string, ResolvedTrait> ModuleArtifact::read_traits(std::istream& in) {
    std::unordered_map<std::string, ResolvedTrait> traits;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedTrait trait;
        trait.name           = read_str(in);
        trait.module_name    = read_str(in);
        trait.canonical_id   = read_str(in);
        trait.symbol_id      = read_optional_symbol_id(in);
        trait.is_pub         = read_bool(in);
        trait.is_stdlib      = read_bool(in);
        uint32_t field_count = read_u32(in);
        trait.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            trait.fields.push_back(read_field(in));
        }
        traits[trait.name] = std::move(trait);
    }
    return traits;
}

std::unordered_map<std::string, ResolvedStruct> ModuleArtifact::read_structs(std::istream& in) {
    std::unordered_map<std::string, ResolvedStruct> structs;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedStruct strct;
        strct.name           = read_str(in);
        strct.module_name    = read_str(in);
        strct.canonical_id   = read_str(in);
        strct.symbol_id      = read_optional_symbol_id(in);
        uint32_t field_count = read_u32(in);
        strct.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            strct.fields.push_back(read_field(in));
        }
        structs[strct.name] = std::move(strct);
    }
    return structs;
}

std::unordered_map<std::string, ResolvedEnum> ModuleArtifact::read_enums(std::istream& in) {
    std::unordered_map<std::string, ResolvedEnum> enums;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedEnum enm;
        enm.name           = read_str(in);
        enm.module_name    = read_str(in);
        enm.canonical_id   = read_str(in);
        enm.symbol_id      = read_optional_symbol_id(in);
        uint32_t var_count = read_u32(in);
        enm.variants.reserve(var_count);
        for (uint32_t j = 0; j < var_count; ++j) {
            enm.variants.push_back(read_str(in));
        }
        enums[enm.name] = std::move(enm);
    }
    return enums;
}

std::unordered_map<std::string, ResolvedFunc> ModuleArtifact::read_funcs(std::istream& in) {
    std::unordered_map<std::string, ResolvedFunc> funcs;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedFunc func;
        func.name         = read_str(in);
        func.module_name  = read_str(in);
        func.canonical_id = read_str(in);
        func.symbol_id    = read_optional_symbol_id(in);
        func.is_pub       = read_bool(in);
        func.is_extern    = read_bool(in);
        func.is_stdlib    = read_bool(in);
        if (read_bool(in)) {
            func.effect_summary = read_string_set(in);
        }
        uint32_t param_count = read_u32(in);
        func.params.reserve(param_count);
        for (uint32_t j = 0; j < param_count; ++j) {
            ResolvedParam p;
            p.name = read_str(in);
            p.type = read_type_info(in);
            func.params.push_back(std::move(p));
        }
        bool has_ret = read_bool(in);
        if (has_ret) {
            func.return_type = read_type_info(in);
        }
        funcs[func.name] = std::move(func);
    }
    return funcs;
}

std::unordered_map<std::string, ResolvedEvent> ModuleArtifact::read_events(std::istream& in) {
    std::unordered_map<std::string, ResolvedEvent> events;
    const auto count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedEvent event;
        event.name             = read_str(in);
        event.module_name      = read_str(in);
        event.canonical_id     = read_str(in);
        event.symbol_id        = read_optional_symbol_id(in);
        event.is_pub           = read_bool(in);
        event.is_external      = read_bool(in);
        const auto field_count = read_u32(in);
        event.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            event.fields.push_back(read_field(in));
        }
        events[event.name] = std::move(event);
    }
    return events;
}

std::unordered_map<std::string, ResolvedPhase> ModuleArtifact::read_phases(std::istream& in) {
    std::unordered_map<std::string, ResolvedPhase> phases;
    const auto count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        ResolvedPhase phase;
        phase.name             = read_str(in);
        phase.module_name      = read_str(in);
        phase.canonical_id     = read_str(in);
        phase.symbol_id        = read_optional_symbol_id(in);
        phase.is_pub           = read_bool(in);
        phase.has_every        = read_bool(in);
        phase.has_max          = read_bool(in);
        const auto field_count = read_u32(in);
        phase.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            phase.fields.push_back(read_field(in));
        }
        const auto source_count = read_u32(in);
        phase.from_sources.reserve(source_count);
        for (uint32_t j = 0; j < source_count; ++j) {
            phase.from_sources.push_back(read_trigger(in));
        }
        const auto after_count = read_u32(in);
        phase.after_phases.reserve(after_count);
        for (uint32_t j = 0; j < after_count; ++j) {
            phase.after_phases.push_back(read_trigger(in));
        }
        phase.upstream_phases = read_symbol_vector(in);
        phase.runtime_root    = read_optional_symbol_id(in);
        if (read_bool(in)) {
            phase.every_seconds = read_double(in);
        }
        if (read_bool(in)) {
            phase.max_repetitions = read_i64(in);
        }
        if (read_bool(in)) {
            RenderPassInfo info;
            info.pass_field_index   = read_u32(in);
            info.target_field_index = read_u32(in);
            info.pass_enum_id       = read_symbol_id(in);
            info.target_enum_id     = read_symbol_id(in);
            info.vertex_trigger     = read_trigger(in);
            info.fragment_trigger   = read_trigger(in);
            phase.render_pass       = std::move(info);
        }
        phases[phase.name] = std::move(phase);
    }
    return phases;
}

std::unordered_set<std::string> ModuleArtifact::read_string_set(std::istream& in) {
    std::unordered_set<std::string> values;
    uint32_t count = ModuleArtifact::read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        values.insert(ModuleArtifact::read_str(in));
    }
    return values;
}

std::unordered_set<SymbolId> ModuleArtifact::read_symbol_set(std::istream& in) {
    std::unordered_set<SymbolId> values;
    const auto count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        values.insert(read_symbol_id(in));
    }
    return values;
}

std::vector<SymbolId> ModuleArtifact::read_symbol_vector(std::istream& in) {
    std::vector<SymbolId> values;
    const auto count = read_u32(in);
    values.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        values.push_back(read_symbol_id(in));
    }
    return values;
}

std::vector<std::string> ModuleArtifact::read_string_vector(std::istream& in) {
    std::vector<std::string> values;
    const auto count = read_u32(in);
    values.reserve(count);
    for (uint32_t i = 0; i < count && in.good(); ++i) {
        values.push_back(read_str(in));
    }
    return values;
}

std::vector<RuleDependency> ModuleArtifact::read_dep_graph(std::istream& in) {
    std::vector<RuleDependency> graph;
    uint32_t count = read_u32(in);
    graph.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        RuleDependency dep;
        dep.rule_name = read_str(in);
        dep.rule_id   = read_optional_symbol_id(in);

        uint32_t reads_count = read_u32(in);
        for (uint32_t j = 0; j < reads_count; ++j) {
            dep.reads.insert(read_str(in));
        }

        uint32_t writes_count = read_u32(in);
        for (uint32_t j = 0; j < writes_count; ++j) {
            dep.writes.insert(read_str(in));
        }

        uint32_t emits_count = read_u32(in);
        for (uint32_t j = 0; j < emits_count; ++j) {
            dep.emits.insert(read_str(in));
        }

        uint32_t after_count = read_u32(in);
        dep.after_rules.reserve(after_count);
        for (uint32_t j = 0; j < after_count; ++j) {
            dep.after_rules.push_back(read_str(in));
        }
        dep.resolved_after_rule_ids = read_symbol_vector(in);

        graph.push_back(std::move(dep));
    }
    return graph;
}

HandlerContract ModuleArtifact::read_contract(std::istream& in) {
    HandlerContract contract;
    contract.domain_kind = static_cast<HandlerDomainKind>(read_u8(in));
    contract.selection   = read_symbol_vector(in);
    contract.exclusion   = read_symbol_vector(in);

    const auto binding_count = read_u32(in);
    contract.pair_bindings.reserve(binding_count);
    for (uint32_t i = 0; i < binding_count; ++i) {
        RelationBinding binding;
        binding.name            = read_str(in);
        binding.required_traits = read_symbol_vector(in);
        contract.pair_bindings.push_back(std::move(binding));
    }

    contract.reads = read_symbol_set(in);

    const auto bound_read_count = read_u32(in);
    contract.bound_reads.reserve(bound_read_count);
    for (uint32_t i = 0; i < bound_read_count; ++i) {
        BoundTraitAccess access;
        access.binding_index = static_cast<std::size_t>(read_u64(in));
        access.trait         = read_symbol_id(in);
        contract.bound_reads.push_back(access);
    }

    contract.writes          = read_symbol_set(in);
    contract.projects        = read_symbol_set(in);
    contract.emits           = read_symbol_set(in);
    const auto command_count = read_u32(in);
    contract.commands.reserve(command_count);
    for (uint32_t i = 0; i < command_count; ++i) {
        contract.commands.push_back(InferredHandlerCommand{.kind   = static_cast<HandlerCommandKind>(read_u8(in)),
                                                           .target = read_optional_symbol_id(in)});
    }
    contract.effects = read_string_set(in);

    if (read_bool(in)) {
        const auto read_access = [&in]() {
            SpatialJoinAccess access;
            access.trait                    = read_symbol_id(in);
            const auto field_path_count = read_u32(in);
            access.field_path.reserve(field_path_count);
            for (uint32_t i = 0; i < field_path_count; ++i) {
                access.field_path.push_back(read_str(in));
            }
            return access;
        };
        const auto read_binding = [&in, &read_access]() {
            SpatialJoinBinding binding;
            binding.binding_index = static_cast<std::size_t>(read_u64(in));
            binding.position       = read_access();
            binding.radius         = read_access();
            return binding;
        };
        SpatialJoinPlan plan;
        plan.dimension               = static_cast<SpatialJoinDimension>(read_u8(in));
        plan.left                    = read_binding();
        plan.right                   = read_binding();
        plan.matched_predicate_index = static_cast<std::size_t>(read_u64(in));
        contract.spatial_join        = std::move(plan);
    }

    return contract;
}

std::vector<InferredHandlerContract> ModuleArtifact::read_handler_contracts(std::istream& in) {
    std::vector<InferredHandlerContract> contracts;
    const auto count = read_u32(in);
    contracts.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        InferredHandlerContract contract;
        contract.rule                           = read_symbol_id(in);
        contract.trigger                        = read_trigger(in);
        static_cast<HandlerContract&>(contract) = read_contract(in);
        contracts.push_back(std::move(contract));
    }
    return contracts;
}

ExecutionGraph ModuleArtifact::read_execution_graph(std::istream& in) {
    ExecutionGraph graph;
    const auto phase_count = read_u32(in);
    graph.phases.reserve(phase_count);
    for (uint32_t i = 0; i < phase_count; ++i) {
        PhasePlan phase;
        phase.phase             = read_symbol_id(in);
        const auto source_count = read_u32(in);
        phase.source_dependencies.reserve(source_count);
        for (uint32_t j = 0; j < source_count; ++j) {
            phase.source_dependencies.push_back(read_trigger(in));
        }
        phase.completion_dependencies = read_symbol_vector(in);
        const auto field_count        = read_u32(in);
        phase.fields.reserve(field_count);
        for (uint32_t j = 0; j < field_count; ++j) {
            phase.fields.push_back(read_field(in));
        }
        phase.runtime_root = read_optional_symbol_id(in);
        if (read_bool(in)) {
            phase.every_seconds = read_double(in);
        }
        if (read_bool(in)) {
            phase.max_repetitions = read_i64(in);
        }
        phase.declaration_order = read_declaration_order(in);
        graph.phases.push_back(std::move(phase));
    }

    const auto handler_count = read_u32(in);
    graph.handlers.reserve(handler_count);
    for (uint32_t i = 0; i < handler_count; ++i) {
        HandlerNode handler;
        handler.identity       = read_handler_identity(in);
        handler.implementation = static_cast<HandlerImplementationKind>(read_u8(in));
        handler.contract       = read_contract(in);
        const auto after_count = read_u32(in);
        handler.explicit_after.reserve(after_count);
        for (uint32_t j = 0; j < after_count; ++j) {
            handler.explicit_after.push_back(read_handler_identity(in));
        }
        handler.declaration_order = read_declaration_order(in);
        handler.location          = read_location(in);
        graph.handlers.push_back(std::move(handler));
    }

    const auto render_pass_count = read_u32(in);
    graph.render_passes.reserve(render_pass_count);
    for (uint32_t i = 0; i < render_pass_count; ++i) {
        RenderPassPlan plan;
        plan.phase           = read_symbol_id(in);
        plan.vertex_handler   = read_handler_identity(in);
        plan.fragment_handler = read_handler_identity(in);
        graph.render_passes.push_back(std::move(plan));
    }

    const auto schedule_count = read_u32(in);
    graph.schedule_edges.reserve(schedule_count);
    for (uint32_t i = 0; i < schedule_count; ++i) {
        ScheduleEdge edge;
        edge.before             = read_handler_identity(in);
        edge.after              = read_handler_identity(in);
        edge.kind               = static_cast<ScheduleEdgeKind>(read_u8(in));
        edge.orientation        = static_cast<ScheduleEdgeOrientation>(read_u8(in));
        edge.trait_provenance   = read_symbol_vector(in);
        const auto effect_count = read_u32(in);
        edge.effect_provenance.reserve(effect_count);
        for (uint32_t j = 0; j < effect_count; ++j) {
            edge.effect_provenance.push_back(read_str(in));
        }
        graph.schedule_edges.push_back(std::move(edge));
    }

    const auto barrier_count = read_u32(in);
    graph.phase_barriers.reserve(barrier_count);
    for (uint32_t i = 0; i < barrier_count; ++i) {
        graph.phase_barriers.push_back(
            PhaseBarrierEdge{.upstream_phase = read_symbol_id(in), .downstream_handler = read_handler_identity(in)});
    }

    const auto flow_count = read_u32(in);
    graph.event_flows.reserve(flow_count);
    for (uint32_t i = 0; i < flow_count; ++i) {
        graph.event_flows.push_back(EventFlowEdge{
            .producer = read_handler_identity(in), .event = read_symbol_id(in), .consumer = read_handler_identity(in)});
    }

    const auto order_count = read_u32(in);
    graph.stable_topological_order.reserve(order_count);
    for (uint32_t i = 0; i < order_count; ++i) {
        graph.stable_topological_order.push_back(read_handler_identity(in));
    }

    const auto level_count = read_u32(in);
    graph.dependency_levels.reserve(level_count);
    for (uint32_t i = 0; i < level_count; ++i) {
        DependencyLevel level;
        level.activation = read_trigger(in);
        level.index      = read_u64(in);
        const auto count = read_u32(in);
        level.handlers.reserve(count);
        for (uint32_t j = 0; j < count; ++j) {
            level.handlers.push_back(read_handler_identity(in));
        }
        graph.dependency_levels.push_back(std::move(level));
    }
    return graph;
}

StringPool ModuleArtifact::read_string_pool(std::istream& in) {
    StringPool pool;
    uint32_t count = read_u32(in);
    for (uint32_t i = 0; i < count; ++i) {
        auto s = read_str(in);
        pool.intern(s);
    }
    return pool;
}

// ── Save ─────────────────────────────────────────────────────────────────────

bool ModuleArtifact::save(const DecoratedProgram& program,
                          const std::string& module_name,
                          const fs::path& build_dir,
                          bool create_dir) {
    if (create_dir && !fs::exists(build_dir)) {
        std::error_code ec;
        fs::create_directories(build_dir, ec);
        if (ec) {
            errors_.error({}, "cannot create build dir: " + build_dir.string() + ": " + ec.message());
            return false;
        }
    }

    auto output = build_dir / artifact_filename(module_name);

    std::ofstream out(output, std::ios::binary);
    if (!out) {
        errors_.error({}, "cannot write artifact: " + output.string());
        return false;
    }

    // Header
    out.write(MAGIC, 4);
    write_u8(out, CURRENT_VERSION);
    write_str(out, module_name);

    // Sections
    write_traits(out, program.traits, module_name);
    write_structs(out, program.structs, module_name);
    write_enums(out, program.enums, module_name);
    write_funcs(out, program.funcs, module_name);
    write_events(out, program.events, module_name);
    write_phases(out, program.phases, module_name);
    write_string_set(out, program.pub_templates);
    write_string_set(out, program.non_pub_templates);
    write_string_set(out, program.pub_events);
    write_dep_graph(out, program.dependency_graph);
    write_handler_contracts(out, program.handler_contracts);
    write_execution_graph(out, program.execution_graph);
    write_string_pool(out, program.string_pool);
    write_template_metadata(out, program);
    write_persistence_metadata(out, program.persistence);

    return out.good();
}

// ── Load ─────────────────────────────────────────────────────────────────────

std::optional<DecoratedProgram> ModuleArtifact::load(const fs::path& path, std::string& module_name_out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors_.error({}, "cannot read artifact: " + path.string());
        return std::nullopt;
    }

    // Validate magic
    std::array<char, 4> magic = {};
    in.read(magic.data(), 4);
    if (std::memcmp(magic.data(), MAGIC, 4) != 0) {
        errors_.error({}, "invalid artifact format in '" + path.filename().string() + "'");
        return std::nullopt;
    }

    // Validate version
    uint8_t version = read_u8(in);
    if (version != CURRENT_VERSION) {
        errors_.error({},
                      "incompatible module artifact version in '" + path.filename().string() + "'; please recompile");
        return std::nullopt;
    }

    module_name_out = read_str(in);

    DecoratedProgram program;
    program.module_name       = module_name_out;
    program.traits            = read_traits(in);
    program.structs           = read_structs(in);
    program.enums             = read_enums(in);
    program.funcs             = read_funcs(in);
    program.events            = read_events(in);
    program.phases            = read_phases(in);
    program.pub_templates     = read_string_set(in);
    program.non_pub_templates = read_string_set(in);
    program.pub_events        = read_string_set(in);
    program.dependency_graph  = read_dep_graph(in);
    program.handler_contracts = read_handler_contracts(in);
    program.execution_graph   = read_execution_graph(in);
    program.string_pool       = read_string_pool(in);
    read_template_metadata(in, program);
    program.persistence       = read_persistence_metadata(in);
    program.ast               = nullptr;  // not serialized

    if (!in.good()) {
        errors_.error({}, "truncated artifact: " + path.string());
        return std::nullopt;
    }

    return program;
}

// ── Extract pub symbols ───────────────────────────────────────────────────────

std::optional<ImportedSymbols> ModuleArtifact::extract_pub_symbols(const fs::path& path) {
    std::string module_name;
    auto program = load(path, module_name);
    if (!program) {
        return std::nullopt;
    }

    ImportedSymbols symbols;
    symbols.module_name = module_name;

    for (auto& [name, trait] : program->traits) {
        if (trait.is_pub) {
            // canonical_id is populated during load (read_traits). Ensure non-empty.
            if (trait.canonical_id.empty()) {
                trait.canonical_id = make_canonical_id(module_name, trait.name);
            }
            if (trait.module_name.empty()) {
                trait.module_name = module_name;
            }
            if (!trait.symbol_id.has_value()) {
                trait.symbol_id = make_symbol_id(SymbolKind::Trait, trait.module_name, trait.name);
            }
            symbols.traits[name] = trait;
        }
    }
    for (auto& [name, strct] : program->structs) {
        if (strct.canonical_id.empty()) {
            strct.canonical_id = make_canonical_id(module_name, strct.name);
        }
        if (strct.module_name.empty()) {
            strct.module_name = module_name;
        }
        if (!strct.symbol_id.has_value()) {
            strct.symbol_id = make_symbol_id(SymbolKind::Struct, strct.module_name, strct.name);
        }
        symbols.structs[name] = strct;
    }
    for (auto& [name, enm] : program->enums) {
        if (enm.canonical_id.empty()) {
            enm.canonical_id = make_canonical_id(module_name, enm.name);
        }
        if (enm.module_name.empty()) {
            enm.module_name = module_name;
        }
        if (!enm.symbol_id.has_value()) {
            enm.symbol_id = make_symbol_id(SymbolKind::Enum, enm.module_name, enm.name);
        }
        symbols.enums[name] = enm;
    }
    for (auto& [name, func] : program->funcs) {
        if (func.is_pub) {
            if (func.canonical_id.empty()) {
                func.canonical_id = make_canonical_id(module_name, func.name);
            }
            if (func.module_name.empty()) {
                func.module_name = module_name;
            }
            if (!func.symbol_id.has_value()) {
                func.symbol_id = make_symbol_id(SymbolKind::Func, func.module_name, func.name);
            }
            symbols.funcs[name] = func;
        }
    }

    for (const auto& tmpl_name : program->pub_templates) {
        symbols.templates[tmpl_name] = exported_template(*program, module_name, tmpl_name);
    }

    for (const auto& dep : program->dependency_graph) {
        const auto symbol = dep.rule_id.value_or(make_symbol_id(SymbolKind::Rule, module_name, dep.rule_name));
        ImportedRule rule;
        rule.name                    = symbol.local_name;
        rule.module_name             = symbol.module.name;
        rule.canonical_id            = make_canonical_id(symbol);
        rule.symbol_id               = symbol;
        rule.after_rules             = dep.after_rules;
        symbols.rules[dep.rule_name] = rule;
    }

    for (const auto& [name, event] : program->events) {
        if (!event.is_pub) {
            continue;
        }
        const auto symbol = event.symbol_id.value_or(make_symbol_id(SymbolKind::Event, module_name, name));
        symbols.events.insert(name);
        symbols.event_symbols[name] = ImportedEvent{.name         = symbol.local_name,
                                                    .module_name  = symbol.module.name,
                                                    .canonical_id = make_canonical_id(symbol),
                                                    .symbol_id    = symbol,
                                                    .fields       = event.fields,
                                                    .is_external  = event.is_external};
    }

    for (const auto& [name, phase] : program->phases) {
        if (!phase.is_pub) {
            continue;
        }
        const auto symbol           = phase.symbol_id.value_or(make_symbol_id(SymbolKind::Phase, module_name, name));
        symbols.phase_symbols[name] = ImportedPhase{.name            = symbol.local_name,
                                                    .module_name     = symbol.module.name,
                                                    .canonical_id    = make_canonical_id(symbol),
                                                    .symbol_id       = symbol,
                                                    .fields          = phase.fields,
                                                    .upstream_phases = phase.upstream_phases,
                                                    .runtime_root    = phase.runtime_root,
                                                    .every_seconds   = phase.every_seconds,
                                                    .max_repetitions = phase.max_repetitions};
    }

    return symbols;
}

void ModuleArtifact::write_enum_member(std::ostream& out, const std::optional<ResolvedEnumMember>& member) {
    write_bool(out, member.has_value());
    if (!member.has_value()) {
        return;
    }
    write_symbol_id(out, member->enum_id);
    write_str(out, member->member);
    write_i64(out, member->index);
}

// Expression kinds go on the wire as ExprNode::Variant indices, so reordering
// the variant silently reinterprets every previously written artifact.
template <std::size_t Index, typename Alternative>
constexpr bool expr_alternative_is = std::is_same_v<std::variant_alternative_t<Index, ExprNode::Variant>, Alternative>;
static_assert(expr_alternative_is<0, LiteralExpr>);
static_assert(expr_alternative_is<1, IdentExpr>);
// 2, 10 and 11 have no encoding; expression_is_serializable rejects them by
// type, so their indices are pinned too.
static_assert(expr_alternative_is<2, SelfExpr>);
static_assert(expr_alternative_is<10, SpawnExpr>);
static_assert(expr_alternative_is<11, QueryCallExpr>);
static_assert(expr_alternative_is<3, BinaryExpr>);
static_assert(expr_alternative_is<4, UnaryExpr>);
static_assert(expr_alternative_is<5, CallExpr>);
static_assert(expr_alternative_is<6, MemberExpr>);
static_assert(expr_alternative_is<7, MatchExpr>);
static_assert(expr_alternative_is<8, IfExpr>);
static_assert(expr_alternative_is<9, ListExpr>);

namespace {

// write_expression fails the stream on an alternative it has no encoding for,
// which would abort the whole artifact rather than just drop one blueprint.
bool expression_is_serializable(const ExprNode& expression) {
    bool serializable = true;
    visit_expression(expression, [&serializable](const ExprNode& node) {
        serializable = serializable && !std::holds_alternative<SelfExpr>(node.expr) &&
                       !std::holds_alternative<SpawnExpr>(node.expr) &&
                       !std::holds_alternative<QueryCallExpr>(node.expr);
    });
    return serializable;
}

bool traits_are_serializable(const std::vector<ArchetypeTraitEntry>& traits) {
    return std::ranges::all_of(traits, [](const auto& trait) {
        return std::ranges::all_of(trait.assignments,
                                   [](const auto& assignment) { return expression_is_serializable(*assignment.value); });
    });
}

bool children_are_serializable(const std::vector<ChildArchetypeNode>& children) {
    return std::ranges::all_of(children, [](const auto& child) {
        return traits_are_serializable(child.traits) && children_are_serializable(child.children);
    });
}

bool blueprint_is_serializable(const TemplateNode& blueprint) {
    return traits_are_serializable(blueprint.traits) && children_are_serializable(blueprint.children) &&
           std::ranges::all_of(blueprint.initializers, [](const auto& slot) {
               return slot.value == nullptr || expression_is_serializable(*slot.value);
           });
}

}  // namespace

void ModuleArtifact::write_expression(std::ostream& out, const ExprNode& expression) {
    write_u8(out, static_cast<uint8_t>(expression.expr.index()));
    write_location(out, expression.location);
    auto expressions = [&](const auto& values) {
        write_u32(out, static_cast<uint32_t>(values.size()));
        for (const auto& value : values) {
            write_expression(out, *value);
        }
    };
    std::visit([&](const auto& node) {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, LiteralExpr>) {
            write_u8(out, static_cast<uint8_t>(node.kind));
            write_str(out, node.value);
        } else if constexpr (std::is_same_v<Node, IdentExpr>) {
            write_str(out, node.name);
            write_bool(out, node.template_slot.has_value());
            write_u64(out, node.template_slot.value_or(0));
            write_type_info(out, node.template_type);
        } else if constexpr (std::is_same_v<Node, BinaryExpr>) {
            write_str(out, node.op);
            write_expression(out, *node.left);
            write_expression(out, *node.right);
        } else if constexpr (std::is_same_v<Node, UnaryExpr>) {
            write_str(out, node.op);
            write_expression(out, *node.operand);
        } else if constexpr (std::is_same_v<Node, CallExpr>) {
            write_optional_symbol_id(out, node.resolved_callee_id);
            write_expression(out, *node.callee);
            expressions(node.args);
        } else if constexpr (std::is_same_v<Node, MemberExpr>) {
            write_expression(out, *node.object);
            write_str(out, node.member);
            write_enum_member(out, node.resolved_enum_member);
        } else if constexpr (std::is_same_v<Node, IfExpr>) {
            write_expression(out, *node.condition);
            write_expression(out, *node.then_expr);
            write_expression(out, *node.else_expr);
        } else if constexpr (std::is_same_v<Node, ListExpr>) {
            expressions(node.elements);
        } else if constexpr (std::is_same_v<Node, MatchExpr>) {
            write_expression(out, *node.subject);
            write_u32(out, static_cast<uint32_t>(node.arms.size()));
            for (const auto& arm : node.arms) {
                write_expression(out, *arm.pattern);
                write_expression(out, *arm.body);
            }
        } else {
            out.setstate(std::ios::failbit);
        }
    }, expression.expr);
}

std::unique_ptr<ExprNode> ModuleArtifact::read_expression(std::istream& in) {
    const auto kind = read_u8(in);
    const auto location = read_location(in);
    auto wrap = [&](auto node) {
        node.location = location;
        return std::make_unique<ExprNode>(ExprNode::Variant{std::move(node)}, location);
    };
    auto expressions = [&]() {
        std::vector<std::unique_ptr<ExprNode>> values;
        const auto count = read_u32(in);
        for (uint32_t i = 0; i < count && in.good(); ++i) {
            values.push_back(read_expression(in));
        }
        return values;
    };
    switch (kind) {
        case 0: {
            LiteralExpr node;
            node.kind = static_cast<LiteralExpr::Kind>(read_u8(in));
            node.value = read_str(in);
            return wrap(std::move(node));
        }
        case 1: {
            IdentExpr node;
            node.name = read_str(in);
            const auto has_slot = read_bool(in);
            const auto index = read_u64(in);
            if (has_slot) {
                node.template_slot = static_cast<std::size_t>(index);
            }
            node.template_type = read_type_info(in);
            return wrap(std::move(node));
        }
        case 3: {
            BinaryExpr node;
            node.op = read_str(in);
            node.left = read_expression(in);
            node.right = read_expression(in);
            return wrap(std::move(node));
        }
        case 4: {
            UnaryExpr node;
            node.op = read_str(in);
            node.operand = read_expression(in);
            return wrap(std::move(node));
        }
        case 5: {
            CallExpr node;
            node.resolved_callee_id = read_optional_symbol_id(in);
            node.callee = read_expression(in);
            node.args = expressions();
            return wrap(std::move(node));
        }
        case 6: {
            MemberExpr node;
            node.object = read_expression(in);
            node.member = read_str(in);
            if (read_bool(in)) {
                ResolvedEnumMember member;
                member.enum_id = read_symbol_id(in);
                member.member = read_str(in);
                member.index = static_cast<int32_t>(read_i64(in));
                node.resolved_enum_member = std::move(member);
            }
            return wrap(std::move(node));
        }
        case 7: {
            MatchExpr node;
            node.subject = read_expression(in);
            const auto count = read_u32(in);
            for (uint32_t i = 0; i < count && in.good(); ++i) {
                MatchArm arm;
                arm.pattern = read_expression(in);
                arm.body = read_expression(in);
                arm.location = location;
                node.arms.push_back(std::move(arm));
            }
            return wrap(std::move(node));
        }
        case 8: {
            IfExpr node;
            node.condition = read_expression(in);
            node.then_expr = read_expression(in);
            node.else_expr = read_expression(in);
            return wrap(std::move(node));
        }
        case 9:
            return wrap(ListExpr{.elements = expressions()});
        default:
            in.setstate(std::ios::failbit);
            return nullptr;
    }
}

void ModuleArtifact::write_archetype_traits(std::ostream& out, const std::vector<ArchetypeTraitEntry>& traits) {
    write_u32(out, static_cast<uint32_t>(traits.size()));
    for (const auto& trait : traits) {
        write_str(out, trait.trait_name);
        write_optional_symbol_id(out, trait.resolved_trait_id);
        write_location(out, trait.location);
        write_u32(out, static_cast<uint32_t>(trait.assignments.size()));
        for (const auto& assignment : trait.assignments) {
            write_str(out, assignment.name);
            write_location(out, assignment.location);
            write_expression(out, *assignment.value);
        }
    }
}

std::vector<ArchetypeTraitEntry> ModuleArtifact::read_archetype_traits(std::istream& in) {
    std::vector<ArchetypeTraitEntry> traits;
    const auto count = read_u32(in);
    for (uint32_t i = 0; i < count && in.good(); ++i) {
        ArchetypeTraitEntry trait;
        trait.trait_name = read_str(in);
        trait.resolved_trait_id = read_optional_symbol_id(in);
        trait.location = read_location(in);
        const auto fields = read_u32(in);
        for (uint32_t j = 0; j < fields && in.good(); ++j) {
            FieldAssignment assignment;
            assignment.name = read_str(in);
            assignment.location = read_location(in);
            assignment.value = read_expression(in);
            trait.assignments.push_back(std::move(assignment));
        }
        traits.push_back(std::move(trait));
    }
    return traits;
}

void ModuleArtifact::write_archetype_children(std::ostream& out, const std::vector<ChildArchetypeNode>& children) {
    write_u32(out, static_cast<uint32_t>(children.size()));
    for (const auto& child : children) {
        write_str(out, child.role);
        write_location(out, child.location);
        write_archetype_traits(out, child.traits);
        write_archetype_children(out, child.children);
    }
}

std::vector<ChildArchetypeNode> ModuleArtifact::read_archetype_children(std::istream& in) {
    std::vector<ChildArchetypeNode> children;
    const auto count = read_u32(in);
    for (uint32_t i = 0; i < count && in.good(); ++i) {
        ChildArchetypeNode child;
        child.role = read_str(in);
        child.location = read_location(in);
        child.traits = read_archetype_traits(in);
        child.children = read_archetype_children(in);
        children.push_back(std::move(child));
    }
    return children;
}

void ModuleArtifact::write_template_metadata(std::ostream& out, const DecoratedProgram& program) {
    // template_parameters also holds every imported template, keyed by its
    // qualified spelling. Re-encoding those would store a second, staleable copy
    // of another module's blueprints; this module only speaks for its own.
    const auto is_local = [](const std::string& name) { return !name.contains('.'); };
    const auto local_count =
        std::ranges::count_if(program.template_parameters, [&](const auto& entry) { return is_local(entry.first); });
    write_u32(out, static_cast<uint32_t>(local_count));
    for (const auto& [name, parameters] : program.template_parameters) {
        if (!is_local(name)) {
            continue;
        }
        write_str(out, name);
        write_u32(out, static_cast<uint32_t>(parameters.size()));
        for (const auto& parameter : parameters) {
            write_str(out, parameter.name);
            write_type_info(out, parameter.type);
            write_location(out, parameter.location);
            const bool has_default =
                parameter.default_value != nullptr && expression_is_serializable(*parameter.default_value);
            write_bool(out, has_default);
            if (has_default) {
                write_expression(out, *parameter.default_value);
            }
        }
        const auto found = program.template_blueprints.find(name);
        const auto* blueprint = found == program.template_blueprints.end() ? nullptr : found->second.get();
        // A blueprint the expression encoder cannot represent is dropped rather
        // than failing the stream: importers already handle a missing blueprint,
        // but a failed stream loses the whole artifact.
        if (blueprint != nullptr && !blueprint_is_serializable(*blueprint)) {
            blueprint = nullptr;
        }
        write_bool(out, blueprint != nullptr);
        if (blueprint == nullptr) {
            continue;
        }
        write_optional_symbol_id(out, blueprint->resolved_template_id);
        write_archetype_traits(out, blueprint->traits);
        write_archetype_children(out, blueprint->children);
        write_u32(out, static_cast<uint32_t>(blueprint->initializers.size()));
        for (const auto& slot : blueprint->initializers) {
            write_u64(out, slot.index);
            write_type_info(out, slot.type);
            write_bool(out, slot.value != nullptr);
            if (slot.value != nullptr) {
                write_expression(out, *slot.value);
            }
        }
    }
}

void ModuleArtifact::write_persistence_metadata(std::ostream& out, const ModulePersistenceMetadata& metadata) {
    write_bool(out, metadata.attaches_persistent_trait);
    write_u32(out, static_cast<uint32_t>(metadata.archetypes.size()));
    for (const auto& archetype : metadata.archetypes) {
        write_symbol_id(out, archetype.node.archetype);
        write_string_vector(out, archetype.node.role_path);
        write_u32(out, static_cast<uint32_t>(archetype.baseline_traits.size()));
        for (const auto& baseline : archetype.baseline_traits) {
            write_symbol_id(out, baseline.trait);
            write_string_vector(out, baseline.assigned_fields);
        }
        write_string_vector(out, archetype.parameters);
        write_string_vector(out, archetype.child_roles);
        write_bool(out, archetype.declares_persistent_trait);
    }
}

ModulePersistenceMetadata ModuleArtifact::read_persistence_metadata(std::istream& in) {
    ModulePersistenceMetadata metadata;
    metadata.attaches_persistent_trait = read_bool(in);
    const auto archetype_count         = read_u32(in);
    for (uint32_t i = 0; i < archetype_count && in.good(); ++i) {
        PersistenceArchetypeDescriptor archetype;
        archetype.node.archetype  = read_symbol_id(in);
        archetype.node.role_path  = read_string_vector(in);
        const auto baseline_count = read_u32(in);
        archetype.baseline_traits.reserve(baseline_count);
        for (uint32_t b = 0; b < baseline_count && in.good(); ++b) {
            PersistenceBaselineTrait baseline;
            baseline.trait           = read_symbol_id(in);
            baseline.assigned_fields = read_string_vector(in);
            archetype.baseline_traits.push_back(std::move(baseline));
        }
        archetype.parameters                = read_string_vector(in);
        archetype.child_roles               = read_string_vector(in);
        archetype.declares_persistent_trait = read_bool(in);
        metadata.archetypes.push_back(std::move(archetype));
    }
    return metadata;
}

void ModuleArtifact::read_template_metadata(std::istream& in, DecoratedProgram& program) {
    const auto count = read_u32(in);
    for (uint32_t i = 0; i < count && in.good(); ++i) {
        const auto name = read_str(in);
        const auto parameter_count = read_u32(in);
        auto& parameters = program.template_parameters[name];
        for (uint32_t j = 0; j < parameter_count && in.good(); ++j) {
            ResolvedTemplateParameter parameter;
            parameter.name = read_str(in);
            parameter.type = read_type_info(in);
            parameter.location = read_location(in);
            if (read_bool(in)) {
                parameter.default_value = read_expression(in);
            }
            parameters.push_back(std::move(parameter));
        }
        if (!read_bool(in)) {
            continue;
        }
        auto blueprint = std::make_shared<TemplateNode>();
        blueprint->name = name;
        blueprint->resolved_template_id = read_optional_symbol_id(in);
        blueprint->traits = read_archetype_traits(in);
        blueprint->children = read_archetype_children(in);
        const auto slot_count = read_u32(in);
        for (uint32_t j = 0; j < slot_count && in.good(); ++j) {
            InitializerSlot slot;
            slot.index = static_cast<std::size_t>(read_u64(in));
            slot.type = read_type_info(in);
            if (read_bool(in)) {
                slot.value = read_expression(in);
            }
            blueprint->initializers.push_back(std::move(slot));
        }
        program.template_blueprints[name] = std::move(blueprint);
    }
}

}  // namespace cactus
