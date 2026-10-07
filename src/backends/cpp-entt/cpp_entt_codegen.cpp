#include "backends/cpp-entt/cpp_entt_codegen.hpp"
#include "common/ast_expressions.hpp"

#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/component_emitter.hpp"
#include "backends/cpp-entt/event_emitter.hpp"
#include "backends/cpp-entt/persistence_emitter.hpp"
#include "backends/cpp-entt/persistence_restore_emitter.hpp"
#include "backends/cpp-entt/render_pass_emitter.hpp"
#include "backends/cpp-entt/system_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cactus {

namespace {

std::string snake_case(const std::string& value);
std::string archetype_create_at_function_name(const std::string& module_name, const std::string& archetype_name);
const ResolvedEvent* find_external_frame_event(const DecoratedProgram& program);
const ResolvedEvent* find_std_core_event(const DecoratedProgram& program,
                                         std::string_view name,
                                         bool require_external = false);
const ResolvedEvent* find_std_persistence_event(const DecoratedProgram& program, std::string_view name);
std::string emit_persistence_boundary_processor(const DecoratedProgram& program);
bool program_has_event_handler(const DecoratedProgram& program, const SymbolId& event_symbol);
const PhasePlan* find_render_phase(const DecoratedProgram& program);
bool creation_takes_parameters(const std::vector<InitializerSlot>& slots);

bool program_uses_module(const DecoratedProgram& program, std::string_view module_name) {
    if (program.ast == nullptr) {
        return false;
    }
    for (const auto& decl : program.ast->declarations) {
        if (const auto* use = std::get_if<UseNode>(&decl)) {
            if (use->module_name == module_name) {
                return true;
            }
        }
    }
    return false;
}

bool uses_text_format(const DecoratedProgram& program) {
    return program_uses_module(program, "std.text");
}
bool module_uses_camera_flat(const DecoratedProgram& program) {
    return program_uses_module(program, "std.camera.flat");
}
bool module_uses_camera_viewport(const DecoratedProgram& program) {
    return program_uses_module(program, "std.camera.viewport");
}
bool module_uses_camera_volume(const DecoratedProgram& program) {
    return program_uses_module(program, "std.camera.volume");
}
bool module_uses_editor(const DecoratedProgram& program) {
    return program_uses_module(program, "std.editor");
}

// Task 6.1: Check if the program has any extern funcs requiring the runtime header
bool has_extern_funcs(const DecoratedProgram& program) {
    return std::ranges::any_of(program.funcs, [](const auto& entry) { return entry.second.is_extern; });
}

std::string ast_event_cpp_name(const EventNode& node, const DecoratedProgram& program) {
    const auto& module = node.module_name.empty() ? program.module_name : node.module_name;
    return canonical_to_cpp_name(module, node.name) + "Event";
}

std::string resolved_event_cpp_name(const ResolvedEvent& event) {
    return canonical_to_cpp_name(event.module_name, event.name) + "Event";
}

// AST declaration order when an AST exists, canonical-id order otherwise.
std::string emit_event_structs(const DecoratedProgram& program) {
    std::ostringstream out;
    if (program.ast != nullptr) {
        std::unordered_map<std::string, const ResolvedEvent*> by_canonical_id;
        by_canonical_id.reserve(program.events.size());
        for (const auto& [_, event] : program.events) {
            by_canonical_id.emplace(event.canonical_id, &event);
        }
        for (const auto& decl : program.ast->declarations) {
            const auto* node = std::get_if<EventNode>(&decl);
            if (node == nullptr) {
                continue;
            }
            const auto found = node->resolved_event_id.has_value()
                                   ? by_canonical_id.find(make_canonical_id(*node->resolved_event_id))
                                   : by_canonical_id.end();
            if (found == by_canonical_id.end()) {
                throw std::runtime_error("cpp-entt event '" + node->name + "' has no resolved event declaration");
            }
            out << EnttEventEmitter::emit_event(*found->second, ast_event_cpp_name(*node, program)) << "\n";
        }
        return out.str();
    }

    std::vector<const ResolvedEvent*> events;
    events.reserve(program.events.size());
    for (const auto& [_, event] : program.events) {
        events.push_back(&event);
    }
    std::ranges::sort(events, {}, &ResolvedEvent::canonical_id);
    for (const auto* event : events) {
        out << EnttEventEmitter::emit_event(*event, resolved_event_cpp_name(*event)) << "\n";
    }
    return out.str();
}

std::string event_runtime_cpp_type(const DecoratedProgram& program, const SymbolId& event) {
    if (program.ast != nullptr) {
        for (const auto& decl : program.ast->declarations) {
            if (const auto* node = std::get_if<EventNode>(&decl);
                node != nullptr && node->resolved_event_id.has_value() && *node->resolved_event_id == event) {
                return ast_event_cpp_name(*node, program);
            }
        }
    }
    const auto canonical = make_canonical_id(event);
    for (const auto& [_, resolved] : program.events) {
        if ((resolved.symbol_id.has_value() && *resolved.symbol_id == event) || resolved.canonical_id == canonical) {
            return resolved_event_cpp_name(resolved);
        }
    }
    return event_cpp_type_name(event);
}

std::string handler_trigger_cpp_type(const DecoratedProgram& program, const ResolvedHandlerTrigger& trigger) {
    if (trigger.kind == HandlerTriggerKind::Event) {
        return event_runtime_cpp_type(program, trigger.symbol);
    }
    return "cactus::runtime::entt_backend::" + canonical_to_cpp_name(trigger.symbol) + "PhaseRuntimeState";
}

std::string external_handler_callback_name(const HandlerIdentity& identity) {
    return "cactus_external__" + canonical_to_cpp_name(identity.rule) + "__on__" +
           canonical_to_cpp_name(identity.trigger.symbol);
}

std::vector<SymbolId> sorted_symbols(const std::unordered_set<SymbolId>& symbols) {
    std::vector<SymbolId> result(symbols.begin(), symbols.end());
    std::ranges::sort(
        result, [](const auto& left, const auto& right) { return make_canonical_id(left) < make_canonical_id(right); });
    return result;
}

std::vector<std::string> sorted_strings(const std::unordered_set<std::string>& strings) {
    std::vector<std::string> result(strings.begin(), strings.end());
    std::ranges::sort(result);
    return result;
}

std::string external_handler_capability_name(const HandlerIdentity& identity) {
    return "Capabilities__" + canonical_to_cpp_name(identity.rule) + "__on__" +
           canonical_to_cpp_name(identity.trigger.symbol);
}

std::string external_handler_command_method_name(const InferredHandlerCommand& command) {
    std::string name = "command_" + std::string(handler_command_kind_name(command.kind));
    if (command.target.has_value()) {
        name += "_" + canonical_to_cpp_name(*command.target);
    }
    return name;
}

std::string external_handler_effect_method_name(const std::string& effect) {
    std::string name = "effect_";
    for (const char character : effect) {
        if (character == '.') {
            name += "__";
        } else if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_') {
            name += character;
        } else {
            name += '_';
        }
    }
    return name;
}

const TemplateNode* find_template(const DecoratedProgram& program, const SymbolId& template_id) {
    if (program.ast == nullptr) {
        return nullptr;
    }
    for (const auto& declaration : program.ast->declarations) {
        const auto* template_node = std::get_if<TemplateNode>(&declaration);
        if (template_node != nullptr && template_node->resolved_template_id.has_value() &&
            *template_node->resolved_template_id == template_id) {
            return template_node;
        }
    }
    return nullptr;
}

const ExternRuleNode* find_external_rule(const DecoratedProgram& program, const SymbolId& rule_id) {
    if (program.ast == nullptr) {
        return nullptr;
    }
    for (const auto& declaration : program.ast->declarations) {
        const auto* rule = std::get_if<ExternRuleNode>(&declaration);
        if (rule != nullptr && rule->resolved_rule_id.has_value() && *rule->resolved_rule_id == rule_id) {
            return rule;
        }
    }
    return nullptr;
}

bool is_user_external_handler(const DecoratedProgram& program, const HandlerNode& node) {
    const auto* rule = find_external_rule(program, node.identity.rule);
    return node.implementation == HandlerImplementationKind::External && (rule == nullptr || !rule->is_stdlib);
}

// The generated function of a Cactus rule handler, found through the AST.
std::optional<std::string> cactus_handler_callee(const DecoratedProgram& program, const HandlerIdentity& identity) {
    for (const auto& declaration : program.ast->declarations) {
        const auto* rule = std::get_if<RuleNode>(&declaration);
        if (rule == nullptr || !rule->resolved_rule_id.has_value() || *rule->resolved_rule_id != identity.rule) {
            continue;
        }
        const auto handler = std::ranges::find_if(rule->handlers, [&](const auto& candidate) {
            return candidate.resolved_trigger.has_value() && *candidate.resolved_trigger == identity.trigger;
        });
        if (handler != rule->handlers.end()) {
            return system_function_name(
                program.module_name, rule->name, EnttCodegenUtils::handler_function_suffix(*handler, program));
        }
    }
    return std::nullopt;
}

bool has_added_trigger(const DecoratedProgram& program) {
    return std::ranges::any_of(program.execution_graph.handlers, [](const auto& handler) {
        return handler.identity.trigger.kind == HandlerTriggerKind::TraitAdded;
    });
}

// Commit can deliver either kind for any watched trait, so both are occurrences.
std::vector<ResolvedHandlerTrigger> lifecycle_occurrence_triggers(const DecoratedProgram& program) {
    std::vector<ResolvedHandlerTrigger> triggers;
    for (const auto& trait : EnttCodegenUtils::watched_lifecycle_traits(program)) {
        triggers.push_back({.kind = HandlerTriggerKind::TraitAdded, .symbol = trait});
        triggers.push_back({.kind = HandlerTriggerKind::TraitRemoved, .symbol = trait});
    }
    return triggers;
}

std::string lifecycle_trackers_cpp_type(const DecoratedProgram& program) {
    std::string traits;
    for (const auto& trait : EnttCodegenUtils::watched_lifecycle_traits(program)) {
        traits += (traits.empty() ? "" : ", ") + EnttCodegenUtils::trait_cpp_name(trait, trait.local_name, program);
    }
    return "LifecycleTrackers<" + traits + ">";
}

bool has_compiler_owned_external_handler(const DecoratedProgram& program, const ExternRuleNode& rule) {
    if (!rule.resolved_rule_id.has_value()) {
        return false;
    }
    return std::ranges::any_of(program.execution_graph.handlers, [&](const auto& handler) {
        return handler.identity.rule == *rule.resolved_rule_id &&
               handler.implementation == HandlerImplementationKind::External &&
               !is_user_external_handler(program, handler);
    });
}

bool has_user_external_handler(const DecoratedProgram& program, const ExternRuleNode& rule) {
    if (!rule.resolved_rule_id.has_value()) {
        return false;
    }
    return std::ranges::any_of(program.execution_graph.handlers, [&](const auto& handler) {
        return handler.identity.rule == *rule.resolved_rule_id && is_user_external_handler(program, handler);
    });
}

std::string emit_external_command_forward_declarations(const DecoratedProgram& program) {
    if (program.execution_graph.phases.empty()) {
        return {};
    }

    std::unordered_set<std::string> declarations;
    for (const auto& node : program.execution_graph.handlers) {
        if (!is_user_external_handler(program, node)) {
            continue;
        }
        for (const auto& command : node.contract.commands) {
            if (command.kind != HandlerCommandKind::Spawn || !command.target.has_value()) {
                continue;
            }
            const auto* template_node = find_template(program, *command.target);
            if (template_node == nullptr) {
                throw std::runtime_error("cpp-entt cannot lower external spawn capability for missing template '" +
                                         make_canonical_id(*command.target) + "'");
            }
            if (creation_takes_parameters(template_node->initializers)) {
                throw std::runtime_error(
                    "cpp-entt cannot lower external spawn capability for parameterized template '" +
                    make_canonical_id(*command.target) +
                    "': an external handler has no place to supply template arguments");
            }
            declarations.insert("entt::entity " +
                                archetype_create_at_function_name(program.module_name, template_node->name) +
                                "(entt::registry& registry, entt::entity hint);");
        }
    }
    if (EnttCodegenUtils::find_trait(program, "Parent") != nullptr) {
        declarations.insert(
            "[[maybe_unused]] static void cactus_destroy_entity_recursive("
            "entt::registry& registry, entt::entity entity);");
    }
    if (declarations.empty()) {
        return {};
    }

    std::vector<std::string> ordered(declarations.begin(), declarations.end());
    std::ranges::sort(ordered);
    std::ostringstream out;
    out << "// ── External Command Target Declarations ─────────────────────────────\n\n";
    for (const auto& declaration : ordered) {
        out << declaration << "\n";
    }
    out << "\n";
    return out.str();
}

// Optional fields keep the capability queue-only: the callback names values
// to write and never sees the current ones.
void emit_external_set_capability(std::ostringstream& out,
                                  const DecoratedProgram& program,
                                  const InferredHandlerCommand& command,
                                  const std::string& method_name) {
    if (!command.target.has_value()) {
        throw std::runtime_error("cpp-entt received an external set capability without a target");
    }
    const auto type            = EnttCodegenUtils::trait_cpp_name(*command.target);
    const auto canonical_id    = make_canonical_id(*command.target);
    const auto* resolved_trait = EnttCodegenUtils::find_trait(program, canonical_id);
    if (resolved_trait == nullptr) {
        throw std::runtime_error("cpp-entt cannot lower external set capability for missing trait '" + canonical_id +
                                 "'");
    }
    const auto patch_type = type + "_patch";
    out << "    struct " << patch_type << " {\n";
    for (const auto& field : resolved_trait->fields) {
        out << "        std::optional<decltype(" << type << "::" << field.name << ")> " << field.name << ";\n";
    }
    out << "    };\n";
    out << "    void " << method_name << "(entt::entity target, " << patch_type << " patch) const {\n";
    out << "        generated_queue_structural_command(StructuralCommand::Kind::Set,\n";
    out << "            [target, patch = std::move(patch)](entt::registry& registry) {\n";
    out << "                auto* value = ::durable_" << type << "(registry, target);\n";
    out << "                if (value == nullptr) { return; }\n";
    for (const auto& field : resolved_trait->fields) {
        out << "                if (patch." << field.name << ") { value->" << field.name << " = *patch." << field.name
            << "; }\n";
    }
    out << "            });\n";
    out << "    }\n";
}

std::string emit_graph_external_handler_abi(const DecoratedProgram& program) {
    if (program.execution_graph.phases.empty()) {
        return {};
    }

    std::ostringstream out;
    out << "// ── Contract-Shaped External Handler ABI ────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";
    out << "struct EffectService {\n";
    out << "    std::string_view domain;\n";
    out << "};\n\n";
    for (const auto& node : program.execution_graph.handlers) {
        if (!is_user_external_handler(program, node)) {
            continue;
        }
        const auto capability_name = external_handler_capability_name(node.identity);
        out << "struct " << capability_name << " {\n";
        out << "    entt::registry& registry;\n";
        for (const auto& trait : sorted_symbols(node.contract.projects)) {
            const auto type            = EnttCodegenUtils::trait_cpp_name(trait);
            const auto* resolved_trait = EnttCodegenUtils::find_trait(program, make_canonical_id(trait));
            if (resolved_trait == nullptr) {
                throw std::runtime_error("cpp-entt cannot lower external project capability for missing trait '" +
                                         make_canonical_id(trait) + "'");
            }
            if (resolved_trait->fields.empty()) {
                out << "    [[nodiscard]] bool project_" << type << "(entt::entity target) const {\n";
                out << "        if (!registry.valid(target)) { return false; }\n";
                out << "        ::project_" << type << "(registry, target);\n";
                out << "        return true;\n";
                out << "    }\n";
            } else {
                out << "    [[nodiscard]] " << type << "* project_" << type << "(entt::entity target) const {\n";
                out << "        if (!registry.valid(target)) { return nullptr; }\n";
                out << "        return &::project_" << type << "(registry, target);\n";
                out << "    }\n";
            }
        }
        for (const auto& event : sorted_symbols(node.contract.emits)) {
            const auto event_name = canonical_to_cpp_name(event);
            const auto event_type = event_runtime_cpp_type(program, event);
            out << "    void emit_" << event_name << "(" << event_type << " occurrence) const {\n";
            out << "        generated_emit_event(std::move(occurrence));\n";
            out << "    }\n";
        }
        std::unordered_set<std::string> emitted_commands;
        for (const auto& command : node.contract.commands) {
            const auto method_name = external_handler_command_method_name(command);
            if (!emitted_commands.insert(method_name).second) {
                continue;
            }
            switch (command.kind) {
                case HandlerCommandKind::Spawn: {
                    if (!command.target.has_value()) {
                        throw std::runtime_error("cpp-entt received an external spawn capability without a target");
                    }
                    const auto* template_node = find_template(program, *command.target);
                    if (template_node == nullptr) {
                        throw std::runtime_error(
                            "cpp-entt cannot lower external spawn capability for missing template '" +
                            make_canonical_id(*command.target) + "'");
                    }
                    if (creation_takes_parameters(template_node->initializers)) {
                        throw std::runtime_error(
                            "cpp-entt cannot lower external spawn capability for parameterized template '" +
                            make_canonical_id(*command.target) +
                            "': an external handler has no place to supply template arguments");
                    }
                    const auto factory = archetype_create_at_function_name(program.module_name, template_node->name);
                    out << "    [[nodiscard]] entt::entity " << method_name << "() const {\n";
                    out << "        const auto entity = generated_reserve_entity(registry);\n";
                    out << "        generated_queue_structural_command(StructuralCommand::Kind::Spawn,\n";
                    out << "            [entity](entt::registry& registry) { (void)::" << factory
                        << "(registry, entity); });\n";
                    out << "        return entity;\n";
                    out << "    }\n";
                    break;
                }
                case HandlerCommandKind::Destroy: {
                    out << "    void " << method_name << "(entt::entity target) const {\n";
                    out << "        generated_queue_structural_command(StructuralCommand::Kind::Destroy,\n";
                    out << "            [target](entt::registry& registry) {\n";
                    out << "                if (!registry.valid(target)) { return; }\n";
                    if (EnttCodegenUtils::find_trait(program, "Parent") != nullptr) {
                        out << "                ::cactus_destroy_entity_recursive(registry, target);\n";
                    } else {
                        out << "                registry.destroy(target);\n";
                    }
                    out << "            });\n";
                    out << "    }\n";
                    break;
                }
                case HandlerCommandKind::Add: {
                    if (!command.target.has_value()) {
                        throw std::runtime_error("cpp-entt received an external add capability without a target");
                    }
                    const auto type = EnttCodegenUtils::trait_cpp_name(*command.target);
                    out << "    void " << method_name << "(entt::entity target, " << type << " value = {}) const {\n";
                    out << "        generated_queue_structural_command(StructuralCommand::Kind::Add,\n";
                    out << "            [target, value = std::move(value)](entt::registry& registry) mutable {\n";
                    out << "                if (!registry.valid(target)) { return; }\n";
                    out << "                ::cancel_projected_" << type << "(target);\n";
                    out << "                registry.emplace_or_replace<" << type << ">(target, std::move(value));\n";
                    if (EnttPersistenceEmitter::structural_site_tracks_construction(
                            program, command.target, command.target->local_name)) {
                        out << EnttPersistenceEmitter::emit_retain_construction(type, "target", "                ");
                    }
                    out << "            });\n";
                    out << "    }\n";
                    break;
                }
                case HandlerCommandKind::Remove: {
                    if (!command.target.has_value()) {
                        throw std::runtime_error("cpp-entt received an external remove capability without a target");
                    }
                    const auto type = EnttCodegenUtils::trait_cpp_name(*command.target);
                    out << "    void " << method_name << "(entt::entity target) const {\n";
                    out << "        generated_queue_structural_command(StructuralCommand::Kind::Remove,\n";
                    out << "            [target](entt::registry& registry) {\n";
                    out << "                if (!registry.valid(target)) { return; }\n";
                    out << "                ::cancel_projected_" << type << "(target);\n";
                    out << "                if (registry.all_of<" << type << ">(target)) { registry.remove<" << type
                        << ">(target); }\n";
                    if (EnttPersistenceEmitter::structural_site_tracks_construction(
                            program, command.target, command.target->local_name)) {
                        out << EnttPersistenceEmitter::emit_discard_construction(type, "target", "                ");
                    }
                    out << "            });\n";
                    out << "    }\n";
                    break;
                }
                case HandlerCommandKind::Set:
                    emit_external_set_capability(out, program, command, method_name);
                    break;
            }
        }
        for (const auto& effect : sorted_strings(node.contract.effects)) {
            out << "    [[nodiscard]] EffectService " << external_handler_effect_method_name(effect)
                << "() const noexcept {\n";
            out << "        return EffectService{.domain = \"" << effect << "\"};\n";
            out << "    }\n";
        }
        out << "};\n\n";
    }
    out << "}  // namespace cactus::runtime::entt_backend\n\n";

    for (const auto& node : program.execution_graph.handlers) {
        if (!is_user_external_handler(program, node)) {
            continue;
        }
        const auto trigger_type = handler_trigger_cpp_type(program, node.identity.trigger);
        out << "void " << external_handler_callback_name(node.identity) << "(const " << trigger_type << "& trigger";
        if (!node.contract.is_selectionless()) {
            out << ", entt::entity entity";
        }
        const auto reads  = sorted_symbols(node.contract.reads);
        const auto writes = sorted_symbols(node.contract.writes);
        for (const auto& trait : reads) {
            if (node.contract.writes.contains(trait)) {
                continue;
            }
            const auto type = EnttCodegenUtils::trait_cpp_name(trait);
            out << ", const " << type << "& read_" << type;
        }
        for (const auto& trait : writes) {
            const auto type = EnttCodegenUtils::trait_cpp_name(trait);
            out << ", " << type << "& write_" << type;
        }
        out << ", const cactus::runtime::entt_backend::" << external_handler_capability_name(node.identity)
            << "& capabilities);\n\n";
    }
    return out.str();
}

