#include "backends/cpp-entt/persistence_restore_emitter.hpp"

#include "backends/cpp-entt/construction_retention.hpp"
#include "backends/cpp-entt/persistence_emitter.hpp"
#include "backends/cpp-entt/range_checks.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <vector>

namespace cactus {

namespace {

constexpr const char* kRuntime = "cactus::runtime::entt_backend::";

// C++ expression reading one value of `type` from `in` (a ValueReader over a
// layout this program records) back into the backend's native
// representation — the inverse of persistence_emitter.cpp's put_statement.
std::string read_expression(const DecoratedProgram& program, const TypeInfo& type) {
    switch (describe_persistence_value(type).kind) {
        case PersistenceValueKind::Bool:
            return "in.get_bool()";
        case PersistenceValueKind::Int:
            return "in.get_int()";
        case PersistenceValueKind::Float:
            return "in.get_float()";
        case PersistenceValueKind::String:
            return "std::string(in.get_name())";
        case PersistenceValueKind::Vector: {
            const char* function = "read_persistence_vector2";
            if (type.kind == TypeKind::Vec3) {
                function = "read_persistence_vector3";
            } else if (type.kind == TypeKind::Quat) {
                function = "read_persistence_quat";
            } else if (type.kind == TypeKind::Color) {
                function = "read_persistence_color";
            }
            return std::string(kRuntime) + function + "(in)";
        }
        case PersistenceValueKind::EntityRef:
            return "ids.resolve(in.get_entity())";
        case PersistenceValueKind::Enum: {
            // A layout this program records lists the variants in declaration order.
            const auto* declaration = EnttCodegenUtils::find_enum(program, type.name);
            return "static_cast<" + cpp_type_ref(SymbolKind::Enum, declaration->module_name, declaration->name) +
                   ">(in.get_index())";
        }
        case PersistenceValueKind::Struct: {
            const auto* declaration = EnttCodegenUtils::find_struct(program, type.name);
            return "generated_read_struct<" +
                   cpp_type_ref(SymbolKind::Struct, declaration->module_name, declaration->name) + ">(in, ids)";
        }
        case PersistenceValueKind::List: {
            // type_to_cpp() maps EntityId to "uint32_t", not the registry's live
            // entt::entity storage — a list of entity_id needs the latter.
            const auto element_cpp = type.element->kind == TypeKind::EntityId
                                         ? std::string("entt::entity")
                                         : EnttCodegenUtils::type_to_cpp(*type.element);
            return std::string(kRuntime) + "read_persistence_list<" + element_cpp + ">(in, [&]() { return " +
                   read_expression(program, *type.element) + "; })";
        }
        case PersistenceValueKind::AssetRef:
            return std::string(kRuntime) + "read_persistence_handle<std::uint32_t>(in, ids, " + kRuntime +
                   "generated_asset_handle_for_name)";
        case PersistenceValueKind::InputRef:
            return std::string(kRuntime) + "read_persistence_handle<std::uint8_t>(in, ids, " + kRuntime +
                   (type.kind == TypeKind::InputAxis ? "generated_input_axis_handle_for_name)"
                                                     : "generated_input_button_handle_for_name)");
        case PersistenceValueKind::Unsupported:
            return {};
    }
    return {};
}

// A trait's layout holds its construction fields, then its persisted ones.
std::vector<const ResolvedField*> trait_layout_fields(const DecoratedProgram& program,
                                                      const std::vector<ResolvedField>& fields) {
    auto ordered         = persisting_fields(program, fields, false);
    const auto persisted = persisting_fields(program, fields, true);
    ordered.insert(ordered.end(), persisted.begin(), persisted.end());
    return ordered;
}

void emit_restore_struct_functions(std::ostringstream& out, const DecoratedProgram& program) {
    out << "template <class T>\n";
    out << "[[nodiscard]] T generated_read_struct(cactus::persistence::ValueReader& in, RestoreIdMap& ids);\n\n";
    const auto structs = unique_structs(program);
    for (const auto* declaration : structs) {
        const auto struct_cpp = cpp_type_ref(SymbolKind::Struct, declaration->module_name, declaration->name);
        out << "template <>\n";
        out << "[[nodiscard]] inline " << struct_cpp << " generated_read_struct<" << struct_cpp
            << ">(cactus::persistence::ValueReader& in, RestoreIdMap& ids);\n";
    }
    out << "\n";
    for (const auto* declaration : structs) {
        const auto struct_cpp = cpp_type_ref(SymbolKind::Struct, declaration->module_name, declaration->name);
        out << "template <>\n";
        out << "[[nodiscard]] inline " << struct_cpp << " generated_read_struct<" << struct_cpp
            << ">(cactus::persistence::ValueReader& in, RestoreIdMap& ids) {\n";
        out << "    (void)in;\n";
        out << "    (void)ids;\n";
        out << "    " << struct_cpp << " result{};\n";
        for (const auto* field : persisting_fields(program, declaration->fields)) {
            out << "    result." << cpp_escaped(field->name) << " = " << read_expression(program, field->type) << ";\n";
        }
        out << "    return result;\n";
        out << "}\n\n";
    }
}

void emit_restore_trait_functions(std::ostringstream& out,
                                  const DecoratedProgram& program,
                                  const std::vector<PersistableTrait>& traits) {
    out << "template <class T>\n";
    out << "[[nodiscard]] T generated_read_trait(cactus::persistence::ValueReader& in, RestoreIdMap& ids);\n\n";
    for (const auto& trait : traits) {
        if (trait.declaration->fields.empty()) {
            continue;  // marker traits carry no payload to decode
        }
        out << "template <>\n";
        out << "[[nodiscard]] inline " << trait.cpp_name << " generated_read_trait<" << trait.cpp_name
            << ">(cactus::persistence::ValueReader& in, RestoreIdMap& ids) {\n";
        out << "    (void)in;\n";
        out << "    (void)ids;\n";
        out << "    " << trait.cpp_name << " value{};\n";
        for (const auto* field : trait_layout_fields(program, trait.declaration->fields)) {
            out << "    value." << cpp_escaped(field->name) << " = " << read_expression(program, field->type) << ";\n";
        }
        out << "    return value;\n";
        out << "}\n\n";
    }
}

void emit_restore_construction_copy(std::ostringstream& out,
                                    const DecoratedProgram& program,
                                    const PersistableTrait& trait,
                                    const ConstructionRetention& retention) {
    const auto& symbol = trait.declaration->symbol_id;
    if (retention.retains_everywhere(symbol)) {
        out << "                retain_construction<" << trait.cpp_name << ">(registry, entity, value);\n";
        return;
    }
    if (!retention.retains_anywhere(symbol)) {
        return;
    }
    const auto nodes = program.persistence.archetypes.size();
    out << "                static constexpr std::array<bool, " << nodes << "> kKeepsCopy{";
    for (std::size_t node = 0; node < nodes; ++node) {
        out << (node == 0 ? "" : ", ")
            << (retention.retains(static_cast<std::uint32_t>(node), symbol) ? "true" : "false");
    }
    out << "};\n";
    out << "                if (kKeepsCopy[node]) {\n";
    out << "                    retain_construction<" << trait.cpp_name << ">(registry, entity, value);\n";
    out << "                }\n";
}

// Applies every recorded present trait to `entity`, decoding it through the
// matching generated_read_trait<T> specialization and re-establishing capture
// provenance so a world can be re-saved immediately after being restored.
// Absent (present == false) and unrecorded traits are left unset — restore
// creates only what the document actually carries. `traits` must already
// exclude Parent (see without_parent_trait): restore applies it from the entity
// row's parent with root/reparent semantics the generic decode path does not
// know. `layouts` maps each document layout to its index in `layout_traits`.
void emit_restore_node_traits(std::ostringstream& out,
                              const DecoratedProgram& program,
                              const std::vector<PersistableTrait>& layout_traits,
                              const std::vector<PersistableTrait>& traits) {
    const ConstructionRetention retention(program, persistable_symbols(traits));
    out << "inline void generated_restore_node_traits(entt::registry& registry,\n";
    out << "                                          entt::entity entity,\n";
    out << "                                          std::uint32_t node,\n";
    out << "                                          const cactus::persistence::Snapshot& snapshot,\n";
    out << "                                          const cactus::persistence::EntityRow& row,\n";
    out << "                                          std::span<const std::uint32_t> layouts,\n";
    out << "                                          RestoreIdMap& ids) {\n";
    out << "    (void)node;\n";
    out << "    for (std::uint32_t offset = 0; offset < row.trait_count; ++offset) {\n";
    out << "        const auto& trait = snapshot.trait_rows()[row.first_trait + offset];\n";
    out << "        if (!trait.present) {\n";
    out << "            continue;\n";
    out << "        }\n";
    out << "        auto in = cactus::persistence::TraitView(snapshot, row.first_trait + offset).reader();\n";
    out << "        (void)in;\n";
    out << "        switch (layouts[trait.shape]) {\n";
    for (const auto& trait : traits) {
        out << "            case " << layout_index(layout_traits, trait.canonical) << ": {\n";
        if (trait.declaration->fields.empty()) {
            out << "                registry.emplace_or_replace<" << trait.cpp_name << ">(entity);\n";
        } else {
            out << "                auto value = generated_read_trait<" << trait.cpp_name << ">(in, ids);\n";
            out << "                registry.emplace_or_replace<" << trait.cpp_name << ">(entity, value);\n";
            emit_restore_construction_copy(out, program, trait, retention);
        }
        out << "                break;\n";
        out << "            }\n";
    }
    out << "            default:\n";
    out << "                break;\n";
    out << "        }\n";
    out << "    }\n";
    out << "}\n\n";
}

// One decoder per stored relation: fields by name, so any layout validation accepted reads back.
void emit_restore_relation_functions(std::ostringstream& out, const DecoratedProgram& program) {
    const auto relations = stored_relations(program);
    for (std::size_t index = 0; index < relations.size(); ++index) {
        const auto& relation = *relations[index];
        const auto row       = EnttCodegenUtils::relation_cpp_name(*relation.symbol_id);
        out << "inline void generated_restore_relation_" << index
            << "(entt::registry& staged, const cactus::persistence::Snapshot& source,\n";
        out << "                                     const cactus::persistence::RelationRow& row, RestoreIdMap& ids) "
               "{\n";
        out << "    " << row << " value{};\n";
        const auto fields = persisting_fields(program, relation.fields);
        if (fields.empty()) {
            out << "    (void)source;\n";
        } else {
            out << "    cactus::persistence::ValueReader in(source, source.payload(row.values, false));\n";
            out << "    for (const auto& field : source.trait_shapes()[row.values.shape].construction) {\n";
            out << "        const auto name = source.name(field.name);\n";
            for (const auto* field : fields) {
                out << "        if (name == \"" << field->name << "\") {\n";
                out << "            value." << cpp_escaped(field->name) << " = " << read_expression(program, field->type)
                    << ";\n";
                out << "            continue;\n";
                out << "        }\n";
            }
            out << "        source.skip_value(field.type, in);\n";
            out << "    }\n";
        }
        out << "    relation_store<" << row << ">(staged).add(ids.resolve({.id = row.from, .present = true}),\n";
        out << "                                          ids.resolve({.id = row.to, .present = true}), value);\n";
        out << "}\n\n";
    }
}

// Rebuilds stored rows after every entity of the staged world exists.
void emit_restore_relation_rows(std::ostringstream& out, const DecoratedProgram& program) {
    const auto relations = stored_relations(program);
    if (relations.empty()) {
        return;
    }
    out << "    for (const auto& row : source.relation_rows()) {\n";
    out << "        const auto relation = source.relation_name(row);\n";
    for (std::size_t index = 0; index < relations.size(); ++index) {
        out << "        if (relation == \"" << relations[index]->canonical_id << "\") {\n";
        out << "            generated_restore_relation_" << index << "(staged, source, row, ids);\n";
        out << "        }\n";
    }
    out << "    }\n";
}

// Rejects a staged world holding a ranged value outside its range. A container
// handle stale in the document is accepted: the value was valid when made.
void emit_staged_range_checks(std::ostringstream& out,
                              const DecoratedProgram& program,
                              const std::vector<PersistableTrait>& traits) {
    for (const auto& trait : traits) {
        const auto* ranged = find_ranged_trait(program, trait.declaration->symbol_id, trait.declaration->name);
        if (ranged == nullptr) {
            continue;
        }
        out << "    for (const auto entity : staged.view<" << trait.cpp_name << ">()) {\n";
        out << "        const auto fault = " << range_check_function_name(ranged->module_name, ranged->name).ref()
            << "(staged, staged.get<" << trait.cpp_name << ">(entity));\n";
        out << "        if (fault.has_value() && fault->reason != RangeFaultReason::StaleContainer) {\n";
        out << "            return RestoreOutcome{\n";
        out << "                .ok = false, .code = \"invalid_data\", .message = describe_range_fault(*fault)};\n";
        out << "        }\n";
        out << "    }\n";
    }
}

}  // namespace

std::string EnttRestoreEmitter::emit_world_restore(const DecoratedProgram& program) {
    if (!EnttPersistenceEmitter::program_retains_provenance(program)) {
        return {};
    }
    const auto layout_traits = persistable_traits(program);
    const auto traits        = without_parent_trait(layout_traits, program);
    const auto* parent_trait = EnttCodegenUtils::find_trait(program, "Parent");

    std::ostringstream out;
    out << "// ── World Persistence Restore ───────────────────────────────────────\n\n";
    out << EnttPersistenceEmitter::emit_reference_name_reverse_lookups(program);
    out << EnttPersistenceEmitter::emit_archetype_node_reverse_lookup();

    out << "namespace cactus::runtime::entt_backend {\n\n";
    // Defined later (emit_graph_scheduler_state, after SchedulerState's own
    // definition): forward-declared here so generated_restore_world's
    // publication step resolves it without needing the full type visible
    // this early in the file.
    out << "void generated_reset_scheduler_state();\n\n";
    const bool tracks_lifecycle = EnttCodegenUtils::tracks_lifecycle(program);
    if (tracks_lifecycle) {
        out << "void generated_connect_lifecycle_trackers(entt::registry& registry);\n\n";
    }

    emit_restore_struct_functions(out, program);
    emit_restore_trait_functions(out, program, traits);
    emit_restore_node_traits(out, program, layout_traits, traits);
    emit_restore_relation_functions(out, program);

    out << "// Reconstructs recorded nodes into a freshly staged registry — allocating\n";
    out << "// every record's handle first, so forward references and cycles resolve —\n";
    out << "// then publishes it into the caller's live registry with a single in-place\n";
    out << "// assignment: no partially restored world and no staged entity are ever\n";
    out << "// observable through `registry`. Runs no gameplay handler or lifecycle trigger.\n";
    out << "// Publication also resets every runtime holder of a replaced-world handle\n";
    out << "// (pointer hover/capture, the pending-destruction cascade guard, the editor\n";
    out << "// camera rig, and the whole scheduler's fixed-step/catch-up state) so none\n";
    out << "// of them can reference an entity the restore just discarded. Ineligible\n";
    out << "// content (cameras, HUD, anything with no persist field) does not survive —\n";
    out << "// rebuild it from a RestoreCompleted handler, not from a holder reset.\n";
    out << "[[nodiscard]] inline RestoreOutcome generated_restore_world(\n";
    out << "    entt::registry& registry, const cactus::persistence::Snapshot& snapshot) {\n";
    out << "    const auto problems = cactus::persistence::validate_document(generated_schema, snapshot);\n";
    out << "    if (!problems.empty()) {\n";
    out << "        std::string message = problems.front().field_path + \": \" + problems.front().message;\n";
    out << "        for (std::size_t index = 1; index < problems.size(); ++index) {\n";
    out << "            message += \"; \" + problems[index].field_path + \": \" + problems[index].message;\n";
    out << "        }\n";
    // Three standard restore error codes come out of validation: an
    // incompatible schema descriptor short-circuits to a single "$schema"
    // problem (see cactus::persistence::validate_document); otherwise a
    // structural problem (unknown archetype/trait, duplicate/dangling
    // identity, hierarchy cycle, configured limits) takes priority over a
    // value-kind/content problem, since the latter may not even be
    // meaningful until the document's structure itself is sound.
    out << "        const bool any_structural = std::ranges::any_of(problems, [](const auto& problem) {\n";
    out << "            return problem.category == cactus::persistence::ValidationErrorCategory::Structural;\n";
    out << "        });\n";
    out << "        std::string code;\n";
    out << "        if (problems.front().field_path == \"$schema\") {\n";
    out << "            code = \"incompatible_schema\";\n";
    out << "        } else if (any_structural) {\n";
    out << "            code = \"invalid_data\";\n";
    out << "        } else {\n";
    out << "            code = \"unsupported_value\";\n";
    out << "        }\n";
    out << "        return RestoreOutcome{.ok = false, .code = std::move(code), .message = std::move(message)};\n";
    out << "    }\n";
    // The typed decoders read this program's layouts; a valid document that
    // records a trait another way (written by an adapter, say) is copied into
    // them by name first.
    out << "    cactus::persistence::SnapshotBuilder canonical;\n";
    out << "    const auto tables = generated_register_document_tables(canonical);\n";
    out << "    auto match = cactus::persistence::match_trait_layouts(snapshot, canonical.document(), tables.traits);\n";
    out << "    std::optional<cactus::persistence::Snapshot> relaid;\n";
    out << "    if (!match.complete) {\n";
    out << "        relaid = cactus::persistence::relayout(snapshot, std::move(canonical));\n";
    out << "        match  = cactus::persistence::match_trait_layouts(*relaid, *relaid, tables.traits);\n";
    out << "        if (!match.complete) {\n";
    out << "            return RestoreOutcome{.ok = false, .code = \"invalid_data\",\n";
    out << "                                  .message = \"a trait is recorded in a layout this program cannot read\"};\n";
    out << "        }\n";
    out << "    }\n";
    out << "    const auto& source = relaid.has_value() ? *relaid : snapshot;\n";
    out << "    entt::registry staged;\n";
    out << "    RestoreIdMap ids;\n";
    out << "    ids.reserve(source.entity_count());\n";
    out << "    std::vector<entt::entity> allocated;\n";
    out << "    allocated.reserve(source.entity_count());\n";
    out << "    for (const auto& row : source.entity_rows()) {\n";
    out << "        const auto entity = create_entity(staged);\n";
    out << "        ids.allocate(row.id, entity);\n";
    out << "        allocated.push_back(entity);\n";
    out << "    }\n";
    out << "    ids.seal();\n";
    // Validation already guarantees every record names a known archetype.
    out << "    std::vector<std::uint32_t> node_of_name(source.names().size(), cactus::persistence::kNoIndex);\n";
    out << "    for (std::size_t index = 0; index < source.entity_count(); ++index) {\n";
    out << "        const auto& row   = source.entity_rows()[index];\n";
    out << "        const auto entity = allocated[index];\n";
    out << "        auto& node        = node_of_name[row.archetype];\n";
    out << "        if (node == cactus::persistence::kNoIndex) {\n";
    out << "            node = *generated_archetype_node_index(source.name(row.archetype));\n";
    out << "        }\n";
    out << "        staged.emplace<ArchetypeOrigin>(entity, ArchetypeOrigin{.node = node});\n";
    out << "        staged.emplace<CreationOrdinal>(entity, CreationOrdinal{.value = "
           "generated_next_creation_ordinal()});\n";
    if (parent_trait != nullptr) {
        const auto parent_cpp = EnttCodegenUtils::trait_cpp_name(parent_trait->symbol_id, parent_trait->name, program);
        out << "        if (row.parent.present) {\n";
        out << "            staged.emplace_or_replace<" << parent_cpp << ">(entity, " << parent_cpp
            << "{.parent = ids.resolve(row.parent)});\n";
        out << "        }\n";
    }
    out << "        generated_restore_node_traits(staged, entity, node, source, row, match.traits, ids);\n";
    out << "    }\n";
    emit_restore_relation_rows(out, program);
    emit_staged_range_checks(out, program, traits);
    out << "    if (ids.resource_preparation_failed()) {\n";
    out << "        return RestoreOutcome{.ok = false, .code = \"resource_preparation_failure\",\n";
    out << "                              .message = \"no declared asset or input action named '\" + "
           "ids.unresolved_resource_name() + \"'\"};\n";
    out << "    }\n";
    // Publication: reset every holder of a replaced-world handle. Frame-local
    // projections are cleared against the still-live old registry first, so
    // their trackers reset their own internal state correctly; the handle-
    // only holders below need no registry and can clear in any order.
    out << "    ::cactus_gen::detail::clear_projected_traits(registry);\n";
    out << "    registry = std::move(staged);\n";
    out << "    advance_binding_index_epoch();\n";
    out << "    reset_pointer_router_state();\n";
    out << "    reset_editor_camera_rig_state();\n";
    // Resets the whole SchedulerState, not just .activation: per-phase
    // runtime state (fixed-step accumulators and catch-up counters) also
    // holds backlog that must not carry into the restored world.
    out << "    generated_reset_scheduler_state();\n";
    if (parent_trait != nullptr) {
        out << "    (void)" << EnttCodegenUtils::hierarchy_index_expr(program) << ";\n";
    }
    // The staged registry never had storage signals connected. Restore fires
    // no lifecycle trigger itself (recording is off outside a commit).
    if (tracks_lifecycle) {
        out << "    generated_connect_lifecycle_trackers(registry);\n";
    }
    if (!program.relations.empty()) {
        out << "    generated_connect_relations(registry);\n";
    }
    if (!EnttCodegenUtils::declared_entities(program).empty()) {
        out << "    ::cactus_gen::detail::generated_rebind_named_slots(registry);\n";
    }
    out << "    return RestoreOutcome{.ok = true};\n";
    out << "}\n\n";

    out << "// Reads one document through the registered adapter and, if it\n";
    out << "// decodes and reads as schema-compatible, hands it to\n";
    out << "// generated_restore_world. An adapter read failure (including\n";
    out << "// adapter_unavailable/incompatible_schema from read_persistence_\n";
    out << "// document itself) never reaches generated_restore_world at all.\n";
    out << "[[nodiscard]] inline PersistenceOutcome generated_execute_restore_request(\n";
    out << "    entt::registry& registry, const std::string& slot, int request_id,\n";
    out << "    const cactus::persistence::SchemaDescriptor& schema) {\n";
    out << "    const auto read = read_persistence_document(slot, schema);\n";
    out << "    if (!read.ok) {\n";
    out << "        return PersistenceOutcome{\n";
    out << "            .ok = false, .slot = slot, .request_id = request_id, .code = read.code, .message = "
           "read.message};\n";
    out << "    }\n";
    out << "    const auto outcome = generated_restore_world(registry, read.snapshot);\n";
    out << "    if (!outcome.ok) {\n";
    out << "        return PersistenceOutcome{\n";
    out << "            .ok = false, .slot = slot, .request_id = request_id, .code = outcome.code, .message = "
           "outcome.message};\n";
    out << "    }\n";
    out << "    return PersistenceOutcome{.ok = true, .slot = slot, .request_id = request_id};\n";
    out << "}\n\n";

    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

}  // namespace cactus
