#include "backends/cpp-entt/persistence_restore_emitter.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/persistence_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <sstream>
#include <unordered_set>

namespace cactus {

namespace {

// C++ text resolving an AssetRef/InputRef's declaration name back to this
// build's numeric handle via `lookup_function`, recording a resource-
// preparation failure on `ids` (checked once after reconstruction, before
// publication) when this build declares no such name.
std::string resource_handle_expression(const char* lookup_function,
                                       const char* handle_type,
                                       const std::string& access) {
    return std::string("[&]() { const auto name = (") + access + ").as_declaration(); const auto handle = " +
           "cactus::runtime::entt_backend::" + lookup_function +
           "(name); if (!handle.has_value()) { ids.mark_resource_unresolved(name); return " + handle_type +
           "{0}; } return *handle; }()";
}

// C++ text decoding `access` (a cactus::persistence::Value expression) back
// into the field's native backend representation — the inverse of
// persistence_emitter.cpp's capture_expression, and required to agree with it
// on every case: a field capture can represent must also be restorable, and
// vice versa. Empty when the schema cannot represent the type.
std::string decode_expression(const DecoratedProgram& program, const TypeInfo& type, const std::string& access) {
    switch (describe_persistence_value(type).kind) {
        case PersistenceValueKind::Bool:
            return "cactus::runtime::entt_backend::generated_decode_bool(" + access + ")";
        case PersistenceValueKind::Int:
            return "cactus::runtime::entt_backend::generated_decode_int(" + access + ")";
        case PersistenceValueKind::Float:
            return "cactus::runtime::entt_backend::generated_decode_float(" + access + ")";
        case PersistenceValueKind::String:
            return "cactus::runtime::entt_backend::generated_decode_string(" + access + ")";
        case PersistenceValueKind::Vector: {
            const char* function = "generated_decode_vector2";
            if (type.kind == TypeKind::Vec3) {
                function = "generated_decode_vector3";
            } else if (type.kind == TypeKind::Quat) {
                function = "generated_decode_quat";
            } else if (type.kind == TypeKind::Color) {
                function = "generated_decode_color";
            }
            return std::string("cactus::runtime::entt_backend::") + function + "(" + access + ")";
        }
        case PersistenceValueKind::EntityRef:
            return "cactus::runtime::entt_backend::generated_decode_entity(" + access + ", ids)";
        case PersistenceValueKind::Enum: {
            const auto* declaration = EnttCodegenUtils::find_enum(program, type.name);
            if (declaration == nullptr || EnttComponentEmitter::emit_enum(*declaration).empty()) {
                return {};  // deferred to a raylib enum: no generated variant parser
            }
            const auto enum_cpp = canonical_to_cpp_name(declaration->module_name, declaration->name);
            return "generated_restore_variant_" + enum_cpp + "(" + access + ".as_enum().variant)";
        }
        case PersistenceValueKind::Struct: {
            const auto* declaration = EnttCodegenUtils::find_struct(program, type.name);
            if (declaration == nullptr) {
                return {};
            }
            return "generated_restore_struct_" + canonical_to_cpp_name(declaration->module_name, declaration->name) +
                   "(" + access + ", ids)";
        }
        case PersistenceValueKind::List: {
            if (type.element == nullptr) {
                return {};
            }
            // type_to_cpp() maps EntityId to "uint32_t" for sync's replication
            // representation, not the registry's live entt::entity storage —
            // a list of entity_id needs the latter to hold what
            // generated_decode_entity() actually returns.
            const auto element_cpp = type.element->kind == TypeKind::EntityId
                                         ? std::string("entt::entity")
                                         : EnttCodegenUtils::type_to_cpp(*type.element);
            const auto element_decode = decode_expression(program, *type.element, "item");
            if (element_decode.empty()) {
                return {};
            }
            return "[&]() { std::vector<" + element_cpp + "> items; const auto& list_value = (" + access +
                   ").as_list(); items.reserve(list_value.items.size()); for (const auto& item : "
                   "list_value.items) { items.push_back(" +
                   element_decode + "); } return items; }()";
        }
        case PersistenceValueKind::AssetRef:
            return resource_handle_expression("generated_asset_handle_for_name", "std::uint32_t", access);
        case PersistenceValueKind::InputRef:
            return resource_handle_expression(
                type.kind == TypeKind::InputAxis ? "generated_input_axis_handle_for_name"
                                                 : "generated_input_button_handle_for_name",
                "std::uint8_t", access);
        case PersistenceValueKind::Unsupported:
            return {};
    }
    return {};
}

void emit_restore_enum_functions(std::ostringstream& out, const DecoratedProgram& program) {
    std::unordered_set<std::string> seen;
    for (const auto& [key, declaration] : program.enums) {
        if (!seen.insert(declaration_identity(declaration)).second ||
            EnttComponentEmitter::emit_enum(declaration).empty()) {
            continue;
        }
        const auto enum_cpp = canonical_to_cpp_name(declaration.module_name, declaration.name);
        out << "[[nodiscard]] inline " << enum_cpp << " generated_restore_variant_" << enum_cpp
            << "(std::string_view variant) {\n";
        for (const auto& variant : declaration.variants) {
            out << "    if (variant == \"" << variant << "\") { return " << enum_cpp << "::" << variant << "; }\n";
        }
        out << "    return static_cast<" << enum_cpp << ">(0);\n";
        out << "}\n\n";
    }
}

void emit_restore_struct_functions(std::ostringstream& out, const DecoratedProgram& program) {
    std::unordered_set<std::string> seen;
    for (const auto& [key, declaration] : program.structs) {
        if (!seen.insert(declaration_identity(declaration)).second) {
            continue;
        }
        const auto struct_cpp = canonical_to_cpp_name(declaration.module_name, declaration.name);
        out << "[[nodiscard]] inline " << struct_cpp << " generated_restore_struct_" << struct_cpp
            << "(const cactus::persistence::Value& value, RestoreIdMap& ids) {\n";
        out << "    (void)ids;\n";
        out << "    " << struct_cpp << " result{};\n";
        out << "    const auto& fields = value.as_struct().fields;\n";
        for (const auto& field : declaration.fields) {
            const auto decode = decode_expression(program, field.type, "field->value");
            if (decode.empty()) {
                continue;
            }
            out << "    if (const auto* field = cactus::persistence::find_field(fields, \"" << field.name
                << "\")) {\n";
            out << "        result." << field.name << " = " << decode << ";\n";
            out << "    }\n";
        }
        out << "    return result;\n";
        out << "}\n\n";
    }
}

void emit_restore_trait_functions(std::ostringstream& out,
                                  const DecoratedProgram& program,
                                  const std::vector<PersistableTrait>& traits) {
    for (const auto& trait : traits) {
        if (trait.declaration->fields.empty()) {
            continue;  // marker traits carry no payload to decode
        }
        out << "[[nodiscard]] inline " << trait.cpp_name << " generated_restore_trait_" << trait.cpp_name
            << "(const cactus::persistence::TraitRecord& record, RestoreIdMap& ids) {\n";
        out << "    (void)ids;\n";
        out << "    " << trait.cpp_name << " value{};\n";
        for (const auto& field : trait.declaration->fields) {
            const auto decode = decode_expression(program, field.type, "field->value");
            if (decode.empty()) {
                continue;
            }
            out << "    if (const auto* field = cactus::persistence::find_field("
                << (field.is_persist ? "record.persisted" : "record.construction") << ", \"" << field.name
                << "\")) {\n";
            out << "        value." << field.name << " = " << decode << ";\n";
            out << "    }\n";
        }
        out << "    return value;\n";
        out << "}\n\n";
    }
}

// Applies every recorded present trait to `entity`, decoding it through the
// matching generated_restore_trait_* function and re-establishing capture
// provenance so a world can be re-saved immediately after being restored.
// Absent (present == false) and unrecorded traits are left unset — restore
// creates only what the document actually carries. `traits` must already
// exclude Parent (see without_parent_trait): restore applies it from
// EntityRecord::parent with root/reparent semantics the generic decode path
// does not know, so decoding it again here would wrongly turn an absent
// parent (a root) into a live Parent component pointing at a stale handle.
void emit_restore_node_traits(std::ostringstream& out, const std::vector<PersistableTrait>& traits) {
    out << "inline void generated_restore_node_traits(entt::registry& registry,\n";
    out << "                                          entt::entity entity,\n";
    out << "                                          const cactus::persistence::EntityRecord& record,\n";
    out << "                                          RestoreIdMap& ids) {\n";
    out << "    (void)ids;\n";
    for (const auto& trait : traits) {
        out << "    if (const auto* found = cactus::persistence::find_trait(record, \"" << trait.canonical
            << "\"); found != nullptr && found->present) {\n";
        if (trait.declaration->fields.empty()) {
            out << "        registry.emplace_or_replace<" << trait.cpp_name << ">(entity);\n";
        } else {
            out << "        auto value = generated_restore_trait_" << trait.cpp_name << "(*found, ids);\n";
            out << "        registry.emplace_or_replace<" << trait.cpp_name << ">(entity, value);\n";
            out << "        retain_construction<" << trait.cpp_name << ">(registry, entity, value);\n";
        }
        out << "    }\n";
    }
    out << "}\n\n";
}

}  // namespace

std::string EnttRestoreEmitter::emit_world_restore(const DecoratedProgram& program) {
    if (!EnttPersistenceEmitter::program_retains_provenance(program)) {
        return {};
    }
    const auto traits        = without_parent_trait(persistable_traits(program), program);
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

    emit_restore_enum_functions(out, program);
    emit_restore_struct_functions(out, program);
    emit_restore_trait_functions(out, program, traits);
    emit_restore_node_traits(out, traits);

    out << "// Reconstructs recorded nodes into a freshly staged registry — allocating\n";
    out << "// every record's handle first, so forward references and cycles resolve —\n";
    out << "// then publishes it into the caller's live registry with a single in-place\n";
    out << "// assignment: no partially restored world and no staged entity are ever\n";
    out << "// observable through `registry`. Runs no gameplay spawn/destroy handler.\n";
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
    out << "    entt::registry staged;\n";
    out << "    RestoreIdMap ids;\n";
    out << "    std::vector<entt::entity> allocated;\n";
    out << "    allocated.reserve(snapshot.entities.size());\n";
    out << "    for (const auto& record : snapshot.entities) {\n";
    out << "        const auto entity = staged.create();\n";
    out << "        ids.allocate(record.id, entity);\n";
    out << "        allocated.push_back(entity);\n";
    out << "    }\n";
    out << "    for (std::size_t index = 0; index < snapshot.entities.size(); ++index) {\n";
    out << "        const auto& record = snapshot.entities[index];\n";
    out << "        const auto entity = allocated[index];\n";
    // Validation already guarantees every record names a known archetype;
    // the schema-descriptor check above rejected the document otherwise.
    out << "        const auto node = *generated_archetype_node_index(record.archetype);\n";
    out << "        staged.emplace<ArchetypeOrigin>(entity, ArchetypeOrigin{.node = node});\n";
    out << "        staged.emplace<CreationOrdinal>(entity, CreationOrdinal{.value = "
           "generated_next_creation_ordinal()});\n";
    if (parent_trait != nullptr) {
        const auto parent_cpp = EnttCodegenUtils::trait_cpp_name(parent_trait->symbol_id, parent_trait->name, program);
        out << "        if (record.parent.present) {\n";
        out << "            staged.emplace_or_replace<" << parent_cpp << ">(entity, " << parent_cpp
            << "{.parent = ids.resolve(record.parent)});\n";
        out << "        }\n";
    }
    out << "        generated_restore_node_traits(staged, entity, record, ids);\n";
    out << "    }\n";
    out << "    if (ids.resource_preparation_failed()) {\n";
    out << "        return RestoreOutcome{.ok = false, .code = \"resource_preparation_failure\",\n";
    out << "                              .message = \"no declared asset or input action named '\" + "
           "ids.unresolved_resource_name() + \"'\"};\n";
    out << "    }\n";
    // Publication: reset every holder of a replaced-world handle. Frame-local
    // projections are cleared against the still-live old registry first, so
    // their trackers reset their own internal state correctly; the handle-
    // only holders below need no registry and can clear in any order.
    out << "    clear_projected_traits(registry);\n";
    out << "    registry = std::move(staged);\n";
    out << "    reset_pointer_router_state();\n";
    out << "    reset_pending_destruction_state();\n";
    out << "    reset_editor_camera_rig_state();\n";
    // Resets the whole SchedulerState, not just .activation: per-phase
    // runtime state (fixed-step accumulators and catch-up counters) also
    // holds backlog that must not carry into the restored world.
    out << "    generated_reset_scheduler_state();\n";
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