// target_expr, when non-empty, names a `std::optional<entt::entity>` C++
// expression in scope: a unary external handler then runs at most once, for
// that recipient, gated on it satisfying the handler's selection, instead of
// broadcasting across the full matching view (targeted-event-delivery). Empty
// means no recipient concept applies at this call site (phase dispatch).
void emit_external_handler_call(std::ostringstream& out,
                                const HandlerNode& node,
                                std::string_view trigger,
                                std::string_view indent,
                                std::string_view target_expr = {}) {
    const auto reads                = sorted_symbols(node.contract.reads);
    const auto writes               = sorted_symbols(node.contract.writes);
    std::vector<SymbolId> selection = reads;
    for (const auto& trait : writes) {
        if (std::ranges::find(selection, trait) == selection.end()) {
            selection.push_back(trait);
        }
    }
    std::ranges::sort(selection, [](const auto& left, const auto& right) {
        return make_canonical_id(left) < make_canonical_id(right);
    });

    const auto emit_callback = [&](std::string_view callback_indent) {
        out << callback_indent << "::" << external_handler_callback_name(node.identity) << "(" << trigger;
        if (!node.contract.is_selectionless()) {
            out << ", entity";
        }
        for (const auto& trait : reads) {
            if (node.contract.writes.contains(trait)) {
                continue;
            }
            out << ", registry.get<" << EnttCodegenUtils::trait_cpp_name(trait) << ">(entity)";
        }
        for (const auto& trait : writes) {
            out << ", registry.get<" << EnttCodegenUtils::trait_cpp_name(trait) << ">(entity)";
        }
        out << ", " << external_handler_capability_name(node.identity) << "{registry});\n";
    };

    if (node.contract.is_selectionless()) {
        emit_callback(indent);
        return;
    }

    const auto emit_view_loop = [&](std::string_view loop_indent) {
        out << loop_indent << "for (const auto entity : registry.view<";
        for (std::size_t index = 0; index < selection.size(); ++index) {
            out << (index == 0 ? "" : ", ") << EnttCodegenUtils::trait_cpp_name(selection[index]);
        }
        out << ">()) {\n";
        emit_callback(std::string(loop_indent) + "    ");
        out << loop_indent << "}\n";
    };

    if (target_expr.empty()) {
        emit_view_loop(indent);
        return;
    }
    out << indent << "if (" << target_expr << ".has_value()) {\n";
    out << indent << "    entt::entity entity = *" << target_expr << ";\n";
    out << indent << "    if (registry.all_of<";
    for (std::size_t index = 0; index < selection.size(); ++index) {
        out << (index == 0 ? "" : ", ") << EnttCodegenUtils::trait_cpp_name(selection[index]);
    }
    out << ">(entity)) {\n";
    emit_callback(std::string(indent) + "        ");
    out << indent << "    }\n";
    out << indent << "} else {\n";
    emit_view_loop(std::string(indent) + "    ");
    out << indent << "}\n";
}

std::string cpp_double_literal(double value) {
    std::ostringstream literal;
    literal << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    if (literal.str().find_first_of(".eE") == std::string::npos) {
        literal << ".0";
    }
    return literal.str();
}

std::vector<const PhasePlan*> phase_activation_order(const DecoratedProgram& program) {
    std::vector<const PhasePlan*> remaining;
    remaining.reserve(program.execution_graph.phases.size());
    for (const auto& phase : program.execution_graph.phases) {
        remaining.push_back(&phase);
    }
    std::ranges::sort(remaining, [](const auto* left, const auto* right) {
        if (left->declaration_order.declaration_index != right->declaration_order.declaration_index) {
            return left->declaration_order.declaration_index < right->declaration_order.declaration_index;
        }
        return make_canonical_id(left->phase) < make_canonical_id(right->phase);
    });

    std::vector<const PhasePlan*> ordered;
    std::unordered_set<SymbolId> completed;
    while (!remaining.empty()) {
        const auto ready = std::ranges::find_if(remaining, [&](const auto* phase) {
            const bool completion_ready = std::ranges::all_of(
                phase->completion_dependencies, [&](const auto& dependency) { return completed.contains(dependency); });
            const bool source_ready = std::ranges::all_of(phase->source_dependencies, [&](const auto& dependency) {
                return dependency.kind != HandlerTriggerKind::Phase || completed.contains(dependency.symbol);
            });
            return completion_ready && source_ready;
        });
        if (ready == remaining.end()) {
            throw std::runtime_error("cpp-entt codegen received a cyclic phase activation graph");
        }
        ordered.push_back(*ready);
        completed.insert((*ready)->phase);
        remaining.erase(ready);
    }
    return ordered;
}

std::string emit_graph_scheduler_state(const DecoratedProgram& program) {
    if (program.execution_graph.phases.empty()) {
        return {};
    }

    std::vector<const ResolvedEvent*> all_events;
    std::vector<const ResolvedEvent*> external_events;
    for (const auto& [_, event] : program.events) {
        if (event.symbol_id.has_value()) {
            all_events.push_back(&event);
        }
        if (event.is_external && event.symbol_id.has_value()) {
            external_events.push_back(&event);
        }
    }
    std::ranges::sort(all_events,
                      [](const auto* left, const auto* right) { return left->canonical_id < right->canonical_id; });
    std::ranges::sort(external_events,
                      [](const auto* left, const auto* right) { return left->canonical_id < right->canonical_id; });

    const auto* frame_event  = find_external_frame_event(program);
    const auto* render_phase = find_render_phase(program);

    // Programs with no lifecycle trigger get no tracking code at all.
    const auto lifecycle_triggers = lifecycle_occurrence_triggers(program);
    std::vector<std::string> occurrence_types;
    occurrence_types.reserve(all_events.size() + lifecycle_triggers.size());
    for (const auto* event : all_events) {
        occurrence_types.push_back(event_runtime_cpp_type(program, *event->symbol_id));
    }
    std::vector<std::string> lifecycle_types;
    lifecycle_types.reserve(lifecycle_triggers.size());
    for (const auto& trigger : lifecycle_triggers) {
        lifecycle_types.push_back(*EnttCodegenUtils::lifecycle_occurrence_cpp_type(trigger, program));
    }
    occurrence_types.insert(occurrence_types.end(), lifecycle_types.begin(), lifecycle_types.end());

    // Mirrors the same flag/name computation CppEnttCodegen::generate() uses
    // for the legacy per-viewport render loop and edit-mode HUD overlay (see
    // the early __translate_camera_2d/3d helper emission there) — these are
    // pure functions of `program`, so recomputing them here is safe and keeps
    // this function self-contained rather than threading extra parameters
    // through every call site.
    const bool viewport_uses_flat             = module_uses_camera_flat(program);
    const bool viewport_uses_volume           = module_uses_camera_volume(program);
    const bool render_uses_viewport           = module_uses_camera_viewport(program) && render_phase != nullptr;
    const WorldTransformUsage render_wt_usage = EnttCodegenUtils::world_transform_usage(program);
    const bool render_rig_is_2d               = render_wt_usage.flat || (viewport_uses_flat && !render_wt_usage.volume);
    const bool render_rig_is_3d               = render_wt_usage.volume;
    const std::string render_vp_cpp           = EnttCodegenUtils::trait_cpp_name("Viewport", program);
    const std::string render_cam2d_cpp        = EnttCodegenUtils::trait_cpp_name("std.camera.flat.Camera", program);
    const std::string render_cam3d_cpp        = EnttCodegenUtils::trait_cpp_name("std.camera.volume.Camera", program);
    const bool render_emit_2d_helper =
        render_uses_viewport && viewport_uses_flat && (render_rig_is_2d || !render_rig_is_3d);
    const bool render_emit_3d_helper = render_uses_viewport && viewport_uses_volume && render_rig_is_3d;

    std::ostringstream out;
    out << "// ── Graph Activation Runtime State ──────────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";
    // kMaxEventCascadeDepth, QueuedEvent<Occurrence>, and ActivationRuntime<Occurrence>
    // are runtime-hosted (backends/cpp-entt/runtime.hpp) — EventOccurrence
    // (the program's concrete event-type list) is the one genuinely
    // program-specific piece, so it stays generated.
    if (occurrence_types.empty()) {
        out << "using EventOccurrence = std::variant<std::monostate>;\n";
    } else {
        out << "using EventOccurrence = std::variant<";
        for (std::size_t index = 0; index < occurrence_types.size(); ++index) {
            out << (index == 0 ? "" : ", ") << occurrence_types[index];
        }
        out << ">;\n";
    }
    out << "\n";

    for (const auto& phase : program.execution_graph.phases) {
        const auto phase_name = canonical_to_cpp_name(phase.phase);
        out << "struct " << phase_name << "PhaseRuntimeState {\n";
        if (phase.every_seconds.has_value()) {
            out << "    double accumulator{};\n";
            out << "    double alpha{};\n";
        }
        out << "    std::uint64_t completed_batches{};\n";
        for (const auto& field : phase.fields) {
            if (field.is_completion_only) {
                continue;
            }
            out << "    " << EnttCodegenUtils::value_type_to_cpp(field.type) << " " << field.name << "{};\n";
        }
        out << "};\n\n";
    }

    out << "struct SchedulerState {\n";
    out << "    ActivationRuntime<EventOccurrence> activation;\n";
    for (const auto& phase : program.execution_graph.phases) {
        const auto phase_name = canonical_to_cpp_name(phase.phase);
        out << "    " << phase_name << "PhaseRuntimeState " << phase_name << ";\n";
    }
    out << "};\n\n";
    out << "SchedulerState& generated_scheduler_state() {\n";
    out << "    static SchedulerState state;\n";
    out << "    return state;\n";
    out << "}\n\n";
    // Wraps `generated_scheduler_state() = {};` behind a function so callers
    // emitted earlier in the file (world restore's publication step) can
    // forward-declare just the function, not the SchedulerState type itself.
    out << "void generated_reset_scheduler_state() {\n";
    out << "    generated_scheduler_state() = {};\n";
    out << "}\n\n";
    // Kept apart from SchedulerState: storage signals point at the trackers, so
    // resetting the scheduler state must not move or replace them.
    if (!lifecycle_triggers.empty()) {
        const auto trackers_type = lifecycle_trackers_cpp_type(program);
        out << trackers_type << "& generated_lifecycle_trackers() {\n";
        out << "    static " << trackers_type << " trackers;\n";
        out << "    return trackers;\n";
        out << "}\n\n";
        // Called on every registry the world lives in: at init, and again after
        // world restore publishes a freshly staged registry.
        out << "void generated_connect_lifecycle_trackers(entt::registry& registry) {\n";
        out << "    generated_lifecycle_trackers().connect(registry);\n";
        out << "}\n\n";
    }

    out << "entt::entity generated_reserve_entity(entt::registry& registry) {\n";
    out << "    return reserve_entity(registry, generated_scheduler_state().activation);\n";
    out << "}\n\n";

    out << "void generated_queue_structural_command(StructuralCommand::Kind kind,\n";
    out << "                                        std::function<void(entt::registry&)> apply) {\n";
    out << "    queue_structural_command(generated_scheduler_state().activation, kind, std::move(apply));\n";
    out << "}\n\n";

    // The generated_drain_event_cascade forward declaration precedes
    // generated_commit_activation, which passes it to the runtime commit.
    out << "template <typename Occurrence>\n";
    out << "void generated_emit_event(Occurrence occurrence) {\n";
    out << "    emit_event(generated_scheduler_state().activation, std::move(occurrence));\n";
    out << "}\n\n";

    // Targeted counterpart of generated_emit_event (targeted-event-delivery):
    // the recipient is evaluated once at the call site (by the caller, before
    // this is invoked) and carried in the queued envelope so it survives
    // cascade deferral unchanged. Delivery constrains consumer cardinality by
    // this recipient; it is never lowered to a validity guard around
    // broadcast dispatch.
    out << "template <typename Occurrence>\n";
    out << "void generated_emit_targeted_event(Occurrence occurrence, entt::entity target) {\n";
    out << "    emit_targeted_event(generated_scheduler_state().activation, std::move(occurrence), target);\n";
    out << "}\n\n";

    out << "void generated_drain_event_cascade(entt::registry& registry);\n\n";

    out << "void generated_commit_activation(entt::registry& registry) {\n";
    out << "    auto& activation = generated_scheduler_state().activation;\n";
    if (lifecycle_triggers.empty()) {
        out << "    commit_activation(activation, registry, &generated_drain_event_cascade);\n";
    } else {
        out << "    commit_activation(activation, registry, &generated_drain_event_cascade, "
               "generated_lifecycle_trackers());\n";
    }
    out << "}\n\n";

    for (const auto* event : external_events) {
        const auto type = event_runtime_cpp_type(program, *event->symbol_id);
        out << "void generated_inject_external_event(" << type << " occurrence) {\n";
        out << "    generated_scheduler_state().activation.root_event_queue.push_back(\n";
        out << "        QueuedEvent<EventOccurrence>{.occurrence = std::move(occurrence), .cascade_depth = 0});\n";
        out << "}\n\n";
    }

    out << "template <typename Occurrence>\n";
    out << "void generated_dispatch_event(entt::registry&, const Occurrence&) {}\n\n";

    out << "struct StableHandlerDispatch {\n";
    out << "    std::string_view canonical_id;\n";
    out << "    bool selectionless;\n";
    out << "};\n";
    out << "inline constexpr std::array<StableHandlerDispatch, "
        << program.execution_graph.stable_topological_order.size() << "> kStableHandlerDispatch = {{\n";
    for (const auto& identity : program.execution_graph.stable_topological_order) {
        const auto found = std::ranges::find_if(program.execution_graph.handlers,
                                                [&](const auto& handler) { return handler.identity == identity; });
        out << "    {\"" << identity.canonical_id() << "\", "
            << (found != program.execution_graph.handlers.end() && found->contract.is_selectionless() ? "true"
                                                                                                      : "false")
            << "},\n";
    }
    out << "}};\n\n";

    // Emits the render phase's dispatch call. When the program links
    // std.camera.viewport, the dispatch runs once per active viewport —
    // bracketed by that viewport's scissor region and camera — so queued
    // mesh/sprite submissions and immediate-mode draws (e.g. 2D shapes) both
    // land in the right screen region using the right camera, mirroring the
    // legacy generated_render_project's per-viewport loop (see
    // CppEnttCodegen::generate()). Without std.camera.viewport, it's a plain
    // single call, relying on the runtime's static default camera.
    const auto emit_render_phase_dispatch = [&](const std::string& indent, const std::string& phase_name) {
        if (!render_uses_viewport || (!render_emit_2d_helper && !render_emit_3d_helper)) {
            out << indent << "generated_dispatch_phase_" << phase_name << "(registry, phase);\n";
            return;
        }
        out << indent << "{\n";
        out << indent << "    const int __sw = cactus::runtime::raylib::GetScreenWidth();\n";
        out << indent << "    const int __sh = cactus::runtime::raylib::GetScreenHeight();\n";
        out << indent << "    static std::vector<std::pair<int,entt::entity>> __vps;\n";
        out << indent << "    __vps.clear();\n";
        out << indent << "    for (const auto& [__vp_e, __vp] : registry.view<" << render_vp_cpp << ">().each()) {\n";
        out << indent << "        if (__vp.active) { __vps.emplace_back(__vp.depth, __vp_e); }\n";
        out << indent << "    }\n";
        out << indent << "    std::ranges::sort(__vps);\n";
        out << indent << "    for (auto& [__depth, __vp_ent] : __vps) {\n";
        out << indent << "        (void)__depth;\n";
        out << indent << "        const auto& __vp = registry.get<" << render_vp_cpp << ">(__vp_ent);\n";
        out << indent << "        cactus::runtime::raylib::BeginScissorMode(\n";
        out << indent << "            static_cast<int>(__vp.x * static_cast<float>(__sw)),\n";
        out << indent << "            static_cast<int>(__vp.y * static_cast<float>(__sh)),\n";
        out << indent << "            static_cast<int>(__vp.width * static_cast<float>(__sw)),\n";
        out << indent << "            static_cast<int>(__vp.height * static_cast<float>(__sh)));\n";
        out << indent << "        if (__vp.clear) { cactus::runtime::raylib::ClearBackground(__vp.clear_color); }\n";
        if (render_emit_2d_helper) {
            out << indent << "        if (registry.all_of<" << render_cam2d_cpp << ">(__vp_ent)) {\n";
            out << indent << "            const auto& __cam = registry.get<" << render_cam2d_cpp << ">(__vp_ent);\n";
            out << indent << "            set_active_camera_2d(__translate_camera_2d(__cam, __sw, __sh));\n";
            out << indent << "        }\n";
        }
        if (render_emit_3d_helper) {
            out << indent
                << (render_emit_2d_helper ? "        else if (registry.all_of<" : "        if (registry.all_of<")
                << render_cam3d_cpp << ">(__vp_ent)) {\n";
            out << indent << "            const auto& __cam = registry.get<" << render_cam3d_cpp << ">(__vp_ent);\n";
            out << indent << "            set_active_camera_3d(__translate_camera_3d(__vp_ent, __cam, registry));\n";
            out << indent << "        }\n";
        }
        out << indent << "        generated_dispatch_phase_" << phase_name << "(registry, phase);\n";
        out << indent << "        flush_viewport_3d();\n";
        out << indent << "        cactus::runtime::raylib::EndScissorMode();\n";
        out << indent << "    }\n";
        out << indent << "}\n";
    };

    const auto phase_order = phase_activation_order(program);
    for (const auto* phase : phase_order) {
        if (!phase->runtime_root.has_value()) {
            continue;
        }
        const auto phase_name            = canonical_to_cpp_name(phase->phase);
        const auto root_type             = event_runtime_cpp_type(program, *phase->runtime_root);
        const bool is_render_phase_batch = phase == render_phase;
        out << "void generated_dispatch_phase_" << phase_name << "(entt::registry&, const " << phase_name
            << "PhaseRuntimeState&);\n\n";
        out << "void generated_run_phase_batch_" << phase_name << "(entt::registry& registry, const " << root_type
            << "& root_event) {\n";
        out << "    auto& scheduler = generated_scheduler_state();\n";
        out << "    auto& phase = scheduler." << phase_name << ";\n";
        if (phase->every_seconds.has_value()) {
            const auto interval = *phase->every_seconds;
            out << "    constexpr double interval = " << cpp_double_literal(interval) << ";\n";
            out << "    phase.accumulator += static_cast<double>(root_event.dt);\n";
            out << "    const auto due = static_cast<std::uint64_t>(std::floor(phase.accumulator / interval));\n";
            if (phase->max_repetitions.has_value()) {
                out << "    constexpr std::uint64_t max_repetitions = " << *phase->max_repetitions << ";\n";
                out << "    const auto repetitions = std::min(due, max_repetitions);\n";
            } else {
                out << "    const auto repetitions = due;\n";
            }
            out << "    phase.dt = interval;\n";
            out << "    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {\n";
            out << "        scheduler.activation.active = true;\n";
            if (is_render_phase_batch) {
                out << "        begin_render_frame();\n";
                emit_render_phase_dispatch("        ", phase_name);
                out << "        end_render_frame();\n";
            } else {
                out << "        generated_dispatch_phase_" << phase_name << "(registry, phase);\n";
            }
            out << "        generated_drain_event_cascade(registry);\n";
            out << "        generated_commit_activation(registry);\n";
            out << "        scheduler.activation.active = false;\n";
            out << "    }\n";
            out << "    phase.accumulator -= static_cast<double>(due) * interval;\n";
            out << "    if (phase.accumulator < 0.0) { phase.accumulator = 0.0; }\n";
            out << "    if (phase.accumulator >= interval) { phase.accumulator = std::fmod(phase.accumulator, "
                   "interval); }\n";
            out << "    phase.alpha = phase.accumulator / interval;\n";
        } else {
            bool used_root_event = false;
            for (const auto& field : phase->fields) {
                if (!field.source_binding.has_value()) {
                    continue;
                }
                const auto& binding   = *field.source_binding;
                const auto field_type = EnttCodegenUtils::value_type_to_cpp(field.type);
                if (binding.kind == PhaseFieldSource::Kind::RootEvent) {
                    used_root_event = true;
                    out << "    phase." << field.name << " = static_cast<" << field_type << ">(root_event."
                        << binding.member << ");\n";
                } else {
                    out << "    phase." << field.name << " = static_cast<" << field_type << ">(scheduler."
                        << canonical_to_cpp_name(binding.source) << "." << binding.member << ");\n";
                }
            }
            if (!used_root_event) {
                out << "    (void)root_event;\n";
            }
            out << "    scheduler.activation.active = true;\n";
            if (is_render_phase_batch) {
                out << "    begin_render_frame();\n";
                emit_render_phase_dispatch("    ", phase_name);
                out << "    end_render_frame();\n";
            } else {
                out << "    generated_dispatch_phase_" << phase_name << "(registry, phase);\n";
            }
            out << "    generated_drain_event_cascade(registry);\n";
            out << "    generated_commit_activation(registry);\n";
            out << "    scheduler.activation.active = false;\n";
        }
        out << "    ++phase.completed_batches;\n";
        out << "}\n\n";
    }

    // The per-event generated_dispatch_event overloads are defined later in
    // the file but called by the root-event processors below.
    for (const auto& type : occurrence_types) {
        out << "void generated_dispatch_event(entt::registry&, const " << type
            << "&, std::optional<entt::entity> = std::nullopt);\n";
    }
    out << "\n";

    // An occurrence no phase claims as its root (e.g. std.persistence's save
    // outcomes, a deferred internal event, or a deferred lifecycle delivery)
    // is still its own activation: dispatch its handlers, drain whatever
    // cascade they queue, then commit, exactly like a phase batch does.
    const auto emit_own_activation = [&out] {
        out << "    if (target.has_value() && !registry.valid(*target)) {\n";
        out << "        return;\n";
        out << "    }\n";
        out << "    generated_scheduler_state().activation.active = true;\n";
        out << "    generated_dispatch_event(registry, root_event, target);\n";
        out << "    generated_drain_event_cascade(registry);\n";
        out << "    generated_commit_activation(registry);\n";
        out << "    generated_scheduler_state().activation.active = false;\n";
    };

    // Every event gets a processor, not only external ones: a cascade-deferred
    // occurrence of any event is re-queued as a root event.
    for (const auto* event : all_events) {
        const auto root_type     = event_runtime_cpp_type(program, *event->symbol_id);
        const bool is_frame_root = event == frame_event;
        out << "void generated_process_root_event(entt::registry& registry, const " << root_type
            << "& root_event, std::optional<entt::entity> target = std::nullopt) {\n";
        // Input-consumption reset fires once per real (display) frame, before
        // any phase batch observes input — mirrors the legacy
        // generated_update_project ordering.
        if (is_frame_root) {
            out << "    reset_consumed_input();\n";
        }
        bool routed_to_phase = false;
        for (const auto* phase : phase_order) {
            if (phase->runtime_root.has_value() && *phase->runtime_root == *event->symbol_id) {
                routed_to_phase = true;
                out << "    generated_run_phase_batch_" << canonical_to_cpp_name(phase->phase)
                    << "(registry, root_event);\n";
                // Projected-trait cleanup fires once per frame, immediately
                // after the render phase batch completes.
                if (is_frame_root && phase == render_phase) {
                    out << "    clear_projected_traits(registry);\n";
                }
            }
        }
        if (routed_to_phase) {
            out << "    (void)target;\n";
        } else {
            emit_own_activation();
        }
        out << "}\n\n";
    }
    for (const auto& type : lifecycle_types) {
        out << "void generated_process_root_event(entt::registry& registry, const " << type
            << "& root_event, std::optional<entt::entity> target = std::nullopt) {\n";
        emit_own_activation();
        out << "}\n\n";
    }

    // Defined after this function (its body needs generated_capture_world_snapshot
    // and the outcome event types, emitted later): declared here so the call
    // below resolves regardless of definition order in the file.
    out << "void generated_process_persistence_boundary(entt::registry& registry);\n\n";

    out << "void generated_drain_external_events(entt::registry& registry) {\n";
    out << "    auto& activation = generated_scheduler_state().activation;\n";
    out << "    auto& queue = activation.root_event_queue;\n";
    out << "    while (!activation.deferred_events.empty()) {\n";
    out << "        queue.push_back(std::move(activation.deferred_events.front()));\n";
    out << "        activation.deferred_events.pop_front();\n";
    out << "    }\n";
    out << "    while (!queue.empty()) {\n";
    out << "        auto queued = std::move(queue.front());\n";
    out << "        queue.pop_front();\n";
    out << "        std::visit([&](const auto& occurrence) { generated_process_root_event(registry, occurrence, queued.target); },\n";
    out << "                   queued.occurrence);\n";
    out << "    }\n";
    // Persistence requests are processed only once this drain's own root
    // events (and everything their handlers cascaded/committed) are fully
    // settled — the boundary decision-6 requires. generated_process_persistence_boundary
    // itself guards against the reentrant nested call this makes when it
    // delivers each outcome as its own activation.
    out << "    generated_process_persistence_boundary(registry);\n";
    out << "}\n\n";
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

namespace {

// program.phases is simple-name-keyed for a single-module compile but
// re-keyed by canonical_id once ProgramLinker::merge_into runs (its
// insert_key prefers canonical_id over the source map's simple-name key, the
// same duality EnttCodegenUtils::find_trait already handles for traits) — so
// a render-pass phase's ResolvedPhase entry must be found by resolved symbol
// identity, not assumed to sit at the bare local-name key.
const ResolvedPhase* find_resolved_phase(const DecoratedProgram& program, const SymbolId& phase_symbol) {
    if (const auto it = program.phases.find(phase_symbol.local_name);
        it != program.phases.end() && (!it->second.symbol_id.has_value() || *it->second.symbol_id == phase_symbol)) {
        return &it->second;
    }
    for (const auto& [_, candidate] : program.phases) {
        if (candidate.symbol_id.has_value() && *candidate.symbol_id == phase_symbol) {
            return &candidate;
        }
    }
    return nullptr;
}

}  // namespace

std::string emit_graph_handler_dispatch(const DecoratedProgram& program) {
    if (program.execution_graph.phases.empty() || program.ast == nullptr) {
        return {};
    }

    std::ostringstream out;
    out << "namespace cactus::runtime::entt_backend {\n\n";
    for (const auto& phase : program.execution_graph.phases) {
        const auto phase_name = canonical_to_cpp_name(phase.phase);
        out << "void generated_dispatch_phase_" << phase_name << "(entt::registry& registry, const " << phase_name
            << "PhaseRuntimeState& phase) {\n";
        out << "    (void)phase;\n";

        // dsl-render-passes: a render-pass phase's derived vertex/fragment
        // stage handlers use HandlerTriggerKind::RenderStage, not Phase, so
        // the generic per-handler loop below finds nothing for them by
        // design (their bodies are translated to GLSL, not C++) — substitute
        // the render-pass draw step wholesale instead.
        if (const auto* resolved_phase = find_resolved_phase(program, phase.phase);
            resolved_phase != nullptr && resolved_phase->render_pass.has_value()) {
            const auto render_pass_body = emit_render_pass_dispatch_body(program, *resolved_phase);
            out << (render_pass_body.has_value() ? *render_pass_body : "    (void)registry;\n");
            out << "}\n\n";
            continue;
        }

        bool emitted = false;
        // Two distinct execution-graph nodes in this phase batch can resolve
        // to the same underlying generated implementation (e.g.
        // std.transform.flat.TransformPropagation and
        // std.transform.volume.TransformPropagation both compiling to the
        // same system_function_name when only one WorldTransform dimension
        // is active). Track callee names already emitted for this phase
        // batch and skip re-emitting the call for a repeat, while still
        // treating the node as handled for the existing fallback bookkeeping
        // below. Scoped per-phase (declared inside this loop) so a
        // legitimate call to the same function in a different phase is
        // unaffected.
        std::unordered_set<std::string> emitted_callees;
        for (const auto& identity : program.execution_graph.stable_topological_order) {
            if (identity.trigger.kind != HandlerTriggerKind::Phase || identity.trigger.symbol != phase.phase) {
                continue;
            }
            const auto graph_node = std::ranges::find_if(program.execution_graph.handlers,
                                                         [&](const auto& node) { return node.identity == identity; });
            if (graph_node == program.execution_graph.handlers.end()) {
                continue;
            }
            if (is_user_external_handler(program, *graph_node)) {
                emitted = true;
                if (emitted_callees.insert(external_handler_callback_name(graph_node->identity)).second) {
                    emit_external_handler_call(out, *graph_node, "phase", "    ");
                }
                continue;
            }
            if (graph_node->implementation == HandlerImplementationKind::External) {
                const auto* rule = find_external_rule(program, graph_node->identity.rule);
                if (rule != nullptr && rule->is_stdlib) {
                    const auto callee = system_function_name(program.module_name, rule->name, "tick");
                    emitted           = true;
                    if (emitted_callees.insert(callee).second) {
                        out << "    ::" << callee << "(registry);\n";
                    }
                }
                continue;
            }
            if (graph_node->implementation != HandlerImplementationKind::Cactus) {
                continue;
            }
            if (const auto callee = cactus_handler_callee(program, identity)) {
                emitted = true;
                if (emitted_callees.insert(*callee).second) {
                    out << "    ::" << *callee << "(registry, phase);\n";
                }
            }
        }
        if (!emitted) {
            out << "    (void)registry;\n";
            out << "    (void)phase;\n";
        }
        out << "}\n\n";
    }

    std::vector<const ResolvedEvent*> events;
    for (const auto& [_, event] : program.events) {
        if (event.symbol_id.has_value()) {
            events.push_back(&event);
        }
    }
    std::ranges::sort(events,
                      [](const auto* left, const auto* right) { return left->canonical_id < right->canonical_id; });
    for (const auto* event : events) {
        const auto event_type = event_runtime_cpp_type(program, *event->symbol_id);
        // The default argument lives on the forward declaration emitted by
        // emit_graph_scheduler_state; repeating it here would be a redefinition.
        out << "void generated_dispatch_event(entt::registry& registry, const " << event_type << "& occurrence, "
            << "std::optional<entt::entity> target) {\n";
        out << "    (void)occurrence;\n";
        out << "    (void)target;\n";
        // std.persistence.SaveRequested is a runtime-owned effect domain
        // (dsl-stdlib-persistence decision 6), not a gameplay handler target:
        // every occurrence joins the request queue regardless of whether any
        // rule also declares `on SaveRequested:`. This is a direct special
        // case rather than an `extern rule ...: effects: persistence` in the
        // style of std.debug's renderers, because that style is not actually
        // a generic effects-keyed dispatch: `system_emitter.cpp`'s renderer
        // table is a hardcoded (module, rule_name) lookup that still needs a
        // native C++ handler registered by name for each entry. Routing
        // through it here would trade this one dispatch-site branch for a
        // second hardcoded table entry plus a native function — more
        // machinery for the same outcome, not less.
        if (event->symbol_id.has_value() && event->name == "SaveRequested" &&
            event->symbol_id->module.name == "std.persistence") {
            out << "    generated_queue_save_request(occurrence.slot, occurrence.request_id);\n";
        }
        // RestoreRequested is the same runtime-owned effect domain as
        // SaveRequested above, sharing its request queue and boundary.
        if (event->symbol_id.has_value() && event->name == "RestoreRequested" &&
            event->symbol_id->module.name == "std.persistence") {
            out << "    generated_queue_restore_request(occurrence.slot, occurrence.request_id);\n";
        }
        bool emitted = false;
        for (const auto& identity : program.execution_graph.stable_topological_order) {
            if (identity.trigger.kind != HandlerTriggerKind::Event || identity.trigger.symbol != *event->symbol_id) {
                continue;
            }
            const auto graph_node = std::ranges::find_if(program.execution_graph.handlers,
                                                         [&](const auto& node) { return node.identity == identity; });
            if (graph_node == program.execution_graph.handlers.end()) {
                continue;
            }
            if (is_user_external_handler(program, *graph_node)) {
                emit_external_handler_call(out, *graph_node, "occurrence", "    ", "target");
                emitted = true;
                continue;
            }
            if (graph_node->implementation == HandlerImplementationKind::External) {
                const auto* rule = find_external_rule(program, graph_node->identity.rule);
                if (rule != nullptr && rule->is_stdlib) {
                    // Event-triggered extern rules (editor-debug-draw / editor-screen-ui generic
                    // renderers) have no filter view to iterate, so their generated body takes the
                    // occurrence directly instead of the (registry)-only shape phase-triggered
                    // filter/view extern rules use — see emit_event_renderer_body in system_emitter.cpp.
                    out << "    ::" << system_function_name(program.module_name, rule->name, "tick")
                        << "(registry, occurrence);\n";
                    emitted = true;
                }
                continue;
            }
            if (graph_node->implementation != HandlerImplementationKind::Cactus) {
                continue;
            }
            if (const auto callee = cactus_handler_callee(program, identity)) {
                out << "    ::" << *callee << "(registry, occurrence, target);\n";
                emitted = true;
            }
        }
        if (!emitted) {
            out << "    (void)registry;\n";
            out << "    (void)occurrence;\n";
        }
        out << "}\n\n";
    }

    // Lifecycle triggers are only ever declared on Cactus rules.
    for (const auto& trigger : lifecycle_occurrence_triggers(program)) {
        out << "void generated_dispatch_event(entt::registry& registry, const "
            << *EnttCodegenUtils::lifecycle_occurrence_cpp_type(trigger, program)
            << "& occurrence, std::optional<entt::entity> target) {\n";
        bool emitted = false;
        for (const auto& identity : program.execution_graph.stable_topological_order) {
            if (identity.trigger != trigger) {
                continue;
            }
            if (const auto callee = cactus_handler_callee(program, identity)) {
                out << "    ::" << *callee << "(registry, occurrence, target);\n";
                emitted = true;
            }
        }
        if (!emitted) {
            out << "    (void)registry;\n";
            out << "    (void)occurrence;\n";
            out << "    (void)target;\n";
        }
        out << "}\n\n";
    }

    // Stale-recipient dropping (targeted-event-delivery): a targeted
    // occurrence whose recipient is no longer valid at delivery time is
    // dropped before any consumer executes — checked once per occurrence
    // here, not re-checked per consumer, so no handler/command/effect runs
    // for it.
    out << "void generated_drain_event_cascade(entt::registry& registry) {\n";
    out << "    drain_event_cascade(\n";
    out << "        generated_scheduler_state().activation, registry,\n";
    out << "        [](entt::registry& reg, const auto& occurrence, std::optional<entt::entity> target) {\n";
    out << "            generated_dispatch_event(reg, occurrence, target);\n";
    out << "        });\n";
    out << "}\n\n";
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

std::string emit_projected_trait_registry_helpers(const DecoratedProgram& program) {
    std::ostringstream out;
    out << "// ── Projected Trait Registry Tracking ────────────────────────────────\n\n";
    out << "namespace {\n\n";
    for (const auto& [name, trait] : program.traits) {
        const std::string cpp_name = canonical_to_cpp_name(trait.module_name, trait.name);
        // Tracking storage/logic is runtime-hosted (ProjectedTraitTracker,
        // backends/cpp-entt/runtime.hpp) — one instantiation per projected
        // trait type, replacing the former per-type remember/project/cancel/
        // clear quartet emitted as inline text. The instantiated variable
        // keeps the pre-existing `projected_<cpp_name>` name and these thin
        // wrappers keep the pre-existing `project_<cpp_name>`/
        // `cancel_projected_<cpp_name>` call-site names so system_emitter.cpp
        // needs no changes.
        out << "cactus::runtime::entt_backend::ProjectedTraitTracker<" << cpp_name << "> projected_" << cpp_name
            << ";  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)\n\n";
        if (trait.fields.empty()) {
            out << "[[maybe_unused]] void project_" << cpp_name
                << "(entt::registry& registry, entt::entity entity) {\n";
            out << "    projected_" << cpp_name << ".project(registry, entity);\n";
            out << "}\n\n";
        } else {
            out << "[[maybe_unused]] " << cpp_name << "& project_" << cpp_name
                << "(entt::registry& registry, entt::entity entity) {\n";
            out << "    return projected_" << cpp_name << ".project(registry, entity);\n";
            out << "}\n\n";
            out << "[[maybe_unused]] " << cpp_name << "* durable_" << cpp_name
                << "(entt::registry& registry, entt::entity entity) {\n";
            out << "    return projected_" << cpp_name << ".durable_value(registry, entity);\n";
            out << "}\n\n";
        }
        out << "[[maybe_unused]] void cancel_projected_" << cpp_name << "(entt::entity entity) {\n";
        out << "    projected_" << cpp_name << ".cancel(entity);\n";
        out << "}\n\n";
    }
    out << "void clear_projected_traits(entt::registry& registry) {\n";
    for (const auto& [name, trait] : program.traits) {
        const std::string cpp_name = canonical_to_cpp_name(trait.module_name, trait.name);
        out << "    projected_" << cpp_name << ".clear(registry);\n";
    }
    out << "}\n\n";
    out << "}  // namespace\n\n";
    return out.str();
}

// Authored funcs are declared first so a constant may call one; constants then
// follow in dependency order, so each initializer reads only constants above it.
std::string emit_module_constants(const DecoratedProgram& program) {
    if (program.ast == nullptr) {
        return {};
    }
    std::ostringstream out;
    for (const auto& phase : program.execution_graph.phases) {
        if (phase.every_seconds.has_value()) {
            out << "inline constexpr float " << EnttCodegenUtils::phase_dt_cpp_name(phase.phase)
                << " = static_cast<float>(" << cpp_double_literal(*phase.every_seconds) << ");\n";
        }
    }
    std::unordered_map<SymbolId, const ExprNode*> values;
    for (const auto& decl : program.ast->declarations) {
        if (const auto* func = std::get_if<FuncNode>(&decl)) {
            out << EnttSystemEmitter::emit_func_declaration(*func, program);
        } else if (const auto* block = std::get_if<ConstBlockNode>(&decl)) {
            for (const auto& assignment : block->assignments) {
                if (assignment.resolved_const_id.has_value()) {
                    values.emplace(*assignment.resolved_const_id, assignment.value.get());
                }
            }
        }
    }
    for (const auto& symbol : program.const_order) {
        const auto value     = values.find(symbol);
        const auto* constant = EnttCodegenUtils::find_const(program, symbol);
        if (value == values.end() || constant == nullptr) {
            continue;
        }
        out << "inline const " << EnttCodegenUtils::type_to_cpp(constant->type) << " "
            << EnttCodegenUtils::symbol_cpp_name(symbol) << " = " << EnttCodegenUtils::emit_expr(*value->second, program)
            << ";\n";
    }
    out << "\n";
    return out.str();
}

// std.physics.volume collider queries: the only generated code that names the
// program's component types. Runtime kernels do the geometry.
std::string emit_collider_query_helpers(const DecoratedProgram& program) {
    const auto physics_trait = [](std::string_view name) {
        return make_symbol_id(SymbolKind::Trait, std::string(kPhysicsVolumeModule), std::string(name));
    };
    const auto sweep_hit_id = make_symbol_id(SymbolKind::Struct, std::string(kPhysicsVolumeModule), "SweepHit");
    if (EnttCodegenUtils::find_struct(program, sweep_hit_id) == nullptr) {
        return {};
    }
    const std::string ns     = "cactus::runtime::physics::";
    const std::string shape  = "std::optional<" + ns + "ColliderShape>";
    const auto hit_cpp       = EnttCodegenUtils::struct_cpp_name(sweep_hit_id);
    const auto transform_id  = make_symbol_id(SymbolKind::Trait, std::string(kTransformVolumeModule), "WorldTransform");
    const bool has_transform = EnttCodegenUtils::find_trait(program, make_canonical_id(transform_id)) != nullptr;
    const auto transform_cpp = EnttCodegenUtils::trait_cpp_name(transform_id);
    const auto collider_cpp  = EnttCodegenUtils::trait_cpp_name(physics_trait("Collider"));
    struct ShapeBuilder {
        std::string_view trait;
        std::string_view call;
    };
    // Dispatch order when a runtime add leaves an entity with two shapes.
    static constexpr std::array<ShapeBuilder, 3> kBuilders{{
        {.trait = "BoxCollider", .call = "box_shape(transform->position, transform->rotation, shape->size"},
        {.trait = "CapsuleCollider", .call = "capsule_shape(transform->position, shape->radius, shape->height"},
        {.trait = "SphereCollider", .call = "sphere_shape(transform->position, shape->radius"},
    }};
    std::ostringstream out;
    out << "// ── Collider queries ────────────────────────────────────────────────\n\n";
    for (const auto& builder : kBuilders) {
        out << "[[maybe_unused]] inline " << shape << " " << EnttCodegenUtils::collider_shape_builder(builder.trait)
            << "([[maybe_unused]] entt::registry& registry, [[maybe_unused]] entt::entity entity) {\n";
        if (!has_transform) {
            out << "    return std::nullopt;\n}\n\n";
            continue;
        }
        out << "    const auto* transform = registry.try_get<" << transform_cpp << ">(entity);\n";
        out << "    const auto* collider  = registry.try_get<" << collider_cpp << ">(entity);\n";
        out << "    const auto* shape     = registry.try_get<"
            << EnttCodegenUtils::trait_cpp_name(physics_trait(builder.trait)) << ">(entity);\n";
        out << "    if (transform == nullptr || collider == nullptr || shape == nullptr) {\n";
        out << "        return std::nullopt;\n";
        out << "    }\n";
        out << "    return " << ns << builder.call << ", collider->layer, collider->mask);\n";
        out << "}\n\n";
    }
    out << "[[maybe_unused]] inline " << shape << " " << EnttCodegenUtils::collider_shape_builder()
        << "(entt::registry& registry, entt::entity entity) {\n";
    for (const auto& builder : kBuilders) {
        out << "    if (registry.all_of<" << EnttCodegenUtils::trait_cpp_name(physics_trait(builder.trait))
            << ">(entity)) {\n";
        out << "        return " << EnttCodegenUtils::collider_shape_builder(builder.trait) << "(registry, entity);\n";
        out << "    }\n";
    }
    out << "    return std::nullopt;\n";
    out << "}\n\n";
    out << "[[maybe_unused]] inline " << hit_cpp << " cactus_collider_sweep_hit(const " << ns
        << "SweepResult& result, entt::entity other) {\n";
    out << "    return " << hit_cpp
        << "{.hit = result.hit, .other = result.hit ? other : entt::entity{entt::null}, .t = result.t, "
           ".point = result.point, .normal = result.normal, .surface = result.surface};\n";
    out << "}\n\n";
    out << "[[maybe_unused]] inline " << hit_cpp << " cactus_collider_sweep_miss() {\n";
    out << "    return cactus_collider_sweep_hit(" << ns << "sweep_miss(), entt::null);\n";
    out << "}\n\n";
    out << "[[maybe_unused]] inline " << hit_cpp << " cactus_collider_sweep(const " << shape
        << "& subject_shape, Vector3 delta, const " << shape
        << "& target_shape, entt::entity subject, entt::entity target) {\n";
    out << "    return cactus_collider_sweep_hit(" << ns
        << "sweep(subject_shape, delta, target_shape, subject == target), target);\n";
    out << "}\n\n";
    out << "[[maybe_unused]] inline bool cactus_collider_touching(const " << shape << "& a_shape, const " << shape
        << "& b_shape, entt::entity a, entt::entity b) {\n";
    out << "    return " << ns << "touching(a_shape, b_shape, a == b);\n";
    out << "}\n\n";
    const auto body_cpp = EnttCodegenUtils::trait_cpp_name(physics_trait("CharacterBody"));
    out << "[[maybe_unused]] inline Vector3 cactus_collider_push_out(entt::registry& registry, const " << shape
        << "& a_shape, const " << shape << "& b_shape, entt::entity a, entt::entity b) {\n";
    out << "    const bool both_bodies = registry.all_of<" << body_cpp << ">(a) && registry.all_of<" << body_cpp
        << ">(b);\n";
    out << "    return " << ns << "push_out(a_shape, b_shape, a == b, both_bodies);\n";
    out << "}\n\n";
    return out.str();
}

// The root module is linked last, so its window constant wins over an imported one.
std::optional<std::string> window_constant(const DecoratedProgram& program,
                                           const std::string& name,
                                           const std::string& suffix = {}) {
    const auto found = std::ranges::find_if(program.const_order | std::views::reverse,
                                            [&](const SymbolId& symbol) { return symbol.local_name == name; });
    if (found == (program.const_order | std::views::reverse).end()) {
        return std::nullopt;
    }
    return EnttCodegenUtils::symbol_cpp_name(*found) + suffix;
}

std::string upper_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string snake_case(const std::string& value) {
    std::string result;
    for (char ch : value) {
        if (std::isupper(static_cast<unsigned char>(ch)) != 0) {
            if (!result.empty() && result.back() != '_') {
                result += '_';
            }
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else {
            result += ch;
        }
    }
    return result;
}

std::string input_action_constant_name(const std::string& input_name) {
    return "K_" + upper_copy(snake_case(input_name));
}

std::string cpp_string_literal(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

// Asset paths are module-relative (dsl-model-assets D5): join the declaring
// module's directory with the declared path and normalize. Absolute module
// paths (e.g. from build tooling) are relativized against the compiler's
// working directory — the project root — so generated registration code stays
// portable and the runtime resolves against the process working directory.
std::string normalized_asset_path(const AssetDeclNode& asset) {
    namespace fs = std::filesystem;
    const fs::path declared{asset.path};
    const auto module_dir = fs::path{asset.location.filename}.parent_path();
    auto joined           = (module_dir / declared).lexically_normal();
    if (joined.is_absolute()) {
        std::error_code ec;
        const auto cwd = fs::current_path(ec);
        if (!ec) {
            auto relative = joined.lexically_proximate(cwd);
            if (!relative.empty() && relative.begin()->string() != "..") {
                joined = std::move(relative);
            }
        }
    }
    return joined.generic_string();
}

std::string asset_register_call(const AssetDeclNode& asset) {
    const auto path_literal = cpp_string_literal(normalized_asset_path(asset));
    switch (asset.asset_kind) {
        case AssetKind::Mesh:
            return "shared_asset_registry().register_mesh(" + asset.name + ", " + path_literal + ", static_cast<int>(" +
                   asset.name + "));";
        case AssetKind::Model:
            return "shared_asset_registry().register_model(" + asset.name + ", " + path_literal +
                   ", static_cast<int>(" + asset.name + "));";
        case AssetKind::Texture:
            return "shared_asset_registry().register_texture(" + asset.name + ", " + path_literal +
                   ", static_cast<int>(" + asset.name + "));";
        case AssetKind::Material:
            return "shared_asset_registry().register_material(" + asset.name + ", " + path_literal +
                   ", static_cast<int>(" + asset.name + "));";
        case AssetKind::Sound:
        case AssetKind::Music:
        case AssetKind::Font:
            return {};
    }
    return {};
}

// Input bindings are emitted from the resolved enum member identity attached
// by semantic analysis (unified-name-resolution change) — never from source
// spelling or AST shape, which broke when examples moved to alias-qualified
// constants (`inp.Key.A`).
const ResolvedEnumMember* resolved_input_member(const ExprNode& expr) {
    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        if (member->resolved_enum_member.has_value()) {
            return &*member->resolved_enum_member;
        }
    }
    return nullptr;
}

bool is_std_input_enum(const ResolvedEnumMember& member, std::string_view enum_name) {
    return member.enum_id.module.name == "std.input" && member.enum_id.local_name == enum_name;
}

std::optional<std::string> raylib_key_constant(const ExprNode& expr) {
    const auto* member = resolved_input_member(expr);
    if (member == nullptr || !is_std_input_enum(*member, "Key")) {
        return std::nullopt;
    }
    // raylib has no side-agnostic modifier constants, only
    // KEY_LEFT_*/KEY_RIGHT_*; the DSL modifiers map to the left keys.
    if (member->member == "Shift") {
        return "KEY_LEFT_SHIFT";
    }
    if (member->member == "Ctrl") {
        return "KEY_LEFT_CONTROL";
    }
    if (member->member == "Alt") {
        return "KEY_LEFT_ALT";
    }
    return "KEY_" + upper_copy(snake_case(member->member));
}

std::optional<std::string> raylib_mouse_constant(const ExprNode& expr) {
    const auto* member = resolved_input_member(expr);
    if (member == nullptr || !is_std_input_enum(*member, "MouseButton")) {
        return std::nullopt;
    }
    return "MOUSE_BUTTON_" + upper_copy(snake_case(member->member));
}

// A binding property that reaches codegen without the matching resolved enum
// member is an internal error: the frontend rejects such programs. Failing
// loudly here replaces the old silent `0`/`-1` dead-input fallbacks.
[[noreturn]] void fail_unresolved_input_binding(const std::string& input_name, const std::string& prop_key) {
    throw std::runtime_error("internal error: input '" + input_name + "' property '" + prop_key +
                             "' lacks a resolved std.input binding; semantic analysis must reject this program");
}

// Emits InputButton/InputAxis constants, their cactus_input_button_key/mouse
// and cactus_input_axis_value lookup functions, and the InputEvent facade
// struct, for whichever of axis/button input kinds the program declares.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string emit_input_constants(const DecoratedProgram& program) {
    std::ostringstream out;
    if (program.ast == nullptr) {
        return out.str();
    }
    bool has_axis_input   = false;
    bool has_button_input = false;
    for (const auto& decl : program.ast->declarations) {
        if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
            has_axis_input   = has_axis_input || input->input_kind == InputKind::Axis;
            has_button_input = has_button_input || input->input_kind == InputKind::Button;
        }
    }

    if (has_button_input) {
        out << "using InputButton = std::uint8_t;\n";
        std::uint8_t button_index = 0;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                if (input->input_kind != InputKind::Button) {
                    continue;
                }
                out << "[[maybe_unused]] constexpr InputButton " << input_action_constant_name(input->name)
                    << " = static_cast<InputButton>(" << static_cast<int>(button_index++) << ");\n";
            }
        }
        out << "\n";

        out << "namespace cactus::runtime::entt_backend {\n";
        out << "int cactus_input_button_key(std::uint8_t button) noexcept {\n";
        out << "    switch (button) {\n";
        button_index = 0;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                if (input->input_kind != InputKind::Button) {
                    continue;
                }
                std::string key = "0";
                for (const auto& prop : input->props) {
                    if (prop.key == "key") {
                        if (auto maybe_key = raylib_key_constant(*prop.value)) {
                            key = *maybe_key;
                        } else {
                            fail_unresolved_input_binding(input->name, prop.key);
                        }
                    }
                }
                out << "        case static_cast<InputButton>(" << static_cast<int>(button_index++) << "): return "
                    << key << ";\n";
            }
        }
        out << "        default:\n";
        out << "            break;\n";
        out << "    }\n";
        out << "    return 0;\n";
        out << "}\n";
        out << "int cactus_input_button_mouse(std::uint8_t button) noexcept {\n";
        out << "    switch (button) {\n";
        button_index = 0;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                if (input->input_kind != InputKind::Button) {
                    continue;
                }
                std::string mouse = "-1";
                for (const auto& prop : input->props) {
                    if (prop.key == "mouse") {
                        if (auto maybe_mouse = raylib_mouse_constant(*prop.value)) {
                            mouse = *maybe_mouse;
                        } else {
                            fail_unresolved_input_binding(input->name, prop.key);
                        }
                    }
                }
                out << "        case static_cast<InputButton>(" << static_cast<int>(button_index++) << "): return "
                    << mouse << ";\n";
            }
        }
        out << "        default:\n";
        out << "            break;\n";
        out << "    }\n";
        out << "    return -1;\n";
        out << "}\n";
        out << "}  // namespace cactus::runtime::entt_backend\n\n";
    }

    if (has_axis_input) {
        out << "using InputAxis = std::uint8_t;\n";
        out << "enum class InputAxisTag : std::uint8_t { ";
        bool first = true;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                if (input->input_kind != InputKind::Axis) {
                    continue;
                }
                out << (first ? "" : ", ") << input->name;
                first = false;
            }
        }
        out << " };\n\n";

        for (const auto& decl : program.ast->declarations) {
            if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                if (input->input_kind != InputKind::Axis) {
                    continue;
                }
                out << "[[maybe_unused]] constexpr InputAxis " << input_action_constant_name(input->name)
                    << " = static_cast<InputAxis>(InputAxisTag::" << input->name << ");\n";
            }
        }
        out << "\n";

        out << "namespace cactus::runtime::entt_backend {\n";
        out << "float cactus_input_axis_value(std::uint8_t action) noexcept {\n";
        out << "    switch (action) {\n";
        for (const auto& decl : program.ast->declarations) {
            if (const auto* input = std::get_if<InputDeclNode>(&decl)) {
                if (input->input_kind != InputKind::Axis) {
                    continue;
                }
                // Consumed keys contribute 0.0 so editor-owned controls stay
                // invisible to same-key gameplay axes (editor input override).
                const auto axis_side = [](const std::string& key) {
                    std::string result = "((!is_input_key_consumed(";
                    result += key;
                    result += ") && cactus::runtime::raylib::IsKeyDown(";
                    result += key;
                    result += ")) ? 1.0F : 0.0F)";
                    return result;
                };
                std::string negative = "0";
                std::string positive = "0";
                for (const auto& prop : input->props) {
                    if (prop.key == "negative") {
                        if (auto key = raylib_key_constant(*prop.value)) {
                            negative = axis_side(*key);
                        } else {
                            fail_unresolved_input_binding(input->name, prop.key);
                        }
                    } else if (prop.key == "positive") {
                        if (auto key = raylib_key_constant(*prop.value)) {
                            positive = axis_side(*key);
                        } else {
                            fail_unresolved_input_binding(input->name, prop.key);
                        }
                    }
                }
                out << "        case static_cast<InputAxis>(InputAxisTag::" << input->name << "):\n";
                out << "            return " << positive << " - " << negative << ";\n";
            }
        }
        out << "        default:\n";
        out << "            return 0.0F;\n";
        out << "    }\n";
        out << "}\n\n";
        out << "}  // namespace cactus::runtime::entt_backend\n\n";
    }

    if (has_axis_input || has_button_input) {
        out << "struct InputEvent {\n";
        if (has_axis_input) {
            out << "    [[nodiscard]] static float axis(InputAxis action) {\n";
            out << "        return cactus::runtime::entt_backend::axis(action);\n";
            out << "    }\n";
        }
        if (has_button_input) {
            out << "    [[nodiscard]] static bool pressed(InputButton action) {\n";
            out << "        return cactus::runtime::entt_backend::pressed(action);\n";
            out << "    }\n";
            out << "    [[nodiscard]] static bool down(InputButton action) {\n";
            out << "        return cactus::runtime::entt_backend::down(action);\n";
            out << "    }\n";
            out << "    [[nodiscard]] static bool released(InputButton action) {\n";
            out << "        return cactus::runtime::entt_backend::released(action);\n";
            out << "    }\n";
        }
        out << "};\n\n";
    }

    return out.str();
}

// Emits the __translate_camera_2d/3d helpers (raylib-facing camera struct
// conversion), once, gated on whichever of the 2D/3D rigs the program uses.
std::string emit_camera_translate_helpers(bool emit_2d_helper,
                                          bool emit_3d_helper,
                                          const std::string& cam2d_cpp,
                                          const std::string& cam3d_cpp,
                                          const std::string& wt3d_cpp) {
    std::ostringstream out;
    if (!emit_2d_helper && !emit_3d_helper) {
        return out.str();
    }
    out << "namespace {\n";
    if (emit_2d_helper) {
        out << "Camera2D __translate_camera_2d(const " << cam2d_cpp << "& cam, int sw, int sh) noexcept {\n";
        out << "    Camera2D cam2d{};\n";
        out << "    cam2d.target   = cam.offset;\n";
        out << "    cam2d.zoom     = cam.zoom;\n";
        out << "    cam2d.rotation = cam.rotation * (180.0F / 3.14159265F);\n";
        out << "    cam2d.offset   = {.x = static_cast<float>(sw) * 0.5F,\n";
        out << "                      .y = static_cast<float>(sh) * 0.5F};\n";
        out << "    return cam2d;\n";
        out << "}\n";
    }
    if (emit_3d_helper) {
        out << "Camera3D __translate_camera_3d(entt::entity entity, const " << cam3d_cpp
            << "& cam, entt::registry& registry) {\n";
        out << "    Camera3D cam3d{};\n";
        out << "    cam3d.fovy       = cam.fov_y;\n";
        out << "    cam3d.projection = CAMERA_PERSPECTIVE;\n";
        out << "    cam3d.up         = {.x = 0.0F, .y = 1.0F, .z = 0.0F};\n";
        out << "    if (registry.all_of<" << wt3d_cpp << ">(entity)) {\n";
        out << "        const auto& xform = registry.get<" << wt3d_cpp << ">(entity);\n";
        out << "        cam3d.position = xform.position;\n";
        out << "        const auto& q  = xform.rotation;\n";
        out << "        cam3d.target   = {.x = xform.position.x + (-(2.0F * ((q.x * q.z) + (q.w * q.y)))),\n";
        out << "                          .y = xform.position.y + (2.0F * ((q.w * q.x) - (q.y * q.z))),\n";
        out << "                          .z = xform.position.z + (-(1.0F - (2.0F * ((q.x * q.x) + (q.y * "
               "q.y)))))};\n";
        out << "    }\n";
        out << "    return cam3d;\n";
        out << "}\n";
    }
    out << "}  // namespace\n\n";
    return out.str();
}

std::string pad_to_width(const std::string& value, std::size_t width) {
    if (value.size() >= width) {
        return value;
    }
    return value + std::string(width - value.size(), ' ');
}

SymbolId entity_symbol(const EntityNode& entity, const DecoratedProgram& program) {
    return resolved_or_local_symbol(SymbolKind::Entity, entity.resolved_entity_id, program.module_name, entity.name);
}

// A slot follows the restored record whose archetype origin is its declaration,
// and is stale when the document has none.
std::string emit_named_slots(const DecoratedProgram& program) {
    const auto entities = EnttCodegenUtils::declared_entities(program);
    if (entities.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "// ── Named Entities ─────────────────────────────────────────────────\n\n";
    out << "struct GeneratedNamedSlots {\n";
    for (const auto* entity : entities) {
        out << "    entt::entity " << canonical_to_cpp_name(entity_symbol(*entity, program)) << " = entt::null;\n";
    }
    out << "};\n\n";
    out << "inline GeneratedNamedSlots& generated_named_slots() {\n";
    out << "    static GeneratedNamedSlots slots;\n";
    out << "    return slots;\n";
    out << "}\n";
    out << "\ninline void generated_rebind_named_slots([[maybe_unused]] entt::registry& registry) {\n";
    std::string origins;
    for (const auto* entity : entities) {
        const auto symbol = entity_symbol(*entity, program);
        const auto origin = EnttPersistenceEmitter::archetype_origin_index(program, symbol);
        if (origin.has_value()) {
            origins += (origins.empty() ? "{" : ", {") + std::to_string(*origin) + ", &" +
                       EnttCodegenUtils::named_slot_name(symbol) + "}";
        } else {
            out << "    " << EnttCodegenUtils::named_slot_name(symbol) << " = entt::null;\n";
        }
    }
    if (!origins.empty()) {
        out << "    cactus::runtime::entt_backend::rebind_named_slots(registry, {" << origins << "});\n";
    }
    out << "}\n\n";
    return out.str();
}

std::string archetype_create_function_name(const std::string& module_name, const std::string& archetype_name) {
    return "create_" + canonical_to_cpp_name(module_name, snake_case(archetype_name));
}

std::string archetype_create_at_function_name(const std::string& module_name, const std::string& archetype_name) {
    return archetype_create_function_name(module_name, archetype_name) + "_at";
}

void emit_archetype_trait_initializers(std::ostringstream& out,
                                       const std::vector<ArchetypeTraitEntry>& traits,
                                       const DecoratedProgram& program,
                                       const std::string& entity_name,
                                       int indent,
                                       const std::optional<std::uint32_t>& origin) {
    const std::string ind(static_cast<std::size_t>(indent) * 4U, ' ');
    // Every DSL-authored creation site (load-time archetypes, hierarchical
    // children, and committed spawns) funnels through this helper, so
    // assigning the creation ordinal here covers all three. A small number of
    // engine-internal entities are created outside the DSL entirely (e.g. the
    // editor camera rig in emit_camera_rig_activation/-exit below) and must
    // independently emplace CreationOrdinal at their own reg.create()
    // site, since any pairs: rule binding on a trait such an entity also
    // carries could otherwise match it in emit_pair_binding_snapshot's view.
    out << ind << "registry.emplace<cactus::runtime::entt_backend::CreationOrdinal>(" << entity_name
        << ", cactus::runtime::entt_backend::CreationOrdinal{.value = "
           "cactus::runtime::entt_backend::generated_next_creation_ordinal()});\n";
    if (origin.has_value()) {
        out << EnttPersistenceEmitter::emit_archetype_origin(*origin, entity_name, ind);
    }
    for (const auto& trait : traits) {
        const std::string cpp_name =
            EnttCodegenUtils::trait_cpp_name(trait.resolved_trait_id, trait.trait_name, program);
        // Prefer canonical key lookup; fall back to source-name lookup for
        // single-module programs that don't go through the artifact linker.
        const std::string lookup_key = trait.resolved_trait_id.has_value()
                                           ? cactus::make_canonical_id(*trait.resolved_trait_id)
                                           : trait.trait_name;
        auto resolved_trait          = program.traits.find(lookup_key);
        if (resolved_trait == program.traits.end()) {
            resolved_trait = program.traits.find(trait.trait_name);
        }
        // A state variant also sets its state's slot.
        const auto variant = EnttCodegenUtils::state_variant(program, trait.resolved_trait_id);
        if (resolved_trait != program.traits.end() && resolved_trait->second.fields.empty()) {
            out << ind
                << (variant.has_value() ? EnttCodegenUtils::enter_state_call(*variant, entity_name, cpp_name + "{}")
                                        : "registry.emplace<" + cpp_name + ">(" + entity_name + ");\n");
            continue;
        }

        const bool retain = origin.has_value() && EnttPersistenceEmitter::trait_has_construction_payload(
                                                      program, trait.resolved_trait_id, trait.trait_name);
        out << ind << "{\n";
        std::size_t widest = std::string("auto component").size();
        for (const auto& assignment : trait.assignments) {
            widest = std::max(widest, std::string("component.").size() + assignment.name.size());
        }
        out << ind << "    " << pad_to_width("auto component", widest) << " = " << cpp_name << "{};\n";
        for (const auto& assignment : trait.assignments) {
            out << ind << "    " << pad_to_width("component." + assignment.name, widest) << " = "
                << EnttCodegenUtils::emit_expr(*assignment.value, program) << ";\n";
        }
        out << ind << "    "
            << (variant.has_value() ? EnttCodegenUtils::enter_state_call(*variant, entity_name, "component")
                                    : "registry.emplace<" + cpp_name + ">(" + entity_name + ", component);\n");
        if (retain) {
            out << EnttPersistenceEmitter::emit_retain_construction(cpp_name, entity_name, ind + "    ");
        }
        out << ind << "}\n";
    }
}

std::string emit_archetype_creation_function(const std::string& archetype_name,
                                             const SymbolId& archetype,
                                             const std::vector<ArchetypeTraitEntry>& traits,
                                             const DecoratedProgram& program) {
    std::ostringstream out;
    const auto at_name     = archetype_create_at_function_name(program.module_name, archetype_name);
    const auto create_name = archetype_create_function_name(program.module_name, archetype_name);
    out << "entt::entity " << at_name << "(entt::registry& registry, entt::entity hint) {\n";
    out << "    auto entity = cactus::runtime::entt_backend::adopt_or_create(registry, hint);\n";
    emit_archetype_trait_initializers(
        out, traits, program, "entity", 1, EnttPersistenceEmitter::archetype_origin_index(program, archetype));
    out << "    return entity;\n";
    out << "}\n\n";
    // Delegates rather than re-running emit_archetype_trait_initializers: the
    // hinted function above already performs every field initialization; the
    // only thing this one adds is an arbitrary (non-hinted) entity.
    out << "entt::entity " << create_name << "(entt::registry& registry) {\n";
    out << "    return " << at_name << "(registry, registry.create());\n";
    out << "}\n\n";
    return out.str();
}

// ── Hierarchical archetype creation (dsl-hierarchical-entity-templates) ──────

// Internal per-node creation helper name: create_<archetype>__node for the
// root, create_<archetype>__node__<role path> for descendants. These are not
// registered in the editor template palette.
std::string archetype_node_create_function_name(const std::string& module_name,
                                                const std::string& archetype_name,
                                                const std::vector<std::string>& role_path) {
    std::string name = "create_" + canonical_to_cpp_name(module_name, snake_case(archetype_name)) + "__node";
    for (const auto& role : role_path) {
        name += "__" + snake_case(role);
    }
    return name;
}

std::string archetype_node_create_at_function_name(const std::string& module_name,
                                                   const std::string& archetype_name,
                                                   const std::vector<std::string>& role_path) {
    return archetype_node_create_function_name(module_name, archetype_name, role_path) + "_at";
}

void emit_archetype_node_helpers(std::ostringstream& out,
                                 const std::string& archetype_name,
                                 const SymbolId& archetype,
                                 const std::vector<ChildArchetypeNode>& children,
                                 const DecoratedProgram& program,
                                 std::vector<std::string>& role_path,
                                 const std::string& parameters = {}) {
    for (const auto& child : children) {
        role_path.push_back(child.role);
        out << "static entt::entity "
            << archetype_node_create_function_name(program.module_name, archetype_name, role_path)
            << "(entt::registry& registry" << parameters << ") {\n";
        out << "    auto entity = registry.create();\n";
        emit_archetype_trait_initializers(
            out,
            child.traits,
            program,
            "entity",
            1,
            EnttPersistenceEmitter::archetype_origin_index(program, archetype, role_path));
        out << "    return entity;\n";
        out << "}\n\n";
        emit_archetype_node_helpers(out, archetype_name, archetype, child.children, program, role_path, parameters);
        role_path.pop_back();
    }
}

// Emit deterministic parent-first preorder creation of descendants: each child
// is created via its per-node helper, receives a generated Parent relation to
// its immediate parent, and then its own descendants follow (D3/D8).
void emit_child_creation_sequence(std::ostringstream& out,
                                  const std::string& module_name,
                                  const std::string& archetype_name,
                                  const std::vector<ChildArchetypeNode>& children,
                                  const std::string& parent_var,
                                  const std::string& var_prefix,
                                  std::vector<std::string>& role_path,
                                  const DecoratedProgram& program,
                                  const std::string& arguments = {}) {
    std::size_t index = 0;
    for (const auto& child : children) {
        const std::string var = var_prefix + "_" + std::to_string(index);
        role_path.push_back(child.role);
        out << "    auto " << var << " = "
            << archetype_node_create_function_name(module_name, archetype_name, role_path) << "(registry" << arguments << ");\n";
        const std::string parent_cpp = EnttCodegenUtils::trait_cpp_name("Parent", program);
        out << "    registry.emplace_or_replace<" << parent_cpp << ">(" << var << ", " << parent_cpp
            << "{.parent = " << parent_var << "});\n";
        emit_child_creation_sequence(out, module_name, archetype_name, child.children, var, var, role_path, program, arguments);
        role_path.pop_back();
        ++index;
    }
}

// A slot without a bound value becomes a parameter of create_<archetype>, so
// the archetype has no nullary factory for callers that need one.
bool creation_takes_parameters(const std::vector<InitializerSlot>& slots) {
    return std::ranges::any_of(slots, [](const auto& slot) { return slot.value == nullptr; });
}

// For hierarchical archetypes, emit per-node helpers plus a canonical
// create_<archetype> wrapper that expands the override-free tree and returns
// the root entity (D9). Flat archetypes generate the same code as before.
std::string emit_planned_creation_functions(const std::string& name,
                                            const SymbolId& archetype,
                                            const std::vector<ArchetypeTraitEntry>& traits,
                                            const std::vector<ChildArchetypeNode>& children,
                                            const std::vector<InitializerSlot>& slots,
                                            const DecoratedProgram& program) {
    std::ostringstream out;
    std::string parameters;
    std::string arguments;
    std::string all_parameters;
    std::string all_arguments;
    for (const auto& slot : slots) {
        const auto declaration = ", [[maybe_unused]] " + EnttCodegenUtils::type_to_cpp(slot.type) + " " + initializer_slot_name(slot.index);
        const auto argument = ", " + initializer_slot_name(slot.index);
        all_parameters += declaration;
        all_arguments += argument;
        if (slot.value != nullptr) {
            continue;
        }
        parameters += declaration;
        arguments += argument;
    }
    const auto create = archetype_create_function_name(program.module_name, name);
    const auto create_at = archetype_create_at_function_name(program.module_name, name);
    std::vector<std::string> role_path;
    const auto node_at     = archetype_node_create_at_function_name(program.module_name, name, role_path);
    const auto node_create = archetype_node_create_function_name(program.module_name, name, role_path);
    out << "static entt::entity " << node_at << "(entt::registry& registry, entt::entity hint" << all_parameters
        << ") {\n";
    out << "    auto entity = cactus::runtime::entt_backend::adopt_or_create(registry, hint);\n";
    emit_archetype_trait_initializers(
        out, traits, program, "entity", 1, EnttPersistenceEmitter::archetype_origin_index(program, archetype));
    out << "    return entity;\n}\n";
    out << "[[maybe_unused]] static entt::entity " << node_create << "(entt::registry& registry" << all_parameters
        << ") {\n";
    out << "    return " << node_at << "(registry, registry.create()" << all_arguments << ");\n}\n";
    emit_archetype_node_helpers(out, name, archetype, children, program, role_path, all_parameters);
    out << "entt::entity " << create_at << "(entt::registry& registry, entt::entity hint" << parameters << ") {\n";
    for (const auto& slot : slots) {
        if (slot.value != nullptr) {
            out << "    [[maybe_unused]] const " << EnttCodegenUtils::type_to_cpp(slot.type) << " " << initializer_slot_name(slot.index)
                << " = " << EnttCodegenUtils::emit_expr(*slot.value, program) << ";\n";
        }
    }
    out << "    auto entity = " << node_at << "(registry, hint" << all_arguments << ");\n";
    emit_child_creation_sequence(out, program.module_name, name, children, "entity", "child", role_path, program, all_arguments);
    out << "    return entity;\n}\n";
    out << "entt::entity " << create << "(entt::registry& registry" << parameters << ") {\n";
    out << "    return " << create_at << "(registry, registry.create()" << arguments << ");\n}\n\n";
    return out.str();
}

std::string emit_archetype_creation_functions(const std::string& archetype_name,
                                              const SymbolId& archetype,
                                              const std::vector<ArchetypeTraitEntry>& traits,
                                              const std::vector<ChildArchetypeNode>& children,
                                              const DecoratedProgram& program,
                                              const std::vector<InitializerSlot>& slots) {
    if (!slots.empty()) {
        return emit_planned_creation_functions(archetype_name, archetype, traits, children, slots, program);
    }
    if (children.empty()) {
        return emit_archetype_creation_function(archetype_name, archetype, traits, program);
    }

    std::ostringstream out;
    std::vector<std::string> role_path;

    const auto node_at_name = archetype_node_create_at_function_name(program.module_name, archetype_name, role_path);
    const auto node_name    = archetype_node_create_function_name(program.module_name, archetype_name, role_path);
    out << "static entt::entity " << node_at_name << "(entt::registry& registry, entt::entity hint) {\n";
    out << "    auto entity = cactus::runtime::entt_backend::adopt_or_create(registry, hint);\n";
    emit_archetype_trait_initializers(
        out, traits, program, "entity", 1, EnttPersistenceEmitter::archetype_origin_index(program, archetype));
    out << "    return entity;\n";
    out << "}\n\n";
    // Delegates rather than re-running emit_archetype_trait_initializers, same
    // as the flat-archetype path in emit_archetype_creation_function.
    out << "static entt::entity " << node_name << "(entt::registry& registry) {\n";
    out << "    return " << node_at_name << "(registry, registry.create());\n";
    out << "}\n\n";
    emit_archetype_node_helpers(out, archetype_name, archetype, children, program, role_path);

    out << "entt::entity " << archetype_create_function_name(program.module_name, archetype_name)
        << "(entt::registry& registry) {\n";
    out << "    auto entity = " << archetype_node_create_function_name(program.module_name, archetype_name, role_path)
        << "(registry);\n";
    emit_child_creation_sequence(
        out, program.module_name, archetype_name, children, "entity", "child", role_path, program);
    out << "    return entity;\n";
    out << "}\n\n";

    out << "entt::entity " << archetype_create_at_function_name(program.module_name, archetype_name)
        << "(entt::registry& registry, entt::entity hint) {\n";
    out << "    auto entity = "
        << archetype_node_create_at_function_name(program.module_name, archetype_name, role_path)
        << "(registry, hint);\n";
    emit_child_creation_sequence(
        out, program.module_name, archetype_name, children, "entity", "child", role_path, program);
    out << "    return entity;\n";
    out << "}\n\n";
    return out.str();
}

// Canonical-aware, loud on ambiguous simple names — see EnttCodegenUtils::find_trait.
const ResolvedTrait* find_trait(const DecoratedProgram& program, const std::string& name) {
    return EnttCodegenUtils::find_trait(program, name);
}

const ResolvedField* find_field(const ResolvedTrait* trait, const std::string& field_name) {
    if (trait == nullptr) {
        return nullptr;
    }
    auto it = std::ranges::find_if(trait->fields, [&](const auto& field) { return field.name == field_name; });
    return it == trait->fields.end() ? nullptr : &*it;
}

bool trait_field_is(const DecoratedProgram& program,
                    const std::string& trait_name,
                    const std::string& field_name,
                    TypeKind kind) {
    const auto* field = find_field(find_trait(program, trait_name), field_name);
    return field != nullptr && field->type.kind == kind;
}

bool has_flat_collider_support(const DecoratedProgram& program) {
    // Canonical ids: Collider/WorldTransform/BoxCollider/CapsuleCollider exist in
    // both flat and volume stdlib modules, so simple-name lookups are ambiguous
    // whenever both variants are linked (std.editor imports both transforms).
    const auto* collider = find_trait(program, "std.physics.flat.Collider");
    return collider != nullptr && collider->is_stdlib && find_field(collider, "layer") != nullptr &&
           find_field(collider, "mask") != nullptr &&
           trait_field_is(program, "std.transform.flat.WorldTransform", "position", TypeKind::Vec2) &&
           trait_field_is(program, "std.transform.flat.WorldTransform", "rotation", TypeKind::Float) &&
           trait_field_is(program, "std.transform.flat.WorldTransform", "scale", TypeKind::Vec2) &&
           trait_field_is(program, "std.physics.flat.BoxCollider", "size", TypeKind::Vec2) &&
           trait_field_is(program, "CircleCollider", "radius", TypeKind::Float) &&
           trait_field_is(program, "std.physics.flat.CapsuleCollider", "height", TypeKind::Float);
}

bool has_flat_physics_query_api(const DecoratedProgram& program) {
    return EnttCodegenUtils::find_enum(program, "QueryResultKind") != nullptr &&
           EnttCodegenUtils::find_struct(program, "QueryContact2D") != nullptr &&
           EnttCodegenUtils::find_struct(program, "QueryResult2D") != nullptr;
}

std::string emit_flat_query_fallback_helpers(const DecoratedProgram& program) {
    const std::string qc2d = EnttCodegenUtils::struct_cpp_name("QueryContact2D", program);
    const std::string qr2d = EnttCodegenUtils::struct_cpp_name("QueryResult2D", program);
    const std::string qrk  = EnttCodegenUtils::enum_cpp_name("QueryResultKind", program);
    std::ostringstream out;
    out << "\n// ── Stdlib 2D Query Fallbacks ────────────────────────────────────────────────\n\n";
    out << "namespace {\n\n";
    out << qc2d
        << " cactus_flat_contact(entt::entity entity, Vector2 normal, float distance, Vector2 overlap) noexcept {\n";
    out << "    return " << qc2d << "{.other = entity, .normal = normal, .distance = distance, .overlap = overlap};\n";
    out << "}\n\n";
    out << qr2d << " cactus_empty_query_result() noexcept {\n";
    out << "    return " << qr2d << "{.kind = " << qrk << "::Empty,\n";
    out << "                         .contact = cactus_flat_contact(entt::entity{entt::null},\n";
    out << "                                                        Vector2{.x = 0.0F, .y = 0.0F},\n";
    out << "                                                        0.0F,\n";
    out << "                                                        Vector2{.x = 0.0F, .y = 0.0F})};\n";
    out << "}\n\n";
    out << "}  // namespace\n\n";
    out << qr2d << " cactus_query_cast_nearest(entt::registry& registry,\n";
    out << "                                        entt::entity subject_entity,\n";
    out << "                                        Vector2 delta,\n";
    out << "                                        int mask,\n";
    out << "                                        entt::entity exclude) {\n";
    out << "    (void)registry;\n";
    out << "    (void)subject_entity;\n";
    out << "    (void)delta;\n";
    out << "    (void)mask;\n";
    out << "    (void)exclude;\n";
    out << "    return cactus_empty_query_result();\n";
    out << "}\n\n";
    out << qr2d << " cactus_query_overlap_deepest(entt::registry& registry,\n";
    out << "                                           entt::entity subject_entity,\n";
    out << "                                           int mask,\n";
    out << "                                           entt::entity exclude) {\n";
    out << "    (void)registry;\n";
    out << "    (void)subject_entity;\n";
    out << "    (void)mask;\n";
    out << "    (void)exclude;\n";
    out << "    return cactus_empty_query_result();\n";
    out << "}\n\n";
    out << "std::vector<" << qc2d << "> cactus_query_overlap_all(entt::registry& registry,\n";
    out << "                                                     entt::entity subject_entity,\n";
    out << "                                                     int mask,\n";
    out << "                                                     entt::entity exclude) {\n";
    out << "    (void)registry;\n";
    out << "    (void)subject_entity;\n";
    out << "    (void)mask;\n";
    out << "    (void)exclude;\n";
    out << "    return {};\n";
    out << "}\n\n";
    return out.str();
}

std::string emit_flat_collision_helpers(const DecoratedProgram& program) {
    auto replace_token = [](std::string& s, const std::string& from, const std::string& to) {
        auto is_ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
        std::string::size_type pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            const bool ok = (pos == 0 || !is_ident(s[pos - 1])) &&
                            (pos + from.size() == s.size() || !is_ident(s[pos + from.size()]));
            if (ok) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            } else {
                pos += from.size();
            }
        }
    };
    const auto wt    = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
    const auto col   = EnttCodegenUtils::trait_cpp_name("std.physics.flat.Collider", program);
    const auto box   = EnttCodegenUtils::trait_cpp_name("std.physics.flat.BoxCollider", program);
    const auto cir   = EnttCodegenUtils::trait_cpp_name("CircleCollider", program);
    const auto cap   = EnttCodegenUtils::trait_cpp_name("std.physics.flat.CapsuleCollider", program);
    const auto qc2d  = EnttCodegenUtils::struct_cpp_name("QueryContact2D", program);
    const auto qr2d  = EnttCodegenUtils::struct_cpp_name("QueryResult2D", program);
    const auto qrk   = EnttCodegenUtils::enum_cpp_name("QueryResultKind", program);
    std::string code = R"(
// ── Stdlib 2D Collider Runtime ───────────────────────────────────────────────

namespace {

struct FlatColliderRef {
    entt::entity entity{entt::null};
    Vector2 position{};
    Collider collider{};
    Vector2 half_extents{};
};

Vector2 cactus_flat_box_overlap(const FlatColliderRef& lhs, const FlatColliderRef& rhs) noexcept {
    const float lhs_center_x = lhs.position.x + lhs.half_extents.x;
    const float lhs_center_y = lhs.position.y + lhs.half_extents.y;
    const float rhs_center_x = rhs.position.x + rhs.half_extents.x;
    const float rhs_center_y = rhs.position.y + rhs.half_extents.y;
    const float delta_x = rhs_center_x - lhs_center_x;
    const float delta_y = rhs_center_y - lhs_center_y;
    const float overlap_x = (lhs.half_extents.x + rhs.half_extents.x) - std::abs(delta_x);
    const float overlap_y = (lhs.half_extents.y + rhs.half_extents.y) - std::abs(delta_y);
    if (overlap_x <= 0.0F || overlap_y <= 0.0F) {
        return Vector2{.x = 0.0F, .y = 0.0F};
    }
    if (overlap_x < overlap_y) {
        return Vector2{.x = delta_x < 0.0F ? -overlap_x : overlap_x, .y = 0.0F};
    }
    return Vector2{.x = 0.0F, .y = delta_y < 0.0F ? -overlap_y : overlap_y};
}

void cactus_collect_flat_colliders(entt::registry& registry, std::vector<FlatColliderRef>& colliders) {
    auto boxes = registry.view<WorldTransform, Collider, BoxCollider>();
    boxes.each([&](entt::entity entity,
                   const WorldTransform& transform,
                   const Collider& collider,
                   const BoxCollider& box) {
        colliders.push_back(FlatColliderRef{.entity       = entity,
                                                  .position     = transform.position,
                                                  .collider     = collider,
                                                  .half_extents = Vector2{.x = box.size.x * 0.5F,
                                                                          .y = box.size.y * 0.5F}});
    });
    auto circles = registry.view<WorldTransform, Collider, CircleCollider>();
    circles.each([&](entt::entity entity,
                     const WorldTransform& transform,
                     const Collider& collider,
                     const CircleCollider& circle) {
        colliders.push_back(FlatColliderRef{.entity       = entity,
                                                  .position     = Vector2{.x = transform.position.x - circle.radius,
                                                                          .y = transform.position.y - circle.radius},
                                                  .collider     = collider,
                                                  .half_extents = Vector2{.x = circle.radius,
                                                                          .y = circle.radius}});
    });
    auto capsules = registry.view<WorldTransform, Collider, CapsuleCollider>();
    capsules.each([&](entt::entity entity,
                      const WorldTransform& transform,
                      const Collider& collider,
                      const CapsuleCollider& capsule) {
        colliders.push_back(FlatColliderRef{.entity       = entity,
                                                  .position     = Vector2{.x = transform.position.x - capsule.radius,
                                                                          .y = transform.position.y - (capsule.height * 0.5F)},
                                                  .collider     = collider,
                                                  .half_extents = Vector2{.x = capsule.radius,
                                                                          .y = capsule.height * 0.5F}});
    });
}

Vector2 cactus_flat_overlap_normal(Vector2 overlap) noexcept {
    if (overlap.x != 0.0F) {
        return Vector2{.x = overlap.x < 0.0F ? -1.0F : 1.0F, .y = 0.0F};
    }
    if (overlap.y != 0.0F) {
        return Vector2{.x = 0.0F, .y = overlap.y < 0.0F ? -1.0F : 1.0F};
    }
    return Vector2{.x = 0.0F, .y = 0.0F};
}

float cactus_flat_length(Vector2 value) noexcept {
    return std::sqrt((value.x * value.x) + (value.y * value.y));
}

QueryContact2D cactus_flat_contact(entt::entity entity, Vector2 normal, float distance, Vector2 overlap) noexcept {
    return QueryContact2D{.other = entity, .normal = normal, .distance = distance, .overlap = overlap};
}

QueryResult2D cactus_empty_query_result() noexcept {
    return QueryResult2D{.kind = QueryResultKind::Empty,
                         .contact = cactus_flat_contact(entt::entity{entt::null},
                                                        Vector2{.x = 0.0F, .y = 0.0F},
                                                        0.0F,
                                                        Vector2{.x = 0.0F, .y = 0.0F})};
}

QueryResult2D cactus_hit_query_result(QueryContact2D contact) noexcept {
    return QueryResult2D{.kind = QueryResultKind::Hit, .contact = contact};
}

bool cactus_query_mask_allows(const FlatColliderRef& candidate, int mask) noexcept {
    return (candidate.collider.layer & mask) != 0;
}

bool cactus_find_flat_collider(entt::registry& registry, entt::entity entity, FlatColliderRef& result) {
    std::vector<FlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    for (const auto& collider : colliders) {
        if (collider.entity == entity) {
            result = collider;
            return true;
        }
    }
    return false;
}

std::optional<QueryContact2D> cactus_flat_overlap_contact(const FlatColliderRef& subject,
                                                          const FlatColliderRef& candidate) noexcept {
    const auto overlap = cactus_flat_box_overlap(candidate, subject);
    if (overlap.x == 0.0F && overlap.y == 0.0F) {
        return std::nullopt;
    }
    return cactus_flat_contact(candidate.entity, cactus_flat_overlap_normal(overlap), 0.0F, overlap);
}

std::optional<QueryContact2D> cactus_flat_cast_contact(const FlatColliderRef& subject,
                                                       const FlatColliderRef& candidate,
                                                       Vector2 delta) noexcept {
    if (auto overlap = cactus_flat_overlap_contact(subject, candidate)) {
        return overlap;
    }

    const float subject_min_x = subject.position.x;
    const float subject_max_x = subject.position.x + (subject.half_extents.x * 2.0F);
    const float subject_min_y = subject.position.y;
    const float subject_max_y = subject.position.y + (subject.half_extents.y * 2.0F);
    const float candidate_min_x = candidate.position.x;
    const float candidate_max_x = candidate.position.x + (candidate.half_extents.x * 2.0F);
    const float candidate_min_y = candidate.position.y;
    const float candidate_max_y = candidate.position.y + (candidate.half_extents.y * 2.0F);

    constexpr float NEG_INF = -std::numeric_limits<float>::infinity();
    constexpr float POS_INF = std::numeric_limits<float>::infinity();
    float x_entry = NEG_INF;
    float x_exit = POS_INF;
    if (delta.x > 0.0F) {
        x_entry = (candidate_min_x - subject_max_x) / delta.x;
        x_exit = (candidate_max_x - subject_min_x) / delta.x;
    } else if (delta.x < 0.0F) {
        x_entry = (candidate_max_x - subject_min_x) / delta.x;
        x_exit = (candidate_min_x - subject_max_x) / delta.x;
    } else if (subject_max_x <= candidate_min_x || subject_min_x >= candidate_max_x) {
        return std::nullopt;
    }

    float y_entry = NEG_INF;
    float y_exit = POS_INF;
    if (delta.y > 0.0F) {
        y_entry = (candidate_min_y - subject_max_y) / delta.y;
        y_exit = (candidate_max_y - subject_min_y) / delta.y;
    } else if (delta.y < 0.0F) {
        y_entry = (candidate_max_y - subject_min_y) / delta.y;
        y_exit = (candidate_min_y - subject_max_y) / delta.y;
    } else if (subject_max_y <= candidate_min_y || subject_min_y >= candidate_max_y) {
        return std::nullopt;
    }

    const float entry = std::max(x_entry, y_entry);
    const float exit = std::min(x_exit, y_exit);
    if (entry > exit || entry < 0.0F || entry > 1.0F) {
        return std::nullopt;
    }

    Vector2 normal{.x = 0.0F, .y = 0.0F};
    if (x_entry > y_entry) {
        normal.x = delta.x > 0.0F ? -1.0F : 1.0F;
    } else {
        normal.y = delta.y > 0.0F ? -1.0F : 1.0F;
    }
    const float distance = cactus_flat_length(delta) * std::max(0.0F, entry);
    return cactus_flat_contact(candidate.entity, normal, distance, Vector2{.x = 0.0F, .y = 0.0F});
}

}  // namespace

QueryResult2D cactus_query_cast_nearest(entt::registry& registry,
                                        entt::entity subject_entity,
                                        Vector2 delta,
                                        int mask,
                                        entt::entity exclude) {
    if (delta.x == 0.0F && delta.y == 0.0F) {
        return cactus_empty_query_result();
    }

    FlatColliderRef subject;
    if (!cactus_find_flat_collider(registry, subject_entity, subject)) {
        return cactus_empty_query_result();
    }

    std::vector<FlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    std::optional<QueryContact2D> nearest;
    for (const auto& candidate : colliders) {
        if (candidate.entity == exclude || !cactus_query_mask_allows(candidate, mask)) {
            continue;
        }
        const auto contact = cactus_flat_cast_contact(subject, candidate, delta);
        if (!contact.has_value()) {
            continue;
        }
        if (!nearest.has_value() || contact->distance < nearest->distance) {
            nearest = *contact;
        }
    }
    return nearest.has_value() ? cactus_hit_query_result(*nearest) : cactus_empty_query_result();
}

QueryResult2D cactus_query_overlap_deepest(entt::registry& registry,
                                           entt::entity subject_entity,
                                           int mask,
                                           entt::entity exclude) {
    FlatColliderRef subject;
    if (!cactus_find_flat_collider(registry, subject_entity, subject)) {
        return cactus_empty_query_result();
    }

    std::vector<FlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    std::optional<QueryContact2D> deepest;
    float deepest_amount = 0.0F;
    for (const auto& candidate : colliders) {
        if (candidate.entity == exclude || !cactus_query_mask_allows(candidate, mask)) {
            continue;
        }
        const auto contact = cactus_flat_overlap_contact(subject, candidate);
        if (!contact.has_value()) {
            continue;
        }
        const float amount = std::abs(contact->overlap.x) + std::abs(contact->overlap.y);
        if (!deepest.has_value() || amount > deepest_amount) {
            deepest = *contact;
            deepest_amount = amount;
        }
    }
    return deepest.has_value() ? cactus_hit_query_result(*deepest) : cactus_empty_query_result();
}

std::vector<QueryContact2D> cactus_query_overlap_all(entt::registry& registry,
                                                     entt::entity subject_entity,
                                                     int mask,
                                                     entt::entity exclude) {
    FlatColliderRef subject;
    if (!cactus_find_flat_collider(registry, subject_entity, subject)) {
        return {};
    }

    std::vector<FlatColliderRef> colliders;
    cactus_collect_flat_colliders(registry, colliders);
    std::vector<QueryContact2D> contacts;
    for (const auto& candidate : colliders) {
        if (candidate.entity == exclude || !cactus_query_mask_allows(candidate, mask)) {
            continue;
        }
        if (const auto contact = cactus_flat_overlap_contact(subject, candidate)) {
            contacts.push_back(*contact);
        }
    }
    return contacts;
}

)";
    replace_token(code, "CapsuleCollider", cap);
    replace_token(code, "CircleCollider", cir);
    replace_token(code, "BoxCollider", box);
    replace_token(code, "WorldTransform", wt);
    replace_token(code, "QueryContact2D", qc2d);
    replace_token(code, "QueryResult2D", qr2d);
    replace_token(code, "QueryResultKind", qrk);
    replace_token(code, "Collider", col);
    return code;
}

// Resolves a std.core-owned event by local name, preferring the std.core
// declaration itself and falling back to the lowest-canonical-id same-named
// event from another linked module. `require_external` additionally
// restricts matches to externally-implemented events (used for `frame`).
const ResolvedEvent* find_std_core_event(const DecoratedProgram& program,
                                         std::string_view name,
                                         bool require_external) {
    const ResolvedEvent* fallback = nullptr;
    for (const auto& [_, event] : program.events) {
        if (event.name != name || !event.symbol_id.has_value() || (require_external && !event.is_external)) {
            continue;
        }
        if (event.symbol_id->module.name == "std.core") {
            return &event;
        }
        if (fallback == nullptr || event.canonical_id < fallback->canonical_id) {
            fallback = &event;
        }
    }
    return fallback;
}

const ResolvedEvent* find_external_frame_event(const DecoratedProgram& program) {
    return find_std_core_event(program, "frame", /*require_external=*/true);
}

const ResolvedEvent* find_std_persistence_event(const DecoratedProgram& program, std::string_view name) {
    for (const auto& [_, event] : program.events) {
        if (event.name == name && event.symbol_id.has_value() && event.symbol_id->module.name == "std.persistence") {
            return &event;
        }
    }
    return nullptr;
}

// `use std.persistence` always brings all six events (the Save and Restore
// triples) into program.events together, whether or not the program's own
// code references every one by name — so finding any one std.persistence
// event implies the module is imported and the rest are present too.
bool program_uses_persistence(const DecoratedProgram& program) {
    return find_std_persistence_event(program, "SaveRequested") != nullptr ||
           find_std_persistence_event(program, "RestoreRequested") != nullptr;
}

// Bridges the activation scheduler to world persistence: SaveRequested/
// RestoreRequested occurrences accumulate in one runtime queue (see
// generated_queue_save_request/generated_queue_restore_request) rather than
// reaching gameplay handlers; this is the boundary decision-6 requires them
// frozen into a batch and processed at, in emission order regardless of kind
// (dsl-stdlib-persistence "Save and restore share a batch").
//
// generated_drain_external_events unconditionally forward-declares and calls
// this whenever the program is graph-driven at all (emit_graph_scheduler_state
// does not gate that on persistence usage), so a definition must always exist
// for such programs even when they never import std.persistence — hence the
// no-op stub below rather than emitting nothing.
std::string emit_persistence_boundary_processor(const DecoratedProgram& program) {
    if (program.execution_graph.phases.empty()) {
        return {};  // not graph-driven: nothing forward-declares or calls this
    }
    if (!program_uses_persistence(program)) {
        return "namespace cactus::runtime::entt_backend {\n\n"
              "inline void generated_process_persistence_boundary(entt::registry&) {}\n\n"
              "}  // namespace cactus::runtime::entt_backend\n\n";
    }
    const auto* save_completed    = find_std_persistence_event(program, "SaveCompleted");
    const auto* save_failed       = find_std_persistence_event(program, "SaveFailed");
    const auto* restore_completed = find_std_persistence_event(program, "RestoreCompleted");
    const auto* restore_failed    = find_std_persistence_event(program, "RestoreFailed");
    const auto save_completed_type    = event_runtime_cpp_type(program, *save_completed->symbol_id);
    const auto save_failed_type       = event_runtime_cpp_type(program, *save_failed->symbol_id);
    const auto restore_completed_type = event_runtime_cpp_type(program, *restore_completed->symbol_id);
    const auto restore_failed_type    = event_runtime_cpp_type(program, *restore_failed->symbol_id);
    const bool has_schema             = EnttPersistenceEmitter::program_retains_provenance(program);

    std::ostringstream out;
    out << "namespace cactus::runtime::entt_backend {\n\n";
    out << "inline void generated_process_persistence_boundary(entt::registry& registry) {\n";
    out << "    auto& persistence_state = generated_persistence_state();\n";
    out << "    if (persistence_state.processing_batch) { return; }\n";
    out << "    auto batch = generated_freeze_persistence_request_batch();\n";
    out << "    if (batch.empty()) { return; }\n";
    out << "    cactus::runtime::entt_backend::ScopedPersistenceBatch batch_guard(persistence_state);\n";
    out << "    const auto& schema = " << (has_schema ? "generated_schema" : "cactus::persistence::SchemaDescriptor{}")
        << ";\n";
    // Every request runs to completion before any outcome is delivered
    // (decision 6: "both captures and writes run before either outcome event
    // is delivered"): a single generated_drain_external_events call after
    // this loop delivers every injected outcome as one cascade, in request
    // order, rather than draining (and so running that request's own
    // gameplay handler) between each one. A save's snapshot is captured
    // lazily and cached across the loop, invalidated only by a successful
    // restore (the only thing here that can change `registry`), so N
    // consecutive saves with no restore between them still capture once,
    // while a save processed after a restore in the same batch observes the
    // restored world rather than the one that existed when the boundary
    // opened. A throw — the adapter ABI is a host-supplied std::function, so
    // a non-compliant custom adapter can throw even though the shipped
    // example adapter never does — becomes that one request's io_failure
    // outcome instead of aborting the rest of the batch or leaking past this
    // boundary.
    out << "    std::optional<cactus::persistence::Snapshot> cached_snapshot;\n";
    out << "    for (const auto& request : batch) {\n";
    out << "        try {\n";
    out << "            if (request.kind == PersistenceRequestKind::Save) {\n";
    out << "                if (!cached_snapshot.has_value()) {\n";
    out << "                    cached_snapshot = "
        << (has_schema ? "generated_capture_world_snapshot(registry)" : "cactus::persistence::Snapshot{}") << ";\n";
    out << "                }\n";
    out << "                const auto outcome = generated_execute_save_request(request.slot, request.request_id, "
           "schema, *cached_snapshot);\n";
    out << "                if (outcome.ok) {\n";
    out << "                    generated_inject_external_event(" << save_completed_type
        << "{.slot = outcome.slot, .request_id = outcome.request_id});\n";
    out << "                } else {\n";
    out << "                    generated_inject_external_event(" << save_failed_type
        << "{.slot = outcome.slot, .request_id = outcome.request_id, .code = outcome.code, .message = "
           "outcome.message});\n";
    out << "                }\n";
    out << "            } else {\n";
    out << "                const auto outcome = "
        << (has_schema ? "generated_execute_restore_request(registry, request.slot, request.request_id, schema)"
                       : "execute_noop_restore_request(request.slot, request.request_id, schema)")
        << ";\n";
    out << "                if (outcome.ok) {\n";
    out << "                    cached_snapshot.reset();\n";
    out << "                    generated_inject_external_event(" << restore_completed_type
        << "{.slot = outcome.slot, .request_id = outcome.request_id});\n";
    out << "                } else {\n";
    out << "                    generated_inject_external_event(" << restore_failed_type
        << "{.slot = outcome.slot, .request_id = outcome.request_id, .code = outcome.code, .message = "
           "outcome.message});\n";
    out << "                }\n";
    out << "            }\n";
    out << "        } catch (const std::exception& persistence_adapter_error) {\n";
    out << "            if (request.kind == PersistenceRequestKind::Save) {\n";
    out << "                generated_inject_external_event(" << save_failed_type
        << "{.slot = request.slot, .request_id = request.request_id, .code = \"io_failure\", .message = "
           "persistence_adapter_error.what()});\n";
    out << "            } else {\n";
    out << "                generated_inject_external_event(" << restore_failed_type
        << "{.slot = request.slot, .request_id = request.request_id, .code = \"io_failure\", .message = "
           "persistence_adapter_error.what()});\n";
    out << "            }\n";
    out << "        }\n";
    out << "    }\n";
    out << "    generated_drain_external_events(registry);\n";
    out << "}\n\n";
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

// Whether any handler in the execution graph's stable topological order is
// triggered by this event — the same per-event handler-presence check
// generated_dispatch_event's body construction already performs (see the
// HandlerTriggerKind::Event match below), reused here to gate emission of
// the boot/teardown activations on actual consumer presence.
bool program_has_event_handler(const DecoratedProgram& program, const SymbolId& event_symbol) {
    return std::ranges::any_of(program.execution_graph.stable_topological_order, [&](const auto& identity) {
        return identity.trigger.kind == HandlerTriggerKind::Event && identity.trigger.symbol == event_symbol;
    });
}

// The phase treated as "the render phase" for graph-driven codegen: the
// phase batch whose dispatch call gets wrapped in begin_render_frame()/
// end_render_frame() so render-phase extern rules (mesh/sprite/light/text
// renderers, the viewport loop, editor HUD overlay, gizmos) actually flush
// to the screen. Prefers std.core's `render` phase (present in every real
// program); falls back to a linked program's own phase literally named
// "render" for programs that don't import std.core. Programs with no phase
// named "render" at all have no render-frame flush wrapped (e.g. the
// game.scheduler test fixtures, which only declare input/fixed_tick).
const PhasePlan* find_render_phase(const DecoratedProgram& program) {
    const PhasePlan* fallback = nullptr;
    for (const auto& phase : program.execution_graph.phases) {
        if (phase.phase.local_name != "render") {
            continue;
        }
        if (phase.phase.module.name == "std.core") {
            return &phase;
        }
        if (fallback == nullptr || phase.phase.module.name < fallback->phase.module.name) {
            fallback = &phase;
        }
    }
    return fallback;
}

std::string emit_backend_main(const DecoratedProgram& program) {
    std::ostringstream out;
    const auto* frame_event       = find_external_frame_event(program);
    const bool graph_driven_frame = !program.execution_graph.phases.empty() && frame_event != nullptr;

    // Boot/teardown one-shot activations (load/unload) are scoped to the
    // graph-driven main loop only (task 2.6): the legacy generated_update_project/
    // generated_render_project path has no currently-built example and is left
    // untouched, so these resolve to null/false whenever graph_driven_frame is false.
    const ResolvedEvent* load_event   = graph_driven_frame ? find_std_core_event(program, "load") : nullptr;
    const ResolvedEvent* unload_event = graph_driven_frame ? find_std_core_event(program, "unload") : nullptr;
    const bool has_load_handler = load_event != nullptr && program_has_event_handler(program, *load_event->symbol_id);
    const bool has_unload_handler =
        unload_event != nullptr && program_has_event_handler(program, *unload_event->symbol_id);

    // Emitted inline here rather than inside generated_load_project's body
    // (design task 2.5): emit_backend_main already has the event/handler-presence
    // lookups this needs, so driving the activation from the call site avoids
    // threading that context into a separately-defined hook function.
    // generated_load_project stays the pre-existing empty compatibility stub for
    // hosts built against runtime.hpp; its call site in main() is unchanged.
    const auto emit_boundary_activation = [&](const ResolvedEvent& event) {
        const auto event_type = event_runtime_cpp_type(program, *event.symbol_id);
        out << "    {\n";
        out << "        auto& boundary_activation = "
               "cactus::runtime::entt_backend::generated_scheduler_state().activation;\n";
        out << "        boundary_activation.active = true;\n";
        out << "        cactus::runtime::entt_backend::generated_dispatch_event(registry, " << event_type << "{});\n";
        out << "        cactus::runtime::entt_backend::generated_drain_event_cascade(registry);\n";
        out << "        cactus::runtime::entt_backend::generated_commit_activation(registry);\n";
        // Scene transitions obey the same boundary ordering as any other
        // activation (dsl-stdlib-persistence): a SaveRequested queued during
        // this load/unload activation must be processed here too, not left
        // to accumulate until the next frame (or forever, for unload).
        out << "        cactus::runtime::entt_backend::generated_process_persistence_boundary(registry);\n";
        out << "        boundary_activation.active = false;\n";
        out << "    }\n";
    };

    out << "\n// ── Backend Entry Point ───────────────────────────────────────────────\n\n";
    out << "#ifndef CACTUS_GENERATED_NO_MAIN\n";
    out << "int main() try {\n";
    out << "    const auto config = cactus::runtime::entt_backend::generated_project_config();\n";
    out << "    InitWindow(config.window_width, config.window_height, config.window_title);\n";
    out << "    SetExitKey(KEY_NULL);\n";
    out << "    SetTargetFPS(config.target_fps);\n\n";
    out << "    entt::registry registry;\n";
    out << "    entt::dispatcher dispatcher;\n";
    out << "    cactus::runtime::entt_backend::generated_setup_dispatcher(dispatcher);\n";
    if (program_uses_persistence(program)) {
        // Saving works without host C++ integration (dsl-stdlib-persistence):
        // an author who only writes Cactus still gets a working adapter.
        // Hosts building their own main() via CACTUS_GENERATED_NO_MAIN
        // register whatever adapter they want instead of this default.
        out << "    cactus::runtime::entt_backend::register_persistence_adapter(\n";
        out << "        cactus::runtime::entt_backend::make_example_file_adapter(\"saves\"));\n";
    }
    out << "    cactus::runtime::entt_backend::generated_init_project(registry);\n";
    out << "    cactus::runtime::entt_backend::generated_load_project(registry);\n";
    if (has_load_handler) {
        emit_boundary_activation(*load_event);
    }
    out << "    while (!WindowShouldClose()) {\n";
    out << "        const float dt = GetFrameTime();\n";
    out << "        BeginDrawing();\n";
    out << "        ClearBackground(RAYWHITE);\n";
    if (graph_driven_frame) {
        const auto frame_type = event_runtime_cpp_type(program, *frame_event->symbol_id);
        out << "        cactus::runtime::entt_backend::generated_inject_external_event(" << frame_type
            << "{.dt = dt});\n";
        out << "        cactus::runtime::entt_backend::generated_drain_external_events(registry);\n";
    } else {
        out << "        cactus::runtime::entt_backend::generated_update_project(registry, dispatcher, dt);\n";
        out << "        cactus::runtime::entt_backend::generated_render_project(registry, dispatcher);\n";
    }
    out << "        EndDrawing();\n";
    out << "    }\n\n";
    if (has_unload_handler) {
        emit_boundary_activation(*unload_event);
    }
    out << "    CloseWindow();\n";
    out << "    return 0;\n";
    out << "} catch (...) {\n";
    out << "    return 1;\n";
    out << "}\n";
    out << "#endif  // CACTUS_GENERATED_NO_MAIN\n";
    return out.str();
}
}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string CppEnttCodegen::generate(const DecoratedProgram& program) {
    std::ostringstream out;

    const bool uses_flat     = module_uses_camera_flat(program);
    const bool uses_viewport = module_uses_camera_viewport(program);
    const bool uses_volume   = module_uses_camera_volume(program);
    const bool uses_text     = uses_text_format(program);
    const bool uses_editor   = module_uses_editor(program);
    // Dimensionality from the root module's resolved WorldTransform references —
    // deterministic even when std.editor links both flat and volume variants.
    const WorldTransformUsage wt_usage = EnttCodegenUtils::world_transform_usage(program);
    // Camera-rig classification, computed once and reused everywhere it's
    // needed below (the early __translate_camera_2d/3d helper emission and
    // the legacy generated_render_project viewport loop): the 2D rig stores
    // only EditorCamera2D + Camera + Viewport, so it exists for any
    // flat-camera program that is not 3D (including transforms-free UI
    // tools); the 3D rig carries a volume WorldTransform and requires it.
    const bool rig_is_2d        = wt_usage.flat || (uses_flat && !wt_usage.volume);
    const bool rig_is_3d        = wt_usage.volume;
    const std::string cam2d_cpp = EnttCodegenUtils::trait_cpp_name("std.camera.flat.Camera", program);
    const std::string cam3d_cpp = EnttCodegenUtils::trait_cpp_name("std.camera.volume.Camera", program);
    const std::string wt3d_cpp  = EnttCodegenUtils::trait_cpp_name("std.transform.volume.WorldTransform", program);
    const bool emit_2d_helper   = uses_viewport && uses_flat && (rig_is_2d || !rig_is_3d);
    const bool emit_3d_helper   = uses_viewport && uses_volume && rig_is_3d;

    // Header
    out << "// Generated by Cactus DSL Compiler (cpp-entt backend)\n\n";
    out << "// NOLINTBEGIN(modernize-use-std-numbers,readability-function-cognitive-complexity,"
           "bugprone-branch-clone,bugprone-reserved-identifier,bugprone-throwing-static-initialization,"
           "cppcoreguidelines-init-variables,cppcoreguidelines-pro-type-member-init,"
           "readability-redundant-member-init,readability-simplify-boolean-expr,"
           "readability-braces-around-statements,readability-isolate-declaration,"
           "readability-math-missing-parentheses,readability-qualified-auto,readability-redundant-parentheses,"
           "performance-move-const-arg,readability-named-parameter,modernize-use-designated-initializers,"
           "readability-use-std-min-max)\n";
    out << "// Generated C++ mirrors authored DSL constants, declarations, and system control flow.\n\n";
    out << "#include \"backends/cpp-entt/runtime.hpp\"\n";
    if (program_uses_persistence(program)) {
        out << "#include \"backends/cpp-entt/persistence_file_adapter.hpp\"\n";
    }
    out << "\n";
    out << "#include <entt/entt.hpp>\n";
    out << "#include <raylib.h>\n";
    out << "\n";
    out << "#ifdef PI\n";
    out << "#undef PI\n";
    out << "#endif\n";
    // raylib defines `typedef Camera3D Camera;` which conflicts with the Camera DSL
    // component struct. Redirect the token so struct Camera resolves to its canonical C++ name.
    // Use Camera3D directly for any raylib 3D-camera uses.
    if (uses_flat || uses_volume) {
        // When both camera modules are in the merged traits map (e.g. via std.editor
        // which transitively imports both), disambiguate by WorldTransform dimensionality:
        // 3D programs use volume camera, 2D programs use flat camera.
        const std::string camera_cpp = (uses_volume && wt_usage.volume)
                                           ? EnttCodegenUtils::trait_cpp_name("std.camera.volume.Camera", program)
                                           : EnttCodegenUtils::trait_cpp_name("std.camera.flat.Camera", program);
        out << "// Suppress raylib Camera typedef; DSL Camera struct takes this name\n";
        out << "#define Camera " << camera_cpp << "\n";
    }
    out << "\n";
    out << "#include <algorithm>\n";
    out << "#include <array>\n";
    out << "#include <cmath>\n";
    out << "#include <cstdint>\n";
    if (!program.execution_graph.phases.empty()) {
        out << "#include <deque>\n";
        out << "#include <functional>\n";
    }
    out << "#include <limits>\n";
    out << "#include <optional>\n";
    out << "#include <string>\n";
    out << "#include <string_view>\n";
    out << "#include <unordered_map>\n";
    out << "#include <unordered_set>\n";
    if (!program.execution_graph.phases.empty()) {
        out << "#include <stdexcept>\n";
        out << "#include <utility>\n";
        out << "#include <variant>\n";
    }
    out << "#include <vector>\n";
    if (uses_text) {
        out << "#include <format>\n";
    }
    // Task 6.2: Include runtime header when extern funcs are present
    if (has_extern_funcs(program)) {
        out << "#include \"common/cactus_runtime.hpp\"\n";
    }
    out << "\n";

    out << "using Quat = cactus::runtime::Quat;\n";
    out << "\n";

    // vec2/vec3 constructors live in common/cactus_runtime.hpp's global
    // namespace (always reachable via the runtime.hpp include above) - not
    // emitted here to avoid a duplicate/ambiguous definition. quat's
    // constructor stays codegen-emitted; out of scope for this migration.
    out << "inline cactus::runtime::Quat quat(float x, float y, float z, float w) {\n";
    out << "    return cactus::runtime::Quat{.x = x, .y = y, .z = z, .w = w};\n";
    out << "}\n\n";

    out << emit_input_constants(program);

    if (program.ast != nullptr) {
        std::uint32_t next_asset_handle = 1U;
        bool emitted_asset_constants    = false;
        for (const auto& decl : program.ast->declarations) {
            if (const auto* asset = std::get_if<AssetDeclNode>(&decl)) {
                if (!emitted_asset_constants) {
                    out << "// ── Asset Handles ──────────────────────────────────────────────────\n\n";
                    emitted_asset_constants = true;
                }
                out << "constexpr cactus::runtime::AssetHandle " << asset->name << " = " << next_asset_handle++
                    << "U; // NOLINT(readability-identifier-naming)\n";
            }
        }
        if (emitted_asset_constants) {
            out << "\n";
        }
    }

    // Enums
    for (const auto& [name, e] : program.enums) {
        auto emitted_code = EnttComponentEmitter::emit_enum(e);
        if (!emitted_code.empty()) {
            out << emitted_code << "\n";
        }
    }

    // stdlib::random types: emit canonical-name aliases to the runtime header types so that
    // runtime function return values (e.g. seeded(), uniform()) are assignment-compatible with
    // the component fields and ECS views that use the canonical names (std_random__Rng, etc.).
    const bool uses_random = program_uses_module(program, "std.random");
    if (uses_random) {
        out << "using std_random__Rng        = cactus::runtime::stdlib::random::Rng;\n";
        out << "using std_random__Uniform    = cactus::runtime::stdlib::random::Uniform;\n";
        out << "using std_random__UniformInt = cactus::runtime::stdlib::random::UniformInt;\n";
        out << "using std_random__Normal     = cactus::runtime::stdlib::random::Normal;\n";
        out << "\n";
    }

    // POD structs (stdlib::random struct types are suppressed — aliases above cover them).
    static const std::unordered_set<std::string> kRandomRuntimeTypes{"Rng", "Uniform", "UniformInt", "Normal"};
    for (const auto& [name, s] : program.structs) {
        if (uses_random && kRandomRuntimeTypes.contains(s.name)) {
            continue;
        }
        out << EnttComponentEmitter::emit_pod_struct(s) << "\n";
    }

    out << emit_module_constants(program);

    // Component structs (from traits)
    // stdlib::random trait types are exposed via 'using' aliases above — suppress their generated structs.
    for (const auto& [name, t] : program.traits) {
        if (uses_random && t.module_name == "std.random" && kRandomRuntimeTypes.contains(t.name)) {
            continue;
        }
        out << EnttComponentEmitter::emit_component(t, program) << "\n";
    }
    out << EnttComponentEmitter::emit_state_helpers(program);

    out << emit_collider_query_helpers(program);
    out << emit_named_slots(program);
    out << EnttPersistenceEmitter::emit_schema_descriptor(program);
    out << EnttPersistenceEmitter::emit_archetype_node_table(program);
    out << EnttPersistenceEmitter::emit_missing_provenance_probe(program);

    // Monotonic, non-reused per-entity creation order (dsl-pair-relations):
    // pair handlers sort their binding snapshots by this ordinal so tuple and
    // emitted-event order is deterministic and backend-independent. Assigned
    // once at every entity's creation site (load-time and committed spawn both
    // route through emit_archetype_trait_initializers), never reassigned.
    // Runtime-hosted (backends/cpp-entt/runtime.hpp): CreationOrdinal has no
    // program-specific shape, so it isn't re-emitted here — a second
    // definition of the same name in the same namespace would be a
    // duplicate-definition compile error (runtime.hpp is always included by
    // generated code).

    // ── Viewport camera-translate helpers ─────────────────────────────────────
    // Emitted once, early — after component structs exist, before the
    // graph-driven scheduler state (which needs to call these from the render
    // phase batch's per-viewport wrap) and before the legacy generated_render_project
    // (which also calls these). Defining them once here avoids a redefinition
    // between the two call sites.
    out << emit_camera_translate_helpers(emit_2d_helper, emit_3d_helper, cam2d_cpp, cam3d_cpp, wt3d_cpp);

    out << emit_projected_trait_registry_helpers(program);

    out << EnttPersistenceEmitter::emit_world_capture(program);
    out << EnttRestoreEmitter::emit_world_restore(program);

    out << emit_event_structs(program);

    out << emit_external_command_forward_declarations(program);
    out << emit_graph_scheduler_state(program);
    out << emit_persistence_boundary_processor(program);
    out << emit_graph_external_handler_abi(program);

    if (has_flat_collider_support(program)) {
        out << emit_flat_collision_helpers(program);
    } else if (has_flat_physics_query_api(program)) {
        out << emit_flat_query_fallback_helpers(program);
    }

    out << EnttSystemEmitter::emit_entt_hierarchy_helpers(program);

    // Persist serialization stubs
    out << "// ── Persist Serialization ────────────────────────────────────────────\n\n";
    for (const auto& [name, t] : program.traits) {
        if (trait_declares_persist_field(t)) {
            const std::string cpp_name = canonical_to_cpp_name(t.module_name, t.name);
            out << "void save_" << cpp_name << "(const " << cpp_name << "& comp) {\n";
            out << "    (void)comp;\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    out << "    // serialize comp." << f.name << "\n";
                }
            }
            out << "}\n\n";
            out << "void load_" << cpp_name << "(" << cpp_name << "& comp) {\n";
            out << "    (void)comp;\n";
            for (const auto& f : t.fields) {
                if (f.is_persist) {
                    out << "    // deserialize into comp." << f.name << "\n";
                }
            }
            out << "}\n\n";
        }
    }

    // Entity creation from flattened templates and entities (inline and template-backed)
    if (program.ast != nullptr) {
        out << "// ── Entity Creation ─────────────────────────────────────────────────\n\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* tmpl = std::get_if<TemplateNode>(&decl)) {
                out << emit_archetype_creation_functions(
                    tmpl->name,
                    resolved_or_local_symbol(
                        SymbolKind::Template, tmpl->resolved_template_id, program.module_name, tmpl->name),
                    tmpl->traits,
                    tmpl->children,
                    program,
                    tmpl->initializers);
            }
        }
        for (auto& decl : program.ast->declarations) {
            if (auto* entity = std::get_if<EntityNode>(&decl)) {
                out << emit_archetype_creation_functions(
                    entity->name,
                    resolved_or_local_symbol(
                        SymbolKind::Entity, entity->resolved_entity_id, program.module_name, entity->name),
                    entity->traits,
                    entity->children,
                    program,
                    entity->initializers);
            }
        }
    }

    // Template registry (used by editor_spawn_template and EditorTemplatePalette)
    if (uses_editor && program.ast != nullptr) {
        // kEditorPaletteMaxSlots below sizes a fixed palette-button label pool; a
        // pub template count beyond it would silently drop palette buttons with no
        // indication why (editor-declarative-rendering), so fail the build instead.
        constexpr std::size_t kEditorPaletteMaxSlots = 16;
        std::size_t pub_template_count               = 0;
        for (const auto& decl : program.ast->declarations) {
            const auto* tmpl = std::get_if<TemplateNode>(&decl);
            if (tmpl == nullptr || !tmpl->is_pub) {
                continue;
            }
            ++pub_template_count;
            // The palette stores each factory as a plain entt::entity(*)(entt::registry&);
            // a template parameter would add an argument the palette cannot supply.
            if (creation_takes_parameters(tmpl->initializers)) {
                throw std::runtime_error(
                    "cpp-entt backend: pub template '" + tmpl->name +
                    "' takes template parameters, but std.editor's palette can only spawn parameterless "
                    "templates; drop `pub`, or wrap it in a parameterless pub template that applies it");
            }
        }
        if (pub_template_count > kEditorPaletteMaxSlots) {
            throw std::runtime_error(
                "cpp-entt backend: " + std::to_string(pub_template_count) +
                " pub templates registered, but std.editor's palette button pool only supports " +
                std::to_string(kEditorPaletteMaxSlots) +
                "; reduce the number of pub templates or raise kEditorPaletteMaxSlots in cpp_entt_codegen.cpp");
        }

        out << "// ── Template Registry ───────────────────────────────────────────────\n\n";
        out << "using TemplateFactory = entt::entity(*)(entt::registry&);\n";
        out << "static const std::unordered_map<std::string, TemplateFactory> cactus_template_registry = {\n";
        for (const auto& decl : program.ast->declarations) {
            if (const auto* tmpl = std::get_if<TemplateNode>(&decl)) {
                if (tmpl->is_pub) {
                    out << "    {\"" << tmpl->name << "\", "
                        << archetype_create_function_name(program.module_name, tmpl->name) << "},\n";
                }
            }
        }
        out << "};\n\n";

        // Declaration-ordered companion to cactus_template_registry: the map's iteration
        // order is not guaranteed to match source order, so template_names()/template_index()
        // (editor-template-registry) need an explicit ordered list instead.
        out << "static const std::vector<std::string> cactus_template_registry_order = {\n";
        for (const auto& decl : program.ast->declarations) {
            if (const auto* tmpl = std::get_if<TemplateNode>(&decl)) {
                if (tmpl->is_pub) {
                    out << "    \"" << tmpl->name << "\",\n";
                }
            }
        }
        out << "};\n\n";
        out << "namespace cactus::runtime::entt_backend {\n";
        // Not noexcept: returns std::vector<std::string> by value, which allocates and could
        // (in principle, on OOM) throw — clang-tidy's bugprone-exception-escape correctly flags
        // a noexcept function that isn't actually exception-safe.
        out << "std::vector<std::string> editor_template_names() { return cactus_template_registry_order; }\n";
        out << "int editor_template_index(const std::string& name) noexcept {\n";
        out << "    const auto it = std::ranges::find(cactus_template_registry_order, name);\n";
        out << "    if (it == cactus_template_registry_order.end()) { return -1; }\n";
        out << "    return static_cast<int>(std::distance(cactus_template_registry_order.begin(), it));\n";
        out << "}\n";
        out << "}  // namespace cactus::runtime::entt_backend\n\n";

        // EditorTemplatePalette (stdlib-editor) needs one ScreenLabel-carrying entity per
        // button, but ScreenLabel is entity-attached, not an event, and the palette rule
        // (self-scoped to EditorState) has no DSL mechanism to spawn or discover entities
        // indexed by an arbitrary per-frame count — bounded `for` can't mutate an outer-scope
        // counter (see editor-declarative-rendering design notes) and there's no list-index
        // or query-by-field-value expression. A small fixed pool of lazily-spawned label
        // entities, indexed by editor_palette_label_slot(), sidesteps both: the palette rule
        // gets a stable per-index entity_id via `project ScreenLabel to
        // palette_label_slot(idx): ...` (the existing cross-entity project-to-target
        // mechanism). Sized generously past any realistic template count; the pub
        // template count is checked against this pool size above and the build
        // fails loudly rather than silently dropping palette buttons.
        const std::string sl_cpp = EnttCodegenUtils::trait_cpp_name("ScreenLabel", program);
        out << "// ── Editor Palette Label Slot Pool ──────────────────────────────────\n\n";
        out << "namespace cactus::runtime::entt_backend {\n";
        out << "inline constexpr int kEditorPaletteMaxSlots = " << kEditorPaletteMaxSlots << ";\n";
        // Not noexcept: reserve()/push_back() allocate and could (in principle, on OOM) throw —
        // same reasoning as editor_template_names() above.
        out << "std::vector<entt::entity>& editor_palette_label_slots(entt::registry& registry) {\n";
        out << "    static std::unordered_map<entt::registry*, std::vector<entt::entity>> pools;\n";
        out << "    auto& slots = pools[&registry];\n";
        out << "    if (slots.empty()) {\n";
        out << "        slots.reserve(kEditorPaletteMaxSlots);\n";
        out << "        for (int i = 0; i < kEditorPaletteMaxSlots; ++i) {\n";
        out << "            auto entity = registry.create();\n";
        out << "            registry.emplace<" << sl_cpp << ">(entity);\n";
        out << "            slots.push_back(entity);\n";
        out << "        }\n";
        out << "    }\n";
        out << "    return slots;\n";
        out << "}\n";
        // Not noexcept either: calls editor_palette_label_slots() above, which isn't noexcept.
        out << "entt::entity editor_palette_label_slot(entt::registry& registry, int index) {\n";
        out << "    auto& slots = editor_palette_label_slots(registry);\n";
        out << "    if (index < 0 || static_cast<std::size_t>(index) >= slots.size()) { return "
               "entt::entity{entt::null}; "
               "}\n";
        out << "    return slots[static_cast<std::size_t>(index)];\n";
        out << "}\n";
        out << "}  // namespace cactus::runtime::entt_backend\n\n";
    }

    // Authored pure functions must precede systems that call them.
    if (program.ast != nullptr) {
        out << "// ── Authored Functions ───────────────────────────────────────────────\n\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* func = std::get_if<FuncNode>(&decl); func != nullptr && !func->is_extern) {
                out << EnttSystemEmitter::emit_func(*func, program);
            }
        }
    }

    // System functions
    if (program.ast != nullptr) {
        out << "// ── Systems ─────────────────────────────────────────────────────────\n\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* rule = std::get_if<RuleNode>(&decl)) {
                out << EnttSystemEmitter::emit_system(*rule, program);
            }
            if (auto* rule = std::get_if<ExternRuleNode>(&decl); rule != nullptr) {
                if (has_compiler_owned_external_handler(program, *rule)) {
                    out << EnttSystemEmitter::emit_extern_system(*rule, program);
                } else if (program.execution_graph.phases.empty() && has_user_external_handler(program, *rule)) {
                    throw std::runtime_error("cpp-entt user external rule '" +
                                             make_canonical_id(*rule->resolved_rule_id) +
                                             "' requires a linked phase graph and contract-shaped callback ABI");
                }
            }
        }
    }

    out << emit_graph_handler_dispatch(program);

    // Dispatcher setup
    out << "// ── Event Dispatcher ────────────────────────────────────────────────\n\n";
    out << "namespace cactus::runtime::entt_backend {\n\n";
    out << "void generated_setup_dispatcher(entt::dispatcher& dispatcher) {\n";
    if (program.ast != nullptr) {
        out << "    (void)dispatcher;\n";
        for (auto& decl : program.ast->declarations) {
            if (auto* event = std::get_if<EventNode>(&decl)) {
                out << "    " << EnttEventEmitter::emit_sink_connection(*event, program);
            }
        }
    }
    out << "}\n\n";

    // Main game loop — extract window constants from const block if available
    const auto win_width  = window_constant(program, "WINDOW_WIDTH").value_or("800");
    const auto win_height = window_constant(program, "WINDOW_HEIGHT").value_or("600");
    const auto win_title  = window_constant(program, "WINDOW_TITLE", ".c_str()").value_or("\"Cactus Game\"");
    const auto win_fps    = window_constant(program, "TARGET_FPS").value_or("60");

    out << "// ── Runtime Glue ────────────────────────────────────────────────────\n\n";
    out << "ProjectConfig generated_project_config() noexcept {\n";
    out << "    return {.window_width = " << win_width << ", .window_height = " << win_height
        << ", .window_title = " << win_title << ", .target_fps = " << win_fps << "};\n";
    out << "}\n\n";

    const bool tracks_lifecycle = EnttCodegenUtils::tracks_lifecycle(program);
    out << "void generated_init_project(entt::registry& registry) {\n";
    out << "    (void)registry;\n";
    if (tracks_lifecycle) {
        out << "    generated_connect_lifecycle_trackers(registry);\n";
    }
    if (program.ast != nullptr) {
        for (auto& decl : program.ast->declarations) {
            if (auto* asset = std::get_if<AssetDeclNode>(&decl)) {
                const auto registration = asset_register_call(*asset);
                if (!registration.empty()) {
                    out << "    " << registration << "\n";
                }
            }
        }
        // Every handle first, so initializers can name any entity of the module.
        for (const auto* entity : EnttCodegenUtils::declared_entities(program)) {
            out << "    " << EnttCodegenUtils::named_slot_name(entity_symbol(*entity, program))
                << " = registry.create();\n";
        }
        for (const auto* entity : EnttCodegenUtils::declared_entities(program)) {
            out << "    " << archetype_create_at_function_name(program.module_name, entity->name) << "(registry, "
                << EnttCodegenUtils::named_slot_name(entity_symbol(*entity, program)) << ");\n";
        }
    }
    // ── Editor runtime glue ───────────────────────────────────────────────────
    // Gates use canonical trait identity (D1) and root-program dimensionality
    // (D2). Simple-name presence probes on the merged trait map are ambiguous
    // once std.editor links both stdlib transform variants, and linked programs
    // key the map by canonical id, so `contains(<simple>)` is always false.
    // rig_is_2d/rig_is_3d/cam2d_cpp/cam3d_cpp/wt3d_cpp are computed once, above.
    const std::string wt2d_cpp   = EnttCodegenUtils::trait_cpp_name("std.transform.flat.WorldTransform", program);
    const std::string locked_cpp = uses_editor ? EnttCodegenUtils::trait_cpp_name("EditorLocked", program) : "";
    if (uses_editor && wt_usage.flat && EnttCodegenUtils::has_trait(program, "std.physics.flat.BoxCollider")) {
        const std::string box_cpp = EnttCodegenUtils::trait_cpp_name("std.physics.flat.BoxCollider", program);
        out << "    cactus::runtime::entt_backend::register_editor_hit_test_impl(\n";
        out << "        [](entt::registry& reg, Vector2 world_pos, int /*mask*/) -> entt::entity {\n";
        out << "            auto view = reg.view<" << wt2d_cpp << ", " << box_cpp << ">(entt::exclude<" << locked_cpp
            << ">);\n";
        out << "            for (auto entity : view) {\n";
        out << "                const auto& xform = reg.get<" << wt2d_cpp << ">(entity);\n";
        out << "                const auto& box   = reg.get<" << box_cpp << ">(entity);\n";
        out << "                if (world_pos.x < xform.position.x || world_pos.x > xform.position.x + box.size.x) { "
               "continue; }\n";
        out << "                if (world_pos.y < xform.position.y || world_pos.y > xform.position.y + box.size.y) { "
               "continue; }\n";
        out << "                return entity;\n";
        out << "            }\n";
        out << "            return entt::entity{entt::null};\n";
        out << "        });\n";
    }
    if (uses_editor && (wt_usage.flat || wt_usage.volume)) {
        // The applied position argument follows the rig dimensionality: volume
        // programs place with pos3d, flat with pos2d (volume wins when both).
        const bool volume_transform    = wt_usage.volume;
        const std::string wt_cpp_spawn = volume_transform ? wt3d_cpp : wt2d_cpp;
        const std::string lt_canonical =
            volume_transform ? "std.transform.volume.LocalTransform" : "std.transform.flat.LocalTransform";
        const bool has_local     = EnttCodegenUtils::has_trait(program, lt_canonical);
        const std::string lt_cpp = has_local ? EnttCodegenUtils::trait_cpp_name(lt_canonical, program) : "";
        const char* pos_arg2d    = volume_transform ? "Vector2 /*pos2d*/" : "Vector2 pos2d";
        const char* pos_arg3d    = volume_transform ? "Vector3 pos3d" : "Vector3 /*pos3d*/";
        const char* pos_value    = volume_transform ? "pos3d" : "pos2d";
        out << "    cactus::runtime::entt_backend::register_editor_spawn_impl(\n";
        out << "        [](entt::registry& reg, const std::string& name, " << pos_arg2d << ", " << pos_arg3d
            << ") -> entt::entity {\n";
        out << "            auto it = cactus_template_registry.find(name);\n";
        out << "            if (it == cactus_template_registry.end()) { return entt::entity{entt::null}; }\n";
        out << "            auto entity = it->second(reg);\n";
        // The editor places an entity at a runtime-chosen position, the same
        // kind of per-entity override an authored `spawn Template: Field:
        // value:` gets from emit_spawn_overrides — so this site must retain
        // it the same way, or a saved editor-placed entity would report its
        // template's baseline position instead of where it was placed.
        const std::string wt_canonical =
            volume_transform ? "std.transform.volume.WorldTransform" : "std.transform.flat.WorldTransform";
        if (has_local) {
            out << "            if (auto* lt = reg.try_get<" << lt_cpp << ">(entity)) {\n";
            out << "                lt->position = " << pos_value << ";\n";
            if (EnttPersistenceEmitter::structural_site_tracks_construction(program, std::nullopt, lt_canonical)) {
                out << "                cactus::runtime::entt_backend::retain_construction<" << lt_cpp
                    << ">(reg, entity, *lt);\n";
            }
            out << "            }\n";
        }
        out << "            if (auto* wt = reg.try_get<" << wt_cpp_spawn << ">(entity)) {\n";
        out << "                wt->position = " << pos_value << ";\n";
        if (EnttPersistenceEmitter::structural_site_tracks_construction(program, std::nullopt, wt_canonical)) {
            out << "                cactus::runtime::entt_backend::retain_construction<" << wt_cpp_spawn
                << ">(reg, entity, *wt);\n";
        }
        out << "            }\n";
        out << "            return entity;\n";
        out << "        });\n";
    }
    if (uses_editor && rig_is_3d && EnttCodegenUtils::has_trait(program, "ModelRenderer")) {
        const std::string mr_cpp = EnttCodegenUtils::trait_cpp_name("ModelRenderer", program);
        out << "    cactus::runtime::entt_backend::register_editor_raycast_impl(\n";
        out << "        [](entt::registry& reg, Ray ray, int /*mask*/) -> entt::entity {\n";
        out << "            entt::entity nearest = entt::null;\n";
        out << "            float nearest_distance = 0.0F;\n";
        out << "            auto view = reg.view<" << wt3d_cpp << ", " << mr_cpp << ">(entt::exclude<" << locked_cpp
            << ">);\n";
        out << "            for (auto entity : view) {\n";
        out << "                const auto& xform    = reg.get<" << wt3d_cpp << ">(entity);\n";
        out << "                const auto& renderer = reg.get<" << mr_cpp << ">(entity);\n";
        out << "                BoundingBox box = cactus::runtime::entt_backend::model_bounds_box(renderer.model);\n";
        out << "                if (box.max.x - box.min.x <= 0.0F && box.max.y - box.min.y <= 0.0F &&\n";
        out << "                    box.max.z - box.min.z <= 0.0F) {\n";
        out << "                    box = BoundingBox{.min = {.x = -0.5F, .y = -0.5F, .z = -0.5F},\n";
        out << "                                      .max = {.x = 0.5F, .y = 0.5F, .z = 0.5F}};\n";
        out << "                }\n";
        out << "                box.min = Vector3{.x = xform.position.x + (box.min.x * xform.scale.x),\n";
        out << "                                  .y = xform.position.y + (box.min.y * xform.scale.y),\n";
        out << "                                  .z = xform.position.z + (box.min.z * xform.scale.z)};\n";
        out << "                box.max = Vector3{.x = xform.position.x + (box.max.x * xform.scale.x),\n";
        out << "                                  .y = xform.position.y + (box.max.y * xform.scale.y),\n";
        out << "                                  .z = xform.position.z + (box.max.z * xform.scale.z)};\n";
        out << "                const RayCollision hit = GetRayCollisionBox(ray, box);\n";
        out << "                if (hit.hit && (nearest == entt::null || hit.distance < nearest_distance)) {\n";
        out << "                    nearest          = entity;\n";
        out << "                    nearest_distance = hit.distance;\n";
        out << "                }\n";
        out << "            }\n";
        out << "            return nearest;\n";
        out << "        });\n";
    }
    if (uses_editor) {
        // EditorState/Editor are declared unconditionally in std.editor, so this is
        // always available whenever std.editor is imported.
        const std::string es_cpp = EnttCodegenUtils::trait_cpp_name("EditorState", program);
        out << "    cactus::runtime::entt_backend::register_editor_active_mode_impl(\n";
        out << "        [](entt::registry& reg) -> int {\n";
        out << "            auto view = reg.view<" << es_cpp << ">();\n";
        out << "            for (auto entity : view) { return static_cast<int>(view.get<" << es_cpp
            << ">(entity).mode); }\n";
        out << "            return 0;\n";
        out << "        });\n";
        out << "    cactus::runtime::entt_backend::register_editor_is_active_impl(\n";
        out << "        [](entt::registry& reg) -> bool {\n";
        out << "            auto view = reg.view<" << es_cpp << ">();\n";
        out << "            for (auto entity : view) { return view.get<" << es_cpp << ">(entity).active; }\n";
        out << "            return false;\n";
        out << "        });\n";
    }
    // ── Camera rig lifecycle impls ────────────────────────────────────────────
    // Rig dimensionality comes from wt_usage (D2) — std.editor imports both
    // camera and transform modules transitively, so map presence and
    // uses_flat/uses_volume cannot distinguish 2D from 3D programs. When the
    // root references both variants, both rig branches are emitted and the
    // existing camera_enter(use_3d) dispatch selects at runtime.
    const std::string vp_cpp_rig   = uses_editor ? EnttCodegenUtils::trait_cpp_name("Viewport", program) : "";
    const std::string ec2d_cpp_rig = uses_editor ? EnttCodegenUtils::trait_cpp_name("EditorCamera2D", program) : "";
    const std::string ec3d_cpp_rig = uses_editor ? EnttCodegenUtils::trait_cpp_name("EditorCamera3D", program) : "";
    if (uses_editor && uses_viewport) {
        out << "    cactus::runtime::entt_backend::register_editor_camera_enter_impl(\n";
        out << "        [](entt::registry& reg, bool use_3d) -> entt::entity {\n";
        if (!rig_is_2d && !rig_is_3d) {
            out << "            (void)use_3d;\n";
            out << "            return entt::entity{entt::null};\n";
        } else {
            if (rig_is_2d) {
                out << "            if (!use_3d) {\n";
                out << "                auto __cam2d = cactus::runtime::entt_backend::get_active_camera_2d();\n";
                out << "                if (__cam2d.zoom == 0.0F) {\n";
                out << "                    for (const auto& [__e, __vp, __cam] : reg.view<" << vp_cpp_rig << ", "
                    << cam2d_cpp << ">().each()) {\n";
                out << "                        if (__vp.active) {\n";
                out << "                            __cam2d.target = __cam.offset;\n";
                out << "                            __cam2d.zoom = (__cam.zoom == 0.0F) ? 1.0F : __cam.zoom;\n";
                out << "                            break;\n";
                out << "                        }\n";
                out << "                    }\n";
                out << "                }\n";
                out << "                if (__cam2d.zoom == 0.0F) { __cam2d.zoom = 1.0F; }\n";
                out << "                std::vector<entt::entity> __saved;\n";
                out << "                for (auto __ent : reg.view<" << vp_cpp_rig << ">()) {\n";
                out << "                    if (auto* __vp = reg.try_get<" << vp_cpp_rig
                    << ">(__ent); __vp != nullptr && __vp->active) {\n";
                out << "                        __saved.push_back(__ent); __vp->active = false;\n";
                out << "                    }\n";
                out << "                }\n";
                out << "                "
                       "cactus::runtime::entt_backend::set_editor_saved_viewports(std::move(__saved));\n";
                out << "                auto __rig = reg.create();\n";
                // The editor camera rig is created outside the archetype/spawn choke point in
                // emit_archetype_trait_initializers, so it must independently receive a creation
                // ordinal: any pairs: rule binding on a trait this rig also carries (e.g.
                // Viewport) would otherwise match it in emit_pair_binding_snapshot's view and
                // read a CreationOrdinal component that was never emplaced (UB).
                out << "                reg.emplace<cactus::runtime::entt_backend::CreationOrdinal>(__rig, "
                       "cactus::runtime::entt_backend::CreationOrdinal{.value = "
                       "cactus::runtime::entt_backend::generated_next_creation_ordinal()});\n";
                out << "                reg.emplace<" << ec2d_cpp_rig << ">(__rig, " << ec2d_cpp_rig
                    << "{.view_center = __cam2d.target, .zoom = __cam2d.zoom, .pan_speed = 1.0F, .zoom_speed = 0.1F, "
                       ".min_zoom = 0.05F, .max_zoom = 20.0F});\n";
                out << "                reg.emplace<" << cam2d_cpp << ">(__rig, " << cam2d_cpp
                    << "{.zoom = __cam2d.zoom, .offset = __cam2d.target});\n";
                out << "                reg.emplace<" << vp_cpp_rig << ">(__rig);\n";
                out << "                return __rig;\n";
                out << "            }\n";
            }
            if (rig_is_3d) {
                out << "            if (use_3d) {\n";
                out << "                auto __cam3d = cactus::runtime::entt_backend::get_active_camera_3d();\n";
                out << "                if (__cam3d.fovy == 0.0F) {\n";
                out << "                    for (const auto& [__e, __vp, __cam, __wt] : reg.view<" << vp_cpp_rig << ", "
                    << cam3d_cpp << ", " << wt3d_cpp << ">().each()) {\n";
                out << "                        if (__vp.active) {\n";
                out << "                            __cam3d.fovy = __cam.fov_y;\n";
                out << "                            __cam3d.position = __wt.position;\n";
                out << "                            const Vector3 __fwd = "
                       "cactus::runtime::stdlib::math::quat::forward(__wt.rotation);\n";
                out << "                            __cam3d.target = {.x = __wt.position.x + __fwd.x, .y = "
                       "__wt.position.y + __fwd.y, .z = __wt.position.z + __fwd.z};\n";
                out << "                            break;\n";
                out << "                        }\n";
                out << "                    }\n";
                out << "                }\n";
                out << "                if (__cam3d.fovy == 0.0F) {\n";
                out << "                    __cam3d.fovy = 60.0F;\n";
                out << "                    __cam3d.position = Vector3{.x = 0.0F, .y = 5.0F, .z = 10.0F};\n";
                out << "                    __cam3d.target = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};\n";
                out << "                }\n";
                out << "                const Vector3 __pos = __cam3d.position;\n";
                out << "                const Vector3 __tgt = __cam3d.target;\n";
                out << "                const float __dx = __pos.x - __tgt.x;\n";
                out << "                const float __dy = __pos.y - __tgt.y;\n";
                out << "                const float __dz = __pos.z - __tgt.z;\n";
                out << "                const float __dist = cactus::runtime::stdlib::math::vec3::length(Vector3{.x = "
                       "__dx, .y = __dy, .z = __dz});\n";
                out << "                const float __distance = (__dist < 0.1F) ? 0.1F : __dist;\n";
                out << "                const float __pitch = std::asin(std::clamp(__dy / __distance, -1.0F, 1.0F));\n";
                out << "                const float __yaw = std::atan2(-__dx, -__dz);\n";
                out << "                const Quat __rot = cactus::runtime::stdlib::math::quat::from_euler(__pitch, "
                       "__yaw, 0.0F);\n";
                out << "                std::vector<entt::entity> __saved;\n";
                out << "                for (auto __ent : reg.view<" << vp_cpp_rig << ">()) {\n";
                out << "                    if (auto* __vp = reg.try_get<" << vp_cpp_rig
                    << ">(__ent); __vp != nullptr && __vp->active) {\n";
                out << "                        __saved.push_back(__ent); __vp->active = false;\n";
                out << "                    }\n";
                out << "                }\n";
                out << "                "
                       "cactus::runtime::entt_backend::set_editor_saved_viewports(std::move(__saved));\n";
                out << "                auto __rig = reg.create();\n";
                // See the matching comment in the 2D branch above: this rig is created outside
                // the archetype/spawn choke point and must independently receive an ordinal.
                out << "                reg.emplace<cactus::runtime::entt_backend::CreationOrdinal>(__rig, "
                       "cactus::runtime::entt_backend::CreationOrdinal{.value = "
                       "cactus::runtime::entt_backend::generated_next_creation_ordinal()});\n";
                out << "                reg.emplace<" << ec3d_cpp_rig << ">(__rig, " << ec3d_cpp_rig
                    << "{.focus = __tgt, .yaw = __yaw, .pitch = __pitch, .distance = __distance, .orbit_speed = "
                       "0.005F, .pan_speed = 0.002F, .zoom_speed = 0.1F, .min_pitch = -1.5F, .max_pitch = 1.5F, "
                       ".min_distance = 0.1F, .max_distance = 1000.0F});\n";
                out << "                reg.emplace<" << cam3d_cpp << ">(__rig, " << cam3d_cpp
                    << "{.fov_y = __cam3d.fovy, .near = 0.1F, .far = 1000.0F});\n";
                out << "                reg.emplace<" << vp_cpp_rig << ">(__rig);\n";
                out << "                reg.emplace<" << wt3d_cpp << ">(__rig, " << wt3d_cpp
                    << "{.position = __pos, .rotation = __rot, .scale = Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}});\n";
                out << "                return __rig;\n";
                out << "            }\n";
            }
            out << "            return entt::entity{entt::null};\n";
        }
        out << "        });\n";
        out << "    cactus::runtime::entt_backend::register_editor_camera_exit_impl(\n";
        out << "        [](entt::registry& reg, entt::entity rig) {\n";
        out << "            if (reg.valid(rig)) { reg.destroy(rig); }\n";
        out << "            for (auto __saved_ent : cactus::runtime::entt_backend::editor_saved_viewports()) {\n";
        out << "                if (auto* __vp = reg.try_get<" << vp_cpp_rig
            << ">(__saved_ent)) { __vp->active = true; }\n";
        out << "            }\n";
        out << "            cactus::runtime::entt_backend::set_editor_saved_viewports({});\n";
        out << "        });\n";
    }
    if (uses_editor && rig_is_2d && uses_viewport) {
        out << "    cactus::runtime::entt_backend::register_editor_apply_camera_2d_impl(\n";
        out << "        [](entt::registry& reg, entt::entity rig, Vector2 view_center, float zoom) {\n";
        out << "            if (auto* __cam = reg.try_get<" << cam2d_cpp << ">(rig)) {\n";
        out << "                __cam->zoom = zoom;\n";
        out << "                __cam->offset = view_center;\n";
        out << "            }\n";
        out << "        });\n";
    }
    if (wt_usage.flat) {
        out << "    cactus::runtime::entt_backend::register_editor_entity_position_2d_impl(\n";
        out << "        [](entt::registry& reg, entt::entity __eid) -> Vector2 {\n";
        out << "            if (reg.valid(__eid)) {\n";
        out << "                if (const auto* __wt = reg.try_get<" << wt2d_cpp << ">(__eid)) {\n";
        out << "                    return Vector2{.x = __wt->position.x, .y = __wt->position.y};\n";
        out << "                }\n";
        out << "            }\n";
        out << "            return Vector2{.x = 0.0F, .y = 0.0F};\n";
        out << "        });\n";
    }
    if (uses_editor && rig_is_3d && uses_viewport) {
        out << "    cactus::runtime::entt_backend::register_editor_apply_camera_3d_impl(\n";
        out << "        [](entt::registry& reg, entt::entity rig, Vector3 position, Quat rotation) {\n";
        out << "            if (auto* __wt = reg.try_get<" << wt3d_cpp << ">(rig)) {\n";
        out << "                __wt->position = position;\n";
        out << "                __wt->rotation = rotation;\n";
        out << "            }\n";
        out << "        });\n";
    }
    if (rig_is_3d) {
        out << "    cactus::runtime::entt_backend::register_editor_entity_position_3d_impl(\n";
        out << "        [](entt::registry& reg, entt::entity __eid) -> Vector3 {\n";
        out << "            if (reg.valid(__eid)) {\n";
        out << "                if (const auto* __wt = reg.try_get<" << wt3d_cpp << ">(__eid)) {\n";
        out << "                    return __wt->position;\n";
        out << "                }\n";
        out << "            }\n";
        out << "            return Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};\n";
        out << "        });\n";
    }
    // ── Generic pointer candidate providers (std.pointer, design decision #8) ──
    // Registered like the editor hit-test/raycast impls above: real component
    // types are only known here (codegen time), so the shared, program-
    // agnostic pointer_top_target() in runtime.cpp delegates to whichever
    // impls a given program actually needs. Window and world (flat XOR
    // volume, matching rig_is_2d/rig_is_3d's mutual exclusivity) are gated
    // independently since a program may use either, both, or neither.
    if (EnttCodegenUtils::has_trait(program, "std.pointer.PointerTarget")) {
        const std::string ptarget_cpp = EnttCodegenUtils::trait_cpp_name("std.pointer.PointerTarget", program);
        if (EnttCodegenUtils::has_trait(program, "std.ui.ComputedLayout")) {
            const std::string cl_cpp = EnttCodegenUtils::trait_cpp_name("std.ui.ComputedLayout", program);
            out << "    cactus::runtime::entt_backend::register_pointer_window_candidates_impl(\n";
            out << "        [](entt::registry& reg, Vector2 pointer_pos) -> "
                   "std::vector<cactus::runtime::entt_backend::PointerCandidate> {\n";
            out << "            std::vector<cactus::runtime::entt_backend::PointerCandidate> __result;\n";
            out << "            auto view = reg.view<" << cl_cpp << ", " << ptarget_cpp << ">();\n";
            out << "            for (auto entity : view) {\n";
            out << "                const auto& layout = view.get<" << cl_cpp << ">(entity);\n";
            out << "                const auto& target = view.get<" << ptarget_cpp << ">(entity);\n";
            out << "                if (!layout.effective_visible) { continue; }\n";
            out << "                const Vector2 __max = Vector2{.x = layout.position.x + layout.size.x,\n";
            out << "                                              .y = layout.position.y + layout.size.y};\n";
            out << "                if (!cactus::runtime::entt_backend::point_in_rect(pointer_pos, layout.position, "
                   "__max)) { continue; }\n";
            out << "                if (!cactus::runtime::entt_backend::point_in_rect(\n";
            out << "                        pointer_pos, layout.clip_min, layout.clip_max)) { continue; }\n";
            out << "                __result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
            out << "                    .entity = entity, .enabled = target.enabled, .blocks_lower = "
                   "target.blocks_lower,\n";
            out << "                    .priority = target.priority, .draw_order = layout.draw_order});\n";
            out << "            }\n";
            out << "            cactus::runtime::entt_backend::sort_window_pointer_candidates(__result);\n";
            out << "            return __result;\n";
            out << "        });\n";
        }
        const bool has_flat_box      = EnttCodegenUtils::has_trait(program, "std.physics.flat.BoxCollider");
        const bool has_flat_circle   = EnttCodegenUtils::has_trait(program, "std.physics.flat.CircleCollider");
        const bool has_volume_box    = EnttCodegenUtils::has_trait(program, "std.physics.volume.BoxCollider");
        const bool has_volume_sphere = EnttCodegenUtils::has_trait(program, "std.physics.volume.SphereCollider");
        if (wt_usage.flat && (has_flat_box || has_flat_circle)) {
            out << "    cactus::runtime::entt_backend::register_pointer_world_candidates_impl(\n";
            out << "        [](entt::registry& reg, Vector2 pointer_screen_pos) -> "
                   "std::vector<cactus::runtime::entt_backend::PointerCandidate> {\n";
            out << "            std::vector<cactus::runtime::entt_backend::PointerCandidate> __result;\n";
            out << "            const Vector2 __world_pos = "
                   "cactus::runtime::entt_backend::editor_screen_to_world_2d(pointer_screen_pos);\n";
            if (has_flat_box) {
                const std::string box_cpp = EnttCodegenUtils::trait_cpp_name("std.physics.flat.BoxCollider", program);
                out << "            {\n";
                out << "                auto view = reg.view<" << wt2d_cpp << ", " << ptarget_cpp << ", " << box_cpp
                    << ">();\n";
                out << "                for (auto entity : view) {\n";
                out << "                    const auto& xform  = view.get<" << wt2d_cpp << ">(entity);\n";
                out << "                    const auto& target = view.get<" << ptarget_cpp << ">(entity);\n";
                out << "                    const auto& box    = view.get<" << box_cpp << ">(entity);\n";
                out << "                    if (!cactus::runtime::entt_backend::point_in_flat_box(\n";
                out << "                            __world_pos, xform.position, box.size)) { continue; }\n";
                out << "                    __result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
                out << "                        .entity = entity, .enabled = target.enabled,\n";
                out << "                        .blocks_lower = target.blocks_lower, .priority = target.priority,\n";
                out << "                        .creation_ordinal = "
                       "reg.get<cactus::runtime::entt_backend::CreationOrdinal>(entity).value});\n";
                out << "                }\n";
                out << "            }\n";
            }
            if (has_flat_circle) {
                const std::string circle_cpp =
                    EnttCodegenUtils::trait_cpp_name("std.physics.flat.CircleCollider", program);
                out << "            {\n";
                out << "                auto view = reg.view<" << wt2d_cpp << ", " << ptarget_cpp << ", " << circle_cpp
                    << ">();\n";
                out << "                for (auto entity : view) {\n";
                out << "                    const auto& xform  = view.get<" << wt2d_cpp << ">(entity);\n";
                out << "                    const auto& target = view.get<" << ptarget_cpp << ">(entity);\n";
                out << "                    const auto& circle = view.get<" << circle_cpp << ">(entity);\n";
                out << "                    if (!cactus::runtime::entt_backend::point_in_flat_circle(\n";
                out << "                            __world_pos, xform.position, circle.radius)) { continue; }\n";
                out << "                    __result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
                out << "                        .entity = entity, .enabled = target.enabled,\n";
                out << "                        .blocks_lower = target.blocks_lower, .priority = target.priority,\n";
                out << "                        .creation_ordinal = "
                       "reg.get<cactus::runtime::entt_backend::CreationOrdinal>(entity).value});\n";
                out << "                }\n";
                out << "            }\n";
            }
            out << "            cactus::runtime::entt_backend::sort_flat_world_pointer_candidates(__result);\n";
            out << "            return __result;\n";
            out << "        });\n";
        } else if (wt_usage.volume && (has_volume_box || has_volume_sphere)) {
            out << "    cactus::runtime::entt_backend::register_pointer_world_candidates_impl(\n";
            out << "        [](entt::registry& reg, Vector2 pointer_screen_pos) -> "
                   "std::vector<cactus::runtime::entt_backend::PointerCandidate> {\n";
            out << "            std::vector<cactus::runtime::entt_backend::PointerCandidate> __result;\n";
            out << "            const Ray __ray = GetScreenToWorldRay(\n";
            out << "                pointer_screen_pos, cactus::runtime::entt_backend::get_active_camera_3d());\n";
            if (has_volume_box) {
                const std::string box3d_cpp =
                    EnttCodegenUtils::trait_cpp_name("std.physics.volume.BoxCollider", program);
                out << "            {\n";
                out << "                auto view = reg.view<" << wt3d_cpp << ", " << ptarget_cpp << ", " << box3d_cpp
                    << ">();\n";
                out << "                for (auto entity : view) {\n";
                out << "                    const auto& xform  = view.get<" << wt3d_cpp << ">(entity);\n";
                out << "                    const auto& target = view.get<" << ptarget_cpp << ">(entity);\n";
                out << "                    const auto& box    = view.get<" << box3d_cpp << ">(entity);\n";
                out << "                    const BoundingBox __bbox{\n";
                out << "                        .min = Vector3{.x = xform.position.x - (box.size.x * 0.5F),\n";
                out << "                                       .y = xform.position.y - (box.size.y * 0.5F),\n";
                out << "                                       .z = xform.position.z - (box.size.z * 0.5F)},\n";
                out << "                        .max = Vector3{.x = xform.position.x + (box.size.x * 0.5F),\n";
                out << "                                       .y = xform.position.y + (box.size.y * 0.5F),\n";
                out << "                                       .z = xform.position.z + (box.size.z * 0.5F)}};\n";
                out << "                    const RayCollision __hit = GetRayCollisionBox(__ray, __bbox);\n";
                out << "                    if (!__hit.hit || __hit.distance <= 0.0F) { continue; }\n";
                out << "                    __result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
                out << "                        .entity = entity, .enabled = target.enabled,\n";
                out << "                        .blocks_lower = target.blocks_lower, .priority = target.priority,\n";
                out << "                        .distance = __hit.distance});\n";
                out << "                }\n";
                out << "            }\n";
            }
            if (has_volume_sphere) {
                const std::string sphere_cpp =
                    EnttCodegenUtils::trait_cpp_name("std.physics.volume.SphereCollider", program);
                out << "            {\n";
                out << "                auto view = reg.view<" << wt3d_cpp << ", " << ptarget_cpp << ", " << sphere_cpp
                    << ">();\n";
                out << "                for (auto entity : view) {\n";
                out << "                    const auto& xform  = view.get<" << wt3d_cpp << ">(entity);\n";
                out << "                    const auto& target = view.get<" << ptarget_cpp << ">(entity);\n";
                out << "                    const auto& sphere = view.get<" << sphere_cpp << ">(entity);\n";
                out << "                    const RayCollision __hit =\n";
                out << "                        GetRayCollisionSphere(__ray, xform.position, sphere.radius);\n";
                out << "                    if (!__hit.hit || __hit.distance <= 0.0F) { continue; }\n";
                out << "                    __result.push_back(cactus::runtime::entt_backend::PointerCandidate{\n";
                out << "                        .entity = entity, .enabled = target.enabled,\n";
                out << "                        .blocks_lower = target.blocks_lower, .priority = target.priority,\n";
                out << "                        .distance = __hit.distance});\n";
                out << "                }\n";
                out << "            }\n";
            }
            out << "            cactus::runtime::entt_backend::sort_volume_world_pointer_candidates(__result);\n";
            out << "            return __result;\n";
            out << "        });\n";
        }
    }
    out << "}\n\n";

    // Runs after generated_init_project and before the load activation, in
    // generated main() and in hosts alike. With an `on added` trigger it is
    // the startup arrival activation: placed entities get the same `on added`
    // setup as spawned ones. World restore never calls it.
    out << "void generated_load_project(entt::registry& registry) {\n";
    out << "    (void)registry;\n";
    if (tracks_lifecycle && has_added_trigger(program)) {
        out << "    auto& activation = generated_scheduler_state().activation;\n";
        out << "    activation.active = true;\n";
        out << "    generated_lifecycle_trackers().deliver_arrivals(activation, registry);\n";
        out << "    generated_drain_event_cascade(registry);\n";
        out << "    generated_commit_activation(registry);\n";
        out << "    activation.active = false;\n";
    }
    out << "}\n\n";

    out << "void generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt) {\n";

    out << "    (void)registry;\n";
    out << "    (void)dt;\n\n";

    // Editor input override: consumption only lasts one frame, so clear it
    // before any input handler runs (stdlib editor handlers consume first,
    // gameplay handlers later in the same frame observe the consumed state).
    out << "    cactus::runtime::entt_backend::reset_consumed_input();\n\n";

    out << "    dispatcher.update();\n";
    out << "}\n\n";

    // vp_cpp names the Viewport trait; emit_2d_helper/emit_3d_helper (computed
    // once, above) drive which branches of the viewport loop below reference
    // the __translate_camera_2d/3d functions emitted early.
    const std::string vp_cpp = EnttCodegenUtils::trait_cpp_name("Viewport", program);

    out << "void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher) {\n";
    out << "    (void)dispatcher;\n";
    if (!uses_viewport) {
        out << "    (void)registry;\n";
    }
    out << "    cactus::runtime::entt_backend::begin_render_frame();\n";

    if (uses_viewport) {
        out << "    {\n";
        out << "        const int __sw = cactus::runtime::raylib::GetScreenWidth();\n";
        out << "        const int __sh = cactus::runtime::raylib::GetScreenHeight();\n";
        out << "        static std::vector<std::pair<int,entt::entity>> __vps;\n";
        out << "        __vps.clear();\n";
        out << "        for (const auto& [__vp_e, __vp] : registry.view<" << vp_cpp << ">().each()) {\n";
        out << "            if (__vp.active) { __vps.emplace_back(__vp.depth, __vp_e); }\n";
        out << "        }\n";
        out << "        std::ranges::sort(__vps);\n";
        out << "        for (auto& [__depth, __vp_ent] : __vps) {\n";
        out << "            (void)__depth;\n";
        out << "            const auto& __vp = registry.get<" << vp_cpp << ">(__vp_ent);\n";
        out << "            cactus::runtime::raylib::BeginScissorMode(\n";
        out << "                static_cast<int>(__vp.x * static_cast<float>(__sw)),\n";
        out << "                static_cast<int>(__vp.y * static_cast<float>(__sh)),\n";
        out << "                static_cast<int>(__vp.width * static_cast<float>(__sw)),\n";
        out << "                static_cast<int>(__vp.height * static_cast<float>(__sh)));\n";
        out << "            if (__vp.clear) { cactus::runtime::raylib::ClearBackground(__vp.clear_color); }\n";
        // Camera helper selection keys on the viewport entity's resolved camera
        // component type (flat vs volume Camera), not a shared bare Camera token.
        if (emit_2d_helper) {
            out << "            if (registry.all_of<" << cam2d_cpp << ">(__vp_ent)) {\n";
            out << "                const auto& __cam = registry.get<" << cam2d_cpp << ">(__vp_ent);\n";
            out << "                cactus::runtime::entt_backend::set_active_camera_2d(\n";
            out << "                    __translate_camera_2d(__cam, __sw, __sh));\n";
            out << "            }\n";
        }
        if (emit_3d_helper) {
            if (emit_2d_helper) {
                out << "            else if (registry.all_of<" << cam3d_cpp << ">(__vp_ent)) {\n";
            } else {
                out << "            if (registry.all_of<" << cam3d_cpp << ">(__vp_ent)) {\n";
            }
            out << "                const auto& __cam = registry.get<" << cam3d_cpp << ">(__vp_ent);\n";
            out << "                cactus::runtime::entt_backend::set_active_camera_3d(\n";
            out << "                    __translate_camera_3d(__vp_ent, __cam, registry));\n";
            out << "            }\n";
        }
        // Draw this viewport's queued world content now (inside its scissor +
        // camera) so each split-screen region renders from its own camera.
        out << "            cactus::runtime::entt_backend::flush_viewport_3d();\n";
        out << "            cactus::runtime::raylib::EndScissorMode();\n";
        out << "        }\n";
        out << "    }\n";
    }

    out << "    cactus::runtime::entt_backend::end_render_frame();\n";

    // The unconditional HUD overlay splice that used to live here is gone —
    // EditorHUDOverlay (stdlib/std/editor.cactus) draws it now via the standard
    // DSL rule dispatch (editor-declarative-rendering). That relies on custom
    // event dispatch (emit DrawScreenRect), which — like all custom events —
    // only works under the graph-driven main loop; the legacy path here has no
    // custom-event dispatch regardless (a pre-existing gap this change doesn't
    // change the scope of, consistent with task 2.2's scoping of the new
    // debug-draw/screen-ui event dispatch to graph-driven only).

    out << "    clear_projected_traits(registry);\n";
    out << "}\n";
    out << "\n}  // namespace cactus::runtime::entt_backend\n";

    out << emit_backend_main(program);

    out << "\n// NOLINTEND(modernize-use-std-numbers,readability-function-cognitive-complexity,"
           "bugprone-branch-clone,bugprone-reserved-identifier,bugprone-throwing-static-initialization,"
           "cppcoreguidelines-init-variables,cppcoreguidelines-pro-type-member-init,"
           "readability-redundant-member-init,readability-simplify-boolean-expr,"
           "readability-braces-around-statements,readability-isolate-declaration,"
           "readability-math-missing-parentheses,readability-qualified-auto,readability-redundant-parentheses,"
           "performance-move-const-arg,readability-named-parameter,modernize-use-designated-initializers,"
           "readability-use-std-min-max)\n";

    return out.str();
}

}  // namespace cactus
